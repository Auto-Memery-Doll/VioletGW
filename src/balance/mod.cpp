#include "mod.hpp"
#include "balance/base_balance.hpp"
#include "base/util.hpp"
#include "base/log.hpp"
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace fg {
namespace balance {

uint64_t Mod::get_index() {
    return ++_mod % _count;
}

void Mod::reset() {
    _mod = 0;
}

void Mod::init(void *arg) {
    InitArg *init_arg = static_cast<InitArg*>(arg);

    std::unique_lock<std::shared_mutex> lock(_mtx);
    _count = init_arg->nodes.size();
    _nodes.swap(init_arg->nodes);
    _mod = 0;
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
    _mod = 0;
}

GetRet Mod::get(const ip_t& name) {
    std::shared_lock<std::shared_mutex> lock(_mtx);
    return {_nodes[get_index()], BalanceError::ok};
}


}   // balance
}   // fg