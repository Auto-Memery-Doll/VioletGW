#include "arp.hpp"
#include "config.hpp"
#include "neighbor.hpp"
#include "packet.hpp"
#include "vgw.h"

#include <cstring>
#include <gtest/gtest.h>
#include <rte_arp.h>
#include <rte_byteorder.h>
#include <rte_ether.h>

using vgw::VioletGW;
using vgw::arp::ArpConfig;
using vgw::arp::ArpHandler;
using vgw::forward::HandleResult;
using vgw::neighbor::NeighborTable;
using vgw::packet::parse_arp;

namespace {

constexpr uint16_t kArpFrameLen =
    static_cast<uint16_t>(sizeof(rte_ether_hdr) + sizeof(rte_arp_hdr));

void bind_mbuf(rte_mbuf* m, uint8_t* buf, uint16_t len) {
    std::memset(m, 0, sizeof(*m));
    m->buf_addr = buf;
    m->buf_iova = reinterpret_cast<rte_iova_t>(buf);
    m->data_off = 0;
    m->data_len = len;
    m->pkt_len = len;
    m->nb_segs = 1;
}

void fill_arp_request(uint8_t* buf,
                      const rte_ether_addr& src_mac,
                      uint32_t src_ip_be,
                      uint32_t target_ip_be) {
    std::memset(buf, 0, kArpFrameLen);
    auto* eth = reinterpret_cast<rte_ether_hdr*>(buf);
    rte_ether_addr_copy(&src_mac, &eth->src_addr);
    eth->dst_addr.addr_bytes[0] = 0xff;
    eth->dst_addr.addr_bytes[1] = 0xff;
    eth->dst_addr.addr_bytes[2] = 0xff;
    eth->dst_addr.addr_bytes[3] = 0xff;
    eth->dst_addr.addr_bytes[4] = 0xff;
    eth->dst_addr.addr_bytes[5] = 0xff;
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP);

    auto* arp = reinterpret_cast<rte_arp_hdr*>(buf + sizeof(rte_ether_hdr));
    arp->arp_hardware = rte_cpu_to_be_16(RTE_ARP_HRD_ETHER);
    arp->arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    arp->arp_hlen = RTE_ETHER_ADDR_LEN;
    arp->arp_plen = 4;
    arp->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REQUEST);
    rte_ether_addr_copy(&src_mac, &arp->arp_data.arp_sha);
    arp->arp_data.arp_sip = src_ip_be;
    arp->arp_data.arp_tip = target_ip_be;
}

void fill_arp_reply(uint8_t* buf,
                    const rte_ether_addr& src_mac,
                    uint32_t src_ip_be,
                    const rte_ether_addr& dst_mac,
                    uint32_t dst_ip_be) {
    std::memset(buf, 0, kArpFrameLen);
    auto* eth = reinterpret_cast<rte_ether_hdr*>(buf);
    rte_ether_addr_copy(&src_mac, &eth->src_addr);
    rte_ether_addr_copy(&dst_mac, &eth->dst_addr);
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP);

    auto* arp = reinterpret_cast<rte_arp_hdr*>(buf + sizeof(rte_ether_hdr));
    arp->arp_hardware = rte_cpu_to_be_16(RTE_ARP_HRD_ETHER);
    arp->arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    arp->arp_hlen = RTE_ETHER_ADDR_LEN;
    arp->arp_plen = 4;
    arp->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REPLY);
    rte_ether_addr_copy(&src_mac, &arp->arp_data.arp_sha);
    arp->arp_data.arp_sip = src_ip_be;
    rte_ether_addr_copy(&dst_mac, &arp->arp_data.arp_tha);
    arp->arp_data.arp_tip = dst_ip_be;
}

}  // namespace

TEST(ArpHandlerTest, ReplyToGatewayRequest) {
    NeighborTable neighbors;
    ArpConfig cfg;
    cfg.gateway_ip_be = VioletGW::ipv4_from(vgw::config::GATEWAY_IP_OCTETS);
    cfg.vip_ip_be = VioletGW::ipv4_from(vgw::config::VIP_IP_OCTETS);
    cfg.gateway_mac = VioletGW::mac_from(vgw::config::GATEWAY_MAC);
    ArpHandler arp(cfg, &neighbors);

    rte_ether_addr requester{};
    requester.addr_bytes[0] = 0xaa;
    requester.addr_bytes[5] = 0xbb;
    const uint32_t requester_ip = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 50));

    alignas(64) uint8_t buf[128];
    fill_arp_request(buf, requester, requester_ip, cfg.gateway_ip_be);
    rte_mbuf m;
    bind_mbuf(&m, buf, kArpFrameLen);

    ASSERT_EQ(arp.handle(&m, 1).result, HandleResult::tx_arp);

    rte_ether_hdr* eth = nullptr;
    rte_arp_hdr* ah = nullptr;
    ASSERT_EQ(parse_arp(&m, &eth, &ah), vgw::packet::ParseStatus::ok);
    EXPECT_EQ(rte_be_to_cpu_16(ah->arp_opcode), RTE_ARP_OP_REPLY);
    EXPECT_EQ(ah->arp_data.arp_sip, cfg.gateway_ip_be);
    EXPECT_EQ(std::memcmp(eth->src_addr.addr_bytes, cfg.gateway_mac.addr_bytes,
                          RTE_ETHER_ADDR_LEN),
              0);
    EXPECT_EQ(std::memcmp(eth->dst_addr.addr_bytes, requester.addr_bytes,
                          RTE_ETHER_ADDR_LEN),
              0);
}

TEST(ArpHandlerTest, ReplyToVipProxyRequest) {
    NeighborTable neighbors;
    ArpConfig cfg;
    cfg.gateway_ip_be = VioletGW::ipv4_from(vgw::config::GATEWAY_IP_OCTETS);
    cfg.vip_ip_be = VioletGW::ipv4_from(vgw::config::VIP_IP_OCTETS);
    cfg.gateway_mac = VioletGW::mac_from(vgw::config::GATEWAY_MAC);
    ArpHandler arp(cfg, &neighbors);

    rte_ether_addr requester{};
    requester.addr_bytes[0] = 0xcc;

    alignas(64) uint8_t buf[128];
    fill_arp_request(buf, requester, rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 2)),
                     cfg.vip_ip_be);
    rte_mbuf m;
    bind_mbuf(&m, buf, kArpFrameLen);

    ASSERT_EQ(arp.handle(&m, 1).result, HandleResult::tx_arp);

    rte_ether_hdr* eth = nullptr;
    rte_arp_hdr* ah = nullptr;
    ASSERT_EQ(parse_arp(&m, &eth, &ah), vgw::packet::ParseStatus::ok);
    EXPECT_EQ(ah->arp_data.arp_sip, cfg.vip_ip_be);
}

TEST(ArpHandlerTest, LearnFromReply) {
    NeighborTable neighbors;
    ArpConfig cfg;
    cfg.gateway_ip_be = VioletGW::ipv4_from(vgw::config::GATEWAY_IP_OCTETS);
    cfg.gateway_mac = VioletGW::mac_from(vgw::config::GATEWAY_MAC);
    ArpHandler arp(cfg, &neighbors);

    rte_ether_addr peer{};
    peer.addr_bytes[0] = 0xdd;
    peer.addr_bytes[5] = 0xee;
    const uint32_t peer_ip = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 99));

    alignas(64) uint8_t buf[128];
    fill_arp_reply(buf, peer, peer_ip, cfg.gateway_mac, cfg.gateway_ip_be);
    rte_mbuf m;
    bind_mbuf(&m, buf, kArpFrameLen);

    EXPECT_EQ(arp.handle(&m, 100).result, HandleResult::drop);

    rte_ether_addr out{};
    ASSERT_TRUE(neighbors.lookup(peer_ip, &out));
    EXPECT_EQ(std::memcmp(out.addr_bytes, peer.addr_bytes, RTE_ETHER_ADDR_LEN),
              0);
}

TEST(ArpHandlerTest, DropUnknownTarget) {
    NeighborTable neighbors;
    ArpConfig cfg;
    cfg.gateway_ip_be = VioletGW::ipv4_from(vgw::config::GATEWAY_IP_OCTETS);
    cfg.vip_ip_be = VioletGW::ipv4_from(vgw::config::VIP_IP_OCTETS);
    cfg.gateway_mac = VioletGW::mac_from(vgw::config::GATEWAY_MAC);
    ArpHandler arp(cfg, &neighbors);

    rte_ether_addr requester{};
    requester.addr_bytes[0] = 0x11;

    alignas(64) uint8_t buf[128];
    fill_arp_request(buf, requester, rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 1)),
                     rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 2)));
    rte_mbuf m;
    bind_mbuf(&m, buf, kArpFrameLen);

    EXPECT_EQ(arp.handle(&m, 1).result, HandleResult::drop);
}
