#pragma once

#include "base/ring.hpp"
#include "base/singleton.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <rte_ether.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

namespace vgm {
namespace dpdk {

extern struct rte_mempool* DPDK_mempool;
extern struct rte_ether_addr DPDK_ether_addr[];

/** mbuf_buf_size 0 = use config::DPDK_mempool_block_size */
void init(int argc, char* argv[], unsigned mbuf_buf_size = 0);
void clean();

int rx_burst(uint16_t port_id, uint16_t queue_id, struct rte_mbuf** rx_pkts,
             uint16_t nb_pkts);
int tx_burst(uint16_t port_id, uint16_t queue_id, struct rte_mbuf** tx_pkts,
             uint16_t nb_pkts);

rte_mbuf* get_mbuf();

}  // namespace dpdk

/** Per-port DPDK I/O: NIC ↔ mbuf rings. No adapter, no async free. */
class DpdkNetif {
    friend class DpdkNetifManager;

public:
    ~DpdkNetif();

    /** Worker API: pull RX mbufs / push TX mbufs (ownership transfers). */
    unsigned recv_burst(rte_mbuf** pkts, unsigned n);
    unsigned send_burst(rte_mbuf** pkts, unsigned n);

    /** Drop mbufs on this thread (sync free). */
    static void free_burst(rte_mbuf** pkts, unsigned n);

    uint16_t port() const { return port_id_; }

private:
    DpdkNetif() = default;
    void init(uint16_t port, uint16_t ring_size);

    void nic_recv();
    void nic_send();

    static int run_recv(void* arg);
    static int run_send(void* arg);

    uint16_t port_id_ = 0;
    Ring<rte_mbuf>::ptr rx_ring_;
    Ring<rte_mbuf>::ptr tx_ring_;
    bool stop_ = false;
};

class DpdkNetifManager : public base::Singletion<DpdkNetifManager> {
    friend class base::Singletion<DpdkNetifManager>;

public:
    using ptr = std::shared_ptr<DpdkNetifManager>;
    ~DpdkNetifManager();

    struct IoOptions {
        bool rx_lcore;
        bool tx_lcore;
        uint16_t ring_size;  /* 0 → config::VDEV_*_ring_num */
    };

    void init();
    void init(const IoOptions& opts);
    void stop();
    DpdkNetif* get_netif(int port);

private:
    DpdkNetifManager() = default;

    std::map<int, DpdkNetif*> netifs_;
    std::mutex mtx_;
};

inline auto dpdk_netif_mg() -> DpdkNetifManager::ptr {
    return DpdkNetifManager::GetInstance();
}

}  // namespace vgm

