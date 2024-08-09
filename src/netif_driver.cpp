#include "netif_driver.hpp"
#include "lwip/netif.h"
#include "lwip/opt.h"
#include "lwipopts.h"
#include "base/singleton.hpp"
#include "base/type.hpp"
#include "base/util.hpp"
#include "config.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <rte_ether.h>

namespace fg {
/** 端口对应的mac地址，用来初始化lwip的抽象网卡接口 */
namespace dpdk {
extern struct rte_ether_addr DPDK_ether_addr[];
}

class LwipNetifManager : public base::Singletion<LwipNetifManager> {
    friend class base::Singletion<LwipNetifManager>;
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
                
            lwip_netif_t l_netif = new LwipNetif;
            assert(l_netif != nullptr);
            
            /** 配置网卡信息 */
            l_netif->hwaddr_len = NETIF_MAX_HWADDR_LEN;

            l_netif->hwaddr[0] = dpdk::DPDK_ether_addr[i].addr_bytes[0];
            l_netif->hwaddr[1] = dpdk::DPDK_ether_addr[i].addr_bytes[1];
            l_netif->hwaddr[2] = dpdk::DPDK_ether_addr[i].addr_bytes[2];
            l_netif->hwaddr[3] = dpdk::DPDK_ether_addr[i].addr_bytes[3];
            l_netif->hwaddr[4] = dpdk::DPDK_ether_addr[i].addr_bytes[4];
            l_netif->hwaddr[5] = dpdk::DPDK_ether_addr[i].addr_bytes[5];

            // TODO
            l_netif->mtu = 1500;

            l_netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

            /** 加入map中 */
            _netifs.insert({i, l_netif});
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

private:
    util::SpinMutex _mtx;
    std::map<int, lwip_netif_t> _netifs;
};





}   // fg