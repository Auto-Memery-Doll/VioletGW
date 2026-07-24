#include "session.hpp"

#include <gtest/gtest.h>
#include <rte_ip.h>

using vgm::session::FlowKey;
using vgm::session::Session;
using vgm::session::SessionTable;

namespace {

FlowKey make_key(uint32_t sip, uint16_t sport, uint32_t dip, uint16_t dport) {
    FlowKey k;
    k.src_ip = sip;
    k.src_port = sport;
    k.dst_ip = dip;
    k.dst_port = dport;
    k.proto = 17;
    return k;
}

}  // namespace

TEST(SessionTableTest, CreateAndLookupForwardReverse) {
    const uint32_t gw = RTE_IPV4(192, 168, 1, 10);
    const uint32_t client = RTE_IPV4(10, 0, 0, 1);
    const uint32_t vip = RTE_IPV4(192, 168, 1, 100);
    const uint32_t upstream = RTE_IPV4(10, 1, 0, 2);

    SessionTable table(gw, /*idle_timeout_ms=*/1000);

    FlowKey fwd = make_key(client, 5000, vip, 80);
    Session* s = table.create(fwd, upstream, 8080, /*now_ms=*/100);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(table.size(), 1u);
    EXPECT_NE(s->snat_port, 0);

    EXPECT_EQ(table.lookup_forward(fwd), s);

    FlowKey rev = make_key(upstream, 8080, gw, s->snat_port);
    EXPECT_EQ(table.lookup_reverse(rev), s);
}

TEST(SessionTableTest, ExpireIdleSessions) {
    const uint32_t gw = RTE_IPV4(192, 168, 1, 10);
    SessionTable table(gw, /*idle_timeout_ms=*/50);

    FlowKey fwd =
        make_key(RTE_IPV4(10, 0, 0, 1), 1111, RTE_IPV4(192, 168, 1, 100), 53);
    Session* s = table.create(fwd, RTE_IPV4(10, 1, 0, 2), 53, /*now_ms=*/0);
    ASSERT_NE(s, nullptr);

    EXPECT_EQ(table.expire(/*now_ms=*/10), 0u);
    EXPECT_EQ(table.size(), 1u);

    EXPECT_EQ(table.expire(/*now_ms=*/60), 1u);
    EXPECT_EQ(table.size(), 0u);
    EXPECT_EQ(table.lookup_forward(fwd), nullptr);
}

TEST(SessionTableTest, TouchExtendsLifetime) {
    const uint32_t gw = RTE_IPV4(192, 168, 1, 10);
    SessionTable table(gw, /*idle_timeout_ms=*/50);

    FlowKey fwd =
        make_key(RTE_IPV4(10, 0, 0, 1), 2222, RTE_IPV4(192, 168, 1, 100), 53);
    Session* s = table.create(fwd, RTE_IPV4(10, 1, 0, 2), 53, /*now_ms=*/0);
    ASSERT_NE(s, nullptr);

    table.touch(s, 40);
    EXPECT_EQ(table.expire(/*now_ms=*/80), 0u);
    EXPECT_EQ(table.size(), 1u);

    EXPECT_EQ(table.expire(/*now_ms=*/100), 1u);
    EXPECT_EQ(table.size(), 0u);
}

TEST(SessionTableTest, SnatPortExhaustion) {
    const uint32_t gw = RTE_IPV4(192, 168, 1, 10);
    // Tiny port range: only one port available.
    SessionTable table(gw, 1000, /*begin=*/20000, /*end=*/20001);

    FlowKey a =
        make_key(RTE_IPV4(10, 0, 0, 1), 1, RTE_IPV4(192, 168, 1, 100), 80);
    FlowKey b =
        make_key(RTE_IPV4(10, 0, 0, 2), 2, RTE_IPV4(192, 168, 1, 100), 80);

    ASSERT_NE(table.create(a, RTE_IPV4(10, 1, 0, 2), 80, 0), nullptr);
    EXPECT_EQ(table.create(b, RTE_IPV4(10, 1, 0, 2), 80, 0), nullptr);
}
