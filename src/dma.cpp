#include "dma.hpp"
#include "config.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <rte_branch_prediction.h>
#include <rte_build_config.h>
#include <rte_common.h>
#include <rte_dmadev.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_mbuf.h>
#include <rte_mbuf_core.h>
#include <rte_memcpy.h>
#include <rte_mempool.h>
#include <rte_ring.h>
#include <rte_ring_core.h>
#include <stdio.h>

namespace fg {
namespace dma {

struct rxtx_port_config {
    uint16_t rxtx_port;
    uint16_t nb_queues;
    /** 用于软件复制 */
    struct rte_ring *rx_to_tx_ring;
    /** 用于硬件复制 */
    uint16_t damdev_ids[config::DMA_max_rx_queues_count];
};

struct rxtx_transmission_config {
    struct rxtx_port_config ports[RTE_MAX_ETHPORTS];    /** 每个端口对应的配置 */
    uint16_t nb_ports;  /** 启用的端口数 */
    uint16_t nb_lcores; /** 核心数 */
};
static struct rxtx_transmission_config g_cfg;


/** 信息统计 */
struct dma_prot_statistics {
    uint64_t rx[RTE_MAX_ETHPORTS]; /** 接收的数据包的数量 */
    uint64_t tx[RTE_MAX_ETHPORTS];  /** 发送的数据包的数量 */
    uint64_t tx_dropped[RTE_MAX_ETHPORTS];  /** 发送失败的数据包的数量 */
    uint64_t copy_dropped[RTE_MAX_ETHPORTS]; /** 复制失败的数据包的数量 */
};
/** 全局变量 */
static dma_prot_statistics g_port_statistics;

/** 总体的传输情况 */
struct dma_total_statistics {
    uint64_t packets_dropped;
    uint64_t packets_tx;
    uint64_t packets_rx;
    uint64_t submitted;
    uint64_t completed;
    uint64_t failed;
};

/** 用于管理正在进行DMA复制的数据包 */
struct dma_bufs {
    struct rte_mbuf *bufs[config::DMA_mbuf_ring_size]; /* 原始的数据包 */
    struct rte_mbuf *copies[config::DMA_mbuf_ring_size]; /* 通过dma复制后的数据包 */
    uint16_t sent; /* 统计发送成的数据包的数量 */
};
/* 管理所有的dma设备上正在进行复制的数据包 */
static struct dma_bufs g_dma_bufs[RTE_DMADEV_DEFAULT_MAX];

/** 是否停止 */
static volatile bool g_foce_queit;
/** 内存池，需要在外部进行初始化 */
struct rte_mempool *DMA_pktmbuf_pool = nullptr;

/** 每个端口对应的mac地址，用于mac更新 */
/** 如果 |config::DMA_mac_updating| == 1 才使用 */
static struct rte_ether_addr g_ports_eth_addr[RTE_MAX_ETHPORTS];

/** 初始化内存指针 */
void init(struct rte_mempool * dma_mempool) {
    DMA_pktmbuf_pool = dma_mempool;
}

static void 
update_mac_addrs(struct rte_mbuf *m, uint32_t dest_portid) {

    struct rte_ether_hdr *eth;
    void *temp;
    // 获取mbuf中的以太网头
    eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr*);

    temp = &eth->dst_addr.addr_bytes[0];
    /* 5 * 8 = 40, 网络字节序是小端 */
    // 最终的dst addr的形式是: 02:00:00:00:00:xx
    // 其中xx是对应的dma设备的端口号，
    *((uint64_t *)temp) = 0x00'00'00'00'00'02 + ((uint64_t)dest_portid << 40); 

    rte_ether_addr_copy(&g_ports_eth_addr[dest_portid], &eth->src_addr);
}

/** 复制元数据 */
static inline void
pktmbuf_metadata_copy(const struct rte_mbuf *src, struct rte_mbuf *dst) {
    dst->data_off = src->data_off;
    memcpy(&dst->rx_descriptor_fields1, &src->rx_descriptor_fields1, 
        offsetof(struct rte_mbuf, buf_len) - 
        offsetof(struct rte_mbuf, rx_descriptor_fields1));
}

/** 在dma软件模式下进行数据包的拷贝 */
/// @param src 源数据包
/// @param dst 指定拷贝到的数据包
static inline void 
pktmbuf_sw_copy(struct rte_mbuf *src, struct rte_mbuf *dst) {

    rte_memcpy(
        rte_pktmbuf_mtod(dst, char *), 
        rte_pktmbuf_mtod(src, char *), 
        RTE_MAX(src->buf_len, config::DMA_force_min_copy_size));
}

/** 将需要发送的数据包加入dma的处理队列中 */
/// @param pkts
/// @param pkts_copy 
/// @param nb_rx 接收的数据包的数量
/// @param dev_id dma设备的id号
/// @param vchan dma传输上下文
static uint32_t 
enqueue_packets(struct rte_mbuf *pkts[], struct rte_mbuf *pkts_copy[], 
    uint32_t nb_rx, uint16_t dev_id, uint16_t vchan) {
    
    struct dma_bufs *dma = &g_dma_bufs[dev_id];
    int ret;
    uint32_t i;

    for (i = 0; i < nb_rx; ++ i) {
        // 进行dma复制，
        // 函数内部会调用相应的dma驱动注册的复制接口
        ret = rte_dma_copy(dev_id, vchan,
            rte_pktmbuf_iova(pkts[i]),
            rte_pktmbuf_iova(pkts_copy[i]),
            RTE_MAX(rte_pktmbuf_data_len(pkts[i]), config::DMA_force_min_copy_size),
            0);

        if (ret < 0) {
            break;
        }
        /** 为什么需要同时维护原始的数据包(pkts)和复制的新数据包(pkts_copy) */
        // 跟踪原始数据包：pkts数组包含了原始的数据包的指针，这对于跟踪数据包的
        //               状态和后续处理非常重要。例如：如果复制过程中出现了错误，
        //               原数据包可能需要被处理（重试）或者释放
        // 跟踪复制数据包：pkts_copy数组包含了复制后的数据指针，这些数据包将在
        //               复制完成后被进一步处理，例如发送到其他端口或者进行其他
        //               操作
        dma->bufs[ret & config::DMA_ring_mask] = pkts[i];
        dma->copies[ret & config::DMA_ring_mask] = pkts_copy[i];
    }

    ret = i;
    return ret;
}

/** 向dma vchan中添加复制任务 */
/// @param pkts
/// @param pkts_copy
/// @param num
/// @param step
/// @param dev_id 
/// @param vchan 
static inline uint32_t
enqueue(struct rte_mbuf *pkts[], struct rte_mbuf *pkts_copy[], 
        uint32_t num, uint32_t step, uint16_t dev_id, uint16_t vchan) {

    uint32_t k = 0;
    for (uint32_t i = 0, m = 0, n = 0; i < num; i += m) {
        m = RTE_MIN(step, num - i);
        n = enqueue_packets(pkts + i, pkts_copy + i, m, dev_id, vchan);
        k += n;
        // 让对应的dma设备开始工作
        if (n > 0) {
            rte_dma_submit(dev_id, vchan);
        }

        // HW queue 已经满了
        if (n != m) {
            break;
        }
    }
    return k;
}

/** dma vchan中已经完成传输的mbuf移除 */
static inline uint32_t
dequeue(struct rte_mbuf *src[], struct rte_mbuf *dst[], uint32_t num,
        uint16_t dev_id, uint16_t vchan) {
    
    struct dma_bufs *dma = &g_dma_bufs[dev_id];
    uint16_t nb_dq, filled;

    nb_dq = rte_dma_completed(dev_id, vchan, num, NULL, NULL);

    for (filled = 0; filled < nb_dq; ++ filled) {
        /** 环形队列 */
        src[filled] = dma->bufs[(dma->sent + filled) & config::DMA_ring_mask];
        dst[filled] = dma->copies[(dma->sent + filled) & config::DMA_ring_mask];
    }

    dma->sent += nb_dq;
    return filled;
}

/** 在一个端口上接收数据包并且将其添加到 dmadev(hw) 或者 rte_ring(sw)中 */
void
rx_port(struct rxtx_port_config *rx_config) {
    
    int32_t ret;
    uint32_t nb_rx/*接收数据包的数量*/, nb_enq/*加入到 dmadev 或者 rte_ring 中的数据包的数量*/;
    struct rte_mbuf *pkts_burst[config::DMA_max_pkt_burst]; /* 维护从网卡上接收到的数据包 */
    struct rte_mbuf *pkts_burst_copy[config::DMA_max_pkt_burst]; /* 维护通过 hw 或者 sw 进行复制的数据包*/

    // 遍历网上的所有接收队列(多队列网卡)
    for (int i = 0; i < rx_config->nb_queues; ++ i) {
        // 抓取数据包
        nb_rx = rte_eth_rx_burst(rx_config->rxtx_port, i, 
            pkts_burst, config::DMA_max_pkt_burst);

        // todo: vchan should not be spcified
        if (nb_rx == 0) {
            if (config::DMA_copy_mode == copy_mode::num && 
                (nb_rx = dequeue(
                    pkts_burst, pkts_burst_copy, config::DMA_max_pkt_burst, 
                    rx_config->damdev_ids[i], 0)) > 0)
                goto handle_tx;
            continue;
        }

        /** 统计数据 */
        g_port_statistics.rx[rx_config->rxtx_port] += nb_rx;

        /** 从内存池中分配nb_rx个mbuf */ 
        ret = rte_mempool_get_bulk(
            DMA_pktmbuf_pool, (void **)pkts_burst_copy, nb_rx);
        
        if (unlikely(ret < 0))
            rte_exit(EXIT_FAILURE, 
                "Unable to allocate memory.\n");

        for (int i = 0; i < nb_rx; ++ i) 
            pktmbuf_metadata_copy(pkts_burst[i], 
                pkts_burst_copy[i]);
        
        /** 硬件复制 */
        if (config::DMA_copy_mode == copy_mode::hw) {
            // 将任务提交到任务队列中，让dma设备进行拷贝
            nb_enq = enqueue(pkts_burst, pkts_burst_copy, 
                nb_rx, config::DMA_batch_sz, 
                rx_config->damdev_ids[i], 0);
            
            // 将没有使用到的数据包归还到内存池中
            rte_mempool_put_bulk(DMA_pktmbuf_pool, 
                (void **)&pkts_burst_copy[nb_enq], nb_rx - nb_enq);
            rte_mempool_put_bulk(DMA_pktmbuf_pool, 
                (void **)&pkts_burst_copy[nb_enq], nb_rx - nb_enq);
            
            // 复制完毕，将这批任务从队列中移除
            nb_rx = dequeue(pkts_burst, pkts_burst_copy, 
                config::DMA_max_pkt_burst, 
                rx_config->damdev_ids[i], 0);
        }
        /** 软件复制 */
        else {
            for (int i = 0; i < nb_rx; ++ i) 
                pktmbuf_sw_copy(pkts_burst[i], 
                    pkts_burst_copy[i]);
        }

handle_tx:
        rte_mempool_put_bulk(DMA_pktmbuf_pool, (void **)pkts_burst, nb_rx);

        /** 将拷贝好的数据包的地址传递到指定的ring中，进行下一步处理 */
        nb_enq = rte_ring_enqueue_burst(rx_config->rx_to_tx_ring, 
            (void **)pkts_burst_copy, nb_rx, NULL);

        rte_mempool_put_bulk(DMA_pktmbuf_pool, 
            (void **)&pkts_burst_copy[nb_enq], nb_rx - nb_enq);

        g_port_statistics.copy_dropped[rx_config->rxtx_port] +=
            (nb_rx - nb_enq);
    }
}

void 
tx_port(struct rxtx_port_config *tx_config) {

    uint32_t nb_dq, nb_tx;
    struct rte_mbuf *mbufs[config::DMA_max_pkt_burst];

    /** 遍历所有的发送队列（在多队列网卡中，发送队列和接收队列是独立的） */
    for (uint32_t i = 0; i < tx_config->nb_queues; ++ i) {
        /** 将处理好的数据包从ring中取出 */
        nb_dq = rte_ring_dequeue_burst(tx_config->rx_to_tx_ring, 
            (void **)mbufs, 
            config::DMA_max_pkt_burst, NULL);
        // 队列空闲
        if (nb_dq == 0) {
            continue;
        }

        /** 更新数据包的mac地址 */
        // 因为在数据包进行dma传输之后，mac地址是对应的dma设备的硬件地址（相对）
        // 这个mac地址与网卡地址无关，
        // 需要重新设置成正确的mac地址
        if (config::DMA_mac_updating) {
            for (int j = 0; j < nb_dq; ++ j)
                update_mac_addrs(mbufs[i], 
                    tx_config->rxtx_port);
        }

        /** 发送数据包 */
        nb_tx = rte_eth_tx_burst(tx_config->rxtx_port, 
            0,  mbufs, nb_dq);

        g_port_statistics.tx[tx_config->rxtx_port] += nb_tx;

        if (unlikely(nb_tx < nb_dq)) {
            g_port_statistics.tx_dropped[tx_config->rxtx_port] += 
                (nb_dq - nb_tx);
            // 将发送失败的数据包释放
            rte_mempool_put_bulk(DMA_pktmbuf_pool, 
                (void **)&mbufs[nb_tx], nb_dq - nb_tx);
        }
    }
}

// todo: replace [printf] to a log api
/** 检查是否有足够的端口可用 */
int
check_link_status() {
    uint32_t port_mask = config::DMA_enable_prot_mask; /** 配置的掩码，标识那个端口是可用的 */
    uint16_t portid; /** 用于遍历，类似迭代器 */
    struct rte_eth_link link;
    int ret, link_states = 0;
    char link_status_text[RTE_ETH_LINK_MAX_STR_LEN]; /** 描述端口的状态 */
    
    printf("\nChecking link status.\n");
    RTE_ETH_FOREACH_DEV(portid) {
        if ((port_mask & (1 << portid)) == 0) 
            continue;

        memset(&link, 0, sizeof(rte_eth_link));
        ret = rte_eth_link_get(portid, &link);
        if (ret < 0) {
            printf("Port %u link get failed: err=%d\n");
            continue;
        }

        /** 输出端口的状态 */
        rte_eth_link_to_str(link_status_text, 
            sizeof(rte_eth_link), &link);
        printf("Port %d %s\n", portid, link_status_text);

        if (link.link_status) 
            link_states = 1;
    }
    return link_states;
}
 
// todo: replace [printf] to a log api
static void 
configure_dmadev_queue(uint32_t dev_id) {

    struct rte_dma_info dmadev_info;
    struct rte_dma_conf dev_config = {
        .nb_vchans = 1,
    };
    // todo: it should be configure by user, but not is specified
    uint16_t vchan = 0;
    struct rte_dma_vchan_conf qconf = {
        .direction = RTE_DMA_DIR_MEM_TO_MEM,
        .nb_desc = config::DMA_ring_size,  
    };

    if (rte_dma_configure(dev_id, &dev_config) != 0) {
        rte_exit(EXIT_FAILURE, 
            "Error with rte_dma_configure()\n");
    }

    if (rte_dma_vchan_setup(dev_id, vchan, &qconf) != 0) {
        rte_exit(EXIT_FAILURE, 
            "Error with queue configuration.\n");
    }

    rte_dma_info_get(dev_id, &dmadev_info);
    if (dmadev_info.nb_vchans != 1) {
		printf("Error, no configured queues reported on device id %u\n", dev_id);
		rte_panic();
    }

    /** 启动dma设备 */
    if (rte_dma_start(dev_id) != 0) {
        rte_exit(EXIT_FAILURE, "Error with rte_dma_start()\n");
    }
}

/** 将dma设备分配到网卡的接收队列中 */
void 
assign_dmadevs(void) {
    uint16_t nb_dmadev = 0;
    /** 遍历第一个有效的dma设备 */
    int16_t dev_id = rte_dma_next_dev(0);

    for (uint32_t i = 0; i < g_cfg.nb_ports; i++) {
        for (uint32_t j = 0; j < g_cfg.ports[i].nb_queues; ++ j) {
            if (dev_id == -1) // 已经没有有效的端口了
                goto end;
            
            g_cfg.ports[i].damdev_ids[j] = dev_id;
            configure_dmadev_queue(g_cfg.ports[i].damdev_ids[j]);

            /** 遍历，寻找下一个可用的dma设备 */
            dev_id = rte_dma_next_dev(dev_id + 1);
            ++nb_dmadev;
        }
    }
end:
    if (nb_dmadev < g_cfg.nb_ports * g_cfg.ports[0].nb_queues) {
        rte_exit(EXIT_FAILURE, 
            "Not enough dmadevs (%u) for al queues (%u).\n",
            nb_dmadev, g_cfg.nb_ports * g_cfg.ports[0].nb_queues);
    }

    printf("Number of used dmadevs: %u", nb_dmadev);
} 

/** 为每一个端口分配一个环形缓冲区 */
// 一个dma设备只能够专用于接收或者发送，所以每个dma只需一个ring
void
assign_rings(void) {

    for (uint32_t i = 0; i < g_cfg.nb_ports; ++ i) {
        char ring_name[RTE_RING_NAMESIZE];

        snprintf(ring_name, sizeof(ring_name), "rx_to_tx_ring_%u", i);
        /** 创建一个环形队列 */
        g_cfg.ports[i].rx_to_tx_ring = rte_ring_create(
            ring_name, config::DMA_mbuf_ring_size, 
            rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
        
        if (g_cfg.ports[i].rx_to_tx_ring == NULL) {
            rte_exit(EXIT_FAILURE, 
                "Ring create failed: %s\n", 
                rte_strerror(rte_errno));
        }
    }
}

/** 多线程运行例程函数 */
void
rx_main_loop(void)
{
	uint16_t i;
	uint16_t nb_ports = g_cfg.nb_ports;

	printf("Entering main rx loop for copy on lcore %u\n",
		rte_lcore_id());

	while (!g_foce_queit)
		for (i = 0; i < nb_ports; i++)
			rx_port(&g_cfg.ports[i]);
}

void
tx_main_loop(void)
{
	uint16_t i;
	uint16_t nb_ports = g_cfg.nb_ports;

	printf("Entering main tx loop for copy on lcore %u\n",
		rte_lcore_id());

	while (!g_foce_queit)
		for (i = 0; i < nb_ports; i++)
			tx_port(&g_cfg.ports[i]);
}

/** 单线程运行例程函数 */
// todo: printf
void
rxtx_main_loop(void)
{
	uint16_t i;
	uint16_t nb_ports = g_cfg.nb_ports;

	printf("Entering main rx and tx loop for copy on"
		" lcore %u\n", rte_lcore_id());

	while (!g_foce_queit)
		for (i = 0; i < nb_ports; i++) {
			rx_port(&g_cfg.ports[i]);
			tx_port(&g_cfg.ports[i]);
		}
}
}   // dma
}   // fg