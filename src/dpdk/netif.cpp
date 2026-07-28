#include "dpdk/netif.hpp"

#include "base/util.hpp"
#include "dpdk/config.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <unistd.h>

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <spdlog/spdlog.h>

namespace vgw {
namespace dpdk {

struct rte_mempool* DPDK_mempool = nullptr;
struct rte_ether_addr DPDK_ether_addr[RTE_MAX_ETHPORTS];

static struct rte_eth_conf g_port_conf = {
    .rxmode =
        {
            .max_lro_pkt_size = config::DPDK_port_rxmode_max_lro_size,
        },
};

static uint16_t g_nb_rx_desc = config::DPDK_nb_rx_queue_desc;
static uint16_t g_nb_tx_desc = config::DPDK_nb_tx_queue_desc;

static void init_rx_queue(int port, int queue_id) {
    int ret = rte_eth_rx_queue_setup(port, queue_id, config::DPDK_nb_rx_queue_desc,
                                     rte_eth_dev_socket_id(port), nullptr,
                                     DPDK_mempool);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE,
                 "Could not setup RX queue{port: %d queue: %d}.\nwhat():%s\n",
                 port, queue_id, rte_strerror(-ret));
    }
}

static void init_tx_queue(int port, int queue_id) {
    if (rte_eth_tx_queue_setup(port, queue_id, config::DPDK_nb_tx_queue_desc,
                               rte_eth_dev_socket_id(port), nullptr) < 0) {
        rte_exit(EXIT_FAILURE, "Could not setup TX queue{port: %d queue: %d}.\n",
                 port, queue_id);
    }
}

static void port_init(uint32_t port_mask) {
    uint16_t avail = rte_eth_dev_count_avail();
    if (avail == 0) {
        rte_exit(EXIT_FAILURE, "No supported eth found\n");
    }

    uint16_t port_it = 0;
    RTE_ETH_FOREACH_DEV(port_it) {
        if ((port_mask & (1u << port_it)) == 0) {
            continue;
        }

        SPDLOG_INFO("Initializing port {}...", port_it);

        struct rte_eth_dev_info dev_info;
        memset(&dev_info, 0, sizeof(dev_info));
        int ret = rte_eth_dev_info_get(port_it, &dev_info);
        if (ret < 0) {
            rte_exit(EXIT_FAILURE, "Cannot get device info: %s, port=%u\n",
                     rte_strerror(-ret), port_it);
        }

        struct rte_eth_conf l_port_conf = g_port_conf;
        ret = rte_eth_dev_configure(port_it, config::DPDK_rx_queue_num,
                                    config::DPDK_tx_queue_num, &l_port_conf);
        if (ret < 0) {
            rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n",
                     ret, port_it);
        }

        ret = rte_eth_dev_adjust_nb_rx_tx_desc(port_it, &g_nb_rx_desc, &g_nb_tx_desc);
        if (ret < 0) {
            rte_exit(EXIT_FAILURE,
                     "Cannot adjust number of descriptors: err=%d, port=%u\n", ret,
                     port_it);
        }

        rte_eth_macaddr_get(port_it, &DPDK_ether_addr[port_it]);

        for (uint16_t q = 0; q < config::DPDK_rx_queue_num; ++q) {
            init_rx_queue(port_it, q);
        }
        for (uint16_t q = 0; q < config::DPDK_tx_queue_num; ++q) {
            init_tx_queue(port_it, q);
        }

        ret = rte_eth_dev_start(port_it);
        if (ret < 0) {
            rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n", ret,
                     port_it);
        }

        rte_eth_promiscuous_enable(port_it);
        char mac_addr[VGW_MAC_DUMP_LEN];
        util::mac_dump(mac_addr, DPDK_ether_addr[port_it]);
        SPDLOG_INFO("Port {}, MAC address: {}", port_it, mac_addr);
    }
}

static int check_link_status(uint32_t port_mask) {
    uint16_t portid = 0;
    int link_up = 0;

    SPDLOG_INFO("Checking link status...");
    RTE_ETH_FOREACH_DEV(portid) {
        if ((port_mask & (1u << portid)) == 0) {
            continue;
        }

        struct rte_eth_link link;
        memset(&link, 0, sizeof(link));
        int ret = rte_eth_link_get(portid, &link);
        if (ret < 0) {
            SPDLOG_ERROR("Port {} link get failed: err={}", portid, ret);
            continue;
        }

        char link_status_text[RTE_ETH_LINK_MAX_STR_LEN];
        rte_eth_link_to_str(link_status_text, sizeof(link_status_text), &link);
        SPDLOG_INFO("Port {} {}", portid, link_status_text);

        if (link.link_status) {
            link_up = 1;
        }
    }
    return link_up;
}

void init(int argc, char** argv, unsigned mbuf_buf_size, uint32_t port_mask) {
    if (rte_eal_init(argc, argv) < 0) {
        rte_exit(EXIT_FAILURE, "rte_eal_init() failure.\n");
    }

    const unsigned pool_buf_size =
        mbuf_buf_size > 0 ? mbuf_buf_size : config::DPDK_mempool_block_size;
    struct rte_mempool* pool = rte_pktmbuf_pool_create(
        config::DPDK_mempool_name, config::DPDK_mempool_block_num,
        config::DPDK_mempool_cache_size, config::DPDK_mempool_private_size,
        pool_buf_size, rte_socket_id());
    if (pool == nullptr) {
        rte_exit(EXIT_FAILURE, "rte_pktmbuf_pool_create() failure(%s)\n",
                 config::DPDK_mempool_name);
    }
    DPDK_mempool = pool;

    const uint32_t effective_port_mask =
        port_mask != 0 ? port_mask : config::DPDK_vaild_port_marks;
    port_init(effective_port_mask);

    constexpr int kLinkCheckAttempts = 4;  // 1 immediate + 3 retries
    bool link_ok = false;
    for (int attempt = 1; attempt <= kLinkCheckAttempts; ++attempt) {
        if (check_link_status(effective_port_mask) != 0) {
            link_ok = true;
            break;
        }
        if (attempt == kLinkCheckAttempts) {
            break;
        }
        SPDLOG_WARN(
            "link not ready on port_mask={:#x} (attempt {}/{}), retrying in 1s",
            effective_port_mask, attempt, kLinkCheckAttempts);
        sleep(1);
    }
    if (!link_ok) {
        SPDLOG_ERROR(
            "no link up on enabled ports (port_mask={:#x}) after {} attempts",
            effective_port_mask, kLinkCheckAttempts);
        rte_exit(EXIT_FAILURE,
                 "link check failed after %d attempts (port_mask=0x%x)\n",
                 kLinkCheckAttempts, effective_port_mask);
    }
}

void clean() {
    rte_eal_mp_wait_lcore();
    rte_eal_cleanup();
}

int rx_burst(uint16_t port_id, uint16_t queue_id, struct rte_mbuf** rx_pkts,
             uint16_t nb_pkts) {
    return rte_eth_rx_burst(port_id, queue_id, rx_pkts, nb_pkts);
}

int tx_burst(uint16_t port_id, uint16_t queue_id, struct rte_mbuf** tx_pkts,
             uint16_t nb_pkts) {
    return rte_eth_tx_burst(port_id, queue_id, tx_pkts, nb_pkts);
}

rte_mbuf* get_mbuf() {
    rte_mbuf* m = rte_pktmbuf_alloc(DPDK_mempool);
    if (m == nullptr) {
        SPDLOG_ERROR("rte_pktmbuf_alloc failed");
    }
    return m;
}

}  // namespace dpdk

DpdkNetif::~DpdkNetif() {
    reset_state();
    SPDLOG_INFO("DpdkNetif port {} down.", port_id_);
}

void DpdkNetif::reset_state() {
    if (!state_active_) {
        return;
    }

    if (mode_ == DatapathMode::Pipeline) {
        state_.pipeline.~PipelineState();
    } else {
        state_.rtc.~RtcState();
    }
    state_active_ = false;
}

void DpdkNetif::init(uint16_t port, const DatapathConfig& cfg) {
    reset_state();

    mode_ = cfg.mode;
    port_id_ = port;
    queue_id_ = 0;

    if (mode_ == DatapathMode::Pipeline) {
        new (&state_.pipeline) PipelineState();
        state_active_ = true;
        const uint16_t ring_cap = cfg.u.pipeline.ring_size > 0
                                      ? cfg.u.pipeline.ring_size
                                      : config::IO_RING_SIZE;
        state_.pipeline.rx_ring =
            make_ring<rte_mbuf>(util::RX_RING_NAME(port).c_str(), ring_cap,
                                config::IO_RING_FLAGS);
        state_.pipeline.tx_ring =
            make_ring<rte_mbuf>(util::TX_RING_NAME(port).c_str(), ring_cap,
                                config::IO_RING_FLAGS);
        state_.pipeline.tx_ring_full_sleep_us =
            cfg.u.pipeline.tx_ring_full_sleep_us;
        return;
    }

    new (&state_.rtc) RtcState();
    state_active_ = true;
}

void DpdkNetif::free_burst(rte_mbuf** pkts, unsigned n) {
    if (n == 0) {
        return;
    }
    rte_pktmbuf_free_bulk(pkts, n);
}

unsigned DpdkNetif::recv_burst(rte_mbuf** pkts, unsigned n) {
    switch (mode_) {
    case DatapathMode::Pipeline:
        return state_.pipeline.rx_ring->pop_burst(pkts, n);
    case DatapathMode::Rtc: {
        const uint16_t nb = static_cast<uint16_t>(std::min(
            n, static_cast<unsigned>(std::numeric_limits<uint16_t>::max())));
        return static_cast<unsigned>(
            dpdk::rx_burst(port_id_, queue_id_, pkts, nb));
        }
    }
    return 0;
}

unsigned DpdkNetif::send_burst(rte_mbuf** pkts, unsigned n) {
    switch (mode_) {
    case DatapathMode::Pipeline: {
        unsigned sent = 0;
        while (sent < n) {
            const unsigned en =
                state_.pipeline.tx_ring->push_burst(pkts + sent, n - sent);
            if (en == 0) {
                usleep(state_.pipeline.tx_ring_full_sleep_us);
                continue;
            }
            sent += en;
        }
        return sent;
    }
    case DatapathMode::Rtc: {
        const uint16_t nb = static_cast<uint16_t>(std::min(
            n, static_cast<unsigned>(std::numeric_limits<uint16_t>::max())));
        return static_cast<unsigned>(
            dpdk::tx_burst(port_id_, queue_id_, pkts, nb));
    }
    }
    return 0;
}

void DpdkNetif::nic_recv() {
    rte_mbuf* mbufs[config::IO_RX_BURST];

    for (uint16_t q = 0; q < config::DPDK_rx_queue_num; ++q) {
        const uint16_t n =
            dpdk::rx_burst(port_id_, q, mbufs, config::IO_RX_BURST);
        if (n == 0) {
            continue;
        }

        unsigned enqueued = 0;
        while (enqueued < n) {
            unsigned got =
                state_.pipeline.rx_ring->push_burst(mbufs + enqueued, n - enqueued);
            if (got == 0) {
                // Ring full: drop remainder on this lcore (sync free).
                free_burst(mbufs + enqueued, n - enqueued);
                break;
            }
            enqueued += got;
        }
    }
}

void DpdkNetif::nic_send() {
    rte_mbuf* mbufs[config::IO_TX_BURST];
    const unsigned n =
        state_.pipeline.tx_ring->pop_burst(mbufs, config::IO_TX_BURST);
    if (n == 0) {
        return;
    }

    const unsigned sent =
        dpdk::tx_burst(port_id_, 0, mbufs, static_cast<uint16_t>(n));
    // Driver owns successfully transmitted mbufs; free the rest here.
    if (sent < n) {
        free_burst(mbufs + sent, n - sent);
    }
}

int DpdkNetif::run_recv(void* arg) {
    auto* netif = static_cast<DpdkNetif*>(arg);
    while (!netif->state_.pipeline.stop) {
        netif->nic_recv();
    }
    return 0;
}

int DpdkNetif::run_send(void* arg) {
    auto* netif = static_cast<DpdkNetif*>(arg);
    while (!netif->state_.pipeline.stop) {
        netif->nic_send();
    }
    return 0;
}

DpdkNetifManager::~DpdkNetifManager() {
    for (auto& entry : netifs_) {
        delete entry.second;
    }
}

void DpdkNetifManager::init() {
    init(datapath_config_defaults());
}

void DpdkNetifManager::init(const IoOptions& opts) {
    DatapathConfig cfg = datapath_config_defaults();
    cfg.u.pipeline.ring_size = opts.ring_size;
    cfg.u.pipeline.launch_rx_lcore = opts.rx_lcore;
    cfg.u.pipeline.launch_tx_lcore = opts.tx_lcore;
    init(cfg);
}

void DpdkNetifManager::init(const DatapathConfig& cfg) {
    std::lock_guard<std::mutex> lock(mtx_);

    cfg_ = cfg;
    if (cfg_.mode == DatapathMode::Rtc &&
        resolve_rtc_path(cfg_.u.rtc, /*hw_rss_usable=*/false) !=
            RtcResolvedPath::Direct) {
        SPDLOG_ERROR(
            "RTC multi-worker paths are not available: P1/P2 not implemented; "
            "use --rtc_workers=1");
        rte_exit(EXIT_FAILURE,
                 "P1/P2 not implemented; use --rtc_workers=1\n");
    }

    uint32_t port_mask = cfg_.port_mask;
    unsigned rx_id = 0;
    unsigned tx_id = 0;
    if (cfg_.mode == DatapathMode::Pipeline) {
        rx_id = rte_get_next_lcore(rte_lcore_id(), true, false);
        tx_id = rte_get_next_lcore(rx_id, true, false);
    }
    int cnt = 0;

    for (int i = 0; i < 32; ++i) {
        if ((port_mask & (1u << i)) == 0) {
            continue;
        }

        auto* netif = new DpdkNetif();
        netif->init(static_cast<uint16_t>(i), cfg_);
        netifs_.insert({i, netif});

        if (cfg_.mode == DatapathMode::Pipeline &&
            cfg_.u.pipeline.launch_rx_lcore) {
            rte_eal_remote_launch(DpdkNetif::run_recv, netif, rx_id);
        }
        if (cfg_.mode == DatapathMode::Pipeline &&
            cfg_.u.pipeline.launch_tx_lcore) {
            rte_eal_remote_launch(DpdkNetif::run_send, netif, tx_id);
        }

        char mac_addr[VGW_MAC_DUMP_LEN];
        util::mac_dump(mac_addr, dpdk::DPDK_ether_addr[i]);
        SPDLOG_INFO("init port {}: {}", i, mac_addr);

        if (cfg_.mode == DatapathMode::Pipeline) {
            ++cnt;
            if (cnt == config::IO_PORTS_PER_RXTX_LCORE_PAIR) {
                cnt = 0;
                rx_id = rte_get_next_lcore(tx_id, true, false);
                tx_id = rte_get_next_lcore(rx_id, true, false);
            }
        }
    }
}

void DpdkNetifManager::stop() {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& entry : netifs_) {
        if (entry.second->mode_ == DatapathMode::Pipeline) {
            entry.second->state_.pipeline.stop = true;
        }
    }
}

DpdkNetif* DpdkNetifManager::get_netif(int port) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = netifs_.find(port);
    if (it == netifs_.end()) {
        return nullptr;
    }
    return it->second;
}

}  // namespace vgw
