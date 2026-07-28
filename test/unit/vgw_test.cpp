#include "config.hpp"
#include "packet.hpp"
#include "vgw.h"

#include <cstring>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <rte_arp.h>
#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
#include <string>
#include <unistd.h>

using vgw::VioletGW;
using vgw::forward::HandleOutcome;
using vgw::forward::HandleResult;
using vgw::packet::PacketView;
using vgw::packet::ParseStatus;
using vgw::packet::parse_udp_ipv4;
using vgw::packet::refresh_ipv4_checksum;
using vgw::packet::refresh_udp_checksum;

namespace {

void fill_udp(uint8_t* buf,
              size_t cap,
              uint32_t sip,
              uint16_t sport,
              uint32_t dip,
              uint16_t dport) {
    const size_t need =
        sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr) + 4;
    ASSERT_TRUE(cap >= need);
    std::memset(buf, 0, cap);

    auto* eth = reinterpret_cast<rte_ether_hdr*>(buf);
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    auto* ip = reinterpret_cast<rte_ipv4_hdr*>(buf + sizeof(rte_ether_hdr));
    ip->version_ihl = 0x45;
    ip->total_length = rte_cpu_to_be_16(
        static_cast<uint16_t>(sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr) + 4));
    ip->time_to_live = 64;
    ip->next_proto_id = IPPROTO_UDP;
    ip->src_addr = sip;
    ip->dst_addr = dip;
    refresh_ipv4_checksum(ip);

    auto* udp = reinterpret_cast<rte_udp_hdr*>(
        buf + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr));
    udp->src_port = rte_cpu_to_be_16(sport);
    udp->dst_port = rte_cpu_to_be_16(dport);
    udp->dgram_len = rte_cpu_to_be_16(sizeof(rte_udp_hdr) + 4);
    std::memcpy(reinterpret_cast<uint8_t*>(udp) + sizeof(rte_udp_hdr), "ping",
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
}

uint16_t frame_len() {
    return static_cast<uint16_t>(sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) +
                                 sizeof(rte_udp_hdr) + 4);
}

class VioletGWPipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        shm_ = std::string("/vgw_unit_") + std::to_string(getpid()) + "_" +
               std::to_string(reinterpret_cast<uintptr_t>(this));
        ASSERT_EQ(gw_.init_services(shm_.c_str()), 0);
    }

    void TearDown() override {
        vgw::control::CpShm::unlink_name(shm_);
    }

    VioletGW gw_;
    std::string shm_;
};

}  // namespace

TEST_F(VioletGWPipelineTest, ForwardAndReverseViaGatewayHandle) {
    const uint32_t client = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 1));
    const uint32_t vip = VioletGW::ipv4_from(vgw::config::VIP_IP_OCTETS);
    const uint32_t gw_ip = VioletGW::ipv4_from(vgw::config::GATEWAY_IP_OCTETS);
    const uint32_t upstream =
        VioletGW::ipv4_from(vgw::config::UPSTREAM0_IP_OCTETS);

    alignas(64) uint8_t buf[256];
    rte_mbuf m;
    const uint16_t len = frame_len();
    fill_udp(buf, sizeof(buf), client, 4000, vip, vgw::config::VIP_PORT);
    bind_mbuf(&m, buf, len);

    ASSERT_EQ(gw_.handle(&m, 1).result, HandleResult::tx_forward);

    PacketView view;
    ASSERT_EQ(parse_udp_ipv4(&m, &view, false), ParseStatus::ok);
    EXPECT_EQ(view.src_ip(), gw_ip);
    EXPECT_EQ(view.dst_ip(), upstream);
    EXPECT_EQ(view.dst_port(), vgw::config::UPSTREAM0_PORT);
    const uint16_t snat = view.src_port();
    ASSERT_NE(snat, 0);

    fill_udp(buf, sizeof(buf), upstream, vgw::config::UPSTREAM0_PORT, gw_ip,
             snat);
    bind_mbuf(&m, buf, len);

    ASSERT_EQ(gw_.handle(&m, 2).result, HandleResult::tx_reverse);
    ASSERT_EQ(parse_udp_ipv4(&m, &view, false), ParseStatus::ok);
    EXPECT_EQ(view.src_ip(), vip);
    EXPECT_EQ(view.dst_ip(), client);
    EXPECT_EQ(view.src_port(), vgw::config::VIP_PORT);
    EXPECT_EQ(view.dst_port(), 4000);
}

TEST_F(VioletGWPipelineTest, PollControlIdempotent) {
    EXPECT_FALSE(gw_.poll_control());
    EXPECT_GE(gw_.upstreams()->size(), 1u);
}

TEST_F(VioletGWPipelineTest, ArpRequestForGateway) {
    const uint32_t gw_ip = VioletGW::ipv4_from(vgw::config::GATEWAY_IP_OCTETS);

    rte_ether_addr requester{};
    requester.addr_bytes[0] = 0xaa;

    alignas(64) uint8_t buf[128];
    const uint16_t len = static_cast<uint16_t>(sizeof(rte_ether_hdr) +
                                               sizeof(rte_arp_hdr));
    std::memset(buf, 0, len);
    auto* eth = reinterpret_cast<rte_ether_hdr*>(buf);
    rte_ether_addr_copy(&requester, &eth->src_addr);
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP);
    auto* arp = reinterpret_cast<rte_arp_hdr*>(buf + sizeof(rte_ether_hdr));
    arp->arp_hardware = rte_cpu_to_be_16(RTE_ARP_HRD_ETHER);
    arp->arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    arp->arp_hlen = RTE_ETHER_ADDR_LEN;
    arp->arp_plen = 4;
    arp->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REQUEST);
    rte_ether_addr_copy(&requester, &arp->arp_data.arp_sha);
    arp->arp_data.arp_sip = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 50));
    arp->arp_data.arp_tip = gw_ip;

    rte_mbuf m;
    bind_mbuf(&m, buf, len);
    ASSERT_EQ(gw_.handle(&m, 1).result, HandleResult::tx_arp);

    rte_ether_hdr* reth = nullptr;
    rte_arp_hdr* rarp = nullptr;
    ASSERT_EQ(vgw::packet::parse_arp(&m, &reth, &rarp),
              vgw::packet::ParseStatus::ok);
    EXPECT_EQ(rte_be_to_cpu_16(rarp->arp_opcode), RTE_ARP_OP_REPLY);
    EXPECT_EQ(rarp->arp_data.arp_sip, gw_ip);
}
