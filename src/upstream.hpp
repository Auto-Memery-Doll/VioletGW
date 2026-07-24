#pragma once

#include "session.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace vgm {
namespace upstream {

struct UpstreamEndpoint {
    uint32_t ip_be = 0;
    uint16_t port = 0;
};

enum class BalancePolicy : uint8_t {
    mod = 0,
    rr = 1,
};

/**
 * Control-plane-published upstream membership + policy.
 * Hot-path pick() uses only atomic loads (no mutex).
 */
class UpstreamTable {
public:
    UpstreamTable();

    /** Whole-table replace (control plane). */
    void set(std::vector<UpstreamEndpoint> endpoints);

    void set_policy(BalancePolicy policy);

    /**
     * Select an upstream for a new flow. Lock-free.
     * @return false if the published list is empty or out is null
     */
    bool pick(const session::FlowKey& key, UpstreamEndpoint* out) const;

    size_t size() const;

private:
    using Snapshot = std::vector<UpstreamEndpoint>;

    static uint32_t hash_key(const session::FlowKey& key);

    std::atomic<BalancePolicy> policy_{BalancePolicy::mod};
    std::shared_ptr<const Snapshot> nodes_;
    mutable std::atomic<uint64_t> rr_counter_{0};
};

}  // namespace upstream
}  // namespace vgm

