#include "neighbor.hpp"

namespace vgw {
namespace neighbor {

NeighborTable::NeighborTable() = default;

NeighborTable::NeighborTable(Config cfg) : cfg_(cfg) {}

void NeighborTable::seed(uint32_t ip_be, const rte_ether_addr& mac) {
    NeighborEntry e;
    e.ip_be = ip_be;
    rte_ether_addr_copy(&mac, &e.mac);
    e.is_static = true;
    entries_[ip_be] = e;
}

bool NeighborTable::lookup(uint32_t ip_be, rte_ether_addr* out) const {
    if (out == nullptr) {
        return false;
    }
    const auto it = entries_.find(ip_be);
    if (it == entries_.end()) {
        return false;
    }
    rte_ether_addr_copy(&it->second.mac, out);
    return true;
}

void NeighborTable::learn(uint32_t ip_be,
                          const rte_ether_addr& mac,
                          uint64_t now_ms) {
    auto it = entries_.find(ip_be);
    if (it != entries_.end() && it->second.is_static) {
        return;
    }
    NeighborEntry e;
    e.ip_be = ip_be;
    rte_ether_addr_copy(&mac, &e.mac);
    e.last_seen_ms = now_ms;
    e.is_static = false;
    entries_[ip_be] = e;
}

void NeighborTable::expire(uint64_t now_ms) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.is_static) {
            ++it;
            continue;
        }
        if (now_ms - it->second.last_seen_ms >= cfg_.entry_timeout_ms) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace neighbor
}  // namespace vgw
