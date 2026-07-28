#pragma once

#include "forward.hpp"
#include "neighbor.hpp"

#include <cstdint>
#include <rte_ether.h>
#include <rte_mbuf.h>

namespace vgw {
namespace arp {

struct ArpConfig {
    uint32_t gateway_ip_be = 0;
    uint32_t vip_ip_be = 0;
    rte_ether_addr gateway_mac{};
};

struct ArpOutcome {
    forward::HandleResult result = forward::HandleResult::drop;
    /** Network-order IPv4 learned from an ARP reply. */
    uint32_t learned_ip_be = 0;
};

/**
 * Handle inbound ARP requests/replies and build outbound ARP probes.
 * Replies to ARP requests targeting gateway or VIP (proxy ARP).
 */
class ArpHandler {
public:
    ArpHandler(ArpConfig cfg, neighbor::NeighborTable* neighbors);

    ArpOutcome handle(rte_mbuf* m, uint64_t now_ms);

    /**
     * Build an ARP request mbuf (caller must free or send).
     * Returns nullptr if mbuf allocation fails.
     */
    rte_mbuf* make_request(uint32_t target_ip_be) const;

    void set_gateway_mac(const rte_ether_addr& mac) { cfg_.gateway_mac = mac; }

private:
    forward::HandleResult handle_request(rte_mbuf* m,
                                           rte_ether_hdr* eth,
                                           rte_arp_hdr* arp);
    forward::HandleResult handle_reply(rte_mbuf* m,
                                       rte_arp_hdr* arp,
                                       uint64_t now_ms,
                                       uint32_t* learned_ip_be = nullptr);
    forward::HandleResult make_reply(rte_mbuf* req,
                                     rte_ether_hdr* eth,
                                     rte_arp_hdr* arp,
                                     uint32_t reply_ip_be);
    bool is_proxy_target(uint32_t ip_be) const;

    ArpConfig cfg_;
    neighbor::NeighborTable* neighbors_;
};

}  // namespace arp
}  // namespace vgw
