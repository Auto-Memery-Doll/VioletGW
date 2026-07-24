#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace vgm {
namespace session {

/** UDP/IPv4 5-tuple. IPs are network-byte-order; ports are host-byte-order. */
struct FlowKey {
    uint32_t src_ip = 0;
    uint32_t dst_ip = 0;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    uint8_t proto = 17;  // IPPROTO_UDP

    bool operator==(const FlowKey& o) const {
        return src_ip == o.src_ip && dst_ip == o.dst_ip && src_port == o.src_port &&
               dst_port == o.dst_port && proto == o.proto;
    }
};

struct FlowKeyHash {
    size_t operator()(const FlowKey& k) const noexcept {
        size_t h = k.src_ip;
        h ^= static_cast<size_t>(k.dst_ip) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<size_t>(k.src_port) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<size_t>(k.dst_port) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= static_cast<size_t>(k.proto) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct Session {
    FlowKey forward_key;  // client -> VIP
    FlowKey reverse_key;  // upstream -> gateway (SNAT)

    uint32_t client_ip = 0;
    uint16_t client_port = 0;
    uint32_t vip_ip = 0;
    uint16_t vip_port = 0;
    uint32_t upstream_ip = 0;
    uint16_t upstream_port = 0;
    uint16_t snat_port = 0;

    uint64_t last_active_ms = 0;
};

/**
 * Bidirectional UDP session table for L4 DNAT/SNAT.
 * Forward map: client->VIP key → session
 * Reverse map: upstream->gw key → session
 */
class SessionTable {
public:
    SessionTable(uint32_t gateway_ip_be,
                 uint64_t idle_timeout_ms = 60'000,
                 uint16_t snat_port_begin = 10000,
                 uint16_t snat_port_end = 60000);

    Session* lookup_forward(const FlowKey& key);
    Session* lookup_reverse(const FlowKey& key);

    /**
     * Create a new session for client->VIP flow.
     * @return nullptr if SNAT ports are exhausted
     */
    Session* create(const FlowKey& client_to_vip,
                    uint32_t upstream_ip_be,
                    uint16_t upstream_port,
                    uint64_t now_ms);

    /** Touch last_active_ms for an existing session. */
    void touch(Session* s, uint64_t now_ms);

    /** Drop idle sessions; returns number removed. */
    size_t expire(uint64_t now_ms);

    size_t size() const { return sessions_.size(); }
    uint32_t gateway_ip() const { return gateway_ip_; }

private:
    uint16_t alloc_snat_port();
    void free_snat_port(uint16_t port);
    void erase_session(Session* s);

    uint32_t gateway_ip_;
    uint64_t idle_timeout_ms_;
    uint16_t snat_begin_;
    uint16_t snat_end_;
    uint16_t snat_next_;

    std::vector<Session*> sessions_;
    std::unordered_map<FlowKey, Session*, FlowKeyHash> forward_;
    std::unordered_map<FlowKey, Session*, FlowKeyHash> reverse_;
    std::vector<uint16_t> free_ports_;
    std::vector<uint8_t> port_in_use_;  // indexed by port
};

}  // namespace session
}  // namespace vgm

