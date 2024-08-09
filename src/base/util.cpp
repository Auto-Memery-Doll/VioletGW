#include "util.hpp"
#include <cstdio>
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

}   // util
}   // fg