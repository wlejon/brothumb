#include "brothumb/generator.h"
#include "bmp_decoder.h"
#include "image_resizer.h"
#include "ppm_decoder.h"
#include "lodepng/lodepng.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>

namespace brothumb {

namespace {

std::string normalize_ext(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

Result read_file_bytes(const std::filesystem::path& path, std::vector<uint8_t>& out_bytes) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return Result::failure("File does not exist: " + path.string());
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return Result::failure("Cannot open file: " + path.string());
    }

    std::streamsize size = file.tellg();
    if (size <= 0) {
        return Result::failure("File is empty: " + path.string());
    }

    file.seekg(0, std::ios::beg);
    out_bytes.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(out_bytes.data()), size)) {
        return Result::failure("Failed to read file: " + path.string());
    }

    return Result::success();
}

}  // namespace

ImageThumbnailGenerator::ImageThumbnailGenerator() {
    default_extensions_ = {".png", ".bmp", ".dib", ".ppm", ".pnm"};
}

bool ImageThumbnailGenerator::can_generate(const std::filesystem::path& path,
                                           const std::string& mime_hint) const {
    if (!mime_hint.empty()) {
        if (mime_hint == "image/png" ||
            mime_hint == "image/bmp" ||
            mime_hint == "image/x-ms-bmp" ||
            mime_hint == "image/x-portable-pixmap") {
            return true;
        }
    }

    std::string ext = normalize_ext(path);
    if (custom_decoders_.find(ext) != custom_decoders_.end()) {
        return true;
    }

    for (const auto& s : default_extensions_) {
        if (ext == s) return true;
    }
    return false;
}

void ImageThumbnailGenerator::register_decoder(std::string extension, ImageDecoderFunc decoder) {
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (!extension.empty() && extension[0] != '.') {
        extension.insert(extension.begin(), '.');
    }
    custom_decoders_[extension] = std::move(decoder);
}

Result ImageThumbnailGenerator::generate(const std::filesystem::path& path,
                                         int32_t target_size, Image& out_image) {
    std::vector<uint8_t> bytes;
    Result read_res = read_file_bytes(path, bytes);
    if (!read_res) return read_res;

    std::string ext = normalize_ext(path);
    Image decoded;

    // Check custom decoders first
    auto it = custom_decoders_.find(ext);
    if (it != custom_decoders_.end()) {
        Result res = it->second(bytes.data(), bytes.size(), decoded);
        if (!res) return res;
    } else if (ext == ".png") {
        unsigned w = 0, h = 0;
        unsigned error = lodepng::decode(decoded.rgba, w, h, bytes.data(), bytes.size());
        if (error) {
            return Result::failure(std::string("PNG decode error: ") + lodepng_error_text(error));
        }
        decoded.width = static_cast<int32_t>(w);
        decoded.height = static_cast<int32_t>(h);
    } else if (ext == ".bmp" || ext == ".dib") {
        Result res = detail::decode_bmp_from_memory(bytes.data(), bytes.size(), decoded);
        if (!res) return res;
    } else if (ext == ".ppm" || ext == ".pnm") {
        Result res = detail::decode_ppm_from_memory(bytes.data(), bytes.size(), decoded);
        if (!res) return res;
    } else {
        return Result::failure("Unsupported image format: " + ext);
    }

    if (decoded.empty()) {
        return Result::failure("Decoded image is empty");
    }

    out_image = resize_to_fit(decoded, target_size);
    return Result::success();
}

}  // namespace brothumb
