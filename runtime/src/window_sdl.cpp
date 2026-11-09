/*
 * window_sdl.cpp â€” the native host: SDL2 window, input, tab menu and audio.
 *
 * The machine runs at the board's fixed 60 Hz and the renderer always uses
 * vsync. The video mode selects 60 fps (crisp) or 120/144 fps (cross-faded)
 * presentation; 120/144 present extra interpolated frames without changing the
 * emulated 60 Hz timing.
 */
#include "likethewind/window.h"

#include "likethewind/audio.h"
#include "likethewind/crash.h"
#include "likethewind/machine.h"
#include "likethewind/menu.h"
#include "likethewind/settings.h"

#ifdef LIKETHEWIND_HAVE_SDL
#include <SDL.h>
#include "window_input.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

namespace likethewind {

#ifdef LIKETHEWIND_HAVE_SDL

namespace {
constexpr const char *kSettingsPath = "likethewind-settings.cfg";

uint32_t argb(uint16_t v) {
    uint8_t r = (uint8_t)((((v & 0x0F) << 1) | ((v >> 12) & 1)) * 255 / 31);
    uint8_t g = (uint8_t)(((((v >> 4) & 0x0F) << 1) | ((v >> 13) & 1)) * 255 / 31);
    uint8_t b = (uint8_t)(((((v >> 8) & 0x0F) << 1) | ((v >> 14) & 1)) * 255 / 31);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Host-side "Special Tour" state. The board still owns the race simulation;
// this only chooses the course/stage shown on the course-select screen and
// advances after a completed course. Course stages 0-4 are the normal
// championship, and stage 5 is the hidden special stage for that course.
struct SpecialTourState {
    static constexpr int kCourses = 5;
    static constexpr int kStages = 5;

    int phase = 0;   // 0 = A-E, 1 = Afterburner, 2 = Hang-On, 3 = complete
    int start_course = 0;
    int course = 0;
    int stage = 0;
    int courses_done = 0;
    bool started = false;
    bool race_started = false;
    bool race_finished = false;
    int last_mode = -1;
    std::array<std::array<int8_t, kStages>, kCourses> places{};
    std::array<int8_t, 2> special_places{{-1, -1}};
    bool tracker_ready = false;
    bool results_hold = false;   // keep final totals up until a new game
    bool score_baseline_pending = true;
    int last_score_seen = 0;
    uint64_t total_frames = 0;
    uint64_t total_hundredths = 0;
    uint64_t stage_start_hundredths = 0;
    int64_t total_score = 0;
    int64_t race_start_frame = -1;

    SpecialTourState() { reset(); }

    void reset() {
        phase = 0;
        course = std::clamp(start_course, 0, 4);
        stage = 0;
        courses_done = 0;
        started = false;
        race_started = false;
        race_finished = false;
        last_mode = -1;
        total_frames = 0;
        total_hundredths = 0;
        stage_start_hundredths = 0;
        total_score = 0;
        race_start_frame = -1;
        for (auto &row : places) row.fill(-1);
        special_places.fill(-1);
        tracker_ready = false;
        results_hold = false;
        score_baseline_pending = true;
        last_score_seen = 0;
    }

    bool course_all_first(int c) const {
        if (c < 0 || c >= kCourses) return false;
        for (int8_t place : places[c])
            if (place != 0) return false;
        return true;
    }

};

int player_place(const Machine &machine) {
    const auto word32 = [&](int off) {
        return ((uint32_t)machine.shared[off] << 24) |
               ((uint32_t)machine.shared[off + 1] << 16) |
               ((uint32_t)machine.shared[off + 2] << 8) |
               (uint32_t)machine.shared[off + 3];
    };
    // $CE620 points at the player object; $26(a0) is the race position
    // (0 = first, 11 = last). The finish handler reads the same field.
    const uint32_t ptr = word32(0xE620);
    if (ptr < 0xC0000u || ptr >= 0xD0000u) return -1;
    const size_t off = (size_t)(ptr - 0xC0000u) + 0x26;
    if (off >= sizeof(machine.shared)) return -1;
    return machine.shared[off];
}

int bcd_value(uint8_t value) {
    return ((value >> 4) * 10) + (value & 0x0F);
}

uint64_t game_clock_hundredths(const Machine &machine) {
    // The race clock is the three-byte BCD value at CE8A0: minutes,
    // seconds, hundredths. The HUD builds its TIME line from these bytes.
    const int minutes = bcd_value(machine.shared[0xE8A0]);
    const int seconds = bcd_value(machine.shared[0xE8A1]);
    const int hundredths = bcd_value(machine.shared[0xE8A2]);
    return (uint64_t)minutes * 6000 + (uint64_t)seconds * 100 +
           (uint64_t)hundredths;
}

int game_score(const Machine &machine) {
    // The live score is a 32-bit packed BCD value at CFF60. The HUD score
    // line is generated from it by the sub CPU.
    const uint32_t raw = ((uint32_t)machine.shared[0xFF60] << 24) |
                         ((uint32_t)machine.shared[0xFF61] << 16) |
                         ((uint32_t)machine.shared[0xFF62] << 8) |
                         (uint32_t)machine.shared[0xFF63];
    uint32_t value = raw;
    uint32_t result = 0;
    uint32_t scale = 1;
    for (int i = 0; i < 8; ++i) {
        const uint32_t digit = value & 0x0F;
        if (digit > 9) return (int)raw;  // not BCD; keep the raw value
        result += digit * scale;
        scale *= 10;
        value >>= 4;
    }
    return (int)result;
}

uint16_t be16_at(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

// The HUD uses a table of pre-rendered two-character tiles. Resolve a tile
// address back to its numeric pair so the tracker can follow the exact value
// the HUD is showing, including any score bonus that is still pending display.
int hud_pair_value(const Machine &machine, uint16_t tile, bool high_only) {
    const uint8_t *rom = machine.suby_rom();
    if (!rom || !tile) return -1;
    const int count = high_only ? 10 : 100;
    const int base = high_only ? 0x3F28 : 0x3F48;
    for (int value = 0; value < count; ++value) {
        const int bcd = high_only ? value : ((value / 10) << 4) | (value % 10);
        const uint16_t def = be16_at(rom + base + bcd * 2);
        if (!def) continue;
        if (be16_at(rom + def + 2) == tile) return value;
    }
    return -1;
}

int64_t read_hud_time(const Machine &machine, const uint8_t *sprite_list) {
    int pair[3] = {-1, -1, -1};
    for (int entry = 0; entry < 256; ++entry) {
        const uint8_t *d = sprite_list + entry * 16;
        const uint16_t w0 = be16_at(d);
        const int top = w0 & 0xFF;
        const int bottom = w0 >> 8;
        if (top != 51 || bottom != 57) continue;
        const int screen_x = (be16_at(d + 2) & 0x01FF) - 0xBD;
        int slot = -1;
        if (screen_x == 34) slot = 0;
        else if (screen_x == 52) slot = 1;
        else if (screen_x == 70) slot = 2;
        if (slot < 0) continue;
        int value = hud_pair_value(machine, be16_at(d + 6), true);
        if (value < 0) value = hud_pair_value(machine, be16_at(d + 6), false);
        pair[slot] = value;
    }
    if (pair[1] >= 0 && pair[2] >= 0) {
        const int minutes = pair[0] >= 0 ? pair[0] : 0;
        return (int64_t)minutes * 6000 + pair[1] * 100 + pair[2];
    }
    return -1;
}

std::string format_tour_clock(uint64_t hundredths) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu:%02llu.%02llu",
                  (unsigned long long)(hundredths / 6000),
                  (unsigned long long)((hundredths / 100) % 60),
                  (unsigned long long)(hundredths % 100));
    return buf;
}

// A complete mixer snapshot, so per-song changes can cross-fade instead of
// jumping to the new channel trims. A/B and the Audio Mix preset bypass this
// and apply their target immediately.
struct MixParams {
    int ym_volume[8]{};
    int pcm_volume[16]{};
    int ym_send[8]{};
    int pcm_send[16]{};
    int sfx_volume[Settings::kSfxChannels]{};
    int sfx_send[Settings::kSfxChannels]{};
    int sfx_reverb_send[Settings::kSfxChannels]{};
    int ym_reverb_send[8]{};
    int pcm_reverb_send[16]{};
    bool chorus_enabled = false;
    int chorus_rate = 15;
    int chorus_depth = 50;
    int chorus_delay = 12;
    int chorus_feedback = 0;
    int chorus_mix = 30;
    bool reverb_enabled = false;
    int reverb_decay = 60;
    int reverb_damping = 40;
    int reverb_predelay = 0;
    int reverb_mix = 25;
    uint32_t solo_mask = 0;
};

MixParams stock_mix() {
    MixParams p;
    std::fill(std::begin(p.ym_volume), std::end(p.ym_volume), 100);
    std::fill(std::begin(p.pcm_volume), std::end(p.pcm_volume), 100);
    std::fill(std::begin(p.sfx_volume), std::end(p.sfx_volume), 100);
    return p;
}

MixParams custom_mix(const Settings &s) {
    MixParams p;
    std::copy(std::begin(s.ym_volume), std::end(s.ym_volume), std::begin(p.ym_volume));
    std::copy(std::begin(s.pcm_volume), std::end(s.pcm_volume), std::begin(p.pcm_volume));
    std::copy(std::begin(s.ym_send), std::end(s.ym_send), std::begin(p.ym_send));
    std::copy(std::begin(s.pcm_send), std::end(s.pcm_send), std::begin(p.pcm_send));
    std::copy(std::begin(s.sfx_lane_volume), std::end(s.sfx_lane_volume),
              std::begin(p.sfx_volume));
    std::copy(std::begin(s.sfx_lane_send), std::end(s.sfx_lane_send),
              std::begin(p.sfx_send));
    std::copy(std::begin(s.sfx_lane_reverb_send),
              std::end(s.sfx_lane_reverb_send), std::begin(p.sfx_reverb_send));
    std::copy(std::begin(s.ym_reverb_send), std::end(s.ym_reverb_send),
              std::begin(p.ym_reverb_send));
    std::copy(std::begin(s.pcm_reverb_send), std::end(s.pcm_reverb_send),
              std::begin(p.pcm_reverb_send));
    p.chorus_enabled = s.chorus_enabled;
    p.chorus_rate = s.chorus_rate;
    p.chorus_depth = s.chorus_depth;
    p.chorus_delay = s.chorus_delay;
    p.chorus_feedback = s.chorus_feedback;
    p.chorus_mix = s.chorus_mix;
    p.reverb_enabled = s.reverb_enabled;
    p.reverb_decay = s.reverb_decay;
    p.reverb_damping = s.reverb_damping;
    p.reverb_predelay = s.reverb_predelay;
    p.reverb_mix = s.reverb_mix;
    p.solo_mask = s.solo_mask;
    return p;
}

void apply_mix(Audio *audio, const MixParams &p) {
    if (!audio) return;
    audio->set_channel_volumes(p.ym_volume, p.pcm_volume);
    audio->set_sfx_volumes(p.sfx_volume, p.sfx_send, p.sfx_reverb_send);
    audio->set_chorus_sends(p.ym_send, p.pcm_send);
    audio->set_reverb_sends(p.ym_reverb_send, p.pcm_reverb_send);
    audio->set_solo_mask(p.solo_mask);
    audio->set_chorus(p.chorus_enabled, p.chorus_rate, p.chorus_depth, p.chorus_delay,
                      p.chorus_feedback, p.chorus_mix);
    audio->set_reverb(p.reverb_enabled, p.reverb_decay, p.reverb_damping,
                      p.reverb_predelay, p.reverb_mix);
}

} // namespace

bool run_window(Machine &machine, Settings &settings, int max_frames, bool save_settings,
                bool open_menu) {
    settings.clamp();
    // Audio is optional: a machine with no working audio backend should still
    // be able to open the game window.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    {
        const char *driver = SDL_GetCurrentVideoDriver();
        std::fprintf(stderr, "[video] driver: %s\n",
                     driver ? driver : "(unknown)");
    }
    // If SDL fell back to the headless offscreen backend despite a desktop
    // session, retry the real backends. Prefer X11 when DISPLAY exists because
    // XWayland is available on most Wayland desktops; WAYLAND_DISPLAY alone
    // does not guarantee that SDL's Wayland backend can load libwayland.
    if (SDL_GetCurrentVideoDriver() &&
        std::strcmp(SDL_GetCurrentVideoDriver(), "offscreen") == 0) {
        const char *candidates[2] = {nullptr, nullptr};
        int count = 0;
        if (std::getenv("DISPLAY")) candidates[count++] = "x11";
        if (std::getenv("WAYLAND_DISPLAY")) candidates[count++] = "wayland";
        bool have_desktop_driver = false;
        for (int i = 0; i < count; ++i) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, candidates[i],
                                    SDL_HINT_OVERRIDE);
            if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0) {
                const char *active = SDL_GetCurrentVideoDriver();
                if (active && std::strcmp(active, "offscreen") != 0) {
                    std::fprintf(stderr, "[video] retry driver: %s\n", active);
                    have_desktop_driver = true;
                    break;
                }
            }
            std::fprintf(stderr, "[video] %s retry failed: %s\n",
                         candidates[i], SDL_GetError());
        }
        if (!have_desktop_driver)
            std::fprintf(stderr,
                         "[video] no usable X11/Wayland display server "
                         "(DISPLAY=%s WAYLAND_DISPLAY=%s)\n",
                         std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "",
                         std::getenv("WAYLAND_DISPLAY")
                             ? std::getenv("WAYLAND_DISPLAY")
                             : "");
    }
    bool audio_ready = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;
    if (!audio_ready) {
        // The default probe can land on OSS/dsp on a system without /dev/dsp.
        // Retry the common Linux backends before accepting a silent game.
        static const char *drivers[] = {"pipewire", "pulseaudio", "alsa", "dsp", "dummy"};
        for (const char *driver : drivers) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            SDL_SetHint(SDL_HINT_AUDIODRIVER, driver);
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
                audio_ready = true;
                break;
            }
        }
        if (!audio_ready) {
            std::fprintf(stderr, "[audio] no SDL audio backend: %s\n",
                         SDL_GetError());
            SDL_ClearError();
        }
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    // Optional user mapping database next to the executable/working directory.
    if (SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt") < 0)
        SDL_ClearError();
    SDL_GameControllerEventState(SDL_ENABLE);

    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    SDL_Texture *tex = nullptr;
    std::vector<uint32_t> pixels;
    int cur_scale = -1;
    int cur_aspect = -1;
    int cur_fullscreen = -1;
    int display_hz = 60;
    Menu menu(settings);

    auto recreate_video = [&]() -> bool {
        if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        if (ren) { SDL_DestroyRenderer(ren); ren = nullptr; }
        if (win) { SDL_DestroyWindow(win); win = nullptr; }

        machine.widescreen = settings.aspect != 0;
        machine.view_width = settings.view_width();
        machine.view_x0 =
            -(machine.view_width - Machine::kVisibleWidth) / 2;

        Uint32 window_flags = SDL_WINDOW_RESIZABLE;
        if (settings.fullscreen)
            window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        win = SDL_CreateWindow("LikeTheWind",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               machine.view_width * settings.scale,
                               Machine::kVisibleLines * settings.scale,
                               window_flags);
        if (!win) {
            std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
            return false;
        }
        SDL_ShowWindow(win);
        SDL_RaiseWindow(win);
        {
            const int di = SDL_GetWindowDisplayIndex(win);
            SDL_DisplayMode dm{};
            display_hz = (di >= 0 && SDL_GetDesktopDisplayMode(di, &dm) == 0 &&
                          dm.refresh_rate > 0)
                             ? dm.refresh_rate
                             : 60;
            menu.set_display_hz(display_hz);
            menu.set_present_stats(0.0, 0);
            std::fprintf(stderr, "[video] display %d Hz\n", display_hz);
        }
        const Uint32 flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
        ren = SDL_CreateRenderer(win, -1, flags);
        if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!ren) {
            std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
            return false;
        }
        {
            SDL_RendererInfo info{};
            if (SDL_GetRendererInfo(ren, &info) == 0)
                std::fprintf(stderr, "[video] renderer: %s%s\n", info.name,
                             (info.flags & SDL_RENDERER_PRESENTVSYNC) ? " (vsync)" : "");
        }
        SDL_RenderSetLogicalSize(ren, machine.view_width, Machine::kVisibleLines);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                machine.view_width, Machine::kVisibleLines);
        if (!tex) {
            std::fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
            return false;
        }
        pixels.resize((size_t)machine.view_width * Machine::kVisibleLines);
        cur_scale = settings.scale;
        cur_aspect = settings.aspect;
        cur_fullscreen = settings.fullscreen ? 1 : 0;
        return true;
    };

    if (!recreate_video()) {
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
        return false;
    }

    // Let SDL choose the system default device/rate/format/channels. The
    // engine's fixed output is converted with an SDL_AudioStream below.
    SDL_AudioSpec have{};
    SDL_AudioDeviceID adev = 0;
    SDL_AudioStream *audio_stream = nullptr;
    bool audio_started = false;
    Uint32 audio_start_bytes = 0;
    if (audio_ready) {
        // SDL_OpenAudioDevice dereferences `desired` in some SDL builds, so
        // query the default spec/device first rather than passing NULL.
        SDL_AudioSpec want{};
        char *default_name = nullptr;
        if (SDL_GetDefaultAudioInfo(&default_name, &want, 0) != 0) {
            want.freq = 48000;
            want.format = AUDIO_S16SYS;
            want.channels = 2;
        }
        // The system-reported buffer can be only ~10 ms (441 frames at
        // 44.1 kHz). The game queues one 16.7 ms board frame at a time, so a
        // buffer that small can starve between updates and produce dropouts.
        if (want.samples < 2048) want.samples = 2048;
        adev = SDL_OpenAudioDevice(default_name, 0, &want, &have,
                                    SDL_AUDIO_ALLOW_ANY_CHANGE);
        SDL_free(default_name);
        if (adev) {
            std::fprintf(stderr,
                         "[audio] SDL device opened: %d Hz, %d channel(s), "
                         "format %04X, %d samples\n",
                         have.freq, have.channels, (unsigned)have.format,
                         have.samples);
            if (have.freq != 48000 || have.channels != 2 ||
                have.format != AUDIO_S16SYS) {
                audio_stream = SDL_NewAudioStream(
                    AUDIO_S16SYS, 2, 48000, have.format, have.channels,
                    have.freq);
                if (!audio_stream)
                    std::fprintf(stderr, "[audio] stream conversion failed: %s\n",
                                 SDL_GetError());
            }
            const int bytes_per_frame =
                SDL_AUDIO_BITSIZE(have.format) / 8 * have.channels;
            const double buffer_seconds =
                have.freq > 0 ? (double)have.samples / have.freq : 0.0;
            const double target_seconds =
                std::max(0.04, buffer_seconds + 0.02);
            audio_start_bytes = (Uint32)std::ceil(
                target_seconds * have.freq * bytes_per_frame);
        } else {
            std::fprintf(stderr, "[audio] SDL_OpenAudioDevice failed: %s\n",
                         SDL_GetError());
        }
    }

    SDL_GameController *pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
    }
    if (pad) {
        std::fprintf(stderr, "[input] controller: %s\n", SDL_GameControllerName(pad));
    } else {
        std::fprintf(stderr, "[input] no SDL game controller found (keyboard still works)\n");
    }

    const size_t screen_pixels = (size_t)Machine::kRenderWidth * Machine::kVisibleLines;
    std::vector<uint16_t> prev_screen(screen_pixels, 0);
    std::vector<int16_t> audio_scratch;
    std::vector<uint8_t> audio_converted;
    if (open_menu) {
        machine.run_frame();   // draw one board frame behind the menu
        menu.toggle();
    }
    bool gear = false, prevGear = false;
    SteeringState steer_state;
    int preview_wheel = 0x80;
    SteeringState preview_state;
    int autoplay_frame = 0;
    bool autoplay_was = false;
    int prevMode = -1;
    SpecialTourState tour;
    tour.start_course = settings.tour_start_course;
    tour.reset();
    int tour_start_was = settings.tour_start_course;
    bool tour_was_enabled = settings.special_tour;
    auto force_tour_course = [&](bool with_stage) {
        int course = std::clamp(tour.course, 0, 4);
        int stage = std::clamp(tour.stage, 0, 4);
        if (tour.phase == 1) {
            course = 0;  // Afterburner
            stage = 5;
        } else if (tour.phase == 2) {
            course = 1;  // Hang-On
            stage = 5;
        }
        machine.shared[0xEA2A] = (uint8_t)(course >> 8);
        machine.shared[0xEA2B] = (uint8_t)course;
        if (with_stage) {
            machine.shared[0xEA38] = (uint8_t)(stage >> 8);
            machine.shared[0xEA39] = (uint8_t)stage;
            // Lock course selection only on the course-select frame. The
            // garage/driver screen must keep normal steering input.
            const uint8_t wheel =
                (uint8_t)std::clamp(0x40 + course * 0x20, 0x20, 0xE0);
            machine.in_wheel = wheel;
            machine.shared[0xE0CE] = 0;
            machine.shared[0xE0CF] = wheel;
        }
    };
    auto advance_tour = [&]() {
        if (tour.phase == 0) {
            if (tour.stage < 4) {
                ++tour.stage;
            } else {
                tour.stage = 0;
                ++tour.courses_done;
                if (tour.courses_done < 5) {
                    // The tour wraps: starting on E gives E A B C D.
                    tour.course = (tour.course + 1) % 5;
                } else {
                    // Bonus stages are part of the tour regardless of rank.
                    tour.phase = 1;  // Afterburner
                    tour.course = 0;
                }
            }
        } else if (tour.phase == 1) {
            tour.phase = 2;  // Hang-On
            tour.course = 1;
        } else if (tour.phase == 2) {
            tour.phase = 3;
            tour.course = 4;
            tour.results_hold = true;
        }
    };
    auto observe_score = [&](int score) {
        if (score < 0) return;
        if (tour.score_baseline_pending) {
            // The HUD can still show the previous race's score for a frame
            // or two at the start of a new race. Treat the first sample as a
            // baseline instead of adding it to the tour total.
            tour.last_score_seen = score;
            tour.score_baseline_pending = false;
            return;
        }
        if (score >= tour.last_score_seen) {
            tour.total_score += score - tour.last_score_seen;
        }
        // A smaller value means the board started a new letter/stage and
        // reset its displayed score. Keep the accumulated tour total.
        tour.last_score_seen = score;
    };
    bool running = true;
    int frames = 0;

    const double frame_time = 1.0 / 60.0;
    const Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    double accumulator = frame_time;   // display the first frame immediately
    double interp_age = frame_time;    // time since the newest board frame
    Uint64 rate_start = last;
    int rate_frames = 0;
    double measured_fps = 60.0;
    double measured_present_fps = 0.0;
    int measured_blend_pct = 0;
    int present_frames = 0;
    int blend_frames = 0;
    Uint64 present_rate_start = last;
    Uint64 title_restore_at = 0;
    int last_frame_rate = settings.frame_rate;
    bool sound_test_was_active = false;
    bool sound_test_played = false;
    double paused_audio_acc = 0.0;

    uint8_t mix_song = 0xFF;
    int applied_preset = settings.audio_preset;
    uint8_t sound_test_cmd = 0xFF;
    int last_game_mode = -1;
    double music_fade = 1.0;
    bool music_fade_active = false;


    std::string mix_dir = "mixes";
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(mix_dir, ec) &&
            std::filesystem::is_directory("../mixes", ec))
            mix_dir = "../mixes";
    }
    auto mix_path = [&](uint8_t cmd) {
        char b[96];
        std::snprintf(b, sizeof(b), "%s/%02X.cfg", mix_dir.c_str(), (unsigned)cmd);
        return std::string(b);
    };
    auto save_mix_for = [&](uint8_t cmd) {
        if (cmd >= 0x90 && cmd <= 0x97) settings.save_mix(mix_path(cmd));
    };
    auto load_song_mix = [&](uint8_t cmd) {
        settings.reset_music_mixer();
        settings.load_mix(mix_path(cmd));   // missing file keeps the flat default
        mix_song = cmd;
    };
    auto active_game_song = [&]() -> uint8_t {
        return (machine.last_music_cmd >= 0x90 && machine.last_music_cmd <= 0x97)
                   ? machine.last_music_cmd
                   : 0xFF;
    };
    auto current_mix = [&]() -> MixParams {
        // In Hojo mode the current mixer is used even before a song command,
        // so the attract screen starts with the custom mix rather than stock.
        if (settings.ab_original || settings.audio_preset == 0) return stock_mix();
        return custom_mix(settings);
    };
    auto sync_song_mix = [&]() {
        const int preset = settings.audio_preset;
        const uint8_t song =
            (menu.sound_test() && sound_test_cmd >= 0x90 && sound_test_cmd <= 0x97)
                ? sound_test_cmd
                : active_game_song();
        if (preset != applied_preset) {
            if (applied_preset == 1) save_mix_for(mix_song);
            applied_preset = preset;
            mix_song = 0xFF;
        }
        if (preset == 1 && song != 0xFF && song != mix_song) {
            save_mix_for(mix_song);
            load_song_mix(song);
        } else if (preset == 0) {
            mix_song = 0xFF;
        }
    };

    auto reset_clock = [&]() {
        last = SDL_GetPerformanceCounter();
        accumulator = 0.0;
        interp_age = 0.0;
        // Do not let the paused menu time contaminate the next FPS sample.
        rate_start = last;
        rate_frames = 0;
        present_rate_start = last;
        present_frames = 0;
        blend_frames = 0;
    };

    auto save_settings_file = [&]() {
        if (settings.audio_preset == 1) save_mix_for(mix_song);
        if (save_settings) settings.save(kSettingsPath);
    };

    auto queue_audio = [&]() {
        if (!adev || !machine.audio()) return;
        const size_t n = machine.audio()->sample_count();
        if (!n) return;
        const int16_t *s = machine.audio()->samples();
        const int vol =
            settings.muted ? 0 : std::clamp(settings.volume, 0, Settings::kBusMax);
        const int16_t *source = s;
        if (vol != 100) {
            audio_scratch.resize(n);
            for (size_t i = 0; i < n; i++)
                audio_scratch[i] = (int16_t)std::clamp(
                    (int)s[i] * vol / 100, -32768, 32767);
            source = audio_scratch.data();
        }
        auto start_audio = [&]() {
            const Uint32 queued = SDL_GetQueuedAudioSize(adev);
            if (!audio_started && queued >= audio_start_bytes) {
                SDL_PauseAudioDevice(adev, 0);
                audio_started = true;
                std::fprintf(stderr, "[audio] prebuffered %u bytes\n", queued);
            }
        };
        if (!audio_stream) {
            SDL_QueueAudio(adev, source, (Uint32)(n * sizeof(int16_t)));
            start_audio();
            return;
        }
        const int bytes = (int)(n * sizeof(int16_t));
        if (SDL_AudioStreamPut(audio_stream, source, bytes) != 0) {
            std::fprintf(stderr, "[audio] stream put failed: %s\n",
                         SDL_GetError());
            return;
        }
        int available = SDL_AudioStreamAvailable(audio_stream);
        while (available > 0) {
            audio_converted.resize((size_t)available);
            const int got = SDL_AudioStreamGet(audio_stream,
                                                audio_converted.data(),
                                                available);
            if (got <= 0) break;
            SDL_QueueAudio(adev, audio_converted.data(), (Uint32)got);
            available = SDL_AudioStreamAvailable(audio_stream);
        }
        start_audio();
    };

    auto apply_audio_settings = [&]() {
        if (!machine.audio()) return;
        const double eased = music_fade * music_fade * (3.0 - 2.0 * music_fade);
        const int music_volume =
            (int)std::lround(settings.music_volume * eased);
        machine.audio()->set_volumes(music_volume, settings.sfx_volume,
                                     settings.engine_volume);
        // Voice normalization is part of Hojo's Mix only; the stock mix and
        // the raw A/B comparison stay untouched.
        machine.audio()->set_voice_normalizer(settings.audio_preset == 1 &&
                                              !settings.ab_original &&
                                              machine.player_start_seen);
        const int audio_mode =
            (machine.shared[0xE09C] << 8) | machine.shared[0xE09D];
        machine.audio()->set_attract_active(audio_mode >= 1 && audio_mode < 7);
        machine.audio()->set_race_active(audio_mode >= 20 && audio_mode <= 21);
        apply_mix(machine.audio(), current_mix());
    };

    auto handle_menu = [&](MenuResult r) {
        if (r.sound_cmd >= 0 && machine.audio()) {
            sound_test_played = true;
            if (r.sound_cmd >= 0x90 && r.sound_cmd <= 0x97)
                sound_test_cmd = (uint8_t)r.sound_cmd;
            else if (r.sound_cmd == 0xFF)
                sound_test_cmd = 0xFF;
            machine.audio()->sound_latch((uint8_t)r.sound_cmd);
        }
        if (r.reset) {
            machine.reset();
            machine.dip_a = settings.dip_a();
            machine.dip_b = settings.dip_b();
            if (adev) SDL_ClearQueuedAudio(adev);
            frames = 0;
            rate_frames = 0;
            rate_start = SDL_GetPerformanceCounter();
            reset_clock();
        }
        if (r.close) {
            menu.close();
            save_settings_file();
            reset_clock();
        }
        if (r.quit) running = false;
    };

    int binding_seen = -1;
    bool binding_release = false;
    auto any_gamepad_button = [&]() {
        if (!pad) return false;
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
            if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)b))
                return true;
        return false;
    };
    auto update_binding_state = [&]() {
        const int current = menu.binding_action();
        if (current < 0) {
            binding_seen = -1;
            binding_release = false;
            menu.set_binding_waiting(false);
            return;
        }
        if (current != binding_seen) {
            binding_seen = current;
            binding_release = any_gamepad_button();
            menu.set_binding_waiting(binding_release);
        } else if (binding_release && !any_gamepad_button()) {
            binding_release = false;
            menu.set_binding_waiting(false);
        }
    };

    while (running) {
        const Uint64 loop_start = SDL_GetPerformanceCounter();
        crash_heartbeat();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) {
                const SDL_Keycode sym = e.key.keysym.sym;
                if (sym == SDLK_TAB && !e.key.repeat) {
                    menu.toggle();
                    if (menu.visible()) {
                        preview_wheel = machine.in_wheel;
                        preview_state = {};
                        menu.set_status(machine.frame, measured_fps);
                    } else {
                        save_settings_file();
                    }
                    reset_clock();
                    continue;
                }
                if (sym == SDLK_ESCAPE) {
                    if (menu.visible()) {
                        if (menu.binding_action() >= 0) {
                            menu.cancel_binding();
                            continue;
                        }
                        menu.close();
                        save_settings_file();
                        reset_clock();
                    } else {
                        running = false;
                    }
                    continue;
                }
                // Fallback shortcuts work during play and from the menu.
                if (sym == SDLK_F9 && !e.key.repeat) {
                    machine.reset_coverage();
                    SDL_SetWindowTitle(win, "LikeTheWind - fallback tracking reset");
                    title_restore_at = SDL_GetTicks64() + 2000;
                    std::fprintf(stderr, "[coverage] fallback tracking reset\n");
                    continue;
                }
                if (sym == SDLK_F10 && !e.key.repeat) {
                    const std::string report = machine.fallback_report();
                    const bool wrote = machine.write_coverage("fallback_report.txt");
                    const bool copied = SDL_SetClipboardText(report.c_str()) == 0;
                    const char *action = copied ? "copied" : wrote ? "saved" : "failed";
                    char title[160];
                    std::snprintf(title, sizeof(title),
                                  "LikeTheWind - fallback report %s (%zu PCs)",
                                  action, machine.fallback_unique_pcs());
                    SDL_SetWindowTitle(win, title);
                    title_restore_at = SDL_GetTicks64() + 3000;
                    std::fprintf(stderr,
                                 "[coverage] fallback report %s (%zu PCs, %llu instructions)\n",
                                 action, machine.fallback_unique_pcs(),
                                 (unsigned long long)machine.fallback_steps());
                    continue;
                }
                if (!e.key.repeat && sym >= SDLK_F5 && sym <= SDLK_F8) {
                    static const char *markers[] = {"passing", "wallhit", "enemyhit",
                                                    "announcer"};
                    machine.log_event_marker(markers[sym - SDLK_F5]);
                    std::fprintf(stderr, "[event] %s at frame %lld\n",
                                 markers[sym - SDLK_F5], (long long)machine.frame);
                    continue;
                }
                if (menu.visible()) {
                    MenuKey k;
                    if (menu_key_from_sym(sym, k)) handle_menu(menu.key(k));
                    continue;
                }
            }
            if (e.type == SDL_CONTROLLERAXISMOTION) {
                if (menu.visible() && menu.binding_axis()) {
                    if (!binding_release &&
                        std::abs(e.caxis.value) >= 12000) {
                        menu.bind_axis(e.caxis.axis, e.caxis.value);
                        save_settings_file();
                    }
                    continue;
                }
            }
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                if (menu.visible() && menu.binding_action() >= 0) {
                    if (!binding_release) {
                        menu.bind_button(e.cbutton.button);
                        save_settings_file();
                    }
                    continue;
                }
                if (pad) {
                    const bool lb =
                        e.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
                    const bool rb =
                        e.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
                    if ((lb && SDL_GameControllerGetButton(
                                   pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) ||
                        (rb && SDL_GameControllerGetButton(
                                   pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))) {
                        menu.toggle();

                        if (menu.visible()) {
                            preview_wheel = machine.in_wheel;
                            preview_state = {};
                            menu.set_status(machine.frame, measured_fps);
                        } else {
                            save_settings_file();
                        }
                        reset_clock();
                        continue;
                    }
                }
            }
            if (menu.visible() && e.type == SDL_CONTROLLERBUTTONDOWN) {
                MenuKey k;
                if (menu_key_from_button(e.cbutton.button, k)) handle_menu(menu.key(k));
                continue;
            }
            if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) {
                pad = SDL_GameControllerOpen(e.cdevice.which);
                if (pad) std::fprintf(stderr, "[input] controller added: %s\n",
                                      SDL_GameControllerName(pad));
            }
            if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad &&
                e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                SDL_GameControllerClose(pad);
                pad = nullptr;
                std::fprintf(stderr, "[input] controller removed\n");
            }
        }
        update_binding_state();
        if (!running) break;
        if (title_restore_at && SDL_GetTicks64() >= title_restore_at) {
            SDL_SetWindowTitle(win, "LikeTheWind");
            title_restore_at = 0;
        }

        // Settings that can change while the menu is open.
        machine.dip_a = settings.dip_a();
        machine.dip_b = settings.dip_b();
        machine.start_gate = settings.push_start;
        machine.set_voice_restoration(settings.restore_female_voices,
                                      settings.restore_additional_voices);
        {
            const int mode = (machine.shared[0xE09C] << 8) | machine.shared[0xE09D];
            if (mode < 8 && prevMode >= 8) {
                machine.start_gate_armed = false;
                machine.start_pending = false;
                // A demo/attract replay is not an active player game.
                machine.player_start_seen = false;
            }
            if (machine.shared[0xE852] == 0) machine.start_gate_armed = false;
            prevMode = mode;
        }
        if (settings.aspect != cur_aspect || settings.scale != cur_scale ||
            (settings.fullscreen ? 1 : 0) != cur_fullscreen) {
            if (!recreate_video()) { running = false; break; }
            if (menu.visible()) machine.render_frame();
        }

        const bool sound_test_now = menu.sound_test();
        if (sound_test_was_active && !sound_test_now) {
            if (sound_test_played && machine.audio())
                machine.audio()->sound_latch(machine.last_music_cmd);
            sound_test_played = false;
            sound_test_cmd = 0xFF;
        }
        sound_test_was_active = sound_test_now;
        sync_song_mix();

        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        const Uint64 now = SDL_GetPerformanceCounter();
        const bool menu_up = menu.visible();
        const bool paused = menu_up && !settings.run_in_menu;
        const double dt = (double)(now - last) / (double)perf_freq;
        if (paused) {
            last = now;
            accumulator = 0.0;
            interp_age = 0.0;
            // When the game is paused behind the menu, silence the game audio
            // too. The sound-test page is the exception: it needs the sound
            // board running so the selected track can be auditioned.
            if (machine.audio() && menu.sound_test()) {
                paused_audio_acc += dt;
                if (paused_audio_acc > frame_time * 4) paused_audio_acc = frame_time * 4;
                while (paused_audio_acc >= frame_time) {
                    apply_audio_settings();
                    machine.audio()->run_frame();
                    queue_audio();
                    paused_audio_acc -= frame_time;
                }
            } else {
                paused_audio_acc = 0.0;
                if (adev) SDL_ClearQueuedAudio(adev);
                if (audio_stream) SDL_AudioStreamClear(audio_stream);
            }
        } else {
            accumulator += dt;
            interp_age += dt;
            last = now;
            paused_audio_acc = 0.0;
            // Never let a stall cause a burst of game frames.
            if (accumulator > frame_time * 4) accumulator = frame_time * 4;
        }

        while (!paused && accumulator >= frame_time && running) {
            apply_input(machine, keys, pad, gear, prevGear, steer_state, settings,
                        menu.visible());
            if (settings.autoplay) {
                if (!autoplay_was) autoplay_frame = 0;
                ++autoplay_frame;

                const int mode = (machine.shared[0xE09C] << 8) |
                                 machine.shared[0xE09D];
                if (mode <= 8) {
                    // Auto-insert one credit and advance the attract/start
                    // screens, matching the deterministic auto-start path.
                    // Mode 8 is the course-select screen, not the race.
                    uint8_t g = 0xDF;
                    if (autoplay_frame >= 60 && autoplay_frame < 64) g &= (uint8_t)~0x40;
                    if (autoplay_frame >= 90 && autoplay_frame < 94) g &= (uint8_t)~0x08;
                    if (autoplay_frame >= 300 && autoplay_frame < 304) g &= (uint8_t)~0x08;
                    if (autoplay_frame >= 600 && autoplay_frame < 604) g &= (uint8_t)~0x08;
                    if (autoplay_frame == 90 || autoplay_frame == 300 ||
                        autoplay_frame == 600)
                        machine.player_start_seen = true;
                    machine.in_general = g;
                    machine.in_wheel = 0x80;
                    machine.in_gas = 0;
                } else {
                    // Keep the car moving and make gentle steering changes so
                    // the mixer has live engine/SFX activity.
                    machine.in_gas = 255;
                    const int target =
                        0x80 + (int)(std::sin(autoplay_frame * 0.035) * 0x28);
                    machine.in_wheel =
                        (uint8_t)std::clamp(target, 0x20, 0xE0);
                }
            }
            autoplay_was = settings.autoplay;

            if (settings.tour_start_course != tour_start_was) {
                tour.start_course = settings.tour_start_course;
                tour.reset();
                tour_start_was = settings.tour_start_course;
            }
            if (settings.special_tour != tour_was_enabled) {
                tour.start_course = settings.tour_start_course;
                tour.reset();
                tour_was_enabled = settings.special_tour;
            }

            const int game_mode =
                (machine.shared[0xE09C] << 8) | machine.shared[0xE09D];
            if (settings.special_tour && tour.started &&
                game_mode >= 22 && game_mode <= 25) {
                // The finish bonus can land after the mode-21 snapshot; pick
                // it up before leaving the result screen. Use the live BCD
                // score, not the HUD tiles: the HUD can lag the score by a
                // few frames and catch up with a large apparent jump.
                observe_score(game_score(machine));
                crash_note("post-race mode=%d phase=%d course=%d stage=%d "
                           "done=%d",
                           game_mode, tour.phase, tour.course, tour.stage,
                           tour.courses_done);
                bool start_stage = false;
                if (tour.phase == 3) {
                    // Tour complete: let the board finish normally.
                } else if (tour.phase == 0 && tour.stage < 4) {
                    // A finish below 3rd normally ends the credit. Continue
                    // into the next stage using the board's own next-stage
                    // mode so the clock, score and music keep running.
                    ++tour.stage;
                    start_stage = true;
                } else {
                    advance_tour();
                    if (tour.phase == 0) {
                        // Next course letter: back to course select.
                        crash_note("next-course course=%d done=%d",
                                   tour.course, tour.courses_done);
                        force_tour_course(true);
                        machine.shared[0xE09C] = 0;
                        machine.shared[0xE09D] = 8;
                    } else if (tour.phase == 1 || tour.phase == 2) {
                        // Hidden course: enter it the same way the board
                        // enters its own special stage.
                        start_stage = true;
                    }
                }
                if (start_stage) {
                    crash_note("start-stage phase=%d course=%d stage=%d",
                               tour.phase, tour.course, tour.stage);
                    force_tour_course(true);
                    if (tour.phase == 1 || tour.phase == 2) {
                        machine.shared[0xE314] = 0x00;
                        machine.shared[0xE315] = 0x80;
                    }
                    machine.shared[0xFF2D] &= (uint8_t)~0x80;
                    machine.shared[0xE865] = 0;
                    machine.shared[0xE312] = 0;
                    machine.shared[0xE09C] = 0;
                    machine.shared[0xE09D] = 0x0A;
                }
                tour.race_started = false;
                tour.race_finished = false;
            }
            if (settings.special_tour && tour.started && tour.phase < 3 &&
                game_mode >= 8 && game_mode <= 17) {
                // Keep the course latched through the car-select and garage
                // transition. Only mode 8 resets the stage; later transition
                // modes must preserve the stage the board has advanced to.
                force_tour_course(game_mode == 8);
            }
            if (last_game_mode == 8 && game_mode > 8) {
                // The course-select theme hands over to the course music.
                // Ease the new track in instead of snapping it on.
                music_fade = 0.0;
                music_fade_active = true;
            }
            if (music_fade_active) {
                music_fade = std::min(1.0, music_fade + 1.0 / (0.8 * 60.0));
                if (music_fade >= 1.0) music_fade_active = false;
            }
            last_game_mode = game_mode;

            apply_audio_settings();
            std::copy(machine.screen.begin(), machine.screen.end(), prev_screen.begin());
            machine.run_frame();
            interp_age = 0.0;
            queue_audio();

            const int mode_after =
                (machine.shared[0xE09C] << 8) | machine.shared[0xE09D];
            if (settings.special_tour) {
                // A board reset returns to the attract loop. Start a fresh
                // tour rather than resuming at the course we last forced.
                // Once the tour is complete, keep the final totals up through
                // attract/insert-coin and clear them only when a new game
                // reaches course select.
                if (mode_after <= 2 && tour.last_mode >= 7 &&
                    !tour.results_hold) {
                    tour.start_course = settings.tour_start_course;
                    tour.reset();
                    machine.player_start_seen = false;
                }
                if (tour.results_hold && mode_after == 8) {
                    tour.start_course = settings.tour_start_course;
                    tour.reset();
                }

                // The first course choice happens on the normal course-select
                // screen. Once the player starts it and enters the garage,
                // capture that course as the tour origin.
                if (mode_after >= 9 && !tour.started &&
                    machine.player_start_seen) {
                    const int selected_course =
                        (machine.shared[0xEA2A] << 8) | machine.shared[0xEA2B];
                    tour.start_course = std::clamp(selected_course, 0, 4);
                    tour.course = tour.start_course;
                    const int game_stage =
                        (machine.shared[0xEA38] << 8) | machine.shared[0xEA39];
                    if (game_stage >= 0 && game_stage < 5)
                        tour.stage = game_stage;
                    tour.started = true;
                    tour.tracker_ready = true;
                }

                if (mode_after == 10 && tour.started && tour.race_finished) {
                    // The board advanced to the next stage itself. Keep the
                    // tour's stage counter in step and pick up any score
                    // adjustment that landed after the finish screen.
                    const int game_stage =
                        (machine.shared[0xEA38] << 8) | machine.shared[0xEA39];
                    if (tour.phase == 0 && game_stage >= 0 && game_stage < 5)
                        tour.stage = game_stage;
                    observe_score(game_score(machine));
                    tour.race_started = false;
                    tour.race_finished = false;
                }
                if (mode_after == 20 && tour.started && tour.last_mode != 20) {
                    const int game_stage =
                        (machine.shared[0xEA38] << 8) | machine.shared[0xEA39];
                    if (tour.phase == 0 && game_stage >= 0 && game_stage < 5)
                        tour.stage = game_stage;
                    const int64_t hud_start =
                        read_hud_time(machine, machine.bsprite);
                    tour.stage_start_hundredths =
                        hud_start >= 0 ? (uint64_t)hud_start
                                       : game_clock_hundredths(machine);
                    tour.race_started = true;
                    tour.race_finished = false;
                    tour.score_baseline_pending = true;
                    tour.race_start_frame = machine.frame;
                }
                if (mode_after == 21 && tour.started && tour.race_started &&
                    !tour.race_finished) {
                    tour.race_finished = true;
                    uint64_t stage_frames = 0;
                    if (tour.race_start_frame >= 0) {
                        stage_frames =
                            (uint64_t)(machine.frame - tour.race_start_frame);
                        tour.total_frames += stage_frames;
                        tour.race_start_frame = -1;
                    }
                    const int64_t hud_clock =
                        read_hud_time(machine, machine.bsprite);
                    const uint64_t clock =
                        hud_clock >= 0 ? (uint64_t)hud_clock
                                       : game_clock_hundredths(machine);
                    if (clock > 0) {
                        const uint64_t duration =
                            clock >= tour.stage_start_hundredths
                                ? clock - tour.stage_start_hundredths
                                : clock;
                        tour.total_hundredths += duration;
                    } else {
                        tour.total_hundredths += stage_frames * 100 / 60;
                    }

                    // The live BCD score is authoritative. The HUD tiles lag
                    // and can briefly show an older value, which makes delta
                    // accumulation count the catch-up as new points.
                    const int score = game_score(machine);
                    observe_score(score);
                    crash_note("finish course=%d stage=%d place=%d "
                               "total=%llu score=%d",
                               tour.course, tour.stage, player_place(machine),
                               (unsigned long long)tour.total_hundredths,
                               score);

                    if (tour.phase == 0) {
                        const int course =
                            (machine.shared[0xEA2A] << 8) | machine.shared[0xEA2B];
                        const int stage =
                            (machine.shared[0xEA38] << 8) | machine.shared[0xEA39];
                        if (course >= 0 && course < SpecialTourState::kCourses &&
                            stage >= 0 && stage < SpecialTourState::kStages) {
                            tour.places[course][stage] =
                                (int8_t)player_place(machine);
                            // Preserve the original game's per-course
                            // special-stage unlock, but let the tour decide
                            // when the hidden stages are played.
                            if (tour.course_all_first(course)) {
                                machine.shared[0xF816] = 1;
                                machine.shared[0xF819] = 1;
                            }
                        }
                    } else if (tour.phase == 1 || tour.phase == 2) {
                        tour.special_places[tour.phase - 1] =
                            (int8_t)player_place(machine);
                    }
                }
                tour.last_mode = mode_after;
            }

            accumulator -= frame_time;
            frames++;
            rate_frames++;
            if (rate_frames >= 60) {
                const Uint64 now2 = SDL_GetPerformanceCounter();
                const double secs = (double)(now2 - rate_start) / (double)perf_freq;
                measured_fps = rate_frames / secs;
                if (settings.show_fps && frames % 300 == 0)
                    std::fprintf(stderr,
                                 "[timing] %.2f game fps (%.1f ms), %.1f present fps, "
                                 "%d%% blended\n",
                                 measured_fps, secs * 1000.0 / rate_frames,
                                 measured_present_fps, measured_blend_pct);
                rate_start = now2;
                rate_frames = 0;
            }
            if (max_frames > 0 && frames >= max_frames) running = false;
        }
        if (menu.visible()) {
            const int steer_axis_value =
                controller_axis(pad, settings.pad_steer_axis);
            preview_wheel = update_wheel(preview_wheel, preview_state,
                                         steer_axis_value, false, false,
                                         settings);
            menu.set_steer(steer_axis_value, preview_wheel);
            menu.set_status(machine.frame, measured_fps);
            menu.set_coverage(machine.fallback_steps(), machine.fallback_unique_pcs(),
                              machine.translated_percent(),
                              machine.translated_unique_routines(),
                              machine.translated_routine_total());
            float ym_levels[8] = {};
            float pcm_levels[16] = {};
            float sfx_levels[Settings::kSfxChannels] = {};
            if ((!paused || menu.sound_test()) && machine.audio()) {
                machine.audio()->get_levels(ym_levels, pcm_levels);
                machine.audio()->get_sfx_levels(sfx_levels);
            }
            menu.set_levels(ym_levels, pcm_levels);
            menu.set_sfx_levels(sfx_levels);
        }

        // The loop is paced below to the selected presentation rate, so every
        // iteration presents exactly one frame. In 60 FPS mode this keeps the
        // output at 60 presents even on a 120/144 Hz display instead of
        // repeating the newest board frame at the display refresh.
        const bool do_present = true;

        if (settings.frame_rate != last_frame_rate) {
            last_frame_rate = settings.frame_rate;
            menu.set_present_stats(0.0, 0);
            present_frames = 0;
            blend_frames = 0;
            present_rate_start = SDL_GetPerformanceCounter();
        }

        bool blended = false;
        if (do_present) {
            double alpha = 1.0;
            if (!menu_up && settings.frame_rate > 60)
                alpha = interp_age / frame_time;
            if (frames <= 1) alpha = 1.0;
            alpha = std::clamp(alpha, 0.0, 1.0);
            const int ia = (int)(alpha * 256.0);
            // Ignore tiny timing remainders: a near-0 alpha is a pure frame,
            // not a visible cross-fade.
            blended = (ia >= 16 && ia <= 240);
            for (int y = 0; y < Machine::kVisibleLines; y++) {
                for (int x = 0; x < machine.view_width; x++) {
                    const size_t si = (size_t)y * Machine::kRenderWidth + x;
                    const uint16_t prev_idx = prev_screen[si];
                    const uint16_t cur_idx = machine.screen[si];
                    uint32_t ca = argb(machine.palette[prev_idx & 0x3FFF]);
                    uint32_t cb = argb(machine.palette[cur_idx & 0x3FFF]);
                    if (ia >= 256) {
                        pixels[(size_t)y * machine.view_width + x] = cb;
                    } else {
                        uint32_t r = (((ca >> 16) & 0xFF) * (256 - ia) +
                                      ((cb >> 16) & 0xFF) * ia) >> 8;
                        uint32_t g = (((ca >> 8) & 0xFF) * (256 - ia) +
                                      ((cb >> 8) & 0xFF) * ia) >> 8;
                        uint32_t b = ((ca & 0xFF) * (256 - ia) + (cb & 0xFF) * ia) >> 8;
                        pixels[(size_t)y * machine.view_width + x] =
                            0xFF000000u | (r << 16) | (g << 8) | b;
                    }
                }
            }
            if (!menu_up)
                menu.draw_gear(pixels.data(), machine.view_width, Machine::kVisibleLines,
                               (machine.in_general & 0x20) != 0);
            if (menu_up) menu.draw(pixels.data(), machine.view_width, Machine::kVisibleLines);
            const int mode_now =
                (machine.shared[0xE09C] << 8) | machine.shared[0xE09D];
            static int note_mode = -1;
            if (mode_now != note_mode) {
                crash_note("mode=%d tour phase=%d course=%d stage=%d "
                           "done=%d elapsed=%llu",
                           mode_now, tour.phase, tour.course, tour.stage,
                           tour.courses_done,
                           (unsigned long long)tour.total_hundredths);
                note_mode = mode_now;
            }
            if (!menu_up && settings.special_tour && tour.started &&
                tour.tracker_ready &&
                ((mode_now >= 8 && mode_now <= 25) || tour.results_hold)) {
                uint64_t current_hundredths = 0;
                uint64_t total_hundredths = tour.total_hundredths;
                if (tour.race_start_frame >= 0) {
                    const int64_t hud_clock =
                        read_hud_time(machine, machine.bsprite);
                    if (hud_clock >= 0) {
                        current_hundredths = (uint64_t)hud_clock;
                    } else {
                        const uint64_t current_clock =
                            game_clock_hundredths(machine);
                        if (current_clock > 0)
                            current_hundredths = current_clock;
                        else
                            current_hundredths =
                                (uint64_t)(machine.frame -
                                           tour.race_start_frame) *
                                100 / 60;
                    }
                    if (current_hundredths < tour.stage_start_hundredths)
                        tour.stage_start_hundredths = current_hundredths;
                    const uint64_t duration =
                        current_hundredths >= tour.stage_start_hundredths
                            ? current_hundredths - tour.stage_start_hundredths
                            : current_hundredths;
                    total_hundredths += duration;
                    observe_score(game_score(machine));
                }
                // Two compact lines sized to the portrait strip: placements
                // and the two special stages on top; total tour time and
                // score underneath.
                const std::string stats_line =
                    "TIME " + format_tour_clock(total_hundredths) +
                    "  SCORE " + std::to_string(tour.total_score);
                menu.draw_tour_tracker(pixels.data(), machine.view_width,
                                       Machine::kVisibleLines, tour.places,
                                       tour.special_places, tour.start_course,
                                       stats_line);
            }
            if (!menu_up && settings.special_tour && tour.started &&
                mode_now >= 8 && mode_now < 20 &&
                (machine.frame / 30) % 2 == 0) {
                menu.draw_tour_notice(pixels.data(), machine.view_width,
                                      Machine::kVisibleLines,
                                      "SPECIAL TOUR MODE ENABLED");
            }
            if (settings.show_fps && !menu_up)
                menu.draw_fps(pixels.data(), machine.view_width, Machine::kVisibleLines,
                              measured_fps, measured_present_fps, measured_blend_pct);

            SDL_UpdateTexture(tex, nullptr, pixels.data(), machine.view_width * 4);
        }

        if (do_present) {
            present_frames++;
            if (blended) blend_frames++;
        }
        const Uint64 stats_now = SDL_GetPerformanceCounter();
        const double stats_elapsed =
            (double)(stats_now - present_rate_start) / (double)perf_freq;
        if (stats_elapsed >= 1.0) {
            measured_present_fps = present_frames / stats_elapsed;
            measured_blend_pct =
                present_frames ? 100 * blend_frames / present_frames : 0;
            menu.set_present_stats(measured_present_fps, measured_blend_pct);
            present_rate_start = stats_now;
            present_frames = 0;
            blend_frames = 0;
        }

        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);

        // Pace the loop to the selected output rate. Without this, 60 FPS mode
        // would present once per display refresh (e.g. 144 duplicates/second)
        // because RenderPresent only waits for the next vblank.
        // Pace to the selected mode, not the reported desktop refresh. SDL
        // can report 60 Hz for a window on a high-refresh desktop (remote
        // sessions, mixed-Hz monitors, and some compositor modes); the
        // renderer's vsync already caps us at the real refresh. Keep the
        // settings overlay at the selected rate too, so its PRESENT reading
        // does not misleadingly show 60 while the menu is open. Outside the
        // race itself, use 60 Hz: the garage and rotating course-load screens
        // are sprite-heavy and do not benefit from extra interpolated frames.
        const int playback_mode =
            (machine.shared[0xE09C] << 8) | machine.shared[0xE09D];
        const bool racing = (playback_mode >= 20 && playback_mode <= 21);
        const int target_hz =
            std::max(1, (menu_up || racing) ? settings.frame_rate : 60);
        const double target_interval = 1.0 / target_hz;
        const Uint64 deadline =
            loop_start + (Uint64)(target_interval * (double)perf_freq);
        for (;;) {
            const Uint64 now = SDL_GetPerformanceCounter();
            if (now >= deadline) break;
            const double remain_ms =
                (double)(deadline - now) * 1000.0 / (double)perf_freq;
            if (remain_ms > 1.5)
                SDL_Delay((Uint32)(remain_ms - 1.0));
            // The last millisecond is a short spin so SDL's millisecond timer
            // resolution does not pull 60 FPS down to the mid-50s.
        }
    }

    save_settings_file();
    if (audio_stream) SDL_FreeAudioStream(audio_stream);
    if (adev) SDL_CloseAudioDevice(adev);
    if (pad) SDL_GameControllerClose(pad);
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
    return true;
}

#else

bool run_window(Machine &, Settings &, int, bool, bool) {
    std::fprintf(stderr, "this build has no SDL2 window; rebuild with vendor/SDL2 present\n");
    return false;
}

#endif

} // namespace likethewind
