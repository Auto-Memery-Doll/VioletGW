#include "cp_shm.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace vgw {
namespace control {
namespace {

constexpr size_t kShmBytes = sizeof(vgw_cp_shm);

uint32_t load_version(const vgw_cp_shm* hdr) {
    return __atomic_load_n(&hdr->version, __ATOMIC_ACQUIRE);
}

void store_version(vgw_cp_shm* hdr, uint32_t v) {
    __atomic_store_n(&hdr->version, v, __ATOMIC_RELEASE);
}

}  // namespace

CpShm::CpShm(std::string name, int fd, vgw_cp_shm* hdr)
    : name_(std::move(name))
    , fd_(fd)
    , hdr_(hdr) {}

CpShm::~CpShm() {
    if (hdr_ != nullptr) {
        munmap(hdr_, kShmBytes);
        hdr_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    // Do not shm_unlink here: the segment is shared with the control plane.
}

void CpShm::unlink_name(const std::string& name) {
    shm_unlink(name.c_str());
}

std::unique_ptr<CpShm> CpShm::open(const std::string& name,
                                   bool create_if_missing,
                                   const CpShmSeed* seed) {
    int fd = shm_open(name.c_str(), O_RDWR, 0660);
    bool created = false;
    if (fd < 0 && create_if_missing && errno == ENOENT) {
        fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0660);
        if (fd < 0) {
            return nullptr;
        }
        created = true;
        if (ftruncate(fd, static_cast<off_t>(kShmBytes)) != 0) {
            close(fd);
            shm_unlink(name.c_str());
            return nullptr;
        }
    }
    if (fd < 0) {
        return nullptr;
    }

    void* mem = mmap(nullptr, kShmBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        close(fd);
        if (created) {
            shm_unlink(name.c_str());
        }
        return nullptr;
    }

    auto* hdr = static_cast<vgw_cp_shm*>(mem);
    auto shm = std::unique_ptr<CpShm>(new CpShm(name, fd, hdr));

    if (created) {
        std::memset(hdr, 0, kShmBytes);
        hdr->magic = VGW_CP_SHM_MAGIC;
        if (seed != nullptr) {
            shm->write_seed_unlocked(*seed, /*version=*/1);
        } else {
            store_version(hdr, 0);
        }
    } else if (hdr->magic != VGW_CP_SHM_MAGIC) {
        return nullptr;
    }

    return shm;
}

void CpShm::write_seed_unlocked(const CpShmSeed& seed, uint32_t version) {
    uint16_t n = static_cast<uint16_t>(seed.endpoints.size());
    if (n > VGW_CP_SHM_MAX_EP) {
        n = VGW_CP_SHM_MAX_EP;
    }
    hdr_->policy = static_cast<uint8_t>(seed.policy);
    hdr_->flags = 0;
    hdr_->count = n;
    for (uint16_t i = 0; i < n; ++i) {
        hdr_->endpoints[i].ip_be = seed.endpoints[i].ip_be;
        hdr_->endpoints[i].port = seed.endpoints[i].port;
        hdr_->endpoints[i]._pad = 0;
    }
    for (uint16_t i = n; i < VGW_CP_SHM_MAX_EP; ++i) {
        hdr_->endpoints[i] = {};
    }
    store_version(hdr_, version);
}

void CpShm::publish(const CpShmSeed& seed) {
    const uint32_t next = load_version(hdr_) + 1;
    write_seed_unlocked(seed, next == 0 ? 1 : next);
}

bool CpShm::poll_apply(upstream::UpstreamTable* table) {
    if (table == nullptr || hdr_ == nullptr) {
        return false;
    }
    if (hdr_->magic != VGW_CP_SHM_MAGIC) {
        return false;
    }

    const uint32_t ver = load_version(hdr_);
    if (ver == 0 || ver == last_applied_) {
        return false;
    }

    const uint8_t policy = hdr_->policy;
    uint16_t count = hdr_->count;
    if (count > VGW_CP_SHM_MAX_EP) {
        count = VGW_CP_SHM_MAX_EP;
    }

    std::vector<upstream::UpstreamEndpoint> eps;
    eps.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        upstream::UpstreamEndpoint e;
        e.ip_be = hdr_->endpoints[i].ip_be;
        e.port = hdr_->endpoints[i].port;
        eps.push_back(e);
    }

    // Re-check version after copy; if changed mid-read, skip and retry next poll.
    if (load_version(hdr_) != ver) {
        return false;
    }

    if (policy == VGW_CP_POLICY_RR) {
        table->set_policy(upstream::BalancePolicy::rr);
    } else {
        table->set_policy(upstream::BalancePolicy::mod);
    }
    table->set(std::move(eps));
    last_applied_ = ver;
    return true;
}

}  // namespace control
}  // namespace vgw
