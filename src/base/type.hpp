#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

// DPDK headers manage their own C++ linkage. Do not wrap them in an outer
// extern "C" — that breaks DPDK 25.x bitops C++ overloads (rte_bitops.h).
#include <generic/rte_spinlock.h>
#include <rte_ether.h>

namespace vgw {

using vgw_mutex_t = std::mutex;
using vgw_cond_t = std::condition_variable;
using vgw_thread_t = std::thread;

using mac_addr_t = struct rte_ether_addr;
using spinlock_t = rte_spinlock_t;

using timeval_s = uint32_t;
using timeval_ms = uint32_t;
using timeval_us = uint32_t;
using timeval_min = uint32_t;
using timeval_h = uint32_t;

using vgw_clock_t = std::chrono::time_point<std::chrono::high_resolution_clock>;
using vgw_duration_t = std::chrono::duration<uint32_t>;

using vgw_hash_fnv_t = uint32_t;
using vgw_hash_crc32_t = uint32_t;
using vgw_hash_std_t = uint32_t;

}  // namespace vgw

