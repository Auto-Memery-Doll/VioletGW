#ifndef FLOW_GATEWAY_UTIL_HPP
#define FLOW_GATEWAY_UTIL_HPP

#include "base/noncopyable.hpp"
#include "base/singleton.hpp"
#include "base/type.hpp"
#include "lwip/ip4_addr.h"
#include <atomic>
#include <chrono>
#include <cstdint>
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
std::string ui32toa(uint32_t);
std::string dtoa(double);
std::string ui64toa(uint64_t);

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

//
//
// 日志库的代码生成器

/** 生成日志类的各级日志输出器 */
#define FG_LOG_TEMPLATE(type, level) \
class level {                \
public: \
    FG_##type##_LOG_TEMPLATE(level)    \
    FG_##type##_LOG_USER(level) \
}


/** 一次性生成所有的日志输出其 */
#define FG_LOG_TEMPLATE_ALL(type) \
    FG_LOG_TEMPLATE(type, info);  \
    FG_LOG_TEMPLATE(type, debug); \
    FG_LOG_TEMPLATE(type, warnning);  \
    FG_LOG_TEMPLATE(type, error)


/** 生成对应的工厂函数 */
#define FG_LOG_FACTORY(type, level) \
level& type##_logger_##level(const char *file, int line, const char* func)

/** 输入日志的模板：[level] \n 文件名+行数 \n 函数名 \n 日志内容 */
#define FG_LOG_FORMAT(level) _##level \
    << file << ":" << line << "\n" \
    << func << "\n";

/** logger的成员属性 */
#define FG_LOG_MEMBER   \
private:    \
    info _info; \
    debug _debug;   \
    warnning _warnning; \
    error _error

#define FG_LOG_CLASS(type) \
class type##_logger : public base::Singletion<type##_logger> {    \
    friend class base::Singletion<type##_logger>;  \
public: \
    FG_LOG_TEMPLATE_ALL(type);    \
FG_LOG_MEMBER;  \
public: 

#define FG_LOG_CLASS_END(type, param) };

/** 日志输出的公用宏 */
#define LOG(type, level)  fg::type##_logger::GetInstance()-> type##_logger_##level(__FILE__, __LINE__, __func__)

//
//
// crc32哈希算法
// cyclic redundanct check 32是一种广泛使用的循环冗余校验算法，它主要用于检测数据传输
// 过程中可能发生的错误。CRC32生成一个32位的校验值，可以附加到数据末尾，在接收端重新计算
// 并验证数据的完整性
//
// 1.多项式除法：
//      发送方将数据视为一个二进制的多项式，使用该多项式去除以一个固定的生成多项式
//      得到的余数就是CRC值
// 2.位运算
//      实际的CRC运算通常使用位运算来实现，包括XOR，移位等操作
// 3.为了提高效率，CRC32通常使用预先计算好的查找标来进行快速计算
uint32_t crc32(const std::string& data);


//
//
// fnv哈希算法(直接使用boost库)
uint32_t fnv(const std::string& data);

//
//
// 获取当前的时间
inline fg_clock_t now() {
    return std::chrono::high_resolution_clock::now();
}

//
//
// 将时间转换成字符串
std::string clock_to_str(const fg_clock_t& tp);
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