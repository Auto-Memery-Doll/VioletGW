#ifndef FLOW_GATEWAY_UTIL_HPP
#define FLOW_GATEWAY_UTIL_HPP


#include "base/type.hpp"
#include <cstdint>
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

}
}

#endif // !FLOW_GATEWAY_UTIL_HPP