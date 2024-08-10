#ifndef FLOW_GATEWAY_CLOSURE_HPP
#define FLOW_GATEWAY_CLOSURE_HPP

#include "base/noncopyable.hpp"
#include "base/singleton.hpp"
#include "base/type.hpp"
#include <atomic>
#include <deque>
#include <memory>

namespace fg {
namespace base {

// 抽象类：用于执行相应的异步任务
class Closure {
public:
    virtual ~Closure() {}
    virtual void* Run() = 0;
};

class ClosureQueue : public Singletion<ClosureQueue> {
public:
    using ptr = std::shared_ptr<ClosureQueue>;

    ~ClosureQueue() = default;
    DISABLE_MOVE(ClosureQueue);

    bool commit(Closure* closure);
    bool init();
    bool stop();
    void join();
    
private:
    friend Singletion<ClosureQueue>;
    ClosureQueue() = default;

    bool run();

private:
    fg_thread_t _t;
    std::deque<Closure*> _queue;
    std::atomic_bool _stopped;
    fg_cond_t _cond;
    fg_mutex_t _mtx;
};

inline ClosureQueue::ptr closure_queue() {
    return ClosureQueue::GetInstance();
}

}   // base
}   // fg

#endif // !FLOW_GATEWAY_CLOSURE_HPP