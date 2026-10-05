// Self-contained SHA-256 implementation (FIPS 180-2).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace brothumb::detail {

class SHA256 {
public:
    SHA256();
    void update(const uint8_t* data, size_t length);
    void update(std::string_view sv) {
        update(reinterpret_cast<const uint8_t*>(sv.data()), sv.size());
    }
    void finalize(uint8_t digest[32]);
    std::string hex_digest();

    static std::string hash_string(std::string_view input);

private:
    void transform(const uint8_t block[64]);

    uint32_t state_[8];
    uint64_t bit_count_ = 0;
    uint8_t buffer_[64];
};

}  // namespace brothumb::detail
