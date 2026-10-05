#include "bmp_decoder.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace brothumb::detail {

namespace {

inline uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline int32_t read_i32_le(const uint8_t* p) {
    return static_cast<int32_t>(read_u32_le(p));
}

}  // namespace

Result decode_bmp_from_memory(const uint8_t* data, size_t size, Image& out_image) {
    if (!data || size < 54) {
        return Result::failure("Buffer too small for BMP file");
    }

    // BMP magic "BM"
    if (data[0] != 'B' || data[1] != 'M') {
        return Result::failure("Invalid BMP magic bytes");
    }

    uint32_t pixel_offset = read_u32_le(data + 10);
    uint32_t dib_header_size = read_u32_le(data + 14);

    if (dib_header_size < 12 || 14 + dib_header_size > size) {
        return Result::failure("Invalid BMP DIB header size");
    }

    int32_t width = 0;
    int32_t height = 0;
    uint16_t planes = 1;
    uint16_t bpp = 0;
    uint32_t compression = 0;

    if (dib_header_size == 12) {
        // BITMAPCOREHEADER
        width = read_u16_le(data + 18);
        height = read_u16_le(data + 20);
        planes = read_u16_le(data + 22);
        bpp = read_u16_le(data + 24);
    } else {
        // BITMAPINFOHEADER and newer
        width = read_i32_le(data + 18);
        height = read_i32_le(data + 22);
        planes = read_u16_le(data + 26);
        bpp = read_u16_le(data + 28);
        compression = read_u32_le(data + 30);
    }

    if (planes != 1) {
        return Result::failure("Unsupported BMP planes count: " + std::to_string(planes));
    }

    if (width <= 0 || width > 16384 || height == 0 || std::abs(height) > 16384) {
        return Result::failure("Invalid BMP dimensions: " + std::to_string(width) + "x" + std::to_string(height));
    }

    bool top_down = (height < 0);
    int32_t abs_height = std::abs(height);

    if (compression != 0 && compression != 3) {
        return Result::failure("Compressed BMPs not supported (compression=" + std::to_string(compression) + ")");
    }

    if (bpp != 8 && bpp != 24 && bpp != 32) {
        return Result::failure("Unsupported BMP bit depth: " + std::to_string(bpp));
    }

    if (pixel_offset >= size) {
        return Result::failure("BMP pixel offset exceeds file size");
    }

    // Handle 8-bit palette
    std::vector<uint32_t> palette;
    if (bpp == 8) {
        size_t pal_offset = 14 + dib_header_size;
        size_t pal_entry_size = (dib_header_size == 12) ? 3 : 4;
        size_t pal_entries = 256;
        if (pal_offset + pal_entries * pal_entry_size > pixel_offset &&
            pal_offset + pal_entries * pal_entry_size > size) {
            return Result::failure("BMP palette truncated");
        }
        palette.resize(256);
        for (size_t i = 0; i < 256; ++i) {
            const uint8_t* entry = data + pal_offset + i * pal_entry_size;
            uint8_t b = entry[0];
            uint8_t g = entry[1];
            uint8_t r = entry[2];
            palette[i] = (static_cast<uint32_t>(r) << 0) |
                         (static_cast<uint32_t>(g) << 8) |
                         (static_cast<uint32_t>(b) << 16) |
                         (0xFFu << 24);
        }
    }

    out_image.width = width;
    out_image.height = abs_height;
    out_image.rgba.resize(static_cast<size_t>(width) * abs_height * 4);

    size_t row_stride = ((static_cast<size_t>(width) * bpp + 31) / 32) * 4;

    for (int32_t y = 0; y < abs_height; ++y) {
        int32_t src_row_idx = top_down ? y : (abs_height - 1 - y);
        size_t src_offset = pixel_offset + static_cast<size_t>(src_row_idx) * row_stride;
        if (src_offset + row_stride > size && src_offset + (static_cast<size_t>(width) * bpp / 8) > size) {
            return Result::failure("Unexpected end of BMP pixel data");
        }

        const uint8_t* src_row = data + src_offset;
        uint8_t* dst_row = &out_image.rgba[static_cast<size_t>(y) * width * 4];

        if (bpp == 24) {
            for (int32_t x = 0; x < width; ++x) {
                uint8_t b = src_row[x * 3 + 0];
                uint8_t g = src_row[x * 3 + 1];
                uint8_t r = src_row[x * 3 + 2];
                dst_row[x * 4 + 0] = r;
                dst_row[x * 4 + 1] = g;
                dst_row[x * 4 + 2] = b;
                dst_row[x * 4 + 3] = 255;
            }
        } else if (bpp == 32) {
            for (int32_t x = 0; x < width; ++x) {
                uint8_t b = src_row[x * 4 + 0];
                uint8_t g = src_row[x * 4 + 1];
                uint8_t r = src_row[x * 4 + 2];
                uint8_t a = src_row[x * 4 + 3];
                // If entire image has alpha == 0 (common in 32-bit BGRX Windows bitmaps), treat as 255
                dst_row[x * 4 + 0] = r;
                dst_row[x * 4 + 1] = g;
                dst_row[x * 4 + 2] = b;
                dst_row[x * 4 + 3] = a;
            }
        } else if (bpp == 8) {
            for (int32_t x = 0; x < width; ++x) {
                uint8_t idx = src_row[x];
                uint32_t rgba = palette[idx];
                dst_row[x * 4 + 0] = static_cast<uint8_t>((rgba >> 0) & 0xff);
                dst_row[x * 4 + 1] = static_cast<uint8_t>((rgba >> 8) & 0xff);
                dst_row[x * 4 + 2] = static_cast<uint8_t>((rgba >> 16) & 0xff);
                dst_row[x * 4 + 3] = 255;
            }
        }
    }

    // Check if 32-bit image has all zero alpha channel (BGRX)
    if (bpp == 32) {
        bool has_nonzero_alpha = false;
        for (size_t i = 3; i < out_image.rgba.size(); i += 4) {
            if (out_image.rgba[i] != 0) {
                has_nonzero_alpha = true;
                break;
            }
        }
        if (!has_nonzero_alpha) {
            for (size_t i = 3; i < out_image.rgba.size(); i += 4) {
                out_image.rgba[i] = 255;
            }
        }
    }

    return Result::success();
}

}  // namespace brothumb::detail
