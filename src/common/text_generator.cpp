#include "brothumb/generator.h"
#include "image_io.h"
#include "text_font.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace brothumb {

namespace {

inline void set_pixel_rgba(Image& img, int x, int y, uint32_t color) {
    if (x < 0 || x >= img.width || y < 0 || y >= img.height) return;
    size_t idx = (static_cast<size_t>(y) * img.width + x) * 4;
    img.rgba[idx + 0] = static_cast<uint8_t>((color >> 24) & 0xff);
    img.rgba[idx + 1] = static_cast<uint8_t>((color >> 16) & 0xff);
    img.rgba[idx + 2] = static_cast<uint8_t>((color >>  8) & 0xff);
    img.rgba[idx + 3] = static_cast<uint8_t>((color >>  0) & 0xff);
}

void fill_rect(Image& img, int x0, int y0, int w, int h, uint32_t color) {
    int x1 = std::min(img.width, x0 + w);
    int y1 = std::min(img.height, y0 + h);
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            set_pixel_rgba(img, x, y, color);
        }
    }
}

void draw_char(Image& img, char c, int x0, int y0, int scale, uint32_t color) {
    for (int cy = 0; cy < detail::FONT_HEIGHT; ++cy) {
        for (int cx = 0; cx < detail::FONT_WIDTH; ++cx) {
            if (detail::get_font_pixel(c, cx, cy)) {
                if (scale == 1) {
                    set_pixel_rgba(img, x0 + cx, y0 + cy, color);
                } else {
                    fill_rect(img, x0 + cx * scale, y0 + cy * scale, scale, scale, color);
                }
            }
        }
    }
}

void draw_string(Image& img, const std::string& str, int x0, int y0, int scale, uint32_t color) {
    int cur_x = x0;
    int step = detail::FONT_WIDTH * scale;
    for (char c : str) {
        if (cur_x + step > img.width) break;
        draw_char(img, c, cur_x, y0, scale, color);
        cur_x += step;
    }
}

bool is_keyword(std::string_view word) {
    static const std::string_view keywords[] = {
        "fn", "def", "class", "struct", "import", "include", "return", "if",
        "else", "for", "while", "const", "let", "var", "function", "public",
        "private", "namespace", "template", "auto", "void", "int", "bool",
        "true", "false", "nil", "null", "none", "async", "await", "export",
        "type", "interface", "package", "val", "fun", "self", "this"
    };
    for (const auto& kw : keywords) {
        if (word == kw) return true;
    }
    return false;
}

}  // namespace

TextThumbnailGenerator::TextThumbnailGenerator() = default;

std::vector<std::string> TextThumbnailGenerator::supported_mime_types() const {
    // Everything that is text/plain qualifies; these name the common families for capability
    // reporting.
    return {
        "text/plain", "text/markdown", "text/x-csrc", "text/x-chdr", "text/x-c++src", "text/x-c++hdr",
        "text/x-python", "application/javascript", "application/typescript", "application/json",
        "application/yaml", "application/toml", "application/xml", "text/html", "text/css",
        "application/x-shellscript", "text/x-patch",
    };
}

bool TextThumbnailGenerator::can_generate(const std::filesystem::path& path,
                                         const std::string& mime_hint) const {
    return detail::type_is_a(detail::resolve_type(path, mime_hint), "text/plain");
}

Result TextThumbnailGenerator::generate(const std::filesystem::path& path,
                                        int32_t target_size, Image& out_image) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return Result::failure("File does not exist: " + path.string());
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return Result::failure("Failed to open file: " + path.string());
    }

    // Read first ~40 lines or up to 8KB
    std::vector<std::string> lines;
    std::string line;
    size_t total_chars = 0;
    while (lines.size() < 40 && total_chars < 8192 && std::getline(file, line)) {
        // Expand tabs to 4 spaces
        std::string expanded;
        for (char c : line) {
            if (c == '\t') expanded.append(4, ' ');
            else if (c == '\r') continue;
            else expanded.push_back(c);
        }
        total_chars += expanded.size();
        lines.push_back(std::move(expanded));
    }

    if (lines.empty()) {
        lines.push_back("(empty file)");
    }

    int32_t size = std::max(64, target_size);
    out_image.width = size;
    out_image.height = size;
    out_image.rgba.resize(static_cast<size_t>(size) * size * 4);

    // Fill background
    fill_rect(out_image, 0, 0, size, size, style_.bg_color);

    // Card dimensions
    int margin = std::max(3, size / 32);
    int card_x = margin;
    int card_y = margin;
    int card_w = size - 2 * margin;
    int card_h = size - 2 * margin;

    // Card background & subtle border
    fill_rect(out_image, card_x, card_y, card_w, card_h, style_.border_color);
    fill_rect(out_image, card_x + 1, card_y + 1, card_w - 2, card_h - 2, style_.card_bg);

    int scale = (size >= 256) ? 2 : 1;
    int char_w = detail::FONT_WIDTH * scale;
    int char_h = detail::FONT_HEIGHT * scale;
    int line_spacing = std::max(1, 2 * scale);
    int line_h = char_h + line_spacing;

    int content_top = card_y + 4 * scale;

    // Header bar
    if (style_.show_header) {
        int header_h = char_h + 6 * scale;
        fill_rect(out_image, card_x + 1, card_y + 1, card_w - 2, header_h, 0x21222CFF);

        // Window control dots
        int dot_r = std::max(1, 2 * scale);
        int dot_y = card_y + 3 * scale;
        fill_rect(out_image, card_x + 6 * scale, dot_y, dot_r * 2, dot_r * 2, 0xFF5555FF); // red
        fill_rect(out_image, card_x + 12 * scale, dot_y, dot_r * 2, dot_r * 2, 0xF1FA8CFF); // yellow
        fill_rect(out_image, card_x + 18 * scale, dot_y, dot_r * 2, dot_r * 2, 0x50FA7BFF); // green

        // File extension badge on the right
        std::string ext = path.extension().string();
        if (ext.size() > 1 && ext[0] == '.') ext.erase(0, 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        if (ext.empty()) ext = "TXT";

        int badge_w = static_cast<int>(ext.size()) * char_w + 4 * scale;
        int badge_x = card_x + card_w - badge_w - 4 * scale;
        draw_string(out_image, ext, badge_x + 2 * scale, card_y + 3 * scale, scale, style_.accent_color);

        content_top += header_h;
    }

    // Code lines rendering
    int gutter_chars = (lines.size() >= 10) ? 2 : 1;
    int gutter_w = style_.show_line_numbers ? (gutter_chars * char_w + 4 * scale) : 0;
    int text_x0 = card_x + 4 * scale + gutter_w;
    int cur_y = content_top + 2 * scale;

    for (size_t i = 0; i < lines.size(); ++i) {
        if (cur_y + char_h > card_y + card_h - 2 * scale) break;

        // Draw line number
        if (style_.show_line_numbers) {
            std::string num_str = std::to_string(i + 1);
            if (num_str.size() < static_cast<size_t>(gutter_chars)) {
                num_str.insert(0, gutter_chars - num_str.size(), ' ');
            }
            draw_string(out_image, num_str, card_x + 3 * scale, cur_y, scale, style_.gutter_color);
        }

        // Draw line text with basic syntax coloring
        const std::string& l = lines[i];
        int cur_x = text_x0;

        // Check if comment line
        std::string_view sv = l;
        size_t first_non_ws = sv.find_first_not_of(' ');
        bool is_comment = false;
        if (first_non_ws != std::string_view::npos) {
            if (sv.substr(first_non_ws, 2) == "//" || sv[first_non_ws] == '#') {
                is_comment = true;
            }
        }

        if (is_comment) {
            draw_string(out_image, l, cur_x, cur_y, scale, 0x6272A4FF); // muted cyan/gray
        } else {
            // Token-based highlighting
            size_t pos = 0;
            while (pos < l.size() && cur_x < card_x + card_w - char_w) {
                if (std::isspace(static_cast<unsigned char>(l[pos]))) {
                    cur_x += char_w;
                    ++pos;
                } else if (l[pos] == '"' || l[pos] == '\'') {
                    // String literal
                    char quote = l[pos];
                    std::string str_tok;
                    str_tok.push_back(l[pos++]);
                    while (pos < l.size() && l[pos] != quote) {
                        str_tok.push_back(l[pos++]);
                    }
                    if (pos < l.size()) str_tok.push_back(l[pos++]);
                    draw_string(out_image, str_tok, cur_x, cur_y, scale, 0xF1FA8CFF); // yellow
                    cur_x += static_cast<int>(str_tok.size()) * char_w;
                } else if (std::isalpha(static_cast<unsigned char>(l[pos])) || l[pos] == '_') {
                    // Word / identifier / keyword
                    size_t start = pos;
                    while (pos < l.size() && (std::isalnum(static_cast<unsigned char>(l[pos])) || l[pos] == '_')) {
                        ++pos;
                    }
                    std::string_view word(&l[start], pos - start);
                    uint32_t col = is_keyword(word) ? 0xFF79C6FF : style_.text_color;
                    draw_string(out_image, std::string(word), cur_x, cur_y, scale, col);
                    cur_x += static_cast<int>(word.size()) * char_w;
                } else if (std::isdigit(static_cast<unsigned char>(l[pos]))) {
                    // Number
                    size_t start = pos;
                    while (pos < l.size() && (std::isalnum(static_cast<unsigned char>(l[pos])) || l[pos] == '.')) {
                        ++pos;
                    }
                    std::string_view num(&l[start], pos - start);
                    draw_string(out_image, std::string(num), cur_x, cur_y, scale, 0xBD93F9FF); // purple
                    cur_x += static_cast<int>(num.size()) * char_w;
                } else {
                    // Symbol / punctuation
                    draw_char(out_image, l[pos], cur_x, cur_y, scale, 0xFFB86CFF); // orange/gold
                    cur_x += char_w;
                    ++pos;
                }
            }
        }

        cur_y += line_h;
    }

    return Result::success();
}

}  // namespace brothumb
