#ifndef FLOW_GATEWAY_DPDK_NETIF_HPP
#define FLOW_GATEWAY_DPDK_NETIF_HPP

#include "base/noncopyable.hpp"
#include "base/singleton.hpp"
#include "base/type.hpp"
#include "lwip/pbuf.h"
#include "base/type.hpp"
#include <cstdint>
#include <deque>
#include <map>
#include <rte_ether.h>
#include <rte_mbuf_core.h>
#include <rte_mempool.h>
#include <memory>

namespace fg {
namespace dpdk {

extern struct rte_mempool *DPDK_mempool;
extern struct rte_ether_addr DPDK_ether_addr[];

void init(int argc, char *argv[]);

inline int rx_burst(
    uint16_t port_id, uint16_t queue_id, struct rte_mbuf **rx_pkts, uint16_t nb_pkts);

inline int tx_burst(
    uint16_t port_id, uint16_t queue_id, struct rte_mbuf **tx_pkts, uint16_t nb_pkts);

inline rte_mbuf* get_mbuf();

}   // dpdk
}   // fg


namespace fg {

struct DpdkNetifInfo {
    char mac_addr[6];
};


class DpdkNetifManager;
// 通过dpdk虚拟出一个网卡
class DpdkNetif : public base::noncopyable {
    friend class DpdkNetifManager;
public:
    using ptr = std::shared_ptr<DpdkNetif>;
    ~DpdkNetif();

    /* 提供两个接口供lwip协议栈调用 */
    auto netif_output() -> pbuf*;
    void netif_input(pbuf *pbuf_chain);



private:
    DpdkNetif() = default;
    void init();
    void run();

private:
    
    fg_mutex_t _mutex;
    std::deque<pbuf*> _tx_queue;
    std::deque<pbuf*> _rx_queue;
    fg_thread_t _t; // 处理收发网络数据包
};


class DpdkNetifManager : public base::Singletion<DpdkNetifManager> {
    friend base::Singletion<DpdkNetifManager>;
public:
    using ptr = std::shared_ptr<DpdkNetifManager>;

    auto get_dpdk_netif(mac_addr_t mac) -> DpdkNetif::ptr;

    

private:
    std::map<int64_t, DpdkNetif::ptr> _netifs;

};

inline auto dpdk_netif_mg() -> DpdkNetifManager::ptr {
    return DpdkNetifManager::GetInstance();   
}


// dpdk网卡驱动程序，用于连接dpdk网卡和lwip协议栈
class DpdkNetifDriver : public base::noncopyable {

};

}   // base

#endif // !FLOW_GATEWAY_DPDK_NETIF_HPP