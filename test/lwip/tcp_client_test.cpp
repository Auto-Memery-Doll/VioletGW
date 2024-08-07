#include "lwip/err.h"
#include "lwip/ip.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/prot/ieee.h"
#include "lwip/tcp.h"
#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/netif.h"
#include "netif/ethernet.h"
#include "lwip/snmp.h"
#include "lwip/timeouts.h"
#include "lwip/def.h"
#include "lwip/init.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string.h>

 /* USER CODE BEGIN 0 */
 #define DEST_IP_ADDR0               192
 #define DEST_IP_ADDR1               168
 #define DEST_IP_ADDR2                 0
 #define DEST_IP_ADDR3               102

 #define DEST_PORT                  6000

 #define UDP_SERVER_PORT            5002
 #define UDP_CLIENT_PORT            5002

 #define LOCAL_PORT                 5001

 /*Static IP ADDRESS: IP_ADDR0.IP_ADDR1.IP_ADDR2.IP_ADDR3 */
 #define IP_ADDR0                    192
 #define IP_ADDR1                    168
 #define IP_ADDR2                      0
 #define IP_ADDR3                    122

 /*NETMASK*/
 #define NETMASK_ADDR0               255
 #define NETMASK_ADDR1               255
 #define NETMASK_ADDR2               255
 #define NETMASK_ADDR3                 0

 /*Gateway Address*/
 #define GW_ADDR0                    192
 #define GW_ADDR1                    168
 #define GW_ADDR2                      0
 #define GW_ADDR3                      1

#define TCP_CLIENT_PORT 11000

struct netif g_netif;
ip4_addr_t ip_addr;
ip4_addr_t netmask;
ip4_addr_t gw;
uint8_t IP_ADDRESS[4];
uint8_t NETMASK_ADDRESS[4];
uint8_t GW_ADDRESS[4];

void Lwip_Init(void) {
    
    IP4_ADDR(&ip_addr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
    IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
    IP4_ADDR(&gw, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);

    lwip_init();
    netif_add(&g_netif, &ip_addr, &netmask, &gw, 
        NULL, NULL/*netif init*/, NULL/*netif input*/);

    netif_set_default(&g_netif);

    if (netif_is_link_up(&g_netif)) {
        netif_set_up(&g_netif);
    } else {
        netif_set_down(&g_netif);
    }

    /** USER CODE BEGIN 3 */    

    /** USER CODE END 3 */
}

struct tcp_pcb *client_pcb = nullptr;
void TCP_Client_Init();

void client_err(void *arg, err_t err) {
    printf("connect error! closed by core!!\n");
    printf("try to connect to server again!!\n");

    tcp_close(client_pcb);

    TCP_Client_Init();
}

err_t client_send(void *arg, struct tcp_pcb *tpcb) {
    uint8_t send_buf[] = "This is a TCP Client test...\n";

    tcp_write(tpcb, send_buf, sizeof(send_buf), 1);

    return ERR_OK;
}

static err_t client_recv(void *arg,
                         struct tcp_pcb *tpcb,
                         struct pbuf *p,
                         err_t err) {
    if (p != NULL) {
        /** 更新窗口 */
        tcp_recved(tpcb, p->tot_len);

        /** 返回接收到的数据 */
        tcp_write(tpcb, p->payload, p->tot_len, 1);

        memset(p->payload, 0, p->tot_len);
        pbuf_free(p);
    }
    else if (err == ERR_OK) {
        printf("server has been disconnected!\n");
        
        tcp_close(tpcb);
        TCP_Client_Init();
    }
    return ERR_OK;
}

static err_t client_connected(void *arg, 
                              struct tcp_pcb *pcb,
                              err_t err) {
    printf("connected ok!\n");

    /** 注册一个周期周期性的回调函数 */
    tcp_poll(pcb, client_send, 2);

    /** 注册一个接收函数 */
    tcp_recv(pcb, client_recv);

    return ERR_OK;
}

void TCP_Client_Init(void) {
    
    ip4_addr_t server_ip;

    /** 创建一个TCP控制块 */
    client_pcb = tcp_new();
    IP4_ADDR(&server_ip, 192, 168, 0, 181);

    printf("client start connect!\n");

    ip_addr_t addr;
    addr.u_addr.ip4 = server_ip;
    addr.type = ETHTYPE_IP;
    // 开始连接
    tcp_connect(client_pcb, &addr, TCP_CLIENT_PORT, client_connected);

    // 注册异常处理函数
    tcp_err(client_pcb, client_err);
}

int flag = 0;
int main(int argc, const char** argv) {
 
    Lwip_Init();
    TCP_Client_Init();
    while (true) {
        if (flag) {
            flag = 0;
            
        }
        sys_check_timeouts();
    }

    return 0;
}