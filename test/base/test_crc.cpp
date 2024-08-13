
#include "base/log.hpp"
#include "base/util.hpp"
#include <boost/crc.hpp>
#include <string>
#include <vector>
void test() {

    std::vector<std::string> datas;
    for (int i = 0; i < 100'000'000; ++ i) {
        std::string str;
        for (int j = 0; j < 20; ++ j) {
            str += (char)fg::util::generate_random(0, 128);
        }
        datas.push_back(str);
    }

    LOG(consule, info) << "test base::crc\n";
    fg::util::TestTimer timer1;
    for (auto& data : datas) {
        fg::util::crc32(data);
    }
    timer1.stop();
    timer1.time_inter();

    LOG(consule, info) << "test boost::crc\n";
    fg::util::TestTimer timer2;
    for (auto& data : datas) {
        boost::crc_32_type crc32;
        crc32.process_bytes(data.data(), data.length());
    }
    timer2.stop();
    timer2.time_inter();

}

int main() {
    test();
}