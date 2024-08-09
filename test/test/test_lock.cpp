
#include "base/noncopyable.hpp"
#include "base/type.hpp"
#include <generic/rte_spinlock.h>
#include <mutex>
class TestMutex : public fg::base::noncopyable{
    void lock() {
        rte_spinlock_lock(&_mtx);
    }
    void unlock() {
        rte_spinlock_unlock(&_mtx);
    }
private:
    fg::spinlock_t _mtx;
};

int main(int argc, const char** argv) {
    TestMutex _mtx;
    std::unique_lock<TestMutex> lock(_mtx);
    lock.unlock();
    lock.lock();
    lock.owns_lock();
    lock.try_lock();
    //lock.try_lock_for(const chrono::duration<Rep, Period> &rtime);
    //lock.try_lock_until(const chrono::time_point<Clock, Duration> &atime);
    //lock.mutex();
    //lock.release();
    //lock.swap(unique_lock<TestMutex> &u);
    return 0;
}