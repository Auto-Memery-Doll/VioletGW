#include "vgw.h"

#include "base/log.hpp"
#include "config.hpp"
#include "dpdk/config.hpp"
#include "dpdk/datapath_flags.hpp"
#include "packet.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <spdlog/spdlog.h>

namespace vgw {
namespace {

const char* datapath_mode_name(DatapathMode mode) {
    switch (mode) {
    case DatapathMode::Pipeline:
        return "pipeline";
    case DatapathMode::Rtc:
        return "rtc";
    }
    return "unknown";
}

const char* ip_str(uint32_t ip_be, char* buf, size_t n) {
    if (inet_ntop(AF_INET, &ip_be, buf, static_cast<socklen_t>(n)) == nullptr) {
        std::snprintf(buf, n, "?");
    }
    return buf;
}

uint32_t ipv4_src_be(rte_mbuf* m) {
    if (m == nullptr ||
        rte_pktmbuf_data_len(m) < sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr)) {
        return 0;
    }
    const auto* eth = rte_pktmbuf_mtod(m, const rte_ether_hdr*);
    if (rte_be_to_cpu_16(eth->ether_type) != RTE_ETHER_TYPE_IPV4) {
        return 0;
    }
    const auto* ip = reinterpret_cast<const rte_ipv4_hdr*>(
        rte_pktmbuf_mtod_offset(m, const uint8_t*, sizeof(rte_ether_hdr)));
    return ip->src_addr;
}

}  // namespace

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
    // Network byte order (same as rte_ipv4_hdr::{src,dst}_addr).
    return rte_cpu_to_be_32(RTE_IPV4(o[0], o[1], o[2], o[3]));
}

int VioletGW::init_services(const char* shm_name) {
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

    neighbor::NeighborTable::Config ncfg;
    ncfg.entry_timeout_ms = config::ARP_ENTRY_TIMEOUT_MS;
    neighbors_ = std::make_unique<neighbor::NeighborTable>(ncfg);
    neighbors_->seed(ipv4_from(config::UPSTREAM0_IP_OCTETS),
                     mac_from(config::UPSTREAM_NH_MAC));

    forward::ForwardConfig fcfg;
    fcfg.vip.ip_be = vip_ip;
    fcfg.vip.port = config::VIP_PORT;
    fcfg.gateway_ip_be = gw_ip;
    fcfg.gateway_mac = mac_from(config::GATEWAY_MAC);
    fcfg.upstream_mac = mac_from(config::UPSTREAM_NH_MAC);
    fcfg.client_side_mac = mac_from(config::CLIENT_NH_MAC);

    arp::ArpConfig acfg;
    acfg.gateway_ip_be = gw_ip;
    acfg.vip_ip_be = vip_ip;
    acfg.gateway_mac = fcfg.gateway_mac;
    arp_ = std::make_unique<arp::ArpHandler>(acfg, neighbors_.get());

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
        },
        neighbors_.get());

    SPDLOG_INFO(
        "vgw services ready: vip={}.{}.{}.{}:{} shm={} upstreams={}",
        config::VIP_IP_OCTETS[0], config::VIP_IP_OCTETS[1],
        config::VIP_IP_OCTETS[2], config::VIP_IP_OCTETS[3], config::VIP_PORT,
        shm_name_, upstreams_.size());
    return 0;
}

int VioletGW::init(int argc, char** argv) {
    init_logging();
    datapath_ = datapath_config_from_flags();
    SPDLOG_INFO("datapath mode={} worker_port={} port_mask={:#x}",
                datapath_mode_name(datapath_.mode), datapath_.worker_port,
                datapath_.port_mask);

    dpdk::init(argc, argv, /*mbuf_buf_size=*/0, datapath_.port_mask);
    dpdk_netif_mg()->init(datapath_);

    netif_ = dpdk_netif_mg()->get_netif(datapath_.worker_port);
    if (netif_ == nullptr) {
        SPDLOG_ERROR("no DPDK netif for port {}", datapath_.worker_port);
        return 1;
    }

    if (const int rc = init_services()) {
        return rc;
    }

    const uint16_t port = netif_->port();
    if (port < RTE_MAX_ETHPORTS) {
        const rte_ether_addr nic_mac = dpdk::DPDK_ether_addr[port];
        forwarder_->set_gateway_mac(nic_mac);
        arp_->set_gateway_mac(nic_mac);
        SPDLOG_INFO(
            "vgw using NIC MAC on port {}: "
            "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
            port, nic_mac.addr_bytes[0], nic_mac.addr_bytes[1],
            nic_mac.addr_bytes[2], nic_mac.addr_bytes[3],
            nic_mac.addr_bytes[4], nic_mac.addr_bytes[5]);
    }

    SPDLOG_INFO("vgw init complete (port={})", netif_->port());
    return 0;
}

forward::HandleOutcome VioletGW::handle(rte_mbuf* m, uint64_t now_ms) {
    if (m == nullptr) {
        return {};
    }

    const uint16_t etype = packet::ethertype(m);
    if (etype == RTE_ETHER_TYPE_ARP) {
        if (arp_ == nullptr) {
            return {};
        }
        const arp::ArpOutcome ao = arp_->handle(m, now_ms);
        forward::HandleOutcome ho;
        ho.result = ao.result;
        ho.learned_ip_be = ao.learned_ip_be;
        return ho;
    }
    if (etype == RTE_ETHER_TYPE_IPV4) {
        if (forwarder_ == nullptr) {
            return {};
        }
        if (neighbors_ != nullptr) {
            packet::passive_learn_ipv4(m, neighbors_.get(), now_ms);
            const uint32_t src_ip_be = ipv4_src_be(m);
            if (src_ip_be != 0) {
                char ipbuf[INET_ADDRSTRLEN];
                SPDLOG_DEBUG("passive learn: {}",
                             ip_str(src_ip_be, ipbuf, sizeof(ipbuf)));
            }
        }
        forward::HandleOutcome ho = forwarder_->handle(m, now_ms);
        const uint32_t src_ip_be = ipv4_src_be(m);
        if (src_ip_be != 0 && arp_pending_.find(src_ip_be) != arp_pending_.end()) {
            ho.learned_ip_be = src_ip_be;
        }
        return ho;
    }
    return {};
}

void VioletGW::enqueue_pending(uint32_t ip_be, rte_mbuf* m, uint64_t now_ms) {
    if (m == nullptr || ip_be == 0) {
        return;
    }
    auto& q = arp_pending_[ip_be];
    if (q.size() >= config::ARP_PENDING_MAX_PER_IP) {
        char ipbuf[INET_ADDRSTRLEN];
        SPDLOG_DEBUG("arp pending drop: queue full for {} (max={})",
                     ip_str(ip_be, ipbuf, sizeof(ipbuf)),
                     config::ARP_PENDING_MAX_PER_IP);
        DpdkNetif::free_burst(&m, 1);
        return;
    }
    q.push_back(ArpPendingEntry{m, now_ms});
    char ipbuf[INET_ADDRSTRLEN];
    SPDLOG_DEBUG("arp pending enqueue: {} depth={}",
                 ip_str(ip_be, ipbuf, sizeof(ipbuf)), q.size());
}

void VioletGW::maybe_probe_arp(uint32_t ip_be,
                               uint64_t now_ms,
                               rte_mbuf** tx,
                               unsigned* n_tx,
                               unsigned tx_cap) {
    if (arp_ == nullptr || ip_be == 0 || tx == nullptr || n_tx == nullptr) {
        return;
    }
    const auto it = arp_probe_last_ms_.find(ip_be);
    if (it != arp_probe_last_ms_.end() &&
        now_ms - it->second < config::ARP_PROBE_INTERVAL_MS) {
        return;
    }
    rte_mbuf* probe = arp_->make_request(ip_be);
    if (probe == nullptr) {
        SPDLOG_DEBUG("arp probe alloc failed");
        return;
    }
    if (*n_tx >= tx_cap) {
        rte_pktmbuf_free(probe);
        SPDLOG_DEBUG("arp probe deferred: tx batch full");
        return;
    }
    tx[(*n_tx)++] = probe;
    arp_probe_last_ms_[ip_be] = now_ms;
    char ipbuf[INET_ADDRSTRLEN];
    SPDLOG_DEBUG("arp probe sent: {}", ip_str(ip_be, ipbuf, sizeof(ipbuf)));
}

void VioletGW::flush_pending(uint32_t ip_be,
                             uint64_t now_ms,
                             rte_mbuf** tx,
                             unsigned* n_tx,
                             unsigned tx_cap) {
    if (forwarder_ == nullptr || ip_be == 0 || tx == nullptr || n_tx == nullptr) {
        return;
    }
    const auto it = arp_pending_.find(ip_be);
    if (it == arp_pending_.end() || it->second.empty()) {
        return;
    }

    char ipbuf[INET_ADDRSTRLEN];
    unsigned flushed = 0;
    unsigned dropped = 0;
    std::vector<ArpPendingEntry> remaining;
    remaining.reserve(it->second.size());

    for (ArpPendingEntry& entry : it->second) {
        if (now_ms - entry.enqueued_ms >= config::ARP_PENDING_TIMEOUT_MS) {
            DpdkNetif::free_burst(&entry.m, 1);
            ++dropped;
            continue;
        }
        if (!forwarder_->finish_l2(entry.m, ip_be)) {
            remaining.push_back(entry);
            continue;
        }
        if (*n_tx >= tx_cap) {
            remaining.push_back(entry);
            continue;
        }
        tx[(*n_tx)++] = entry.m;
        ++flushed;
    }

    if (remaining.empty()) {
        arp_pending_.erase(it);
    } else {
        it->second = std::move(remaining);
    }

    if (flushed > 0 || dropped > 0) {
        const size_t remain =
            flushed > 0 || dropped > 0
                ? (arp_pending_.count(ip_be) ? arp_pending_[ip_be].size() : 0u)
                : 0u;
        SPDLOG_DEBUG("arp pending flush: {} sent={} remain={} expired={}",
                     ip_str(ip_be, ipbuf, sizeof(ipbuf)), flushed, remain,
                     dropped);
    }
}

void VioletGW::expire_pending(uint64_t now_ms) {
    for (auto it = arp_pending_.begin(); it != arp_pending_.end();) {
        char ipbuf[INET_ADDRSTRLEN];
        unsigned expired = 0;
        auto& q = it->second;
        for (auto entry_it = q.begin(); entry_it != q.end();) {
            if (now_ms - entry_it->enqueued_ms >= config::ARP_PENDING_TIMEOUT_MS) {
                DpdkNetif::free_burst(&entry_it->m, 1);
                entry_it = q.erase(entry_it);
                ++expired;
            } else {
                ++entry_it;
            }
        }
        if (expired > 0) {
            SPDLOG_DEBUG("arp pending expire: {} count={}",
                         ip_str(it->first, ipbuf, sizeof(ipbuf)), expired);
        }
        if (q.empty()) {
            it = arp_pending_.erase(it);
        } else {
            ++it;
        }
    }
}

bool VioletGW::poll_control() {
    if (cp_ == nullptr) {
        return false;
    }
    return cp_->poll_apply(&upstreams_);
}

void VioletGW::expire_sessions(uint64_t now_ms) {
    if (sessions_ != nullptr) {
        sessions_->expire(now_ms);
    }
}

void VioletGW::expire_neighbors(uint64_t now_ms) {
    if (neighbors_ != nullptr) {
        neighbors_->expire(now_ms);
    }
}

bool VioletGW::ready_for_run() const {
    return netif_ != nullptr && forwarder_ != nullptr &&
           sessions_ != nullptr && cp_ != nullptr;
}

int VioletGW::run() {
    if (!ready_for_run()) {
        SPDLOG_ERROR(
            "VioletGW::run called before successful init "
            "(netif={} forwarder={} sessions={} cp={})",
            netif_ != nullptr, forwarder_ != nullptr, sessions_ != nullptr,
            cp_ != nullptr);
        return 1;
    }

    constexpr unsigned kBurst = config::IO_RX_BURST;
    rte_mbuf* rx[kBurst];
    rte_mbuf* tx[kBurst];
    rte_mbuf* drop[kBurst];
    uint64_t last_expire_ms = now_ms();
    uint64_t last_arp_expire_ms = last_expire_ms;
    uint64_t last_cp_poll_ms = last_expire_ms;

    SPDLOG_INFO("vgw worker loop start: mode={} port={} burst={}",
                datapath_mode_name(datapath_.mode), netif_->port(), kBurst);

    while (true) {
        const uint64_t now = now_ms();
        if (now - last_expire_ms >= config::SESSION_EXPIRE_INTERVAL_MS) {
            expire_sessions(now);
            last_expire_ms = now;
        }
        if (now - last_arp_expire_ms >= config::ARP_EXPIRE_INTERVAL_MS) {
            expire_neighbors(now);
            expire_pending(now);
            last_arp_expire_ms = now;
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
            const forward::HandleOutcome o = handle(rx[i], now);
            if (o.learned_ip_be != 0) {
                flush_pending(o.learned_ip_be, now, tx, &n_tx, kBurst);
            }
            if (o.result == forward::HandleResult::drop) {
                drop[n_drop++] = rx[i];
            } else if (o.result == forward::HandleResult::pending_arp) {
                enqueue_pending(o.arp_wait_ip_be, rx[i], now);
                maybe_probe_arp(o.arp_wait_ip_be, now, tx, &n_tx, kBurst);
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
                VGW_IO_LOG_WARN("TX incomplete: sent={}/{}, freeing remainder",
                                sent, n_tx);
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
