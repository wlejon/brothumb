#if defined(_WIN32)

#include "brothumb/native.h"
#include <algorithm>
#include <vector>
#include <windows.h>
#include <shobjidl.h>
#include <thumbcache.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace brothumb {

namespace {

struct ScopedCom {
    HRESULT hr;
    ScopedCom() {
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    }
    ~ScopedCom() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
};

}  // namespace

bool NativeThumbnailExtractor::is_supported() {
    return true;
}

std::string NativeThumbnailExtractor::provider_name() {
    return "Windows Shell (IShellItemImageFactory)";
}

PlatformCapabilities NativeThumbnailExtractor::capabilities() {
    PlatformCapabilities caps;
    caps.has_xdg_cache = true;
    caps.has_windows_shell = true;
    caps.has_macos_quicklook = false;
    caps.has_pdf_rendering = true;
    caps.pdf_backend = "Windows.Data.Pdf / Shell";
    caps.supported_extensions = {
        ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".ico", ".webp",
        ".heic", ".heif", ".avif",  // with the Store's HEIF/HEVC/AV1 extensions; else the built-in generator
        ".mp4", ".mkv", ".avi", ".wmv", ".mov",
        ".pdf", ".docx", ".xlsx", ".pptx",
        ".txt", ".md", ".cpp", ".h"
    };
    return caps;
}

Result NativeThumbnailExtractor::extract(const std::filesystem::path& path,
                                         int32_t target_size,
                                         Image& out_image) {
    ScopedCom com;

    ComPtr<IShellItem> shell_item;
    HRESULT hr = SHCreateItemFromParsingName(path.wstring().c_str(), nullptr,
                                            IID_PPV_ARGS(&shell_item));
    if (FAILED(hr) || !shell_item) {
        return Result::failure("SHCreateItemFromParsingName failed with hr=" + std::to_string(hr));
    }

    ComPtr<IShellItemImageFactory> factory;
    hr = shell_item.As(&factory);
    if (FAILED(hr) || !factory) {
        return Result::failure("QueryInterface for IShellItemImageFactory failed");
    }

    SIZE req_size = {target_size, target_size};
    HBITMAP hbitmap = nullptr;
    // Try resize to fit; if thumbnail only is needed: SIIGBF_RESIZETOFIT
    hr = factory->GetImage(req_size, SIIGBF_RESIZETOFIT, &hbitmap);
    if (FAILED(hr) || !hbitmap) {
        // Retry with SIIGBF_BIGGERSIZEOK
        hr = factory->GetImage(req_size, SIIGBF_BIGGERSIZEOK, &hbitmap);
    }

    if (FAILED(hr) || !hbitmap) {
        return Result::failure("IShellItemImageFactory::GetImage failed with hr=" + std::to_string(hr));
    }

    BITMAP bm = {};
    if (!GetObject(hbitmap, sizeof(BITMAP), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
        DeleteObject(hbitmap);
        return Result::failure("GetObject failed on Shell HBITMAP");
    }

    int width = bm.bmWidth;
    int height = bm.bmHeight;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint8_t> bgra(static_cast<size_t>(width) * height * 4);

    HDC hdc = GetDC(nullptr);
    int scan_lines = GetDIBits(hdc, hbitmap, 0, height, bgra.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    DeleteObject(hbitmap);

    if (scan_lines <= 0) {
        return Result::failure("GetDIBits failed to extract pixel data");
    }

    // Convert BGRA to RGBA8
    out_image.width = width;
    out_image.height = height;
    out_image.rgba.resize(static_cast<size_t>(width) * height * 4);

    bool has_nonzero_alpha = false;
    for (size_t i = 0; i < bgra.size(); i += 4) {
        if (bgra[i + 3] != 0) {
            has_nonzero_alpha = true;
            break;
        }
    }

    for (size_t i = 0; i < bgra.size(); i += 4) {
        uint8_t b = bgra[i + 0];
        uint8_t g = bgra[i + 1];
        uint8_t r = bgra[i + 2];
        uint8_t a = has_nonzero_alpha ? bgra[i + 3] : 255;

        out_image.rgba[i + 0] = r;
        out_image.rgba[i + 1] = g;
        out_image.rgba[i + 2] = b;
        out_image.rgba[i + 3] = a;
    }

    return Result::success();
}

}  // namespace brothumb

#endif  // _WIN32
