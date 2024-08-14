#include "consistent_hash.hpp"
#include "balance/base_balance.hpp"
#include "base/util.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace fg {
namespace balance {

void ConsistentHash::init(void *arg) {
    InitArg* arg_ = static_cast<InitArg*>(arg);
    _vnode_num = arg_->count;
    _use_fnv = arg_->use_fnv;
}

void ConsistentHash::add(const ip_t& elt) {
    std::unique_lock<std::shared_mutex> lock(_mtx);
    _add(elt);
}

void ConsistentHash::remove(const ip_t& elt) {
    std::unique_lock<std::shared_mutex> lock(_mtx);
    _remove(elt);
}

void ConsistentHash::set(const std::vector<ip_t>& elts) {
    std::unique_lock<std::shared_mutex> lock(_mtx);
    std::vector<ip_t> to_move;
    for (auto& mem : _members) {
        bool found = false;
        for (auto &elt : elts) {
            if (elt == mem.first) {
                found = true;
                break;
            }
        }
        if (!found) {
            to_move.push_back(mem.first);
        }
    }

    // clean
    for (auto& move : to_move) {
        _remove(move);
    }

    for (auto & elt : elts) {
        auto it = _members.find(elt);
        if (it != _members.end()) {
            // already exit
            continue;
        }
        _add(elt);
    }
}

uint32_t ConsistentHash::search(uint32_t key) {
    auto it = std::find(_vnodes.begin(), _vnodes.end(), key);
    if (it != _vnodes.end()) {
        return *it;
    }
    return 0;
}

GetRet ConsistentHash::get(const ip_t& name) {
    std::shared_lock<std::shared_mutex> lock(_mtx);
    if (_circle.size() == 0) {
        return {
            .name = {},
            .e = BalanceError::none,
        };
    }
    uint32_t key = hash_key(name);
    uint32_t hash = search(key);
    return {
        .name = _circle[hash],
        .e = BalanceError::ok,
    };
}

void ConsistentHash::update() {
    std::vector<uint32_t> hashes;

    // 内存优化：判断_vnodes的内存是否过多
    if (_vnodes.size() / (_vnode_num * 4) <= _circle.size()) {
        hashes.reserve(_vnodes.size());
    }

    for (auto &k : _circle) {
        hashes.push_back(k.first);
    }

    std::sort(hashes.begin(), hashes.end());
    _vnodes.swap(hashes);
}

uint32_t ConsistentHash::hash_key(const ip_t& key) {
    if (_use_fnv) {
        return hash_key_fnv(key);
    }
    return hash_key_crc32(key);
}


uint32_t ConsistentHash::hash_key_fnv(const ip_t& key) {
    return util::fnv(key); 
}

uint32_t ConsistentHash::hash_key_crc32(const ip_t& key) {
    return util::crc32(key);
}

void ConsistentHash::_remove(const ip_t& elt) {
    for (int i = 0; i < _vnode_num; ++ i) {
        _circle.erase(hash_key(elt_key(elt, i)));
    }
    _members.erase(elt);
    update();
    _node_num--;
}

void ConsistentHash::_add(const ip_t& elt) {
    for (int i = 0; i < _vnode_num; ++ i) {
        _circle[hash_key(elt_key(elt, i))] = elt;
    }
    _members[elt] = true;
    update();
    _node_num++;
}

std::string ConsistentHash::elt_key(const ip_t& key, int idx) {
    return util::itoa(idx) + key;
}

std::vector<ip_t> ConsistentHash::get_cluster() {
    std::vector<ip_t> cluster_info;
    for (auto &entry : _members) {
        cluster_info.push_back(entry.first);
    }
    return cluster_info;
}

}   // balance
}   // fg