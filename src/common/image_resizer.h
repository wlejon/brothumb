// Aspect-preserving thumbnail scaling over broimage's alpha-aware resize.
#pragma once

#include "brothumb/common.h"

namespace brothumb {

// Resizes source image to fit within max_width x max_height while preserving
// aspect ratio. If only target_square is given, max_width = max_height = target_square.
// If the image already fits within the bounds and allow_upscale is false,
// a copy of the source image is returned.
Image resize_to_fit(const Image& src, int32_t max_width, int32_t max_height,
                    bool allow_upscale = false);

inline Image resize_to_fit(const Image& src, int32_t target_square, bool allow_upscale = false) {
    return resize_to_fit(src, target_square, target_square, allow_upscale);
}

// Resizes to exact dimensions: area-averaged when shrinking (no aliasing at large reduction
// ratios), bilinear when growing, filtered in premultiplied alpha so transparent pixels do not
// bleed their colour into the edges.
Image resize_exact(const Image& src, int32_t target_w, int32_t target_h);

}  // namespace brothumb
