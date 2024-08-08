#include "dpdk_netif.hpp"
#include "lwip/pbuf.h"
#include "config.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <rte_build_config.h>
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mbuf_core.h>
#include <rte_mempool.h>
#include <unistd.h>

namespace fg {
namespace dpdk {

/** 全局内存池 */
struct rte_mempool *DPDK_mempool;
/** 每个端口对应的mac地址（只使用ipv4，ipv6是没有mac地址的） */
struct rte_ether_addr DPDK_ether_addr[RTE_MAX_ETHPORTS];

/** 网络端口配置 */
static struct rte_eth_conf g_port_conf = {
    .rxmode = {
        .max_lro_pkt_size = config::DPDK_port_rxmode_max_lro_size,
        .mq_mode = RTE_ETH_MQ_RX_RSS, /** 启用接收端拓展 */
    },
    /** 接收队列配置 */
    .rx_adv_conf = {
        /**接收端拓展配置 */
        .rss_conf = {
            .rss_key = NULL,/**使用默认的散列键进行队列之间的负载均衡 */
            .rss_hf = RTE_ETH_RSS_PROTO_MASK,/**所支持的协议的类型 */
        },
    },
    /** 发送队列配置 */
    .tx_adv_conf = {

    },
};

static struct rte_eth_dev_info g_dev_info = {

};


/** 获取配置信息中每个端口的接收队列和发送队列的数量 */
static uint16_t g_nb_rx_desc = config::DPDK_rx_queue_num; 
static uint16_t g_nb_tx_desc = config::DPDK_nb_tx_queue_desc;

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
static int
config_port_max_pkt_len(struct rte_eth_conf *conf, 
        struct rte_eth_dev_info *dev_info) {
    uint32_t overhead_len;

    if (config::DPDK_max_frame_size == 0)
        return 0;

    if (config::DPDK_max_frame_size < RTE_ETHER_MIN_LEN) 
        return -1;
    
    overhead_len = eth_dev_get_overhead_len(dev_info->max_rx_pktlen, 
        dev_info->max_mtu);
    conf->rxmode.mtu = config::DPDK_max_frame_size - overhead_len;

    return 0;
}

/** 初始化|port|的发送队列 */
static void init_rx_queue(int port, int queue_id, struct rte_eth_rxconf* rxq_conf) {
    if (rte_eth_rx_queue_setup(
            port, queue_id, 
            config::DPDK_nb_tx_queue_desc, 
            rte_eth_dev_socket_id(port), 
            rxq_conf,
            DPDK_mempool) < 0) {

            rte_exit(EXIT_FAILURE, "Could not setup RX queue{port: %d queue: %d}.\n",
                port, queue_id);
    }
}

/** 初始化|port|的接收队列 */
static void init_tx_queue(int port, int queue_id, struct rte_eth_txconf *txq_conf) {
    if (rte_eth_tx_queue_setup(
        port, queue_id, 
        config::DPDK_nb_rx_queue_desc, 
        rte_eth_dev_socket_id(port), 
        txq_conf) < 0) {

        rte_exit(EXIT_FAILURE, "Could not setup TX queue{port: %d queue: %d}.\n",
            port, queue_id);
    }
}

inline static void init_multi_tx_queue(int port, struct rte_eth_txconf *txq_conf) {
    for (int i = 0; i < g_nb_tx_desc; ++ i) {
        init_tx_queue(port, i, txq_conf);
    }
}

inline static void init_multi_rx_queue(int port, struct rte_eth_rxconf *rxq_conf) {
    for (int i = 0; i < g_nb_rx_desc; ++ i) {
        init_rx_queue(port, i, rxq_conf);
    }
}

/** 初始化端口的rx和tx队列 */
static void port_init() {
    // 检查是否有端口可用
    uint16_t vaild_ports = rte_eth_dev_count_avail();
    if (vaild_ports <= 0) {
        rte_exit(EXIT_FAILURE, "No supported eth found\n");
    }
    printf("\nChecking link status\n");

    uint16_t port_it;/**遍历端口用的iterator */
    uint32_t ports_mark = config::DPDK_vaild_port_marks;/** 用户的配置信息 */
    int ret = 0;
    struct rte_eth_dev_info dev_info; /**端口信息 */
    // 检查指定的端口是否可用
    RTE_ETH_FOREACH_DEV(port_it) {
        // 判断端口是否为指定的端口
        if ((ports_mark & (1 << port_it)) == 0) 
            continue;

        printf("Initializing port %u... \n", port_it);

        /** 获取端口信息 */
        memset(&dev_info, 0, sizeof(struct rte_eth_dev_info));
        ret = rte_eth_dev_info_get(port_it, &dev_info);
        if (ret < 0)
            rte_exit(EXIT_FAILURE, "Cannot get device info: %s, port=%u\n",
                rte_strerror(-ret), port_it);
        
        /** 计算出端口的数据包的最大负载 */
        ret = config_port_max_pkt_len(&g_port_conf, &dev_info);
        if (ret != 0) 
            rte_exit(EXIT_FAILURE, 
                "Invalid max frame size: %u (port %u)\n",
                config::DPDK_max_frame_size, port_it);

        /** 配置端口 */
        g_port_conf.rx_adv_conf.rss_conf.rss_hf &=
            dev_info.flow_type_rss_offloads;
        ret = rte_eth_dev_configure(port_it, 
            config::DPDK_rx_queue_num, 
            config::DPDK_tx_queue_num, 
            &g_port_conf);
        if (ret < 0) {
            rte_exit(EXIT_FAILURE, "Cannot configure device:"
                " err=%d, port=%u\n", ret, port_it);
        }

        /** 获取实际端口支持的接收队列和发送队列的数量 */
        ret = rte_eth_dev_adjust_nb_rx_tx_desc(port_it, 
            &g_nb_rx_desc, 
            &g_nb_tx_desc);
        if (ret < 0) {
            rte_exit(EXIT_FAILURE, 
                "Cannot adjust number of descriptors: err=%d, port=%u\n",
                ret, port_it);
        }

        /** 获取端口的mac地址 */
        rte_eth_macaddr_get(port_it, &DPDK_ether_addr[port_it]);

        /** 挂在发送队列和接收队列 */
        struct rte_eth_rxconf rxq_conf = dev_info.default_rxconf;
        rxq_conf.offloads = g_port_conf.rxmode.offloads;
        init_multi_rx_queue(port_it, &rxq_conf);
       
        struct rte_eth_txconf txq_conf = dev_info.default_txconf;
        txq_conf.offloads = g_port_conf.txmode.offloads;
        init_multi_tx_queue(port_it, &txq_conf);

        /** 启动端口 */
        ret = rte_eth_dev_start(port_it);
        if (ret < 0) {
		rte_exit(EXIT_FAILURE,
			"rte_eth_dev_start:err=%d, port=%u\n",
			ret, port_it);
        }

        rte_eth_promiscuous_enable(port_it);

        printf("Port %u, MAC address: " RTE_ETHER_ADDR_PRT_FMT "\n\n",
                port_it,
                RTE_ETHER_ADDR_BYTES(&DPDK_ether_addr[port_it]));
    }
}

int
check_link_status() {
    uint32_t port_mask = config::DPDK_vaild_port_marks; /** 配置的掩码，标识那个端口是可用的 */
    uint16_t portid; /** 用于遍历，类似迭代器 */
    struct rte_eth_link link;
    int ret, link_states = 0;
    char link_status_text[RTE_ETH_LINK_MAX_STR_LEN]; /** 描述端口的状态 */
    
    printf("\nChecking link status...\n");
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
 

/** 初始化dpdk环境 */
void init(int argc, char *argv[]) {

    if (rte_eal_init(argc, argv) < 0) {
        rte_exit(EXIT_FAILURE, 
            "rte_eal_init() failure.\n");
    }

    /** 创建内存池 */
    struct rte_mempool *temp =  rte_pktmbuf_pool_create(
        config::DPDK_mempool_name, 
        config::DPDK_mempool_block_num, 
        config::DPDK_mempool_cache_size, 
        config::DPDK_mempool_private_size,
        config::DPDK_mempool_block_size, 
        rte_socket_id());
    if (temp == NULL) {
        rte_exit(EXIT_FAILURE, 
            "rte_pktmbuf_pool_create() failure.\n");
    }
    DPDK_mempool = temp;

    /** 初始化使用的端口 */
    port_init();

    /** 等待有链路变得可用 */
    while (!check_link_status()) 
        sleep(1);
}

/** 从网卡上接收数据包 */
int rx_burst(uint16_t port_id, 
                    uint16_t queue_id, 
                    struct rte_mbuf **rx_pkts, 
                    const uint16_t nb_pkts) {
    return rte_eth_rx_burst(port_id,  queue_id, rx_pkts, nb_pkts);
}
/** 从将数据包发送到网卡上 */
int tx_burst(uint16_t port_id, 
                    uint16_t queue_id, 
                    struct rte_mbuf **tx_pkts, 
                    uint16_t nb_pkts) {
    return rte_eth_tx_burst(port_id, queue_id, tx_pkts, nb_pkts);
}

/** 向内存池内申请一个mbuf */
// todo:
rte_mbuf * get_mbuf() {
    struct rte_mbuf * buf_ = rte_pktmbuf_alloc(DPDK_mempool);
    if (!buf_) {
        printf("there is no mbuf in g_mempool.\n");
        return nullptr;
    }
    return buf_;
}

}   // dpdk
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