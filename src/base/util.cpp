#include "util.hpp"
#include <cstdio>
#include <cstring>
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

}   // util
}   // fg