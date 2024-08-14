#ifndef FLOW_GATEWAY_BASE_BALANCE_HPP
#define FLOW_GATEWAY_BASE_BALANCE_HPP

#include <ostream>
#include <string>
#include <vector>
namespace fg {
namespace balance {

using ip_t  = std::string;

/** 查找过程中出现的错误的类型 */
enum class BalanceError {
    running,    /** 节点正常运行 */
    maintenance, /** 节点正在维护中 */
    unavailable,    /** 节点不可用 */
    starting,   /** 节点正在启动中 */
    stopped,    /** 节点已经被关闭 */

    none,       /** 集群中没有节点 */
    ok, 
    unkonwn,    /** 未知错误 */   
};

inline std::ostream& operator<<(std::ostream& os, BalanceError e) {
    switch (e) {
    case BalanceError::running:
        os << "node is ok(running)\n";
        break;
    case BalanceError::maintenance:
        os << "node is maintenance\n";
        break;
    case BalanceError::unavailable:
        os << "node is unavailable(gateway cannot communicate with it)\n";
        break;
    case BalanceError::starting:
        os << "node is starting, please request a while\n";
        break;
    case BalanceError::stopped:
        os << "nod is stoppend, and donnot response request\n";
        break;
    case BalanceError::none:
        os << "there is empty closue.\n";
        break;
    case BalanceError::unkonwn:
        os << "unkonwn error\n";
        break;
    };
    return os;
}

struct GetRet {
    ip_t name;
    BalanceError e;
};

// 抽象类
// 提供接口方便动态平滑更改负载均衡算法
class BaseBalance {
public:
    /** 初始化负载均衡器 */
    virtual void init(void* arg);
    /** 增加节点 */
    virtual void add(const ip_t& elt) = 0;
    /** 删除节点 */
    virtual void remove(const ip_t& elt) = 0;
    /** 重新设置集群的状态 */
    virtual void set(const std::vector<ip_t>& elts) = 0;
    /** 根据id获取对应的节点 */
    virtual GetRet get(const ip_t& id) = 0;
    /** 获取集群的节点信息 */
    virtual std::vector<ip_t> get_cluster() = 0;
};

}   // balance
}   // fg

#endif // !FLOW_GATEWAY_BASE_BALANCE_HPP