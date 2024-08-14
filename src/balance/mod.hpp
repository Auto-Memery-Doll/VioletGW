#ifndef FLOW_GATEWAY_MOD_HPP
#define FLOW_GATEWAY_MOD_HPP

#include "base/singleton.hpp"
#include "balance/base_balance.hpp"
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <vector>
namespace fg {
namespace balance {

class Mod : public BaseBalance, public base::Singletion<Mod> {
    friend class base::Singletion<Mod>;
public:
    using ptr = std::shared_ptr<Mod>;
    Mod() = default;
    ~Mod() = default;

    void init(void *arg) override;
    void add(const ip_t& elt) override;
    void remove(const ip_t& elt) override;
    void set(const std::vector<ip_t>& elts) override;
    GetRet get(const ip_t& name) override;
    std::vector<ip_t> get_cluster() override;

    struct InitArg {
        std::vector<ip_t> nodes;
    };

private:
    uint64_t get_index(const ip_t& ip);

private:
    std::shared_mutex _mtx;
    int _count; /** 当前集群中节点的数量 */
    std::vector<ip_t> _nodes;
};

inline auto switch_to_mod() -> Mod::ptr {
    return Mod::GetInstance();
}

}   // balance
}   // fg

#endif // !FLOW_GATEWAY_MOD_HPP