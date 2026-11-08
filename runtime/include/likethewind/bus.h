/*
 * likethewind/bus.h — the 68000 address space, as seen by recompiled code.
 *
 * The page table is three tables (main/subX/subY) and `ng_rmap`/`ng_wmap`
 * point at the active one. Pages that hold plain memory are direct host
 * pointers; device pages are NULL and take the slow path in bus.cpp.
 *
 * All guest memory is big-endian, as the 68000 sees it.
 */
#ifndef LIKETHEWIND_BUS_H
#define LIKETHEWIND_BUS_H

#include <stdint.h>
#include <stddef.h>

#include "likethewind/cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The active CPU's tables. The scheduler swaps these with the CPU context. */
extern uint8_t **ng_rmap;
extern uint8_t **ng_wmap;

uint8_t  ng_r8_slow(uint32_t a);
uint16_t ng_r16_slow(uint32_t a);
void     ng_w8_slow(uint32_t a, uint8_t v);
void     ng_w16_slow(uint32_t a, uint16_t v);

static inline uint8_t ng_r8(uint32_t a) {
    a &= 0x1FFFFF;
    uint8_t *p = ng_rmap[a >> 16];
    return p ? p[a & 0xFFFF] : ng_r8_slow(a);
}
static inline uint16_t ng_r16(uint32_t a) {
    a &= 0x1FFFFE;
    uint8_t *p = ng_rmap[a >> 16];
    if (p) {
        p += a & 0xFFFF;
        return (uint16_t)((p[0] << 8) | p[1]);
    }
    return ng_r16_slow(a);
}
static inline uint32_t ng_r32(uint32_t a) {
    return ((uint32_t)ng_r16(a) << 16) | ng_r16(a + 2);
}
static inline void ng_w8(uint32_t a, uint8_t v) {
    a &= 0x1FFFFF;
    uint8_t *p = ng_wmap[a >> 16];
    if (p) p[a & 0xFFFF] = v; else ng_w8_slow(a, v);
}
static inline void ng_w16(uint32_t a, uint16_t v) {
    a &= 0x1FFFFE;
    uint8_t *p = ng_wmap[a >> 16];
    if (p) {
        p += a & 0xFFFF;
        p[0] = (uint8_t)(v >> 8);
        p[1] = (uint8_t)v;
    } else {
        ng_w16_slow(a, v);
    }
}
static inline void ng_w32(uint32_t a, uint32_t v) {
    ng_w16(a, (uint16_t)(v >> 16));
    ng_w16(a + 2, (uint16_t)v);
}

/* The emulated stack. */
static inline void ng_push16(uint16_t v) { CPU.a[7] -= 2; ng_w16(CPU.a[7], v); }
static inline void ng_push32(uint32_t v) { CPU.a[7] -= 4; ng_w32(CPU.a[7], v); }
static inline uint16_t ng_pop16(void) { uint16_t v = ng_r16(CPU.a[7]); CPU.a[7] += 2; return v; }
static inline uint32_t ng_pop32(void) { uint32_t v = ng_r32(CPU.a[7]); CPU.a[7] += 4; return v; }

#ifdef __cplusplus
}
#endif

#endif
