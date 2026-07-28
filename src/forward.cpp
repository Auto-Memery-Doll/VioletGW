#include "forward.hpp"

#include "base/log.hpp"
#include "packet.hpp"

#include <cstdio>
#include <netinet/in.h>
#include <rte_byteorder.h>
#include <spdlog/spdlog.h>

#if VGW_IO_TRACE
#include "base/pkt_filter.hpp"

#include <arpa/inet.h>
#include <cstdio>
#include <sys/socket.h>
#endif

namespace vgw {
#if VGW_IO_TRACE
static const vgw::util::PktFilter kVipFilter{
    .dst_mac = vgw::util::parse_mac("02:00:00:00:00:01"),
    .dst_ip_be = vgw::util::parse_ipv4_be("192.168.1.100"),
};
/** Return traffic before reverse-NAT: dst = gateway. */
static const vgw::util::PktFilter kGwDstFilter{
    .dst_ip_be = vgw::util::parse_ipv4_be("192.168.1.10"),
};
#endif

namespace forward {

namespace {

const char* ip_str(uint32_t ip_be, char* buf, size_t n) {
    if (inet_ntop(AF_INET, &ip_be, buf, static_cast<socklen_t>(n)) == nullptr) {
        std::snprintf(buf, n, "?");
    }
    return buf;
}

HandleOutcome pending_for(uint32_t next_hop_ip_be, const char* path) {
    char ipbuf[INET_ADDRSTRLEN];
    SPDLOG_DEBUG("arp wait: {} next-hop {} (neighbor miss, L4 ok)",
                 path, ip_str(next_hop_ip_be, ipbuf, sizeof(ipbuf)));
    HandleOutcome o;
    o.result = HandleResult::pending_arp;
    o.arp_wait_ip_be = next_hop_ip_be;
    return o;
}

bool lookup_next_hop_mac(neighbor::NeighborTable* neighbors,
                         uint32_t ip_be,
                         rte_ether_addr* out,
                         const rte_ether_addr& fallback,
                         const char* path) {
    char ipbuf[INET_ADDRSTRLEN];
    if (neighbors != nullptr && neighbors->lookup(ip_be, out)) {
        SPDLOG_DEBUG("arp hit: {} next-hop {}",
                     path, ip_str(ip_be, ipbuf, sizeof(ipbuf)));
        return true;
    }
    if (neighbors == nullptr) {
        rte_ether_addr_copy(&fallback, out);
        return true;
    }
    return false;
}

}  // namespace

session::FlowKey flow_key_from_view(const packet::PacketView& v) {
    session::FlowKey k;
    k.src_ip = v.src_ip();
    k.dst_ip = v.dst_ip();
    k.src_port = v.src_port();
    k.dst_port = v.dst_port();
    k.proto = IPPROTO_UDP;
    return k;
}

void set_l2(packet::PacketView* v,
            const rte_ether_addr& src,
            const rte_ether_addr& dst) {
    if (v == nullptr || v->eth == nullptr) {
        return;
    }
    rte_ether_addr_copy(&src, &v->eth->src_addr);
    rte_ether_addr_copy(&dst, &v->eth->dst_addr);
}

static void finish_l4_checksum(packet::PacketView* v, bool recompute_udp) {
    packet::refresh_ipv4_checksum(v->ip);
    if (recompute_udp) {
        packet::refresh_udp_checksum(v->ip, v->udp);
    } else {
        packet::clear_udp_checksum(v->udp);
    }
}

void apply_forward_nat(packet::PacketView* v,
                       const session::Session& s,
                       uint32_t gateway_ip_be,
                       bool recompute_udp_checksum) {
    // client:sport -> vip:vport  becomes  gw:snat -> upstream:uport
    v->ip->src_addr = gateway_ip_be;
    v->ip->dst_addr = s.upstream_ip;
    v->udp->src_port = rte_cpu_to_be_16(s.snat_port);
    v->udp->dst_port = rte_cpu_to_be_16(s.upstream_port);
    finish_l4_checksum(v, recompute_udp_checksum);
}

void apply_reverse_nat(packet::PacketView* v,
                       const session::Session& s,
                       bool recompute_udp_checksum) {
    // upstream:uport -> gw:snat  becomes  vip:vport -> client:sport
    v->ip->src_addr = s.vip_ip;
    v->ip->dst_addr = s.client_ip;
    v->udp->src_port = rte_cpu_to_be_16(s.vip_port);
    v->udp->dst_port = rte_cpu_to_be_16(s.client_port);
    finish_l4_checksum(v, recompute_udp_checksum);
}

Forwarder::Forwarder(ForwardConfig cfg,
                     session::SessionTable* sessions,
                     UpstreamPicker picker,
                     neighbor::NeighborTable* neighbors)
    : cfg_(cfg)
    , sessions_(sessions)
    , picker_(std::move(picker))
    , neighbors_(neighbors) {}

bool Forwarder::finish_l2(rte_mbuf* m, uint32_t next_hop_ip_be) const {
    packet::PacketView view;
    if (packet::parse_udp_ipv4(m, &view, false) != packet::ParseStatus::ok) {
        return false;
    }
    rte_ether_addr dst_mac;
    if (!lookup_next_hop_mac(neighbors_, next_hop_ip_be, &dst_mac,
                             cfg_.upstream_mac, "pending")) {
        return false;
    }
    set_l2(&view, cfg_.gateway_mac, dst_mac);
    return true;
}

#if VGW_IO_TRACE
namespace {

const char* ip_str(uint32_t ip_be, char* buf, size_t n) {
    if (inet_ntop(AF_INET, &ip_be, buf, static_cast<socklen_t>(n)) == nullptr) {
        std::snprintf(buf, n, "?");
    }
    return buf;
}

bool trace_pkt(const rte_mbuf* m) {
    return vgw::util::pkt_filter_match(m, kVipFilter) ||
           vgw::util::pkt_filter_match(m, kGwDstFilter);
}

}  // namespace
#endif

HandleOutcome Forwarder::handle_forward(packet::PacketView* v, uint64_t now_ms) {
#if VGW_IO_TRACE
    char sip[INET_ADDRSTRLEN];
    char dip[INET_ADDRSTRLEN];
    char uip[INET_ADDRSTRLEN];
    char gip[INET_ADDRSTRLEN];

    const bool tr = v != nullptr && trace_pkt(v->mbuf);
#endif
    const session::FlowKey key = flow_key_from_view(*v);
#if VGW_IO_TRACE
    if (tr) {
        VGW_IO_LOG_INFO(
            "fwd in: {}:{} -> {}:{} proto={}",
            ip_str(key.src_ip, sip, sizeof(sip)), key.src_port,
            ip_str(key.dst_ip, dip, sizeof(dip)), key.dst_port,
            static_cast<unsigned>(key.proto));
    }
#endif

    session::Session* s = sessions_->lookup_forward(key);
    if (s == nullptr) {
        Upstream up;
        if (!picker_) {
#if VGW_IO_TRACE
            if (tr) {
                VGW_IO_LOG_WARN("fwd drop: no upstream picker");
            }
#endif
            return HandleOutcome{};
        }
        if (!picker_(key, &up) || up.ip_be == 0) {
#if VGW_IO_TRACE
            if (tr) {
                VGW_IO_LOG_WARN("fwd drop: upstream pick failed");
            }
#endif
            return HandleOutcome{};
        }
#if VGW_IO_TRACE
        if (tr) {
            VGW_IO_LOG_INFO("fwd new session: upstream={}:{}",
                            ip_str(up.ip_be, uip, sizeof(uip)), up.port);
        }
#endif
        s = sessions_->create(key, up.ip_be, up.port, now_ms);
        if (s == nullptr) {
#if VGW_IO_TRACE
            if (tr) {
                VGW_IO_LOG_WARN(
                    "fwd drop: session create failed (snat exhausted?)");
            }
#endif
            return HandleOutcome{};
        }
#if VGW_IO_TRACE
        if (tr) {
            VGW_IO_LOG_INFO("fwd created: snat_port={} sessions={}",
                            s->snat_port, sessions_->size());
        }
#endif
    } else {
        sessions_->touch(s, now_ms);
#if VGW_IO_TRACE
        if (tr) {
            VGW_IO_LOG_INFO("fwd hit: snat_port={}", s->snat_port);
        }
#endif
    }

    apply_forward_nat(v, *s, cfg_.gateway_ip_be, cfg_.recompute_udp_checksum);
    rte_ether_addr dst_mac;
    if (!lookup_next_hop_mac(neighbors_, s->upstream_ip, &dst_mac,
                             cfg_.upstream_mac, "forward")) {
        return pending_for(s->upstream_ip, "forward");
    }
    set_l2(v, cfg_.gateway_mac, dst_mac);
#if VGW_IO_TRACE
    if (tr) {
        VGW_IO_LOG_INFO(
            "fwd out: {}:{} -> {}:{} (gw={})",
            ip_str(v->src_ip(), gip, sizeof(gip)), v->src_port(),
            ip_str(v->dst_ip(), uip, sizeof(uip)), v->dst_port(),
            ip_str(cfg_.gateway_ip_be, sip, sizeof(sip)));
    }
#endif
    HandleOutcome o;
    o.result = HandleResult::tx_forward;
    return o;
}

HandleOutcome Forwarder::handle_reverse(packet::PacketView* v, uint64_t now_ms) {
#if VGW_IO_TRACE
    char sip[INET_ADDRSTRLEN];
    char dip[INET_ADDRSTRLEN];

    const bool tr = v != nullptr && trace_pkt(v->mbuf);
#endif
    const session::FlowKey key = flow_key_from_view(*v);
#if VGW_IO_TRACE
    if (tr) {
        VGW_IO_LOG_INFO(
            "rev in: {}:{} -> {}:{}",
            ip_str(key.src_ip, sip, sizeof(sip)), key.src_port,
            ip_str(key.dst_ip, dip, sizeof(dip)), key.dst_port);
    }
#endif

    session::Session* s = sessions_->lookup_reverse(key);
    if (s == nullptr) {
#if VGW_IO_TRACE
        if (tr) {
            VGW_IO_LOG_WARN("rev drop: no session for reverse key");
        }
#endif
        return HandleOutcome{};
    }
    sessions_->touch(s, now_ms);
    apply_reverse_nat(v, *s, cfg_.recompute_udp_checksum);
    rte_ether_addr dst_mac;
    if (!lookup_next_hop_mac(neighbors_, s->client_ip, &dst_mac,
                             cfg_.client_side_mac, "reverse")) {
        return pending_for(s->client_ip, "reverse");
    }
    set_l2(v, cfg_.gateway_mac, dst_mac);
#if VGW_IO_TRACE
    if (tr) {
        VGW_IO_LOG_INFO(
            "rev out: {}:{} -> {}:{}",
            ip_str(v->src_ip(), sip, sizeof(sip)), v->src_port(),
            ip_str(v->dst_ip(), dip, sizeof(dip)), v->dst_port());
    }
#endif
    HandleOutcome o;
    o.result = HandleResult::tx_reverse;
    return o;
}

HandleOutcome Forwarder::handle(rte_mbuf* m, uint64_t now_ms) {
    packet::PacketView view;
    const packet::ParseStatus st =
        packet::parse_udp_ipv4(m, &view, cfg_.verify_checksum);
    if (st != packet::ParseStatus::ok) {
#if VGW_IO_TRACE
        if (trace_pkt(m)) {
            VGW_IO_LOG_WARN("drop: parse_udp_ipv4 status={}",
                            static_cast<unsigned>(st));
        }
#endif
        return HandleOutcome{};
    }

    const uint32_t dst = view.dst_ip();
    const uint16_t dport = view.dst_port();

#if VGW_IO_TRACE
    const bool tr = trace_pkt(m);
    if (tr) {
        char vip_buf[INET_ADDRSTRLEN];
        char dest_buf[INET_ADDRSTRLEN];
        VGW_IO_LOG_INFO("classify: vip={}:{} pkt_dst={}:{}",
                        ip_str(cfg_.vip.ip_be, vip_buf, sizeof(vip_buf)),
                        cfg_.vip.port,
                        ip_str(dst, dest_buf, sizeof(dest_buf)), dport);
    }
#endif
    if (dst == cfg_.vip.ip_be && dport == cfg_.vip.port) {
        return handle_forward(&view, now_ms);
    }
    if (dst == cfg_.gateway_ip_be) {
        return handle_reverse(&view, now_ms);
    }
#if VGW_IO_TRACE
    if (tr) {
        char dest_buf[INET_ADDRSTRLEN];
        VGW_IO_LOG_WARN("drop: not vip/gw dst={}:{}",
                        ip_str(dst, dest_buf, sizeof(dest_buf)), dport);
    }
#endif
    return HandleOutcome{};
}

}  // namespace forward
}  // namespace vgw
