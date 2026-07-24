#include "forward.hpp"
#include "packet.hpp"
#include "session.hpp"

#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <rte_eal.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_udp.h>

namespace {

constexpr uint32_t kClient = RTE_IPV4(10, 0, 0, 1);
constexpr uint32_t kVip = RTE_IPV4(192, 168, 1, 100);
constexpr uint32_t kGw = RTE_IPV4(192, 168, 1, 10);
constexpr uint32_t kUpstream = RTE_IPV4(10, 1, 0, 2);
constexpr uint16_t kVipPort = 53;
constexpr uint16_t kClientPort = 4000;
constexpr uint16_t kUpPort = 53;

uint16_t frame_len() {
    return static_cast<uint16_t>(sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr) +
                                 sizeof(rte_udp_hdr) + 4);
}

void fill_udp(uint8_t* buf,
              uint32_t sip,
              uint16_t sport,
              uint32_t dip,
              uint16_t dport) {
    std::memset(buf, 0, frame_len());
    auto* eth = reinterpret_cast<rte_ether_hdr*>(buf);
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    auto* ip = reinterpret_cast<rte_ipv4_hdr*>(buf + sizeof(rte_ether_hdr));
    ip->version_ihl = 0x45;
    ip->total_length = rte_cpu_to_be_16(
        static_cast<uint16_t>(sizeof(rte_ipv4_hdr) + sizeof(rte_udp_hdr) + 4));
    ip->time_to_live = 64;
    ip->next_proto_id = IPPROTO_UDP;
    ip->src_addr = sip;
    ip->dst_addr = dip;
    vgm::packet::refresh_ipv4_checksum(ip);

    auto* udp = reinterpret_cast<rte_udp_hdr*>(
        buf + sizeof(rte_ether_hdr) + sizeof(rte_ipv4_hdr));
    udp->src_port = rte_cpu_to_be_16(sport);
    udp->dst_port = rte_cpu_to_be_16(dport);
    udp->dgram_len = rte_cpu_to_be_16(sizeof(rte_udp_hdr) + 4);
    std::memcpy(reinterpret_cast<uint8_t*>(udp) + sizeof(rte_udp_hdr), "ping",
                4);
    vgm::packet::clear_udp_checksum(udp);
}

bool expect_eq_u32(const char* what, uint32_t got, uint32_t want) {
    if (got != want) {
        std::fprintf(stderr, "FAIL %s: got=0x%08x want=0x%08x\n", what, got,
                     want);
        return false;
    }
    return true;
}

bool expect_eq_u16(const char* what, uint16_t got, uint16_t want) {
    if (got != want) {
        std::fprintf(stderr, "FAIL %s: got=%u want=%u\n", what, got, want);
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        std::fprintf(stderr, "rte_eal_init failed\n");
        return 1;
    }

    rte_mempool* pool = rte_pktmbuf_pool_create(
        "fwd_loop_pool", 256, 0, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (pool == nullptr) {
        std::fprintf(stderr, "mempool create failed\n");
        rte_eal_cleanup();
        return 1;
    }

    vgm::session::SessionTable sessions(kGw);
    vgm::forward::ForwardConfig cfg;
    cfg.vip = {kVip, kVipPort};
    cfg.gateway_ip_be = kGw;

    vgm::forward::Forwarder fwd(
        cfg, &sessions,
        [](const vgm::session::FlowKey&, vgm::forward::Upstream* up) {
            up->ip_be = kUpstream;
            up->port = kUpPort;
            return true;
        });

    rte_mbuf* m = rte_pktmbuf_alloc(pool);
    if (m == nullptr) {
        std::fprintf(stderr, "mbuf alloc failed\n");
        rte_eal_cleanup();
        return 1;
    }

    uint8_t* data = rte_pktmbuf_mtod(m, uint8_t*);
    fill_udp(data, kClient, kClientPort, kVip, kVipPort);
    m->data_len = frame_len();
    m->pkt_len = frame_len();

    if (fwd.handle(m, /*now_ms=*/1) != vgm::forward::HandleResult::tx_forward) {
        std::fprintf(stderr, "FAIL forward handle\n");
        rte_pktmbuf_free(m);
        rte_eal_cleanup();
        return 1;
    }

    vgm::packet::PacketView view;
    if (vgm::packet::parse_udp_ipv4(m, &view, false) !=
        vgm::packet::ParseStatus::ok) {
        std::fprintf(stderr, "FAIL parse after forward\n");
        rte_pktmbuf_free(m);
        rte_eal_cleanup();
        return 1;
    }

    bool ok = true;
    ok &= expect_eq_u32("fwd src_ip", view.src_ip(), kGw);
    ok &= expect_eq_u32("fwd dst_ip", view.dst_ip(), kUpstream);
    ok &= expect_eq_u16("fwd dst_port", view.dst_port(), kUpPort);
    const uint16_t snat = view.src_port();
    if (snat == 0) {
        std::fprintf(stderr, "FAIL snat_port is 0\n");
        ok = false;
    }

    // Reverse: upstream -> gw:snat
    fill_udp(data, kUpstream, kUpPort, kGw, snat);
    m->data_len = frame_len();
    m->pkt_len = frame_len();

    if (fwd.handle(m, /*now_ms=*/2) != vgm::forward::HandleResult::tx_reverse) {
        std::fprintf(stderr, "FAIL reverse handle\n");
        ok = false;
    } else if (vgm::packet::parse_udp_ipv4(m, &view, false) !=
               vgm::packet::ParseStatus::ok) {
        std::fprintf(stderr, "FAIL parse after reverse\n");
        ok = false;
    } else {
        ok &= expect_eq_u32("rev src_ip", view.src_ip(), kVip);
        ok &= expect_eq_u32("rev dst_ip", view.dst_ip(), kClient);
        ok &= expect_eq_u16("rev src_port", view.src_port(), kVipPort);
        ok &= expect_eq_u16("rev dst_port", view.dst_port(), kClientPort);
    }

    rte_pktmbuf_free(m);
    rte_eal_cleanup();

    if (!ok) {
        return 1;
    }
    std::printf("fwd_loop_smoke OK (forward+reverse on real mbuf)\n");
    return 0;
}
