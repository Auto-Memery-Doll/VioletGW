#ifndef FLOW_GATEWAY_DMA_HPP
#define FLOW_GATEWAY_DMA_HPP


#include <rte_build_config.h>
#include <rte_ring_core.h>
#include <rte_mempool.h>

namespace fg {

// 该命名空间下的东西是基于rte_eth_rx_burst()和rte_eth_tx_burst()
// 来模拟dma的行为，
// 在实际的应用中的话，如果使用这个{dma}的接口来进行开发，
// 其效果是不如直接使用rte_eth_tx(rx)_burst()
// 因为这会增加内存拷贝的次数，无论是使用hw模式的dma还是sw模式的dma
namespace dma {

/** dma复制模式 */
enum class copy_mode {   
    sw,         /** 软件复制（通过多线程的内存拷贝实现） */
    hw,         /** 硬件复制（直接使用dma设备） */
    invalid,    /** 无效参数 */
    num,        
};

/** dma是运行模式 */
enum class run_mode
{
    singal,     /** 单线程运行dma（发送和接收在同一线程上）*/
    multiple,   /** 多线程运行dma（发送和接收时独立的两个线程）*/  
};

/************ dpdk dma dev api *****************/
/** 前置声明 */
struct rxtx_port_config;

void    init(struct rte_mempool *dma_mempool);
void    rx_port(struct rxtx_port_config *rx_config);
void    tx_port(struct rxtx_port_config *tx_config);
int     check_link_status();
void    assign_dmadevs();
void    assign_rings();
/** 多线程模式（发送和接收是分离的） */
void    rx_main_loop();
void    tx_main_loop();
/** 单线程模式（发送和接收在同一线程上） */
void    rxtx_main_loop();
}   // dma
}   // fg

#endif // !FLOW_GATEWAY_DMA_HPP