#include "dpdk_netif.hpp"
#include "lwip/pbuf.h"
#include "config.hpp"
#include <rte_ethdev.h>
#include <rte_ether.h>

namespace fg {

/** 计算网络设备的数据包头部开销长度 */
/// @param max_rx_pktlen 最大接收数据包长度
/// @param max_mtx 最大传输单元
static uint32_t
eth_dev_get_overhead_len(uint32_t max_rx_pktlen, uint16_t max_mtu) {

    uint32_t overhead_len;
    // 检查mtu是否未被指定，或者使用了最大值
    if (max_mtu != UINT16_MAX && max_rx_pktlen > max_mtu) {
        // 
        overhead_len = max_rx_pktlen - max_mtu;
    } else {
        // CRC(Cyclic Redundancy Check, 循环冗余检验)
        overhead_len = RTE_ETHER_HDR_LEN + RTE_ETHER_CRC_LEN;
    }
    return overhead_len;
}

/** 设置mtu */
int
config_port_max_pkt_len(struct rte_eth_conf *conf, 
        struct rte_eth_dev_info *dev_info) {
    uint32_t overhead_len;

    if (config::DMA_max_frame_size == 0)
        return 0;

    if (config::DMA_max_frame_size < RTE_ETHER_MIN_LEN) 
        return -1;
    
    overhead_len = eth_dev_get_overhead_len(dev_info->max_rx_pktlen, 
        dev_info->max_mtu);
    conf->rxmode.mtu = config::DMA_max_frame_size - overhead_len;

    return 0;
}




}   // fg

namespace fg {

void DpdkNetif::init() {

}

void DpdkNetif::netif_input(pbuf *pbuf_chain) {

}

pbuf* DpdkNetif::netif_output() {

}

void DpdkNetif::run() {
    
}

}   // fg