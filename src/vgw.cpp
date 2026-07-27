#include "vgw.h"

#include "base/log.hpp"
#include "config.hpp"

#include <chrono>
#include <cstring>
#include <rte_ether.h>
#include <rte_ip.h>
#include <spdlog/spdlog.h>

namespace vgw {

uint64_t VioletGW::now_ms() {
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now().time_since_epoch())
            .count());
}

rte_ether_addr VioletGW::mac_from(const uint8_t bytes[6]) {
    rte_ether_addr mac{};
    std::memcpy(mac.addr_bytes, bytes, RTE_ETHER_ADDR_LEN);
    return mac;
}

uint32_t VioletGW::ipv4_from(const uint8_t o[4]) {
    return RTE_IPV4(o[0], o[1], o[2], o[3]);
}

int VioletGW::init_pipeline(const char* shm_name) {
    const uint32_t vip_ip = ipv4_from(config::VIP_IP_OCTETS);
    const uint32_t gw_ip = ipv4_from(config::GATEWAY_IP_OCTETS);

    sessions_ = std::make_unique<session::SessionTable>(
        gw_ip, config::SESSION_IDLE_TIMEOUT_MS);

    control::CpShmSeed seed;
    seed.policy =
        static_cast<upstream::BalancePolicy>(config::BALANCE_POLICY);
    seed.endpoints = {{ipv4_from(config::UPSTREAM0_IP_OCTETS),
                       config::UPSTREAM0_PORT}};

    shm_name_ = shm_name != nullptr ? shm_name : config::CP_SHM_NAME;
    cp_ = control::CpShm::open(shm_name_, /*create_if_missing=*/true, &seed);
    if (cp_ == nullptr) {
        SPDLOG_ERROR("failed to open control SHM {}", shm_name_);
        return 1;
    }

    if (!cp_->poll_apply(&upstreams_)) {
        upstreams_.set_policy(seed.policy);
        upstreams_.set(seed.endpoints);
    }

    forward::ForwardConfig fcfg;
    fcfg.vip.ip_be = vip_ip;
    fcfg.vip.port = config::VIP_PORT;
    fcfg.gateway_ip_be = gw_ip;
    fcfg.gateway_mac = mac_from(config::GATEWAY_MAC);
    fcfg.upstream_mac = mac_from(config::UPSTREAM_NH_MAC);
    fcfg.client_side_mac = mac_from(config::CLIENT_NH_MAC);

    forwarder_ = std::make_unique<forward::Forwarder>(
        fcfg, sessions_.get(),
        [this](const session::FlowKey& key, forward::Upstream* out) {
            upstream::UpstreamEndpoint ep;
            if (!upstreams_.pick(key, &ep)) {
                return false;
            }
            out->ip_be = ep.ip_be;
            out->port = ep.port;
            return true;
        });

    SPDLOG_INFO(
        "vgw pipeline ready: vip={}.{}.{}.{}:{} shm={} upstreams={}",
        config::VIP_IP_OCTETS[0], config::VIP_IP_OCTETS[1],
        config::VIP_IP_OCTETS[2], config::VIP_IP_OCTETS[3], config::VIP_PORT,
        shm_name_, upstreams_.size());
    return 0;
}

int VioletGW::init(int argc, char** argv) {
    init_logging();
    dpdk::init(argc, argv);
    dpdk_netif_mg()->init();

    netif_ = dpdk_netif_mg()->get_netif(0);
    if (netif_ == nullptr) {
        SPDLOG_ERROR("no DPDK netif for port 0");
        return 1;
    }

    return init_pipeline();
}

forward::HandleResult VioletGW::handle(rte_mbuf* m, uint64_t now_ms) {
    if (forwarder_ == nullptr) {
        return forward::HandleResult::drop;
    }
    return forwarder_->handle(m, now_ms);
}

bool VioletGW::poll_control() {
    if (cp_ == nullptr) {
        return false;
    }
    return cp_->poll_apply(&upstreams_);
}

void VioletGW::expire_sessions(uint64_t now) {
    if (sessions_ != nullptr) {
        sessions_->expire(now);
    }
}

int VioletGW::run() {
    if (netif_ == nullptr || forwarder_ == nullptr || sessions_ == nullptr ||
        cp_ == nullptr) {
        SPDLOG_ERROR("VioletGW::run called before successful init");
        return 1;
    }

    constexpr unsigned kBurst = 32;
    rte_mbuf* rx[kBurst];
    rte_mbuf* tx[kBurst];
    rte_mbuf* drop[kBurst];
    uint64_t last_expire_ms = now_ms();
    uint64_t last_cp_poll_ms = last_expire_ms;

    while (true) {
        const uint64_t now = now_ms();
        if (now - last_expire_ms >= config::SESSION_EXPIRE_INTERVAL_MS) {
            expire_sessions(now);
            last_expire_ms = now;
        }
        if (now - last_cp_poll_ms >= config::CP_POLL_INTERVAL_MS) {
            if (poll_control()) {
                SPDLOG_INFO("control SHM applied: version={} upstreams={}",
                            cp_->last_applied_version(), upstreams_.size());
            }
            last_cp_poll_ms = now;
        }

        const unsigned n = netif_->recv_burst(rx, kBurst);
        if (n == 0) {
            continue;
        }

        unsigned n_tx = 0;
        unsigned n_drop = 0;
        for (unsigned i = 0; i < n; ++i) {
            const auto r = handle(rx[i], now);
            if (r == forward::HandleResult::drop) {
                drop[n_drop++] = rx[i];
            } else {
                tx[n_tx++] = rx[i];
            }
        }

        if (n_tx > 0) {
            unsigned sent = 0;
            while (sent < n_tx) {
                const unsigned m = netif_->send_burst(tx + sent, n_tx - sent);
                if (m == 0) {
                    break;
                }
                sent += m;
            }
            if (sent < n_tx) {
                DpdkNetif::free_burst(tx + sent, n_tx - sent);
            }
        }
        if (n_drop > 0) {
            DpdkNetif::free_burst(drop, n_drop);
        }
    }

    return 0;
}

}  // namespace vgw
