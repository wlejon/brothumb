#include "image_resizer.h"
#include <algorithm>
#include <cmath>

namespace brothumb {

Image resize_to_fit(const Image& src, int32_t max_width, int32_t max_height,
                    bool allow_upscale) {
    if (src.empty() || max_width <= 0 || max_height <= 0) {
        return {};
    }

    if (!allow_upscale && src.width <= max_width && src.height <= max_height) {
        return src;
    }

    float scale_w = static_cast<float>(max_width) / static_cast<float>(src.width);
    float scale_h = static_cast<float>(max_height) / static_cast<float>(src.height);
    float scale = std::min(scale_w, scale_h);

    if (!allow_upscale && scale >= 1.0f) {
        return src;
    }

    int32_t target_w = std::max(1, static_cast<int32_t>(std::round(src.width * scale)));
    int32_t target_h = std::max(1, static_cast<int32_t>(std::round(src.height * scale)));

    return resize_bilinear(src, target_w, target_h);
}

Image resize_bilinear(const Image& src, int32_t target_w, int32_t target_h) {
    if (src.empty() || target_w <= 0 || target_h <= 0) {
        return {};
    }

    if (src.width == target_w && src.height == target_h) {
        return src;
    }

    Image dst;
    dst.width = target_w;
    dst.height = target_h;
    dst.rgba.resize(static_cast<size_t>(target_w) * target_h * 4);

    float x_ratio = static_cast<float>(src.width) / static_cast<float>(target_w);
    float y_ratio = static_cast<float>(src.height) / static_cast<float>(target_h);

    for (int32_t y = 0; y < target_h; ++y) {
        float src_y = (y + 0.5f) * y_ratio - 0.5f;
        int32_t y0 = std::max(0, static_cast<int32_t>(std::floor(src_y)));
        int32_t y1 = std::min(src.height - 1, y0 + 1);
        float fy = src_y - y0;
        if (fy < 0.0f) fy = 0.0f;
        float fy_inv = 1.0f - fy;

        const uint8_t* row0 = &src.rgba[y0 * src.width * 4];
        const uint8_t* row1 = &src.rgba[y1 * src.width * 4];
        uint8_t* dst_row = &dst.rgba[y * target_w * 4];

        for (int32_t x = 0; x < target_w; ++x) {
            float src_x = (x + 0.5f) * x_ratio - 0.5f;
            int32_t x0 = std::max(0, static_cast<int32_t>(std::floor(src_x)));
            int32_t x1 = std::min(src.width - 1, x0 + 1);
            float fx = src_x - x0;
            if (fx < 0.0f) fx = 0.0f;
            float fx_inv = 1.0f - fx;

            float w00 = fx_inv * fy_inv;
            float w10 = fx * fy_inv;
            float w01 = fx_inv * fy;
            float w11 = fx * fy;

            const uint8_t* p00 = &row0[x0 * 4];
            const uint8_t* p10 = &row0[x1 * 4];
            const uint8_t* p01 = &row1[x0 * 4];
            const uint8_t* p11 = &row1[x1 * 4];

            uint8_t* out = &dst_row[x * 4];
            for (int c = 0; c < 4; ++c) {
                float val = p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11;
                out[c] = static_cast<uint8_t>(std::clamp(std::round(val), 0.0f, 255.0f));
            }
        }
    }

    return dst;
}

}  // namespace brothumb
