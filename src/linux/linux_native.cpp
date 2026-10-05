#if !defined(_WIN32) && !defined(__APPLE__)

#include "brothumb/native.h"
#include "brothumb/cache.h"
#include "brothumb/generator.h"

namespace brothumb {

bool NativeThumbnailExtractor::is_supported() {
    return true;
}

std::string NativeThumbnailExtractor::provider_name() {
    return "Linux Freedesktop XDG Thumbnail Cache";
}

PlatformCapabilities NativeThumbnailExtractor::capabilities() {
    PlatformCapabilities caps;
    caps.has_xdg_cache = true;
    caps.has_windows_shell = false;
    caps.has_macos_quicklook = false;
    caps.has_pdf_rendering = PdfThumbnailGenerator::is_available();
    caps.pdf_backend = PdfThumbnailGenerator::backend_name();
    caps.supported_extensions = {
        ".png", ".jpg", ".jpeg", ".bmp", ".ppm",
        ".txt", ".md", ".cpp", ".h"
    };
    if (caps.has_pdf_rendering) {
        caps.supported_extensions.push_back(".pdf");
    }
    return caps;
}

Result NativeThumbnailExtractor::extract(const std::filesystem::path& path,
                                         int32_t target_size,
                                         Image& out_image) {
    ThumbnailCache default_cache;
    ThumbnailSize size = size_from_pixels(target_size);
    auto lookup_res = default_cache.lookup(path, size, true, true);
    if (lookup_res.found && !lookup_res.image.empty()) {
        out_image = std::move(lookup_res.image);
        return Result::success();
    }
    return Result::failure("No cached native XDG thumbnail found for: " + path.string());
}

}  // namespace brothumb

#endif  // !defined(_WIN32) && !defined(__APPLE__)
