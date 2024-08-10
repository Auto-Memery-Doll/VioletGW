#include "util.hpp"
#include <cstdio>
#include <cstring>
#include <generic/rte_spinlock.h>
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

}   // util
}   // fg

//
//
// 特化以下std::unique_lock<fg::util::SpinMutex>
template<>
void std::unique_lock<fg::util::SpinMutex>::lock() {
    this->_M_device->lock();
}

template<>
std::unique_lock<fg::util::SpinMutex>::unique_lock(fg::util::SpinMutex& _mtx) 
:   _M_device(&_mtx)
,   _M_owns(false)
{
    _M_device->lock();
}

template<>
std::unique_lock<fg::util::SpinMutex>::~unique_lock() {
    this->_M_device->unlock();
}

template<>
void std::unique_lock<fg::util::SpinMutex>::unlock() {
    this->_M_device->lock();
}
