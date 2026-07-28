#pragma once

#include <cstddef>
#include <cstdint>
#include <rte_mbuf.h>
#include <string>

namespace vgw {
namespace util {

/**
 * Human-readable dump of an Ethernet/IPv4/UDP mbuf (first segment).
 * Includes L2/L3/L4 fields plus payload as ASCII ('.' for non-printable)
 * and a hex preview. Non-UDP / truncated packets return an error summary.
 *
 * @param max_payload  max payload bytes to show (0 = headers only)
 */
std::string dump_udp_mbuf(const rte_mbuf* m, size_t max_payload = 64);

}  // namespace util
}  // namespace vgw
