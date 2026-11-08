/*
 * likethewind/settings.h — user-facing options: DIP switches, video, audio, input.
 *
 * The menu edits this struct; the host applies it to the Machine and the SDL
 * window. DIP bits follow MAME's Power Drift ports: SWA is coinage (A in the
 * low nibble, B in the high nibble), SWB is cabinet/demo/credits/continue/
 * difficulty.
 */
#ifndef LIKETHEWIND_SETTINGS_H
#define LIKETHEWIND_SETTINGS_H

#include <cstdint>
#include <string>

namespace likethewind {

struct Settings {
    static constexpr int kChannelMax = 400;   // per-channel mixer headroom
    static constexpr int kBusMax = 200;       // master/music/SFX/engine boost

    // DIP switches (1 = off in the raw port value; the menu stores the decoded
    // option number instead).
    uint8_t coin_a = 15;
    uint8_t coin_b = 15;
    uint8_t cabinet = 2;        // 0 unused, 1 mini upright, 2 upright, 3 moving
    uint8_t demo_sounds = 0;    // 0 on, 1 off
    uint8_t credits = 1;        // 0 2/2, 1 1/1, 2 3/2, 3 2/1
    uint8_t allow_continue = 1; // 0 no, 1 yes
    uint8_t difficulty = 3;     // 0 hardest, 1 hard, 2 easy, 3 normal

    // Video
    // 0 = 4:3, 1 = 16:9, 2 = 21:9, 3 = 18:9, 4 = 19:9, 5 = 20:9.
    // Kept as a small enum-like integer so the settings file stays readable
    // and old widescreen settings can migrate.
    int aspect = 1;
    int scale = 3;
    bool fullscreen = false;    // borderless desktop fullscreen
    // Presentation mode: 60 = crisp board-rate output, 120/144 = interpolate
    // toward that display refresh (the host still runs the board at 60 Hz).
    int frame_rate = 60;
    bool show_fps = false;
    bool show_gear = true;      // draw the gearshift indicator
    int gear_corner = 1;        // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
    bool auto_gear = false;     // shift automatically at the speed threshold

    // Audio
    int audio_preset = 1;       // 0 stock (flat), 1 per-song custom mixes
    int volume = 100;           // master, 0..100
    int music_volume = 100;     // YM2151 + PCM music voices
    int sfx_volume = 100;       // PCM sound-effect voices
    int engine_volume = 100;    // PCM engine/continuous voices
    int ym_volume[8] = {100, 100, 100, 100, 100, 100, 100, 100};
    int pcm_volume[16] = {100, 100, 100, 100, 100, 100, 100, 100,
                          100, 100, 100, 100, 100, 100, 100, 100};
    int ym_send[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int pcm_send[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                        0, 0, 0, 0, 0, 0, 0, 0};
    // Hojo's default SFX trims. The stock mix bypasses these and stays flat.
    int sfx_lane_volume[16] = {130, 130, 130, 250, 250, 110, 130, 110,
                               130, 130, 130, 130, 130, 130, 130, 150};
    int sfx_lane_send[16] = {};
    int sfx_lane_reverb_send[16] = {};
    int ym_reverb_send[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int pcm_reverb_send[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                               0, 0, 0, 0, 0, 0, 0, 0};
    bool muted = false;

    // Post-mix stereo chorus
    bool chorus_enabled = false;
    int chorus_rate = 15;       // tenths of a Hz, 1..50
    int chorus_depth = 50;      // 0..100
    int chorus_delay = 12;      // milliseconds, 5..30
    int chorus_feedback = 0;    // 0..90
    int chorus_mix = 30;        // 0..100

    // Plate reverb on a second channel send
    bool reverb_enabled = false;
    int reverb_decay = 60;      // 0..100
    int reverb_damping = 40;    // 0..100
    int reverb_predelay = 0;    // milliseconds, 0..50
    int reverb_mix = 25;        // 0..100

    // Monitor-only A/B switch; not part of a saved mix.
    bool ab_original = false;

    // Temporary mixer solo (not persisted): bits 0-7 YM 1-8, bits 8-23 PCM 1-16.
    uint32_t solo_mask = 0;

    // System
    bool run_in_menu = true;    // keep the game running behind the menu
    bool autoplay = false;      // simple demo driver for watching the mixer

    // Voice restoration. Emily and Lucy are the two female drivers.
    bool restore_female_voices = true;
    bool restore_additional_voices = true;
    bool special_tour = false;  // host-controlled A-E + Special course tour
    int tour_start_course = 0;  // 0=A .. 4=E; the tour wraps after E

    // Input / game behaviour
    bool push_start = false;    // true: credits wait for the start button
    int steer_deadzone = 18;
    int pedal_deadzone = 4;
    int steer_rate = 8;         // digital steering counts per frame, 1..24
    int pad_steer_rate = 8;     // analog steering slew per frame, 0 = direct
    bool racing_wheel = false;  // direct analog steering, no digital assist
    int centre_rate = 5;        // steering return strength, 0..10
    int steer_rebound = 10;     // counter-steer pulse after crossing centre
    int steer_rebound_speed = 5; // return speed during the rebound, 1..10

    // Gamepad button assignments. These are SDL_GameControllerButton values;
    // -1 means unbound.
    int pad_steer_left = 13;    // D-pad left
    int pad_steer_right = 14;   // D-pad right
    int pad_gas = 0;            // A
    int pad_brake = 1;          // B
    int pad_gear = 3;           // Y
    int pad_start = 6;          // Start
    int pad_coin = 4;           // Back

    // Analog assignments. These are SDL_GameControllerAxis values (-1 = none).
    // Invert selects the negative half of a stick axis for gas/brake.
    int pad_steer_axis = 0;        // left stick X
    int pad_gas_axis = 5;          // right trigger
    bool pad_gas_axis_invert = false;
    int pad_brake_axis = 4;        // left trigger
    bool pad_brake_axis_invert = false;

    int view_width() const {
        switch (aspect) {
        case 0: return 320;
        case 2: return 512;
        case 3: return 448;
        case 4: return 473;
        case 5: return 498;
        default: return 426;
        }
    }

    uint8_t dip_a() const {
        return (uint8_t)(((coin_b & 0x0F) << 4) | (coin_a & 0x0F));
    }
    uint8_t dip_b() const {
        return (uint8_t)(((difficulty & 3) << 6) | ((allow_continue & 1) << 5) |
                         ((credits & 3) << 3) | ((demo_sounds & 1) << 2) |
                         (cabinet & 3));
    }

    void clamp();
    void reset_mixer(bool flat = false); // restore trims/sends/effects to defaults
    void reset_music_mixer(); // music lanes only; keep global SFX/engine lanes
    void reset_gamepad(); // restore the default controller button assignments
    bool load(const std::string &path);
    void save(const std::string &path) const;
    // Per-song mix only (trims, sends and chorus). Used by the mixer build.
    bool load_mix(const std::string &path);
    void save_mix(const std::string &path) const;
};

} // namespace likethewind

#endif
