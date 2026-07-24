#include "upstream.hpp"

#include <gtest/gtest.h>
#include <rte_ip.h>

using vgm::session::FlowKey;
using vgm::upstream::BalancePolicy;
using vgm::upstream::UpstreamEndpoint;
using vgm::upstream::UpstreamTable;

namespace {

FlowKey make_key(uint32_t sip, uint16_t sport) {
    FlowKey k;
    k.src_ip = sip;
    k.src_port = sport;
    k.dst_ip = RTE_IPV4(192, 168, 1, 100);
    k.dst_port = 53;
    k.proto = 17;
    return k;
}

}  // namespace

TEST(UpstreamTableTest, EmptyPickFails) {
    UpstreamTable table;
    UpstreamEndpoint out;
    EXPECT_FALSE(table.pick(make_key(RTE_IPV4(10, 0, 0, 1), 5000), &out));
    EXPECT_EQ(table.size(), 0u);
}

TEST(UpstreamTableTest, SetThenPickReturnsMember) {
    UpstreamTable table;
    table.set({{RTE_IPV4(10, 1, 0, 2), 8080},
               {RTE_IPV4(10, 1, 0, 3), 8080}});

    UpstreamEndpoint out;
    ASSERT_TRUE(table.pick(make_key(RTE_IPV4(10, 0, 0, 1), 5000), &out));
    EXPECT_TRUE((out.ip_be == RTE_IPV4(10, 1, 0, 2) && out.port == 8080) ||
                (out.ip_be == RTE_IPV4(10, 1, 0, 3) && out.port == 8080));
    EXPECT_EQ(table.size(), 2u);
}

TEST(UpstreamTableTest, ModSameKeyStable) {
    UpstreamTable table;
    table.set_policy(BalancePolicy::mod);
    table.set({{RTE_IPV4(10, 1, 0, 2), 8080},
               {RTE_IPV4(10, 1, 0, 3), 8080},
               {RTE_IPV4(10, 1, 0, 4), 8080}});

    const FlowKey key = make_key(RTE_IPV4(10, 0, 0, 7), 12345);
    UpstreamEndpoint a;
    UpstreamEndpoint b;
    ASSERT_TRUE(table.pick(key, &a));
    ASSERT_TRUE(table.pick(key, &b));
    EXPECT_EQ(a.ip_be, b.ip_be);
    EXPECT_EQ(a.port, b.port);
}

TEST(UpstreamTableTest, SetReplaceChangesMembership) {
    UpstreamTable table;
    table.set_policy(BalancePolicy::mod);
    table.set({{RTE_IPV4(10, 1, 0, 2), 8080}});

    UpstreamEndpoint out;
    ASSERT_TRUE(table.pick(make_key(RTE_IPV4(10, 0, 0, 1), 1), &out));
    EXPECT_EQ(out.ip_be, RTE_IPV4(10, 1, 0, 2));

    table.set({{RTE_IPV4(10, 9, 0, 9), 9090}});
    ASSERT_TRUE(table.pick(make_key(RTE_IPV4(10, 0, 0, 1), 1), &out));
    EXPECT_EQ(out.ip_be, RTE_IPV4(10, 9, 0, 9));
    EXPECT_EQ(out.port, 9090);
}

TEST(UpstreamTableTest, PolicyUsesSameNodeList) {
    UpstreamTable table;
    table.set({{RTE_IPV4(10, 1, 0, 2), 8080},
               {RTE_IPV4(10, 1, 0, 3), 8080}});

    table.set_policy(BalancePolicy::mod);
    UpstreamEndpoint mod_out;
    ASSERT_TRUE(table.pick(make_key(RTE_IPV4(10, 0, 0, 1), 5000), &mod_out));

    table.set_policy(BalancePolicy::rr);
    UpstreamEndpoint rr_out;
    ASSERT_TRUE(table.pick(make_key(RTE_IPV4(10, 0, 0, 1), 5000), &rr_out));

    // Both picks must land on an endpoint from the same published list.
    auto is_member = [](const UpstreamEndpoint& e) {
        return (e.ip_be == RTE_IPV4(10, 1, 0, 2) ||
                e.ip_be == RTE_IPV4(10, 1, 0, 3)) &&
               e.port == 8080;
    };
    EXPECT_TRUE(is_member(mod_out));
    EXPECT_TRUE(is_member(rr_out));
}
