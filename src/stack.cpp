#include "stack.hpp"
#include "base/err.hpp"
#include "lwip/arch.h"
#include "lwip/err.h"
#include "lwip/ip.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/prot/ieee.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"

namespace fg {

/******** Udp ********/
// 将lwip的raw api进行封装

//
// 创建一个udp控制块，将控制块与对应的端口号进行绑定
// 才能进行报文发送，而在接收报文的时候，这个端口号就是udp报文的唯一标识
// 否则udp报文将无法递交到应用层去处理
static inline auto UdpNew(void) -> udp_pcb* {
    return udp_new_ip_type(IPADDR_TYPE_V4); // 默认创建ipv4的控制块
}

static inline auto UdpRemove(udp_pcb *pcb) -> void {
    return udp_remove(pcb);
}

//
// 绑定控制块，（在服务端调用）
// 将本机ip地址与端口号填写在UDP控制块中，以便表示唯一的应用
// 并且能正常与远端主机进行UDP通信
// 初始化local_ip和local_port，并将UDP控制块添加到udp_pcbs链表中
static inline auto UdpBind(udp_pcb *pcb, const ip_addr_t *ipaddr, u16_t port) -> FG_ERR {
    return (FG_ERR)udp_bind(pcb, ipaddr, port);
}
static inline auto UdpBindNetif(udp_pcb *pcb, const netif* netif) -> void {
    return udp_bind_netif(pcb, netif);
}

// 
// （在客户端调用）
// 设置控制块中的远端ip地址和端口号，然后将UDP控制块的状态
// 设置为会话状态UDP_FLAGS_CONNECTED，并且将UDP控制块插入到upd_pcbs链表中
static inline auto UdpConnect(udp_pcb *pcb, const ip_addr_t *ipaddr, u16_t port) -> FG_ERR {
    return (FG_ERR)udp_connect(pcb, ipaddr, port);
}

// 
// （在客户端调用）
// 断开连接，将udp pcb从udp pcb链表中移除，并将pcb相应的信息清除
// 但是并不会释放UDP控制块，即不会释放UDP控制块的内存
static inline auto UdpDisconnect(struct udp_pcb *pcb) -> void {
    return udp_disconnect(pcb);
}

//
// 接收数据（在服务端和客户端调用）
static inline auto UdpRecv(struct udp_pcb *pcb, udp_recv_fn recv, void *recv_arg) -> void {
    return udp_recv(pcb, recv, recv_arg);
}

//
// 发送数据（在服务端和客户端调用）
static inline auto UdpSendto(struct udp_pcb *pcb, struct pbuf *p,
                                 const ip_addr_t *dst_ip, u16_t dst_port) -> FG_ERR {
    return (FG_ERR)udp_sendto(pcb, p, dst_ip, dst_port);
}

static inline auto UdpSend(struct udp_pcb *pcb, struct pbuf *p) -> FG_ERR {
    return (FG_ERR)udp_send(pcb, p);
}

static inline auto UdpSentoIf(struct udp_pcb *pcb, struct pbuf *p,
                                 const ip_addr_t *dst_ip, u16_t dst_port,
                                 struct netif *netif) -> FG_ERR {
    return (FG_ERR)udp_sendto_if(pcb, p, dst_ip, dst_port, netif);
}

static inline auto UdpSendIfSrc(struct udp_pcb *pcb, struct pbuf *p,
                                 const ip_addr_t *dst_ip, u16_t dst_port,
                                 struct netif *netif, const ip_addr_t *src_ip) -> FG_ERR {
    return (FG_ERR)udp_sendto_if_src(pcb, p, dst_ip, dst_port, netif, src_ip);
}

/******** Tcp ********/

//
// 创建一个ipv4的tcp控制块
static inline auto TcpNew(void) -> tcp_pcb * {
    return tcp_new_ip_type(IPADDR_TYPE_V4);
}

//
// （在服务端调用）
// 将控制块绑定到指定的端口上
static inline auto TcpBind(struct tcp_pcb *pcb, 
                           const ip_addr_t *ipaddr, 
                           u16_t port) {
    return tcp_bind(pcb, ipaddr, port);
}

//
// （在服务端调用）
//  监听tcp pcb的状态，它让服务器处于监听状态，等待tcp客户端连接并且去处理它
//  
static inline auto TcpListen(struct tcp_pcb *tpcb) {
    return tcp_listen(tpcb);
}

// 
// （在服务端调用）
// 当服务器进入监听状态后，就需要立即调用这个函数
// 它向监听TCP控制块的accept字段中注册一个tcp_accept_fn类型的函数，当检测到
// 客户端的连接时，内核就会调用这个函数，已完成连接操作，
// 在accept()用户需要处理这些连接
static inline auto TcpAccept(struct tcp_pcb *tpcb, tcp_accept_fn accept) {
    return tcp_accept(tpcb, accept);
}

//
// （在客户端调用） 
// 与服务端建立连接
/// @param connected lwip内核在注册成功之后就会自动调用这个函数
static inline auto TcpConnect(struct tcp_pcb *tpcb,
                              const ip_addr_t *ipaddr,
                              u16_t port,
                              tcp_connected_fn connected) {
    return tcp_connect(tpcb, ipaddr, port, connected);
}

// 
// （在客户端和服务端都能够调用）
// 断开连接，但是需要注意的是，lwip内核会根据tpc控制块的不同状态来执行不同的操作
//  
// 状态1：CLOSED
//      将TCP控制块从绑定链表tcp_bound_pcbs中移除，并且释放TCP控制块的内存空间
// 状态2：LISTENING
//      将TCP控制块从监听链表tcp_listen_pcbs中移除，并且释放控制块的内存空间
// 状态3：SYN_SENT
//      将TCP控制块从tcp_active_pcbs链表中删除，并且释放控制块的内存空间
// 其他状态：
//      通过tcp_close_shutdown_fin()函数来处理，主动关闭TCP连接
static inline auto TcpClose(struct tcp_pcb *tpcb) {
    return tcp_close(tpcb);
}

//
// （协议栈内部调用）
// 向一个控制块中注册相应的接收数据的回调函数
// 当内核接收到数据的时候调用这个回调函数进而让数据递交到应用层中
static inline auto TcpRecv(struct tcp_pcb* tpcb, 
                           tcp_recv_fn recv) {
    return tcp_recv(tpcb, recv);
}

//
// （协议栈内部调用）
// 向一个控制块中注册相应的发送数据的回调函数
// 当发送的数据被对方确认接收后，内核会将发送窗口向后移动，
// 并且调用该回调函数来告知应用，数据已经被对方接收了，
// 此时用户能能够根据这个函数来讲已经发送的数据删除掉或者发送新的数据
static inline auto TcpSent(struct tcp_pcb *tpcb, 
                           tcp_sent_fn sent) {
    return tcp_sent(tpcb, sent);
}

//
// （协议栈内部调用）
// 用于异常处理，
// 可以拥有完成在连接异常的一些处理，比如连接失败的时候，
// 我们可以自行释放TCP控制块的内存空间，会哦这选择重连
static inline auto TcpErr(struct tcp_pcb *tpcb, tcp_err_fn err) {
    return tcp_err(tpcb, err);
}

//
// （在协议栈内部使用）
// 内核会周期性调用控制块中的poll回调函数，调用周期为|interval * 0.5|s
// 因为0.5s是内核定时器的处理周期，用户可以适当使用poll回调完成一些周期性的事件
// 比如：检测连接的情况，周期性发送一些数据
static inline auto TcpPoll(struct tcp_pcb *tpcb, tcp_poll_fn poll, u8_t interval) {
    return tcp_poll(tpcb, poll, interval);
}

// 
// （客户端和服务端都能够调用）
// 构建报文段，内核在构建好报文段之后并不会马上发送，而是会等待缓冲区的报文长度
// 达到一定长度时候才会发送，或者内核的tcp定时器触发的时候发送，
// 如果希望立即发送数据，可以在调用tcp_write()之后调用tcp_output();
static inline auto TcpWrite(struct tcp_pcb *tpcb, 
                            const void *dataptr,
                            u16_t len) {
    return tcp_write(tpcb, dataptr, len, 1);
}

// 
// （协议栈内部调用）
// 更新tcp连接的接收窗口
static inline auto TcpAckRecv(struct tcp_pcb *tpcb, u16_t len) {
    return tcp_recved(tpcb, len);
}

}   // fg