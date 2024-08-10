#include "netif_driver.hpp"
#include "base/closure.hpp"
#include "base/netif.hpp"
#include "dpdk_netif.hpp"
#include "lwip/err.h"
#include "lwip/etharp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "base/singleton.hpp"
#include "base/type.hpp"
#include "base/util.hpp"
#include "config.hpp"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <rte_ether.h>

namespace fg {
/** 端口对应的mac地址，用来初始化lwip的抽象网卡接口 */
namespace dpdk {
extern struct rte_ether_addr DPDK_ether_addr[];
}

struct LwipNetif : public netif {
    int port;
};

class LwipNetifManager : public base::Singletion<LwipNetifManager> {
    friend class base::Singletion<LwipNetifManager>;
    friend class NetifDriver;
public:
    using ptr = std::shared_ptr<LwipNetifManager>;
    ~LwipNetifManager() {
        for (auto &entry : _netifs) {
            delete entry.second;
        }
    }
    
    void inti() {
        /** 查看配置，使用了哪些端口 */
        uint32_t port_mask = config::DPDK_vaild_port_marks;

        for (int i = 0; i < 32; ++ i) {
            if (!(port_mask & (1 << i))) 
                continue;
            
            LwipNetif *netif_ = new LwipNetif;
            netif_->port = i;

            lwip_netif_t l_netif = netif_;
            assert(l_netif != nullptr);
            
            /** 配置网卡信息 */
            l_netif->hwaddr_len = NETIF_MAX_HWADDR_LEN;

            l_netif->hwaddr[0] = dpdk::DPDK_ether_addr[i].addr_bytes[0];
            l_netif->hwaddr[1] = dpdk::DPDK_ether_addr[i].addr_bytes[1];
            l_netif->hwaddr[2] = dpdk::DPDK_ether_addr[i].addr_bytes[2];
            l_netif->hwaddr[3] = dpdk::DPDK_ether_addr[i].addr_bytes[3];
            l_netif->hwaddr[4] = dpdk::DPDK_ether_addr[i].addr_bytes[4];
            l_netif->hwaddr[5] = dpdk::DPDK_ether_addr[i].addr_bytes[5];

            l_netif->mtu = config::LWIP_max_mtu;
            l_netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
#if LWIP_NETIF_HOSTNAME
            l_netif->hostname = "lwip";
#endif
            ::memcpy(l_netif->name, LWIP_NETIF_NAME(i), 2);

            l_netif->output = etharp_output;

            /** 挂载到协议栈上 */
            ip4_addr_t netif_ip;
            do {
                LWIP_SET_UP_NETIF_IP
            } while(0);
            netif_add(l_netif, &netif_ip, 
                &config::LWIP_netmask_adddr, 
                &config::LWIP_gw_adddr, 
                NULL, 
                LwipNetifManager::_init, 
                ethernet_input);
            
            /** 注册默认网卡 */
            netif_set_default(l_netif);

            if (netif_is_link_up(l_netif)) {
                netif_set_up(l_netif);
            } else {
                netif_set_down(l_netif);
            }

            /** 加入map中 */
            _netifs.insert({i, netif_});
        }
    }

    lwip_netif_t get_netif(int port) {
        std::unique_lock<util::SpinMutex> lock(_mtx);
        auto it = _netifs.find(port);
        lock.unlock();

        if (it == _netifs.end()) {
            return NULL;
        }
        return it->second;
    }

private:
    LwipNetifManager() = default;

    /** netif_add的时候的注册函数 */
    static err_t _init(struct netif *netif) {
        netif->linkoutput = LwipNetifManager::_linkout;
    }

    static err_t _linkout(struct netif *netif, struct pbuf *p) {
        LwipNetif *l_netif = static_cast<LwipNetif*>(netif);
        
        DpdkNetif *p_netif = dpdk_netif_mg()->get_netif(l_netif->port);
        p_netif->netif_tx(p);
        return ERR_OK;
    }

private:
    util::SpinMutex _mtx;
    std::map<int, LwipNetif*> _netifs;
};

inline static auto lwip_netif_mgr() -> LwipNetifManager::ptr {
    return LwipNetifManager::GetInstance();
}

//
//
// NetifDriver
void NetifDriver::input(int port) {
    /** lwip层的虚拟网卡 */
    lwip_netif_t v_netif = lwip_netif_mgr()->get_netif(port);
    assert(v_netif != NULL);
    
    /** 对应的物理网卡 */
    Netif *p_netif = dpdk_netif_mg()->get_netif(port);
    assert(p_netif != NULL);

    /** 从物理网卡上接收数据 */
    pbuf *p = p_netif->netif_rx();
    assert(p != NULL);

    /** 将数据包传递到协议栈 */
    err_t err = v_netif->input(p, v_netif);
    if (err != ERR_OK) {
        LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));

        // 异步 释放数据包
        pbuf_iter it = p, _it;
        while (it) {
            _it = it->next;

            base::Closure *done = (base::Closure*)it->done;
            if (!base::closure_queue()->commit(done)) {
                done->Run();
            }

            it = _it;
        }
    }
}
}   // fg