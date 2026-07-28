#pragma once

#include <cstdint>

namespace vgw {

enum class DatapathMode : uint8_t { Pipeline = 0, Rtc = 1 };
enum class RtcSteer : uint8_t { Auto = 0, HwRss = 1, SoftRr = 2 };
enum class RtcResolvedPath : uint8_t { Direct = 0, HwRss = 1, SoftRr = 2 };

struct PipelineParams {
    uint16_t ring_size = 0;
    bool launch_rx_lcore = true;
    bool launch_tx_lcore = true;
    int tx_ring_full_sleep_us = 10;
};

struct RtcParams {
    RtcSteer steer = RtcSteer::Auto;
    uint16_t workers = 1;
    uint16_t rx_queues = 0;
    uint16_t tx_queues = 0;
    uint16_t dist_ring_size = 0;
};

struct DatapathConfig {
    DatapathMode mode = DatapathMode::Pipeline;
    uint32_t port_mask = 0;
    uint16_t worker_port = 0;
    union {
        PipelineParams pipeline;
        RtcParams rtc;
    } u{};
};

DatapathConfig datapath_config_defaults();
RtcResolvedPath resolve_rtc_path(const RtcParams& rtc, bool hw_rss_usable);
bool parse_datapath_mode(const char* s, DatapathMode* out);
bool parse_rtc_steer(const char* s, RtcSteer* out);

}  // namespace vgw
