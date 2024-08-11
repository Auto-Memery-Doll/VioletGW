#ifndef FLOW_GATEWAY_UTIL_HPP
#define FLOW_GATEWAY_UTIL_HPP

#include "base/noncopyable.hpp"
#include "base/singleton.hpp"
#include "base/type.hpp"
#include "lwip/ip4_addr.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <random>
#include <rte_ether.h>
#include <shared_mutex>
#include <string>
namespace fg {
namespace util {

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
char * lwip_name(char format[], int port);
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

//
//
// 原子读写锁
class AtomicRWLock : public base::noncopyable {
public:

    AtomicRWLock();
    ~AtomicRWLock() = default;
    void r_lock();
    void r_unlock();
    void w_lock();
    void w_unlock();
private:
    std::mutex _mutex;  /** 先用互斥量过渡以下 */

    std::atomic_flag _mtx;  /** 锁 */
    std::atomic<int> _r_cnt;  /** 读者的计数 */
    std::atomic<bool> _writer;
};

//
//
// 随机数生成器
class RandomGenerator : public base::Singletion<RandomGenerator> {
    friend class base::Singletion<RandomGenerator>;
public:
    int random_int(int begin, int end) {
        std::uniform_int_distribution<> dis(begin, end);
        return dis(gen);
    }

private:
    RandomGenerator() 
    :   gen(rd())
    {}

private:
    std::random_device rd;
    std::mt19937 gen;
};

inline int  generate_random(int begin, int end) {
    /** 默认是int类型 */
    return RandomGenerator::GetInstance()->random_int(begin, end);
}

//
//
// 计时器
class TestTimer {
public:
    TestTimer()
    :   _begin(std::chrono::high_resolution_clock::now())
    {}

    void stop() {
        _end = std::chrono::high_resolution_clock::now();
    }

    void beign() {
        _begin = std::chrono::high_resolution_clock::now();
    }

    void time_inter() {
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(_end - _begin);
        std::cout << "Time elapsed: " << duration.count() << " seconds." << std::endl;
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> _begin, _end;
};

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

//
//
// std::unique_lock的fg::util::AtomicRWLock特化
template<>
void std::unique_lock<fg::util::AtomicRWLock>::lock();

template<>
std::unique_lock<fg::util::AtomicRWLock>::unique_lock(fg::util::AtomicRWLock&);

template<>
std::unique_lock<fg::util::AtomicRWLock>::~unique_lock();

template<>
void std::unique_lock<fg::util::AtomicRWLock>::unlock();

//
//
// std::shared_lock
template<>
void std::shared_lock<fg::util::AtomicRWLock>::lock();

template<>
void std::shared_lock<fg::util::AtomicRWLock>::unlock();

template<>
std::shared_lock<fg::util::AtomicRWLock>::shared_lock(fg::util::AtomicRWLock&);

template<>
std::shared_lock<fg::util::AtomicRWLock>::~shared_lock();


#endif // !FLOW_GATEWAY_UTIL_HPP