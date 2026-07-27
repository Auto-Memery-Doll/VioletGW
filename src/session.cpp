#include "session.hpp"

#include <algorithm>

namespace vgw {
namespace session {

SessionTable::SessionTable(uint32_t gateway_ip_be,
                           uint64_t idle_timeout_ms,
                           uint16_t snat_port_begin,
                           uint16_t snat_port_end)
    : gateway_ip_(gateway_ip_be)
    , idle_timeout_ms_(idle_timeout_ms)
    , snat_begin_(snat_port_begin)
    , snat_end_(snat_port_end)
    , snat_next_(snat_port_begin)
    , port_in_use_(65536, 0) {
    if (snat_end_ <= snat_begin_) {
        snat_end_ = static_cast<uint16_t>(snat_begin_ + 1);
    }
}

Session* SessionTable::lookup_forward(const FlowKey& key) {
    auto it = forward_.find(key);
    return it == forward_.end() ? nullptr : it->second;
}

Session* SessionTable::lookup_reverse(const FlowKey& key) {
    auto it = reverse_.find(key);
    return it == reverse_.end() ? nullptr : it->second;
}

void SessionTable::touch(Session* s, uint64_t now_ms) {
    if (s != nullptr) {
        s->last_active_ms = now_ms;
    }
}

uint16_t SessionTable::alloc_snat_port() {
    if (!free_ports_.empty()) {
        const uint16_t p = free_ports_.back();
        free_ports_.pop_back();
        port_in_use_[p] = 1;
        return p;
    }

    for (uint32_t tries = 0; tries < static_cast<uint32_t>(snat_end_ - snat_begin_);
         ++tries) {
        const uint16_t p = snat_next_;
        ++snat_next_;
        if (snat_next_ >= snat_end_) {
            snat_next_ = snat_begin_;
        }
        if (!port_in_use_[p]) {
            port_in_use_[p] = 1;
            return p;
        }
    }
    return 0;
}

void SessionTable::free_snat_port(uint16_t port) {
    if (port == 0 || port >= port_in_use_.size()) {
        return;
    }
    if (port_in_use_[port]) {
        port_in_use_[port] = 0;
        free_ports_.push_back(port);
    }
}

Session* SessionTable::create(const FlowKey& client_to_vip,
                              uint32_t upstream_ip_be,
                              uint16_t upstream_port,
                              uint64_t now_ms) {
    const uint16_t snat = alloc_snat_port();
    if (snat == 0) {
        return nullptr;
    }

    auto* s = new Session();
    s->forward_key = client_to_vip;
    s->client_ip = client_to_vip.src_ip;
    s->client_port = client_to_vip.src_port;
    s->vip_ip = client_to_vip.dst_ip;
    s->vip_port = client_to_vip.dst_port;
    s->upstream_ip = upstream_ip_be;
    s->upstream_port = upstream_port;
    s->snat_port = snat;
    s->last_active_ms = now_ms;

    s->reverse_key.src_ip = upstream_ip_be;
    s->reverse_key.dst_ip = gateway_ip_;
    s->reverse_key.src_port = upstream_port;
    s->reverse_key.dst_port = snat;
    s->reverse_key.proto = client_to_vip.proto;

    sessions_.push_back(s);
    forward_[s->forward_key] = s;
    reverse_[s->reverse_key] = s;
    return s;
}

void SessionTable::erase_session(Session* s) {
    if (s == nullptr) {
        return;
    }
    forward_.erase(s->forward_key);
    reverse_.erase(s->reverse_key);
    free_snat_port(s->snat_port);
    auto it = std::find(sessions_.begin(), sessions_.end(), s);
    if (it != sessions_.end()) {
        sessions_.erase(it);
    }
    delete s;
}

size_t SessionTable::expire(uint64_t now_ms) {
    size_t removed = 0;
    for (size_t i = 0; i < sessions_.size();) {
        Session* s = sessions_[i];
        if (now_ms - s->last_active_ms >= idle_timeout_ms_) {
            erase_session(s);
            ++removed;
            // erase_session removes from sessions_; do not increment i
        } else {
            ++i;
        }
    }
    return removed;
}

}  // namespace session
}  // namespace vgw
