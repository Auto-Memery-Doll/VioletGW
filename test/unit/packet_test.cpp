#include "packet.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

using vgw::packet::ParseStatus;
using vgw::packet::PacketView;
using vgw::packet::parse_udp_ipv4;
using vgw::packet::refresh_ipv4_checksum;
using vgw::packet::refresh_udp_checksum;

namespace {

void fill_minimal_udp_frame(uint8_t* buf, size_t cap) {
    const size_t need =
        sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr) + 4;
    if (cap < need) {
        return;
    }

    std::memset(buf, 0, cap);
    auto* eth = reinterpret_cast<rte_ether_hdr*>(buf);
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    auto* ip = reinterpret_cast<rte_ipv4_hdr*>(buf + sizeof(rte_ether_hdr));
    ip->version_ihl = 0x45;
    const uint16_t ip_len =
        static_cast<uint16_t>(sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr) + 4);
    ip->total_length = rte_cpu_to_be_16(ip_len);
    ip->time_to_live = 64;
    ip->next_proto_id = IPPROTO_UDP;
    ip->src_addr = RTE_IPV4(10, 0, 0, 1);
    ip->dst_addr = RTE_IPV4(10, 0, 0, 2);
    refresh_ipv4_checksum(ip);

    auto* udp = reinterpret_cast<rte_udp_hdr*>(
        buf + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr));
    udp->src_port = rte_cpu_to_be_16(12345);
    udp->dst_port = rte_cpu_to_be_16(53);
    udp->dgram_len = rte_cpu_to_be_16(sizeof(rte_udp_hdr) + 4);
    std::memcpy(reinterpret_cast<uint8_t*>(udp) + sizeof(rte_udp_hdr), "dns!",
                4);
    refresh_udp_checksum(ip, udp);
}

void bind_mbuf(rte_mbuf* m, uint8_t* buf, uint16_t len) {
    std::memset(m, 0, sizeof(*m));
    m->buf_addr = buf;
    m->buf_iova = reinterpret_cast<rte_iova_t>(buf);
    m->data_off = 0;
    m->data_len = len;
    m->pkt_len = len;
    m->nb_segs = 1;
    m->next = nullptr;
}

}  // namespace

TEST(PacketChecksumTest, Ipv4ChecksumStable) {
    rte_ipv4_hdr ip{};
    ip.version_ihl = 0x45;
    ip.total_length = rte_cpu_to_be_16(20);
    ip.time_to_live = 64;
    ip.next_proto_id = IPPROTO_UDP;
    ip.src_addr = RTE_IPV4(192, 168, 1, 1);
    ip.dst_addr = RTE_IPV4(192, 168, 1, 2);
    refresh_ipv4_checksum(&ip);
    const uint16_t c1 = ip.hdr_checksum;
    refresh_ipv4_checksum(&ip);
    EXPECT_EQ(c1, ip.hdr_checksum);
    EXPECT_NE(c1, 0);
}

TEST(PacketParseTest, ParsesUdpIpv4) {
    uint8_t frame[128];
    const uint16_t frame_len = static_cast<uint16_t>(
        sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr) + 4);
    fill_minimal_udp_frame(frame, sizeof(frame));

    rte_mbuf m;
    bind_mbuf(&m, frame, frame_len);

    PacketView view;
    ASSERT_EQ(parse_udp_ipv4(&m, &view, true), ParseStatus::ok);
    EXPECT_EQ(view.src_port(), 12345);
    EXPECT_EQ(view.dst_port(), 53);
    EXPECT_EQ(view.payload_len, 4);
    EXPECT_EQ(std::memcmp(view.payload, "dns!", 4), 0);
}

TEST(PacketParseTest, RejectsNonIpv4) {
    uint8_t frame[128];
    const uint16_t frame_len = static_cast<uint16_t>(
        sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr) + 4);
    fill_minimal_udp_frame(frame, sizeof(frame));

    auto* eth = reinterpret_cast<rte_ether_hdr*>(frame);
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV6);

    rte_mbuf m;
    bind_mbuf(&m, frame, frame_len);

    PacketView view;
    EXPECT_EQ(parse_udp_ipv4(&m, &view, false), ParseStatus::not_ipv4);
}
