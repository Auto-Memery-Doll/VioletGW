#include <boost/version.hpp>
#include <boost/functional/hash.hpp>
#include <boost/static_assert.hpp>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <boost/crc.hpp>
#include <string>

uint32_t boost_crc32(const std::string& data) {
    const uint32_t POLYNOMIAL = 0xED888320;
    uint32_t crc = ~0U;
    for (char c : data) {
        crc ^= static_cast<uint32_t>(c);
        for (int i = 0; i < 8; ++ i) {
            if (crc & 1) {
                crc = (crc >> 1) ^ POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

uint32_t get_crc32(const std::string& my_string) {
    boost::crc_32_type res;
    res.process_bytes(my_string.data(), my_string.length());
    return res.checksum();
}

int main() {
    std::string data = "Hello, world!";
    std::size_t fnv_hash = boost::hash_value(data);
    std::cout << "FNV hash of '" << data << "' is: " 
        << fnv_hash << std::endl;

    uint32_t crc32 = boost_crc32(data);
    std::cout << "CRC32 of '" << data << "' is: " << crc32 << std::endl;
}