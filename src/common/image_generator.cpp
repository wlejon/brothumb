#include "brothumb/generator.h"
#include "image_io.h"
#include "image_resizer.h"

#include <broimage/decode.h>
#include <broimage/heif.h>

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
    // What broimage decodes (libjpeg-turbo, stb_image, and its own TIFF and CR3 readers).
    std::vector<std::string> types = {
        "image/png", "image/jpeg", "image/gif", "image/bmp", "image/x-tga", "image/tiff",
        "image/vnd.adobe.photoshop", "image/vnd.radiance",
        "image/x-portable-pixmap", "image/x-portable-graymap", "image/x-portable-anymap",
        "image/x-canon-cr3",  // its embedded JPEG previews, never the RAW data
    };
    // HEIF, where this process can decode it: AVIF once the host registered an AV1
    // decoder, HEIC/HEIF through the system's decoder (WIC with the HEVC extension, ImageIO).
    for (const char* t : {"image/avif", "image/heic", "image/heif"}) {
        if (broimage::can_decode(t)) types.emplace_back(t);
    }
    return types;
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
    Image decoded;
    auto it = custom_decoders_.find(normalize_ext(path));
    Result res = Result::failure("");
    if (it != custom_decoders_.end()) {
        std::vector<uint8_t> bytes;
        Result read_res = detail::read_file_bytes(path, bytes);
        if (!read_res) return read_res;
        res = it->second(bytes.data(), bytes.size(), decoded);
    } else {
        // Decoded for the thumbnail's size, not the image's (broimage's scaled decode): a
        // JPEG at the smallest DCT scale that covers it, or the file's own thumbnail when
        // that is big enough (a JPEG's EXIF one, a HEIF's thumbnail item, a CR3's
        // previews; then only the head of the file is read). Upright either way.
        broimage::ScaledDecodeOptions o;
        o.width = o.height = target_size;
        broimage::Image img;
        std::string err;
        const std::u8string u8 = path.u8string();  // broimage takes UTF-8 paths
        if (broimage::decode_file_scaled(std::string(u8.begin(), u8.end()), o, img, &err) && img.channels == 4 &&
            !img.pixels.empty()) {
            decoded.width = img.width;
            decoded.height = img.height;
            decoded.rgba = std::move(img.pixels);
            res = Result::success();
        } else {
            res = Result::failure("Image decode failed: " + (err.empty() ? std::string("unknown format") : err));
        }
    }
    if (!res) return res;

    if (decoded.empty()) {
        return Result::failure("Decoded image is empty");
    }

    out_image = resize_to_fit(decoded, target_size);
    return Result::success();
}

}  // namespace brothumb
