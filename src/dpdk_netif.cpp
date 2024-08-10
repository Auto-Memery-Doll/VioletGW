#include "dpdk_netif.hpp"
#include "base/closure.hpp"
#include "base/iobuf.hpp"
#include "base/ring.hpp"
#include "base/type.hpp"
#include "base/util.hpp"
#include "lwip/arch.h"
#include "lwip/pbuf.h"
#include "config.hpp"
#include "lwip/timeouts.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <rte_build_config.h>
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_mbuf_core.h>
#include <rte_mempool.h>
#include <rte_ring_core.h>
#include <unistd.h>

namespace fg {
namespace dpdk {

/** 全局内存池 */
struct rte_mempool *DPDK_mempool;
static struct rte_mempool *LWIP_mempool;
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
            "rte_pktmbuf_pool_create() failure(%s)\n", config::DPDK_mempool_name);
    }
    DPDK_mempool = temp;

    temp = rte_pktmbuf_pool_create(
        config::LWIP_mempool_name, 
        config::LWIP_mempool_block_num, 
        config::LWIP_mempool_cache_size, 
        config::LWIP_mempool_private_size, 
        config::LWIP_mempool_block_size, 
        rte_socket_id());
    if (temp == NULL) {
        rte_exit(EXIT_FAILURE, 
            "rte_pktmbuf_pool_create() failure(%s)\n", config::LWIP_mempool_name);
    }

    /** 初始化使用的端口 */
    port_init();

    /** 等待有链路变得可用 */
    while (!check_link_status()) 
        sleep(1);
}

/** 释放相关的资源 */
void clean() {
    /** 等待每一个线程结束 */
    rte_eal_mp_wait_lcore();

    /** 释放资源 */
    rte_eal_cleanup();
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
rte_mbuf * get_mbuf(bool is_pbuf_to) {
    struct rte_mbuf * buf_ = is_pbuf_to 
                                ?   rte_pktmbuf_alloc(LWIP_mempool) 
                                :   rte_pktmbuf_alloc(DPDK_mempool);
    if (!buf_) {
        printf("there is no mbuf in g_mempool.\n");
        return nullptr;
    }
    return buf_;
}

}   // dpdk
}   // fg

namespace fg {

//
//
// MbufPbufAdapter
MbufPbufAdapter::MbufPbufAdapter(MbufPbufAdapter::Type type, void *buf) {
    do {
        if (type == Type::mbuf_to_pbuf) {
            _mbuf = (mbuf*)buf;/** void -> rte_mbuf -> mbuf */
            _pbuf = mbuf_to_pbuf(buf, this);
            break;   
        }
        if (type == Type::pbuf_to_mbuf) {
            _pbuf = (pbuf*)buf;
            _mbuf = pbuf_to_mbuf(buf, this);
            break;   
        }
    } while(0);
}

void * MbufPbufAdapter::Run() {
    do {
        // 应用层已经将这个pbuf使用完了，将mbuf和pbuf归还到内存池
        if (_type == Type::mbuf_to_pbuf) {
            if (_forward) {
                _type = Type::pbuf_to_mbuf;
                continue;
            }
            PbufFree(_pbuf);
            // 异步释放
            base::closure_queue()->commit(_mbuf);
            delete this;
            break;
        }

        // 应用层将pbuf传递到dpdk
        // 需要等待dpdk将数据包发送到网络中
        if (_type == Type::pbuf_to_mbuf) {
            if (_forward) {
                _forward = false;
                _type = Type::mbuf_to_pbuf;
                continue;
            }
            _mbuf->buf_addr = _mbuf_buf_addr;
            _mbuf->data_off = _mbuf_data_off;
            base::closure_queue()->commit(_mbuf);
            PbufFree(_pbuf);
        }
    } while(1);
    return NULL;
}

mbuf *MbufPbufAdapter::pbuf_to_mbuf(void *buf, void* done) {
    struct rte_mbuf * mbuf_ = dpdk::get_mbuf();
    pbuf *pbuf_ = (struct pbuf*)buf;
    mbuf_->data_len = pbuf_->len;
    mbuf_->pkt_len = pbuf_->tot_len;

    ((MbufPbufAdapter*)done)->_mbuf_buf_addr = mbuf_->buf_addr;
    ((MbufPbufAdapter*)done)->_mbuf_data_off = mbuf_->data_off;
    mbuf_->buf_addr = pbuf_->payload;
    mbuf_->data_off = 0;
}

pbuf *MbufPbufAdapter::mbuf_to_pbuf(void *buf, void* done) {
    struct rte_mbuf* mbuf_ = (struct rte_mbuf*)buf;
    pbuf *pbuf_ = GetEmptyPbuf();

    void *payload = rte_pktmbuf_mtod(mbuf_, struct rte_mbuf*);
    u16_t total_len = rte_pktmbuf_pkt_len(mbuf_);   /** 完整报文的长度 */
    u16_t len = rte_pktmbuf_data_len(mbuf_);    /** 当前报文的长度 */
    SetEmptyPbuf(pbuf_, payload, total_len, len, done);

    return pbuf_;
}

//
//
// DpdkNetif

DpdkNetif::~DpdkNetif() {
    printf("vnetif %d is down.\n", port_id);
}

void DpdkNetif::init(int port) {
    _rx_ring = make_ring<rte_mbuf>(
            util::RX_RING_NAME(port).c_str(), 
            config::VDEV_rx_ring_num, 
            config::VDEV_rx_ring_mode);
    _tx_ring = make_ring<MbufPbufAdapter>(
            util::TX_RING_NAME(port).c_str(), 
            config::VDEV_tx_ring_num, 
            config::VDEV_tx_ring_mode);
}

// todo: printf -> log
void DpdkNetif::netif_tx(pbuf *pbuf_chain) {
    int size = 0;
    MbufPbufAdapter* bufs[config::VDEV_tx_burst_num];

    pbuf_iter it = pbuf_chain;
    while (size != config::VDEV_tx_burst_num && it) {
        bufs[size] = (MbufPbufAdapter*)pbuf_chain->done;
        ++size;
        it = it->next;
    }

    int ret = 0; 
    while ((ret += _tx_ring->push_burst(bufs + ret, size - ret)) != size)
        usleep(config::VDEV_tx_sleep);
}

pbuf* DpdkNetif::netif_rx() {
    int _size;
    while (0 == (_size = _rx_ring->free_size())) 
        usleep(config::VDEV_rx_sleep);

    const int size = _size;
    rte_mbuf *mbufs[size];
    int  ret = _rx_ring->pop_burst(mbufs, size);
    assert(ret == size);

    MbufPbufAdapter* bufs[size];
    pbuf vhead;
    pbuf_iter it = &vhead;
    for (int i = 0; i < size; ++ i) {
        bufs[i] = new MbufPbufAdapter(MbufPbufAdapter::Type::mbuf_to_pbuf, mbufs[i]);
        it->next = bufs[i]->_pbuf;
        it = it->next;
    }
    return vhead.next;
}

void DpdkNetif::netif_recv() {
    static rte_mbuf *mbufs[config::VDEV_rx_burst_num];
    int size, ret = 0;
    for (int i = 0; i < config::DPDK_rx_queue_num; ++ i) {
        memset(mbufs, 0, sizeof(mbufs));
        size = 0, ret = 0;
        
        while (0 == (size = dpdk::rx_burst(port_id, i, 
            mbufs, config::VDEV_rx_burst_num)));
            usleep(config::VDEV_rx_sleep);

        while ((ret += _rx_ring->push_burst(mbufs, size - ret)) != size)
            usleep(config::VDEV_rx_sleep);
    }
}

void DpdkNetif::netif_send() {
    static MbufPbufAdapter *adaters[config::VDEV_tx_burst_num];

    int size = 0;
    memset(adaters, 0, sizeof(adaters));
    while (0 == (size = _tx_ring->pop_burst(adaters, config::VDEV_tx_burst_num)))
        usleep(config::VDEV_tx_sleep);
    
    // 发送负载没有意义，直接不负载均衡了
    static rte_mbuf *mbufs[config::VDEV_tx_burst_num];
    memset(mbufs, 0, sizeof(mbufs));
    for (int i = 0; i < size; ++ i) {
        mbufs[i] = adaters[i]->_mbuf;
    }
    int ret = 0;
    while (size != (ret += dpdk::tx_burst(port_id, 0, mbufs + ret, size - ret)))
        usleep(config::VDEV_tx_sleep);
}

int DpdkNetif::run_recv(void *arg) {
    DpdkNetif *vnetif = (DpdkNetif*)arg;
    while (vnetif->stop) {
        vnetif->netif_recv();
    }
}

int DpdkNetif::run_send(void *arg) {
    DpdkNetif *vnetif = (DpdkNetif*)arg;
    while (vnetif->stop) {
        vnetif->netif_send();
    }
}

//
//
// DpdkNetifManager

DpdkNetifManager::~DpdkNetifManager() {
    for (auto &entry : _netifs) {
        delete entry.second;
    }
}

void DpdkNetifManager::init() {
    std::unique_lock<util::SpinMutex> lock(_mtx);

    /** 根据用户给定掩码初始化虚拟网卡 */
    uint32_t port_mask = config::DPDK_vaild_port_marks;

    /** 当前cpu的di */
    // todo: 需要根据实际情况做调整
    unsigned int cur_id = rte_lcore_id();
    unsigned int rx_id = rte_get_next_lcore(cur_id, true,false);
    unsigned int tx_id = rte_get_next_lcore(rx_id, true, false);
    int cnt = 0;

    for (int i = 0; i < 32; ++ i) {
        if (!(port_mask & (1 << i)))
            continue;

        DpdkNetif *netif = new DpdkNetif;
        assert(netif != nullptr);
        _netifs.insert({i, netif});

        /** 将运行线程绑定一个核心上 */
        rte_eal_remote_launch(DpdkNetif::run_send, netif, tx_id);
        rte_eal_remote_launch(DpdkNetif::run_recv, netif, rx_id);

        char mac_addr[FG_MAC_DUMP_LEN];
        util::mac_dump(mac_addr, dpdk::DPDK_ether_addr[i]);
        printf("init port %d: %s\n", i, mac_addr);

        /** 达到配置的核心最大数量，向下一个核心进行绑定(这里假设网卡的数量不会超过核心数*每核心最大网卡数) */
        if (cnt == config::VDEV_core_max_rxtx) {
            cnt = 0;
            rx_id = rte_get_next_lcore(tx_id, true, false);
            tx_id = rte_get_next_lcore(rx_id, true, false);
        }
    }
}

void DpdkNetifManager::stop() {
    std::unique_lock<util::SpinMutex> lock(_mtx);

    for (auto &entry : _netifs) {
        entry.second->stop = true;
    }
}

DpdkNetif* DpdkNetifManager::get_netif(int port) {
    std::unique_lock<util::SpinMutex> lock(_mtx);
    auto it = _netifs.find(port);
    lock.unlock();

    if (it == _netifs.end()) {
        return nullptr;
    }
    return it->second;
}

}   // fg