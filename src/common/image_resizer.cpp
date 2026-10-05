#include "image_resizer.h"

#include <broimage/alpha.h>
#include <broimage/geometric.h>

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

    return resize_exact(src, target_w, target_h);
}

Image resize_exact(const Image& src, int32_t target_w, int32_t target_h) {
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
    bool shrinking = target_w <= src.width && target_h <= src.height;
    broimage::resize_rgba8_alpha(src.rgba.data(), src.width, src.height, dst.rgba.data(), target_w, target_h,
                                 shrinking ? broimage::Filter::Area : broimage::Filter::Bilinear);
    return dst;
}

}  // namespace brothumb
