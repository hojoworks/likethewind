/*
 * likethewind/palette.h — Y Board palette word to host colour.
 *
 * Each palette word is two bits of highlight plus 5-bit red, green and blue
 * components. The board's normal and highlight palettes share this layout.
 */
#ifndef LIKETHEWIND_PALETTE_H
#define LIKETHEWIND_PALETTE_H

#include <cstdint>

namespace likethewind {

static inline uint32_t palette_rgb(uint16_t v) {
    const uint32_t r = (((v & 0x0F) << 1) | ((v >> 12) & 1)) * 255 / 31;
    const uint32_t g = ((((v >> 4) & 0x0F) << 1) | ((v >> 13) & 1)) * 255 / 31;
    const uint32_t b = ((((v >> 8) & 0x0F) << 1) | ((v >> 14) & 1)) * 255 / 31;
    return (r << 16) | (g << 8) | b;
}

static inline uint32_t palette_argb(uint16_t v) {
    return 0xFF000000u | palette_rgb(v);
}

} // namespace likethewind

#endif // LIKETHEWIND_PALETTE_H
