#ifndef FLOW_GATEWAY_CONSISTENT_HASH_HPP
#define FLOW_GATEWAY_CONSISTENT_HASH_HPP

#include "balance/base_balance.hpp"
#include <cstdint>
#include <map>
#include <shared_mutex>
#include <string>
#include <vector>
namespace fg {
namespace balance {

class ConsistentHash : public BaseBalance {
public:
    ConsistentHash() = default;
    ~ConsistentHash() = default;

    void init(void *arg) override;
    void add(const ip_t& elt) override;
    void remove(const ip_t& elt) override;
    void set(const std::vector<ip_t>& elts) override;
    GetRet get(const ip_t& name) override;

    /** 初始化参数结构 */
    struct InitArg {
        int64_t count;  // 实际节点对应的虚拟节点的数量
        bool use_fnv;   // 是否使用fnv算法，如果为false，则使用crc32算法
    };

private:
    /** 更新vnode列表 */
    void update();
    
    uint32_t hash_key(const ip_t& key);
    uint32_t hash_key_fnv(const ip_t& key);
    uint32_t hash_key_crc32(const ip_t& key);

    void _remove(const ip_t& elt);
    void _add(const ip_t& elt);

    std::string elt_key(const ip_t& key, int idx);
    uint32_t search(uint32_t key);

private:
    // key:哈希值
    // value:节点的名字
    // 存储虚拟节点和实际节点的映射关系
    std::map<uint32_t, ip_t> _circle;

    // 标记哪些节点时集群的成员，通常情况下为true
    std::map<ip_t, bool> _members;

    // 存储按哈希值排序的虚拟节点的列表
    // 用于查找最近的节点
    std::vector<uint32_t> _vnodes;

    // 每个实际节点对应的虚拟节点的数量
    int64_t _vnode_num;

    // 是否使用FNV哈希算法
    bool _use_fnv;

    // 当前集群中物理节点的数量
    int64_t _node_num;

    std::shared_mutex _mtx;
};

}   // balance
}   // fg



#endif // !FLOW_GATEWAY_CONSISTENT_HASH_HPP