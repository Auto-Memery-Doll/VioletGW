#include "util.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <generic/rte_spinlock.h>
#include <iostream>
#include <mutex>
#include <string>

namespace fg {
namespace util {

std::string itoa(int i) {
    char istr[6] = {'0', '0', '0', '0', '0', '0'};
    for (int j = 5; j >= 0; -- j) {
        int r = i % 10;
        i /= 10;
        istr[j] = '0' + r; 
    }
    return std::string(istr);
}

void mac_dump(char *str, const rte_ether_addr &addr) {
    snprintf(str, 30, "%02X:%02X:%02X:%02X:%02X:%02X",
        addr.addr_bytes[0], addr.addr_bytes[1],
        addr.addr_bytes[2], addr.addr_bytes[3],
        addr.addr_bytes[4], addr.addr_bytes[5]);
}

char LWIP_netif_name_format[] = "00";
char * lwip_name(char format[], int port) {
    int size = ::strlen(format);
    std::string istr = itoa(port);
    int it = size -1;
    for (int i = istr.size()-1; i >= 0; -- i) {
        format[it--] = istr[i];
    }
    return format;
}

//
//
// SpinMutex
void SpinMutex::lock() {
    rte_spinlock_lock(&_mtx);
}

void SpinMutex::unlock() {
    rte_spinlock_unlock(&_mtx);
}

//
//
// 原子读写锁

AtomicRWLock::AtomicRWLock() 
:   _r_cnt(0)
,   _writer(false)
{
    _mtx.clear();
}

void AtomicRWLock::r_lock() {
    _mutex.lock();
    return;

    if (_writer.load()) {
        while (_mtx.test_and_set())
            ;
    }
    
    if (_r_cnt.load() > 0) {
        // 读锁已经被获取
        _r_cnt++;
        return;
    }

    /** 尝试获取读锁 */
    while (_r_cnt.load() == 0 && _mtx.test_and_set()) 
        ;
    _r_cnt++;
    std::cout << _r_cnt << std::endl;
}

void AtomicRWLock::r_unlock() {
    _mutex.unlock();
    return;

    _r_cnt --;
    if (_r_cnt.load() == 0)
        _mtx.clear();
    std::cout << _r_cnt << std::endl;
}

void AtomicRWLock::w_lock() {
    _mutex.lock();
    return;

    // 防止写饥饿
    bool except = false;
    _writer.compare_exchange_weak(except, true);
    while (_mtx.test_and_set()) 
        ;
}

void AtomicRWLock::w_unlock() {
    _mutex.unlock();
    return;

    _writer.store(false);
    _mtx.clear();
}

}   // util
}   // fg

//
//
// 特化以下std::unique_lock<fg::util::SpinMutex>
template<>
void std::unique_lock<fg::util::SpinMutex>::lock() {
    if (_M_owns) {
        this->_M_device->lock();
        this->_M_owns = true;
    }
}

template<>
std::unique_lock<fg::util::SpinMutex>::unique_lock(fg::util::SpinMutex& _mtx) 
:   _M_device(&_mtx)
,   _M_owns(false)
{
    _M_device->lock();
    _M_owns = true;
}

template<>
std::unique_lock<fg::util::SpinMutex>::~unique_lock() {
    if (_M_owns) {
        this->_M_owns = false;
        this->_M_device->unlock();
    }
}

template<>
void std::unique_lock<fg::util::SpinMutex>::unlock() {
    this->_M_owns = false;
    this->_M_device->unlock();
}


//
//
// std::unique_lock
template<>
void std::unique_lock<fg::util::AtomicRWLock>::lock() {
    if (_M_owns) {
        _M_device->w_lock();
        this->_M_owns = true;
    }
}

template<>
std::unique_lock<fg::util::AtomicRWLock>::unique_lock(fg::util::AtomicRWLock& _mtx) 
:   _M_device(&_mtx)
,   _M_owns(false) 
{
    _M_device->w_lock();
    this->_M_owns = true;
}

template<>
std::unique_lock<fg::util::AtomicRWLock>::~unique_lock() {
    if (_M_owns) {
        _M_owns = false;
        _M_device->w_unlock();
    }
}

template<>
void std::unique_lock<fg::util::AtomicRWLock>::unlock() {
    _M_owns = false;
    _M_device->w_unlock();
}

//
//
// std::shared_lock
template<>
void std::shared_lock<fg::util::AtomicRWLock>::lock() {
    if (_M_owns) {
        _M_pm->r_lock();
        _M_owns = true;
    }
}

template<>
void std::shared_lock<fg::util::AtomicRWLock>::unlock() {
    if (_M_owns) {
        _M_owns = false;
        _M_pm->r_unlock();
    }
}

template<>
std::shared_lock<fg::util::AtomicRWLock>::shared_lock(fg::util::AtomicRWLock& _mtx) 
:   _M_pm(&_mtx)
,   _M_owns(false) {
    _M_pm->r_lock();
    _M_owns = true;
}

template<>
std::shared_lock<fg::util::AtomicRWLock>::~shared_lock() {
    if (_M_owns) {
        _M_owns = false;
        _M_pm->r_unlock();
    }
}
