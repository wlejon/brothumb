// Built-in and pluggable thumbnail generators.
#pragma once

#include "brothumb/common.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace brothumb {

// Abstract interface for thumbnail generators.
class IThumbnailGenerator {
public:
    virtual ~IThumbnailGenerator() = default;

    // Human-readable identifier for this generator.
    virtual std::string name() const = 0;

    // Returns true if this generator handles the file's type. `mime_hint` is the type when the
    // caller already resolved it; empty asks brovfs (bro::vfs::MimeDatabase::system()), which
    // types the file by name and content.
    virtual bool can_generate(const std::filesystem::path& path,
                              const std::string& mime_hint = "") const = 0;

    // The types this generator renders; subtypes of them are accepted too.
    virtual std::vector<std::string> supported_mime_types() const = 0;

    // Generates a thumbnail image downscaled to fit within target_size x target_size
    // while preserving original aspect ratio.
    virtual Result generate(const std::filesystem::path& path, int32_t target_size,
                            Image& out_image) = 0;
};

// Pluggable image decoder function type: (file_bytes) -> Result + Image.
using ImageDecoderFunc = std::function<Result(const uint8_t* data, size_t size, Image& out_image)>;

// Built-in image generator: everything broimage decodes (PNG, JPEG, GIF, BMP, TGA, PSD, HDR,
// binary PNM; JPEG EXIF orientation applied), plus decoders registered per extension.
class ImageThumbnailGenerator : public IThumbnailGenerator {
public:
    ImageThumbnailGenerator();
    std::string name() const override { return "ImageThumbnailGenerator"; }

    bool can_generate(const std::filesystem::path& path,
                      const std::string& mime_hint = "") const override;
    std::vector<std::string> supported_mime_types() const override;

    Result generate(const std::filesystem::path& path, int32_t target_size,
                    Image& out_image) override;

    // Register a custom image decoder for additional image extensions (e.g. ".webp", ".qoi").
    // A registered extension is used before the type check and before broimage.
    void register_decoder(std::string extension, ImageDecoderFunc decoder);

private:
    std::unordered_map<std::string, ImageDecoderFunc> custom_decoders_;
};

// Built-in text file preview generator (renders first lines into a stylized card). Handles every
// text/plain type: source code, markup, JSON / YAML / TOML, logs, and extensionless text that
// brovfs recognises by content.
class TextThumbnailGenerator : public IThumbnailGenerator {
public:
    TextThumbnailGenerator();
    std::string name() const override { return "TextThumbnailGenerator"; }

    bool can_generate(const std::filesystem::path& path,
                      const std::string& mime_hint = "") const override;
    std::vector<std::string> supported_mime_types() const override;

    Result generate(const std::filesystem::path& path, int32_t target_size,
                    Image& out_image) override;

    // Configurable rendering styles (dark/light background, card theme).
    struct CardStyle {
        uint32_t bg_color = 0x1E1E2EFF;         // Dark sleek slate
        uint32_t card_bg = 0x282A36FF;          // Slightly lighter card container
        uint32_t border_color = 0x44475AFF;     // Subtle card border
        uint32_t text_color = 0xF8F8F2FF;       // Bright white/cream text
        uint32_t gutter_color = 0x6272A4FF;     // Monospace line number gutter
        uint32_t accent_color = 0xBD93F9FF;     // Extension badge / accent color
        bool show_header = true;                // Draw top title/extension tab
        bool show_line_numbers = true;
    };

    void set_style(const CardStyle& style) { style_ = style; }
    const CardStyle& style() const { return style_; }

private:
    CardStyle style_;
};

// PDF first-page thumbnail generator (uses platform native PDF renderer).
class PdfThumbnailGenerator : public IThumbnailGenerator {
public:
    PdfThumbnailGenerator();
    std::string name() const override { return "PdfThumbnailGenerator"; }

    bool can_generate(const std::filesystem::path& path,
                      const std::string& mime_hint = "") const override;
    std::vector<std::string> supported_mime_types() const override { return {"application/pdf"}; }

    Result generate(const std::filesystem::path& path, int32_t target_size,
                    Image& out_image) override;

    // Returns whether PDF rendering is actually supported on the current host.
    static bool is_available();

    // Returns the name of the backend used (e.g. "Windows.Data.Pdf", "PDFKit", "pdftoppm", "none").
    static std::string backend_name();
};

}  // namespace brothumb
