#pragma once

#include "config.hpp"
#include "packet.hpp"

#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>

namespace vgm {
namespace bench {

constexpr uint32_t kClient = RTE_IPV4(10, 0, 0, 1);
constexpr uint32_t kVip = RTE_IPV4(192, 168, 1, 100);
constexpr uint32_t kGw = RTE_IPV4(192, 168, 1, 10);
constexpr uint32_t kUpstream = RTE_IPV4(10, 1, 0, 2);
constexpr uint16_t kVipPort = 53;
constexpr uint16_t kClientPortBase = 4000;
constexpr uint16_t kUpPort = 53;

inline uint16_t frame_len(uint16_t payload_len = 4) {
    return static_cast<uint16_t>(sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) +
                                 sizeof(rte_udp_hdr) + payload_len);
}

inline unsigned required_mbuf_buf_size(unsigned payload_len) {
    const unsigned need = RTE_PKTMBUF_HEADROOM + frame_len(
                              static_cast<uint16_t>(payload_len));
    unsigned size = vgm::config::DPDK_mempool_block_size;
    if (need <= size) {
        return size;
    }
    size = need;
    size = (size + 255u) & ~255u;
    return size;
}

inline void fill_udp(uint8_t* buf,
                     uint32_t sip,
                     uint16_t sport,
                     uint32_t dip,
                     uint16_t dport,
                     uint16_t payload_len = 4) {
    const uint16_t len = frame_len(payload_len);
    std::memset(buf, 0, len);
    auto* eth = reinterpret_cast<rte_ether_hdr*>(buf);
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    auto* ip = reinterpret_cast<rte_ipv4_hdr*>(buf + sizeof(rte_ether_hdr));
    ip->version_ihl = 0x45;
    ip->total_length = rte_cpu_to_be_16(static_cast<uint16_t>(
        sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr) + payload_len));
    ip->time_to_live = 64;
    ip->next_proto_id = IPPROTO_UDP;
    ip->src_addr = sip;
    ip->dst_addr = dip;
    packet::refresh_ipv4_checksum(ip);

    auto* udp = reinterpret_cast<rte_udp_hdr*>(
        buf + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr));
    udp->src_port = rte_cpu_to_be_16(sport);
    udp->dst_port = rte_cpu_to_be_16(dport);
    udp->dgram_len = rte_cpu_to_be_16(sizeof(rte_udp_hdr) + payload_len);
    if (payload_len > 0) {
        std::memset(reinterpret_cast<uint8_t*>(udp) + sizeof(rte_udp_hdr), 'x',
                    payload_len);
    }
    packet::clear_udp_checksum(udp);
}

inline void fill_forward_mbuf(rte_mbuf* m,
                              uint16_t client_port,
                              uint16_t payload_len = 4) {
    const uint16_t len = frame_len(payload_len);
    uint8_t* data = rte_pktmbuf_mtod(m, uint8_t*);
    fill_udp(data, kClient, client_port, kVip, kVipPort, payload_len);
    m->data_len = len;
    m->pkt_len = len;
}

inline void fill_reverse_mbuf(rte_mbuf* m,
                              uint16_t snat_port,
                              uint16_t payload_len = 4) {
    const uint16_t len = frame_len(payload_len);
    uint8_t* data = rte_pktmbuf_mtod(m, uint8_t*);
    fill_udp(data, kUpstream, kUpPort, kGw, snat_port, payload_len);
    m->data_len = len;
    m->pkt_len = len;
}

}  // namespace bench
}  // namespace vgm
