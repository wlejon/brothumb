// Small value types shared across the brothumb library.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brothumb {

// Outcome of an operation. `error` is a human-readable reason when !ok.
struct Result {
    bool ok = false;
    std::string error;

    explicit operator bool() const { return ok; }
    static Result success() { return Result{true, {}}; }
    static Result failure(std::string why) { return Result{false, std::move(why)}; }
};

// Straight (non-premultiplied) RGBA8, row-major, tightly packed:
// rgba.size() == width * height * 4.
struct Image {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> rgba;

    bool empty() const { return width <= 0 || height <= 0 || rgba.empty(); }
    bool operator==(const Image&) const = default;
};

// Standard thumbnail sizes specified by Freedesktop Thumbnail Managing Standard.
enum class ThumbnailSize : int32_t {
    Normal = 128,    // 128x128
    Large = 256,     // 256x256
    XLarge = 512,    // 512x512
    XXLarge = 1024,  // 1024x1024
};

inline int32_t to_pixels(ThumbnailSize size) {
    return static_cast<int32_t>(size);
}

inline const char* to_string(ThumbnailSize size) {
    switch (size) {
        case ThumbnailSize::Normal:  return "normal";
        case ThumbnailSize::Large:   return "large";
        case ThumbnailSize::XLarge:  return "x-large";
        case ThumbnailSize::XXLarge: return "xx-large";
    }
    return "normal";
}

template <class CharT, class Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, ThumbnailSize s) {
    return os << to_string(s);
}

inline ThumbnailSize size_from_string(std::string_view name) {
    if (name == "large") return ThumbnailSize::Large;
    if (name == "x-large" || name == "xlarge") return ThumbnailSize::XLarge;
    if (name == "xx-large" || name == "xxlarge") return ThumbnailSize::XXLarge;
    return ThumbnailSize::Normal;
}

inline ThumbnailSize size_from_pixels(int32_t px) {
    if (px <= 128) return ThumbnailSize::Normal;
    if (px <= 256) return ThumbnailSize::Large;
    if (px <= 512) return ThumbnailSize::XLarge;
    return ThumbnailSize::XXLarge;
}

// Origin/source that produced the thumbnail.
enum class ThumbnailSource {
    Unknown,
    Cache,             // Loaded from XDG or platform disk cache
    GeneratorImage,    // Rendered via the built-in image generator (broimage decoders)
    GeneratorText,     // Rendered via text preview card generator
    GeneratorPdf,      // Rendered via PDF generator (WinRT, PDFKit, or pdftoppm)
    NativeShell,       // Windows Shell IShellItemImageFactory / macOS QuickLook
};

inline const char* to_string(ThumbnailSource src) {
    switch (src) {
        case ThumbnailSource::Unknown:        return "unknown";
        case ThumbnailSource::Cache:          return "cache";
        case ThumbnailSource::GeneratorImage: return "generator_image";
        case ThumbnailSource::GeneratorText:  return "generator_text";
        case ThumbnailSource::GeneratorPdf:   return "generator_pdf";
        case ThumbnailSource::NativeShell:    return "native_shell";
    }
    return "unknown";
}

template <class CharT, class Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os, ThumbnailSource src) {
    return os << to_string(src);
}

// Processing priority for thumbnail generation requests.
enum class Priority : int32_t {
    Low = 0,
    Normal = 1,
    High = 2,
};

// Cancellation token for aborting in-flight or pending requests.
class CancellationToken {
public:
    CancellationToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}
    explicit CancellationToken(std::shared_ptr<std::atomic<bool>> flag) : flag_(std::move(flag)) {}

    void cancel() const {
        if (flag_) flag_->store(true, std::memory_order_relaxed);
    }

    bool is_canceled() const {
        return flag_ && flag_->load(std::memory_order_relaxed);
    }

    std::shared_ptr<std::atomic<bool>> raw_flag() const { return flag_; }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

using RequestId = uint64_t;

// Configuration options for generating or retrieving a thumbnail.
struct ThumbnailOptions {
    ThumbnailSize size = ThumbnailSize::Normal;
    Priority priority = Priority::Normal;
    bool use_cache = true;       // Check disk cache before generating
    bool store_cache = true;     // Save generated thumbnail to disk cache
    bool prefer_native = true;   // Try OS native extractor first when available
    CancellationToken token;     // Optional cancellation token
};

// Capabilities reported honestly by the current platform.
struct PlatformCapabilities {
    bool has_xdg_cache = false;
    bool has_windows_shell = false;
    bool has_macos_quicklook = false;
    bool has_pdf_rendering = false;
    std::string pdf_backend;     // e.g. "Windows.Data.Pdf", "PDFKit", "pdftoppm", or "none"
    std::vector<std::string> supported_extensions;
    // Types the built-in generators render (ThumbnailService only; subtypes included, e.g.
    // every text/plain type). Files are typed by brovfs from name and content.
    std::vector<std::string> supported_mime_types;
};

}  // namespace brothumb
