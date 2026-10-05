// Lightweight built-in Netpbm PPM (P3 and P6) decoder.
#pragma once

#include "brothumb/common.h"
#include <cstddef>
#include <cstdint>

namespace brothumb::detail {

Result decode_ppm_from_memory(const uint8_t* data, size_t size, Image& out_image);

}  // namespace brothumb::detail
