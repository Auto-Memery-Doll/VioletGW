#pragma once

#include <cstdint>
#include <optional>
#include <rte_ether.h>
#include <rte_mbuf.h>
#include <string_view>

namespace vgw {
namespace util {

/** Optional L2/L3 match criteria. Unset fields are ignored (wildcard). */
struct PktFilter {
    std::optional<rte_ether_addr> src_mac;
    std::optional<rte_ether_addr> dst_mac;
    /** IPv4 addresses in network byte order (same as rte_ipv4_hdr). */
    std::optional<uint32_t> src_ip_be;
    std::optional<uint32_t> dst_ip_be;
};

/** Parse "aa:bb:cc:dd:ee:ff" (also accepts '-'). Empty / fail → nullopt. */
std::optional<rte_ether_addr> parse_mac(std::string_view s);

/** Parse "a.b.c.d" to network-order IPv4. Empty / fail → nullopt. */
std::optional<uint32_t> parse_ipv4_be(std::string_view s);

/**
 * True if mbuf matches all set fields in filter.
 * Non-IPv4 frames fail IP criteria; missing/short frames never match.
 * Empty filter (all nullopt) matches any well-formed Ethernet frame.
 */
bool pkt_filter_match(const rte_mbuf* m, const PktFilter& filter);

}  // namespace util
}  // namespace vgw
