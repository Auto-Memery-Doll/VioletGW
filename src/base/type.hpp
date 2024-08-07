#ifndef FLOW_GATEWAY_TYPE_HPP
#define FLOW_GATEWAY_TYPE_HPP


#include "lwip/pbuf.h"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace fg {

using fg_mutex_t = std::mutex;
using fg_cond_t = std::condition_variable;
using fg_thread_t = std::thread;

using pbuf_iter = pbuf*;

using mac_addr_t = char[6];

}

#endif // !FLOW_GATEWAY_TYPE_HPP