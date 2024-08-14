#ifndef FLOW_GATEWAY_LRU_2_HPP
#define FLOW_GATEWAY_LRU_2_HPP

#include "base/noncopyable.hpp"
#include "base/type.hpp"
#include <cstddef>
#include <mutex>
#include <set>

namespace fg {
namespace cache {

/** LRU2链表的节点 */
struct LRU2Node {
    int fd;     /** 文件描述符 */
    fg_clock_t first;   /** 第一次访问时间戳 */
    fg_clock_t second;  /** 第二次访问时间戳 */

    friend inline bool operator<(const LRU2Node& node1, const LRU2Node& node2) {
        if (node1.second < node2.second ||
            (node1.second - node1.first) < (node2.second - node2.first)) {
            return true;
        }
        return false;
    }
};


/// adjust  用来调整外部容器的容量
template <void(*adjust)(size_t)>
class LRU2 : public base::noncopyable {

public:

    void record_access(int fd);

private:

    


private:
    std::mutex _mtx;

    std::set<LRU2Node> _chain;  /** 时间序列，每次都驱逐最前面的节点 */


};

}   // fg
}   // cache


#endif // !FLOW_GATEWAY_LRU_2_HPP