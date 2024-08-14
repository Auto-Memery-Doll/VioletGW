#ifndef FLOW_GATEWAY_TYPE_HPP
#define FLOW_GATEWAY_TYPE_HPP


#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

extern "C" {
#include <generic/rte_spinlock.h>
#include <rte_ether.h>
}

namespace fg {

using fg_mutex_t = std::mutex;
using fg_cond_t = std::condition_variable;
using fg_thread_t = std::thread;

using pbuf_iter = pbuf*;

using mac_addr_t = struct rte_ether_addr;

using spinlock_t = rte_spinlock_t;

//using LwipNetif = struct netif;
using lwip_netif_t = struct netif*;

using timeval_s = uint32_t;
using timeval_ms = uint32_t;
using timeval_us = uint32_t;
using timeval_min = uint32_t;
using timeval_h = uint32_t;

using fg_clock_t = std::chrono::time_point<std::chrono::high_resolution_clock>;
using fg_duration_t = std::chrono::duration<uint32_t>;

using fg_hash_fnv_t = uint32_t;
using fg_hash_crc32_t = uint32_t;
using fg_hash_std_t = uint32_t;

}   // fg

#endif // !FLOW_GATEWAY_TYPE_HPP