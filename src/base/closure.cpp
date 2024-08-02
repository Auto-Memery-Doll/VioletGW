#include "base/closure.hpp"
#include "base/type.hpp"
#include <atomic>
#include <mutex>
#include <thread>

namespace fg {
namespace base {

auto ClosureQueue::commit(Closure *done) -> bool {
    if (done == nullptr) return false;
    if (_stopped.load(std::memory_order_relaxed)) {
        return false;
    }

    std::unique_lock<fg_mutex_t> lock(_mtx);
    _queue.push_back(done);
    return true;
}

auto ClosureQueue::init() -> bool {
    _stopped.store(false, std::memory_order_relaxed);
    _queue.clear();
    // 启动异步线程
    _t = std::thread([this](){
        this->run();
    });
}

auto ClosureQueue::stop() -> bool {
    _stopped.store(true, std::memory_order_relaxed);
    _cond.notify_all();
}

auto ClosureQueue::join() -> void {
    std::unique_lock<fg_mutex_t> lock(_mtx);
    stop();
    _cond.wait(lock, [this](){
        return this->_queue.empty();
    });
}

auto ClosureQueue::run() -> bool {
    std::unique_lock<fg_mutex_t> lock(_mtx);
    lock.unlock();

    int tasks = 0;
    while (!stop())
    {
        lock.lock();
        tasks = _queue.size();
        if (tasks == 0) {
            _cond.wait(lock, [this](){
                return _queue.size() > 0 || stop();
            });
            tasks = _queue.size();
        }
        lock.unlock();

        // 如果是因为stop被唤醒，也需要将任务处理完才能结束，
        // 否则可能会造成内存泄露
        for (; tasks > 0; --tasks) {
            Closure * done = _queue.front();
            _queue.pop_front();
            done->Run();
        }
    }
}


}   // base
}   // fg