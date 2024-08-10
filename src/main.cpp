
#include "dpdk_netif.hpp"
#include "netif_driver.hpp"
#include "stack.hpp"

int main(int argc, char** argv) {
    fg::dpdk::init(argc, argv); /** dpdk eal环境初始化 */

    fg::dpdk_netif_mg()->init();    /** 初始化dpdk网卡，从网络中接收数据 */
    fg::proto_stack()->init();  /** 启动协议栈 */
    fg::lwip_netif_init();  /** 初始化协议栈的网卡 */
    fg::netif_driver()->init(); /** 从dpdk网卡上接收数据传递到协议栈 */

    while (true);

    return 0;
}
