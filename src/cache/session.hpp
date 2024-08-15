#ifndef FLOW_GATEWAY_SESSION_HPP
#define FLOW_GETEWAY_SESSION_HPP

#include "base/closure.hpp"
#include "base/iobuf.hpp"
#include "base/noncopyable.hpp"
#include <cstddef>
#include <future>
#include <memory>
#include <sys/stat.h>
#include <sys/types.h>

namespace fg {
namespace cache {
    
// 用于维护一次数据传输，
// 避免大文件一次性占用了过多的内存
template<int(*send)(int, base::IOBuf*)>
class Session : public base::noncopyable {

public:
    using ptr = std::shared_ptr<Session<send>>;
    Session(int fd);
    ~Session();

    /** 异步执行发送任务 */
    class SendClosure : public base::Closure {
    public:
        void* Run(void) override;
        
        Session<send>* _seesion;
        base::IOBuf* buf;
    };

    void start();
    void join();

private:
    struct stat _info;  /** 会话文件的信息 */
    off_t _offset;  /** 已经传输的字节数 */
    size_t _remain; /** 剩余的字节数 */

    std::promise<size_t> _promise;  /** 调用进程等待传输是否结束 */
};


}   // cache
}   // fg


#endif // !FLOW_GATEWAY_SESSION_HPP