#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

extern "C" {
#include <generic/rte_spinlock.h>
#include <rte_ether.h>
}

namespace vgm {

using vgm_mutex_t = std::mutex;
using vgm_cond_t = std::condition_variable;
using vgm_thread_t = std::thread;

using mac_addr_t = struct rte_ether_addr;
using spinlock_t = rte_spinlock_t;

using timeval_s = uint32_t;
using timeval_ms = uint32_t;
using timeval_us = uint32_t;
using timeval_min = uint32_t;
using timeval_h = uint32_t;

using vgm_clock_t = std::chrono::time_point<std::chrono::high_resolution_clock>;
using vgm_duration_t = std::chrono::duration<uint32_t>;

using vgm_hash_fnv_t = uint32_t;
using vgm_hash_crc32_t = uint32_t;
using vgm_hash_std_t = uint32_t;

}  // namespace vgm

