/*
 * video.cpp — the Y Board video chain.
 *
 * 315-5305 (Y sprites) -> 512x512 indexed framebuffer
 * 315-5306 (rotation)  -> 320x224 (or wider) indexed scan-out
 * 315-5196 (16B)       -> 320x224 line sprites, mixed on top
 * 315-5312 (mixer)     -> priority compare and pen-14 shadow
 *
 * The algorithms follow MAME's sega16sp.cpp / segaic16.cpp and the MiSTer
 * Y-Board core. The +27 rotation X bias and the 0x600/0xBD origins are MAME
 * calibrations.
 */
#include "likethewind/machine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace likethewind {

static inline uint16_t be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
static inline uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static inline uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

// One 64-bit sprite word: 16 four-bit pens, MSB first.
static inline uint64_t sprite_word(const uint8_t *bank, int bank_size, int word_index) {
    int byte = (word_index & 0xFFFF) * 8;   // addr is in 64-bit words
    if (byte < 0 || byte + 8 > bank_size) return 0;
    return be64(bank + byte);
}

void Machine::render_y_sprites() {
    y_fb.fill(0xFFFF);
    const uint8_t *bank0 = ysprite_rom();
    if (!bank0) return;

    bool visited[0x1000] = {};
    int next = 0;
    for (int guard = 0; guard < 0x1000; guard++) {
        const uint8_t *d = ysprite + next * 16;
        uint16_t w0 = be16(d + 0);
        if (w0 & 0x8000) break;
        if (visited[next]) break;
        visited[next] = true;

        uint16_t w1 = be16(d + 2);
        uint16_t w2 = be16(d + 4);
        uint16_t w3 = be16(d + 6);
        uint16_t w4 = be16(d + 8);
        uint16_t w5 = be16(d + 10);
        uint16_t w6 = be16(d + 12);
        uint16_t w7 = be16(d + 14);
        next = w7 & 0x0FFF;

        int hide = w0 & 0x5000;
        const uint8_t *indirect = ysprite + ((w0 & 0x07FF) << 5);
        int bank = ((w1 >> 8) & 0x10) | ((w2 >> 12) & 0x0F);
        int xpos = w1 & 0x0FFF;
        int top = w2 & 0x0FFF;
        uint16_t addr = w3;
        int height = w4;
        int ydelta = (w5 & 0x4000) ? 1 : -1;
        int flip = (~(w5 >> 13)) & 1;
        int xdelta = (w5 & 0x1000) ? 1 : -1;
        int zoom = w5 & 0x07FF;
        int colpri = (w6 << 1) & 0xFE00;
        int pitch = (int8_t)(w6 & 0xFF);

        if (hide || height == 0) continue;
        bank &= 7;
        const uint8_t *bankbase = bank0 + bank * 0x80000;
        if (zoom == 0) zoom = 1;

        int screen_x = xpos - 0x600;
        int ytarget = top + ydelta * height;
        int yacc = 0;

        for (int yy = top; yy != ytarget; yy += ydelta) {
            int sy = yy - 0x600;
            if (sy < 0 || sy >= 512) {
                // Still advance the source address/zoom accumulator.
                yacc += zoom;
                addr += pitch * (yacc >> 9);
                yacc &= 0x1FF;
                continue;
            }

            const uint8_t *clip = rotation + ((sy & ~1) * 2);
            int minx = be16(clip);
            int maxx = be16(clip + 2);
            if ((minx & 0x8000) && ydelta < 0) break;
            if ((minx & 0x4000) && ydelta > 0) break;

            if (!(minx & 0xC000)) {
                // The rotation clip table stores the same $600-based screen
                // coordinates as the sprite x/y fields. Our destination is a
                // real 512x512 buffer, so shift the clip to buffer space.
                // In widescreen the scan-out samples extra columns on both
                // sides; widen the level clip by the same source-space margin
                // so the terrain and scenery do not stop at the 4:3 edge.
                const int wide_margin =
                    widescreen ? (view_width - kVisibleWidth + 1) / 2 : 0;
                minx -= 0x600 + wide_margin;
                maxx -= 0x600 - wide_margin;
                if (minx < 0) minx = 0;
                if (maxx > 511) maxx = 511;
                uint16_t *dest = &y_fb[(size_t)sy * 512];
                int x = screen_x;
                int xacc = 0;
                if (!flip) {
                    int offs = (int)addr - 1;
                    int guard = 0;
                    while ((xdelta > 0 && x <= maxx) || (xdelta < 0 && x >= minx)) {
                        if (++guard > 4096) break;
                        uint64_t pixels = sprite_word(bankbase, 0x80000, ++offs);
                        bool done = false;
                        for (int k = 0; k < 16; k++) {
                            int pix = (int)((pixels >> (60 - 4 * k)) & 0xF);
                            int ind = (indirect + pix * 2 + 1 < ysprite + sizeof(ysprite))
                                          ? be16(indirect + pix * 2) : 0x1FF;
                            while (xacc < 0x200) {
                                if (x >= minx && x <= maxx && ind < 0x1FE && x >= 0 && x < 512)
                                    dest[x] = (uint16_t)(colpri | ind);
                                x += xdelta;
                                xacc += zoom;
                            }
                            xacc -= 0x200;
                            // 0xF terminates the row only as the last pixel of
                            // a 16-pixel group; earlier $F pixels are just
                            // transparent palette entries.
                            if (k == 15 && pix == 0x0F) { done = true; break; }
                        }
                        if (done) break;
                    }
                } else {
                    int offs = (int)addr + 1;
                    int guard = 0;
                    while ((xdelta > 0 && x <= maxx) || (xdelta < 0 && x >= minx)) {
                        if (++guard > 4096) break;
                        uint64_t pixels = sprite_word(bankbase, 0x80000, --offs);
                        bool done = false;
                        for (int k = 0; k < 16; k++) {
                            int pix = (int)((pixels >> (4 * k)) & 0xF);
                            int ind = (indirect + pix * 2 + 1 < ysprite + sizeof(ysprite))
                                          ? be16(indirect + pix * 2) : 0x1FF;
                            while (xacc < 0x200) {
                                if (x >= minx && x <= maxx && ind < 0x1FE && x >= 0 && x < 512)
                                    dest[x] = (uint16_t)(colpri | ind);
                                x += xdelta;
                                xacc += zoom;
                            }
                            xacc -= 0x200;
                            // 0xF terminates the row only as the last pixel of
                            // a 16-pixel group; earlier $F pixels are just
                            // transparent palette entries.
                            if (k == 15 && pix == 0x0F) { done = true; break; }
                        }
                        if (done) break;
                    }
                }
            }

            yacc += zoom;
            addr += pitch * (yacc >> 9);
            yacc &= 0x1FF;
        }
    }
}

void Machine::render_16b_sprites() {
    b_fb.fill(0xFFFF);
    const uint8_t *bank0 = bsprite_rom();
    if (!bank0) return;

    for (int entry = 0; entry < 256; entry++) {
        const uint8_t *d = bsprite + entry * 16;
        uint16_t w0 = be16(d + 0);
        uint16_t w1 = be16(d + 2);
        uint16_t w2 = be16(d + 4);
        if (w2 & 0x8000) break;
        uint16_t w3 = be16(d + 6);
        uint16_t w4 = be16(d + 8);
        uint16_t w5 = be16(d + 10);

        int bottom = w0 >> 8;
        int top = w0 & 0xFF;
        int xpos = w1 & 0x01FF;
        int hide = w2 & 0x4000;
        int flip = w2 & 0x0100;
        int pitch = (int8_t)(w2 & 0xFF);
        uint16_t addr = w3;
        int bank = (w4 >> 8) & 0x0F;
        int colpri = ((w4 & 0xFF) << 4) | (((w1 >> 9) & 0x0F) << 12);
        int vzoom = (w5 >> 5) & 0x1F;
        int hzoom = w5 & 0x1F;

        if (hide || top >= bottom) continue;
        // w0 stores an 8-bit top/bottom range, but b_fb has only the 224
        // visible lines. Without this clip a sprite that extends below the
        // screen writes into the Machine object that follows the buffer.
        if (top >= kVisibleLines) continue;
        const int bottom_clipped = std::min(bottom, kVisibleLines);
        bank &= 3;
        const uint8_t *bankbase = bank0 + bank * 0x20000;
        int screen_x = xpos - 0xBD;

        uint16_t vacc = 0;
        for (int y = top; y < bottom_clipped; y++) {
            addr += pitch;
            vacc += (uint16_t)(vzoom << 10);
            if (vacc & 0x8000) {
                addr += pitch;
                vacc &= 0x7FFF;
            }

            int x = screen_x;
            int xacc = 4 * hzoom;
            uint16_t *dest = &b_fb[(size_t)y * kRenderWidth];
            int word = addr;
            if (!flip) {
                for (int guard = 0; guard < 512; guard++) {
                    uint16_t pixels = 0;
                    int at = (word & 0xFFFF) * 2;
                    if (at + 1 < 0x20000) pixels = be16(bankbase + at);
                    for (int k = 0; k < 4; k++) {
                        int pix = (pixels >> (12 - 4 * k)) & 0xF;
                        xacc = (xacc & 0x3F) + hzoom;
                        if (xacc < 0x40) {
                            if (pix != 0 && pix != 15 && x >= 0 && x < kRenderWidth)
                                dest[x] = (uint16_t)(colpri | pix);
                            x++;
                        }
                        if (k == 3 && pix == 15) goto next_row;
                    }
                    word++;
                }
            } else {
                for (int guard = 0; guard < 512; guard++) {
                    uint16_t pixels = 0;
                    int at = (word & 0xFFFF) * 2;
                    if (at + 1 < 0x20000) pixels = be16(bankbase + at);
                    for (int k = 0; k < 4; k++) {
                        int pix = (pixels >> (4 * k)) & 0xF;
                        xacc = (xacc & 0x3F) + hzoom;
                        if (xacc < 0x40) {
                            if (pix != 0 && pix != 15 && x >= 0 && x < kRenderWidth)
                                dest[x] = (uint16_t)(colpri | pix);
                            x++;
                        }
                        if (k == 3 && pix == 15) goto next_row;
                    }
                    word--;
                }
            }
        next_row:;
        }
    }
}

void Machine::render_scanline(int y) {
    const uint8_t *r = rotation;
    int32_t currx = (int32_t)be32(r + 0x3F0 * 2);
    int32_t curry = (int32_t)be32(r + 0x3F2 * 2);
    int32_t dyy = (int32_t)be32(r + 0x3F4 * 2);
    int32_t dxx = (int32_t)be32(r + 0x3F6 * 2);
    int32_t dxy = (int32_t)be32(r + 0x3F8 * 2);
    int32_t dyx = (int32_t)be32(r + 0x3FA * 2);

    // The board starts at x=0. Widescreen shifts the window left symmetrically.
    const int x0 = view_x0;
    currx += dxx * (x0 + 27) + dxy * y;
    curry += dyx * (x0 + 27) + dyy * y;

    uint16_t *out = &screen[(size_t)y * kRenderWidth];

    // The rotation source is a 512x512 buffer. A wide view can extend past
    // it while the screen is tilted, so choose one source-space origin for
    // the whole frame that centers the visible rectangle. Using a per-line
    // origin makes the image shear as the rotation changes.
    const int32_t source_top_x = currx - dxy * y;
    const int32_t raw_r0 = source_top_x + dxx * (view_width - 1);
    const int32_t raw_b0 = source_top_x + dxy * (kVisibleLines - 1);
    const int32_t raw_br = raw_b0 + dxx * (view_width - 1);
    int raw_min = std::min(std::min((int)(source_top_x >> 14), (int)(raw_r0 >> 14)),
                           std::min((int)(raw_b0 >> 14), (int)(raw_br >> 14)));
    int raw_max = std::max(std::max((int)(source_top_x >> 14), (int)(raw_r0 >> 14)),
                           std::max((int)(raw_b0 >> 14), (int)(raw_br >> 14)));
    const int source_x_base = (raw_min + raw_max - 511) / 2;

    // The Y plane wraps at 512 rows on the board. Pick the copy that contains
    // the tilted view so a seam does not cut through the middle of the frame.
    const int32_t base_curry = (int32_t)be32(r + 0x3F2 * 2);
    const int32_t source_top_y = base_curry + dyx * (view_x0 + 27);
    const int32_t raw_r0y = source_top_y + dyx * (view_width - 1);
    const int32_t raw_b0y = source_top_y + dyy * (kVisibleLines - 1);
    const int32_t raw_bry = raw_r0y + dyy * (kVisibleLines - 1);
    const int raw_min_y =
        std::min(std::min((int)(source_top_y >> 14), (int)(raw_r0y >> 14)),
                 std::min((int)(raw_b0y >> 14), (int)(raw_bry >> 14)));
    const int raw_max_y =
        std::max(std::max((int)(source_top_y >> 14), (int)(raw_r0y >> 14)),
                 std::max((int)(raw_b0y >> 14), (int)(raw_bry >> 14)));
    const int centred_y = (raw_min_y + raw_max_y - 511) / 2;
    const int source_y_base =
        (int)std::lround((double)centred_y / 512.0) * 512;

    for (int x = 0; x < view_width; x++) {
        const int raw_sx = (int)(currx >> 14) - source_x_base;
        const int raw_sy = (int)(curry >> 14) - source_y_base;
        // Mirror horizontally at the source edge so the widescreen margins do
        // not become flat bands. Vertically, use only the source copy selected
        // above; wrapping a second copy into the tilted view can duplicate
        // sprites that are nowhere near the car.
        int sx;
        if (raw_sx < 0) sx = -raw_sx - 1;
        else if (raw_sx >= 512) sx = 1023 - raw_sx;
        else sx = raw_sx;
        sx = std::clamp(sx, 0, 511);
        const bool y_in_source = raw_sy >= 0 && raw_sy < 512;
        const int sy = std::clamp(raw_sy, 0, 511);
        uint16_t pix = y_in_source ? y_fb[(size_t)sy * 512 + sx] : 0xFFFF;
        uint16_t value;
        uint8_t pri;

        if (pix != 0xFFFF) {
            value = (uint16_t)((pix & 0x1FF) | ((pix >> 6) & 0x200) |
                               ((pix >> 3) & 0xC00) | 0x1000);
            pri = (uint8_t)((pix >> 8) | 1);
        } else {
            value = (uint16_t)sy;   // scanline colour (sky/ground gradient)
            pri = 0xFF;
        }

        // 315-5312: front sprites win only when their priority nibble is
        // lower than the rotated layer's priority.
        int bx = x + view_x0;
        if (bx >= 0 && bx < kVisibleWidth) {
            uint16_t sp = b_fb[(size_t)y * kRenderWidth + bx];
            if (sp != 0xFFFF) {
                int spri = (sp >> 11) & 0x1E;
                if (spri < (pri & 0x1F)) {
                    if ((sp & 0xF) == 0xE) value = (uint16_t)(value + 0x2000);
                    else value = (uint16_t)(0x800 | (sp & 0x7FF));
                }
            }
        }
        out[x] = value;
        currx += dxx;
        curry += dyx;
    }
}

void Machine::render_frame() {
    // Main asserts the display-enable latch late in its vblank work, after
    // the line-220 list sample. Render the frame regardless; the game still
    // controls blanking through the palette and scanline data.
    render_y_sprites();
    render_16b_sprites();
    for (int y = 0; y < kVisibleLines; y++) render_scanline(y);
}

} // namespace likethewind
