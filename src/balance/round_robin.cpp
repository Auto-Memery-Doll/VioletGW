#include "round_robin.hpp"
#include "balance/base_balance.hpp"
#include "base/log.hpp"
#include "base/util.hpp"
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace fg {
namespace balance {

void RoundRobin::init(void *arg) {
    InitArg *init_arg = static_cast<InitArg*>(arg);

    std::unique_lock<std::shared_mutex> lock(_mtx);

    for (auto &nw : init_arg->node_weights) {
        auto it = _nodes.find(nw);
        if (it == _nodes.end()) {
            LOG(consule, warnning) << "node " << nw.ip
                << " is not in closue.\n";
            continue;
        }

        _nodes.erase(it);
        _nodes.insert(nw);
    }

    /** 重置集群的负载情况 */
    reset();
}

void RoundRobin::add(const ip_t& elt) {
    std::unique_lock<std::shared_mutex> lock(_mtx);

    Mapping mapping;
    mapping.ip = elt;
    mapping.weight = 1;

    _nodes.insert(mapping);
    _cur_req.push_back(mapping);
}

void RoundRobin::remove(const ip_t& elt) {
    std::unique_lock<std::shared_mutex> lock(_mtx);

    auto it = _nodes.find({elt});
    if (it == _nodes.end()) {
        LOG(consule, warnning) << "node " << elt
            << " is not in closure";
        return; 
    }

    _nodes.erase(it);
    int i = 0;
    for (; i < _cur_req.size(); ++i) {
        if (_cur_req[i].ip == elt) {
            break;
        }
    }
    _cur_req.erase(_cur_req.begin() + i);
}

void RoundRobin::reset() {
    std::unique_lock<std::shared_mutex> lock(_mtx);
    _cur_req.clear();
    _hint = 0;
    for (auto &node : _nodes) {
        _cur_req.push_back(node);
    }
}

void RoundRobin::set(const std::vector<ip_t>& elts) {
    std::vector<ip_t> to_remove;

    std::shared_lock<std::shared_mutex> lock(_mtx);
    for (auto &node : _nodes) {
        bool found = false;
        for (auto &elt : elts) {
            if (node.ip == elt) {
                found = true;
                break;
            }
        }
        if (!found) {
            to_remove.push_back(node.ip);
        }
    }

    for (auto &ip : to_remove) {
        _nodes.erase({ip});
    }

    for (auto &elt : elts) {
        _nodes.insert({elt, 1});
    }
}

GetRet RoundRobin::get(const ip_t& name) {
    int begin = _hint;

    std::shared_lock<std::shared_mutex> lock(_mtx);
    for (; _hint < _cur_req.size(); ) {
        if (++_hint == _cur_req.size()) {
            _hint %= _nodes.size();
        }
        
        if (_hint == begin) {
            lock.unlock();
            reset();
            lock.lock();
        }

        auto it = _cur_req.begin() + _hint;
        if (it->weight > 0) {
            --it->weight;
            return {it->ip, BalanceError::ok};
        }
    }
    return {};
}

}   // balance
}   // fg