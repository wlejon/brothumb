// Freedesktop PNG metadata tags reading, writing, and freshness verification.
#pragma once

#include "brothumb/common.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace brothumb {

// Structured representation of standard Freedesktop thumbnail metadata.
struct ThumbnailMetadata {
    std::string uri;                      // Thumb::URI
    int64_t mtime = 0;                    // Thumb::MTime (Unix epoch seconds)
    int64_t file_size = -1;               // Thumb::Size (-1 if not set)
    std::string mimetype;                 // Thumb::Mimetype
    int32_t image_width = -1;             // Thumb::Image::Width (-1 if not set)
    int32_t image_height = -1;            // Thumb::Image::Height (-1 if not set)
    std::string software;                 // Software
    std::unordered_map<std::string, std::string> custom_tags;

    bool has_uri() const { return !uri.empty(); }
    bool has_mtime() const { return mtime > 0; }
};

// Result of reading PNG metadata from a file or buffer.
struct PngInfo {
    int32_t width = 0;
    int32_t height = 0;
    int32_t bit_depth = 0;
    int32_t color_type = 0;
    ThumbnailMetadata metadata;
};

// Reads PNG header and text metadata chunks (tEXt, zTXt, iTXt) from a PNG file
// without decompressing pixel data.
Result read_png_metadata(const std::filesystem::path& png_path, PngInfo& out_info);

// Reads PNG header and text metadata from an in-memory PNG buffer.
Result read_png_metadata_from_memory(const uint8_t* data, size_t size, PngInfo& out_info);

// Encodes an RGBA8 image to PNG bytes with embedded Freedesktop thumbnail metadata.
Result encode_png_with_metadata(const Image& image, const ThumbnailMetadata& metadata,
                                std::vector<uint8_t>& out_bytes);

// Writes an RGBA8 image to a PNG file with embedded Freedesktop thumbnail metadata.
Result write_png_with_metadata(const std::filesystem::path& dest_path, const Image& image,
                               const ThumbnailMetadata& metadata);

// Verifies whether the cached thumbnail's metadata matches the source file's
// current modification time and canonical URI.
enum class CacheFreshness {
    Valid,           // Thumbnail is up-to-date and matches source file
    Stale,           // Source file mtime differs from cached Thumb::MTime
    MismatchUri,     // Thumb::URI does not match source file URI
    MissingMetadata, // Thumb::URI or Thumb::MTime tag is missing
    FileNotFound,    // Source file or thumbnail does not exist
};

const char* to_string(CacheFreshness freshness);

CacheFreshness verify_cache_freshness(const std::filesystem::path& thumbnail_path,
                                      const std::filesystem::path& source_path);

}  // namespace brothumb
