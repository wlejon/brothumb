// Minimal 6x10 monospace bitmap font for thumbnail preview rendering.
#pragma once

#include <cstdint>

namespace brothumb::detail {

constexpr int FONT_WIDTH = 6;
constexpr int FONT_HEIGHT = 10;

// Each glyph is 10 rows of 6 bits (represented as uint8_t row masks).
// ASCII 32 (' ') to 126 ('~').
extern const uint8_t FONT_6X10[95][10];

inline bool get_font_pixel(char c, int x, int y) {
    if (x < 0 || x >= FONT_WIDTH || y < 0 || y >= FONT_HEIGHT) return false;
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 32 || uc > 126) uc = '?';
    uint8_t row = FONT_6X10[uc - 32][y];
    return (row & (1 << (FONT_WIDTH - 1 - x))) != 0;
}

}  // namespace brothumb::detail
