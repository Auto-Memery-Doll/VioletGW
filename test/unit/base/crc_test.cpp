#include "base/util.hpp"

#include <gtest/gtest.h>
#include <string>

TEST(CrcTest, Deterministic) {
    const char* samples[] = {
        "",
        "a",
        "hello",
        "vgw_udp_ip",
        "abcdefghijklmnopqrstuvwxyz0123456789",
    };

    for (const char* s : samples) {
        const std::string data(s);
        EXPECT_EQ(vgw::util::crc32(data), vgw::util::crc32(data)) << data;
    }
}

TEST(CrcTest, DifferentInputsUsuallyDiffer) {
    EXPECT_NE(vgw::util::crc32("abc"), vgw::util::crc32("abd"));
}

TEST(FnvTest, Deterministic) {
    EXPECT_EQ(vgw::util::fnv("node-1"), vgw::util::fnv("node-1"));
    EXPECT_NE(vgw::util::fnv("node-1"), vgw::util::fnv("node-2"));
}
