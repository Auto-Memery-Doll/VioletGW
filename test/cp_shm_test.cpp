#include "cp_shm.hpp"
#include "upstream.hpp"

#include <gtest/gtest.h>
#include <rte_ip.h>
#include <string>
#include <unistd.h>

using vgm::control::CpShm;
using vgm::control::CpShmSeed;
using vgm::upstream::BalancePolicy;
using vgm::upstream::UpstreamEndpoint;
using vgm::upstream::UpstreamTable;

namespace {

std::string test_shm_name() {
    return std::string("/vg_cp_test_") + std::to_string(getpid()) + "_" +
           std::to_string(reinterpret_cast<uintptr_t>(&test_shm_name));
}

}  // namespace

TEST(CpShmTest, CreateSeedAndPollApply) {
    const std::string name = test_shm_name() + "_seed";
    CpShm::unlink_name(name);

    CpShmSeed seed;
    seed.policy = BalancePolicy::mod;
    seed.endpoints = {{RTE_IPV4(10, 1, 0, 2), 53}};

    auto shm = CpShm::open(name, /*create_if_missing=*/true, &seed);
    ASSERT_NE(shm, nullptr);
    ASSERT_NE(shm->raw(), nullptr);
    EXPECT_EQ(shm->raw()->magic, VG_CP_SHM_MAGIC);
    EXPECT_EQ(shm->raw()->count, 1);

    UpstreamTable table;
    EXPECT_TRUE(shm->poll_apply(&table));
    EXPECT_EQ(table.size(), 1u);
    EXPECT_FALSE(shm->poll_apply(&table));  // same version

    UpstreamEndpoint out;
    vgm::session::FlowKey key{};
    key.src_ip = RTE_IPV4(10, 0, 0, 1);
    key.src_port = 1;
    ASSERT_TRUE(table.pick(key, &out));
    EXPECT_EQ(out.ip_be, RTE_IPV4(10, 1, 0, 2));
    EXPECT_EQ(out.port, 53);

    shm.reset();
    CpShm::unlink_name(name);
}

TEST(CpShmTest, PublishBumpsVersionAndUpdatesTable) {
    const std::string name = test_shm_name() + "_pub";
    CpShm::unlink_name(name);

    CpShmSeed seed;
    seed.policy = BalancePolicy::mod;
    seed.endpoints = {{RTE_IPV4(10, 1, 0, 2), 53}};
    auto shm = CpShm::open(name, true, &seed);
    ASSERT_NE(shm, nullptr);

    UpstreamTable table;
    ASSERT_TRUE(shm->poll_apply(&table));

    CpShmSeed next;
    next.policy = BalancePolicy::rr;
    next.endpoints = {{RTE_IPV4(10, 9, 0, 9), 9090},
                      {RTE_IPV4(10, 9, 0, 8), 9090}};
    shm->publish(next);

    EXPECT_TRUE(shm->poll_apply(&table));
    EXPECT_EQ(table.size(), 2u);

    UpstreamEndpoint out;
    vgm::session::FlowKey key{};
    key.src_ip = RTE_IPV4(1, 2, 3, 4);
    key.src_port = 99;
    ASSERT_TRUE(table.pick(key, &out));
    EXPECT_TRUE(out.ip_be == RTE_IPV4(10, 9, 0, 9) ||
                out.ip_be == RTE_IPV4(10, 9, 0, 8));
    EXPECT_EQ(out.port, 9090);

    shm.reset();
    CpShm::unlink_name(name);
}
