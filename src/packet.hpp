#pragma once

#include "neighbor.hpp"

#include <cstdint>
#include <rte_arp.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>

namespace vgw {
namespace packet {

enum class ParseStatus : uint8_t {
    ok = 0,
    too_short,
    not_ipv4,
    not_udp,
    fragmented,
    bad_ip_checksum,
    bad_udp_checksum,
};

/** Non-owning view of an Ethernet/IPv4/UDP packet in a single mbuf segment. */
struct PacketView {
    rte_mbuf* mbuf = nullptr;
    rte_ether_hdr* eth = nullptr;
    rte_ipv4_hdr* ip = nullptr;
    rte_udp_hdr* udp = nullptr;
    uint8_t* payload = nullptr;
    uint16_t payload_len = 0;

    uint32_t src_ip() const { return ip ? ip->src_addr : 0; }
    uint32_t dst_ip() const { return ip ? ip->dst_addr : 0; }
    uint16_t src_port() const { return udp ? rte_be_to_cpu_16(udp->src_port) : 0; }
    uint16_t dst_port() const { return udp ? rte_be_to_cpu_16(udp->dst_port) : 0; }
};

/**
 * Parse Ethernet + IPv4 + UDP from mbuf.
 * Requires contiguous headers in the first segment (typical for UDP).
 * @param verify_cksum  if true, validate IPv4 and UDP checksums when UDP cksum != 0
 */
ParseStatus parse_udp_ipv4(rte_mbuf* m, PacketView* out, bool verify_cksum = false);

/** Return Ethernet type in host byte order, or 0 if mbuf too short. */
uint16_t ethertype(rte_mbuf* m);

/** Parse Ethernet + IPv4 ARP. Requires hardware=Ethernet, protocol=IPv4. */
ParseStatus parse_arp(rte_mbuf* m,
                      rte_ether_hdr** eth_out,
                      rte_arp_hdr** arp_out);

/** Learn IPv4 src → eth.src into neighbor table (best-effort). */
void passive_learn_ipv4(rte_mbuf* m,
                        neighbor::NeighborTable* neighbors,
                        uint64_t now_ms);

/** Recompute and write IPv4 header checksum. */
void refresh_ipv4_checksum(rte_ipv4_hdr* ip);

/**
 * Recompute and write UDP checksum (IPv4 pseudo-header).
 * Passes payload length from the UDP length field.
 */
void refresh_udp_checksum(const rte_ipv4_hdr* ip, rte_udp_hdr* udp);

/** Set UDP checksum to 0 (optional for IPv4). Faster when rewrite path allows it. */
inline void clear_udp_checksum(rte_udp_hdr* udp) { udp->dgram_cksum = 0; }

}  // namespace packet
}  // namespace vgw

