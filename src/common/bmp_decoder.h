// Lightweight built-in Windows BMP decoder.
#pragma once

#include "brothumb/common.h"
#include <cstdint>
#include <cstddef>

namespace brothumb::detail {

Result decode_bmp_from_memory(const uint8_t* data, size_t size, Image& out_image);

}  // namespace brothumb::detail
