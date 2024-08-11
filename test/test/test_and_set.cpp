

#include <atomic>
#include <iostream>
#include <thread>
#include <unistd.h>
class TestLock {

public:
    void lock() {
        while (_mtx.test_and_set())
            ;
    }

    void unlock() {
        _mtx.clear();
    }

private:
    std::atomic_flag _mtx;

};

void test1(TestLock & lock) {
    lock.lock();
    std::cout << "test1" << std::endl;
    sleep(3);
    lock.unlock();
}
void test2(TestLock & lock) {
    lock.lock();
    std::cout << "test2" << std::endl;
    sleep(3);
    lock.unlock();
}

int main(int argc, const char** argv) {
    
    TestLock lock;

    std::thread t([&](){
        test1(lock);
    });
    sleep(1);
    std::thread t1([&](){
        test2(lock);
    });

    t.join();
    t1.join();
 
    return 0;
}