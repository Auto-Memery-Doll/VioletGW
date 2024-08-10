#ifndef FLOW_GATEWAY_NETIF_DRIVER_HPP
#define FLOW_GATEWAY_NETIF_DRIVER_HPP

namespace fg {

class NetifDriver {
public:
    static void input(int port);
private:
    NetifDriver() = delete;
    ~NetifDriver() = delete;
};

}   // fg


#endif // !FLOW_GATEWAY_NETIF_DRIVER_HPP