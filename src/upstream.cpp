#include "upstream.hpp"

namespace vgw {
namespace upstream {

UpstreamTable::UpstreamTable()
    : nodes_(std::make_shared<const Snapshot>()) {}

void UpstreamTable::set(std::vector<UpstreamEndpoint> endpoints) {
    auto next = std::make_shared<const Snapshot>(std::move(endpoints));
    std::atomic_store_explicit(&nodes_, next, std::memory_order_release);
}

void UpstreamTable::set_policy(BalancePolicy policy) {
    policy_.store(policy, std::memory_order_relaxed);
}

bool UpstreamTable::pick(const session::FlowKey& key,
                         UpstreamEndpoint* out) const {
    if (out == nullptr) {
        return false;
    }

    const auto snap =
        std::atomic_load_explicit(&nodes_, std::memory_order_acquire);
    if (!snap || snap->empty()) {
        return false;
    }

    const size_t n = snap->size();
    size_t idx = 0;
    const BalancePolicy policy = policy_.load(std::memory_order_relaxed);
    switch (policy) {
    case BalancePolicy::rr:
        idx = static_cast<size_t>(rr_counter_.fetch_add(1, std::memory_order_relaxed) %
                                  n);
        break;
    case BalancePolicy::mod:
    default:
        idx = static_cast<size_t>(hash_key(key) % n);
        break;
    }

    *out = (*snap)[idx];
    return true;
}

size_t UpstreamTable::size() const {
    const auto snap =
        std::atomic_load_explicit(&nodes_, std::memory_order_acquire);
    return snap ? snap->size() : 0;
}

uint32_t UpstreamTable::hash_key(const session::FlowKey& key) {
    uint32_t h = key.src_ip;
    h ^= static_cast<uint32_t>(key.src_port) * 0x9e3779b9u;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    return h;
}

}  // namespace upstream
}  // namespace vgw
