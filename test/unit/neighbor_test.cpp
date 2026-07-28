#include "neighbor.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <rte_ether.h>

using vgw::neighbor::NeighborTable;

namespace {

rte_ether_addr make_mac(uint8_t b0) {
    rte_ether_addr mac{};
    for (unsigned i = 0; i < RTE_ETHER_ADDR_LEN; ++i) {
        mac.addr_bytes[i] = static_cast<uint8_t>(b0 + i);
    }
    return mac;
}

}  // namespace

TEST(NeighborTableTest, SeedAndLookup) {
    NeighborTable table;
    const uint32_t ip = 0x0a000001;
    const rte_ether_addr mac = make_mac(0x10);

    table.seed(ip, mac);
    rte_ether_addr out{};
    ASSERT_TRUE(table.lookup(ip, &out));
    EXPECT_EQ(std::memcmp(out.addr_bytes, mac.addr_bytes, RTE_ETHER_ADDR_LEN),
              0);
}

TEST(NeighborTableTest, LearnAndExpire) {
    NeighborTable::Config cfg;
    cfg.entry_timeout_ms = 1000;
    NeighborTable table(cfg);

    const uint32_t ip = 0x0a000002;
    const rte_ether_addr mac = make_mac(0x20);
    table.learn(ip, mac, 100);

    rte_ether_addr out{};
    ASSERT_TRUE(table.lookup(ip, &out));

    table.expire(1099);
    ASSERT_TRUE(table.lookup(ip, &out));

    table.expire(1101);
    EXPECT_FALSE(table.lookup(ip, &out));
}

TEST(NeighborTableTest, StaticEntryNotExpired) {
    NeighborTable::Config cfg;
    cfg.entry_timeout_ms = 10;
    NeighborTable table(cfg);

    const uint32_t ip = 0x0a000003;
    const rte_ether_addr mac = make_mac(0x30);
    table.seed(ip, mac);

    table.expire(999999);
    rte_ether_addr out{};
    EXPECT_TRUE(table.lookup(ip, &out));
}

TEST(NeighborTableTest, LearnDoesNotOverrideStatic) {
    NeighborTable table;
    const uint32_t ip = 0x0a000004;
    const rte_ether_addr seeded = make_mac(0x40);
    const rte_ether_addr learned = make_mac(0x50);

    table.seed(ip, seeded);
    table.learn(ip, learned, 1);

    rte_ether_addr out{};
    ASSERT_TRUE(table.lookup(ip, &out));
    EXPECT_EQ(std::memcmp(out.addr_bytes, seeded.addr_bytes, RTE_ETHER_ADDR_LEN),
              0);
}
