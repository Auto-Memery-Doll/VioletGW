#pragma once

#include "neighbor.hpp"
#include "packet.hpp"
#include "session.hpp"

#include <cstdint>
#include <functional>
#include <rte_ether.h>
#include <rte_mbuf.h>

namespace vgw {
namespace forward {

struct Upstream {
    uint32_t ip_be = 0;
    uint16_t port = 0;
};

/** Select an upstream for a new client→VIP flow. */
using UpstreamPicker =
    std::function<bool(const session::FlowKey& client_to_vip, Upstream* out)>;

struct VipEndpoint {
    uint32_t ip_be = 0;
    uint16_t port = 0;
};

struct ForwardConfig {
    VipEndpoint vip;
    uint32_t gateway_ip_be = 0;
    rte_ether_addr gateway_mac{};
    rte_ether_addr upstream_mac{};  // L2 next hop toward upstreams (simplified)
    rte_ether_addr client_side_mac{};  // L2 next hop toward clients (simplified)
    bool verify_checksum = false;
    bool recompute_udp_checksum = false;  // false → set UDP cksum to 0
};

enum class HandleResult : uint8_t {
    drop = 0,
    tx_forward,  // rewritten toward upstream
    tx_reverse,  // rewritten toward client
    tx_arp,      // ARP reply or other L2 control
    pending_arp, // L4 accepted; hold until neighbor MAC is resolved
};

/** Result of one datapath handle() call. */
struct HandleOutcome {
    HandleResult result = HandleResult::drop;
    /** Target IPv4 (network order) when result == pending_arp. */
    uint32_t arp_wait_ip_be = 0;
    /** Populated when an ARP reply installed a neighbor entry. */
    uint32_t learned_ip_be = 0;
};

session::FlowKey flow_key_from_view(const packet::PacketView& v);

/** DNAT+SNAT client→VIP into gw→upstream. */
void apply_forward_nat(packet::PacketView* v,
                       const session::Session& s,
                       uint32_t gateway_ip_be,
                       bool recompute_udp_checksum);

/** Restore upstream→gw into VIP/gw→client. */
void apply_reverse_nat(packet::PacketView* v,
                       const session::Session& s,
                       bool recompute_udp_checksum);

void set_l2(packet::PacketView* v,
            const rte_ether_addr& src,
            const rte_ether_addr& dst);

/**
 * UDP L4 forward pipeline on a single mbuf.
 * Ownership of mbuf stays with caller; on drop caller should free.
 */
class Forwarder {
public:
    Forwarder(ForwardConfig cfg,
              session::SessionTable* sessions,
              UpstreamPicker picker,
              neighbor::NeighborTable* neighbors = nullptr);

    HandleOutcome handle(rte_mbuf* m, uint64_t now_ms);

    session::SessionTable* sessions() { return sessions_; }
    const ForwardConfig& config() const { return cfg_; }
    void set_gateway_mac(const rte_ether_addr& mac) { cfg_.gateway_mac = mac; }

    /** Apply gateway/src + resolved next-hop MAC to an already-NAT'd mbuf. */
    bool finish_l2(rte_mbuf* m, uint32_t next_hop_ip_be) const;

private:
    HandleOutcome handle_forward(packet::PacketView* v, uint64_t now_ms);
    HandleOutcome handle_reverse(packet::PacketView* v, uint64_t now_ms);

    ForwardConfig cfg_;
    session::SessionTable* sessions_;
    UpstreamPicker picker_;
    neighbor::NeighborTable* neighbors_;
};

}  // namespace forward
}  // namespace vgw

