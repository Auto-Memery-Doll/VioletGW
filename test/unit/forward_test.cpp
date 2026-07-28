#include "config.hpp"
#include "forward.hpp"
#include "neighbor.hpp"
#include "packet.hpp"
#include "session.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

using vgw::forward::ForwardConfig;
using vgw::forward::Forwarder;
using vgw::forward::HandleOutcome;
using vgw::forward::HandleResult;
using vgw::forward::Upstream;
using vgw::forward::apply_forward_nat;
using vgw::forward::apply_reverse_nat;
using vgw::forward::flow_key_from_view;
using vgw::packet::PacketView;
using vgw::packet::parse_udp_ipv4;
using vgw::packet::refresh_ipv4_checksum;
using vgw::packet::refresh_udp_checksum;
using vgw::session::Session;
using vgw::session::SessionTable;

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

}  // namespace

TEST(ForwardNatTest, ForwardAndReverseRewrite) {
    const uint32_t client = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 1));
    const uint32_t vip = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 100));
    const uint32_t gw = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 10));
    const uint32_t upstream = rte_cpu_to_be_32(RTE_IPV4(10, 1, 0, 2));

    SessionTable table(gw);
    auto* s = table.create(
        vgw::session::FlowKey{client, vip, 5000, 53, 17}, upstream, 53, 0);
    ASSERT_NE(s, nullptr);

    uint8_t frame[128];
    fill_udp(frame, sizeof(frame), client, 5000, vip, 53);
    rte_mbuf m;
    bind_mbuf(&m, frame, frame_len());

    PacketView view;
    ASSERT_EQ(parse_udp_ipv4(&m, &view, false), vgw::packet::ParseStatus::ok);
    apply_forward_nat(&view, *s, gw, false);

    EXPECT_EQ(view.src_ip(), gw);
    EXPECT_EQ(view.dst_ip(), upstream);
    EXPECT_EQ(view.src_port(), s->snat_port);
    EXPECT_EQ(view.dst_port(), 53);

    // Simulate reverse packet: upstream -> gw:snat
    fill_udp(frame, sizeof(frame), upstream, 53, gw, s->snat_port);
    bind_mbuf(&m, frame, frame_len());
    ASSERT_EQ(parse_udp_ipv4(&m, &view, false), vgw::packet::ParseStatus::ok);
    apply_reverse_nat(&view, *s, false);

    EXPECT_EQ(view.src_ip(), vip);
    EXPECT_EQ(view.dst_ip(), client);
    EXPECT_EQ(view.src_port(), 53);
    EXPECT_EQ(view.dst_port(), 5000);
}

TEST(ForwarderTest, CreatesSessionAndForwardsVipTraffic) {
    const uint32_t client = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 1));
    const uint32_t vip = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 100));
    const uint32_t gw = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 10));
    const uint32_t upstream = rte_cpu_to_be_32(RTE_IPV4(10, 1, 0, 2));

    SessionTable table(gw);
    ForwardConfig cfg;
    cfg.vip = {vip, 53};
    cfg.gateway_ip_be = gw;

    Forwarder fwd(cfg, &table, [&](const vgw::session::FlowKey&, Upstream* up) {
        up->ip_be = upstream;
        up->port = 53;
        return true;
    });

    uint8_t frame[128];
    fill_udp(frame, sizeof(frame), client, 4000, vip, 53);
    rte_mbuf m;
    bind_mbuf(&m, frame, frame_len());

    EXPECT_EQ(fwd.handle(&m, 1).result, HandleResult::tx_forward);
    EXPECT_EQ(table.size(), 1u);

    PacketView view;
    ASSERT_EQ(parse_udp_ipv4(&m, &view, false), vgw::packet::ParseStatus::ok);
    EXPECT_EQ(view.dst_ip(), upstream);
    EXPECT_EQ(view.src_ip(), gw);
}

TEST(ForwarderTest, DropsUnknownNonVip) {
    SessionTable table(rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 10)));
    ForwardConfig cfg;
    cfg.vip = {rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 100)), 53};
    cfg.gateway_ip_be = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 10));

    Forwarder fwd(cfg, &table, [](const vgw::session::FlowKey&, Upstream*) {
        return false;
    });

    uint8_t frame[128];
    fill_udp(frame, sizeof(frame), rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 1)), 1,
             rte_cpu_to_be_32(RTE_IPV4(8, 8, 8, 8)), 53);
    rte_mbuf m;
    bind_mbuf(&m, frame, frame_len());
    EXPECT_EQ(fwd.handle(&m, 1).result, HandleResult::drop);
}

TEST(ForwarderTest, PendingArpWhenNeighborMiss) {
    const uint32_t client = rte_cpu_to_be_32(RTE_IPV4(10, 0, 0, 1));
    const uint32_t vip = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 100));
    const uint32_t gw = rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 10));
    const uint32_t upstream = rte_cpu_to_be_32(RTE_IPV4(10, 1, 0, 2));

    vgw::neighbor::NeighborTable neighbors;
    SessionTable table(gw);
    ForwardConfig cfg;
    cfg.vip = {vip, 53};
    cfg.gateway_ip_be = gw;

    Forwarder fwd(cfg, &table,
                  [&](const vgw::session::FlowKey&, Upstream* up) {
                      up->ip_be = upstream;
                      up->port = 53;
                      return true;
                  },
                  &neighbors);

    uint8_t frame[128];
    fill_udp(frame, sizeof(frame), client, 4000, vip, 53);
    rte_mbuf m;
    bind_mbuf(&m, frame, frame_len());

    const HandleOutcome o = fwd.handle(&m, 1);
    EXPECT_EQ(o.result, HandleResult::pending_arp);
    EXPECT_EQ(o.arp_wait_ip_be, upstream);
}
