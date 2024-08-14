#include "balance_engine.hpp"
#include "balance/base_balance.hpp"
#include "balance/consistent_hash.hpp"
#include "balance/mod.hpp"
#include "balance/round_robin.hpp"
#include "base/log.hpp"
#include "base/util.hpp"
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace fg {
namespace balance {

int Engine::init(const InitArg& arg) {
    if (_state != EngineState::uninit) {
        LOG(consule, warnning) << "engine is already init\n";
        return 0;
    }

    if (arg.monitor == nullptr) {
        LOG(consule, error) << "monitor must be not nullptr\n";
        return -1;
    }

    if (arg.switch_arg.config_is_change == false) {
        LOG(consule, error) << "Engine::InitARg.Engine::SwitchArg.config_is_change"
            << " must be true.\n";
        return -1;
    }

    if (-1 == switchc_alogrithm(arg.switch_arg)) {
        LOG(consule, error) << "switchc_alogrithm failure.\n";
        return -1;
    }

    _monitor->init(arg.moniter_initarg);

    /** 启动监控线程 */
    _stop = false;
    _t = std::thread([this](){
        this->monitoring();
    });
    return 0;
}

int Engine::switchc_alogrithm(const SwitchArg& arg) {
    if (_state == EngineState::switching) {
        LOG(consule, warnning) << "engine is busy[swtiching]\n";
        return -1;
    }

    std::unique_lock<std::shared_mutex> lock(_mtx);
    /** 获取原来集群的信息 */
    std::vector<ip_t> origin_cluster = _balance->get_cluster();

    arg.type;
    switch (_type) {
    case BalanceType::consistent_hash:
        _balance = switch_to_consistent_hash().get();
        break;
    case BalanceType::rr:
        _balance = switch_to_round_robin().get();
        break;
    case BalanceType::mod:
        _balance = switch_to_mod().get();
        break;
    case BalanceType::srandom:
    case BalanceType::d_random:
        LOG(consule, warnning) << "balance alorithm " << arg.type
            << "is not implement!\n";
        return -1;
    }

    _type = arg.type;
    _balance->init(arg.balance_init_arg);
    if (arg.config_is_change) {
        _balance->set(*arg.cluster);
    } else {
        _balance->set(origin_cluster);
    }
    
    return 0;
}

ip_t Engine::get_upstream_server(const ip_t& client_ip) {
    std::shared_lock<std::shared_mutex> lock(_mtx);

    GetRet ret = _balance->get(client_ip);
    if (ret.e != BalanceError::ok) {
        LOG(consule, error) << ret.e << "\n";
        return "";
    }
    return ret.name;
}

void Engine::add_node(const ip_t& server_ip) {
    std::shared_lock<std::shared_mutex> lock(_mtx);
    _balance->add(server_ip);
}

void Engine::remove_node(const ip_t& server_ip) {
    std::shared_lock<std::shared_mutex> lock(_mtx);
    _balance->remove(server_ip);
}

void Engine::reset(std::vector<ip_t>* servers) {
    std::shared_lock<std::shared_mutex> lock(_mtx);
    _balance->set(*servers);
}

Engine::Engine() 
    :   _state(EngineState::uninit)
    ,   _type(BalanceType::unkonwn)
    ,   _balance(NULL)
    ,   _monitor(nullptr)
    ,   _stop(true)
{}

void Engine::monitoring() {

    while (!_stop) {
        _monitor->check_nodes();
    }

}

}   // balance
}   // fg