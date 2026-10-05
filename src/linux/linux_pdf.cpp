#if !defined(_WIN32) && !defined(__APPLE__)

#include "brothumb/generator.h"
#include "image_resizer.h"
#include "lodepng/lodepng.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

namespace brothumb {

namespace {

std::string find_pdftoppm_binary() {
    static const char* candidates[] = {
        "/usr/bin/pdftoppm",
        "/usr/local/bin/pdftoppm",
        "/bin/pdftoppm"
    };
    for (const char* path : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            return path;
        }
    }
    return "";
}

}  // namespace

PdfThumbnailGenerator::PdfThumbnailGenerator() {}

bool PdfThumbnailGenerator::is_available() {
    return !find_pdftoppm_binary().empty();
}

std::string PdfThumbnailGenerator::backend_name() {
    return is_available() ? "pdftoppm" : "none";
}

bool PdfThumbnailGenerator::can_generate(const std::filesystem::path& path,
                                        const std::string& mime_hint) const {
    if (!is_available()) return false;
    if (mime_hint == "application/pdf") return true;
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".pdf";
}

Result PdfThumbnailGenerator::generate(const std::filesystem::path& path,
                                       int32_t target_size,
                                       Image& out_image) {
    std::string bin = find_pdftoppm_binary();
    if (bin.empty()) {
        return Result::failure("pdftoppm is not installed or available on this system");
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return Result::failure("PDF file does not exist: " + path.string());
    }

    // Prepare temp prefix
    auto temp_dir = std::filesystem::temp_directory_path();
    auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto temp_prefix = temp_dir / ("brothumb_pdf_" + nonce);
    auto temp_output = temp_dir / ("brothumb_pdf_" + nonce + ".png");

    // Command: pdftoppm -png -r 150 -f 1 -l 1 -singlefile <path> <temp_prefix>
    std::string cmd = bin + " -png -r 150 -f 1 -l 1 -singlefile \"" +
                      path.string() + "\" \"" + temp_prefix.string() + "\" 2>&1";

    int ret = std::system(cmd.c_str());
    if (ret != 0 || !std::filesystem::exists(temp_output, ec)) {
        std::filesystem::remove(temp_output, ec);
        return Result::failure("pdftoppm command failed with exit code: " + std::to_string(ret));
    }

    // Read generated PNG
    std::ifstream f(temp_output, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        std::filesystem::remove(temp_output, ec);
        return Result::failure("Failed to open pdftoppm output: " + temp_output.string());
    }

    auto sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> png_bytes(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(png_bytes.data()), sz);
    f.close();
    std::filesystem::remove(temp_output, ec);

    Image decoded;
    unsigned w = 0, h = 0;
    unsigned error = lodepng::decode(decoded.rgba, w, h, png_bytes.data(), png_bytes.size());
    if (error) {
        return Result::failure(std::string("Failed to decode pdftoppm PNG: ") + lodepng_error_text(error));
    }

    decoded.width = static_cast<int32_t>(w);
    decoded.height = static_cast<int32_t>(h);
    out_image = resize_to_fit(decoded, target_size);

    return Result::success();
}

}  // namespace brothumb

#endif  // !defined(_WIN32) && !defined(__APPLE__)
