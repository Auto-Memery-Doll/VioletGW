#ifndef FLOW_GATEWAY_NETIF_HPP
#define FLOW_GATEWAY_NETIF_HPP

#include "base/noncopyable.hpp"
#include "lwip/pbuf.h"
#include <cstdint>
namespace fg {

class Netif : public base::noncopyable {
public:
    virtual pbuf* netif_rx() = 0;
    virtual void netif_tx(pbuf*) = 0;
    //virtual uint64_t sys_now() = 0;
    //virtual void sys_tick_handler() = 0;

};

}   // fg

#endif // !FLOW_GATEWAY_NETIF_HPP