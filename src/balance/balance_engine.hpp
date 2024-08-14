#ifndef FLOW_GATEWAY_BALANCE_ENGINE_HPP
#define FLOW_GATEWAY_BALANCE_ENGINE_HPP 

#include "balance/base_balance.hpp"
#include "balance/heartbeat_monitor.hpp"
#include "base/singleton.hpp"
#include "base/type.hpp"
#include <memory>
#include <ostream>
#include <shared_mutex>
#include <vector>

namespace fg {
namespace balance {

enum class BalanceType {
    consistent_hash,    /** 一致性哈希算法 */
    rr,     /** 轮询算法（根据配置可分为加权轮询和普通轮询 */
    mod,    /** 取模哈希算法 */
    srandom,    /** 单次随机算法 */
    d_random,   /** 双次随机算法 */
    unkonwn,    /** 未初始化 */
};

enum class EngineState {
    uninit, /** 未初始化 */
    working,    /** 正常运行中 */
    switching,  /** 切换负载算法中 */
};

inline std::ostream& operator<<(std::ostream & os, BalanceType type) {
    os << "[";
    switch (type) {
    case BalanceType::consistent_hash:
        os << "consistent hash";
        break;
    case BalanceType::rr:
        os << "route robin";
        break;
    case BalanceType::mod:
        os << "hash mod";
        break;
    case BalanceType::srandom:
        os << "singal random";
        break;
    case BalanceType::d_random:
        os << "double random";
        break;
    }
    os << "]\n";
    return os;
}

class Engine : public base::Singletion<Engine> {
    friend class base::Singletion<Engine>;
public:
    using ptr = std::shared_ptr<Engine>;
    ~Engine();

    struct SwitchArg {
        BalanceType type;
        bool config_is_change;
        std::vector<ip_t>* cluster;
        /** 初始化负载均衡器需要的参数 */
        void *balance_init_arg;
    };

    struct InitArg {
        HeartbeatMonitor* monitor;
        HeartbeatMonitor::InitArg moniter_initarg;
        SwitchArg   switch_arg;
    };

    /** 初始化负载均衡引擎 */
    int init(const InitArg &arg);

    /** 切换负载均衡算法 */
    int switchc_alogrithm(const SwitchArg& arg);

    /** 根据客户端地址获取一个上游服务器的IP地址 */
    // 如果是轮询算法或者随机算法就可能不需要这个ip地址
    ip_t get_upstream_server(const ip_t& client_ip);

    /** 修改集群的配置 */
    void add_node(const ip_t& server_ip);
    void remove_node(const ip_t& server_ip);
    void reset(std::vector<ip_t>* servers);

private:
    Engine();

    /** 后台定时监控上游节点的状态的线程函数 */
    void monitoring();

private:
    /** 模拟队列调度，确保算法切换时的平滑过度 */
    // 当需要切换算法时，获取写锁
    // 其他时候获取读锁
    std::shared_mutex _mtx;

    HeartbeatMonitor::ptr _monitor; /** 进行心跳检测 */
    BalanceType _type;  /** 负载均衡算法 */
    BaseBalance *_balance;  /** 负载均衡器 */
    EngineState _state; /** 负载引擎的状态 */

    fg_thread_t _t; /** 后台上游节点健康状态的监控线程 */
    bool _stop;   /** 是否停止运行 */
};

inline auto engine() -> Engine::ptr {
    return Engine::GetInstance();
}

}   // balance
}   // fg

#endif // !FLOW_GATEWAY_BALANCE_ENGINE_HPP