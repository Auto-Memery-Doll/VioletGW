#include "dpdk/datapath_config.hpp"
#include "dpdk/datapath_flags.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

class DatapathFlagState final {
  public:
    DatapathFlagState()
        : datapath_mode(FLAGS_datapath_mode),
          datapath_port_mask(FLAGS_datapath_port_mask),
          datapath_worker_port(FLAGS_datapath_worker_port),
          io_ring_size(FLAGS_io_ring_size),
          io_rx_lcore(FLAGS_io_rx_lcore),
          io_tx_lcore(FLAGS_io_tx_lcore),
          io_tx_ring_full_sleep_us(FLAGS_io_tx_ring_full_sleep_us),
          rtc_steer(FLAGS_rtc_steer),
          rtc_workers(FLAGS_rtc_workers),
          rtc_dist_ring_size(FLAGS_rtc_dist_ring_size) {}

    ~DatapathFlagState() {
        FLAGS_datapath_mode = datapath_mode;
        FLAGS_datapath_port_mask = datapath_port_mask;
        FLAGS_datapath_worker_port = datapath_worker_port;
        FLAGS_io_ring_size = io_ring_size;
        FLAGS_io_rx_lcore = io_rx_lcore;
        FLAGS_io_tx_lcore = io_tx_lcore;
        FLAGS_io_tx_ring_full_sleep_us = io_tx_ring_full_sleep_us;
        FLAGS_rtc_steer = rtc_steer;
        FLAGS_rtc_workers = rtc_workers;
        FLAGS_rtc_dist_ring_size = rtc_dist_ring_size;
    }

  private:
    const std::string datapath_mode;
    const uint64_t datapath_port_mask;
    const uint32_t datapath_worker_port;
    const uint32_t io_ring_size;
    const bool io_rx_lcore;
    const bool io_tx_lcore;
    const int32_t io_tx_ring_full_sleep_us;
    const std::string rtc_steer;
    const uint32_t rtc_workers;
    const uint32_t rtc_dist_ring_size;
};

std::vector<char*> mutable_argv(std::vector<std::string>* args) {
    std::vector<char*> argv;
    argv.reserve(args->size());
    for (std::string& arg : *args) {
        argv.push_back(arg.data());
    }
    return argv;
}

}  // namespace

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

TEST(DatapathConfigTest, FromFlagsRtcDirect) {
    DatapathFlagState restore;
    FLAGS_datapath_mode = "rtc";
    FLAGS_rtc_workers = 1;
    FLAGS_rtc_steer = "soft_rr";
    auto c = vgw::datapath_config_from_flags();
    EXPECT_EQ(c.mode, vgw::DatapathMode::Rtc);
    EXPECT_EQ(c.u.rtc.workers, 1);
    EXPECT_EQ(vgw::resolve_rtc_path(c.u.rtc, false),
              vgw::RtcResolvedPath::Direct);
}

TEST(DatapathConfigTest, ParsesVgwFlagsAndPreservesEalArgs) {
    DatapathFlagState restore;
    std::vector<std::string> args = {
        "vgw", "--datapath_mode=rtc", "--rtc_workers=1", "-l", "0",
        "--no-huge", "--no-pci",
    };
    std::vector<char*> argv = mutable_argv(&args);
    std::vector<char*> eal_argv;

    vgw::parse_vgw_command_line_flags(static_cast<int>(argv.size()), argv.data(),
                                      &eal_argv);

    EXPECT_EQ(FLAGS_datapath_mode, "rtc");
    EXPECT_EQ(FLAGS_rtc_workers, 1U);
    ASSERT_EQ(eal_argv.size(), 5U);
    EXPECT_STREQ(eal_argv[0], "vgw");
    EXPECT_STREQ(eal_argv[1], "-l");
    EXPECT_STREQ(eal_argv[2], "0");
    EXPECT_STREQ(eal_argv[3], "--no-huge");
    EXPECT_STREQ(eal_argv[4], "--no-pci");
}

TEST(DatapathConfigTest, FromFlagsRejectsRtcWorkersAboveUint16Max) {
    EXPECT_EXIT(
        {
            FLAGS_datapath_mode = "rtc";
            FLAGS_rtc_workers =
                static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()) + 1;
            static_cast<void>(vgw::datapath_config_from_flags());
        },
        ::testing::ExitedWithCode(EXIT_FAILURE), "rtc_workers");
}

TEST(DatapathConfigTest, FromFlagsRejectsPortMaskAboveUint32Max) {
    EXPECT_EXIT(
        {
            FLAGS_datapath_port_mask =
                static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1;
            static_cast<void>(vgw::datapath_config_from_flags());
        },
        ::testing::ExitedWithCode(EXIT_FAILURE), "datapath_port_mask");
}

TEST(DatapathConfigTest, FromFlagsRejectsWorkerPortAboveUint16Max) {
    EXPECT_EXIT(
        {
            FLAGS_datapath_worker_port =
                static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()) + 1;
            static_cast<void>(vgw::datapath_config_from_flags());
        },
        ::testing::ExitedWithCode(EXIT_FAILURE), "datapath_worker_port");
}

TEST(DatapathConfigTest, FromFlagsRejectsPipelineRingSizeAboveUint16Max) {
    EXPECT_EXIT(
        {
            FLAGS_io_ring_size =
                static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()) + 1;
            static_cast<void>(vgw::datapath_config_from_flags());
        },
        ::testing::ExitedWithCode(EXIT_FAILURE), "io_ring_size");
}

TEST(DatapathConfigTest, FromFlagsRejectsRtcRingSizeAboveUint16Max) {
    EXPECT_EXIT(
        {
            FLAGS_datapath_mode = "rtc";
            FLAGS_rtc_dist_ring_size =
                static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()) + 1;
            static_cast<void>(vgw::datapath_config_from_flags());
        },
        ::testing::ExitedWithCode(EXIT_FAILURE), "rtc_dist_ring_size");
}

TEST(DatapathConfigTest, FromFlagsRejectsNegativeTxRingFullSleep) {
    EXPECT_EXIT(
        {
            FLAGS_io_tx_ring_full_sleep_us = -1;
            static_cast<void>(vgw::datapath_config_from_flags());
        },
        ::testing::ExitedWithCode(EXIT_FAILURE), "io_tx_ring_full_sleep_us");
}
