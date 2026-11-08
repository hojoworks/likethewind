/*
 * likethewind/machine.h — the Sega Y Board model.
 *
 * One Machine owns the three 68000 register files, their page tables, all
 * board RAM/ROM and the device state. The scheduler in machine.cpp runs one
 * CPU at a time; bus.cpp routes the active CPU's accesses.
 */
#ifndef LIKETHEWIND_MACHINE_H
#define LIKETHEWIND_MACHINE_H

#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "likethewind/cpu.h"
#include "likethewind/recomp.h"

namespace likethewind {

class Audio;

enum CpuId : int { MAIN = 0, SUBX = 1, SUBY = 2, CPU_COUNT = 3 };

struct Cpu {
    ng_cpu_t state{};
    int64_t cycles = 0;
    int64_t next_event = 0;
    std::array<uint8_t *, 256> rmap{};
    std::array<uint8_t *, 256> wmap{};
    bool reset_held = false;
    uint8_t irq = 0;           // pending level (0 = none)
};

class Machine {
public:
    Machine();
    ~Machine();

    bool load_roms(const std::string &dir, std::string &error);
    bool load_selftest(const std::string &path, std::string &error);
    void set_code(int cpu, const ng_func_entry_t *table, size_t count);
    void clear_code(int cpu) { fn_[cpu].assign(fn_[cpu].size(), nullptr); }
    ng_func_t function_at(int cpu, uint32_t address) const {
        return (address / 2 < fn_[cpu].size()) ? fn_[cpu][address / 2] : nullptr;
    }
    void reset();
    void run_frame();
    void run_until(int cpu, int64_t target);
    bool start_button(bool down);

    // Bus slow path (device pages). Reads/writes outside plain memory.
    uint8_t read8(int cpu, uint32_t address);
    uint16_t read16(int cpu, uint32_t address);
    void write8(int cpu, uint32_t address, uint8_t value);
    void write16(int cpu, uint32_t address, uint16_t value);

    // Device state, exposed for the video/audio models and save states.
    uint8_t shared[0x10000]{};
    uint8_t main_local[0x10000]{};
    uint8_t subx_local[0x4000]{};
    uint8_t subx_backup[0x4000]{};
    uint8_t suby_local[0x10000]{};
    uint8_t ysprite[0x10000]{};    // 315-5305 sprite RAM
    uint8_t bsprite[0x1000]{};     // 315-5196 sprite RAM
    uint8_t rotation[0x800]{};     // 315-5306 rotation RAM (two 1 KB halves)
    uint16_t palette[0x4000]{};    // 8192 normal + 8192 shadow/highlight
    uint8_t io_latch[8]{};
    uint8_t io_direction = 0;
    uint8_t adc_shift = 0;
    uint8_t sound_latch = 0;
    uint8_t last_music_cmd = 0xFF;   // last 90H-97H track sent by the game
    bool display_enable = true;
    bool rotation_swapped = false;

    // Inputs (0..255 analogue, active-high buttons).
    // 315-5248 multiplier and 315-5249 divider, one pair per 68000.
    uint16_t mult_regs[3][2] = {};
    uint16_t div_regs[3][7] = {};

    uint8_t in_wheel = 0x80;   // centred paddle
    uint8_t in_gas = 0x00;     // released pedals, matching MAME's defaults
    uint8_t in_brake = 0x00;
    uint16_t in_general = 0x00DF;  // active-low; bit 5 = gear shift in low
    uint8_t in_limit = 0xE4;       // four active-high sensors, MAME default
    uint8_t dip_a = 0xFF;
    uint8_t dip_b = 0xEA;

    Cpu cpus[CPU_COUNT];
    uint64_t irqCount[CPU_COUNT][8]{};
    int active = MAIN;
    int64_t frame = 0;
    int64_t line = 0;
    bool verbose = false;
    int slice_cycles = 8;
    bool reverse_order = true;

    // Small black box for crash reports: the last routine/instruction entries
    // across all three CPUs. Public so the crash handler can read it.
    struct GuestTrace {
        uint32_t pc = 0;
        uint32_t cycles = 0;
        uint64_t frame = 0;
        uint16_t line = 0;
        uint8_t cpu = 0;
    };
    static constexpr int kGuestTrace = 256;
    GuestTrace guest_trace[kGuestTrace]{};
    uint64_t guest_trace_count = 0;
    void push_guest_trace(int cpu, uint32_t pc);

    // Start-button gate (tab menu "PUSH START"). The game starts from credits
    // at 0x37760 by setting bit 3 of 0xCEFF0; while the gate is closed that
    // write is masked. The host opens it when the start button is released.
    bool start_gate = false;
    bool start_gate_armed = false;
    bool start_pending = false;
    bool player_start_seen = false;  // a real host start press, not attract

    // Trace sampling point. MAME's emu.register_frame_done Lua callback runs at
    // the start of vblank (about line 224), while run_frame() returns at the end
    // of the 262-line frame. Sampling the port at the MAME point removes the
    // ~38-line phase offset from trace comparisons. -1 keeps the frame end.
    struct FrameSample {
        uint32_t main_pc = 0, subx_pc = 0, suby_pc = 0;
        uint16_t shared0 = 0, task_len = 0;
        uint32_t task_fn = 0;
        uint8_t portE = 0, ff0e = 0, ff09 = 0, ff04 = 0, lock = 0, cf810 = 0;
        bool valid = false;
    } sample;
    int sample_line = -1;
    void capture_sample();

    // Optional in-frame RAM dump (--dump-line) used when comparing against a
    // MAME dump taken from register_frame_done.
    std::string dump_prefix;
    int dump_line = -1;
    void dump_ram(const std::string &prefix);

    // Frame geometry (board timing; widescreen is a host crop).
    static constexpr int kLines = 262;
    static constexpr int kVisibleLines = 224;
    static constexpr int kVisibleWidth = 320;
    // The board's 68000s run at 12.5 MHz and the screen at 60 Hz, so a frame is
    // 208,333.33 CPU cycles and a line is 795.165… cycles. Rounding the line to
    // an integer loses ~43 cycles per frame; over a few hundred frames that is
    // enough to move main's foreground loop one vblank out of phase, which
    // showed up as a one-frame lag against MAME. Keep the exact rational time
    // base and derive each line target from the frame's absolute cycle count.
    static constexpr int kCpuClock = 12500000;
    static constexpr int kFrameRate = 60;
    int64_t cycle_at_line_end(int l) const {
        return ((int64_t)kCpuClock * ((frame * kLines) + l + 1)) /
               ((int64_t)kFrameRate * kLines);
    }
    static constexpr int kIrq2Line = 170;
    static constexpr int kVblankLine = 223;

    // 320 (or wider) indexed output. The video model fills this.
    static constexpr int kRenderWidth = 512;
    std::array<uint16_t, kRenderWidth * kVisibleLines> screen{};
    std::array<uint16_t, 512 * 512> y_fb{};   // 315-5305 back-sprite buffer
    std::array<uint16_t, kRenderWidth * kVisibleLines> b_fb{}; // 315-5196 front sprites
    int view_width = kVisibleWidth;           // 320 for 4:3, 398/426 for 16:9
    int view_x0 = 0;                          // left edge in board screen coords
    bool widescreen = false;

    const uint8_t *ysprite_rom() const { return ysprite_rom_.data(); }
    const uint8_t *bsprite_rom() const { return bsprite_rom_.data(); }
    const uint8_t *suby_rom() const { return rom_suby_.data(); }

    Audio *audio() { return audio_.get(); }
    void sound_write(uint8_t value);
    void set_voice_restoration(bool female_voices, bool additional_voices);
    bool open_sound_log(const std::string &path);
    bool open_sample_log(const std::string &path);
    void log_event_marker(const char *label);

    // Fallback/coverage diagnostics.
    uint64_t fallback_steps() const { return coverage_fallback_; }
    size_t fallback_unique_pcs() const;
    size_t translated_unique_routines() const;
    size_t translated_routine_total() const;
    int translated_percent() const;
    int translated_routine_percent() const;
    std::string fallback_report() const;
    bool write_coverage(const std::string &path) const;
    void reset_coverage();

    void render_frame();
    void render_y_sprites();
    void render_16b_sprites();
    void render_scanline(int y);
    void screenshot_ppm(const std::string &path);

private:
    void build_pages();
    void activate(int cpu);
    void deactivate(int cpu);
    void take_interrupts(Cpu &c);
    void reset_cpu(int cpu);
    void set_reset(int cpu, bool held);
    uint8_t analog_mux_value(unsigned sel) const;
    uint8_t read_io(uint32_t address);
    void write_io(uint32_t address, uint8_t value);

    std::vector<uint8_t> rom_main_, rom_subx_, rom_suby_;
    std::vector<uint8_t> z80_, pcm_, ysprite_rom_, bsprite_rom_;
    std::unique_ptr<Audio> audio_;
    bool restore_female_voices_ = true;
    bool restore_additional_voices_ = true;
    std::mt19937 voice_rng_{0x5eed1234u};
    std::ofstream sound_log_;
    std::ofstream sample_log_;
    void write_sound_log(const char *kind, const char *value);
    void write_sample_log(int voice, int group, uint8_t sample);
    static void sample_log_callback(void *user, int voice, int group,
                                    uint8_t sample);
    std::vector<ng_func_t> fn_[CPU_COUNT];
    bool faulted_ = false;

    uint64_t coverage_dispatches_ = 0;
    uint64_t coverage_translated_ = 0;
    uint64_t coverage_fallback_ = 0;
    std::unordered_map<uint32_t, uint64_t> fallback_pc_[CPU_COUNT];
    std::vector<uint8_t> translated_seen_[CPU_COUNT];
    uint64_t translated_unique_[CPU_COUNT] = {};
    size_t translated_table_count_[CPU_COUNT] = {};
};

extern Machine *g_machine;
extern int g_active_cpu;
extern bool bsprite_tap;   // log sub Y writes to the 16B sprite window
extern bool ff0e_tap;      // log accesses to the shared boot handshake byte 0xFF0E

} // namespace likethewind

#endif
