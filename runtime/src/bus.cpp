/*
 * bus.cpp — the active CPU's slow-path bus and the page-table swap.
 */
#include "likethewind/machine.h"
#include "likethewind/bus.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace likethewind {
Machine *g_machine = nullptr;
int g_active_cpu = -1;
bool bsprite_tap = false;
bool ff0e_tap = false;
}

extern "C" {
uint8_t **ng_rmap = nullptr;
uint8_t **ng_wmap = nullptr;
}

using likethewind::g_active_cpu;
using likethewind::g_machine;

extern "C" {
uint8_t ng_r8_slow(uint32_t a) { return g_machine->read8(g_active_cpu, a); }
uint16_t ng_r16_slow(uint32_t a) { return g_machine->read16(g_active_cpu, a); }
void ng_w8_slow(uint32_t a, uint8_t v) { g_machine->write8(g_active_cpu, a, v); }
void ng_w16_slow(uint32_t a, uint16_t v) { g_machine->write16(g_active_cpu, a, v); }
}

extern "C" void ng_bad_dispatch(uint32_t pc) {
    std::fprintf(stderr, "fatal: recompiled routine entered at $%06X, which it does not contain\n", pc);
    std::abort();
}

namespace likethewind {

static inline uint32_t mask21(uint32_t a) { return a & 0x1FFFFFu; }

uint8_t Machine::read8(int cpu, uint32_t a) {
    a = mask21(a);
    if (ff0e_tap && a == 0x0CFF0E) {
        std::fprintf(stderr, "[ff0e] f%lld line%lld r c%d pc %06X = %02X\n",
                     (long long)frame, (long long)line, cpu, ng_cpu.pc,
                     shared[0xFF0E]);
    }
    // The multiplier/divider words answer byte reads at both byte lanes of
    // the bus: even byte address -> high byte, odd -> low byte.
    if (a >= 0x080000 && a < 0x082000) {
        const int off = ((a - 0x080000) >> 1) & 3;
        const uint16_t *r = mult_regs[cpu];
        uint16_t w;
        if (off < 2) w = r[off];
        else {
            const uint32_t product =
                (uint32_t)((int32_t)(int16_t)r[0] * (int32_t)(int16_t)r[1]);
            w = (off == 2) ? (uint16_t)(product >> 16) : (uint16_t)product;
        }
        return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    }
    if (a >= 0x084000 && a < 0x086000) {
        const int off = ((a - 0x084000) >> 1) & 7;
        const uint16_t w = (off <= 6) ? div_regs[cpu][off] : 0xFFFF;
        return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
    }
    switch (cpu) {
    case MAIN:
        if (a >= 0x100000 && a < 0x100020) return read_io(a);
        if (a >= 0x100040 && a < 0x100048) {
            // MSM6253: each read returns the MSB of the shift register on D7.
            uint8_t bit = (adc_shift & 0x80) ? 0x80 : 0x00;
            adc_shift <<= 1;
            return bit;
        }
        if (a == 0x082001) return sound_latch;
        if (a >= 0x0C0000 && a < 0x0D0000) return shared[a - 0x0C0000];
        if (a >= 0x1F0000 && a < 0x200000) return main_local[a - 0x1F0000];
        if (a < rom_main_.size()) return rom_main_[a];
        break;
    case SUBX:
        if (a >= 0x0C0000 && a < 0x0D0000) return shared[a - 0x0C0000];
        if (a >= 0x180000 && a < 0x190000) return ysprite[(a - 0x180000) & 0xFFFF];
        if (a >= 0x1F8000 && a < 0x1FC000) return subx_local[a - 0x1F8000];
        if (a >= 0x1FC000 && a < 0x200000) return subx_backup[a - 0x1FC000];
        if (a < rom_subx_.size()) return rom_subx_[a];
        break;
    case SUBY:
        if (a >= 0x0C0000 && a < 0x0D0000) return shared[a - 0x0C0000];
        if (a >= 0x180000 && a < 0x180800) return rotation[(a - 0x180000) & 0x7FF];
        if (a >= 0x188000 && a < 0x189000) return bsprite[(a - 0x188000) & 0xFFF];
        if (a >= 0x190000 && a < 0x194000) {
            uint16_t w = palette[((a - 0x190000) >> 1) & 0x1FFF];
            return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
        }
        if (a >= 0x198000 && a < 0x19A000) {
            // Reading the control word swaps the two 1 KB halves of rotation RAM
            // and returns FFFF.
            for (int i = 0; i < 0x400; i++) std::swap(rotation[i], rotation[0x400 + i]);
            rotation_swapped = !rotation_swapped;
            return 0xFF;
        }
        if (a >= 0x1F0000 && a < 0x200000) return suby_local[a - 0x1F0000];
        if (a < rom_suby_.size()) return rom_suby_[a];
        break;
    default:
        break;
    }
    if (bsprite_tap && cpu == SUBY) {
        static int unmapped = 0;
        if (unmapped++ < 200)
            std::fprintf(stderr, "[rd?] f%lld line%lld pc %06X r %06X\n",
                         (long long)frame, (long long)line, ng_cpu.pc, a);
    }
    return 0xFF;
}

static void divide_315_5249(uint16_t *r, int mode) {
    // Following MAME's sega_315_5249_divider_device::execute().
    r[6] = 0;
    if (mode == 0) {
        const int32_t dividend = (int32_t)(((uint32_t)r[0] << 16) | r[1]);
        const int32_t divisor = (int16_t)r[2];
        int32_t quotient;
        if (divisor == 0) {
            quotient = dividend;
            r[6] |= 0x4000;
        } else {
            quotient = dividend / divisor;
        }
        if (quotient < -32768) {
            quotient = -32768;
            r[6] |= 0x8000;
        } else if (quotient > 32767) {
            quotient = 32767;
            r[6] |= 0x8000;
        }
        r[4] = (uint16_t)(int16_t)quotient;
        r[5] = (uint16_t)(int16_t)(dividend - quotient * divisor);
    } else {
        const uint32_t dividend = ((uint32_t)r[0] << 16) | r[1];
        const uint32_t divisor = r[2];
        uint32_t quotient;
        if (divisor == 0) {
            quotient = dividend;
            r[6] |= 0x4000;
        } else {
            quotient = dividend / divisor;
        }
        r[4] = (uint16_t)(quotient >> 16);
        r[5] = (uint16_t)quotient;
    }
}

uint16_t Machine::read16(int cpu, uint32_t a) {
    a = mask21(a);
    // Palette and device words are more natural as words.
    if (cpu == SUBY && a >= 0x190000 && a < 0x194000)
        return palette[((a - 0x190000) >> 1) & 0x1FFF];
    if (a >= 0x080000 && a < 0x082000) {
        const int off = ((a - 0x080000) >> 1) & 3;
        const uint16_t *r = mult_regs[cpu];
        if (off < 2) return r[off];
        const uint32_t product =
            (uint32_t)((int32_t)(int16_t)r[0] * (int32_t)(int16_t)r[1]);
        return (off == 2) ? (uint16_t)(product >> 16) : (uint16_t)product;
    }
    if (a >= 0x084000 && a < 0x086000) {
        const int off = ((a - 0x084000) >> 1) & 7;
        return (off <= 6) ? div_regs[cpu][off] : 0xFFFF;
    }
    return (uint16_t)((read8(cpu, a) << 8) | read8(cpu, a + 1));
}

void Machine::write8(int cpu, uint32_t a, uint8_t v) {
    a = mask21(a);
    if (ff0e_tap && a == 0x0CFF0E) {
        std::fprintf(stderr, "[ff0e] f%lld line%lld w c%d pc %06X: %02X -> %02X\n",
                     (long long)frame, (long long)line, cpu, ng_cpu.pc,
                     shared[0xFF0E], v);
    }
    // PUSH START gate: the main task requests a credit start by setting bit 3
    // of 0xCEFF0 (0x37760) while the game is in the attract mode (0xCE09C==2).
    // Hold that request until the host has seen a start press released.
    if (cpu == MAIN && start_gate && !start_gate_armed && a == 0x0CEFF0 && (v & 0x08)) {
        const int mode = (shared[0xE09C] << 8) | shared[0xE09D];
        if (mode < 8) v &= (uint8_t)~0x08;
    }
    switch (cpu) {
    case MAIN:
        if (a >= 0x100000 && a < 0x100020) { write_io(a, v); return; }
        if (a >= 0x100040 && a < 0x100048) {
            // MSM6253: a write selects a channel and starts the conversion;
            // each read shifts one bit out on D7. Channels 0-2 are the analog
            // stick inputs (not fitted on Power Drift, they read 0x80).
            // Channel 3 is the HC4052 mux selected by port E bits 1:0:
            // 0 brake, 1 gas pedal, 2 steering wheel, 3 unused.
            const unsigned sel = (a >> 1) & 3;
            adc_shift = (sel == 3) ? analog_mux_value(io_latch[4] & 3) : 0x80;
            return;
        }
        if (a == 0x082001) { sound_write(v); return; }
        if (a >= 0x0C0000 && a < 0x0D0000) { shared[a - 0x0C0000] = v; return; }
        if (a >= 0x1F0000 && a < 0x200000) { main_local[a - 0x1F0000] = v; return; }
        if (a < rom_main_.size()) return;          // ROM writes are ignored
        break;
    case SUBX:
        if (a >= 0x0C0000 && a < 0x0D0000) { shared[a - 0x0C0000] = v; return; }
        if (a >= 0x180000 && a < 0x190000) { ysprite[(a - 0x180000) & 0xFFFF] = v; return; }
        if (a >= 0x1F8000 && a < 0x1FC000) { subx_local[a - 0x1F8000] = v; return; }
        if (a >= 0x1FC000 && a < 0x200000) { subx_backup[a - 0x1FC000] = v; return; }
        if (a < rom_subx_.size()) return;
        break;
    case SUBY:
        if (a >= 0x0C0000 && a < 0x0D0000) { shared[a - 0x0C0000] = v; return; }
        if (a >= 0x180000 && a < 0x180800) { rotation[(a - 0x180000) & 0x7FF] = v; return; }
        if (a >= 0x188000 && a < 0x189000) { bsprite[(a - 0x188000) & 0xFFF] = v; return; }
        if (a >= 0x190000 && a < 0x194000) {
            uint16_t &w = palette[((a - 0x190000) >> 1) & 0x1FFF];
            if (a & 1) w = (uint16_t)((w & 0xFF00) | v);
            else w = (uint16_t)((w & 0x00FF) | (v << 8));
            return;
        }
        if (a >= 0x198000 && a < 0x19A000) return;
        if (a >= 0x1F0000 && a < 0x200000) { suby_local[a - 0x1F0000] = v; return; }
        if (a < rom_suby_.size()) return;
        break;
    default:
        break;
    }
}

void Machine::write16(int cpu, uint32_t a, uint16_t v) {
    a = mask21(a);
    if (cpu == SUBY && a >= 0x190000 && a < 0x194000) {
        palette[((a - 0x190000) >> 1) & 0x1FFF] = v;
        return;
    }
    if (bsprite_tap && cpu == SUBY && a >= 0x188000 && a < 0x190000) {
        std::fprintf(stderr, "[bsp] f%lld line%lld pc %06X w %06X = %04X\n",
                     (long long)frame, (long long)line, ng_cpu.pc, a, v);
    }
    if (bsprite_tap && cpu == SUBY && a >= 0x1F8600 && a < 0x1F8D00) {
        std::fprintf(stderr, "[stg] f%lld line%lld pc %06X w %06X = %04X\n",
                     (long long)frame, (long long)line, ng_cpu.pc, a, v);
    }
    if (a >= 0x080000 && a < 0x082000) {
        const int off = ((a - 0x080000) >> 1) & 1;
        mult_regs[cpu][off] = v;
        return;
    }
    if (a >= 0x084000 && a < 0x086000) {
        const int off = ((a - 0x084000) >> 1) & 0xF;
        switch (off & 3) {
        case 0: div_regs[cpu][0] = v; break;
        case 1: div_regs[cpu][1] = v; break;
        case 2: div_regs[cpu][2] = v; break;
        default: break;
        }
        // A4 high triggers the divide; A3 selects signed (0) or unsigned (1).
        if (off & 8) divide_315_5249(div_regs[cpu], off & 4);
        return;
    }
    write8(cpu, a, (uint8_t)(v >> 8));
    write8(cpu, a + 1, (uint8_t)v);
}

uint8_t Machine::analog_mux_value(unsigned sel) const {
    // HC4052 channel selected by 315-5296 port E bits 1:0. Power Drift wires
    // brake to mux 0, gas to mux 1 and the wheel to mux 2; mux 3 is unused.
    switch (sel & 3) {
    case 0: return in_brake;
    case 1: return in_gas;
    case 2: return in_wheel;
    default: return 0x80;
    }
}

uint8_t Machine::read_io(uint32_t a) {
    unsigned reg = (a >> 1) & 0x1F;
    if (reg < 8) {
        // Ports A-H. A read of an output port returns its latch.
        bool output = (io_direction >> reg) & 1;
        if (output) return io_latch[reg];
        switch (reg) {
        case 0: return in_wheel;
        case 1: return (uint8_t)(in_general & 0xFF);
        case 2: return in_limit;
        case 3: return 0x00;
        case 4: return 0x80;
        case 5: return dip_b;
        case 6: return dip_a;
        case 7: return 0x00;
        }
    }
    if (reg >= 8 && reg <= 0xB) return "SEGA"[reg - 8];
    if (reg == 0xD || reg == 0xF) return io_direction;
    return 0xFF;
}

void Machine::write_io(uint32_t a, uint8_t v) {
    unsigned reg = (a >> 1) & 0x1F;
    if (reg < 8) {
        io_latch[reg] = v;
        if (reg == 4) {
            // Port E: display enable, watchdog and the sub-CPU reset lines.
            // D7 /KILL (1 = display on), D5 /WDCL, D4 /SRES (Z80),
            // D3 XRES, D2 YRES (1 = held in reset), D1-D0 = ADC mux select.
            display_enable = (v & 0x80) != 0;
            set_reset(SUBX, (v & 0x08) != 0);
            set_reset(SUBY, (v & 0x04) != 0);
        }
        return;
    }
    if (reg == 0xD || reg == 0xF) io_direction = v;
}

} // namespace likethewind
