#include "brothumb/metadata.h"
#include "brothumb/uri.h"
#include "lodepng/lodepng.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace brothumb {

namespace {

int64_t get_file_mtime_epoch(const std::filesystem::path& path) {
#if defined(_WIN32)
    struct _stat64 st;
    if (_wstat64(path.wstring().c_str(), &st) == 0) {
        return static_cast<int64_t>(st.st_mtime);
    }
    return 0;
#else
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        return static_cast<int64_t>(st.st_mtime);
    }
    return 0;
#endif
}

int64_t get_file_size_bytes(const std::filesystem::path& path) {
#if defined(_WIN32)
    struct _stat64 st;
    if (_wstat64(path.wstring().c_str(), &st) == 0) {
        return static_cast<int64_t>(st.st_size);
    }
    return -1;
#else
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        return static_cast<int64_t>(st.st_size);
    }
    return -1;
#endif
}

}  // namespace

const char* to_string(CacheFreshness freshness) {
    switch (freshness) {
        case CacheFreshness::Valid:           return "valid";
        case CacheFreshness::Stale:           return "stale";
        case CacheFreshness::MismatchUri:     return "mismatch_uri";
        case CacheFreshness::MissingMetadata: return "missing_metadata";
        case CacheFreshness::FileNotFound:    return "file_not_found";
    }
    return "unknown";
}

Result read_png_metadata_from_memory(const uint8_t* data, size_t size, PngInfo& out_info) {
    if (!data || size < 8) {
        return Result::failure("Buffer too small for PNG header");
    }

    lodepng::State state;
    unsigned width = 0, height = 0;
    unsigned error = lodepng_inspect(&width, &height, &state, data, size);
    if (error) {
        return Result::failure(std::string("PNG inspect error: ") + lodepng_error_text(error));
    }

    // Inspect ancillary chunks (tEXt, zTXt, iTXt, etc.)
    const unsigned char* chunk = data + 8;
    const unsigned char* end = data + size;
    while (chunk + 12 <= end) {
        size_t pos = static_cast<size_t>(chunk - data);
        lodepng_inspect_chunk(&state, pos, data, size);
        unsigned chunk_len = lodepng_chunk_length(chunk);
        if (chunk + 12 + chunk_len > end) break;
        chunk = lodepng_chunk_next_const(chunk);
        if (!chunk) break;
    }

    out_info.width = static_cast<int32_t>(width);
    out_info.height = static_cast<int32_t>(height);
    out_info.bit_depth = static_cast<int32_t>(state.info_png.color.bitdepth);
    out_info.color_type = static_cast<int32_t>(state.info_png.color.colortype);

    // Extract text metadata
    const LodePNGInfo& info = state.info_png;
    for (size_t i = 0; i < info.text_num; ++i) {
        std::string key = info.text_keys[i] ? info.text_keys[i] : "";
        std::string val = info.text_strings[i] ? info.text_strings[i] : "";

        if (key == "Thumb::URI") {
            out_info.metadata.uri = val;
        } else if (key == "Thumb::MTime") {
            try {
                out_info.metadata.mtime = std::stoll(val);
            } catch (...) {
                out_info.metadata.mtime = 0;
            }
        } else if (key == "Thumb::Size") {
            try {
                out_info.metadata.file_size = std::stoll(val);
            } catch (...) {
                out_info.metadata.file_size = -1;
            }
        } else if (key == "Thumb::Mimetype") {
            out_info.metadata.mimetype = val;
        } else if (key == "Thumb::Image::Width") {
            try {
                out_info.metadata.image_width = std::stoi(val);
            } catch (...) {
                out_info.metadata.image_width = -1;
            }
        } else if (key == "Thumb::Image::Height") {
            try {
                out_info.metadata.image_height = std::stoi(val);
            } catch (...) {
                out_info.metadata.image_height = -1;
            }
        } else if (key == "Software") {
            out_info.metadata.software = val;
        } else if (!key.empty()) {
            out_info.metadata.custom_tags[key] = val;
        }
    }

    // Also check itexts if present
    for (size_t i = 0; i < info.itext_num; ++i) {
        std::string key = info.itext_keys[i] ? info.itext_keys[i] : "";
        std::string val = info.itext_strings[i] ? info.itext_strings[i] : "";
        if (key == "Thumb::URI" && out_info.metadata.uri.empty()) {
            out_info.metadata.uri = val;
        } else if (key == "Thumb::MTime" && out_info.metadata.mtime == 0) {
            try {
                out_info.metadata.mtime = std::stoll(val);
            } catch (...) {
                out_info.metadata.mtime = 0;
            }
        }
    }

    return Result::success();
}

Result read_png_metadata(const std::filesystem::path& png_path, PngInfo& out_info) {
    std::error_code ec;
    if (!std::filesystem::exists(png_path, ec)) {
        return Result::failure("PNG file does not exist: " + png_path.string());
    }

    std::ifstream file(png_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return Result::failure("Failed to open PNG file: " + png_path.string());
    }

    std::streamsize file_size = file.tellg();
    if (file_size <= 0) {
        return Result::failure("PNG file is empty: " + png_path.string());
    }

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        return Result::failure("Failed to read PNG file: " + png_path.string());
    }

    return read_png_metadata_from_memory(buffer.data(), buffer.size(), out_info);
}

Result encode_png_with_metadata(const Image& image, const ThumbnailMetadata& metadata,
                                std::vector<uint8_t>& out_bytes) {
    if (image.empty()) {
        return Result::failure("Cannot encode empty image");
    }

    lodepng::State state;
    // Set RGBA8 input format
    state.info_raw.colortype = LCT_RGBA;
    state.info_raw.bitdepth = 8;
    state.info_png.color.colortype = LCT_RGBA;
    state.info_png.color.bitdepth = 8;

    // Add standard Freedesktop tags
    if (!metadata.uri.empty()) {
        lodepng_add_text(&state.info_png, "Thumb::URI", metadata.uri.c_str());
    }
    if (metadata.mtime > 0) {
        std::string mtime_str = std::to_string(metadata.mtime);
        lodepng_add_text(&state.info_png, "Thumb::MTime", mtime_str.c_str());
    }
    if (metadata.file_size >= 0) {
        std::string size_str = std::to_string(metadata.file_size);
        lodepng_add_text(&state.info_png, "Thumb::Size", size_str.c_str());
    }
    if (!metadata.mimetype.empty()) {
        lodepng_add_text(&state.info_png, "Thumb::Mimetype", metadata.mimetype.c_str());
    }
    if (metadata.image_width >= 0) {
        std::string w_str = std::to_string(metadata.image_width);
        lodepng_add_text(&state.info_png, "Thumb::Image::Width", w_str.c_str());
    }
    if (metadata.image_height >= 0) {
        std::string h_str = std::to_string(metadata.image_height);
        lodepng_add_text(&state.info_png, "Thumb::Image::Height", h_str.c_str());
    }
    std::string software = metadata.software.empty() ? "brothumb 0.1.0" : metadata.software;
    lodepng_add_text(&state.info_png, "Software", software.c_str());

    for (const auto& [k, v] : metadata.custom_tags) {
        lodepng_add_text(&state.info_png, k.c_str(), v.c_str());
    }

    unsigned error = lodepng::encode(out_bytes, image.rgba.data(),
                                     static_cast<unsigned>(image.width),
                                     static_cast<unsigned>(image.height),
                                     state);
    if (error) {
        return Result::failure(std::string("PNG encode error: ") + lodepng_error_text(error));
    }

    return Result::success();
}

Result write_png_with_metadata(const std::filesystem::path& dest_path, const Image& image,
                               const ThumbnailMetadata& metadata) {
    std::vector<uint8_t> bytes;
    Result encode_res = encode_png_with_metadata(image, metadata, bytes);
    if (!encode_res) return encode_res;

    // Write to a temporary file first in the same directory for atomic rename
    auto parent_dir = dest_path.parent_path();
    if (!parent_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent_dir, ec);
    }

    std::filesystem::path temp_path = dest_path;
    temp_path += ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return Result::failure("Cannot open temp file for writing: " + temp_path.string());
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!out.good()) {
            std::error_code ec;
            std::filesystem::remove(temp_path, ec);
            return Result::failure("Failed writing PNG data to: " + temp_path.string());
        }
    }

#if !defined(_WIN32)
    // Freedesktop spec requires 0600 permissions (read/write only by owner)
    ::chmod(temp_path.c_str(), S_IRUSR | S_IWUSR);
#endif

    std::error_code ec;
    std::filesystem::rename(temp_path, dest_path, ec);
    if (ec) {
        // Fallback: try copy + remove
        std::filesystem::copy_file(temp_path, dest_path,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(temp_path, ec);
        if (ec) {
            return Result::failure("Failed to rename temp file to dest: " + ec.message());
        }
    }

    return Result::success();
}

CacheFreshness verify_cache_freshness(const std::filesystem::path& thumbnail_path,
                                      const std::filesystem::path& source_path) {
    std::error_code ec;
    if (!std::filesystem::exists(thumbnail_path, ec) ||
        !std::filesystem::exists(source_path, ec)) {
        return CacheFreshness::FileNotFound;
    }

    PngInfo info;
    Result res = read_png_metadata(thumbnail_path, info);
    if (!res || !info.metadata.has_uri() || !info.metadata.has_mtime()) {
        return CacheFreshness::MissingMetadata;
    }

    std::string canonical_uri = path_to_uri(source_path);
    if (info.metadata.uri != canonical_uri) {
        return CacheFreshness::MismatchUri;
    }

    int64_t source_mtime = get_file_mtime_epoch(source_path);
    if (source_mtime != info.metadata.mtime) {
        return CacheFreshness::Stale;
    }

    if (info.metadata.file_size >= 0) {
        int64_t current_size = get_file_size_bytes(source_path);
        if (current_size >= 0 && current_size != info.metadata.file_size) {
            return CacheFreshness::Stale;
        }
    }

    return CacheFreshness::Valid;
}

}  // namespace brothumb
