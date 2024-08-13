#include "base/log.hpp"
#include "base/util.hpp"
#include <thread>
#include <vector>

void test() {
    LOG_FILE_INIT("test1.log");

    std::string log_str = "test log: 1234567";
    auto func = [&]() {
        for (int i = 0; i < 1000; i ++) {
            LOG(file, info) << log_str << i;
        }
    };

    std::vector<std::thread> pools;
    for (int i = 0; i < 1; ++ i) {
        pools.emplace_back([&](){
            func();
        });
    }

    for (auto & t : pools) {
        t.join();
    }
}

int main(int argc, const char** argv) {
    fg::util::TestTimer timer;
    test();
    timer.stop();
    timer.time_inter();
    return 0;
}