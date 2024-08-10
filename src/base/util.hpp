#ifndef FLOW_GATEWAY_UTIL_HPP
#define FLOW_GATEWAY_UTIL_HPP

#include "base/noncopyable.hpp"
#include "base/type.hpp"
#include "lwip/ip4_addr.h"
#include <chrono>
#include <cstdint>
#include <mutex>
#include <rte_ether.h>
#include <string>
namespace fg {
namespace util {

// 将一个mac地址转换成int64整型变量
auto mac_to_int64(mac_addr_t mac) -> int64_t;

// 将一个int64整型变量转为一个mac地址
// 如果失败则返回false
/// @param mac 返回参数
bool int64_to_mac(int64_t i_mac, mac_addr_t mac);

/// 将注册一个信号
void reg_signal(int signal);

/// 将一个数变为字符串
std::string itoa(int i);

/// 根据port id创建vnetif的ring的名字
inline std::string TX_RING_NAME(int port) {
    static std::string name = "fg_tx_ring_";
    return name + itoa(port);
}

/// 根据port id创建vntif的ring的名字
inline  std::string RX_RING_NAME(int port) {
    static std::string name = "fg_rx_ring_";
    return name + itoa(port);
}

/// 将网卡的mac地址输出
#define FG_MAC_DUMP_LEN 30
void mac_dump(char *str, const rte_ether_addr& addr);

/// fg_spinlock_t
class SpinMutex : public base::noncopyable {
public:
    void lock();
    void unlock();

private:
    friend class std::unique_lock<SpinMutex>;
    void try_lock() {}
    template<typename Rep, typename Period>
    void try_lock_for(const std::chrono::duration<Rep, Period> &) {}
    template<typename Clock, typename Duration>
    void try_lock_until(const std::chrono::time_point<Clock, Duration> &) {}
    void mutex() const {}
    void release() {}
    void swap(SpinMutex&) {}
private:
    spinlock_t _mtx;
};

/// 生成lwip netif的name
extern char LWIP_netif_name_format[];
inline char * lwip_name(char format[], int port);
#define LWIP_NETIF_NAME(port) util::lwip_name(util::LWIP_netif_name_format, port)

/// 生成一个网关地址
#define GW_ADDR(_1, _2, _3, _4)                             \
const struct ip4_addr LWIP_gw_adddr = []() -> struct ip4_addr {  \
    struct ip4_addr gw_addr;                                \
    IP4_ADDR(&gw_addr, _1, _2, _3, _4);                     \
    return gw_addr;                                         \
}()

/// 生成一个网络掩码
#define NETMASK_ADDR(_1, _2, _3, _4)                                \
const struct ip4_addr LWIP_netmask_adddr = []() -> struct ip4_addr {     \
    struct ip4_addr netmask_addr;                                   \
    IP4_ADDR(&netmask_addr, _1, _2, _3, _4);                        \
    return netmask_addr;                                            \
}()

/// 生成一个ip地址
#define IP_ADDR(num, _1, _2, _3, _4)                                \
const struct ip4_addr LWIP_ip_##num##_addr = []() -> struct ip4_addr {   \
    struct ip4_addr ip_addr;                                        \
    IP4_ADDR(&ip_addr, _1, _2, _3, _4);                             \
    return ip_addr;                                                 \
}()

}   // util
}   // fg

//
//
// 声明一个std::unique_lock<fg::util::SpinMutex>
template<>
void std::unique_lock<fg::util::SpinMutex>::lock();

template<>
std::unique_lock<fg::util::SpinMutex>::unique_lock(fg::util::SpinMutex& _mtx);

template<>
std::unique_lock<fg::util::SpinMutex>::~unique_lock();

template<>
void std::unique_lock<fg::util::SpinMutex>::unlock();

#endif // !FLOW_GATEWAY_UTIL_HPP