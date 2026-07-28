#pragma once

#include "cp_shm.hpp"
#include "dpdk/datapath_config.hpp"
#include "dpdk/netif.hpp"
#include "forward.hpp"
#include "neighbor.hpp"
#include "arp.hpp"
#include "session.hpp"
#include "upstream.hpp"

#include <cstdint>
#include <memory>
#include <rte_ether.h>
#include <rte_mbuf.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace vgw {

/** Data-plane gateway: DPDK I/O + session NAT + control-plane SHM apply. */
class VioletGW {
public:
    VioletGW() = default;
    VioletGW(const VioletGW&) = delete;
    VioletGW& operator=(const VioletGW&) = delete;

    /**
     * Build session table, CP SHM, upstreams, and forwarder from config.hpp.
     * Does not touch DPDK ports (safe for unit / mbuf smokes).
     * Not related to DatapathMode::Pipeline — that is the I/O model.
     * @param shm_name override POSIX SHM name; nullptr → config::CP_SHM_NAME
     * @return 0 on success, non-zero on failure
     */
    int init_services(const char* shm_name = nullptr);

    /**
     * Production: logging + DPDK EAL/netif (from gflags datapath config) +
     * init_services(). 0 = ok.
     */
    int init(int argc, char** argv);

    /** One-packet datapath (same path used by run()). */
    forward::HandleOutcome handle(rte_mbuf* m, uint64_t now_ms);

    /** Apply CP SHM if version advanced. */
    bool poll_control();

    void expire_sessions(uint64_t now_ms);
    void expire_neighbors(uint64_t now_ms);

    /** Blocking worker loop on netif_ (requires successful init()). */
    int run();

    session::SessionTable* sessions() { return sessions_.get(); }
    control::CpShm* control() { return cp_.get(); }
    upstream::UpstreamTable* upstreams() { return &upstreams_; }
    forward::Forwarder* forwarder() { return forwarder_.get(); }
    neighbor::NeighborTable* neighbors() { return neighbors_.get(); }
    arp::ArpHandler* arp() { return arp_.get(); }
    DpdkNetif* netif() { return netif_; }
    const DatapathConfig& datapath() const { return datapath_; }

    static uint64_t now_ms();
    static rte_ether_addr mac_from(const uint8_t bytes[6]);
    /** Octets → IPv4 in network byte order (wire / rte_ipv4_hdr). */
    static uint32_t ipv4_from(const uint8_t o[4]);

private:
    struct ArpPendingEntry {
        rte_mbuf* m = nullptr;
        uint64_t enqueued_ms = 0;
    };

    bool ready_for_run() const;
    void enqueue_pending(uint32_t ip_be, rte_mbuf* m, uint64_t now_ms);
    void maybe_probe_arp(uint32_t ip_be,
                          uint64_t now_ms,
                          rte_mbuf** tx,
                          unsigned* n_tx,
                          unsigned tx_cap);
    void flush_pending(uint32_t ip_be,
                       uint64_t now_ms,
                       rte_mbuf** tx,
                       unsigned* n_tx,
                       unsigned tx_cap);
    void expire_pending(uint64_t now_ms);

    DatapathConfig datapath_{};
    DpdkNetif* netif_ = nullptr;
    std::unique_ptr<session::SessionTable> sessions_;
    std::unique_ptr<control::CpShm> cp_;
    upstream::UpstreamTable upstreams_;
    std::unique_ptr<neighbor::NeighborTable> neighbors_;
    std::unique_ptr<arp::ArpHandler> arp_;
    std::unique_ptr<forward::Forwarder> forwarder_;
    std::string shm_name_;
    std::unordered_map<uint32_t, std::vector<ArpPendingEntry>> arp_pending_;
    std::unordered_map<uint32_t, uint64_t> arp_probe_last_ms_;
};

}  // namespace vgw
