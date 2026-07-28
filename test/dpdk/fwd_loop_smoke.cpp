#include "mbuf_fixture.hpp"
#include "packet.hpp"
#include "vgw.h"

#include "base/log.hpp"

#include <cstdio>
#include <string>
#include <unistd.h>

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
    vgw::init_logging();

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

    const std::string shm =
        std::string("/vgw_fwd_smoke_") + std::to_string(getpid());
    vgw::VioletGW gw;
    if (gw.init_services(shm.c_str()) != 0) {
        std::fprintf(stderr, "init_services failed\n");
        vgw::control::CpShm::unlink_name(shm);
        rte_eal_cleanup();
        return 1;
    }

    rte_mbuf* m = rte_pktmbuf_alloc(pool);
    if (m == nullptr) {
        std::fprintf(stderr, "mbuf alloc failed\n");
        vgw::control::CpShm::unlink_name(shm);
        rte_eal_cleanup();
        return 1;
    }

    vgw::bench::fill_forward_mbuf(m, vgw::bench::kClientPortBase);

    bool ok = true;
    if (gw.handle(m, /*now_ms=*/1).result !=
        vgw::forward::HandleResult::tx_forward) {
        std::fprintf(stderr, "FAIL forward handle\n");
        ok = false;
    }

    vgw::packet::PacketView view;
    if (ok && vgw::packet::parse_udp_ipv4(m, &view, false) !=
                  vgw::packet::ParseStatus::ok) {
        std::fprintf(stderr, "FAIL parse after forward\n");
        ok = false;
    }

    uint16_t snat = 0;
    if (ok) {
        ok &= expect_eq_u32("fwd src_ip", view.src_ip(), vgw::bench::kGw());
        ok &= expect_eq_u32("fwd dst_ip", view.dst_ip(), vgw::bench::kUpstream());
        ok &= expect_eq_u16("fwd dst_port", view.dst_port(), vgw::bench::kUpPort());
        snat = view.src_port();
        if (snat == 0) {
            std::fprintf(stderr, "FAIL snat_port is 0\n");
            ok = false;
        }
    }

    if (ok) {
        vgw::bench::fill_reverse_mbuf(m, snat);
        if (gw.handle(m, /*now_ms=*/2).result !=
            vgw::forward::HandleResult::tx_reverse) {
            std::fprintf(stderr, "FAIL reverse handle\n");
            ok = false;
        } else if (vgw::packet::parse_udp_ipv4(m, &view, false) !=
                   vgw::packet::ParseStatus::ok) {
            std::fprintf(stderr, "FAIL parse after reverse\n");
            ok = false;
        } else {
            ok &= expect_eq_u32("rev src_ip", view.src_ip(), vgw::bench::kVip());
            ok &= expect_eq_u32("rev dst_ip", view.dst_ip(), vgw::bench::kClient);
            ok &= expect_eq_u16("rev src_port", view.src_port(),
                                vgw::bench::kVipPort());
            ok &= expect_eq_u16("rev dst_port", view.dst_port(),
                                vgw::bench::kClientPortBase);
        }
    }

    rte_pktmbuf_free(m);
    vgw::control::CpShm::unlink_name(shm);
    rte_eal_cleanup();

    if (!ok) {
        return 1;
    }
    std::printf("fwd_loop_smoke OK (VioletGW forward+reverse on real mbuf)\n");
    return 0;
}
