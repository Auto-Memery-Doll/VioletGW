#ifndef FLOW_GATEWAY_ROUND_ROBIN_HPP
#define FLOW_GATEWAY_ROUND_ROBIN_HPP

#include "base/singleton.hpp"
#include "balance/base_balance.hpp"
#include <memory>
#include <set>
#include <shared_mutex>
#include <vector>
namespace fg {
namespace balance {

class RoundRobin : public BaseBalance, public base::Singletion<RoundRobin> {
    friend class base::Singletion<RoundRobin>;
public:
    using ptr = std::shared_ptr<RoundRobin>;

    struct Mapping {
        ip_t ip;
        int weight;

        friend bool operator<(const Mapping& m1, const Mapping& m2) {
            return m1.ip < m2.ip;
        }
    };

    using NodeAndWeights = std::set<Mapping>;

    RoundRobin() = default;
    ~RoundRobin() = default;

    // 将设置对应的节点的权重
    void init(void *arg) override;
    // 向集群中添加一个节点
    // 其中，权重为默认值1
    void add(const ip_t& elt) override;
    void remove(const ip_t& elt) override;
    // 将重置集群并将集群中所有节点的权重设置为1
    void set(const std::vector<ip_t>& elts) override;
    // 获取通过算法选出一个节点
    GetRet get(const ip_t& name[[maybe_unused]]) override;
    std::vector<ip_t> get_cluster() override;

    // 如果weights.empty() == true 则默认为直接轮询
    struct InitArg {
        std::vector<Mapping> node_weights;
    };

private:
    // 重置集群中节点的次数
    void reset();

private:
    std::shared_mutex _mtx;
    
    int _hint = 0;
    NodeAndWeights _nodes;   /** 节点列表 */
    std::vector<Mapping> _cur_req;   /** 当前周期内节点接收请求的次数 */
};

inline auto switch_to_round_robin() -> RoundRobin::ptr {
    return RoundRobin::GetInstance();
}

}   // balance
}   // fg


#endif // !FLOW_GATEWAY_ROUND_ROBIN_HPP