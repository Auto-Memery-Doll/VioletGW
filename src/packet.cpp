#include "packet.hpp"

#include <cstring>
#include <netinet/in.h>

namespace vgw {
namespace packet {

namespace {

constexpr uint16_t kEthHdrLen = static_cast<uint16_t>(sizeof(rte_ether_hdr));
constexpr uint16_t kMinIpHdrLen = static_cast<uint16_t>(sizeof(rte_ipv4_hdr));
constexpr uint16_t kUdpHdrLen = static_cast<uint16_t>(sizeof(rte_udp_hdr));
constexpr uint16_t kArpHdrLen = static_cast<uint16_t>(sizeof(rte_arp_hdr));

bool headers_fit(const rte_mbuf* m, uint16_t need) {
    return m != nullptr && rte_pktmbuf_data_len(m) >= need;
}

bool udp_checksum_ok(const rte_ipv4_hdr* ip, rte_udp_hdr* udp) {
    if (udp->dgram_cksum == 0) {
        return true;  // optional for IPv4
    }
    const uint16_t saved = udp->dgram_cksum;
    udp->dgram_cksum = 0;
    const uint16_t calc = rte_ipv4_udptcp_cksum(ip, udp);
    udp->dgram_cksum = saved;
    if (saved == calc) {
        return true;
    }
    // One's complement: 0 and 0xffff are equivalent.
    return (saved == 0xffff && calc == 0) || (saved == 0 && calc == 0xffff);
}

}  // namespace

uint16_t ethertype(rte_mbuf* m) {
    if (!headers_fit(m, kEthHdrLen)) {
        return 0;
    }
    const auto* eth = rte_pktmbuf_mtod(m, const rte_ether_hdr*);
    return rte_be_to_cpu_16(eth->ether_type);
}

ParseStatus parse_arp(rte_mbuf* m,
                      rte_ether_hdr** eth_out,
                      rte_arp_hdr** arp_out) {
    if (eth_out != nullptr) {
        *eth_out = nullptr;
    }
    if (arp_out != nullptr) {
        *arp_out = nullptr;
    }
    if (m == nullptr || eth_out == nullptr || arp_out == nullptr) {
        return ParseStatus::too_short;
    }
    if (!headers_fit(m, kEthHdrLen + kArpHdrLen)) {
        return ParseStatus::too_short;
    }

    auto* eth = rte_pktmbuf_mtod(m, rte_ether_hdr*);
    if (rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_ARP) {
        return ParseStatus::not_ipv4;
    }

    auto* arp = reinterpret_cast<rte_arp_hdr*>(
        rte_pktmbuf_mtod_offset(m, uint8_t*, kEthHdrLen));
    if (rte_be_to_cpu_16(arp->arp_hardware) != RTE_ARP_HRD_ETHER ||
        rte_be_to_cpu_16(arp->arp_protocol) != RTE_ETHER_TYPE_IPV4 ||
        arp->arp_hlen != RTE_ETHER_ADDR_LEN || arp->arp_plen != 4) {
        return ParseStatus::too_short;
    }

    *eth_out = eth;
    *arp_out = arp;
    return ParseStatus::ok;
}

void passive_learn_ipv4(rte_mbuf* m,
                        neighbor::NeighborTable* neighbors,
                        uint64_t now_ms) {
    if (m == nullptr || neighbors == nullptr) {
        return;
    }
    if (!headers_fit(m, kEthHdrLen + kMinIpHdrLen)) {
        return;
    }

    const auto* eth = rte_pktmbuf_mtod(m, const rte_ether_hdr*);
    if (rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4) {
        return;
    }

    const auto* ip = reinterpret_cast<const rte_ipv4_hdr*>(
        rte_pktmbuf_mtod_offset(m, const uint8_t*, kEthHdrLen));
    const uint32_t src_ip_be = ip->src_addr;
    if (src_ip_be == 0) {
        return;
    }
    const uint8_t first_octet = (rte_be_to_cpu_32(src_ip_be) >> 24) & 0xff;
    if (first_octet >= 224) {
        return;
    }

    neighbors->learn(src_ip_be, eth->src_addr, now_ms);
}

ParseStatus parse_udp_ipv4(rte_mbuf* m, PacketView* out, bool verify_cksum) {
    if (out == nullptr || m == nullptr) {
        return ParseStatus::too_short;
    }
    std::memset(out, 0, sizeof(*out));
    out->mbuf = m;

    if (!headers_fit(m, kEthHdrLen + kMinIpHdrLen + kUdpHdrLen)) {
        return ParseStatus::too_short;
    }

    auto* eth = rte_pktmbuf_mtod(m, rte_ether_hdr*);
    if (rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4) {
        return ParseStatus::not_ipv4;
    }

    auto* ip = reinterpret_cast<rte_ipv4_hdr*>(
        rte_pktmbuf_mtod_offset(m, uint8_t*, kEthHdrLen));
    const uint16_t ihl = static_cast<uint16_t>((ip->version_ihl & 0x0f) * 4);
    if ((ip->version_ihl >> 4) != 4 || ihl < kMinIpHdrLen) {
        return ParseStatus::not_ipv4;
    }
    if (!headers_fit(m, kEthHdrLen + ihl + kUdpHdrLen)) {
        return ParseStatus::too_short;
    }

    const uint16_t frag = rte_be_to_cpu_16(ip->fragment_offset);
    if ((frag & RTE_IPV4_HDR_MF_FLAG) != 0 ||
        (frag & RTE_IPV4_HDR_OFFSET_MASK) != 0) {
        return ParseStatus::fragmented;
    }

    if (ip->next_proto_id != IPPROTO_UDP) {
        return ParseStatus::not_udp;
    }

    auto* udp = reinterpret_cast<rte_udp_hdr*>(
        rte_pktmbuf_mtod_offset(m, uint8_t*, kEthHdrLen + ihl));
    const uint16_t udp_len = rte_be_to_cpu_16(udp->dgram_len);
    if (udp_len < kUdpHdrLen) {
        return ParseStatus::too_short;
    }
    if (!headers_fit(m, kEthHdrLen + ihl + udp_len)) {
        return ParseStatus::too_short;
    }

    if (verify_cksum) {
        const uint16_t saved_ip = ip->hdr_checksum;
        ip->hdr_checksum = 0;
        const uint16_t calc_ip = rte_ipv4_cksum(ip);
        ip->hdr_checksum = saved_ip;
        if (saved_ip != calc_ip) {
            return ParseStatus::bad_ip_checksum;
        }
        if (!udp_checksum_ok(ip, udp)) {
            return ParseStatus::bad_udp_checksum;
        }
    }

    out->eth = eth;
    out->ip = ip;
    out->udp = udp;
    out->payload = reinterpret_cast<uint8_t*>(udp) + kUdpHdrLen;
    out->payload_len = static_cast<uint16_t>(udp_len - kUdpHdrLen);
    return ParseStatus::ok;
}

void refresh_ipv4_checksum(rte_ipv4_hdr* ip) {
    ip->hdr_checksum = 0;
    ip->hdr_checksum = rte_ipv4_cksum(ip);
}

void refresh_udp_checksum(const rte_ipv4_hdr* ip, rte_udp_hdr* udp) {
    udp->dgram_cksum = 0;
    udp->dgram_cksum = rte_ipv4_udptcp_cksum(ip, udp);
    if (udp->dgram_cksum == 0) {
        udp->dgram_cksum = 0xffff;
    }
}

}  // namespace packet
}  // namespace vgw
