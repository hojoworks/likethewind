/*
 * machine.cpp — three 68000 contexts, the frame scheduler and the board RAM.
 */
#include "likethewind/machine.h"
#include "likethewind/audio.h"
#include "likethewind/bus.h"
#include "likethewind/fallback.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

namespace likethewind {

static bool read_file(const std::string &path, std::vector<uint8_t> &out, size_t expect, std::string &error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "cannot open " + path; return false; }
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    if (expect && (size_t)n != expect) {
        error = path + ": expected " + std::to_string(expect) + " bytes, got " + std::to_string((long long)n);
        return false;
    }
    out.resize((size_t)n);
    f.read((char *)out.data(), n);
    return true;
}

Machine::Machine() { g_machine = this; }
Machine::~Machine() { if (g_machine == this) g_machine = nullptr; }

bool Machine::load_roms(const std::string &dir, std::string &error) {
    struct Item { const char *name; std::vector<uint8_t> *dst; size_t size; } items[] = {
        {"main.bin", &rom_main_, 0x80000},
        {"subx.bin", &rom_subx_, 0x40000},
        {"suby.bin", &rom_suby_, 0x40000},
        {"ysprite.bin", &ysprite_rom_, 0x400000},
        {"bsprite.bin", &bsprite_rom_, 0x80000},
        {"z80.bin", &z80_, 0x10000},
        {"pcm.bin", &pcm_, 0x200000},
    };
    for (auto &it : items) {
        if (!read_file(dir + "/" + it.name, *it.dst, it.size, error)) return false;
    }
    return true;
}

bool Machine::load_selftest(const std::string &path, std::string &error) {
    if (!read_file(path, rom_main_, 0, error)) return false;
    // The synthetic program has no sub-CPU images; keep the vectors inert.
    rom_subx_.assign(0x40000, 0xFF);
    rom_suby_.assign(0x40000, 0xFF);
    return true;
}

void Machine::set_code(int cpu, const ng_func_entry_t *table, size_t count) {
    size_t words = 0;
    switch (cpu) {
    case MAIN: words = rom_main_.size() / 2; break;
    case SUBX: words = rom_subx_.size() / 2; break;
    case SUBY: words = rom_suby_.size() / 2; break;
    }
    fn_[cpu].assign(words, nullptr);
    for (size_t i = 0; i < count; i++) {
        uint32_t a = table[i].addr & 0xFFFFFE;
        if (a / 2 < fn_[cpu].size()) fn_[cpu][a / 2] = table[i].fn;
    }
    translated_seen_[cpu].assign(fn_[cpu].size(), 0);
    translated_unique_[cpu] = 0;
    translated_table_count_[cpu] = count;
    fallback_pc_[cpu].clear();
}

void Machine::build_pages() {
    for (int i = 0; i < CPU_COUNT; i++) {
        cpus[i].rmap.fill(nullptr);
        cpus[i].wmap.fill(nullptr);
    }
    // Main: 512 KB ROM, shared RAM, 64 KB local RAM.
    for (int p = 0; p < 8 && (size_t)(p + 1) * 0x10000 <= rom_main_.size(); p++)
        cpus[MAIN].rmap[p] = rom_main_.data() + p * 0x10000;
    cpus[MAIN].rmap[0x0C] = shared;
    cpus[MAIN].wmap[0x0C] = shared;
    cpus[MAIN].rmap[0x1F] = cpus[MAIN].wmap[0x1F] = main_local;

    // Sub X: 256 KB ROM, shared RAM, 64 KB sprite RAM. 0x1F is slow (local +
    // backup live in one 64 KB page).
    for (int p = 0; p < 4 && (size_t)(p + 1) * 0x10000 <= rom_subx_.size(); p++)
        cpus[SUBX].rmap[p] = rom_subx_.data() + p * 0x10000;
    cpus[SUBX].rmap[0x0C] = shared;
    cpus[SUBX].wmap[0x0C] = shared;
    cpus[SUBX].rmap[0x18] = cpus[SUBX].wmap[0x18] = ysprite;

    // Sub Y: 256 KB ROM, shared RAM, 64 KB local RAM. 0x18/0x19 are slow.
    for (int p = 0; p < 4 && (size_t)(p + 1) * 0x10000 <= rom_suby_.size(); p++)
        cpus[SUBY].rmap[p] = rom_suby_.data() + p * 0x10000;
    cpus[SUBY].rmap[0x0C] = shared;
    cpus[SUBY].wmap[0x0C] = shared;
    cpus[SUBY].rmap[0x1F] = cpus[SUBY].wmap[0x1F] = suby_local;
    if (ff0e_tap) {
        // Route shared RAM through the slow path so the handshake probe sees
        // every access. Debugging option only.
        for (int i = 0; i < CPU_COUNT; i++) {
            cpus[i].rmap[0x0C] = nullptr;
            cpus[i].wmap[0x0C] = nullptr;
        }
    }
}

static uint32_t rd32(const std::vector<uint8_t> &v, size_t at) {
    if (at + 3 >= v.size()) return 0;
    return ((uint32_t)v[at] << 24) | ((uint32_t)v[at + 1] << 16) |
           ((uint32_t)v[at + 2] << 8) | v[at + 3];
}

void Machine::reset_cpu(int i) {
    Cpu &c = cpus[i];
    const std::vector<uint8_t> &rom =
        i == MAIN ? rom_main_ : i == SUBX ? rom_subx_ : rom_suby_;
    const int64_t cycles = c.cycles;   // the board time base is global
    c.state = {};
    c.state.a[7] = rd32(rom, 0);
    c.state.pc = rd32(rom, 4) & 0xFFFFFF;
    c.state.s = 1;
    c.state.ipl = 7;
    c.state.stopped = 0;
    c.cycles = cycles;
    c.irq = 0;
}

void Machine::set_reset(int i, bool held) {
    Cpu &c = cpus[i];
    if (held) {
        if (!c.reset_held) {
            // Port E XRES/YRES: the CPU enters its reset state and stays there.
            reset_cpu(i);
            c.reset_held = true;
        }
    } else if (c.reset_held) {
        // Release: start from the reset vector now, not from the cycle count it
        // had when reset was asserted (the board clock kept running).
        reset_cpu(i);
        c.cycles = std::max(c.cycles, cpus[active].cycles);
        c.reset_held = false;
    }
}

void Machine::reset() {
    g_active_cpu = -1;
    fallback_init();
    build_pages();
    for (int i = 0; i < CPU_COUNT; i++) {
        Cpu &c = cpus[i];
        c.cycles = 0;
        c.next_event = 0;
        c.irq = 0;
        c.reset_held = false;
        reset_cpu(i);
    }
    frame = 0;
    line = 0;
    std::memset(shared, 0, sizeof(shared));
    std::memset(main_local, 0, sizeof(main_local));
    std::memset(subx_local, 0, sizeof(subx_local));
    std::memset(subx_backup, 0, sizeof(subx_backup));
    std::memset(suby_local, 0, sizeof(suby_local));
    std::memset(ysprite, 0, sizeof(ysprite));
    std::memset(bsprite, 0, sizeof(bsprite));
    std::memset(rotation, 0, sizeof(rotation));
    std::memset(palette, 0, sizeof(palette));
    std::memset(mult_regs, 0, sizeof(mult_regs));
    std::memset(div_regs, 0, sizeof(div_regs));
    std::memset(io_latch, 0, sizeof(io_latch));
    io_direction = 0;
    adc_shift = 0;
    display_enable = true;
    start_gate_armed = false;
    start_pending = false;
    player_start_seen = false;
    guest_trace_count = 0;
    std::memset(guest_trace, 0, sizeof(guest_trace));
    reset_coverage();
    faulted_ = false;

    audio_ = std::make_unique<Audio>();
    audio_->reset(z80_, pcm_);
}

void Machine::set_voice_restoration(bool female_voices, bool additional_voices) {
    restore_female_voices_ = female_voices;
    restore_additional_voices_ = additional_voices;
}

void Machine::sound_write(uint8_t value) {
    // The arcade game always uses the male/enemy voice commands. The cut
    // female lines use adjacent driver commands, so swap them when the
    // selected portrait is one of the two female drivers.
    const int character = (shared[0xE310] << 8) | shared[0xE310 + 1];
    // Driver order (zero-based): Michael, Keith, Jason, Storoganoph, Tom,
    // Mark, Jimmy, Emily, Lucy, Jeronimo, Harry, Eric.
    const bool female_driver = character == 7 || character == 8;
    const bool use_female = restore_female_voices_ && female_driver;
    uint8_t output = value;

    const auto pick = [&](std::initializer_list<uint8_t> choices) {
        const auto *begin = choices.begin();
        return *(begin + (voice_rng_() % choices.size()));
    };

    // Passing: BF = "Eat dirt bozo", C1 = "Hey get out of the way",
    // C3 = "Later Suckers".  Female counterparts are BE, AE and BD.
    const bool passing = value == 0xBF || value == 0xC1 || value == 0xC3;
    if (use_female) {
        if (passing) {
            output = restore_additional_voices_
                         ? pick({0xBD, 0xBE, 0xAE})  // 5E, 5F, 51
                         : (value == 0xBF ? 0xBE : value == 0xC1 ? 0xAE : 0xBD);
        } else if (value == 0xBC) {
            output = 0xB7;  // male wall hit -> female wall hit (5C)
        } else if (value == 0xB6) {
            output = restore_additional_voices_
                         ? pick({0xB5, 0xAF})  // 5A, 54
                         : 0xB5;
        }
    } else if (restore_additional_voices_ && passing && !female_driver) {
        output = pick({0xBF, 0xC1, 0xC3});  // 60, 62, 64
    }

    sound_latch = output;
    if (output >= 0x90 && output <= 0x97) last_music_cmd = output;
    if (audio_) audio_->sound_latch(output);
    char cmd[8];
    std::snprintf(cmd, sizeof(cmd), "%02X", value);
    write_sound_log("cmd", cmd);
}

bool Machine::open_sound_log(const std::string &path) {
    sound_log_.open(path, std::ios::out | std::ios::trunc);
    if (!sound_log_) return false;
    sound_log_ << "kind,frame,mode,char,speed,wheel,gas,brake,general,value\n";
    return true;
}

bool Machine::open_sample_log(const std::string &path) {
    sample_log_.open(path, std::ios::out | std::ios::trunc);
    if (!sample_log_) return false;
    sample_log_ << "kind,frame,mode,char,speed,wheel,gas,brake,general,"
                   "voice,group,sample\n";
    if (audio_) audio_->set_sample_log(&Machine::sample_log_callback, this);
    return true;
}

void Machine::sample_log_callback(void *user, int voice, int group,
                                  uint8_t sample) {
    static_cast<Machine *>(user)->write_sample_log(voice, group, sample);
}

void Machine::write_sample_log(int voice, int group, uint8_t sample) {
    if (!sample_log_.is_open()) return;
    const auto word = [&](int offset) {
        return (uint16_t)((shared[offset] << 8) | shared[offset + 1]);
    };
    const int mode = word(0xE09C);
    const int character = word(0xE310);
    const int speed = word(0xEA12);
    char sample_hex[8];
    std::snprintf(sample_hex, sizeof(sample_hex), "%02X", sample);
    sample_log_ << "sample," << frame << ',' << mode << ',' << character << ','
                << speed << ',' << (int)in_wheel << ',' << (int)in_gas << ','
                << (int)in_brake << ',' << (int)in_general << ',' << voice << ','
                << group << ',' << sample_hex << '\n';
    sample_log_.flush();
}

void Machine::write_sound_log(const char *kind, const char *value) {
    if (!sound_log_.is_open()) return;
    const auto word = [&](int offset) {
        return (uint16_t)((shared[offset] << 8) | shared[offset + 1]);
    };
    const int mode = word(0xE09C);
    const int character = word(0xE310);
    const int speed = word(0xEA12);
    sound_log_ << kind << ',' << frame << ',' << mode << ',' << character << ','
               << speed << ',' << (int)in_wheel << ',' << (int)in_gas << ','
               << (int)in_brake << ',' << (int)in_general << ','
               << (value ? value : "") << '\n';
    sound_log_.flush();
}

void Machine::log_event_marker(const char *label) {
    write_sound_log("mark", label);
}

void Machine::push_guest_trace(int cpu, uint32_t pc) {
    GuestTrace &e = guest_trace[guest_trace_count % kGuestTrace];
    e.pc = pc;
    e.cycles = (uint32_t)cpus[cpu].cycles;
    e.frame = (uint64_t)frame;
    e.line = (uint16_t)line;
    e.cpu = (uint8_t)cpu;
    guest_trace_count++;
}

bool Machine::start_button(bool down) {
    if (down) player_start_seen = true;
    if (!start_gate) {
        start_pending = false;
        return down;
    }
    const int mode = (shared[0xE09C] << 8) | shared[0xE09D];
    // In attract (mode < 8) with credits, buffer the first press. Releasing it
    // opens the credit-start gate; the press itself is not passed on, so the
    // course-select screen still waits for its own start.
    if (down && !start_gate_armed && mode < 8 && shared[0xE852] > 0)
        start_pending = true;
    if (!down && start_pending) {
        start_pending = false;
        start_gate_armed = true;
    }
    return down && !start_pending;
}

void Machine::activate(int cpu) {
    if (g_active_cpu >= 0 && g_active_cpu < CPU_COUNT && g_active_cpu != cpu) {
        Cpu &old = cpus[g_active_cpu];
        old.state = ng_cpu;
        old.cycles = ng_cycles;
        old.next_event = ng_next_event;
    }
    active = cpu;
    g_active_cpu = cpu;
    Cpu &c = cpus[cpu];
    ng_cpu = c.state;
    ng_cycles = c.cycles;
    ng_next_event = c.next_event;
    ng_rmap = c.rmap.data();
    ng_wmap = c.wmap.data();
}

void Machine::deactivate(int cpu) {
    Cpu &c = cpus[cpu];
    c.state = ng_cpu;
    c.cycles = ng_cycles;
    c.next_event = ng_next_event;
    g_active_cpu = -1;
}

void Machine::take_interrupts(Cpu &c) {
    // The live register file is ng_cpu while a CPU is active; c.state is the
    // saved copy. Everything here must act on ng_cpu.
    if (!c.irq || ng_cpu.ipl >= c.irq) return;
    uint8_t level = c.irq;
    irqCount[active][level]++;
    uint16_t old = ng_get_sr();
    ng_cpu.stopped = 0;
    ng_set_sr((uint16_t)(((old | 0x2000) & ~0x8700) | (level << 8)));
    ng_push32(ng_cpu.pc);
    ng_push16(old);
    ng_cpu.pc = ng_r32(0x60u + (uint32_t)level * 4);
    ng_cycles += 44;
    c.irq = 0;              // one take per assertion; the frame loop re-arms
    c.state = ng_cpu;
    c.cycles = ng_cycles;
}

void Machine::run_until(int cpu, int64_t target) {
    if (cpu < 0 || cpu >= CPU_COUNT || cpus[cpu].reset_held) return;
    activate(cpu);
    Cpu &c = cpus[cpu];
    int guard = 0;
    // Take a pending line interrupt even if this CPU is already ahead of the
    // slice target. take_interrupts() clears c.irq, so it cannot stack.
    take_interrupts(c);
    while (c.cycles < target && !faulted_) {
        if (c.state.stopped) {
            // STOP waits for an interrupt; idle until the slice ends. Update
            // ng_cycles too, because deactivate() saves that back into c.cycles.
            ng_cycles = std::max(ng_cycles, target);
            c.cycles = ng_cycles;
            break;
        }
        uint32_t pc = c.state.pc & 0xFFFFFE;
        coverage_dispatches_++;
        if (pc / 2 >= fn_[cpu].size() || !fn_[cpu][pc / 2]) {
            // Runtime-only entry: interpret one instruction, then let the
            // dispatcher try again. The build's profile pass feeds these back
            // into the recompiler.
            coverage_fallback_++;
            ++fallback_pc_[cpu][pc];
            static int reported[CPU_COUNT] = {};
            if (verbose && reported[cpu] < 20) {
                std::fprintf(stderr, "[likethewind] cpu %d: interpreting $%06X (frame %lld line %lld)\n",
                             cpu, pc, (long long)frame, (long long)line);
                reported[cpu]++;
            }
            push_guest_trace(cpu, pc);
            fallback_step();
            c.state = ng_cpu;
            c.cycles = ng_cycles;
            if (++guard > 4000000) {
                std::fprintf(stderr, "[likethewind] cpu %d: runaway in interpreter at $%06X\n", cpu, c.state.pc);
                faulted_ = true;
                break;
            }
            continue;
        }
        int64_t before = c.cycles;
        uint32_t before_pc = c.state.pc;
        coverage_translated_++;
        const size_t seen_index = pc / 2;
        if (seen_index < translated_seen_[cpu].size() &&
            !translated_seen_[cpu][seen_index]) {
            translated_seen_[cpu][seen_index] = 1;
            translated_unique_[cpu]++;
        }
        push_guest_trace(cpu, pc);
        ng_next_event = target;
        fn_[cpu][pc / 2]();
        c.state = ng_cpu;
        c.cycles = ng_cycles;
        if (c.cycles == before && c.state.pc == before_pc) {
            std::fprintf(stderr, "[likethewind] cpu %d: routine at $%06X made no progress\n", cpu, pc);
            faulted_ = true;
            break;
        }
        take_interrupts(c);
        if (++guard > 2000000) {
            std::fprintf(stderr, "[likethewind] cpu %d: runaway at $%06X\n", cpu, c.state.pc);
            faulted_ = true;
            break;
        }
    }
    deactivate(cpu);
}

void Machine::run_frame() {
    for (line = 0; line < kLines && !faulted_; line++) {
        int64_t target = cycle_at_line_end((int)line);
        for (int i = 0; i < CPU_COUNT; i++) {
            cpus[i].irq = 0;
            if (line == kIrq2Line) cpus[i].irq = std::max<uint8_t>(cpus[i].irq, 2);
            if (line == kVblankLine) cpus[i].irq = std::max<uint8_t>(cpus[i].irq, 4);
        }
        // Interleave in small slices so shared-RAM RMW stays atomic and the
        // three CPUs stay within a fraction of a scanline.
        for (;;) {
            bool any = false;
            // Run sub Y then sub X then main. The vblank handlers race on the
            // shared frame counter, and Power Drift expects sub Y to read it
            // before main decrements it.
            for (int k = 0; k < CPU_COUNT; k++) {
                int i = reverse_order ? (CPU_COUNT - 1 - k) : k;
                if (cpus[i].reset_held) continue;
                if (cpus[i].cycles < target) any = true;
                // A CPU that has already passed this line only needs a call
                // when it has a pending interrupt.
                if (cpus[i].cycles >= target && cpus[i].irq == 0) continue;
                run_until(i, std::min<int64_t>(target, cpus[i].cycles + slice_cycles));
            }
            if (!any || faulted_) break;
        }
        // The active list is the one built during the previous frame. Sample
        // it just before the line-223 swap, while it is still complete.
        if (line == 220) render_frame();
        if (sample_line >= 0 && line == sample_line) capture_sample();
        if (dump_line >= 0 && line == dump_line && !dump_prefix.empty()) dump_ram(dump_prefix);
    }
    if (audio_) audio_->run_frame();
    frame++;
    if (faulted_) render_frame();
}

void Machine::capture_sample() {
    FrameSample &s = sample;
    s.main_pc = cpus[MAIN].state.pc;
    s.subx_pc = cpus[SUBX].state.pc;
    s.suby_pc = cpus[SUBY].state.pc;
    s.portE = io_latch[4];
    s.shared0 = (uint16_t)((shared[0] << 8) | shared[1]);
    s.ff0e = shared[0xFF0E];
    s.ff09 = shared[0xFF09];
    s.ff04 = shared[0xFF04];
    s.lock = suby_local[0x9F0];
    s.task_fn = (uint32_t)((shared[0x10] << 24) | (shared[0x11] << 16) |
                           (shared[0x12] << 8) | shared[0x13]);
    s.task_len = (uint16_t)((shared[0x14] << 8) | shared[0x15]);
    s.cf810 = shared[0xF810];
    s.valid = true;
}

void Machine::dump_ram(const std::string &prefix) {
    auto dump = [&](const char *suffix, const void *p, size_t n) {
        std::ofstream f(prefix + suffix, std::ios::binary);
        f.write((const char *)p, (std::streamsize)n);
    };
    dump(".shared", shared, sizeof(shared));
    dump(".main", main_local, sizeof(main_local));
    dump(".subx", subx_local, sizeof(subx_local));
    dump(".suby", suby_local, sizeof(suby_local));
    dump(".ysprite", ysprite, sizeof(ysprite));
    dump(".bsprite", bsprite, sizeof(bsprite));
    dump(".rotation", rotation, sizeof(rotation));
    dump(".palette", palette, sizeof(palette));
}

void Machine::screenshot_ppm(const std::string &path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;
    f << "P6\n" << view_width << " " << kVisibleLines << "\n255\n";
    for (int y = 0; y < kVisibleLines; y++) {
        for (int x = 0; x < view_width; x++) {
            uint16_t v = palette[screen[y * kRenderWidth + x] & 0x3FFF];
            uint8_t r = (uint8_t)(((v & 0x0F) << 1) | ((v >> 12) & 1));
            uint8_t g = (uint8_t)((((v >> 4) & 0x0F) << 1) | ((v >> 13) & 1));
            uint8_t b = (uint8_t)((((v >> 8) & 0x0F) << 1) | ((v >> 14) & 1));
            uint8_t rgb[3] = {(uint8_t)(r * 255 / 31), (uint8_t)(g * 255 / 31), (uint8_t)(b * 255 / 31)};
            f.write((char *)rgb, 3);
        }
    }
}

void Machine::reset_coverage() {
    coverage_dispatches_ = 0;
    coverage_translated_ = 0;
    coverage_fallback_ = 0;
    for (int i = 0; i < CPU_COUNT; i++) {
        fallback_pc_[i].clear();
        std::fill(translated_seen_[i].begin(), translated_seen_[i].end(), 0);
        translated_unique_[i] = 0;
    }
}

size_t Machine::fallback_unique_pcs() const {
    size_t n = 0;
    for (int i = 0; i < CPU_COUNT; i++) n += fallback_pc_[i].size();
    return n;
}

size_t Machine::translated_unique_routines() const {
    size_t n = 0;
    for (int i = 0; i < CPU_COUNT; i++) n += (size_t)translated_unique_[i];
    return n;
}

size_t Machine::translated_routine_total() const {
    size_t n = 0;
    for (int i = 0; i < CPU_COUNT; i++) n += translated_table_count_[i];
    return n;
}

int Machine::translated_percent() const {
    if (coverage_dispatches_ == 0) return 0;
    return (int)((coverage_translated_ * 100 + coverage_dispatches_ / 2) /
                 coverage_dispatches_);
}

int Machine::translated_routine_percent() const {
    const size_t total = translated_routine_total();
    if (total == 0) return 0;
    return (int)((translated_unique_routines() * 100 + total / 2) / total);
}

std::string Machine::fallback_report() const {
    static const char *const cpu_names[CPU_COUNT] = {"main", "subx", "suby"};
    std::ostringstream f;

    f << "LikeTheWind fallback/coverage report\n"
      << "# To translate these: python tools\\seed_from_coverage.py <this file>\n"
      << "# Then rebuild with: powershell -ExecutionPolicy Bypass -File "
         "tools\\build.ps1 -EntriesOnly\n"
      << "frame=" << frame << "\n"
      << "dispatches=" << coverage_dispatches_ << "\n"
      << "native_dispatches=" << coverage_translated_ << "\n"
      << "fallback_instructions=" << coverage_fallback_ << "\n"
      << "native_dispatch_percent=" << translated_percent() << "\n"
      << "native_routines=" << translated_unique_routines() << "/"
      << translated_routine_total() << " (" << translated_routine_percent()
      << "%)\n"
      << "unique_fallback_pcs=" << fallback_unique_pcs() << "\n";

    for (int c = 0; c < CPU_COUNT; c++) {
        uint64_t cpu_fallback = 0;
        for (const auto &kv : fallback_pc_[c]) cpu_fallback += kv.second;

        f << "\nCPU " << c << " (" << cpu_names[c] << ")\n"
          << "native_routines=" << translated_unique_[c] << "/"
          << translated_table_count_[c] << "\n"
          << "fallback_instructions=" << cpu_fallback << "\n"
          << "unique_fallback_pcs=" << fallback_pc_[c].size() << "\n"
          << "fallback_pcs:\n";

        std::vector<std::pair<uint32_t, uint64_t>> pcs(fallback_pc_[c].begin(),
                                                       fallback_pc_[c].end());
        std::sort(pcs.begin(), pcs.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
        if (pcs.empty()) {
            f << "  (none)\n";
        } else {
            for (const auto &p : pcs) {
                char line[96];
                std::snprintf(line, sizeof(line), "  $%06X count=%llu\n",
                              (unsigned)p.first, (unsigned long long)p.second);
                f << line;
            }
        }
    }
    return f.str();
}

bool Machine::write_coverage(const std::string &path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << fallback_report();
    return (bool)f;
}

} // namespace likethewind
