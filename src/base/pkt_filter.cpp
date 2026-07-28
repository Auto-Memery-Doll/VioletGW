#include "base/pkt_filter.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <rte_ip.h>

namespace vgw {
namespace util {
namespace {

constexpr uint16_t kEthHdrLen = static_cast<uint16_t>(sizeof(rte_ether_hdr));
constexpr uint16_t kMinIpHdrLen = static_cast<uint16_t>(sizeof(rte_ipv4_hdr));

bool mac_eq(const rte_ether_addr& a, const rte_ether_addr& b) {
    return std::memcmp(a.addr_bytes, b.addr_bytes, RTE_ETHER_ADDR_LEN) == 0;
}

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

std::optional<rte_ether_addr> parse_mac(std::string_view s) {
    if (s.empty()) {
        return std::nullopt;
    }
    rte_ether_addr addr{};
    unsigned idx = 0;
    size_t i = 0;
    while (i < s.size() && idx < RTE_ETHER_ADDR_LEN) {
        while (i < s.size() && (s[i] == ':' || s[i] == '-' || s[i] == ' ')) {
            ++i;
        }
        if (i + 1 >= s.size()) {
            return std::nullopt;
        }
        const int hi = hex_nibble(s[i]);
        const int lo = hex_nibble(s[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        addr.addr_bytes[idx++] = static_cast<uint8_t>((hi << 4) | lo);
        i += 2;
    }
    if (idx != RTE_ETHER_ADDR_LEN) {
        return std::nullopt;
    }
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0) {
        ++i;
    }
    if (i != s.size()) {
        return std::nullopt;
    }
    return addr;
}

std::optional<uint32_t> parse_ipv4_be(std::string_view s) {
    if (s.empty() || s.size() > 15) {
        return std::nullopt;
    }
    char buf[16];
    if (s.size() >= sizeof(buf)) {
        return std::nullopt;
    }
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(buf, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return std::nullopt;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return std::nullopt;
    }
    return rte_cpu_to_be_32(RTE_IPV4(a, b, c, d));
}

bool pkt_filter_match(const rte_mbuf* m, const PktFilter& filter) {
    if (m == nullptr) {
        return false;
    }
    const uint16_t data_len = rte_pktmbuf_data_len(m);
    if (data_len < kEthHdrLen) {
        return false;
    }

    const auto* eth = rte_pktmbuf_mtod(m, const rte_ether_hdr*);
    if (filter.src_mac.has_value() &&
        !mac_eq(eth->src_addr, *filter.src_mac)) {
        return false;
    }
    if (filter.dst_mac.has_value() &&
        !mac_eq(eth->dst_addr, *filter.dst_mac)) {
        return false;
    }

    const bool need_ip =
        filter.src_ip_be.has_value() || filter.dst_ip_be.has_value();
    if (!need_ip) {
        return true;
    }

    if (data_len < kEthHdrLen + kMinIpHdrLen) {
        return false;
    }
    if (rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4) {
        return false;
    }

    const auto* ip = reinterpret_cast<const rte_ipv4_hdr*>(
        rte_pktmbuf_mtod_offset(m, const uint8_t*, kEthHdrLen));
    if ((ip->version_ihl >> 4) != 4) {
        return false;
    }
    if (filter.src_ip_be.has_value() && ip->src_addr != *filter.src_ip_be) {
        return false;
    }
    if (filter.dst_ip_be.has_value() && ip->dst_addr != *filter.dst_ip_be) {
        return false;
    }
    return true;
}

}  // namespace util
}  // namespace vgw
