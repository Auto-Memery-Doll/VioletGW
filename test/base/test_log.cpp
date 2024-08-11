#include "base/util.hpp"
#include "base/log.hpp"

void test() {
    LOG(consule, info) << "this is test\n";
    LOG(consule, error) << "this is error\n";
    LOG(consule, warnning) << "this is warnning\n";
    LOG(consule, debug) << "this is debug\n";

}

int main(int argc, char **argv) {
    test();
    return 0;
}

