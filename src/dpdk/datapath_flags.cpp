#include "dpdk/datapath_flags.hpp"

#include <cstdlib>

#include <rte_common.h>
#include <rte_eal.h>

DEFINE_string(datapath_mode, "pipeline", "pipeline | rtc");
DEFINE_uint64(datapath_port_mask, 0, "0 = use dpdk/config default");
DEFINE_uint32(datapath_worker_port, 0, "worker primary port id");
DEFINE_uint32(io_ring_size, 0, "0 = config::IO_RING_SIZE");
DEFINE_bool(io_rx_lcore, true, "pipeline: launch RX lcore");
DEFINE_bool(io_tx_lcore, true, "pipeline: launch TX lcore");
DEFINE_int32(io_tx_ring_full_sleep_us, 10, "pipeline TX ring full sleep");
DEFINE_string(rtc_steer, "auto", "auto | hw_rss | soft_rr");
DEFINE_uint32(rtc_workers, 1, "RTC worker count; 1 = direct");
DEFINE_uint32(rtc_dist_ring_size, 0, "soft_rr ring size; 0 = default");

namespace vgw {

DatapathConfig datapath_config_from_flags() {
    DatapathConfig c = datapath_config_defaults();
    DatapathMode mode{};
    if (!parse_datapath_mode(FLAGS_datapath_mode.c_str(), &mode)) {
        rte_exit(EXIT_FAILURE, "bad --datapath_mode=%s\n",
                 FLAGS_datapath_mode.c_str());
    }
    c.mode = mode;
    if (FLAGS_datapath_port_mask != 0) {
        c.port_mask = static_cast<uint32_t>(FLAGS_datapath_port_mask);
    }
    c.worker_port = static_cast<uint16_t>(FLAGS_datapath_worker_port);
    if (mode == DatapathMode::Pipeline) {
        c.u.pipeline = {};
        c.u.pipeline.ring_size = static_cast<uint16_t>(FLAGS_io_ring_size);
        c.u.pipeline.launch_rx_lcore = FLAGS_io_rx_lcore;
        c.u.pipeline.launch_tx_lcore = FLAGS_io_tx_lcore;
        c.u.pipeline.tx_ring_full_sleep_us = FLAGS_io_tx_ring_full_sleep_us;
    } else {
        c.u.rtc = {};
        RtcSteer steer{};
        if (!parse_rtc_steer(FLAGS_rtc_steer.c_str(), &steer)) {
            rte_exit(EXIT_FAILURE, "bad --rtc_steer=%s\n",
                     FLAGS_rtc_steer.c_str());
        }
        c.u.rtc.steer = steer;
        c.u.rtc.workers = static_cast<uint16_t>(FLAGS_rtc_workers);
        if (c.u.rtc.workers == 0) {
            c.u.rtc.workers = 1;
        }
        c.u.rtc.dist_ring_size =
            static_cast<uint16_t>(FLAGS_rtc_dist_ring_size);
    }
    return c;
}

}  // namespace vgw
