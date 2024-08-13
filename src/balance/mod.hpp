#ifndef FLOW_GATEWAY_MOD_HPP
#define FLOW_GATEWAY_MOD_HPP

#include "balance/base_balance.hpp"
#include <cstdint>
#include <shared_mutex>
#include <vector>
namespace fg {
namespace balance {

class Mod : public BaseBalance {
public:
    Mod() = default;
    ~Mod() = default;

    void init(void *arg) override;
    void add(const ip_t& elt) override;
    void remove(const ip_t& elt) override;
    void set(const std::vector<ip_t>& elts) override;
    GetRet get(const ip_t& name) override;

    struct InitArg {
        std::vector<ip_t> nodes;
    };

private:
    void reset();
    uint64_t get_index();

private:
    std::shared_mutex _mtx;
    int _count; /** 当前集群中节点的数量 */
    std::vector<ip_t> _nodes;
    uint64_t _mod;
};

}   // balance
}   // fg

#endif // !FLOW_GATEWAY_MOD_HPP