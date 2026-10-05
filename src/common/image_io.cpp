#include "image_io.h"

#include <brovfs/mime.h>
#include <broimage/decode.h>

#include <algorithm>
#include <fstream>

namespace brothumb::detail {

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

Result decode_image(const uint8_t* data, size_t size, Image& out_image) {
    broimage::Image img;
    std::string err;
    if (!broimage::decode_memory_oriented(data, size, img, &err)) {
        return Result::failure("Image decode failed: " + (err.empty() ? std::string("unknown format") : err));
    }
    if (img.channels != 4 || img.width <= 0 || img.height <= 0) {
        return Result::failure("Image decode produced no RGBA pixels");
    }
    out_image.width = img.width;
    out_image.height = img.height;
    out_image.rgba = std::move(img.pixels);
    return Result::success();
}

std::string resolve_type(const std::filesystem::path& path, const std::string& hint) {
    if (!hint.empty()) return hint;
    return bro::vfs::MimeDatabase::system().type_for_file(path).mime;
}

bool type_is_a(std::string_view mime, std::string_view ancestor) {
    return bro::vfs::MimeDatabase::system().is_a(mime, ancestor);
}

std::vector<std::string> extensions_for_types(const std::vector<std::string>& mime_types) {
    std::vector<std::string> out;
    for (const auto& m : mime_types) {
        for (const auto& e : bro::vfs::MimeDatabase::system().extensions_for_type(m)) {
            std::string dotted = "." + e;
            if (std::find(out.begin(), out.end(), dotted) == out.end()) out.push_back(std::move(dotted));
        }
    }
    return out;
}

}  // namespace brothumb::detail
