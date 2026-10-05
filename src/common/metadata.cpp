#include "brothumb/metadata.h"
#include "brothumb/uri.h"
#include "image_io.h"

#include <broimage/png_text.h>

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

    broimage::PngInfo png;
    std::string err;
    if (!broimage::read_png_info(data, size, png, &err)) {
        return Result::failure("PNG inspect error: " + err);
    }

    out_info.width = static_cast<int32_t>(png.width);
    out_info.height = static_cast<int32_t>(png.height);
    out_info.bit_depth = static_cast<int32_t>(png.bit_depth);
    out_info.color_type = static_cast<int32_t>(png.color_type);

    // tEXt, zTXt and iTXt alike, in file order; the first occurrence of a standard tag wins.
    bool seen_uri = false, seen_mtime = false;
    for (const auto& entry : png.text) {
        const std::string& key = entry.keyword;
        const std::string& val = entry.text;

        if (key == "Thumb::URI") {
            if (seen_uri) continue;
            seen_uri = true;
            out_info.metadata.uri = val;
        } else if (key == "Thumb::MTime") {
            if (seen_mtime) continue;
            seen_mtime = true;
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

    return Result::success();
}

Result read_png_metadata(const std::filesystem::path& png_path, PngInfo& out_info) {
    std::error_code ec;
    if (!std::filesystem::exists(png_path, ec)) {
        return Result::failure("PNG file does not exist: " + png_path.string());
    }

    std::vector<uint8_t> buffer;
    Result read = detail::read_file_bytes(png_path, buffer);
    if (!read) {
        return Result::failure("Failed to read PNG file: " + read.error);
    }

    return read_png_metadata_from_memory(buffer.data(), buffer.size(), out_info);
}

Result encode_png_with_metadata(const Image& image, const ThumbnailMetadata& metadata,
                                std::vector<uint8_t>& out_bytes) {
    if (image.empty()) {
        return Result::failure("Cannot encode empty image");
    }
    if (image.rgba.size() != static_cast<size_t>(image.width) * image.height * 4) {
        return Result::failure("Image buffer does not match its size");
    }

    // Standard Freedesktop tags first, then the caller's own.
    std::vector<broimage::PngTextEntry> tags;
    if (!metadata.uri.empty()) tags.push_back({"Thumb::URI", metadata.uri});
    if (metadata.mtime > 0) tags.push_back({"Thumb::MTime", std::to_string(metadata.mtime)});
    if (metadata.file_size >= 0) tags.push_back({"Thumb::Size", std::to_string(metadata.file_size)});
    if (!metadata.mimetype.empty()) tags.push_back({"Thumb::Mimetype", metadata.mimetype});
    if (metadata.image_width >= 0) tags.push_back({"Thumb::Image::Width", std::to_string(metadata.image_width)});
    if (metadata.image_height >= 0) tags.push_back({"Thumb::Image::Height", std::to_string(metadata.image_height)});
    tags.push_back({"Software", metadata.software.empty() ? std::string("brothumb 0.1.0") : metadata.software});
    for (const auto& [k, v] : metadata.custom_tags) {
        tags.push_back({k, v});
    }

    if (!broimage::encode_png_memory_with_text(out_bytes, image.rgba.data(), image.width, image.height, 4, tags)) {
        return Result::failure("PNG encode error (a tag keyword must be 1-79 printable Latin-1 bytes)");
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
