#ifndef FLOW_GATEWAY_CONFIG_HPP
#define FLOW_GATEWAY_CONFIG_HPP

#include "dma.hpp"
#include <cstdint>
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

/** eth数据帧的最大长度 */
const uint32_t DMA_max_frame_size = 1024;

}   // config
}   // fg

#endif // !FLOW_GATEWAY_CONFIG_HPP