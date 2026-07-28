#pragma once

#include "dpdk/datapath_config.hpp"

#include <gflags/gflags.h>

#include <vector>

DECLARE_string(datapath_mode);
DECLARE_uint64(datapath_port_mask);
DECLARE_uint32(datapath_worker_port);
DECLARE_uint32(io_ring_size);
DECLARE_bool(io_rx_lcore);
DECLARE_bool(io_tx_lcore);
DECLARE_int32(io_tx_ring_full_sleep_us);
DECLARE_string(rtc_steer);
DECLARE_uint32(rtc_workers);
DECLARE_uint32(rtc_dist_ring_size);

namespace vgw {

void parse_vgw_command_line_flags(int argc, char** argv,
                                  std::vector<char*>* eal_argv);
DatapathConfig datapath_config_from_flags();

}  // namespace vgw
