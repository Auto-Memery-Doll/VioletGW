#include "base/pkt_filter.hpp"

#include <gtest/gtest.h>
#include <rte_byteorder.h>
#include <rte_ip.h>

TEST(PktFilterTest, ParseMac) {
    auto mac = vgw::util::parse_mac("02:00:00:00:00:03");
    ASSERT_TRUE(mac.has_value());
    EXPECT_EQ(mac->addr_bytes[0], 0x02);
    EXPECT_EQ(mac->addr_bytes[5], 0x03);

    EXPECT_FALSE(vgw::util::parse_mac("bad").has_value());
    EXPECT_FALSE(vgw::util::parse_mac("02:00:00:00:00").has_value());
}

TEST(PktFilterTest, ParseIpv4) {
    auto ip = vgw::util::parse_ipv4_be("192.168.1.100");
    ASSERT_TRUE(ip.has_value());
    EXPECT_EQ(*ip, rte_cpu_to_be_32(RTE_IPV4(192, 168, 1, 100)));

    EXPECT_FALSE(vgw::util::parse_ipv4_be("1.2.3").has_value());
    EXPECT_FALSE(vgw::util::parse_ipv4_be("256.0.0.1").has_value());
}

TEST(PktFilterTest, EmptyFilterMatchesNothingWithoutMbuf) {
    vgw::util::PktFilter f;
    EXPECT_FALSE(vgw::util::pkt_filter_match(nullptr, f));
}
