#ifndef FLOW_GATEWAY_NETIF_HPP
#define FLOW_GATEWAY_NETIF_HPP

#include "base/noncopyable.hpp"
#include "lwip/pbuf.h"
namespace fg {

class Netif : public base::noncopyable {

virtual pbuf* netif_rx() = 0;
virtual void netif_tx(pbuf*) = 0;

};

}   // fg

#endif // !FLOW_GATEWAY_NETIF_HPP