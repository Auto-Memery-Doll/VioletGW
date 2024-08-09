#ifndef FLOW_GATEWAY_DPDK_NETIF_HPP
#define FLOW_GATEWAY_DPDK_NETIF_HPP

#include "base/closure.hpp"
#include "base/noncopyable.hpp"
#include "base/ring.hpp"
#include "base/singleton.hpp"
#include "base/type.hpp"
#include "lwip/pbuf.h"
#include "base/type.hpp"
#include <cstdint>
#include <map>
#include <rte_ether.h>
#include <rte_mbuf.h>
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

inline rte_mbuf* get_mbuf(bool pbuf_to = false);

}   // dpdk
}   // fg


namespace fg {

// usage:
// mbuf _mbuf = (mbuf*)_rte_mbuf
class mbuf : public rte_mbuf, public base::Closure {
public:
    // rte_mbuf -> mbuf don't need to |delete this|
    void *Run() override {
        rte_pktmbuf_free(this);
    }
    mbuf() = delete;
    ~mbuf() = default;
};

class DpdkNetif;
class MbufPbufAdapter : public base::Closure{
    friend class DpdkNetif; 
public:
    enum class Type : char {
        mbuf_to_pbuf,
        pbuf_to_mbuf,
    };

    MbufPbufAdapter(Type type, void* buf);
    ~MbufPbufAdapter() = default;

    /** 异步函数 */
    void* Run() override;
    /** 该数据包是否需要转发 */
    inline void forward() { _forward = true; };

private:
    static pbuf * mbuf_to_pbuf(void *buf, void* done);
    static mbuf * pbuf_to_mbuf(void *buf, void* done);

private:
    MbufPbufAdapter *next;
    mbuf*       _mbuf;
    pbuf*       _pbuf;
    Type        _type;
    bool        _forward = false;

    // for memory safe[pbuf to mbuf]
    void*       _mbuf_buf_addr;
    uint16_t    _mbuf_data_off;
};

class DpdkNetifManager;
// 通过dpdk虚拟出一个网卡
class DpdkNetif : public base::noncopyable {
    friend class DpdkNetifManager;
public:
    using ptr = std::shared_ptr<DpdkNetif>;
    ~DpdkNetif();

    /* 提供两个接口供lwip协议栈调用 */
    auto netif_rx() -> pbuf*;
    auto netif_tx(pbuf *pbuf_chain) -> void;

    /** 向网络中发送和接收数据 */
    auto netif_send() -> void;
    auto netif_recv() -> void;

private:
    DpdkNetif() = default;
    void init(int port);
    static void* run_recv(void *arg);
    static void* run_send(void *arg);

private:
    uint16_t port_id;
    Ring<rte_mbuf>::ptr _rx_ring;
    Ring<MbufPbufAdapter>::ptr _tx_ring;
    bool stop = false;
};

/** vnetif的管理类 */
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