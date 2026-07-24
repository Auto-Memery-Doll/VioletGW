#include "base/util.hpp"

#include <gtest/gtest.h>
#include <string>

TEST(CrcTest, Deterministic) {
    const char* samples[] = {
        "",
        "a",
        "hello",
        "flow_gateway_udp_ip",
        "abcdefghijklmnopqrstuvwxyz0123456789",
    };

    for (const char* s : samples) {
        const std::string data(s);
        EXPECT_EQ(vgm::util::crc32(data), vgm::util::crc32(data)) << data;
    }
}

TEST(CrcTest, DifferentInputsUsuallyDiffer) {
    EXPECT_NE(vgm::util::crc32("abc"), vgm::util::crc32("abd"));
}

TEST(FnvTest, Deterministic) {
    EXPECT_EQ(vgm::util::fnv("node-1"), vgm::util::fnv("node-1"));
    EXPECT_NE(vgm::util::fnv("node-1"), vgm::util::fnv("node-2"));
}
