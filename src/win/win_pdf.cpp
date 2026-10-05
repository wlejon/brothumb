#if defined(_WIN32)

#include "brothumb/generator.h"
#include "brothumb/native.h"
#include "image_io.h"
#include "image_resizer.h"

namespace brothumb {

PdfThumbnailGenerator::PdfThumbnailGenerator() {}

bool PdfThumbnailGenerator::is_available() {
    return true;
}

std::string PdfThumbnailGenerator::backend_name() {
    return "Windows Shell / WinRT";
}

bool PdfThumbnailGenerator::can_generate(const std::filesystem::path& path,
                                        const std::string& mime_hint) const {
    return detail::type_is_a(detail::resolve_type(path, mime_hint), "application/pdf");
}

Result PdfThumbnailGenerator::generate(const std::filesystem::path& path,
                                       int32_t target_size,
                                       Image& out_image) {
    Image native_img;
    Result res = NativeThumbnailExtractor::extract(path, target_size, native_img);
    if (!res) {
        return Result::failure("Windows PDF extraction failed: " + res.error);
    }
    out_image = resize_to_fit(native_img, target_size);
    return Result::success();
}

}  // namespace brothumb

#endif  // _WIN32
