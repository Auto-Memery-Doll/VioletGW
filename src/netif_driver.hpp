#ifndef FLOW_GATEWAY_NETIF_DRIVER_HPP
#define FLOW_GATEWAY_NETIF_DRIVER_HPP

#include "base/singleton.hpp"
#include "base/type.hpp"
#include <atomic>
#include <memory>
namespace fg {

extern void lwip_netif_init();

class NetifDriver : public base::Singletion<NetifDriver> {
    friend class base::Singletion<NetifDriver>;
public:
    using ptr = std::shared_ptr<NetifDriver>;
    ~NetifDriver() = default;

    void init();
    void join();

    static void input(int port);
private:
    static void run(void *arg);
    NetifDriver() = default;
    std::atomic_bool _stop = false;
    fg_thread_t _worker;
};

inline auto netif_driver() -> NetifDriver::ptr {
    return NetifDriver::GetInstance();
}

}   // fg


#endif // !FLOW_GATEWAY_NETIF_DRIVER_HPP