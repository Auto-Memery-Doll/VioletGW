#include "mod.hpp"
#include "balance/base_balance.hpp"
#include "base/util.hpp"
#include "base/log.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace fg {
namespace balance {

uint64_t Mod::get_index(const ip_t& ip) {
    static std::hash<ip_t> hasher;
    size_t hash_value = hasher(ip);
    return hash_value % _count;
}

void Mod::init(void *arg) {
    InitArg *init_arg = static_cast<InitArg*>(arg);

    std::unique_lock<std::shared_mutex> lock(_mtx);
    _count = init_arg->nodes.size();
    _nodes.swap(init_arg->nodes);

}

void Mod::add(const ip_t& elt) {
    std::unique_lock<std::shared_mutex> lock(_mtx);
    _count++;
    _nodes.push_back(elt);
}

void Mod::remove(const ip_t& elt) {
    int index = 0;
    std::unique_lock<std::shared_mutex> lock(_mtx);
    for (; index < _nodes.size(); ++ index) {
        if (_nodes[index] == elt) {
            break;
        }
    }
    if (index == _nodes.size()) {
        LOG(consule, warnning) << "node " << elt 
                << " is not in the closure\n";
        return;
    }

    --_count;
    _nodes.erase(_nodes.begin() + index);
}

void Mod::set(const std::vector<ip_t>& elts) {
    std::unique_lock<std::shared_mutex> lock(_mtx);
    _nodes = elts;
    _count = _nodes.size();
}

GetRet Mod::get(const ip_t& name) {
    int index = get_index(name);
    std::shared_lock<std::shared_mutex> lock(_mtx);
    return {_nodes[index], BalanceError::ok};
}

std::vector<ip_t> Mod::get_cluster() {
    return _nodes;
}

}   // balance
}   // fg