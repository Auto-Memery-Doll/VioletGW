#ifndef FLOW_GATEWAY_LRU_2_HPP
#define FLOW_GATEWAY_LRU_2_HPP

#include "base/noncopyable.hpp"
#include "base/type.hpp"
#include "base/util.hpp"
#include "base/log.hpp"
#include "cache/protocol.hpp"
#include <algorithm>
#include <bits/types/struct_timeval.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <ostream>
#include <set>
#include <vector>

namespace fg {
namespace cache {

/** LRU2链表的节点 */
struct LRU2Node {
    CacheEle * ele; /** 存储在trie节点中的结构 */
    fg_clock_t first;   /** 第一次访问时间戳 */
    fg_clock_t second;  /** 第二次访问时间戳 */

    friend inline bool operator<(const LRU2Node& node1, const LRU2Node& node2) {
        bool ret = node1.second < node2.second;
        if (ret) {
            return true;
        }
        ret = node1.second == node2.second;
        if (ret) {
            ret = node1.first > node2.first; 
            if (ret) {
                return true;
            }
            ret = node1.first == node2.first; 
            if (ret) {
                return node1.ele->fd < node2.ele->fd;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    // for debug
    friend inline bool operator==(const LRU2Node& node1, const LRU2Node& node2) {
        return  node1.ele == node2.ele &&
                node1.first == node2.first &&
                node1.second == node2.second;
    }

    // for debug
    friend inline std::ostream& operator<<(std::ostream& os, const LRU2Node& node) {
        os <<   "[  " << node.ele->fd << "\n"
            <<  "   " << util::clock_to_str(node.first) << "\n"
            <<  "   " << util::clock_to_str(node.second) << "   ]\n";
        return os;
    }
};


/// adjust  用来调整外部容器的容量
template <int size, int factor, uint32_t max_timeval>
class LRU2 : public base::noncopyable {
public:
    LRU2() = default;
    ~LRU2() = default;

    /** 记录一个文件的访问 */
    /// @param to_close 需要关闭的文件的文件描述符（被动删除）
    void record_access(CacheEle* ele, std::vector<CacheEle*>* to_close) {
        std::unique_lock<std::mutex> lock(_mtx);
    
        if (ele->status == CacheNodeStatus::none) {
            LRU2Node node;
            node.ele = ele;
            node.first = fg_clock_t(fg_duration_t(0));
            node.second = util::now();

            ele->status = CacheNodeStatus::once;
            _candidate.insert(node);
            _map.insert({ele, node});

            return;
        }
    
        auto it = _map.find(ele);
        assert(it != _map.end());
        
        auto &node = it->second;

        if (ele->status == CacheNodeStatus::once) {
            ele->status = CacheNodeStatus::opened;
            
       //     for (auto &_node : _candidate) {
       //         LOG(consule, info);
       //         std::cout << node;
       //         std::cout << _node;
       //         if (_node == node) {
       //             LOG(consule, info) << "find node " << node.ele->fd << "\n";
       //         }
       //     }
            
            //auto it = _candidate.find(node);
            //assert(it != _candidate.end());
            //_candidate.erase(it);
            
            assert(_candidate.erase(node) != 0);
        } else {
        //    for (auto &_node : _candidate) {
        //        LOG(consule, info);
        //        std::cout << node;
        //        std::cout << _node;
        //        if (_node == node) {
        //            LOG(consule, info) << "find node " << node.ele->fd << "\n";
        //        }
        //    }

            assert(_chain.erase(node) != 0);
        }

        node.first = node.second;
        node.second = util::now();
        insert_chain(node, to_close);
        return;
    }

    /** 当一个缓存被删除的时候调用，将CacheEle从堆中删除 */
    // 主动关闭文件
    void evit(CacheEle* ele) {
        if (ele->status == CacheNodeStatus::none) {
            return;
        }

        if (ele->status == CacheNodeStatus::once) {
            std::unique_lock<std::mutex> lock(_mtx);
            auto it = _map.find(ele);
            _candidate.erase(it->second);
            _map.erase(it);
            return;
        }

        std::unique_lock<std::mutex> lock(_mtx);
        auto it = _map.find(ele);
        auto ret = _chain.erase(it->second);
        if (ret == 0) {
            LOG(consule, warnning) << "nothing be removed\n";
        }
        _map.erase(it);
        return;
    }

    // for debug
    inline friend std::ostream& operator<<(std::ostream& os, LRU2<size, factor, max_timeval>& lru) {
        for (auto& node : lru._chain) {
            os << node.ele->fd << " ";
        }
        os << "\n";
        return os;
    }

private:
    // 当从容器中驱逐出一个元素的时候判断是否需要缩容
    void check_need_resize() {
        /** 检查前面的几个节点是否在指定的时间内被访问过 */
        int check_num = _size * _factor;

        uint32_t arvg = 0;
        auto it = _chain.begin();
        for (int i = 0; i > check_num; ++i) {
            arvg += (util::now() - it->second).count();
        }

        if (arvg >= _timeval.count() * 2 * check_num) {
            /** 缩容 */
            resize(_size * (double)(1.0 - _factor));
            return;
        }

        if (arvg <= _timeval.count() * check_num) {
            /** 扩容 */
            resize(_size * (double)(1 + _factor));
            return;
        }
    }

    // 动态调整容器的容量
    void resize(int new_size) {
        LOG(consule, debug) << "resize :" << _size << " -> " << new_size << "\n";
        _size = std::max(new_size, size);
    }

    // 在_chain中插入一个链表
    void insert_chain(LRU2Node& node, std::vector<CacheEle*>* eles) {
        /** 检查是否需要扩容 */
        if (_chain.size() >= _size) {
            check_need_resize();
        }

        if (_chain.size() < _size) {
            _chain.insert(node);
            return;
        }

        /** 缩容 */
        while (_chain.size() >= _size) {
            eles->push_back(_chain.begin()->ele);   // 需要将打开的文件描述符关闭
            _chain.erase(_chain.begin());
        }

        _chain.insert(node);
    }

private:
    std::mutex _mtx;
    /** 已经打开的文件 */
    std::set<LRU2Node> _chain; 
    /** 当_candidate中文件的访问频率达到一定次数，就加入到上面的_chain中 */
    std::set<LRU2Node> _candidate;
    /** 保存CacheEle和LRU2Node之间的映射关系 */
    std::map<CacheEle*, LRU2Node> _map;

    // 扩容因子，new size = old size * _factor
    const double _factor = (double)factor / 100;
    // 当短时间内有大量的热点key，这些key的first time和second time之差
    // 都在_timeval容许的范围内，则扩大容量
    const fg_duration_t _timeval = fg_duration_t(max_timeval);
    // lru-k默认容量
    int _size = size;
};

}   // fg
}   // cache


#endif // !FLOW_GATEWAY_LRU_2_HPP