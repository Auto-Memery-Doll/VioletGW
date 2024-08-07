#ifndef FLOW_GATEWAY_DMA_HPP
#define FLOW_GATEWAY_DMA_HPP

#include "base/noncopyable.hpp"
#include <rte_build_config.h>
#include <rte_ring_core.h>
#include <rte_mempool.h>

namespace fg {
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

/** dma的buf内存池 */
extern struct rte_mempool *DMA_pktmbuf_pool;

/************ dpdk dma dev class *****************/
class DmaDev : public base::noncopyable {

};

}   // dma
}   // fg

#endif // !FLOW_GATEWAY_DMA_HPP