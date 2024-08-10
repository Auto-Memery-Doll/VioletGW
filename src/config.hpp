#ifndef FLOW_GATEWAY_CONFIG_HPP
#define FLOW_GATEWAY_CONFIG_HPP

#include "base/util.hpp"
#include "dma.hpp"
#include <cstdint>
#include <rte_ether.h>
#include <rte_mbuf_core.h>
namespace fg {
namespace config {

/****************************** dpdk dma configuration *******************************/
/** 内存池缓冲的大小  */
const int64_t DMA_mempool_cache_size = 65535U;

/** 对数据包进行批处理的最大数量 */
const int64_t DMA_max_pkt_burst = 32;

/** 接收缓冲区的发送缓冲区的大小 */
const int16_t DMA_rx_default_ringsize = 1024;
const int16_t DMA_tx_default_ringsize = 1024;

/** 每个端口的最大接收队列数量 */
const int DMA_max_rx_queues_count = 8;

/** dma的数据复制模式 */
/** 默认是软件复制（因为虚拟机上没有dpdk兼容的dma设备） */
const dma::copy_mode DMA_copy_mode = dma::copy_mode::sw;

/** dma的运行模式 */
/** 默认是多线程模式 */
const dma::run_mode DMA_run_mode = dma::run_mode::multiple;

/** dma端口的掩码，用于标识哪些端口被启用了 */
/** 每一个位代表一个端口 */
const uint32_t DMA_enable_prot_mask = 0b0001111;

/** 网卡中接收队列的数量 */
const uint16_t DMA_nb_queues = 1;

/** 是否更新mac地址 */
/** 有点类似于重定向，将网络数据包通过mac地址对应的硬件设备传输到固定的内存中 */
const int DMA_mac_updating = 1;

/** dma环形缓存区的大小 */
const unsigned short DMA_ring_size = 2048;

/** 时间间隔 */
const unsigned short DMA_stats_interval = 1;

/** dma buf缓冲区的大小 */
const uint16_t DMA_mbuf_ring_size = 2048;
/** 用于进行ring的动作 */
const uint16_t DMA_ring_mask = DMA_mbuf_ring_size - 1;

/** dma软件模式下，对数据包拷贝的最小字节数 */
const uint32_t DMA_force_min_copy_size = 1024;

/** dma批处理数据包的最大数量 */
const uint32_t DMA_batch_sz = 64;

/********************* DPDK base configuration ******************/
/** dpdk 内存池的名字 */
const char DPDK_mempool_name[] = "flow_gateway_dpdk_mempool";
/** 用来给lwip pbuf进行转换的mempool */
const char LWIP_mempool_name[] = "flow_gateway_dpdk_mempool";

/** dpdk 全局内存池中的内存块的size */
const unsigned DPDK_mempool_block_size = 1024;
const unsigned LWIP_mempool_block_size = 0;/** 因为需要使用pbuf的负载 */

/** dpdk 全局内存池的中的内存块的数量 */
const uint16_t DPDK_mempool_block_num = RTE_MBUF_DEFAULT_BUF_SIZE;
const uint16_t LWIP_mempool_block_num = RTE_MBUF_DEFAULT_BUF_SIZE;

/** dpdk 内存池的cache的大小 */
// 1.用于存储与数据包处理相关的缓存信息
// 2.用于提高数据包处理的性能
const unsigned DPDK_mempool_cache_size = 0;
const unsigned LWIP_mempool_cache_size = 0;

/** dpdk private区域的大小 */
// 1.用于存储特定于数据包的私有数据
// 2.可以进行定制化开发和使用
const unsigned DPDK_mempool_private_size = 0;
const unsigned LWIP_mempool_private_size = 0;

/** LRO large receive offload 聚合包的最大大小 */
// LRO是一种技术，它可以将多个小数据包合并成一个大数据包，以减少处理开销
const uint32_t DPDK_port_rxmode_max_lro_size = RTE_ETHER_MAX_LEN;

/** 接收队列的最大容量 */
const uint16_t DPDK_nb_rx_queue_desc = 128;

/** 发送队列的最大容量 */
const uint16_t DPDK_nb_tx_queue_desc = 128;

/** DPDK使用端口的数量（默认为0x01） */
const uint32_t DPDK_vaild_port_marks = 0x001;

/** 接收队列和发送队列的配置是否使用默认的配置 */
const bool DPDK_tx_config_default = true;
const bool DPDK_rx_config_default = true;

/** 每个端口的接收队列和发送的数量 */
const uint16_t DPDK_tx_queue_num = 1;
const uint16_t DPDK_rx_queue_num = 1;

/** eth数据帧的最大长度 */
const uint32_t DPDK_max_frame_size = 1024;

/********************* DPDK vdevice configuration ***************/
/** dpdk device 一次从网卡上接收的数据包的最大数量 */
const int VDEV_rx_burst_num = 32;
/** dpdk device 一次发送到网卡上的数据包的最大数量 */
const int VDEV_tx_burst_num = 32;

/** vnetif的接收队列和发送的队列的空间 */
const uint16_t VDEV_tx_ring_num = 32;
const uint16_t VDEV_rx_ring_num = 32;

/** vnetif的接收队列和发送队列的工作模式（默认为multi-pro-multi-con */
const uint16_t VDEV_tx_ring_mode = 0;
const uint16_t VDEV_rx_ring_mode = 0;

/** 当网卡的tx_ring满了的时候，睡眠等待的时间间隔 */
const int VDEV_tx_sleep = 10; // us
/** 当网卡的rx_ring空了的时候，睡眠等待的时间间隔 */
const int VDEV_rx_sleep = 10; // us

/** 每个核心上运行的网卡的接收和发送线程的数量 */
const int VDEV_core_max_rxtx = 2;

/************************ LWIP netif configuration ******************/
/** mtu */
const int LWIP_max_mtu = 1500; // 固定不可改变

/** 网关地址(192.168.225.1) */
GW_ADDR(192, 168, 225, 1);

/** 网卡的ip地址 */
IP_ADDR(0, 192, 168, 225, 188); // ip0 = 192.168.225.188
IP_ADDR(1, 192, 168, 225, 189); // ip1 = 192.168.225.189

/** 挂在网卡在协议栈上的时候需要手动添加相应的代码 */
#define LWIP_ADD_NETIF_IP_BETCH(index) \
if (i == index) {   \
    netif_ip = config::LWIP_ip_##index##_addr;  \
    break;  \
}

#define LWIP_SET_UP_NETIF_IP \
LWIP_ADD_NETIF_IP_BETCH(0)   \
LWIP_ADD_NETIF_IP_BETCH(1)

/** 网络掩码(225.225.225.0) */
NETMASK_ADDR(225, 225, 225, 0); 


}   // config
}   // fg

#endif // !FLOW_GATEWAY_CONFIG_HPP