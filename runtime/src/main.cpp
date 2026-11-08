/*
 * main.cpp â€” CLI and headless host.
 *
 * The generated translation units expose one table per CPU. They are weak so
 * the executable still links while a build only has the synthetic fixture.
 */
#include "likethewind/audio.h"
#include "likethewind/crash.h"
#include "likethewind/machine.h"
#include "likethewind/menu.h"
#include "likethewind/menu_test.h"
#include "likethewind/rom_prepare.h"
#include "likethewind/settings.h"
#include "likethewind/window.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

extern "C" {
extern const ng_func_entry_t t_ng_recomp_table[] __attribute__((weak));
extern const size_t t_ng_recomp_count __attribute__((weak));
extern const ng_func_entry_t m_ng_recomp_table[] __attribute__((weak));
extern const size_t m_ng_recomp_count __attribute__((weak));
extern const ng_func_entry_t x_ng_recomp_table[] __attribute__((weak));
extern const size_t x_ng_recomp_count __attribute__((weak));
extern const ng_func_entry_t y_ng_recomp_table[] __attribute__((weak));
extern const size_t y_ng_recomp_count __attribute__((weak));
}

using namespace likethewind;

static int selftest() {
    // The board model is larger than the default Windows stack; keep it on the heap.
    auto owner = std::make_unique<Machine>();
    Machine &machine = *owner;
    std::string error;
    if (!machine.load_selftest("tests/fixtures/selftest.bin", error)) {
        std::fprintf(stderr, "selftest: %s\n", error.c_str());
        return 1;
    }
    if (!t_ng_recomp_table) {
        std::fprintf(stderr, "selftest: no translated test table; run tools/build.ps1\n");
        return 1;
    }
    machine.set_code(MAIN, t_ng_recomp_table, t_ng_recomp_count);
    machine.reset();
    machine.run_until(MAIN, 100000);

    bool pass = true;
    // DBRA runs N+1 times (5 -> 6), so D0 is 6 when the loop ends.
    if (machine.shared[3] != 6 || machine.shared[4] != 0x12 || machine.shared[5] != 0x34) {
        std::fprintf(stderr,
                     "selftest: FAIL shared = %02X %02X %02X %02X %02X %02X (want ....06 1234)\n",
                     machine.shared[0], machine.shared[1], machine.shared[2],
                     machine.shared[3], machine.shared[4], machine.shared[5]);
        pass = false;
    }
    machine.screenshot_ppm("work/selftest.ppm");
    std::printf("selftest: %s (shared %02X%02X%02X%02X, pc %06X, %lld cycles)\n",
                pass ? "PASS" : "FAIL", machine.shared[0], machine.shared[1],
                machine.shared[2], machine.shared[3],
                machine.cpus[MAIN].state.pc, (long long)machine.cpus[MAIN].cycles);
    return pass ? 0 : 1;
}

static int fallbacktest() {
    auto owner = std::make_unique<Machine>();
    Machine &machine = *owner;
    std::string error;
    if (!machine.load_selftest("tests/fixtures/selftest.bin", error)) {
        std::fprintf(stderr, "fallbacktest: %s\n", error.c_str());
        return 1;
    }
    if (!t_ng_recomp_table) {
        std::fprintf(stderr, "fallbacktest: no translated test table\n");
        return 1;
    }
    machine.set_code(MAIN, t_ng_recomp_table, t_ng_recomp_count);
    machine.reset();
    // 0x400 is deliberately absent from the generated table.
    machine.cpus[MAIN].state.pc = 0x400;
    machine.run_until(MAIN, 100000);
    bool pass = machine.shared[3] == 9;
    const bool coverage_ok =
        machine.fallback_steps() > 0 && machine.fallback_unique_pcs() > 0;
    std::printf("fallbacktest: %s (shared %02X%02X%02X%02X, pc %06X, "
                "%llu fallback instrs, %zu fallback PCs)\n",
                (pass && coverage_ok) ? "PASS" : "FAIL", machine.shared[0],
                machine.shared[1], machine.shared[2], machine.shared[3],
                machine.cpus[MAIN].state.pc,
                (unsigned long long)machine.fallback_steps(),
                machine.fallback_unique_pcs());
    return (pass && coverage_ok) ? 0 : 1;
}

static void make_test_pattern(Machine &machine) {
    for (int i = 0; i < 0x2000; i++) {
        int r = (i * 5) & 31, g = (i * 3) & 31, b = (i * 7) & 31;
        machine.palette[i] = (uint16_t)(r | (g << 4) | (b << 8));
    }
    for (int y = 0; y < 512; y += 2) {
        uint8_t *p = machine.rotation + y * 2;
        p[0] = 0x00; p[1] = 0x00;
        p[2] = 0x01; p[3] = 0xFF;
    }
    auto put32 = [&](int word, uint32_t v) {
        uint8_t *p = machine.rotation + word * 2;
        p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
        p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
    };
    put32(0x3F0, 0);
    put32(0x3F2, 0);
    put32(0x3F4, 0x00004000);
    put32(0x3F6, 0x00004000);
    put32(0x3F8, 0);
    put32(0x3FA, 0);
}

static int windowtest() {
    auto owner = std::make_unique<Machine>();
    Machine &machine = *owner;
    machine.reset();
    make_test_pattern(machine);
    machine.render_frame();
    for (int i = 0; i < CPU_COUNT; i++) machine.cpus[i].reset_held = true;  // no ROM
    Settings settings;
    settings.scale = 3;
    bool ok = likethewind::run_window(machine, settings, 60, false, false);
    std::printf("windowtest: %s\n", ok ? "PASS" : "SKIP (no display/SDL)");
    return ok ? 0 : 1;
}

static int videotest() {
    auto owner = std::make_unique<Machine>();
    Machine &machine = *owner;
    machine.reset();
    for (int i = 0; i < 0x2000; i++) {
        int r = (i * 5) & 31, g = (i * 3) & 31, b = (i * 7) & 31;
        machine.palette[i] = (uint16_t)(r | (g << 4) | (b << 8));
    }
    // A 1:1 rotation and a full-width clip: the scanline colour should sweep
    // through the palette, proving the scan-out and host path.
    for (int y = 0; y < 512; y += 2) {
        uint8_t *p = machine.rotation + y * 2;
        p[0] = 0x00; p[1] = 0x00;       // min x = 0
        p[2] = 0x01; p[3] = 0xFF;       // max x = 511
    }
    auto put32 = [&](int word, uint32_t v) {
        uint8_t *p = machine.rotation + word * 2;
        p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
        p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
    };
    put32(0x3F0, 0);              // currx
    put32(0x3F2, 0);              // curry
    put32(0x3F4, 0x00004000);     // dyy = 1.0
    put32(0x3F6, 0x00004000);     // dxx = 1.0
    put32(0x3F8, 0);              // dxy
    put32(0x3FA, 0);              // dyx
    machine.render_frame();
    bool any = false;
    for (int y = 0; y < Machine::kVisibleLines && !any; y++)
        for (int x = 0; x < machine.view_width; x++)
            if (machine.screen[y * Machine::kRenderWidth + x]) { any = true; break; }
    machine.screenshot_ppm("work/videotest.ppm");
    std::printf("videotest: %s (view %dx%d)\n", any ? "PASS" : "FAIL",
                machine.view_width, Machine::kVisibleLines);
    return any ? 0 : 1;
}

static void write_wav(const std::string &path, const std::vector<int16_t> &samples, int rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;
    auto u32 = [&](uint32_t v) {
        char b[4] = {(char)(v & 0xFF), (char)((v >> 8) & 0xFF),
                     (char)((v >> 16) & 0xFF), (char)((v >> 24) & 0xFF)};
        f.write(b, 4);
    };
    auto u16 = [&](uint16_t v) {
        char b[2] = {(char)(v & 0xFF), (char)((v >> 8) & 0xFF)};
        f.write(b, 2);
    };
    const uint32_t data_bytes = (uint32_t)(samples.size() * sizeof(int16_t));
    f.write("RIFF", 4);
    u32(36 + data_bytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);                       // PCM
    u16(2);                       // stereo
    u32((uint32_t)rate);
    u32((uint32_t)(rate * 2 * 2));  // byte rate
    u16(4);                       // block align
    u16(16);                      // bits
    f.write("data", 4);
    u32(data_bytes);
    f.write((const char *)samples.data(), (std::streamsize)data_bytes);
}

static void report_coverage(Machine &machine, const std::string &path, bool verbose) {
    if (path.empty() && !verbose) return;
    if (!path.empty() && !machine.write_coverage(path))
        std::fprintf(stderr, "likethewind: cannot write coverage report '%s'\n",
                     path.c_str());
    std::printf("coverage: %d%% native dispatch, %zu/%zu native routines, "
                "%llu fallback instructions, %zu unique fallback PCs\n",
                machine.translated_percent(), machine.translated_unique_routines(),
                machine.translated_routine_total(),
                (unsigned long long)machine.fallback_steps(),
                machine.fallback_unique_pcs());
}


static void show_error(const std::string &message) {
#ifdef _WIN32
    MessageBoxA(nullptr, message.c_str(), "LikeTheWind", MB_OK | MB_ICONERROR);
#else
    std::fprintf(stderr, "likethewind: %s\n", message.c_str());
#endif
}

static void usage(const char *argv0) {
    std::printf("usage: %s [--rom DIR] [--play] [--menu] [--widescreen] [--scale N] [--verbose] "
                "[--frames N] [--auto-start] [--autoplay] [--start-only] [--coin-only] [--coin-frame N] "
                "[--push-start] [--dip-a N] [--dip-b N] "
                "[--trace FILE] [--trace-line N] [--dump-ram PREFIX] "
                "[--dump-line N] [--tap-ff0e] [--steer N] "
                "[--audio-trace] [--wav FILE] [--sound-log FILE] [--sample-log FILE] "
                "[--screenshot FILE] [--slice N] [--forward] [--interp] "
                "[--coverage FILE] "
                "[--selftest] [--videotest] [--fallbacktest] [--windowtest] [--menutest] [--inputtest] "
                "[--crash-test] [--crash-test-frame N] [--crash-test-cpp]\n",
                argv0);
}

int main(int argc, char **argv) {
    install_crash_handler();
    if (std::getenv("LIKETHEWIND_HANG_TEST")) {
        // Diagnostic hook: stop producing heartbeats so the watchdog path can
        // be verified end to end.
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
    const bool release_mode = (argc == 1);
    std::string romdir = "work/roms";
    std::string shot;
    int frames = 0;
    bool test = false;
    bool vtest = false;
    bool ftest = false;
    bool wtest = false;
    bool mtest = false;
    bool itest = false;
    bool crash_test = false;
    bool crash_test_cpp = false;
    int crash_test_frame = -1;
    bool play = false;
    bool start_menu = false;
    bool autoplay_cli = false;
    bool wide = false;
    bool wide_set = false;
    bool verbose = false;
    bool interpSuby = false;
    bool interpAll = false;
    bool forward = false;
    bool autoStart = false;
    bool startOnly = false;
    bool coinOnly = false;
    bool push_start = false;
    int coin_frame = 60;
    int dip_a_override = -1;
    int dip_b_override = -1;
    bool audio_trace = false;
    int fixed_steer = -1;
    int slice = 8;
    int scale = 3;
    bool scale_set = false;
    int trace_line = -1;
    std::string tracefile;
    std::string dumpram;
    std::string dumpfb;
    std::string wavfile;
    std::string sound_log_path;
    std::string sample_log_path;
    std::string coverage_file;
    std::vector<int16_t> wav_samples;
    int dump_line = -1;

    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--selftest")) test = true;
        else if (!std::strcmp(argv[i], "--videotest")) vtest = true;
        else if (!std::strcmp(argv[i], "--fallbacktest")) ftest = true;
        else if (!std::strcmp(argv[i], "--windowtest")) wtest = true;
        else if (!std::strcmp(argv[i], "--menutest")) mtest = true;
        else if (!std::strcmp(argv[i], "--inputtest")) itest = true;
        else if (!std::strcmp(argv[i], "--crash-test")) crash_test = true;
        else if (!std::strcmp(argv[i], "--crash-test-cpp")) crash_test_cpp = true;
        else if (!std::strcmp(argv[i], "--crash-test-frame") && i + 1 < argc) crash_test_frame = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--play")) play = true;
        else if (!std::strcmp(argv[i], "--menu")) start_menu = true;
        else if (!std::strcmp(argv[i], "--autoplay")) autoplay_cli = true;
        else if (!std::strcmp(argv[i], "--widescreen")) { wide = true; wide_set = true; }
        else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
        else if (!std::strcmp(argv[i], "--bsp-tap")) likethewind::bsprite_tap = true;
        else if (!std::strcmp(argv[i], "--tap-ff0e")) likethewind::ff0e_tap = true;
        else if (!std::strcmp(argv[i], "--audio-trace")) audio_trace = true;
        else if (!std::strcmp(argv[i], "--steer") && i + 1 < argc) fixed_steer = (int)std::strtol(argv[++i], nullptr, 0);
        else if (!std::strcmp(argv[i], "--interp-suby")) interpSuby = true;
        else if (!std::strcmp(argv[i], "--interp")) interpAll = true;
        else if (!std::strcmp(argv[i], "--coverage") && i + 1 < argc) coverage_file = argv[++i];
        else if (!std::strcmp(argv[i], "--auto-start")) autoStart = true;
        else if (!std::strcmp(argv[i], "--start-only")) startOnly = true;
        else if (!std::strcmp(argv[i], "--coin-only")) coinOnly = true;
        else if (!std::strcmp(argv[i], "--push-start")) push_start = true;
        else if (!std::strcmp(argv[i], "--coin-frame") && i + 1 < argc) coin_frame = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--dip-a") && i + 1 < argc) dip_a_override = (int)std::strtol(argv[++i], nullptr, 0);
        else if (!std::strcmp(argv[i], "--dip-b") && i + 1 < argc) dip_b_override = (int)std::strtol(argv[++i], nullptr, 0);
        else if (!std::strcmp(argv[i], "--forward")) forward = true;
        else if (!std::strcmp(argv[i], "--slice") && i + 1 < argc) slice = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) { scale = std::atoi(argv[++i]); scale_set = true; }
        else if (!std::strcmp(argv[i], "--trace") && i + 1 < argc) tracefile = argv[++i];
        else if (!std::strcmp(argv[i], "--trace-line") && i + 1 < argc) trace_line = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--dump-ram") && i + 1 < argc) dumpram = argv[++i];
        else if (!std::strcmp(argv[i], "--dump-line") && i + 1 < argc) dump_line = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--dump-fb") && i + 1 < argc) dumpfb = argv[++i];
        else if (!std::strcmp(argv[i], "--wav") && i + 1 < argc) wavfile = argv[++i];
        else if (!std::strcmp(argv[i], "--sound-log") && i + 1 < argc) sound_log_path = argv[++i];
        else if (!std::strcmp(argv[i], "--sample-log") && i + 1 < argc) sample_log_path = argv[++i];
        else if (!std::strcmp(argv[i], "--rom") && i + 1 < argc) romdir = argv[++i];
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--headless")) { /* headless is the default for now */ }
        else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) { usage(argv[0]); return 0; }
        else { std::fprintf(stderr, "unknown option: %s\n", argv[i]); usage(argv[0]); return 2; }
    }

    if (autoplay_cli) play = true;

    if (release_mode) {
        // Double-click launch: no command line. Use the exe's own folder,
        // prepare roms/pdrift.zip on first run, and open the game window.
        play = true;
        const std::string exe_dir = executable_directory();
#ifdef _WIN32
        FreeConsole();
        SetCurrentDirectoryA(exe_dir.c_str());
#endif
        RomPrepareResult prepared = prepare_release_roms(exe_dir);
        if (!prepared.ok) {
            show_error(prepared.error);
            return 1;
        }
        romdir = prepared.rom_dir;
    }

    if (crash_test) {
        // Deliberate access violation to verify the crash trap end to end.
        *(volatile int *)(uintptr_t)0 = 1;
    }
    if (crash_test_cpp) {
        // Uncaught C++ exception: exercises the std::terminate path.
        throw std::runtime_error("deliberate crash-test exception");
    }
    if (test) return selftest();
    if (vtest) return videotest();
    if (ftest) return fallbacktest();
    if (wtest) return windowtest();
    if (mtest) return menutest();
    if (itest) return inputtest();
    if (trace_line >= Machine::kLines || dump_line >= Machine::kLines) {
        std::fprintf(stderr, "likethewind: --trace-line/--dump-line must be in 0..%d\n",
                     Machine::kLines - 1);
        return 2;
    }

    auto owner = std::make_unique<Machine>();
    Machine &machine = *owner;
    std::string error;
    if (!machine.load_roms(romdir, error)) {
        std::fprintf(stderr, "likethewind: %s\n", error.c_str());
        std::fprintf(stderr, "likethewind: run tools/prepare_rom.py with your own pdrift.zip first\n");
        return 1;
    }
    bool have = true;
    if (m_ng_recomp_table) machine.set_code(MAIN, m_ng_recomp_table, m_ng_recomp_count);
    else have = false;
    if (x_ng_recomp_table) machine.set_code(SUBX, x_ng_recomp_table, x_ng_recomp_count);
    else have = false;
    if (y_ng_recomp_table) machine.set_code(SUBY, y_ng_recomp_table, y_ng_recomp_count);
    else have = false;
    if (!have) {
        std::fprintf(stderr, "likethewind: translated code missing; run tools/recompile.py then rebuild\n");
        return 1;
    }
    // (options that touch the machine are applied after construction)
    if (interpAll) {
        for (int i = 0; i < CPU_COUNT; i++) machine.clear_code(i);
    } else if (interpSuby) {
        machine.clear_code(SUBY);
    }
    machine.reverse_order = !forward;
    machine.slice_cycles = slice;
    machine.sample_line = trace_line;
    machine.dump_line = dump_line;
    machine.dump_prefix = dumpram;
    if (wide) {
        machine.widescreen = true;
        machine.view_width = 426;
        machine.view_x0 = -(426 - Machine::kVisibleWidth) / 2;
    }
    machine.reset();
    if (!sound_log_path.empty() && !machine.open_sound_log(sound_log_path))
        std::fprintf(stderr, "likethewind: cannot open sound log '%s'\n",
                     sound_log_path.c_str());
    if (!sample_log_path.empty() && !machine.open_sample_log(sample_log_path))
        std::fprintf(stderr, "likethewind: cannot open sample log '%s'\n",
                     sample_log_path.c_str());
    machine.start_gate = push_start;
    if (dip_a_override >= 0) machine.dip_a = (uint8_t)dip_a_override;
    if (dip_b_override >= 0) machine.dip_b = (uint8_t)dip_b_override;
    machine.verbose = verbose || !tracefile.empty();
    if (audio_trace && machine.audio()) machine.audio()->set_trace(true);
    if (play) {
        Settings settings;
        settings.load("likethewind-settings.cfg");
        if (wide_set) settings.aspect = 1;
        if (scale_set) settings.scale = scale;
        if (autoplay_cli) settings.autoplay = true;
        if (verbose) settings.show_fps = true;
        machine.dip_a = settings.dip_a();
        machine.dip_b = settings.dip_b();
        likethewind::run_window(machine, settings, frames, true, start_menu);
        report_coverage(machine, coverage_file, verbose);
        return 0;
    }
    if (frames <= 0) frames = 60;
    std::ofstream trace;
    if (!tracefile.empty()) {
        trace.open(tracefile);
        trace << "frame,main_pc,subx_pc,suby_pc,portE,shared0,ff0e,ff09,ff04,lock,task_fn,task_len,cf810\n";
    }
    int prev_mode = -1;
    auto set_start = [&](bool down) {
        if (machine.start_button(down)) machine.in_general &= (uint16_t)~0x08;
        else machine.in_general |= 0x08;
    };
    for (int i = 0; i < frames; i++) {
        crash_heartbeat();
        if (i == crash_test_frame) {
            // Deliberate access violation after the machine has run, so the
            // report includes live CPU state and the guest trace ring.
            *(volatile int *)(uintptr_t)0 = 1;
        }
        if (push_start) {
            const int mode = (machine.shared[0xE09C] << 8) | machine.shared[0xE09D];
            if (mode < 8 && prev_mode >= 8) {
                machine.start_gate_armed = false;
                machine.start_pending = false;
            }
            if (machine.shared[0xE852] == 0) machine.start_gate_armed = false;
            prev_mode = mode;
        }
        // Deterministic smoke test: coin at frame 60, start at frame 90.
        if (autoStart) {
            if (i == 60) machine.in_general &= (uint16_t)~0x40;
            else if (i == 64) machine.in_general |= 0x40;
            else if (i == 90) set_start(true);
            else if (i == 94) set_start(false);
            else if (i == 300) set_start(true);
            else if (i == 304) set_start(false);
            else if (i == 600) set_start(true);
            else if (i == 604) set_start(false);
            if (i >= 700) machine.in_gas = 0xFF;
        } else if (startOnly) {
            if (i == 60) set_start(true);
            else if (i == 64) set_start(false);
        } else if (coinOnly) {
            if (i == coin_frame) machine.in_general &= (uint16_t)~0x40;
            else if (i == coin_frame + 4) machine.in_general |= 0x40;
        }
        if (fixed_steer >= 0) machine.in_wheel = (uint8_t)std::clamp(fixed_steer, 0x20, 0xE0);
        machine.run_frame();
        if (!wavfile.empty() && machine.audio()) {
            const int16_t *s = machine.audio()->samples();
            const size_t n = machine.audio()->sample_count();
            wav_samples.insert(wav_samples.end(), s, s + n);
        }
        if (likethewind::bsprite_tap && i >= 845 && i <= 900) {
            char name[64];
            std::snprintf(name, sizeof(name), "work/stg_%04d.bin", i);
            std::ofstream sf(name, std::ios::binary);
            sf.write((const char *)machine.suby_local + 0x8000, 0xE00);
        }
        if (trace) {
            if (machine.sample.valid && machine.sample_line >= 0) {
                const Machine::FrameSample &s = machine.sample;
                char line[160];
                std::snprintf(line, sizeof(line),
                              "%d,%06X,%06X,%06X,%02X,%04X,%02X,%02X,%02X,%02X,%08X,%04X,%02X\n",
                              i + 1, s.main_pc, s.subx_pc, s.suby_pc, s.portE, s.shared0,
                              s.ff0e, s.ff09, s.ff04, s.lock, s.task_fn, s.task_len, s.cf810);
                trace << line;
            } else {
            auto w = [&](int off) {
                return (uint16_t)((machine.shared[off] << 8) | machine.shared[off + 1]);
            };
            char line[160];
            std::snprintf(line, sizeof(line),
                          "%d,%06X,%06X,%06X,%02X,%04X,%02X,%02X,%02X,%02X,%08X,%04X,%02X\n",
                          i + 1, machine.cpus[MAIN].state.pc, machine.cpus[SUBX].state.pc,
                          machine.cpus[SUBY].state.pc, machine.io_latch[4],
                          w(0), machine.shared[0xFF0E], machine.shared[0xFF09],
                          machine.shared[0xFF04], machine.suby_local[0x9F0],
                          (uint32_t)((machine.shared[0x10] << 24) | (machine.shared[0x11] << 16) |
                                     (machine.shared[0x12] << 8) | machine.shared[0x13]),
                          (uint16_t)((machine.shared[0x14] << 8) | machine.shared[0x15]),
                          machine.shared[0xF810]);
            trace << line;
            }
        }
        if ((i + 1) % 60 == 0)
            std::printf("frame %d: main pc %06X subx %06X suby %06X "
                        "(held %d/%d, cycles %lld/%lld/%lld)\n", i + 1,
                        machine.cpus[MAIN].state.pc, machine.cpus[SUBX].state.pc,
                        machine.cpus[SUBY].state.pc,
                        (int)machine.cpus[SUBX].reset_held, (int)machine.cpus[SUBY].reset_held,
                        (long long)machine.cpus[MAIN].cycles, (long long)machine.cpus[SUBX].cycles,
                        (long long)machine.cpus[SUBY].cycles);
        if ((i + 1) % 60 == 0)
            std::printf("   main ipl %d irq %d stopped %d (irq2 %llu irq4 %llu); "
                        "subx ipl %d irq2 %llu irq4 %llu; suby ipl %d irq2 %llu irq4 %llu\n",
                        machine.cpus[MAIN].state.ipl, machine.cpus[MAIN].irq,
                        machine.cpus[MAIN].state.stopped,
                        (unsigned long long)machine.irqCount[MAIN][2],
                        (unsigned long long)machine.irqCount[MAIN][4],
                        machine.cpus[SUBX].state.ipl,
                        (unsigned long long)machine.irqCount[SUBX][2],
                        (unsigned long long)machine.irqCount[SUBX][4],
                        machine.cpus[SUBY].state.ipl,
                        (unsigned long long)machine.irqCount[SUBY][2],
                        (unsigned long long)machine.irqCount[SUBY][4]);
        if ((i + 1) % 60 == 0) {
            auto nz = [](const auto &a) {
                size_t n = 0;
                for (auto v : a) if (v) n++;
                return n;
            };
            std::printf("   video: ysprite %zu/%zu bsprite %zu/%zu palette %zu/%zu rotation %zu/%zu\n",
                        nz(machine.ysprite), sizeof(machine.ysprite),
                        nz(machine.bsprite), sizeof(machine.bsprite),
                        nz(machine.palette), sizeof(machine.palette) / sizeof(machine.palette[0]),
                        nz(machine.rotation), sizeof(machine.rotation));
            if (verbose && machine.audio()) {
                size_t an = 0;
                for (size_t k = 0; k < machine.audio()->sample_count(); k++)
                    if (machine.audio()->samples()[k]) an++;
                std::printf("   audio: %zu/%zu non-zero samples\n", an,
                            machine.audio()->sample_count());
            }
        }
    }
    if (!dumpram.empty() && dump_line < 0) {
        machine.dump_ram(dumpram);
    }
    if (!wavfile.empty()) write_wav(wavfile, wav_samples, 48000);
    if (!dumpfb.empty()) {
        auto write_fb = [&](const char *suffix, const uint16_t *fb) {
            std::ofstream f(dumpfb + suffix, std::ios::binary);
            f << "P6\n" << Machine::kRenderWidth << " " << Machine::kVisibleLines << "\n255\n";
            for (int y = 0; y < Machine::kVisibleLines; y++) {
                for (int x = 0; x < Machine::kRenderWidth; x++) {
                    uint8_t c = fb[y * Machine::kRenderWidth + x] == 0xFFFF ? 0 : 255;
                    uint8_t rgb[3] = {c, c, c};
                    f.write((char *)rgb, 3);
                }
            }
        };
        write_fb(".y.ppm", machine.y_fb.data());
        write_fb(".b.ppm", machine.b_fb.data());
    }
    if (!shot.empty()) machine.screenshot_ppm(shot);
    std::printf("shared: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                machine.shared[0], machine.shared[1], machine.shared[2], machine.shared[3],
                machine.shared[4], machine.shared[5], machine.shared[6], machine.shared[7],
                machine.shared[8], machine.shared[9], machine.shared[10], machine.shared[11]);
    report_coverage(machine, coverage_file, verbose);
    return 0;
}
