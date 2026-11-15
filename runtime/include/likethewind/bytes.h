/*
 * likethewind/bytes.h — big-endian reads from guest memory.
 *
 * Guest memory is big-endian on the Y Board, so the runtime and the host
 * agree on these helpers instead of repeating the shifts in every file.
 */
#ifndef LIKETHEWIND_BYTES_H
#define LIKETHEWIND_BYTES_H

#include <cstdint>

namespace likethewind {

static inline uint16_t read_be16(const uint8_t *p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static inline uint32_t read_be32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

static inline uint64_t read_be64(const uint8_t *p) {
    return (static_cast<uint64_t>(read_be32(p)) << 32) | read_be32(p + 4);
}

} // namespace likethewind

#endif // LIKETHEWIND_BYTES_H
