#ifndef FLOW_GATEWAY_DPDK_NETIF_HPP
#define FLOW_GATEWAY_DPDK_NETIF_HPP

#include "base/noncopyable.hpp"
#include "base/type.hpp"
#include "lwip/pbuf.h"
#include <deque>

namespace fg {

// 通过dpdk虚拟出一个网卡
class DpdkNetif : public base::noncopyable {
public:

    /* 提供两个接口供lwip协议栈调用 */
    auto netif_output() -> pbuf*;
    void netif_input(pbuf *pbuf_chain);



private:
    void run();

private:
    
    fg_mutex_t _mutex;
    std::deque<pbuf*> _tx_queue;
    std::deque<pbuf*> _rx_queue;
    fg_thread_t _t; // 处理收发网络数据包
};

// dpdk网卡驱动程序，用于连接dpdk网卡和lwip协议栈
class DpdkNetifDriver : public base::noncopyable {

};

}   // base

#endif // !FLOW_GATEWAY_DPDK_NETIF_HPP