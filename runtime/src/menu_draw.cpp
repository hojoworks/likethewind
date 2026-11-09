#include "likethewind/menu.h"
#include "menu_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace likethewind {
namespace {
const char *const kTabs[] = {"CONTROLS", "DIP SW", "DISPLAY", "SOUND",
                             "MIXER",    "SYSTEM"};
const char *const kOnOff[2] = {"OFF", "ON"};
constexpr int kTabCount = 6;
struct Glyph {
    char c;
    uint8_t rows[7];
};

// 5x7 bitmap font, uppercase. Menu strings are converted to uppercase.
const Glyph kGlyphs[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
    {'\'', {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'%', {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'*', {0x00, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
};

const uint8_t *glyph(char c) {
    c = (char)std::toupper((unsigned char)c);
    for (const Glyph &g : kGlyphs)
        if (g.c == c) return g.rows;
    return nullptr;
}

int text_width(const std::string &s, int scale = 1) {
    return (int)s.size() * 6 * scale;
}

// Compact 4x5 body font. One pixel of spacing between glyphs, so a character
// advances 5 px instead of 6. Used for menu rows and footers so more entries
// fit on one page.
struct SmallGlyph {
    char c;
    uint8_t rows[5];
};

const SmallGlyph kSmallGlyphs[] = {
    {' ', {0x0, 0x0, 0x0, 0x0, 0x0}},
    {'A', {0x6, 0x9, 0xF, 0x9, 0x9}},
    {'B', {0xE, 0x9, 0xE, 0x9, 0xE}},
    {'C', {0x7, 0x8, 0x8, 0x8, 0x7}},
    {'D', {0xE, 0x9, 0x9, 0x9, 0xE}},
    {'E', {0xF, 0x8, 0xE, 0x8, 0xF}},
    {'F', {0xF, 0x8, 0xE, 0x8, 0x8}},
    {'G', {0x7, 0x8, 0xB, 0x9, 0x7}},
    {'H', {0x9, 0x9, 0xF, 0x9, 0x9}},
    {'I', {0xE, 0x4, 0x4, 0x4, 0xE}},
    {'J', {0xE, 0x2, 0x2, 0xA, 0x4}},
    {'K', {0x9, 0xA, 0xC, 0xA, 0x9}},
    {'L', {0x8, 0x8, 0x8, 0x8, 0xF}},
    {'M', {0x9, 0xF, 0xF, 0x9, 0x9}},
    {'N', {0x9, 0xD, 0xF, 0xB, 0x9}},
    {'O', {0x6, 0x9, 0x9, 0x9, 0x6}},
    {'P', {0xE, 0x9, 0xE, 0x8, 0x8}},
    {'Q', {0x6, 0x9, 0x9, 0xB, 0x7}},
    {'R', {0xE, 0x9, 0xE, 0xA, 0x9}},
    {'S', {0x7, 0x8, 0x6, 0x1, 0xE}},
    {'T', {0xF, 0x4, 0x4, 0x4, 0x4}},
    {'U', {0x9, 0x9, 0x9, 0x9, 0x6}},
    {'V', {0x9, 0x9, 0x9, 0x6, 0x4}},
    {'W', {0x9, 0x9, 0xF, 0xF, 0x9}},
    {'X', {0x9, 0x9, 0x6, 0x9, 0x9}},
    {'Y', {0x9, 0x9, 0x6, 0x4, 0x4}},
    {'Z', {0xF, 0x1, 0x6, 0x8, 0xF}},
    {'0', {0x6, 0xB, 0xD, 0x9, 0x6}},
    {'1', {0x4, 0xC, 0x4, 0x4, 0xE}},
    {'2', {0xE, 0x1, 0x6, 0x8, 0xF}},
    {'3', {0xE, 0x1, 0x6, 0x1, 0xE}},
    {'4', {0xA, 0xA, 0xF, 0x2, 0x2}},
    {'5', {0xF, 0x8, 0xE, 0x1, 0xE}},
    {'6', {0x6, 0x8, 0xE, 0x9, 0x6}},
    {'7', {0xF, 0x1, 0x2, 0x4, 0x4}},
    {'8', {0x6, 0x9, 0x6, 0x9, 0x6}},
    {'9', {0x6, 0x9, 0x7, 0x1, 0x6}},
    {'/', {0x1, 0x2, 0x4, 0x8, 0x0}},
    {'-', {0x0, 0x0, 0xF, 0x0, 0x0}},
    {'.', {0x0, 0x0, 0x0, 0x0, 0x4}},
    {':', {0x0, 0x4, 0x0, 0x4, 0x0}},
    {',', {0x0, 0x0, 0x0, 0x4, 0x8}},
    {'\'', {0x4, 0x8, 0x0, 0x0, 0x0}},
    {'(', {0x2, 0x4, 0x4, 0x4, 0x2}},
    {')', {0x4, 0x2, 0x2, 0x2, 0x4}},
    {'+', {0x0, 0x4, 0xE, 0x4, 0x0}},
    {'%', {0x9, 0x2, 0x4, 0x8, 0x9}},
    {'?', {0xE, 0x1, 0x6, 0x0, 0x4}},
    {'!', {0x4, 0x4, 0x4, 0x0, 0x4}},
    {'>', {0x8, 0x4, 0x2, 0x4, 0x8}},
    {'<', {0x2, 0x4, 0x8, 0x4, 0x2}},
    {'*', {0x0, 0xA, 0x4, 0xA, 0x0}},
    {'=', {0x0, 0xF, 0x0, 0xF, 0x0}},
    {'#', {0xA, 0xF, 0xA, 0xF, 0xA}},
};

const uint8_t *small_glyph(char c) {
    c = (char)std::toupper((unsigned char)c);
    for (const SmallGlyph &g : kSmallGlyphs)
        if (g.c == c) return g.rows;
    return nullptr;
}

int small_text_width(const std::string &s, int scale = 1) {
    return (int)s.size() * 5 * scale;
}

void draw_small_text(uint32_t *px, int w, int h, int x, int y,
                     const std::string &s, uint32_t color, int scale = 1) {
    for (char ch : s) {
        const uint8_t *rows = small_glyph(ch);
        if (rows) {
            for (int r = 0; r < 5; r++) {
                for (int c = 0; c < 4; c++) {
                    if (!(rows[r] & (1u << (3 - c)))) continue;
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            const int X = x + c * scale + dx;
                            const int Y = y + r * scale + dy;
                            if (X >= 0 && X < w && Y >= 0 && Y < h)
                                px[(size_t)Y * w + X] = color;
                        }
                    }
                }
            }
        }
        x += 5 * scale;
    }
}

void draw_text(uint32_t *px, int w, int h, int x, int y, const std::string &s,
               uint32_t color, int scale = 1) {
    for (char ch : s) {
        const uint8_t *rows = glyph(ch);
        if (rows) {
            for (int r = 0; r < 7; r++) {
                for (int c = 0; c < 5; c++) {
                    if (!(rows[r] & (1u << (4 - c)))) continue;
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            const int X = x + c * scale + dx;
                            const int Y = y + r * scale + dy;
                            if (X >= 0 && X < w && Y >= 0 && Y < h)
                                px[(size_t)Y * w + X] = color;
                        }
                    }
                }
            }
        }
        x += 6 * scale;
    }
}

uint32_t blend(uint32_t dst, uint32_t src, int alpha) {
    const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    const int sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    const int r = (dr * (256 - alpha) + sr * alpha) >> 8;
    const int g = (dg * (256 - alpha) + sg * alpha) >> 8;
    const int b = (db * (256 - alpha) + sb * alpha) >> 8;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void fill(uint32_t *px, int w, int h, int x, int y, int fw, int fh, uint32_t color,
          int alpha = 256) {
    for (int yy = y; yy < y + fh; yy++) {
        if (yy < 0 || yy >= h) continue;
        for (int xx = x; xx < x + fw; xx++) {
            if (xx < 0 || xx >= w) continue;
            uint32_t &p = px[(size_t)yy * w + xx];
            p = (alpha >= 256) ? color : blend(p, color, alpha);
        }
    }
}

void draw_button(uint32_t *px, int w, int h, int x, int y, int bw, const char *label,
                 bool active, uint32_t hot, uint32_t dim) {
    constexpr int kButtonHeight = 15;
    if (active) fill(px, w, h, x - 2, y - 2, bw + 4, kButtonHeight + 4, hot);
    fill(px, w, h, x, y, bw, kButtonHeight, active ? 0xFF3A2A08 : 0xFF1A2436);
    draw_small_text(px, w, h, x + (bw - small_text_width(label)) / 2, y + 5, label,
                    active ? hot : dim);
}
} // namespace

using namespace menu_internal;

void Menu::draw_fps(uint32_t *pixels, int width, int height, double fps,
                    double present_fps, int blend_pct) const {
    if (!pixels) return;
    char b[32];
    if (present_fps > 0.0)
        std::snprintf(b, sizeof(b), "PRESENT %d FPS",
                      (int)(present_fps + 0.5));
    else
        std::snprintf(b, sizeof(b), "PRESENT --");
    char b2[48];
    std::snprintf(b2, sizeof(b2), "GAME %.1f  BLEND %d%%", fps, blend_pct);
    const int bgw = std::max(small_text_width(b), small_text_width(b2)) + 4;
    fill(pixels, width, height, 2, 2, bgw, 14, 0x00000000, 160);
    draw_small_text(pixels, width, height, 4, 3, b, 0xFFFFD040);
    draw_small_text(pixels, width, height, 4, 9, b2,
                    blend_pct > 0 ? 0xFFFFD040 : 0xFF8896AA);
}

void Menu::draw_gear(uint32_t *pixels, int width, int height, bool high) const {
    if (!settings_.show_gear || !pixels) return;

    // Two small stacked lamps, in the spirit of an arcade gear selector:
    // H on top, L below, with only the selected lamp lit.
    const int pw = 18;
    const int ph = 28;
    int x = 6;
    int y = 6;
    switch (settings_.gear_corner) {
    case 0: x = 6; y = 6; break;
    case 2: x = 6; y = height - ph - 6; break;
    case 3: x = width - pw - 6; y = height - ph - 6; break;
    default: x = width - pw - 6; y = 6; break;
    }
    if (settings_.gear_corner == 0 && settings_.show_fps) y = 28;

    const uint32_t lit = 0xFF5BE33A;
    const uint32_t lit_highlight = 0xFFB6FF8A;
    const uint32_t unlit = 0xFF242A30;
    const uint32_t frame = 0xFF7A828A;

    fill(pixels, width, height, x, y, pw, ph, frame);
    fill(pixels, width, height, x + 1, y + 1, pw - 2, ph - 2, 0xFF0E1216);

    auto lamp = [&](int ly, bool active, char letter) {
        fill(pixels, width, height, x + 2, ly, 14, 11, 0xFF080A0C);
        fill(pixels, width, height, x + 3, ly + 1, 12, 9, active ? lit : unlit);
        if (active) fill(pixels, width, height, x + 4, ly + 1, 10, 1, lit_highlight);
        char text[2] = {letter, 0};
        draw_text(pixels, width, height, x + 6, ly + 2, text,
                  active ? 0xFF081008 : 0xFF6B737B);
    };

    lamp(y + 2, high, 'H');
    lamp(y + 15, !high, 'L');
}

void Menu::draw_tour_status(uint32_t *pixels, int width, int height,
                            const std::string &title,
                            const std::string &detail) const {
    if (!pixels || title.empty()) return;
    const int title_w = small_text_width(title);
    const int detail_w = detail.empty() ? 0 : small_text_width(detail);
    const int bw = std::max(title_w, detail_w) + 12;
    const int x = (width - bw) / 2;
    const int y = 5;
    const int bh = detail.empty() ? 16 : 25;
    fill(pixels, width, height, x, y, bw, bh, 0x00000000, 190);
    fill(pixels, width, height, x, y, bw, 1, 0xFFFFD040);
    fill(pixels, width, height, x, y + bh - 1, bw, 1, 0xFFFFD040);
    draw_small_text(pixels, width, height, x + 6, y + 4, title, 0xFFFFD040);
    if (!detail.empty())
        draw_small_text(pixels, width, height, x + 6, y + 14, detail,
                        0xFF6FD3FF);
}

void Menu::draw_tour_notice(uint32_t *pixels, int width, int height,
                            const std::string &text) const {
    if (!pixels || text.empty()) return;
    const int tw = small_text_width(text);
    const int x = (width - tw) / 2;
    const int y = height * 2 / 3;
    fill(pixels, width, height, x - 4, y - 4, tw + 8, 13,
         0x00000000, 185);
    draw_small_text(pixels, width, height, x, y, text, 0xFFFFD040);
}

void Menu::draw_tour_tracker(
    uint32_t *pixels, int width, int height,
    const std::array<std::array<int8_t, 5>, 5> &places,
    const std::array<int8_t, 2> &special_places, int start_course,
    const std::string &stats) const {
    if (!pixels) return;

    // Match the in-game portrait strip exactly. It occupies board x=69..244
    // and stretches with the widescreen view.
    constexpr int kContentWidth = 224;
    constexpr int kPortraitLeft = 69;
    constexpr int kPortraitWidth = 175;
    constexpr int kBannerHeight = 17;
    const int banner_w = (kPortraitWidth * width + 160) / 320;
    const int x0 = (kPortraitLeft * width + 160) / 320;
    const int y0 = -1;
    fill(pixels, width, height, x0, y0, banner_w, kBannerHeight,
         0x00000000, 190);
    const auto map_x = [&](int native_x) {
        return x0 + (int)std::lround((double)native_x * banner_w / kContentWidth);
    };

    const uint32_t gold = 0xFFFFD040;
    const uint32_t empty = 0xFF34404E;
    const auto draw_badge = [&](int place, int bx, int by) {
        if (place < 0) {
            // Empty result: a dim slot with a dash.
            fill(pixels, width, height, bx, by, 6, 7, 0x00000000, 100);
            fill(pixels, width, height, bx, by + 6, 6, 1, empty);
            fill(pixels, width, height, bx + 2, by + 3, 2, 1, 0xFF6A7686);
        } else if (place <= 2) {
            // Power Drift-style trophy for a podium finish.
            static constexpr uint8_t kTrophy[7][6] = {
                {0, 1, 1, 1, 1, 0},
                {1, 1, 1, 1, 1, 1},
                {1, 0, 1, 1, 0, 1},
                {1, 0, 1, 1, 0, 1},
                {0, 1, 1, 1, 1, 0},
                {0, 0, 1, 1, 0, 0},
                {0, 1, 1, 1, 1, 0},
            };
            uint32_t main, hi, lo;
            if (place == 0) {
                main = 0xFFFFC020; hi = 0xFFFFF080; lo = 0xFFB07000;
            } else if (place == 1) {
                main = 0xFFC8D0D8; hi = 0xFFFFFFFF; lo = 0xFF707880;
            } else {
                main = 0xFFD08040; hi = 0xFFFFC080; lo = 0xFF804020;
            }
            for (int r = 0; r < 7; ++r) {
                for (int col = 0; col < 6; ++col) {
                    if (!kTrophy[r][col]) continue;
                    uint32_t color = main;
                    if (r == 0 || (col == 1 && r < 3)) color = hi;
                    else if (r >= 5 || col == 4) color = lo;
                    fill(pixels, width, height, bx + col, by + r, 1, 1,
                         color);
                }
            }
        } else {
            // 4th and below: a compact ordinal badge. Two-digit ranks use
            // a narrow 3x5 font so 10/11/12 fit without looking like the
            // course letters A/B/C.
            const int rank = place + 1;
            const uint32_t color =
                place <= 4 ? 0xFFDCE4EE
                           : (place <= 7 ? 0xFF9AD0FF : 0xFF8A96A8);
            fill(pixels, width, height, bx, by, 6, 7, 0x00000000, 210);
            if (rank >= 10) {
                static constexpr uint8_t kMiniDigit[10][5] = {
                    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7},
                    {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
                    {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
                    {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
                    {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
                };
                const auto draw_mini = [&](int digit, int dx) {
                    for (int row = 0; row < 5; ++row) {
                        for (int col = 0; col < 3; ++col) {
                            if (kMiniDigit[digit][row] & (1u << (2 - col)))
                                fill(pixels, width, height, dx + col,
                                     by + 1 + row, 1, 1, color);
                        }
                    }
                };
                draw_mini(rank / 10, bx);
                draw_mini(rank % 10, bx + 3);
            } else {
                draw_small_text(pixels, width, height, bx + 1, by + 1,
                                std::string(1, (char)('0' + rank)), color);
            }
        }
    };

    const int badge_y = y0 + 2;
    // Pack each course's five stage badges tightly, then leave a clear gap
    // before the next course letter. The gap absorbs the extra widescreen
    // width instead of spreading the badges apart.
    constexpr int kBadgeWidth = 6;
    constexpr int kBadgeStep = kBadgeWidth;
    constexpr int kLetterWidth = 5;
    constexpr int kLetterGap = 2;
    constexpr int kSpWidth = 10;
    constexpr int kGroupWidth =
        kLetterWidth + kLetterGap + 5 * kBadgeStep;
    constexpr int kSpGroupWidth = kSpWidth + kLetterGap + 2 * kBadgeStep;
    constexpr int kTightWidth = 5 * kGroupWidth + kSpGroupWidth;
    const int edge_margin = std::max(2, banner_w / 32);
    const int available = banner_w - 2 * edge_margin;
    if (available >= kTightWidth + 10) {
        const int gap = (available - kTightWidth) / 5;
        const int used = kTightWidth + gap * 5;
        int x = x0 + edge_margin + (available - used) / 2;
        for (int i = 0; i < 5; ++i) {
            const int c = (start_course + i) % 5;
            draw_small_text(pixels, width, height, x, badge_y,
                            std::string(1, (char)('A' + c)), gold);
            x += kLetterWidth + kLetterGap;
            for (int s = 0; s < 5; ++s) {
                draw_badge(places[c][s], x, badge_y);
                x += kBadgeStep;
            }
            x += gap;
        }
        draw_small_text(pixels, width, height, x, badge_y, "SP", gold);
        x += kSpWidth + kLetterGap;
        draw_badge(special_places[0], x, badge_y);
        x += kBadgeStep;
        draw_badge(special_places[1], x, badge_y);
    } else {
        // Native 4:3 is too narrow for fixed-size badges with gaps; fall back
        // to proportional placement so everything still fits on the strip.
        int x = 0;
        for (int i = 0; i < 5; ++i) {
            const int c = (start_course + i) % 5;
            draw_small_text(pixels, width, height, map_x(x), badge_y,
                            std::string(1, (char)('A' + c)), gold);
            x += 6;
            for (int s = 0; s < 5; ++s) {
                draw_badge(places[c][s], map_x(x), badge_y);
                x += (s == 4) ? 6 : 7;
            }
        }
        draw_small_text(pixels, width, height, map_x(x), badge_y, "SP", gold);
        x += 11;
        draw_badge(special_places[0], map_x(x), badge_y);
        x += 7;
        draw_badge(special_places[1], map_x(x), badge_y);
    }

    const int stats_w = stats.empty() ? 0 : small_text_width(stats);
    if (!stats.empty()) {
        draw_small_text(pixels, width, height,
                        x0 + (banner_w - stats_w) / 2, y0 + 10, stats,
                        0xFF6FD3FF);
    }
}

void Menu::draw_mixer(uint32_t *pixels, int width, int height, int px0, int py0) const {
    const uint32_t dim = 0xFF8896AA;
    const uint32_t hot = 0xFFFFD040;
    const uint32_t cyan = 0xFF6FD3FF;
    const uint32_t sfx_color = 0xFFFF9A55;
    const uint32_t solo_color = 0xFFFF5A9A;

    std::string status;
    uint32_t status_color = 0xFFDCE4EE;
    const bool stock_mode = (!mixer_mode_ && settings_.audio_preset == 0);
    const std::string tag = stock_mode
                                ? "STOCK  "
                                : (settings_.ab_original ? "A  " : "B  ");
    if (sel_ >= 0 && sel_ < 24) {
        const bool is_ym = sel_ < 8;
        const int i = is_ym ? sel_ : sel_ - 8;
        const bool solo = (settings_.solo_mask & (1u << (is_ym ? i : 8 + i))) != 0;
        status = tag;
        status += (is_ym ? "YM" + std::to_string(i + 1) : pcm_channel_name(i)) +
                  " VOL " +
                  std::to_string(stock_mode
                                     ? 100
                                     : (is_ym ? settings_.ym_volume[i]
                                              : settings_.pcm_volume[i])) +
                  " C" + std::to_string(stock_mode
                                                ? 0
                                                : (is_ym ? settings_.ym_send[i]
                                                         : settings_.pcm_send[i])) +
                  " R" + std::to_string(stock_mode
                                                ? 0
                                                : (is_ym ? settings_.ym_reverb_send[i]
                                                         : settings_.pcm_reverb_send[i]));
        if (solo) {
            status += " SOLO";
            status_color = solo_color;
        } else {
            status_color = hot;
        }
    } else if (sel_ == 24) {
        status = tag + "CHORUS EFFECTS";
        status_color = hot;
    } else if (sel_ == 25) {
        status = tag + "PLATE REVERB";
        status_color = hot;
    } else if (sel_ == 26) {
        if (stock_mode) {
            status = "AUDIO MIX  STOCK";
        } else {
            status = settings_.ab_original ? "A/B  ORIGINAL" : "A/B  MIXED";
        }
        status_color = hot;
    } else {
        status = tag + "RESET ALL CHANNELS AND FX";
        status_color = hot;
    }
    draw_small_text(pixels, width, height, px0 + 12, py0 + 44, status, status_color);

    const int col_w = 34;
    const int start_x = px0 + 12;
    const int row_h = 35;
    const int fh = 16;

    for (int idx = 0; idx < 24; ++idx) {
        const int row = idx / 8;
        const int col = idx % 8;
        const int cx = start_x + col * col_w;
        const int top = py0 + 56 + row * row_h;
        const int fader_x = cx + 8;
        const int vu_x = cx + 16;
        const int fy = top + 8;
        const bool active = (sel_ == idx);
        const bool is_ym = idx < 8;
        const int i = is_ym ? idx : idx - 8;
        const bool solo = (settings_.solo_mask & (1u << (is_ym ? i : 8 + i))) != 0;
        const int volume =
            stock_mode ? 100 : (is_ym ? settings_.ym_volume[i] : settings_.pcm_volume[i]);
        const int send =
            stock_mode ? 0 : (is_ym ? settings_.ym_send[i] : settings_.pcm_send[i]);
        const int rev_send = stock_mode
                                 ? 0
                                 : (is_ym ? settings_.ym_reverb_send[i]
                                          : settings_.pcm_reverb_send[i]);
        const float level = is_ym ? ym_level_[i] : pcm_level_[i];
        const std::string label = is_ym ? "YM" + std::to_string(i + 1)
                                        : pcm_channel_name(i);
        const uint32_t role_color = cyan;
        const uint32_t accent = solo ? solo_color : (active ? hot : role_color);

        draw_small_text(pixels, width, height, cx + (col_w - small_text_width(label)) / 2,
                        top, label, solo ? solo_color : (active ? hot : dim));

        fill(pixels, width, height, fader_x, fy, 5, fh, 0xFF1A2436);
        const int filled = fh * volume / Settings::kChannelMax;
        fill(pixels, width, height, fader_x, fy + fh - filled, 5, filled, accent);
        const int knob_y = std::clamp(fy + fh - filled - 2, fy - 2, fy + fh - 3);
        fill(pixels, width, height, fader_x - 3, knob_y, 11, 4,
             active ? 0xFFFFFFFF : 0xFFB8C4D4);

        fill(pixels, width, height, vu_x, fy, 6, fh, 0xFF101418);
        const int vu_fill = std::clamp((int)(fh * level), 0, fh);
        const int vu_top = fy + fh - vu_fill;
        fill(pixels, width, height, vu_x, vu_top, 6, vu_fill, 0xFF5BE33A);
        if (vu_fill > fh * 7 / 10)
            fill(pixels, width, height, vu_x, vu_top, 6, vu_fill - fh * 7 / 10,
                 0xFFFFD040);
        if (vu_fill > fh * 9 / 10)
            fill(pixels, width, height, vu_x, vu_top, 6, vu_fill - fh * 9 / 10,
                 0xFFFF5A4A);

        // Upper bar: chorus send. Lower bar: plate-reverb send.
        const int sx = cx + 8;
        const int sy = top + 26;
        fill(pixels, width, height, sx, sy, 18, 3, 0xFF1A2436);
        fill(pixels, width, height, sx, sy, 18 * send / 100, 3, accent);
        fill(pixels, width, height, sx, sy + 4, 18, 3, 0xFF1A2436);
        fill(pixels, width, height, sx, sy + 4, 18 * rev_send / 100, 3,
             active ? 0xFFFFB070 : 0xFFB85A2A);
    }

    // CHORUS / REVERB open effect submenus; A/B compares the raw board mix
    // against the current mix; RESET restores the whole song.
    const int pw = std::min(304, width - 8);
    const int btn_w = 52;
    const int gap = 5;
    const int bx = px0 + (pw - (btn_w * 5 + gap * 4)) / 2;
    const int by = py0 + 170;
    draw_button(pixels, width, height, bx, by, btn_w, "CHORUS", sel_ == 24, hot, dim);
    draw_button(pixels, width, height, bx + btn_w + gap, by, btn_w, "REVERB",
                sel_ == 25, hot, dim);
    draw_button(pixels, width, height, bx + 2 * (btn_w + gap), by, btn_w, "A/B",
                sel_ == 26, settings_.ab_original ? 0xFFFF9A50 : hot, dim);
    draw_button(pixels, width, height, bx + 3 * (btn_w + gap), by, btn_w, "RESET",
                sel_ == 27, hot, dim);
    draw_button(pixels, width, height, bx + 4 * (btn_w + gap), by, btn_w, "SFX",
                sel_ == 28, hot, dim);
}

void Menu::draw_sfx(uint32_t *pixels, int width, int height, int px0,
                    int py0) const {
    const uint32_t dim = 0xFF8896AA;
    const uint32_t hot = 0xFFFFD040;
    const uint32_t accent_base = 0xFFFF9A55;
    const int col_w = 48;
    const int start_x = px0 + 12;
    const int row_h = 30;
    const int fh = 12;
    const bool stock_mode = (!mixer_mode_ && settings_.audio_preset == 0);

    std::string status = "SFX CHANNELS";
    uint32_t status_color = hot;
    if (sel_ >= 0 && sel_ < Settings::kSfxChannels) {
        const int i = sel_;
        const bool solo =
            !stock_mode && (settings_.solo_mask & (1u << (24 + i))) != 0;
        status = sfx_channel_name(i) + "  VOL " +
                 std::to_string(stock_mode ? 100 : settings_.sfx_lane_volume[i]);
        if (solo) {
            status += "  SOLO";
            status_color = 0xFFFF5A9A;
        }
    }
    draw_small_text(pixels, width, height, px0 + 12, py0 + 44, status,
                    status_color);
    for (int i = 0; i < Settings::kSfxChannels; ++i) {
        const int row = i / 6;
        const int col = i % 6;
        const int cx = start_x + col * col_w;
        const int top = py0 + 58 + row * row_h;
        const int fader_x = cx + 12;
        const int vu_x = cx + 22;
        const int fy = top + 8;
        const bool active = (sel_ == i);
        const int volume = stock_mode ? 100 : settings_.sfx_lane_volume[i];
        const float level = sfx_level_[i];
        const std::string label = sfx_channel_name(i);
        const bool solo =
            !stock_mode && (settings_.solo_mask & (1u << (24 + i))) != 0;
        const uint32_t accent = solo ? 0xFFFF5A9A : (active ? hot : accent_base);

        draw_small_text(pixels, width, height,
                        cx + (col_w - small_text_width(label)) / 2, top, label,
                        solo ? 0xFFFF5A9A : (active ? hot : dim));
        fill(pixels, width, height, fader_x, fy, 5, fh, 0xFF1A2436);
        const int filled = fh * volume / Settings::kChannelMax;
        fill(pixels, width, height, fader_x, fy + fh - filled, 5, filled, accent);
        const int knob_y = std::clamp(fy + fh - filled - 2, fy - 2, fy + fh - 3);
        fill(pixels, width, height, fader_x - 3, knob_y, 11, 4,
             active ? 0xFFFFFFFF : 0xFFB8C4D4);

        fill(pixels, width, height, vu_x, fy, 6, fh, 0xFF101418);
        const int vu_fill = std::clamp((int)(fh * level), 0, fh);
        fill(pixels, width, height, vu_x, fy + fh - vu_fill, 6, vu_fill,
             0xFF5BE33A);
    }

    const int pw = std::min(304, width - 8);
    draw_button(pixels, width, height, px0 + (pw - 58) / 2, py0 + 170, 58,
                "BACK", sel_ == Settings::kSfxChannels, hot, dim);
}

void Menu::draw_chorus(uint32_t *pixels, int width, int height, int px0,
                       int py0) const {
    const uint32_t dim = 0xFF8896AA;
    const uint32_t hot = 0xFFFFD040;
    const uint32_t cyan = 0xFF6FD3FF;
    const char *const labels[6] = {"CHORUS", "RATE", "DEPTH", "DELAY", "FEEDBACK",
                                   "RETURN"};

    draw_small_text(pixels, width, height, px0 + 12, py0 + 44, "CHORUS EFFECTS", hot);

    const int pw = std::min(304, width - 8);
    const int label_x = px0 + 16;
    const int bar_x = px0 + 112;
    const int bar_w = 108;
    const int value_x = px0 + pw - 16;
    const int row_y = py0 + 60;
    const int row_h = 16;

    for (int c = 0; c < 6; ++c) {
        const int y = row_y + c * row_h;
        const bool active = (sel_ == c);
        int norm = 0;
        std::string value;
        if (c == 0) {
            norm = settings_.chorus_enabled ? 100 : 0;
            value = kOnOff[settings_.chorus_enabled ? 1 : 0];
        } else if (c == 1) {
            norm = (settings_.chorus_rate - 1) * 100 / 49;
            value = fmt_rate(settings_.chorus_rate);
        } else if (c == 2) {
            norm = settings_.chorus_depth;
            value = fmt("%d%%", settings_.chorus_depth);
        } else if (c == 3) {
            norm = (settings_.chorus_delay - 5) * 100 / 25;
            value = fmt("%d MS", settings_.chorus_delay);
        } else if (c == 4) {
            norm = settings_.chorus_feedback * 100 / 90;
            value = fmt("%d%%", settings_.chorus_feedback);
        } else {
            norm = settings_.chorus_mix;
            value = fmt("%d%%", settings_.chorus_mix);
        }
        draw_small_text(pixels, width, height, label_x, y, labels[c],
                        active ? hot : dim);
        fill(pixels, width, height, bar_x, y + 1, bar_w, 5, 0xFF1A2436);
        fill(pixels, width, height, bar_x, y + 1, bar_w * norm / 100, 5,
             active ? hot : cyan);
        draw_small_text(pixels, width, height, value_x - small_text_width(value), y,
                        value, active ? hot : dim);
    }

    draw_button(pixels, width, height, px0 + (pw - 120) / 2, py0 + 170, 120, "BACK",
                sel_ == 6, hot, dim);
}

void Menu::draw_reverb(uint32_t *pixels, int width, int height, int px0,
                       int py0) const {
    const uint32_t dim = 0xFF8896AA;
    const uint32_t hot = 0xFFFFD040;
    const uint32_t cyan = 0xFFFF9A50;
    const char *const labels[5] = {"REVERB", "DECAY", "DAMPING", "PREDELAY",
                                   "RETURN"};

    draw_small_text(pixels, width, height, px0 + 12, py0 + 44, "PLATE REVERB", hot);

    const int pw = std::min(304, width - 8);
    const int label_x = px0 + 16;
    const int bar_x = px0 + 112;
    const int bar_w = 108;
    const int value_x = px0 + pw - 16;
    const int row_y = py0 + 60;
    const int row_h = 16;

    for (int c = 0; c < 5; ++c) {
        const int y = row_y + c * row_h;
        const bool active = (sel_ == c);
        int norm = 0;
        std::string value;
        if (c == 0) {
            norm = settings_.reverb_enabled ? 100 : 0;
            value = kOnOff[settings_.reverb_enabled ? 1 : 0];
        } else if (c == 1) {
            norm = settings_.reverb_decay;
            value = fmt("%d%%", settings_.reverb_decay);
        } else if (c == 2) {
            norm = settings_.reverb_damping;
            value = fmt("%d%%", settings_.reverb_damping);
        } else if (c == 3) {
            norm = settings_.reverb_predelay * 100 / 50;
            value = fmt("%d MS", settings_.reverb_predelay);
        } else {
            norm = settings_.reverb_mix;
            value = fmt("%d%%", settings_.reverb_mix);
        }
        draw_small_text(pixels, width, height, label_x, y, labels[c],
                        active ? hot : dim);
        fill(pixels, width, height, bar_x, y + 1, bar_w, 5, 0xFF1A2436);
        fill(pixels, width, height, bar_x, y + 1, bar_w * norm / 100, 5,
             active ? hot : cyan);
        draw_small_text(pixels, width, height, value_x - small_text_width(value), y,
                        value, active ? hot : dim);
    }

    draw_button(pixels, width, height, px0 + (pw - 120) / 2, py0 + 170, 120, "BACK",
                sel_ == 5, hot, dim);
}

void Menu::draw_controls(uint32_t *pixels, int width, int height, int px0,
                         int py0) const {
    const uint32_t dim = 0xFF8896AA;
    const uint32_t label_color = 0xFFDCE4EE;
    const int x_label = px0 + 10;
    const int x_keyboard = px0 + 72;
    const int x_pad = px0 + 174;
    // Leave a clear gap below the five Input rows before the controls table.
    const int y_header = py0 + 108;
    draw_small_text(pixels, width, height, x_label, y_header, "CONTROL", dim);
    draw_small_text(pixels, width, height, x_keyboard, y_header, "KEYBOARD", dim);
    draw_small_text(pixels, width, height, x_pad, y_header, "PAD", dim);

    struct ControlRow {
        const char *control;
        const char *keyboard;
        const char *pad;
    };
    static const ControlRow rows[] = {
        {"STEER", "ARROWS / A D", "STICK"},
        {"GAS", "UP / W", "RT"},
        {"BRAKE", "DOWN / S", "LT"},
        {"GEAR", "SPACE", "PAD Y"},
        {"START", "1 / ENTER", "PAD START"},
        {"COIN", "5 / C", "PAD BACK"},
    };
    for (int i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); ++i) {
        const int y = y_header + 10 + i * 10;
        draw_small_text(pixels, width, height, x_label, y, rows[i].control,
                        label_color);
        draw_small_text(pixels, width, height, x_keyboard, y, rows[i].keyboard,
                        dim);
        draw_small_text(pixels, width, height, x_pad, y, rows[i].pad, dim);
    }
}

void Menu::draw_steering(uint32_t *pixels, int width, int height, int px0,
                         int py0) const {
    const uint32_t dim = 0xFF8896AA;
    const uint32_t hot = 0xFFFFD040;
    const int pw = std::min(304, width - 8);
    const int gx = px0 + 10;
    const int gy = py0 + 44;
    const int gw = 84;
    const int gh = 56;
    const int cx = gx + gw / 2;
    const int cy = gy + gh / 2;

    fill(pixels, width, height, gx, gy, gw, gh, 0xFF0A0F16);
    fill(pixels, width, height, gx, gy, gw, 1, 0xFF4A6A9A);
    fill(pixels, width, height, gx, gy + gh - 1, gw, 1, 0xFF4A6A9A);
    fill(pixels, width, height, gx, gy, 1, gh, 0xFF4A6A9A);
    fill(pixels, width, height, gx + gw - 1, gy, 1, gh, 0xFF4A6A9A);
    // Dead-zone box and axes.
    const int dz = gw * std::clamp(settings_.steer_deadzone, 0, 40) / 100 / 2;
    if (dz > 0)
        fill(pixels, width, height, cx - dz, cy - dz, dz * 2, dz * 2,
             0xFF1A2436, 180);
    fill(pixels, width, height, cx, gy + 4, 1, gh - 8, 0xFF33465E);
    fill(pixels, width, height, gx + 4, cy, gw - 8, 1, 0xFF33465E);

    const int axis = settings_.pad_steer_axis;
    const int value = std::clamp(steer_value_, -32767, 32767);
    int nx = 0, ny = 0;
    if (axis == 1 || axis == 3)
        ny = value;
    else
        nx = value;
    int dx = cx + nx * (gw / 2 - 5) / 32767;
    int dy = cy + ny * (gh / 2 - 5) / 32767;
    fill(pixels, width, height, dx - 2, dy - 2, 5, 5, hot);

    const int text_x = px0 + 104;
    draw_small_text(pixels, width, height, text_x, py0 + 44,
                    "STEERING SETUP", hot);
    char line[64];
    std::snprintf(line, sizeof(line), "AXIS %s",
                  pad_axis_name(axis).c_str());
    draw_small_text(pixels, width, height, text_x, py0 + 56, line, dim);
    std::snprintf(line, sizeof(line), "VALUE %+d%%", value * 100 / 32767);
    draw_small_text(pixels, width, height, text_x, py0 + 68, line, dim);
    std::snprintf(line, sizeof(line), "WHEEL %d", wheel_);
    draw_small_text(pixels, width, height, text_x, py0 + 80, line, dim);

    const int bar_x = text_x;
    const int bar_y = py0 + 92;
    const int bar_w = std::min(174, pw - (text_x - px0) - 10);
    fill(pixels, width, height, bar_x, bar_y, bar_w, 7, 0xFF2A3448);
    const int wheel = std::clamp(wheel_, 0x20, 0xE0);
    const int centre_x = bar_x + bar_w / 2;
    const int wheel_x =
        bar_x + (wheel - 0x20) * (bar_w - 1) / (0xE0 - 0x20);
    const int fill_lo = std::min(centre_x, wheel_x);
    const int fill_hi = std::max(centre_x, wheel_x);
    if (fill_hi > fill_lo)
        fill(pixels, width, height, fill_lo, bar_y, fill_hi - fill_lo, 7,
             wheel < 0x80 ? 0xFF6FD3FF : hot);
    fill(pixels, width, height, centre_x, bar_y - 2, 1, 11, 0xFFDCE4EE);
    fill(pixels, width, height, wheel_x, bar_y - 2, 2, 11, hot);
    draw_small_text(pixels, width, height, px0 + 10, py0 + 104,
                    "MOVE SELECTED AXIS TO TEST", dim);
}

void Menu::draw(uint32_t *pixels, int width, int height) const {
    if (!open_ || !pixels) return;
    const std::vector<Item> list = items();
    const int sel = list.empty() ? 0 : std::clamp(sel_, 0, (int)list.size() - 1);

    const int pw = std::min(304, width - 8);
    const int ph = std::min(208, height - 8);
    const int px0 = (width - pw) / 2;
    const int py0 = (height - ph) / 2;

    fill(pixels, width, height, px0, py0, pw, ph, 0x000E1626, 224);
    fill(pixels, width, height, px0, py0, pw, 2, 0xFF4A6A9A);
    fill(pixels, width, height, px0, py0 + ph - 2, pw, 2, 0xFF4A6A9A);
    fill(pixels, width, height, px0, py0, 2, ph, 0xFF4A6A9A);
    fill(pixels, width, height, px0 + pw - 2, py0, 2, ph, 0xFF4A6A9A);

    constexpr uint32_t kTitleColor = 0xFFFFE070;
    constexpr uint32_t kTabColor = 0xFF6FD3FF;
    constexpr uint32_t kActiveTabColor = 0xFFFF5A5A;
    draw_text(pixels, width, height, px0 + 8, py0 + 8, "Like The Wind",
              kTitleColor);
    fill(pixels, width, height, px0 + 8, py0 + 17,
         text_width("Like The Wind"), 1, kTitleColor);
    const char *credit = "HOJOWORKS 2026";
    draw_small_text(pixels, width, height,
                    px0 + pw - 8 - small_text_width(credit), py0 + 9, credit,
                    0xFF8896AA);

    if (mixer_mode_) {
        const char *labels[2] = {"SOUND TEST", "MIXER"};
        const int pages[2] = {3, 4};
        constexpr int gap = 28;
        const int widths[2] = {small_text_width(labels[0]),
                               small_text_width(labels[1])};
        int tx = px0 + (pw - (widths[0] + widths[1] + gap)) / 2;
        for (int j = 0; j < 2; ++j) {
            const uint32_t col = (tab_ == pages[j]) ? kActiveTabColor : kTabColor;
            draw_small_text(pixels, width, height, tx, py0 + 27, labels[j], col);
            if (tab_ == pages[j])
                fill(pixels, width, height, tx, py0 + 33, widths[j], 1, col);
            tx += widths[j] + gap;
        }
    } else {
        constexpr int gap = 12;
        int widths[kTabCount];
        int total = gap * (kTabCount - 1);
        for (int i = 0; i < kTabCount; ++i) {
            widths[i] = small_text_width(kTabs[i]);
            total += widths[i];
        }
        int tx = px0 + (pw - total) / 2;
        for (int i = 0; i < kTabCount; i++) {
            const uint32_t col = (i == tab_) ? kActiveTabColor : kTabColor;
            draw_small_text(pixels, width, height, tx, py0 + 27, kTabs[i], col);
            if (i == tab_)
                fill(pixels, width, height, tx, py0 + 33, widths[i], 1, col);
            tx += widths[i] + gap;
        }
    }

    if (tab_ == 4) {
        if (mixer_page_ == 1)
            draw_chorus(pixels, width, height, px0, py0);
        else if (mixer_page_ == 2)
            draw_reverb(pixels, width, height, px0, py0);
        else if (mixer_page_ == 3)
            draw_sfx(pixels, width, height, px0, py0);
        else
            draw_mixer(pixels, width, height, px0, py0);
    } else {
        const bool sound_page = (tab_ == 3 && audio_page_ == 1);
        const bool bindings_page = (tab_ == 0 && input_page_ == 1);
        const bool steering_page = (tab_ == 0 && input_page_ == 2);
        if (sound_page)
            draw_small_text(pixels, width, height, px0 + 10, py0 + 44,
                            mixer_mode_ ? "SELECT SONG" : "SOUND TEST", 0xFFFFD040);
        if (bindings_page) {
            std::string prompt = "CONTROLLER BINDINGS";
            if (binding_ >= 0) {
                prompt = binding_waiting_ ? "RELEASE INPUTS"
                                          : "PRESS A CONTROLLER BUTTON";
                if (!binding_waiting_ && sel >= 0 && sel < (int)list.size())
                    prompt = "BIND " + list[sel].label +
                             (binding_axis() ? "  MOVE STICK/TRIGGER"
                                             : "  PRESS BUTTON");
            }
            draw_small_text(pixels, width, height, px0 + 10, py0 + 44, prompt,
                            binding_ >= 0 ? 0xFFFF5A5A : 0xFFFFD040);
        }
        if (steering_page) draw_steering(pixels, width, height, px0, py0);
        const int row_y =
            py0 + (steering_page ? 116 : (sound_page || bindings_page) ? 58 : 48);
        const int row_h = 10;
        const int label_x = px0 + 10;
        const int value_x = px0 + pw - 10;
        for (int i = 0; i < (int)list.size(); i++) {
            const int y = row_y + i * row_h;
            if (y + 6 > py0 + ph - 16) break;
            const Item &it = list[i];
            const bool active = (i == sel);
            const uint32_t col = (it.kind == Kind::Info) ? 0xFF8896AA : (active ? 0xFFFFD040 : 0xFFDCE4EE);
            if (active && it.kind != Kind::Info)
                draw_small_text(pixels, width, height, px0 + 3, y, ">", 0xFFFFD040);
            draw_small_text(pixels, width, height, label_x, y, it.label, col);
            if (!it.value.empty()) {
                const int tw = small_text_width(it.value);
                if (it.amount >= 0) {
                    const int bar_w = 44;
                    const int bar_h = 5;
                    // Keep the bar in a fixed column so a 2-digit value does
                    // not move it left relative to a 3-digit value.
                    const int value_w =
                        it.value.find('/') != std::string::npos ? 36 : 24;
                    const int bx = value_x - value_w - bar_w - 6;
                    fill(pixels, width, height, bx, y, bar_w, bar_h, 0xFF2A3448);
                    fill(pixels, width, height, bx, y,
                         bar_w * std::clamp(it.amount, 0, Settings::kBusMax) /
                             Settings::kBusMax,
                         bar_h, active ? 0xFFFFD040 : 0xFF6FD3FF);
                }
                const int vx = value_x - tw;
                draw_small_text(pixels, width, height, vx, y, it.value, col);
            }
        }
        if (tab_ == 0 && input_page_ == 0)
            draw_controls(pixels, width, height, px0, py0);
    }

    if (tab_ == 4 && mixer_locked_) {
        fill(pixels, width, height, px0 + 8, py0 + 40, pw - 16, 11,
             0x00000000, 220);
        draw_small_text(pixels, width, height, px0 + 12, py0 + 43,
                        "MIXER LOCKED - PRESS ENTER TO ENABLE", 0xFFFFD040);
    }

    const char *footer = "UP/DN SEL  L/R CHG  Q/E TAB  ENTER OK  TAB EXIT";
    if (tab_ == 3 && audio_page_ == 1) {
        footer = mixer_mode_ ? "UP/DN SEL ENTER PLAY Q/E MIXER"
                             : "UP/DN SEL  ENTER PLAY  BACK STOP";
    } else if (tab_ == 0 && input_page_ == 1) {
        footer = binding_ >= 0
                     ? (binding_axis() ? "MOVE STICK OR TRIGGER  BACKSPACE CANCEL"
                                       : "PRESS A CONTROLLER BUTTON  BACKSPACE CANCEL")
                     : "UP/DN SEL  ENTER BIND  BACKSPACE BACK";
    } else if (tab_ == 0 && input_page_ == 2) {
        footer = "UP/DN SEL  L/R CHG  BACKSPACE BACK";
    } else if (tab_ == 4) {
        if (mixer_locked_) {
            footer = "ENTER ENABLE MIXER  BACKSPACE BACK";
        } else if (mixer_page_ == 3) {
            footer = "L/R SEL UP/DN VOL S SOLO BACKSPACE BACK";
        } else if (mixer_page_ == 1 || mixer_page_ == 2) {
            footer = "UP/DN SEL L/R CHG ENTER TGL BACKSPACE BACK";
        } else if (send_focus_ == 0) {
            footer = "L/R SEL UP/DN VOL +/- CHORUS S SOLO R REVERB";
        } else {
            footer = "L/R SEL UP/DN VOL +/- REVERB S SOLO R CHORUS";
        }
    }
    std::string footer_text = footer;
    if (mixer_mode_ && !mix_track_.empty())
        footer_text = mix_track_ + "  " + footer_text;
    draw_small_text(pixels, width, height, px0 + 8, py0 + ph - 12, footer_text,
                    0xFF8896AA);
}

} // namespace likethewind
