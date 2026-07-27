#include "forward.hpp"

#include "packet.hpp"

#include <netinet/in.h>
#include <rte_byteorder.h>

namespace vgw {
namespace forward {

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
                     UpstreamPicker picker)
    : cfg_(cfg)
    , sessions_(sessions)
    , picker_(std::move(picker)) {}

HandleResult Forwarder::handle_forward(packet::PacketView* v, uint64_t now_ms) {
    const session::FlowKey key = flow_key_from_view(*v);
    session::Session* s = sessions_->lookup_forward(key);
    if (s == nullptr) {
        Upstream up;
        if (!picker_ || !picker_(key, &up) || up.ip_be == 0) {
            return HandleResult::drop;
        }
        s = sessions_->create(key, up.ip_be, up.port, now_ms);
        if (s == nullptr) {
            return HandleResult::drop;
        }
    } else {
        sessions_->touch(s, now_ms);
    }

    apply_forward_nat(v, *s, cfg_.gateway_ip_be, cfg_.recompute_udp_checksum);
    set_l2(v, cfg_.gateway_mac, cfg_.upstream_mac);
    return HandleResult::tx_forward;
}

HandleResult Forwarder::handle_reverse(packet::PacketView* v, uint64_t now_ms) {
    const session::FlowKey key = flow_key_from_view(*v);
    session::Session* s = sessions_->lookup_reverse(key);
    if (s == nullptr) {
        return HandleResult::drop;
    }
    sessions_->touch(s, now_ms);
    apply_reverse_nat(v, *s, cfg_.recompute_udp_checksum);
    set_l2(v, cfg_.gateway_mac, cfg_.client_side_mac);
    return HandleResult::tx_reverse;
}

HandleResult Forwarder::handle(rte_mbuf* m, uint64_t now_ms) {
    packet::PacketView view;
    const packet::ParseStatus st =
        packet::parse_udp_ipv4(m, &view, cfg_.verify_checksum);
    if (st != packet::ParseStatus::ok) {
        return HandleResult::drop;
    }

    const uint32_t dst = view.dst_ip();
    const uint16_t dport = view.dst_port();

    if (dst == cfg_.vip.ip_be && dport == cfg_.vip.port) {
        return handle_forward(&view, now_ms);
    }
    if (dst == cfg_.gateway_ip_be) {
        return handle_reverse(&view, now_ms);
    }
    return HandleResult::drop;
}

}  // namespace forward
}  // namespace vgw
