#include "base/log.hpp"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

TEST(LogTest, InitDefaultLogger) {
    vgw::init_logging();
    auto logger = spdlog::default_logger();
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->name(), "vgw");
    SPDLOG_INFO("gtest log smoke");
}
