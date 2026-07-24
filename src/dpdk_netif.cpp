#include "dpdk_netif.hpp"

#include "base/util.hpp"
#include "config.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
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

namespace vgm {
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

static void port_init() {
    uint16_t avail = rte_eth_dev_count_avail();
    if (avail == 0) {
        rte_exit(EXIT_FAILURE, "No supported eth found\n");
    }

    uint16_t port_it = 0;
    uint32_t ports_mark = config::DPDK_vaild_port_marks;
    RTE_ETH_FOREACH_DEV(port_it) {
        if ((ports_mark & (1u << port_it)) == 0) {
            continue;
        }

        printf("Initializing port %u...\n", port_it);

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
        printf("Port %u, MAC address: " RTE_ETHER_ADDR_PRT_FMT "\n\n", port_it,
               RTE_ETHER_ADDR_BYTES(&DPDK_ether_addr[port_it]));
    }
}

static int check_link_status() {
    uint32_t port_mask = config::DPDK_vaild_port_marks;
    uint16_t portid = 0;
    int link_up = 0;

    printf("\nChecking link status...\n");
    RTE_ETH_FOREACH_DEV(portid) {
        if ((port_mask & (1u << portid)) == 0) {
            continue;
        }

        struct rte_eth_link link;
        memset(&link, 0, sizeof(link));
        int ret = rte_eth_link_get(portid, &link);
        if (ret < 0) {
            printf("Port %u link get failed: err=%d\n", portid, ret);
            continue;
        }

        char link_status_text[RTE_ETH_LINK_MAX_STR_LEN];
        rte_eth_link_to_str(link_status_text, sizeof(link_status_text), &link);
        printf("Port %u %s\n", portid, link_status_text);

        if (link.link_status) {
            link_up = 1;
        }
    }
    return link_up;
}

void init(int argc, char** argv) {
    if (rte_eal_init(argc, argv) < 0) {
        rte_exit(EXIT_FAILURE, "rte_eal_init() failure.\n");
    }

    struct rte_mempool* pool = rte_pktmbuf_pool_create(
        config::DPDK_mempool_name, config::DPDK_mempool_block_num,
        config::DPDK_mempool_cache_size, config::DPDK_mempool_private_size,
        config::DPDK_mempool_block_size, rte_socket_id());
    if (pool == nullptr) {
        rte_exit(EXIT_FAILURE, "rte_pktmbuf_pool_create() failure(%s)\n",
                 config::DPDK_mempool_name);
    }
    DPDK_mempool = pool;

    port_init();
    while (!check_link_status()) {
        sleep(1);
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
        printf("rte_pktmbuf_alloc failed\n");
    }
    return m;
}

}  // namespace dpdk

DpdkNetif::~DpdkNetif() {
    printf("DpdkNetif port %u down.\n", port_id_);
}

void DpdkNetif::init(uint16_t port) {
    port_id_ = port;
    rx_ring_ = make_ring<rte_mbuf>(util::RX_RING_NAME(port).c_str(),
                                   config::VDEV_rx_ring_num,
                                   config::VDEV_rx_ring_mode);
    tx_ring_ = make_ring<rte_mbuf>(util::TX_RING_NAME(port).c_str(),
                                   config::VDEV_tx_ring_num,
                                   config::VDEV_tx_ring_mode);
}

void DpdkNetif::free_burst(rte_mbuf** pkts, unsigned n) {
    if (n == 0) {
        return;
    }
    rte_pktmbuf_free_bulk(pkts, n);
}

unsigned DpdkNetif::recv_burst(rte_mbuf** pkts, unsigned n) {
    return rx_ring_->pop_burst(pkts, n);
}

unsigned DpdkNetif::send_burst(rte_mbuf** pkts, unsigned n) {
    unsigned sent = 0;
    while (sent < n) {
        unsigned en = tx_ring_->push_burst(pkts + sent, n - sent);
        if (en == 0) {
            usleep(config::VDEV_tx_sleep);
            continue;
        }
        sent += en;
    }
    return sent;
}

void DpdkNetif::nic_recv() {
    rte_mbuf* mbufs[config::VDEV_rx_burst_num];

    for (uint16_t q = 0; q < config::DPDK_rx_queue_num; ++q) {
        const uint16_t n =
            dpdk::rx_burst(port_id_, q, mbufs, config::VDEV_rx_burst_num);
        if (n == 0) {
            continue;
        }

        unsigned enqueued = 0;
        while (enqueued < n) {
            unsigned got =
                rx_ring_->push_burst(mbufs + enqueued, n - enqueued);
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
    rte_mbuf* mbufs[config::VDEV_tx_burst_num];
    const unsigned n =
        tx_ring_->pop_burst(mbufs, config::VDEV_tx_burst_num);
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
    while (!netif->stop_) {
        netif->nic_recv();
    }
    return 0;
}

int DpdkNetif::run_send(void* arg) {
    auto* netif = static_cast<DpdkNetif*>(arg);
    while (!netif->stop_) {
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
    std::lock_guard<std::mutex> lock(mtx_);

    uint32_t port_mask = config::DPDK_vaild_port_marks;
    unsigned rx_id = rte_get_next_lcore(rte_lcore_id(), true, false);
    unsigned tx_id = rte_get_next_lcore(rx_id, true, false);
    int cnt = 0;

    for (int i = 0; i < 32; ++i) {
        if ((port_mask & (1u << i)) == 0) {
            continue;
        }

        auto* netif = new DpdkNetif();
        netif->init(static_cast<uint16_t>(i));
        netifs_.insert({i, netif});

        rte_eal_remote_launch(DpdkNetif::run_recv, netif, rx_id);
        rte_eal_remote_launch(DpdkNetif::run_send, netif, tx_id);

        char mac_addr[VGM_MAC_DUMP_LEN];
        util::mac_dump(mac_addr, dpdk::DPDK_ether_addr[i]);
        printf("init port %d: %s\n", i, mac_addr);

        ++cnt;
        if (cnt == config::VDEV_core_max_rxtx) {
            cnt = 0;
            rx_id = rte_get_next_lcore(tx_id, true, false);
            tx_id = rte_get_next_lcore(rx_id, true, false);
        }
    }
}

void DpdkNetifManager::stop() {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& entry : netifs_) {
        entry.second->stop_ = true;
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

}  // namespace vgm
