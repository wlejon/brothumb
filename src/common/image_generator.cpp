#include "brothumb/generator.h"
#include "image_io.h"
#include "image_resizer.h"

#include <algorithm>
#include <cctype>
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

}  // namespace

ImageThumbnailGenerator::ImageThumbnailGenerator() = default;

std::vector<std::string> ImageThumbnailGenerator::supported_mime_types() const {
    // What broimage decodes (stb_image, and its own TIFF decoder).
    return {
        "image/png", "image/jpeg", "image/gif", "image/bmp", "image/x-tga", "image/tiff",
        "image/vnd.adobe.photoshop", "image/vnd.radiance",
        "image/x-portable-pixmap", "image/x-portable-graymap", "image/x-portable-anymap",
    };
}

bool ImageThumbnailGenerator::can_generate(const std::filesystem::path& path,
                                           const std::string& mime_hint) const {
    if (custom_decoders_.find(normalize_ext(path)) != custom_decoders_.end()) {
        return true;
    }
    std::string mime = detail::resolve_type(path, mime_hint);
    for (const auto& t : supported_mime_types()) {
        if (detail::type_is_a(mime, t)) return true;
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
    Result read_res = detail::read_file_bytes(path, bytes);
    if (!read_res) return read_res;

    Image decoded;
    auto it = custom_decoders_.find(normalize_ext(path));
    Result res = it != custom_decoders_.end() ? it->second(bytes.data(), bytes.size(), decoded)
                                              : detail::decode_image(bytes.data(), bytes.size(), decoded);
    if (!res) return res;

    if (decoded.empty()) {
        return Result::failure("Decoded image is empty");
    }

    out_image = resize_to_fit(decoded, target_size);
    return Result::success();
}

}  // namespace brothumb
