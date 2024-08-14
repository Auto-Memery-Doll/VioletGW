#include <chrono>
#include <cstring>
#include <iostream>
#include <unistd.h>

void test() {
    auto begin = 
        std::chrono::high_resolution_clock::now();
    usleep(100);
    auto now = 
        std::chrono::high_resolution_clock::now();

    std::cout << (begin == now) << std::endl;

    ::memset(&now, 0, sizeof(now));
    ::memcpy(&now, &begin, sizeof(begin));

    std::cout << (begin == now) << std::endl;
    std::cout << "sizeof(now) = " << sizeof(now) << std::endl;

}

int main() {
    test();
}