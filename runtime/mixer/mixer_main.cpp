/*
 * mixer_main.cpp — standalone soundtrack mixer.
 *
 * Loads only the sound-board ROMs (z80.bin + pcm.bin), runs the real Z80 /
 * YM2151 / Sega PCM chain and shows just the SOUND TEST and MIXER pages. Each
 * song's trims, sends and chorus settings are saved in mixes/<id>.cfg.
 */
#include "likethewind/audio.h"
#include "likethewind/menu.h"
#include "likethewind/music_tracks.h"
#include "likethewind/settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef LIKETHEWIND_HAVE_SDL
#define SDL_MAIN_HANDLED
#include <SDL.h>
#endif

namespace {

std::vector<uint8_t> read_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

#ifdef LIKETHEWIND_HAVE_SDL

constexpr const char *kMixDir = "mixes";

std::string mix_path(uint8_t cmd) {
    char b[64];
    std::snprintf(b, sizeof(b), "%s/%02X.cfg", kMixDir, (unsigned)cmd);
    return b;
}

bool menu_key_from_sym(SDL_Keycode sym, likethewind::MenuKey &k) {
    switch (sym) {
    case SDLK_UP: k = likethewind::MenuKey::Up; return true;
    case SDLK_DOWN: k = likethewind::MenuKey::Down; return true;
    case SDLK_LEFT: k = likethewind::MenuKey::Left; return true;
    case SDLK_RIGHT: k = likethewind::MenuKey::Right; return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE: k = likethewind::MenuKey::Enter; return true;
    case SDLK_q:
    case SDLK_PAGEUP: k = likethewind::MenuKey::TabPrev; return true;
    case SDLK_e:
    case SDLK_PAGEDOWN: k = likethewind::MenuKey::TabNext; return true;
    case SDLK_BACKSPACE: k = likethewind::MenuKey::Back; return true;
    case SDLK_PLUS:
    case SDLK_EQUALS:
    case SDLK_KP_PLUS: k = likethewind::MenuKey::Plus; return true;
    case SDLK_MINUS:
    case SDLK_KP_MINUS: k = likethewind::MenuKey::Minus; return true;
    case SDLK_s: k = likethewind::MenuKey::Solo; return true;
    case SDLK_r: k = likethewind::MenuKey::SendSelect; return true;
    default: return false;
    }
}

void apply_audio(likethewind::Audio &audio, const likethewind::Settings &s) {
    audio.set_volumes(s.music_volume, s.sfx_volume, s.engine_volume);
    if (s.ab_original) {
        static const int kUnity8[8] = {100, 100, 100, 100, 100, 100, 100, 100};
        static const int kUnity18[18] = {
            100, 100, 100, 100, 100, 100, 100, 100, 100,
            100, 100, 100, 100, 100, 100, 100, 100, 100};
        static const int kZero8[8] = {};
        static const int kZero18[18] = {};
        audio.set_channel_volumes(kUnity8, kUnity18);
        audio.set_sfx_volumes(kUnity18, kZero18, kZero18);
        audio.set_chorus_sends(kZero8, kZero18);
        audio.set_reverb_sends(kZero8, kZero18);
        audio.set_solo_mask(0);
        audio.set_chorus(false, 15, 50, 12, 0, 30);
        audio.set_reverb(false, 60, 40, 0, 25);
        return;
    }
    audio.set_channel_volumes(s.ym_volume, s.pcm_volume);
    audio.set_sfx_volumes(s.sfx_lane_volume, s.sfx_lane_send,
                          s.sfx_lane_reverb_send);
    audio.set_chorus_sends(s.ym_send, s.pcm_send);
    audio.set_reverb_sends(s.ym_reverb_send, s.pcm_reverb_send);
    audio.set_solo_mask(s.solo_mask);
    audio.set_chorus(s.chorus_enabled, s.chorus_rate, s.chorus_depth, s.chorus_delay,
                     s.chorus_feedback, s.chorus_mix);
    audio.set_reverb(s.reverb_enabled, s.reverb_decay, s.reverb_damping,
                     s.reverb_predelay, s.reverb_mix);
}

void queue_audio(SDL_AudioDeviceID adev, likethewind::Audio &audio,
                 const likethewind::Settings &s, std::vector<int16_t> &scratch) {
    if (!adev) return;
    const size_t n = audio.sample_count();
    if (!n) return;
    const int16_t *samples = audio.samples();
    const int vol =
        s.muted ? 0 : std::clamp(s.volume, 0, likethewind::Settings::kBusMax);
    if (vol != 100) {
        scratch.resize(n);
        for (size_t i = 0; i < n; ++i)
            scratch[i] = (int16_t)std::clamp(
                (int)samples[i] * vol / 100, -32768, 32767);
        SDL_QueueAudio(adev, scratch.data(), (Uint32)(n * sizeof(int16_t)));
    } else {
        SDL_QueueAudio(adev, samples, (Uint32)(n * sizeof(int16_t)));
    }
}

// Send every possible sound-latch command through a fresh driver boot and
// trace the PCM triggers. This finds voice/effect samples that never appear
// during an ordinary song or attract-mode trace.
int scan_latches(const std::vector<uint8_t> &z80,
                 const std::vector<uint8_t> &pcm, bool trace) {
    static const uint8_t boot[] = {0x9C, 0x67, 0xFF, 0x9D,
                                   0x05, 0xFF, 0x94, 0x88, 0xA5};
    likethewind::Audio audio;
    for (int cmd = 0; cmd <= 0xFF; ++cmd) {
        audio.reset(z80, pcm);
        audio.set_trace(false);
        for (uint8_t b : boot) {
            audio.sound_latch(b);
            audio.run_frame();
            audio.run_frame();
        }
        audio.set_trace(trace);
        std::fprintf(stderr, "[scan] cmd=%02X\n", cmd);
        audio.sound_latch((uint8_t)cmd);
        for (int f = 0; f < 30; ++f) audio.run_frame();
    }
    return 0;
}

int run_mixer(int argc, char **argv) {
    std::string romdir = "work/roms";
    int scale = 3;
    int max_frames = 0;
    int start_track = 0x90;
    bool trace = false;
    bool save_mix = true;
    bool headless = false;
    bool fast = false;
    bool scan = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--rom") && i + 1 < argc) {
            romdir = argv[++i];
        } else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) {
            scale = (int)std::strtol(argv[++i], nullptr, 0);
        } else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            max_frames = (int)std::strtol(argv[++i], nullptr, 0);
        } else if (!std::strcmp(argv[i], "--track") && i + 1 < argc) {
            start_track = (int)std::strtol(argv[++i], nullptr, 16);
        } else if (!std::strcmp(argv[i], "--trace")) {
            trace = true;
        } else if (!std::strcmp(argv[i], "--no-save")) {
            save_mix = false;
        } else if (!std::strcmp(argv[i], "--headless")) {
            headless = true;
        } else if (!std::strcmp(argv[i], "--fast")) {
            fast = true;
        } else if (!std::strcmp(argv[i], "--scan-latches")) {
            scan = true;
        } else {
            std::fprintf(stderr,
                         "usage: %s [--rom DIR] [--scale N] [--frames N] "
                         "[--track 90..97] [--trace] [--no-save] "
                         "[--headless] [--fast] [--scan-latches]\n",
                         argv[0]);
            return 2;
        }
    }
    scale = std::clamp(scale, 1, 6);
    if (start_track < 0x90 || start_track > 0x97) start_track = 0x90;
    if (headless) fast = true;
    if (fast && max_frames <= 0) max_frames = 600;

    const std::vector<uint8_t> z80 = read_file(romdir + "/z80.bin");
    const std::vector<uint8_t> pcm = read_file(romdir + "/pcm.bin");
    if (z80.size() < 0x10000 || pcm.empty()) {
        std::fprintf(stderr,
                     "likethewind mixer: missing %s\\z80.bin or %s\\pcm.bin.\n"
                     "Run tools\\prepare_rom.py (or a normal build) first.\n",
                     romdir.c_str(), romdir.c_str());
        return 1;
    }

    if (scan) return scan_latches(z80, pcm, trace);

    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;
    SDL_Texture *tex = nullptr;
    SDL_AudioDeviceID adev = 0;
    if (!headless) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
            std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
        win = SDL_CreateWindow("LikeTheWind Mixer", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, 320 * scale, 224 * scale,
                               SDL_WINDOW_RESIZABLE);
        if (!win) {
            std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        const Uint32 flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
        ren = SDL_CreateRenderer(win, -1, flags);
        if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        if (!ren) {
            std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 1;
        }
        SDL_RenderSetLogicalSize(ren, 320, 224);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, 320, 224);
        if (!tex) {
            std::fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
            SDL_DestroyRenderer(ren);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return 1;
        }

        SDL_AudioSpec want{};
        want.freq = 48000;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        adev = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
        if (adev) SDL_PauseAudioDevice(adev, 0);
    }

    likethewind::Audio audio;
    audio.reset(z80, pcm);
    audio.set_trace(trace);

    likethewind::Settings settings;
    settings.reset_mixer();
    std::error_code ec;
    std::filesystem::create_directories(kMixDir, ec);
    if (ec)
        std::fprintf(stderr, "mixer: cannot create %s: %s\n", kMixDir,
                     ec.message().c_str());

    uint8_t active = (uint8_t)start_track;
    settings.load_mix(mix_path(active));   // missing file keeps the reset defaults

    likethewind::Menu menu(settings, true);
    menu.set_mix_track(likethewind::music_track_name(active));
    menu.toggle();
    for (int i = 0; i < likethewind::kMusicTrackCount; ++i)
        if (likethewind::kMusicTracks[i].cmd == active) menu.set_selection(i);

    // The sound driver needs the same short boot sequence the game sends.
    const uint8_t boot[] = {0x9C, 0x67, 0xFF, 0x9D, 0x05, 0xFF, 0x94, 0x88, 0xA5};
    constexpr int kBootCount = (int)(sizeof(boot) / sizeof(boot[0]));
    int boot_index = 0;
    bool booted = false;

    std::vector<uint32_t> pixels((size_t)320 * 224);
    std::vector<int16_t> audio_scratch;
    const double frame_time = 1.0 / 60.0;
    const Uint64 perf_freq = headless ? 1 : SDL_GetPerformanceFrequency();
    Uint64 last = headless ? 0 : SDL_GetPerformanceCounter();
    double acc = frame_time;
    int frame_count = 0;
    bool running = true;

    auto run_one = [&]() {
        if (!booted) {
            if (boot_index < kBootCount && frame_count >= 40 + boot_index * 2)
                audio.sound_latch(boot[boot_index++]);
            if (boot_index >= kBootCount && frame_count >= 40 + kBootCount * 2) {
                audio.sound_latch(active);
                booted = true;
            }
        }
        apply_audio(audio, settings);
        audio.run_frame();
        queue_audio(adev, audio, settings, audio_scratch);
        ++frame_count;
    };

    if (fast) {
        while (frame_count < max_frames) run_one();
        running = false;
    }

    while (running) {
        if (!headless) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                        continue;
                    }
                    likethewind::MenuKey k;
                    if (!menu_key_from_sym(e.key.keysym.sym, k)) continue;
                    const likethewind::MenuResult r = menu.key(k);
                    if (r.close || r.quit) running = false;
                    if (r.sound_cmd >= 0) {
                        audio.sound_latch((uint8_t)r.sound_cmd);
                        if (r.sound_cmd >= 0x90 && r.sound_cmd <= 0x97 &&
                            r.sound_cmd != active) {
                            if (save_mix) settings.save_mix(mix_path(active));
                            active = (uint8_t)r.sound_cmd;
                            settings.reset_mixer();
                            settings.load_mix(mix_path(active));
                            menu.set_mix_track(
                                likethewind::music_track_name(active));
                        }
                    }
                }
            }
            if (!running) break;
        }

        const Uint64 now = SDL_GetPerformanceCounter();
        acc += (double)(now - last) / (double)perf_freq;
        last = now;
        if (acc > frame_time * 4) acc = frame_time * 4;
        while (acc >= frame_time && running) {
            run_one();
            acc -= frame_time;
            if (max_frames > 0 && frame_count >= max_frames) running = false;
        }

        if (!headless) {
            for (int y = 0; y < 224; ++y) {
                const uint8_t shade = (uint8_t)(10 + y * 14 / 224);
                for (int x = 0; x < 320; ++x)
                    pixels[(size_t)y * 320 + x] =
                        0xFF000000u | ((uint32_t)(shade + 4) << 16) |
                        ((uint32_t)(shade + 8) << 8) | (uint32_t)(shade + 12);
            }
            float ym_levels[8] = {};
            float pcm_levels[16] = {};
            audio.get_levels(ym_levels, pcm_levels);
            menu.set_levels(ym_levels, pcm_levels);
            menu.draw(pixels.data(), 320, 224);

            SDL_UpdateTexture(tex, nullptr, pixels.data(), 320 * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            SDL_RenderPresent(ren);
        }
    }

    if (save_mix) settings.save_mix(mix_path(active));
    if (adev) SDL_CloseAudioDevice(adev);
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    if (!headless) SDL_Quit();
    return 0;
}

#endif // LIKETHEWIND_HAVE_SDL

} // namespace

int main(int argc, char **argv) {
#ifdef LIKETHEWIND_HAVE_SDL
    return run_mixer(argc, argv);
#else
    (void)argc;
    (void)argv;
    std::fprintf(stderr, "likethewind mixer: built without SDL2\n");
    return 1;
#endif
}
