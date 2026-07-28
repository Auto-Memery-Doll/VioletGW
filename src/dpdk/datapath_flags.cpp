#include "dpdk/datapath_flags.hpp"

#include <cstdlib>
#include <limits>
#include <string_view>
#include <vector>

#include <rte_common.h>
#include <rte_eal.h>
#include <spdlog/spdlog.h>

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

namespace {

bool has_prefix(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

bool is_vgw_flag(std::string_view arg) {
    return has_prefix(arg, "--datapath_") || has_prefix(arg, "--io_") ||
           has_prefix(arg, "--noio_") || has_prefix(arg, "--rtc_") ||
           has_prefix(arg, "--flagfile") || arg == "--help" ||
           arg == "--helpfull" || arg == "--helpshort" ||
           arg == "--helpxml" || arg == "--version";
}

bool flag_requires_value(std::string_view arg) {
    const size_t value_separator = arg.find('=');
    const std::string_view name = arg.substr(0, value_separator);
    return name == "--datapath_mode" || name == "--datapath_port_mask" ||
           name == "--datapath_worker_port" || name == "--io_ring_size" ||
           name == "--io_tx_ring_full_sleep_us" || name == "--rtc_steer" ||
           name == "--rtc_workers" || name == "--rtc_dist_ring_size" ||
           name == "--flagfile";
}

void check_uint16_flag(const char* name, uint32_t value) {
    if (value > std::numeric_limits<uint16_t>::max()) {
        rte_exit(EXIT_FAILURE, "--%s=%u exceeds the maximum supported value (%u)\n",
                 name, value,
                 static_cast<unsigned>(std::numeric_limits<uint16_t>::max()));
    }
}

}  // namespace

void parse_vgw_command_line_flags(int argc, char** argv,
                                  std::vector<char*>* eal_argv) {
    std::vector<char*> vgw_argv;
    vgw_argv.reserve(static_cast<size_t>(argc));
    eal_argv->clear();
    eal_argv->reserve(static_cast<size_t>(argc));

    vgw_argv.push_back(argv[0]);
    eal_argv->push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--") {
            for (; i < argc; ++i) {
                eal_argv->push_back(argv[i]);
            }
            break;
        }
        if (!is_vgw_flag(arg)) {
            eal_argv->push_back(argv[i]);
            continue;
        }

        vgw_argv.push_back(argv[i]);
        if (flag_requires_value(arg) && arg.find('=') == std::string_view::npos &&
            i + 1 < argc) {
            vgw_argv.push_back(argv[++i]);
        }
    }

    int vgw_argc = static_cast<int>(vgw_argv.size());
    char** vgw_argv_data = vgw_argv.data();
    gflags::ParseCommandLineFlags(&vgw_argc, &vgw_argv_data,
                                  /*remove_flags=*/false);
}

DatapathConfig datapath_config_from_flags() {
    DatapathConfig c = datapath_config_defaults();
    DatapathMode mode{};
    if (!parse_datapath_mode(FLAGS_datapath_mode.c_str(), &mode)) {
        rte_exit(EXIT_FAILURE, "bad --datapath_mode=%s\n",
                 FLAGS_datapath_mode.c_str());
    }
    c.mode = mode;
    if (FLAGS_datapath_port_mask > std::numeric_limits<uint32_t>::max()) {
        rte_exit(EXIT_FAILURE,
                 "--datapath_port_mask=%llu exceeds the maximum supported value "
                 "(%u)\n",
                 static_cast<unsigned long long>(FLAGS_datapath_port_mask),
                 std::numeric_limits<uint32_t>::max());
    }
    check_uint16_flag("datapath_worker_port", FLAGS_datapath_worker_port);
    check_uint16_flag("io_ring_size", FLAGS_io_ring_size);
    check_uint16_flag("rtc_dist_ring_size", FLAGS_rtc_dist_ring_size);
    if (FLAGS_io_tx_ring_full_sleep_us < 0) {
        rte_exit(EXIT_FAILURE,
                 "--io_tx_ring_full_sleep_us=%d must not be negative\n",
                 FLAGS_io_tx_ring_full_sleep_us);
    }
    if (FLAGS_datapath_port_mask != 0) {
        c.port_mask = static_cast<uint32_t>(FLAGS_datapath_port_mask);
    }
    c.worker_port = static_cast<uint16_t>(FLAGS_datapath_worker_port);
    if (mode == DatapathMode::Pipeline) {
        if (FLAGS_rtc_steer != "auto" || FLAGS_rtc_workers != 1 ||
            FLAGS_rtc_dist_ring_size != 0) {
            SPDLOG_WARN(
                "RTC-only flags are ignored in pipeline mode: "
                "--rtc_steer, --rtc_workers, --rtc_dist_ring_size");
        }
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
        check_uint16_flag("rtc_workers", FLAGS_rtc_workers);
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
