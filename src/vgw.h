#pragma once

#include "cp_shm.hpp"
#include "dpdk/netif.hpp"
#include "forward.hpp"
#include "session.hpp"
#include "upstream.hpp"

#include <cstdint>
#include <memory>
#include <rte_ether.h>
#include <rte_mbuf.h>
#include <string>

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
     * @param shm_name override POSIX SHM name; nullptr → config::CP_SHM_NAME
     */
    int init_pipeline(const char* shm_name = nullptr);

    /** Production: logging + DPDK EAL/netif + init_pipeline(). 0 = ok. */
    int init(int argc, char** argv);

    /** One-packet datapath (same path used by run()). */
    forward::HandleResult handle(rte_mbuf* m, uint64_t now_ms);

    /** Apply CP SHM if version advanced. */
    bool poll_control();

    void expire_sessions(uint64_t now_ms);

    /** Blocking worker loop on netif_ (requires init()). */
    int run();

    session::SessionTable* sessions() { return sessions_.get(); }
    control::CpShm* control() { return cp_.get(); }
    upstream::UpstreamTable* upstreams() { return &upstreams_; }
    forward::Forwarder* forwarder() { return forwarder_.get(); }

    static uint64_t now_ms();
    static rte_ether_addr mac_from(const uint8_t bytes[6]);
    static uint32_t ipv4_from(const uint8_t o[4]);

private:
    DpdkNetif* netif_ = nullptr;
    std::unique_ptr<session::SessionTable> sessions_;
    std::unique_ptr<control::CpShm> cp_;
    upstream::UpstreamTable upstreams_;
    std::unique_ptr<forward::Forwarder> forwarder_;
    std::string shm_name_;
};

}  // namespace vgw
