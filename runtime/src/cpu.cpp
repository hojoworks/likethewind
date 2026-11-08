/*
 * cpu.cpp — the 68000 register file and the two operations recompiled code
 * cannot do inline: SR writes (which swap stacks) and exception entry.
 *
 * Adapted from NeoGeoRecomp's cpu.c (MIT, Copyright (c) 2026 Ned Heller).
 */
#include "likethewind/cpu.h"
#include "likethewind/bus.h"

extern "C" {
ng_cpu_t ng_cpu;
int64_t  ng_cycles;
int64_t  ng_next_event;
}

extern "C" void ng_set_sr(uint16_t sr) {
    uint8_t s = (sr >> 13) & 1;
    if (s != CPU.s) {
        uint32_t t = CPU.a[7]; CPU.a[7] = CPU.osp; CPU.osp = t;
        CPU.s = s;
    }
    CPU.t = (sr >> 15) & 1;
    CPU.ipl = (sr >> 8) & 7;
    ng_set_ccr(sr);
}

extern "C" void ng_exception(int vector, uint32_t ret_pc) {
    uint16_t old = ng_get_sr();
    ng_set_sr((uint16_t)((old | 0x2000) & ~0x8000));
    ng_push32(ret_pc);
    ng_push16(old);
    CPU.pc = ng_r32((uint32_t)vector * 4);
    ng_cycles += 34;
}
