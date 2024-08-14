#ifndef FLOW_GATEWAY_HEARTBEAT_MONITOR_HPP
#define FLOW_GATEWAY_HEARTBEAT_MONITOR_HPP

#include "balance/base_balance.hpp"
#include "base/noncopyable.hpp"
#include "base/type.hpp"
#include <memory>
#include <vector>

namespace fg {

/** 描述节点的状态 */
enum class node_status { 
    running,    /** 节点正在运行中 */
    unvaild,    /** 节点失去联系，需要在指定的时间的响应心跳 */
    shutdown,   /** unvaild之后，节点未能在固定的时间内响应心跳，从集群中移除 */
    reconnect,  /** 节点在shutdown的状态下，回应了心跳 */
};

class HeartbeatMonitor : public base::noncopyable {
public:
    using ptr = std::unique_ptr<HeartbeatMonitor>;

    struct InitArg {
        int retry_after_unvaild;    /** 重试心跳检测的次数 */
        timeval_ms running_check;   /** 服务器正常运行时，发送心跳的时间间隔 */
        timeval_s shutdown_check;   /** 服务器宕机时，探测对方是否正常运行的时间间隔 */
    };

    inline void init(const InitArg& arg) {
        _retry_after_unvaild = arg.retry_after_unvaild;
        _running_check = arg.running_check;
        _shutdown_check = arg.shutdown_check;
    }

    /** 查看一个节点的状态 */
    virtual node_status  get_status(balance::ip_t) = 0; 

    /** 发送心跳包检测一个节点的健康状态 */
    // 心跳检测的实现：tcp ping
    // 网关主动发起一个tcp连接，如果连接建立成功，则视为对端响应了心跳，并立即断开
    // 如果对端未能即时响应，等待一定的时间后，在一次发起连接，重试固定的次数之后，视为
    // 该节点被shutdown了，
    // 在节点被shutdown之后，网关以一个新的时间间隔来探测对端的是否重新运行
    virtual void check_nodes() = 0;

    /** 调整当前集群的成员 */
    virtual void set_cluster(const std::vector<balance::ip_t>& cluster) = 0;

    /** 查看当前集群的成员 */
    virtual std::vector<balance::ip_t> get_cluster() = 0;

    virtual void add_node(const balance::ip_t& node) = 0;
    virtual void remove_node(const balance::ip_t& node) = 0;

private:
    int _retry_after_unvaild;
    timeval_ms _running_check;
    timeval_s _shutdown_check;
};


}   // fg

#endif // !FLOW_GATEWAY_HEARTBEAT_MONITOR_HPP