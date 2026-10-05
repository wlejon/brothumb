// Self-contained MD5 implementation (RFC 1321).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace brothumb::detail {

class MD5 {
public:
    MD5();
    void update(const uint8_t* data, size_t length);
    void update(std::string_view sv) {
        update(reinterpret_cast<const uint8_t*>(sv.data()), sv.size());
    }
    void finalize(uint8_t digest[16]);
    std::string hex_digest();

    static std::string hash_string(std::string_view input);

private:
    void transform(const uint8_t block[64]);

    uint32_t state_[4];
    uint64_t count_ = 0;
    uint8_t buffer_[64];
};

}  // namespace brothumb::detail
