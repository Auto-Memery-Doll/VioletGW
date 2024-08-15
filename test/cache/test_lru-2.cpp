#include "base/type.hpp"
#include "base/util.hpp"
#include "cache/lru-2.hpp"
#include "base/log.hpp"
#include "cache/protocol.hpp"
#include <ostream>
#include <sstream>
#include <unistd.h>
#include <vector>
#include <iostream>

using namespace fg;

void test() {
    std::vector<cache::CacheEle> eles;
    for (int i = 0; i < 5; i ++) {
        cache::CacheEle ele;
        ele.fd = i;
        ele.status = cache::CacheNodeStatus::none;
        eles.push_back(ele);
    }        

    //int op = 0 /** 0 record, 1 evit */
 //   const fg_duration_t dur{10};
    cache::LRU2<3, 30, 1> lru_test;
    
    for (int i = 0; i < 10; ++ i) {
        int read = util::generate_random(0, 4);
        std::vector<cache::CacheEle*> to_remove;
        
        LOG(consule, info) << read << "\n";
        lru_test.record_access(&eles[read], &to_remove);
        sleep(1);
        std::cout  << lru_test  ;
        std::cout     << [&]() {
                std::stringstream ss;
                ss << "remove fd : ";
                for (auto& i : to_remove) {
                    ss << i->fd << " ";
                    i->status = cache::CacheNodeStatus::none;
                }
                ss << std::endl;
                return ss.str();
            }();
    }
}

int main(int argc, const char** argv) {
 
    test();
    return 0;
}