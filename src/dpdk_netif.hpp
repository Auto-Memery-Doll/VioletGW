#ifndef FLOW_GATEWAY_DPDK_NETIF_HPP
#define FLOW_GATEWAY_DPDK_NETIF_HPP

#include "base/closure.hpp"
#include "base/netif.hpp"
#include "base/ring.hpp"
#include "base/singleton.hpp"
#include "base/util.hpp"
#include "lwip/pbuf.h"
#include <cstdint>
#include <map>
#include <rte_cycles.h>
#include <rte_ether.h>
#include <rte_mbuf.h>
#include <rte_mbuf_core.h>
#include <rte_mempool.h>
#include <memory>
#include <rte_timer.h>

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

class NetifDriver;
class DpdkNetifManager;
// 通过dpdk虚拟出一个网卡
class DpdkNetif : public Netif {
    friend class DpdkNetifManager;
    friend class NetifDriver;
public:
    ~DpdkNetif();

    /* 提供两个接口供lwip协议栈调用 */
    auto netif_rx() -> pbuf* override;
    auto netif_tx(pbuf *pbuf_chain) -> void override;

private:
    /** 向网络中发送和接收数据 */
    auto netif_send() -> void;
    auto netif_recv() -> void;


    DpdkNetif() = default;
    void init(int port);
    static int run_recv(void *arg);
    static int run_send(void *arg);

private:
    uint16_t port_id;
    Ring<rte_mbuf>::ptr _rx_ring;
    Ring<MbufPbufAdapter>::ptr _tx_ring;
    bool stop = false;
};

/** vnetif的管理类 */
class DpdkNetifManager : public base::Singletion<DpdkNetifManager> {
    friend class base::Singletion<DpdkNetifManager>;
    friend class NetifDriver;
public:
    using ptr = std::shared_ptr<DpdkNetifManager>;
    ~DpdkNetifManager();

    auto init() -> void;
    auto stop() -> void;
    auto get_netif(int port) -> DpdkNetif*;

private:
    DpdkNetifManager() = default;


private:
    // port:netif*
    std::map<int, DpdkNetif*> _netifs;
    util::SpinMutex _mtx; // 保证并发安全
};

inline auto dpdk_netif_mg() -> DpdkNetifManager::ptr {
    return DpdkNetifManager::GetInstance();   
}

}   // base

#endif // !FLOW_GATEWAY_DPDK_NETIF_HPP