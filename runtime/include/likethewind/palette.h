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
    const uint32_t value = v;
    const uint32_t r = (((value & 0x0F) << 1) | ((value >> 12) & 1)) * 255 / 31;
    const uint32_t g = ((((value >> 4) & 0x0F) << 1) | ((value >> 13) & 1)) * 255 / 31;
    const uint32_t b = ((((value >> 8) & 0x0F) << 1) | ((value >> 14) & 1)) * 255 / 31;
    return (r << 16) | (g << 8) | b;
}

static inline uint32_t palette_argb(uint16_t v) {
    return 0xFF000000u | palette_rgb(v);
}

} // namespace likethewind

#endif // LIKETHEWIND_PALETTE_H
