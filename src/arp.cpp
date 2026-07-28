#include "arp.hpp"

#include "dpdk/netif.hpp"
#include "packet.hpp"

#include <cstring>
#include <netinet/in.h>
#include <rte_arp.h>
#include <rte_byteorder.h>
#include <rte_mbuf.h>
#include <spdlog/spdlog.h>

namespace vgw {
namespace arp {

namespace {

constexpr uint16_t kArpFrameLen =
    static_cast<uint16_t>(sizeof(rte_ether_hdr) + sizeof(rte_arp_hdr));

void zero_mac(rte_ether_addr* mac) {
    std::memset(mac->addr_bytes, 0, RTE_ETHER_ADDR_LEN);
}

bool is_zero_mac(const rte_ether_addr& mac) {
    for (unsigned i = 0; i < RTE_ETHER_ADDR_LEN; ++i) {
        if (mac.addr_bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

void fill_arp_hdr(rte_arp_hdr* arp) {
    arp->arp_hardware = rte_cpu_to_be_16(RTE_ARP_HRD_ETHER);
    arp->arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    arp->arp_hlen = RTE_ETHER_ADDR_LEN;
    arp->arp_plen = 4;
}

const char* ip_str(uint32_t ip_be, char* buf, size_t n) {
    if (inet_ntop(AF_INET, &ip_be, buf, static_cast<socklen_t>(n)) == nullptr) {
        std::snprintf(buf, n, "?");
    }
    return buf;
}

}  // namespace

ArpHandler::ArpHandler(ArpConfig cfg, neighbor::NeighborTable* neighbors)
    : cfg_(cfg)
    , neighbors_(neighbors) {}

bool ArpHandler::is_proxy_target(uint32_t ip_be) const {
    return ip_be == cfg_.gateway_ip_be || ip_be == cfg_.vip_ip_be;
}

forward::HandleResult ArpHandler::make_reply(rte_mbuf* req,
                                             rte_ether_hdr* eth,
                                             rte_arp_hdr* arp,
                                             uint32_t reply_ip_be) {
    rte_ether_addr_copy(&eth->src_addr, &eth->dst_addr);
    rte_ether_addr_copy(&cfg_.gateway_mac, &eth->src_addr);

    arp->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REPLY);
    rte_ether_addr_copy(&arp->arp_data.arp_sha, &arp->arp_data.arp_tha);
    rte_ether_addr_copy(&cfg_.gateway_mac, &arp->arp_data.arp_sha);
    const rte_be32_t sender_ip = arp->arp_data.arp_sip;
    arp->arp_data.arp_sip = reply_ip_be;
    arp->arp_data.arp_tip = sender_ip;

    req->pkt_len = kArpFrameLen;
    req->data_len = kArpFrameLen;
    return forward::HandleResult::tx_arp;
}

forward::HandleResult ArpHandler::handle_request(rte_mbuf* m,
                                                 rte_ether_hdr* eth,
                                                 rte_arp_hdr* arp) {
    const uint32_t target_ip_be = arp->arp_data.arp_tip;
    if (!is_proxy_target(target_ip_be)) {
        return forward::HandleResult::drop;
    }
    return make_reply(m, eth, arp, target_ip_be);
}

forward::HandleResult ArpHandler::handle_reply(rte_mbuf* /*m*/,
                                               rte_arp_hdr* arp,
                                               uint64_t now_ms,
                                               uint32_t* learned_ip_be) {
    if (neighbors_ == nullptr) {
        return forward::HandleResult::drop;
    }
    const uint32_t ip_be = arp->arp_data.arp_sip;
    if (ip_be == 0 || is_zero_mac(arp->arp_data.arp_sha)) {
        return forward::HandleResult::drop;
    }
    neighbors_->learn(ip_be, arp->arp_data.arp_sha, now_ms);
    if (learned_ip_be != nullptr) {
        *learned_ip_be = ip_be;
    }
    char ipbuf[INET_ADDRSTRLEN];
    SPDLOG_DEBUG("arp learn: {} from reply",
                 ip_str(ip_be, ipbuf, sizeof(ipbuf)));
    return forward::HandleResult::drop;
}

ArpOutcome ArpHandler::handle(rte_mbuf* m, uint64_t now_ms) {
    ArpOutcome out;
    rte_ether_hdr* eth = nullptr;
    rte_arp_hdr* arp = nullptr;
    if (packet::parse_arp(m, &eth, &arp) != packet::ParseStatus::ok) {
        return out;
    }

    const uint16_t opcode = rte_be_to_cpu_16(arp->arp_opcode);
    if (opcode == RTE_ARP_OP_REQUEST) {
        out.result = handle_request(m, eth, arp);
        return out;
    }
    if (opcode == RTE_ARP_OP_REPLY) {
        out.result = handle_reply(m, arp, now_ms, &out.learned_ip_be);
        return out;
    }
    return out;
}

rte_mbuf* ArpHandler::make_request(uint32_t target_ip_be) const {
    rte_mbuf* m = rte_pktmbuf_alloc(dpdk::DPDK_mempool);
    if (m == nullptr) {
        return nullptr;
    }

    if (rte_pktmbuf_append(m, kArpFrameLen) == nullptr) {
        rte_pktmbuf_free(m);
        return nullptr;
    }

    auto* eth = rte_pktmbuf_mtod(m, rte_ether_hdr*);
    std::memset(eth, 0, sizeof(*eth));
    rte_ether_addr_copy(&cfg_.gateway_mac, &eth->src_addr);
    eth->dst_addr.addr_bytes[0] = 0xff;
    eth->dst_addr.addr_bytes[1] = 0xff;
    eth->dst_addr.addr_bytes[2] = 0xff;
    eth->dst_addr.addr_bytes[3] = 0xff;
    eth->dst_addr.addr_bytes[4] = 0xff;
    eth->dst_addr.addr_bytes[5] = 0xff;
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP);

    auto* arp = reinterpret_cast<rte_arp_hdr*>(eth + 1);
    std::memset(arp, 0, sizeof(*arp));
    fill_arp_hdr(arp);
    arp->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REQUEST);
    rte_ether_addr_copy(&cfg_.gateway_mac, &arp->arp_data.arp_sha);
    arp->arp_data.arp_sip = cfg_.gateway_ip_be;
    zero_mac(&arp->arp_data.arp_tha);
    arp->arp_data.arp_tip = target_ip_be;

    m->pkt_len = kArpFrameLen;
    m->data_len = kArpFrameLen;
    return m;
}

}  // namespace arp
}  // namespace vgw
