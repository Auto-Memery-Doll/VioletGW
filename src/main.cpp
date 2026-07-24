#include "base/log.hpp"
#include "config.hpp"
#include "cp_shm.hpp"
#include "dpdk_netif.hpp"
#include "forward.hpp"
#include "session.hpp"
#include "upstream.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <rte_ether.h>
#include <rte_ip.h>
#include <spdlog/spdlog.h>

namespace {

uint64_t now_ms() {
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now().time_since_epoch())
            .count());
}

rte_ether_addr mac_from(const uint8_t bytes[6]) {
    rte_ether_addr mac{};
    std::memcpy(mac.addr_bytes, bytes, RTE_ETHER_ADDR_LEN);
    return mac;
}

uint32_t ipv4_from(const uint8_t o[4]) {
    return RTE_IPV4(o[0], o[1], o[2], o[3]);
}

}  // namespace

int main(int argc, char** argv) {
    vgm::init_logging();
    vgm::dpdk::init(argc, argv);
    vgm::dpdk_netif_mg()->init();

    vgm::DpdkNetif* netif = vgm::dpdk_netif_mg()->get_netif(0);
    if (netif == nullptr) {
        SPDLOG_ERROR("no DPDK netif for port 0");
        return 1;
    }

    const uint32_t vip_ip = ipv4_from(vgm::config::VIP_IP_OCTETS);
    const uint32_t gw_ip = ipv4_from(vgm::config::GATEWAY_IP_OCTETS);

    vgm::session::SessionTable sessions(gw_ip, vgm::config::SESSION_IDLE_TIMEOUT_MS);

    vgm::control::CpShmSeed seed;
    seed.policy =
        static_cast<vgm::upstream::BalancePolicy>(vgm::config::BALANCE_POLICY);
    seed.endpoints = {{ipv4_from(vgm::config::UPSTREAM0_IP_OCTETS),
                       vgm::config::UPSTREAM0_PORT}};

    auto cp = vgm::control::CpShm::open(vgm::config::CP_SHM_NAME,
                                       /*create_if_missing=*/true, &seed);
    if (cp == nullptr) {
        SPDLOG_ERROR("failed to open control SHM {}", vgm::config::CP_SHM_NAME);
        return 1;
    }

    vgm::upstream::UpstreamTable upstreams;
    // Apply current SHM (or seed just written) into the table.
    if (!cp->poll_apply(&upstreams)) {
        // Existing SHM with version already equal to 0 edge-case: seed locally.
        upstreams.set_policy(seed.policy);
        upstreams.set(seed.endpoints);
    }

    vgm::forward::ForwardConfig fcfg;
    fcfg.vip.ip_be = vip_ip;
    fcfg.vip.port = vgm::config::VIP_PORT;
    fcfg.gateway_ip_be = gw_ip;
    fcfg.gateway_mac = mac_from(vgm::config::GATEWAY_MAC);
    fcfg.upstream_mac = mac_from(vgm::config::UPSTREAM_NH_MAC);
    fcfg.client_side_mac = mac_from(vgm::config::CLIENT_NH_MAC);

    vgm::forward::Forwarder forwarder(
        fcfg, &sessions,
        [&upstreams](const vgm::session::FlowKey& key,
                     vgm::forward::Upstream* out) {
            vgm::upstream::UpstreamEndpoint ep;
            if (!upstreams.pick(key, &ep)) {
                return false;
            }
            out->ip_be = ep.ip_be;
            out->port = ep.port;
            return true;
        });

    SPDLOG_INFO(
        "flow_gw worker up: vip={}.{}.{}.{}:{} shm={} upstreams={}",
        vgm::config::VIP_IP_OCTETS[0], vgm::config::VIP_IP_OCTETS[1],
        vgm::config::VIP_IP_OCTETS[2], vgm::config::VIP_IP_OCTETS[3],
        vgm::config::VIP_PORT, vgm::config::CP_SHM_NAME, upstreams.size());

    constexpr unsigned kBurst = 32;
    rte_mbuf* rx[kBurst];
    rte_mbuf* tx[kBurst];
    rte_mbuf* drop[kBurst];
    uint64_t last_expire_ms = now_ms();
    uint64_t last_cp_poll_ms = last_expire_ms;

    while (true) {
        const uint64_t now = now_ms();
        if (now - last_expire_ms >= vgm::config::SESSION_EXPIRE_INTERVAL_MS) {
            sessions.expire(now);
            last_expire_ms = now;
        }
        if (now - last_cp_poll_ms >= vgm::config::CP_POLL_INTERVAL_MS) {
            if (cp->poll_apply(&upstreams)) {
                SPDLOG_INFO("control SHM applied: version={} upstreams={}",
                            cp->last_applied_version(), upstreams.size());
            }
            last_cp_poll_ms = now;
        }

        const unsigned n = netif->recv_burst(rx, kBurst);
        if (n == 0) {
            continue;
        }

        unsigned n_tx = 0;
        unsigned n_drop = 0;
        for (unsigned i = 0; i < n; ++i) {
            const auto r = forwarder.handle(rx[i], now);
            if (r == vgm::forward::HandleResult::drop) {
                drop[n_drop++] = rx[i];
            } else {
                tx[n_tx++] = rx[i];
            }
        }

        if (n_tx > 0) {
            unsigned sent = 0;
            while (sent < n_tx) {
                const unsigned m = netif->send_burst(tx + sent, n_tx - sent);
                if (m == 0) {
                    break;
                }
                sent += m;
            }
            if (sent < n_tx) {
                vgm::DpdkNetif::free_burst(tx + sent, n_tx - sent);
            }
        }
        if (n_drop > 0) {
            vgm::DpdkNetif::free_burst(drop, n_drop);
        }
    }

    return 0;
}
