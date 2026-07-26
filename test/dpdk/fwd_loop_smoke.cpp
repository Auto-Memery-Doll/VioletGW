#include "forward.hpp"
#include "mbuf_fixture.hpp"
#include "packet.hpp"
#include "session.hpp"

#include <cstdio>

#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

namespace {

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

    vgm::session::SessionTable sessions(vgm::bench::kGw);
    vgm::forward::ForwardConfig cfg;
    cfg.vip = {vgm::bench::kVip, vgm::bench::kVipPort};
    cfg.gateway_ip_be = vgm::bench::kGw;

    vgm::forward::Forwarder fwd(
        cfg, &sessions,
        [](const vgm::session::FlowKey&, vgm::forward::Upstream* up) {
            up->ip_be = vgm::bench::kUpstream;
            up->port = vgm::bench::kUpPort;
            return true;
        });

    rte_mbuf* m = rte_pktmbuf_alloc(pool);
    if (m == nullptr) {
        std::fprintf(stderr, "mbuf alloc failed\n");
        rte_eal_cleanup();
        return 1;
    }

    vgm::bench::fill_forward_mbuf(m, vgm::bench::kClientPortBase);

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
    ok &= expect_eq_u32("fwd src_ip", view.src_ip(), vgm::bench::kGw);
    ok &= expect_eq_u32("fwd dst_ip", view.dst_ip(), vgm::bench::kUpstream);
    ok &= expect_eq_u16("fwd dst_port", view.dst_port(), vgm::bench::kUpPort);
    const uint16_t snat = view.src_port();
    if (snat == 0) {
        std::fprintf(stderr, "FAIL snat_port is 0\n");
        ok = false;
    }

    vgm::bench::fill_reverse_mbuf(m, snat);

    if (fwd.handle(m, /*now_ms=*/2) != vgm::forward::HandleResult::tx_reverse) {
        std::fprintf(stderr, "FAIL reverse handle\n");
        ok = false;
    } else if (vgm::packet::parse_udp_ipv4(m, &view, false) !=
               vgm::packet::ParseStatus::ok) {
        std::fprintf(stderr, "FAIL parse after reverse\n");
        ok = false;
    } else {
        ok &= expect_eq_u32("rev src_ip", view.src_ip(), vgm::bench::kVip);
        ok &= expect_eq_u32("rev dst_ip", view.dst_ip(), vgm::bench::kClient);
        ok &= expect_eq_u16("rev src_port", view.src_port(), vgm::bench::kVipPort);
        ok &= expect_eq_u16("rev dst_port", view.dst_port(),
                            vgm::bench::kClientPortBase);
    }

    rte_pktmbuf_free(m);
    rte_eal_cleanup();

    if (!ok) {
        return 1;
    }
    std::printf("fwd_loop_smoke OK (forward+reverse on real mbuf)\n");
    return 0;
}
