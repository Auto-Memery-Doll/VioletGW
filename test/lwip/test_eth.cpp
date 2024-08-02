#include "lwip/arch.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/mem.h"
#include "lwip/netif.h"
#include "lwip/err.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "lwip/pbuf.h"
#include "lwipopts.h"

// ip地址
#define IP_ADDR0    192
#define IP_ADDR1    168
#define IP_ADDR2    1
#define IP_ADDR3    122
// 掩码地址
#define NETMASK_ADDR0   255
#define NETMASK_ADDR1   255
#define NETMASK_ADDR2   255
#define NETMASK_ADDR3   0
// 网关地址
#define GW_ADDR0    192
#define GW_ADDR1    168
#define GW_ADDR2    1
#define GW_ADDR3    1


#define NETIF_MTU 100

static void arp_timer(void* arg) {

}

static void low_level_init(struct netif * netif_) {

    // 设置网卡的mac地址
    u8_t mac_addr[6];
    memcpy(netif_->hwaddr, mac_addr, sizeof(mac_addr));

    // ip最大传输单元
    netif_->mtu = NETIF_MTU;

    // 支持arp协议
    netif_->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

}


/// 将从网卡中接收的读取到pbuf中
void ethernetif_input(struct netif *netif_, struct pbuf *p) {
    err_t errval;
    struct pbuf *q;

    // 网卡存放数据包的地址
    uint8_t *buffer;

    uint32_t buffer_or_offset = 0;
    uint32_t frame_length = 0;
    uint32_t bytes_left_to_copy = 0;
    uint32_t payload_offset = 0;

    struct pbuf *pbuf_ = pbuf_alloc(PBUF_RAW, 1, PBUF_POOL);
    pbuf_free(pbuf_);
}

void ethernetif_output(struct netif *netif_) {
    
}

void ethernetif_update_config(struct netif *netif_) {

}

void etnernetif_notify_conn_changed(struct netif * netif_) {

}

void lwip_init_test() {
    struct ip4_addr ipaddr;
    struct ip4_addr netmask;
    struct ip4_addr gw;
    IP4_ADDR(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
    IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
    IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);

    lwip_init();

    struct netif gnetif;
    netif_add(&gnetif, &ipaddr, &netmask, 
        &gw, NULL, NULL, NULL);

    netif_set_default(&gnetif);

    if (netif_is_link_up(&gnetif)) {
        netif_set_up(&gnetif);
    }
    else {
        netif_set_down(&gnetif);
    }
}

int main(int argc, const char** argv) {

    lwip_init_test();
    while (1)
    {
           
    }

    return 0;
}