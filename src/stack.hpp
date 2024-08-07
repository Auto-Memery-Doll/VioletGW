#ifndef FLOW_GATEWAY_STACK_HPP
#define FLOW_GATEWAY_STACK_HPP

#include "base/closure.hpp"
#include "base/noncopyable.hpp"
#include "base/singleton.hpp"
#include "base/type.hpp"
#include "lwip/arch.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include <climits>
#include <cstddef>
#include <list>
#include <sys/socket.h>
#include <map>
#include <memory>
#include <unistd.h>

namespace fg {

enum
{
    // 表示sendto中传入的是pbuf还是一个char[]
    // 以便函数采取不同的措施
    MSG_PBUF = 0x8000'0000,
};

class ProtoStack : public base::Singletion<ProtoStack> {
    friend Singletion<ProtoStack>;
public:
    using ptr = std::shared_ptr<ProtoStack>;
    DISABLE_MOVE(ProtoStack);
    ~ProtoStack();
    /** init the proto stack */
    void init(int fds = _fds);

    /** get a socket fd */
    int socket(int domain, int type, int protocol);

    /** tcp client */
    int connect(int fd, const struct sockaddr *addr, socklen_t len);

    /** tcp server */
    int bind(int fd, const struct sockaddr *addr, socklen_t len);
    int listen(int fd, int n);
    int accept(int fd, const struct sockaddr *addr, socklen_t len);

    /** tcp io */
    int recv(int fd, void *buf, size_t n, int flags);
    int send(int fd, void *buf, size_t n, int flags);

    /** udp io */
    int recvfrom(int fd, void *__restrict buf, size_t n, int flags, struct sockaddr *__restrict addr_len);
    int sendto(int fd, void *buf, size_t n, int flags, const struct sockaddr *addr, socklen_t addr_len);

private:
    /** pcb的类型 */
    enum PcbType {
        udp,
        tcp,  
    };

    struct FdToPcb {
        PcbType type;
        union {
            udp_pcb *upcb;
            tcp_pcb *tpcb;
        };
    };

    /** 用于处理异步的回调函数 */
    struct TcpAcceptArg : public base::Closure {
        void * Run() override {
            delete this;
            return nullptr;
        }
    };
    struct TcpRecvArg : public base::Closure {
        void * Run() override {
            delete  this;
            return nullptr;
        }
    };
    struct TcpSentArg : public base::Closure {
        void * Run() override {
            delete this;
            return nullptr;
        }
    };
    struct TcpPollArg : public base::Closure {
        void * Run() override {
            delete this;
            return nullptr;
        }
    };
    struct TcpErrArg : public base::Closure {
        void * Run() override {
            delete  this;
            return nullptr;
        }
    };
    struct TcpConnectedArg : public base::Closure {
        void * Run() override {
            delete this;
            return nullptr;
        }
    };


private:    /** 私有成员函数 */
    // 获取一个socket fd
    // 如果pcb map中还有空闲的pcb，直接从map里面拿，
    // 否则向内存池申请一个，
    int _get_fd(PcbType type);

    // 当程序结束的时候调用，销毁相关的资源
    int _distory();

private:    /** 静态函数 */
    ProtoStack() = default;

    /** fd的最大数量 */
    static const int _fds = INT_MAX;

    /** udp socket接收到数据包的时候调用的回调函数 */
    /// @param arg 用户应用的参数(udp_pcb.recv_arg)
    /// @param upcb 接收数据的udp控制块
    /// @param p 接收到的网络数据包
    /// @param addr 数据包的源ip地址
    /// @param port 数据包的源端口
    static err_t _udp_recv(void *arg, udp_pcb *upcb, pbuf *p, 
        const ip_addr_t *addr, u16_t port);

    /** tcp相关的回调函数 */
    static err_t _tcp_accept(void *arg, tcp_pcb *newpcb, err_t err);
    static err_t _tcp_recv(void *arg, tcp_pcb *tpcb, pbuf *p, err_t err);
    static err_t _tcp_sent(void *arg, tcp_pcb *tpcb, u16_t len);
    static err_t _tcp_poll(void *arg, tcp_pcb *tpcb);
    static err_t _tcp_err(void *arg, tcp_pcb *tpcb, err_t err);
    static err_t _tcp_connected(void *arg, tcp_pcb *tpcb, err_t err);

private:
    using FdMap = std::map<int, FdToPcb>;    

    /** 当一个pcb从内存中创建出来时，就不会被销毁一直复用 */
    fg_thread_t _t; // 执行线程
    FdMap _fd_map;  // fd映射到指定的控制块
    std::list<int> _free_tcp_fds;   // 空闲的socket tcp fd
    std::list<int> _free_udp_fds;   // 空闲的socket udp fd
};

inline ProtoStack::ptr proto_stack() {
    return ProtoStack::GetInstance();
}

}   // fg


#endif // !FLOW_GATEWAY_STACK_HPP