// File bytes, image decoding (broimage) and file types (brovfs) shared by the generators,
// the cache and the platform extractors.
#pragma once

#include "brothumb/common.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace brothumb::detail {

// Whole file into memory (Unicode paths on Windows).
Result read_file_bytes(const std::filesystem::path& path, std::vector<uint8_t>& out_bytes);

// PNG, JPEG, GIF, BMP, TGA, PSD, HDR, binary PNM, TIFF (broimage) to straight RGBA8, with the
// EXIF orientation (JPEG, TIFF, WebP, PNG eXIf) applied so phone photos come out upright.
Result decode_image(const uint8_t* data, size_t size, Image& out_image);

// The type a file is: `hint` when the caller already knows it, otherwise brovfs's answer from
// the name and the first bytes (MimeDatabase::system()).
std::string resolve_type(const std::filesystem::path& path, const std::string& hint);

// `mime` is `ancestor` or a subtype of it (text/x-c++src is text/plain, image/x-ms-bmp is
// image/bmp), in the platform database's terms.
bool type_is_a(std::string_view mime, std::string_view ancestor);

// Extensions with their dot for each type, preferred first, without repeats.
std::vector<std::string> extensions_for_types(const std::vector<std::string>& mime_types);

}  // namespace brothumb::detail
