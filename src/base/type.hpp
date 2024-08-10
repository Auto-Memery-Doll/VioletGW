#ifndef FLOW_GATEWAY_TYPE_HPP
#define FLOW_GATEWAY_TYPE_HPP


#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include <condition_variable>
#include <generic/rte_spinlock.h>
#include <mutex>
#include <rte_ether.h>
#include <thread>

namespace fg {

using fg_mutex_t = std::mutex;
using fg_cond_t = std::condition_variable;
using fg_thread_t = std::thread;

using pbuf_iter = pbuf*;

using mac_addr_t = struct rte_ether_addr;

using spinlock_t = rte_spinlock_t;

//using LwipNetif = struct netif;
using lwip_netif_t = struct netif*;

}

#endif // !FLOW_GATEWAY_TYPE_HPP