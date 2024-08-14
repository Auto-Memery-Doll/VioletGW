#include "base/trie.hpp"
#include "base/util.hpp"
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int hash(char a) {
    return a;
}

void test() {

    fg::base::Trie<std::string, 128, hash> string_tree;
    std::atomic<int> g_cnt = 0;
    auto test_fun = [&]() -> void {
        std::cout << "thread start " << std::this_thread::get_id() << std::endl;
        int cnt = 0;

        std::string test_str = "";
        for (int j = 0; j < 10000; ++j) {
            for (int i = 0; i < 100; ++i) {
                test_str += (char)fg::util::generate_random(0, 127);

                if (string_tree.set(test_str, &test_str)) {
                    auto ret = string_tree.get(test_str);
                    //if (*ret != test_str) {
                    //    std::cout << *ret << " \n:\n " << test_str << "error" << std::endl;
                    //    return;
                    //}
                    cnt++;
                }
            }
        }
        g_cnt.fetch_add(cnt);
    };

    fg::util::TestTimer timer;

    std::vector<std::thread> pool;
    for (int i = 0; i < 10; ++i) {
        pool.emplace_back(test_fun);
    }

    for (auto& j : pool) {
        j.join();
    }

    timer.stop();
    std::cout << std::endl;
    timer.time_inter();
    std::cout << g_cnt << std::endl;
}

int main(int argc, const char** argv) {
    test();
    return 0;
}