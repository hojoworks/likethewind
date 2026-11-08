/*
 * fallback.cpp — Musashi hand-over for addresses with no translated entry.
 *
 * The generated code is flat: it returns to the dispatcher whenever control
 * leaves a routine. If the dispatcher lands somewhere the recompiler never
 * saw (a computed jump, a runtime-generated entry), this executes one 68000
 * instruction in Musashi and returns to the dispatcher. The next address may
 * be translated; if not, the process repeats. The fallback is intentionally
 * slow: it is a bring-up oracle, not the hot path.
 */
#include "likethewind/fallback.h"
#include "likethewind/machine.h"
#include "likethewind/bus.h"

extern "C" {
#include "m68k.h"
int mus_stopped(void);
void mus_set_stopped(void);
void mus_clear_stopped(void);
}

#include <cstdio>

namespace likethewind {

static bool g_musashi_ready = false;

static void to_musashi() {
    m68k_set_reg(M68K_REG_SR, ng_get_sr());
    for (int i = 0; i < 8; i++) m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), ng_cpu.d[i]);
    for (int i = 0; i < 8; i++) m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), ng_cpu.a[i]);
    m68k_set_reg(ng_cpu.s ? M68K_REG_USP : M68K_REG_ISP, ng_cpu.osp);
    m68k_set_reg(M68K_REG_PC, ng_cpu.pc);
    if (ng_cpu.stopped) mus_set_stopped(); else mus_clear_stopped();
}

static void from_musashi() {
    uint16_t sr = (uint16_t)m68k_get_reg(nullptr, M68K_REG_SR);
    ng_cpu.s = (sr >> 13) & 1;
    ng_cpu.t = (sr >> 15) & 1;
    ng_cpu.ipl = (sr >> 8) & 7;
    ng_set_ccr(sr);
    for (int i = 0; i < 8; i++) ng_cpu.d[i] = m68k_get_reg(nullptr, (m68k_register_t)(M68K_REG_D0 + i));
    for (int i = 0; i < 8; i++) ng_cpu.a[i] = m68k_get_reg(nullptr, (m68k_register_t)(M68K_REG_A0 + i));
    ng_cpu.osp = m68k_get_reg(nullptr, ng_cpu.s ? M68K_REG_USP : M68K_REG_ISP);
    ng_cpu.pc = m68k_get_reg(nullptr, M68K_REG_PC);
    ng_cpu.stopped = (uint8_t)mus_stopped();
}

void fallback_init() {
    if (g_musashi_ready) return;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    g_musashi_ready = true;
}

bool fallback_step() {
    if (!g_musashi_ready) fallback_init();
    to_musashi();
    m68k_set_irq(0);                 // the dispatcher takes interrupts itself
    int cycles = m68k_execute(1);
    ng_cycles += cycles;
    from_musashi();
    return true;
}

} // namespace likethewind

// Musashi's memory callbacks, routed through the active CPU's bus.
extern "C" {

unsigned int m68k_read_memory_8(unsigned int a) {
    return likethewind::g_machine->read8(likethewind::g_active_cpu, a);
}
unsigned int m68k_read_memory_16(unsigned int a) {
    return likethewind::g_machine->read16(likethewind::g_active_cpu, a);
}
unsigned int m68k_read_memory_32(unsigned int a) {
    return ((unsigned int)likethewind::g_machine->read16(likethewind::g_active_cpu, a) << 16) |
           likethewind::g_machine->read16(likethewind::g_active_cpu, a + 2);
}
unsigned int m68k_read_immediate_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_immediate_32(unsigned int a) { return m68k_read_memory_32(a); }
unsigned int m68k_read_pcrelative_8(unsigned int a) { return m68k_read_memory_8(a); }
unsigned int m68k_read_pcrelative_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_pcrelative_32(unsigned int a) { return m68k_read_memory_32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) {
    likethewind::g_machine->write8(likethewind::g_active_cpu, a, (uint8_t)v);
}
void m68k_write_memory_16(unsigned int a, unsigned int v) {
    likethewind::g_machine->write16(likethewind::g_active_cpu, a, (uint16_t)v);
}
void m68k_write_memory_32(unsigned int a, unsigned int v) {
    likethewind::g_machine->write16(likethewind::g_active_cpu, a, (uint16_t)(v >> 16));
    likethewind::g_machine->write16(likethewind::g_active_cpu, a + 2, (uint16_t)v);
}

} // extern "C"
