#if !defined(_WIN32) && !defined(__APPLE__)

#include "brothumb/generator.h"
#include "image_io.h"
#include "image_resizer.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

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
    return detail::type_is_a(detail::resolve_type(path, mime_hint), "application/pdf");
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

    // Read and decode the generated PNG
    std::vector<uint8_t> png_bytes;
    Result read_res = detail::read_file_bytes(temp_output, png_bytes);
    std::filesystem::remove(temp_output, ec);
    if (!read_res) {
        return Result::failure("Failed to read pdftoppm output: " + read_res.error);
    }

    Image decoded;
    Result dec = detail::decode_image(png_bytes.data(), png_bytes.size(), decoded);
    if (!dec) {
        return Result::failure("Failed to decode pdftoppm PNG: " + dec.error);
    }
    out_image = resize_to_fit(decoded, target_size);

    return Result::success();
}

}  // namespace brothumb

#endif  // !defined(_WIN32) && !defined(__APPLE__)
