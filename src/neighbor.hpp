#pragma once

#include <cstdint>
#include <rte_ether.h>
#include <unordered_map>

namespace vgw {
namespace neighbor {

struct NeighborEntry {
    uint32_t ip_be = 0;
    rte_ether_addr mac{};
    uint64_t last_seen_ms = 0;
    bool is_static = false;
};

/** IPv4 → MAC cache populated by ARP replies and static seeds. */
class NeighborTable {
public:
    struct Config {
        uint64_t entry_timeout_ms = 300'000;
    };

    NeighborTable();
    explicit NeighborTable(Config cfg);

    /** Insert or replace a static entry (never expired). */
    void seed(uint32_t ip_be, const rte_ether_addr& mac);

    /** Lookup MAC for ip_be. Returns false on miss. */
    bool lookup(uint32_t ip_be, rte_ether_addr* out) const;

    /** Learn or refresh a dynamic entry from an ARP reply. */
    void learn(uint32_t ip_be, const rte_ether_addr& mac, uint64_t now_ms);

    void expire(uint64_t now_ms);

    size_t size() const { return entries_.size(); }

private:
    Config cfg_;
    std::unordered_map<uint32_t, NeighborEntry> entries_;
};

}  // namespace neighbor
}  // namespace vgw
