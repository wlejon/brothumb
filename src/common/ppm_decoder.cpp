#include "ppm_decoder.h"
#include <cctype>
#include <sstream>
#include <string>

namespace brothumb::detail {

namespace {

// Skips whitespace and '#' comment lines
void skip_ppm_comments_and_ws(const uint8_t* data, size_t size, size_t& offset) {
    while (offset < size) {
        char c = static_cast<char>(data[offset]);
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++offset;
        } else if (c == '#') {
            while (offset < size && data[offset] != '\n' && data[offset] != '\r') {
                ++offset;
            }
        } else {
            break;
        }
    }
}

// Reads next integer token
bool read_next_int(const uint8_t* data, size_t size, size_t& offset, int& val) {
    skip_ppm_comments_and_ws(data, size, offset);
    if (offset >= size) return false;

    val = 0;
    bool found = false;
    while (offset < size && std::isdigit(static_cast<unsigned char>(data[offset]))) {
        val = val * 10 + (data[offset] - '0');
        ++offset;
        found = true;
    }
    return found;
}

}  // namespace

Result decode_ppm_from_memory(const uint8_t* data, size_t size, Image& out_image) {
    if (!data || size < 8) {
        return Result::failure("Buffer too small for PPM header");
    }

    if (data[0] != 'P' || (data[1] != '3' && data[1] != '6')) {
        return Result::failure("Invalid PPM magic (expected P3 or P6)");
    }

    bool is_p6 = (data[1] == '6');
    size_t offset = 2;

    int width = 0;
    int height = 0;
    int maxval = 0;

    if (!read_next_int(data, size, offset, width) || width <= 0 || width > 16384) {
        return Result::failure("Invalid PPM width");
    }
    if (!read_next_int(data, size, offset, height) || height <= 0 || height > 16384) {
        return Result::failure("Invalid PPM height");
    }
    if (!read_next_int(data, size, offset, maxval) || maxval <= 0 || maxval > 255) {
        return Result::failure("Unsupported PPM maxval (only 1-255 supported): " + std::to_string(maxval));
    }

    // After maxval there is a single whitespace character before binary data in P6
    if (offset < size && std::isspace(static_cast<unsigned char>(data[offset]))) {
        ++offset;
    }

    out_image.width = width;
    out_image.height = height;
    out_image.rgba.resize(static_cast<size_t>(width) * height * 4);

    size_t total_pixels = static_cast<size_t>(width) * height;

    if (is_p6) {
        size_t needed = total_pixels * 3;
        if (offset + needed > size) {
            return Result::failure("Truncated P6 pixel data");
        }
        const uint8_t* src = data + offset;
        uint8_t* dst = out_image.rgba.data();
        for (size_t i = 0; i < total_pixels; ++i) {
            dst[i * 4 + 0] = src[i * 3 + 0];
            dst[i * 4 + 1] = src[i * 3 + 1];
            dst[i * 4 + 2] = src[i * 3 + 2];
            dst[i * 4 + 3] = 255;
        }
    } else {
        // P3 ASCII
        uint8_t* dst = out_image.rgba.data();
        for (size_t i = 0; i < total_pixels; ++i) {
            int r = 0, g = 0, b = 0;
            if (!read_next_int(data, size, offset, r) ||
                !read_next_int(data, size, offset, g) ||
                !read_next_int(data, size, offset, b)) {
                return Result::failure("Truncated P3 ASCII pixel data at pixel " + std::to_string(i));
            }
            dst[i * 4 + 0] = static_cast<uint8_t>(r);
            dst[i * 4 + 1] = static_cast<uint8_t>(g);
            dst[i * 4 + 2] = static_cast<uint8_t>(b);
            dst[i * 4 + 3] = 255;
        }
    }

    return Result::success();
}

}  // namespace brothumb::detail
