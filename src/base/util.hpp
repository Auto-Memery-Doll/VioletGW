#pragma once

#include "base/singleton.hpp"
#include "base/type.hpp"
#include <chrono>
#include <cstdint>
#include <random>
#include <rte_ether.h>
#include <string>

namespace vgm {
namespace util {

#define VGM_MAC_DUMP_LEN 30
void mac_dump(char* str, const rte_ether_addr& addr);

inline std::string TX_RING_NAME(int port) {
    return std::string("vgm_tx_ring_") + std::to_string(port);
}

inline std::string RX_RING_NAME(int port) {
    return std::string("vgm_rx_ring_") + std::to_string(port);
}

class RandomGenerator : public base::Singletion<RandomGenerator> {
    friend class base::Singletion<RandomGenerator>;

public:
    int random_int(int begin, int end) {
        std::uniform_int_distribution<> dis(begin, end);
        return dis(gen);
    }

private:
    RandomGenerator() : gen(rd()) {}

    std::random_device rd;
    std::mt19937 gen;
};

inline int generate_random(int begin, int end) {
    return RandomGenerator::GetInstance()->random_int(begin, end);
}

uint32_t crc32(const std::string& data);
uint32_t fnv(const std::string& data);

inline vgm_clock_t now() {
    return std::chrono::high_resolution_clock::now();
}

std::string clock_to_str(const vgm_clock_t& tp);

}  // namespace util
}  // namespace vgm

