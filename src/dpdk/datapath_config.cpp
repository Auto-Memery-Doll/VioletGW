#include "dpdk/datapath_config.hpp"

#include "dpdk/config.hpp"

#include <cstring>

namespace vgw {

DatapathConfig datapath_config_defaults() {
    DatapathConfig c{};
    c.mode = DatapathMode::Pipeline;
    c.port_mask = config::DPDK_vaild_port_marks;
    c.worker_port = 0;
    c.u.pipeline.ring_size = 0;
    c.u.pipeline.launch_rx_lcore = config::DPDK_rx_config_default;
    c.u.pipeline.launch_tx_lcore = config::DPDK_tx_config_default;
    c.u.pipeline.tx_ring_full_sleep_us = config::IO_TX_RING_FULL_SLEEP_US;
    return c;
}

RtcResolvedPath resolve_rtc_path(const RtcParams& rtc, bool hw_rss_usable) {
    if (rtc.workers <= 1) {
        return RtcResolvedPath::Direct;
    }
    if (rtc.steer == RtcSteer::HwRss) {
        return RtcResolvedPath::HwRss;
    }
    if (rtc.steer == RtcSteer::SoftRr) {
        return RtcResolvedPath::SoftRr;
    }
    return hw_rss_usable ? RtcResolvedPath::HwRss : RtcResolvedPath::SoftRr;
}

bool parse_datapath_mode(const char* s, DatapathMode* out) {
    if (s == nullptr || out == nullptr) {
        return false;
    }
    if (std::strcmp(s, "pipeline") == 0) {
        *out = DatapathMode::Pipeline;
        return true;
    }
    if (std::strcmp(s, "rtc") == 0) {
        *out = DatapathMode::Rtc;
        return true;
    }
    return false;
}

bool parse_rtc_steer(const char* s, RtcSteer* out) {
    if (s == nullptr || out == nullptr) {
        return false;
    }
    if (std::strcmp(s, "auto") == 0) {
        *out = RtcSteer::Auto;
        return true;
    }
    if (std::strcmp(s, "hw_rss") == 0) {
        *out = RtcSteer::HwRss;
        return true;
    }
    if (std::strcmp(s, "soft_rr") == 0) {
        *out = RtcSteer::SoftRr;
        return true;
    }
    return false;
}

}  // namespace vgw
