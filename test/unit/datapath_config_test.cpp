#include "dpdk/datapath_config.hpp"
#include <gtest/gtest.h>

TEST(DatapathConfigTest, DefaultsArePipeline) {
    auto c = vgw::datapath_config_defaults();
    EXPECT_EQ(c.mode, vgw::DatapathMode::Pipeline);
}

TEST(DatapathConfigTest, WorkersOneAlwaysDirect) {
    vgw::RtcParams rtc{};
    rtc.steer = vgw::RtcSteer::SoftRr;
    rtc.workers = 1;
    EXPECT_EQ(vgw::resolve_rtc_path(rtc, /*hw_rss_usable=*/false),
              vgw::RtcResolvedPath::Direct);
    EXPECT_EQ(vgw::resolve_rtc_path(rtc, /*hw_rss_usable=*/true),
              vgw::RtcResolvedPath::Direct);
}

TEST(DatapathConfigTest, AutoPicksHwOrSoftWhenMultiWorker) {
    vgw::RtcParams rtc{};
    rtc.steer = vgw::RtcSteer::Auto;
    rtc.workers = 2;
    EXPECT_EQ(vgw::resolve_rtc_path(rtc, true), vgw::RtcResolvedPath::HwRss);
    EXPECT_EQ(vgw::resolve_rtc_path(rtc, false), vgw::RtcResolvedPath::SoftRr);
}

TEST(DatapathConfigTest, ParseModeAndSteer) {
    vgw::DatapathMode m{};
    ASSERT_TRUE(vgw::parse_datapath_mode("rtc", &m));
    EXPECT_EQ(m, vgw::DatapathMode::Rtc);
    vgw::RtcSteer s{};
    ASSERT_TRUE(vgw::parse_rtc_steer("soft_rr", &s));
    EXPECT_EQ(s, vgw::RtcSteer::SoftRr);
}
