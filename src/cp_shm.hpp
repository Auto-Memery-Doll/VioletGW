#pragma once

#include "vg_cp_shm.h"
#include "upstream.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vgm {
namespace control {

struct CpShmSeed {
    upstream::BalancePolicy policy = upstream::BalancePolicy::mod;
    std::vector<upstream::UpstreamEndpoint> endpoints;
};

/**
 * POSIX SHM view of vg_cp_shm. Control plane publishes by writing the block
 * and bumping version; data plane polls and applies into UpstreamTable.
 */
class CpShm {
public:
    ~CpShm();

    CpShm(const CpShm&) = delete;
    CpShm& operator=(const CpShm&) = delete;

    /**
     * Attach existing segment, or create+seed if missing and create_if_missing.
     * @return nullptr on failure
     */
    static std::unique_ptr<CpShm> open(const std::string& name,
                                      bool create_if_missing,
                                      const CpShmSeed* seed);

    /** If version advanced, copy into table. Returns true if applied. */
    bool poll_apply(upstream::UpstreamTable* table);

    /** Test / writer helper: publish a full config (bumps version). */
    void publish(const CpShmSeed& seed);

    uint32_t last_applied_version() const { return last_applied_; }
    vg_cp_shm* raw() { return hdr_; }
    const vg_cp_shm* raw() const { return hdr_; }
    const std::string& name() const { return name_; }

    /** Remove a POSIX SHM object by name (tests / cleanup). */
    static void unlink_name(const std::string& name);

private:
    CpShm(std::string name, int fd, vg_cp_shm* hdr);

    void write_seed_unlocked(const CpShmSeed& seed, uint32_t version);

    std::string name_;
    int fd_ = -1;
    vg_cp_shm* hdr_ = nullptr;
    uint32_t last_applied_ = 0;
};

}  // namespace control
}  // namespace vgm

