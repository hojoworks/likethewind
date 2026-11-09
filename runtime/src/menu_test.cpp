#include "likethewind/menu_test.h"
#include "likethewind/machine.h"
#include "likethewind/menu.h"
#include "likethewind/settings.h"
#include "window_input.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace likethewind;

int inputtest() {
    Machine machine;
    Settings settings;
    bool gear = false, prev_gear = false;

    auto course_step = [&](int wheel, SDL_Scancode key) {
        SteeringState state;
        Uint8 keys[SDL_NUM_SCANCODES] = {};
        machine.shared[0xE09C] = 0;
        machine.shared[0xE09D] = 8;
        machine.in_wheel = (uint8_t)wheel;
        keys[key] = 1;
        apply_input(machine, keys, nullptr, gear, prev_gear, state, settings,
                    false);
        return (int)machine.in_wheel;
    };
    const bool course_left = course_step(0x80, SDL_SCANCODE_LEFT) == 0x60 &&
                             course_step(0x60, SDL_SCANCODE_LEFT) == 0x40;
    const bool course_right = course_step(0x80, SDL_SCANCODE_RIGHT) == 0xA0 &&
                              course_step(0xA0, SDL_SCANCODE_RIGHT) == 0xC0;
    const bool course_detent = course_step(0x90, SDL_SCANCODE_LEFT) == 0x60 &&
                               course_step(0x90, SDL_SCANCODE_RIGHT) == 0xA0;

    Machine entry_machine;
    SteeringState entry_state;
    Uint8 keys[SDL_NUM_SCANCODES] = {};
    entry_machine.shared[0xE09C] = 0;
    entry_machine.shared[0xE09D] = 0x1B;
    entry_machine.in_wheel = 0x80;
    auto entry_step = [&](SDL_Scancode key) {
        std::fill(keys, keys + SDL_NUM_SCANCODES, 0);
        keys[key] = 1;
        apply_input(entry_machine, keys, nullptr, gear, prev_gear, entry_state,
                    settings, false);
        const int result = (int)entry_machine.in_wheel;
        std::fill(keys, keys + SDL_NUM_SCANCODES, 0);
        apply_input(entry_machine, keys, nullptr, gear, prev_gear, entry_state,
                    settings, false);
        return result;
    };
    const bool entry_right = entry_step(SDL_SCANCODE_RIGHT) == 0x86 &&
                             entry_step(SDL_SCANCODE_RIGHT) == 0x89;
    const bool entry_left = entry_step(SDL_SCANCODE_LEFT) == 0x86 &&
                            entry_step(SDL_SCANCODE_LEFT) == 0x83;

    std::printf("inputtest: course-select one course per press %s\n",
                course_left && course_right && course_detent ? "PASS"
                                                             : "FAIL");
    std::printf("inputtest: name-entry one symbol per press %s\n",
                entry_right && entry_left ? "PASS" : "FAIL");
    return course_left && course_right && course_detent && entry_right &&
                   entry_left
               ? 0
               : 1;
}

int menutest() {
    Settings settings;
    Menu menu(settings);
    menu.toggle();
    menu.set_status(1234, 60.0);
    bool gamepad_binding_ok = true;
    bool steering_setup_ok = true;
    std::vector<uint32_t> px((size_t)Machine::kVisibleWidth * Machine::kVisibleLines);
    for (int tab = 0; tab < 6; tab++) {
        if (tab == 0) {
            // Exercise PUSH START and RACING WHEEL, then bind and reset a pad
            // button.
            menu.key(MenuKey::Right);
            menu.key(MenuKey::Down);    // RACING WHEEL
            menu.key(MenuKey::Right);
            if (!settings.racing_wheel) gamepad_binding_ok = false;
            menu.key(MenuKey::Left);    // back to normal gamepad mode
            menu.key(MenuKey::Down);    // CONTROLLER BINDINGS
            menu.key(MenuKey::Enter);
            menu.key(MenuKey::Enter);
            menu.bind_button(2);   // SDL_CONTROLLER_BUTTON_X
            if (settings.pad_steer_left != 2) gamepad_binding_ok = false;
            for (int i = 0; i < 11; ++i) menu.key(MenuKey::Down);
            menu.key(MenuKey::Enter);   // RESET DEFAULTS
            if (settings.pad_steer_left != 13) gamepad_binding_ok = false;
            menu.key(MenuKey::Back);
            for (int i = 0; i < 3; ++i) menu.key(MenuKey::Down);
            menu.key(MenuKey::Enter);   // STEERING SETUP
            menu.key(MenuKey::Down);    // DIGITAL STEER RATE
            menu.key(MenuKey::Right);
            if (settings.steer_rate != 9) steering_setup_ok = false;
            menu.key(MenuKey::Down);    // ANALOG STEER RATE
            menu.key(MenuKey::Right);
            if (settings.pad_steer_rate != 9) steering_setup_ok = false;
            for (int i = 0; i < 3; ++i) menu.key(MenuKey::Down);
            menu.key(MenuKey::Right);
            if (settings.steer_rebound_speed != 6) steering_setup_ok = false;
            menu.key(MenuKey::Back);
        }
        if (tab == 1) {
            // Exercise the DIP editing path: select CABINET and set MOVING.
            menu.key(MenuKey::Down);
            menu.key(MenuKey::Down);
            menu.key(MenuKey::Right);
        }
        if (tab == 2) {
            // Cycle FRAME RATE (60 -> 120 -> 144 -> 60) and change the
            // gear-indicator corner.
            menu.key(MenuKey::Down);
            menu.key(MenuKey::Down);
            menu.key(MenuKey::Right);
            menu.key(MenuKey::Right);
            menu.key(MenuKey::Right);
            for (int i = 0; i < 3; i++) menu.key(MenuKey::Down);
            menu.key(MenuKey::Right);
        }
        if (tab == 3) {
            // Exercise the separate music, SFX and engine sliders.
            menu.key(MenuKey::Left);
            menu.key(MenuKey::Down);
            menu.key(MenuKey::Left);
            menu.key(MenuKey::Down);
            menu.key(MenuKey::Left);
        }
        if (tab == 4) {
            // Unlock the mixer before editing.
            menu.key(MenuKey::Enter);
            // Exercise the channel mixer: YM 1 above 100% and PCM 1 below.
            menu.key(MenuKey::Down);
            for (int i = 0; i < 3; ++i) menu.key(MenuKey::Up);
            for (int i = 0; i < 8; ++i) menu.key(MenuKey::Right);
            menu.key(MenuKey::Down);
            menu.key(MenuKey::Minus);
            menu.key(MenuKey::Solo);
            menu.key(MenuKey::Solo);
            for (int i = 0; i < 16; ++i) menu.key(MenuKey::Right);
            // Open the chorus submenu and touch every row, then return.
            menu.key(MenuKey::Enter);
            menu.key(MenuKey::Right);
            for (int i = 0; i < 5; ++i) {
                menu.key(MenuKey::Down);
                menu.key(MenuKey::Right);
            }
            menu.key(MenuKey::Back);
            const float ym_levels[8] = {0.15f, 0.55f, 0.30f, 0.80f, 0.05f, 0.65f, 0.25f, 0.45f};
            const float pcm_levels[16] = {0.90f, 0.55f, 0.20f, 0.75f, 0.10f, 0.35f, 0.60f, 0.05f,
                                          0.40f, 0.25f, 0.70f, 0.15f, 0.50f, 0.05f, 0.30f, 0.20f};
            menu.set_levels(ym_levels, pcm_levels);
        }
        if (tab == 5) {
            // Exercise the run-in-background toggle.
            for (int i = 0; i < 3; i++) menu.key(MenuKey::Down);
            menu.key(MenuKey::Enter);
        }
        for (int y = 0; y < Machine::kVisibleLines; y++) {
            for (int x = 0; x < Machine::kVisibleWidth; x++) {
                const int r = (x * 255) / Machine::kVisibleWidth;
                const int g = (y * 255) / Machine::kVisibleLines;
                const int b = ((x ^ y) & 0x3F) * 3;
                px[(size_t)y * Machine::kVisibleWidth + x] =
                    0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
        menu.draw(px.data(), Machine::kVisibleWidth, Machine::kVisibleLines);
        if (tab == 0) menu.draw_fps(px.data(), Machine::kVisibleWidth, Machine::kVisibleLines,
                                    59.9, 119.8, 50);
        char name[64];
        std::snprintf(name, sizeof(name), "work/menu%d.ppm", tab);
        std::ofstream f(name, std::ios::binary);
        f << "P6\n" << Machine::kVisibleWidth << " " << Machine::kVisibleLines << "\n255\n";
        for (uint32_t p : px) {
            const uint8_t rgb[3] = {(uint8_t)((p >> 16) & 0xFF), (uint8_t)((p >> 8) & 0xFF),
                                    (uint8_t)(p & 0xFF)};
            f.write((const char *)rgb, 3);
        }
        menu.key(MenuKey::TabNext);
    }
    std::printf("menutest: wrote work/menu0..5.ppm (SWA %02X SWB %02X)\n",
                settings.dip_a(), settings.dip_b());

    {
        // Capture the live steering graph page with a non-centred sample.
        Settings setup_settings;
        Menu setup_menu(setup_settings);
        setup_menu.toggle();
        for (int i = 0; i < 3; ++i) setup_menu.key(MenuKey::Down);
        setup_menu.key(MenuKey::Enter);
        setup_menu.set_steer(16000, 0xA0);
        std::fill(px.begin(), px.end(), 0xFF000000u);
        setup_menu.draw(px.data(), Machine::kVisibleWidth, Machine::kVisibleLines);
        std::ofstream f("work/menu_steering.ppm", std::ios::binary);
        f << "P6\n" << Machine::kVisibleWidth << " " << Machine::kVisibleLines
          << "\n255\n";
        for (uint32_t p : px) {
            const uint8_t rgb[3] = {(uint8_t)((p >> 16) & 0xFF),
                                    (uint8_t)((p >> 8) & 0xFF), (uint8_t)(p & 0xFF)};
            f.write((const char *)rgb, 3);
        }
    }
    std::printf("menutest: wrote work/menu_steering.ppm\n");

    {
        Settings sfx_settings;
        Menu sfx_menu(sfx_settings);
        sfx_menu.toggle();
        for (int i = 0; i < 2; ++i) sfx_menu.key(MenuKey::TabPrev);  // MIXER
        sfx_menu.key(MenuKey::Enter);  // unlock mixer
        for (int i = 0; i < 28; ++i) sfx_menu.key(MenuKey::Right);  // SFX action
        sfx_menu.key(MenuKey::Enter);
        const float levels[Settings::kSfxChannels] = {
            0.8f, 0.5f, 0.2f, 0.7f, 0.1f, 0.4f, 0.6f, 0.3f, 0.9f,
            0.2f, 0.5f, 0.7f, 0.1f, 0.4f, 0.6f, 0.3f, 0.5f, 0.6f};
        sfx_menu.set_sfx_levels(levels);
        std::fill(px.begin(), px.end(), 0xFF000000u);
        sfx_menu.draw(px.data(), Machine::kVisibleWidth, Machine::kVisibleLines);
        std::ofstream f("work/menu_sfx.ppm", std::ios::binary);
        f << "P6\n" << Machine::kVisibleWidth << " " << Machine::kVisibleLines
          << "\n255\n";
        for (uint32_t p : px) {
            const uint8_t rgb[3] = {(uint8_t)((p >> 16) & 0xFF),
                                    (uint8_t)((p >> 8) & 0xFF), (uint8_t)(p & 0xFF)};
            f.write((const char *)rgb, 3);
        }
    }
    std::printf("menutest: wrote work/menu_sfx.ppm\n");

    settings.save("work/menu_settings.cfg");
    Settings loaded;
    const bool loaded_ok = loaded.load("work/menu_settings.cfg");
    bool mixer_roundtrip = true;
    for (int i = 0; i < 8; ++i) {
        if (loaded.ym_volume[i] != settings.ym_volume[i]) mixer_roundtrip = false;
        if (loaded.ym_send[i] != settings.ym_send[i]) mixer_roundtrip = false;
        if (loaded.ym_reverb_send[i] != settings.ym_reverb_send[i])
            mixer_roundtrip = false;
    }
    for (int i = 0; i < 16; ++i) {
        if (loaded.pcm_volume[i] != settings.pcm_volume[i]) mixer_roundtrip = false;
        if (loaded.pcm_send[i] != settings.pcm_send[i]) mixer_roundtrip = false;
        if (loaded.pcm_reverb_send[i] != settings.pcm_reverb_send[i])
            mixer_roundtrip = false;
    }
    if (loaded.reverb_enabled != settings.reverb_enabled ||
        loaded.reverb_decay != settings.reverb_decay ||
        loaded.reverb_damping != settings.reverb_damping ||
        loaded.reverb_predelay != settings.reverb_predelay ||
        loaded.reverb_mix != settings.reverb_mix)
        mixer_roundtrip = false;
    const bool roundtrip = loaded_ok &&
                           loaded.dip_a() == settings.dip_a() &&
                           loaded.dip_b() == settings.dip_b() &&
                           loaded.volume == settings.volume &&
                           loaded.music_volume == settings.music_volume &&
                           loaded.sfx_volume == settings.sfx_volume &&
                           loaded.engine_volume == settings.engine_volume &&
                           loaded.show_gear == settings.show_gear &&
                           loaded.gear_corner == settings.gear_corner &&
                           loaded.frame_rate == settings.frame_rate &&
                           loaded.audio_preset == settings.audio_preset &&
                           loaded.run_in_menu == settings.run_in_menu &&
                           loaded.chorus_enabled == settings.chorus_enabled &&
                           loaded.chorus_rate == settings.chorus_rate &&
                           loaded.chorus_depth == settings.chorus_depth &&
                           loaded.chorus_delay == settings.chorus_delay &&
                           loaded.chorus_feedback == settings.chorus_feedback &&
                           loaded.chorus_mix == settings.chorus_mix &&
                           loaded.push_start == settings.push_start &&
                           loaded.steer_rebound == settings.steer_rebound &&
                           loaded.aspect == settings.aspect &&
                           loaded.pad_steer_left == settings.pad_steer_left &&
                           loaded.pad_steer_right == settings.pad_steer_right &&
                           loaded.pad_gas == settings.pad_gas &&
                           loaded.pad_brake == settings.pad_brake &&
                           loaded.pad_gear == settings.pad_gear &&
                           loaded.pad_start == settings.pad_start &&
                           loaded.pad_coin == settings.pad_coin &&
                           loaded.pad_steer_axis == settings.pad_steer_axis &&
                           loaded.pad_gas_axis == settings.pad_gas_axis &&
                           loaded.pad_gas_axis_invert == settings.pad_gas_axis_invert &&
                           loaded.pad_brake_axis == settings.pad_brake_axis &&
                           loaded.pad_brake_axis_invert == settings.pad_brake_axis_invert &&
                           loaded.pad_steer_rate == settings.pad_steer_rate &&
                           loaded.racing_wheel == settings.racing_wheel &&
                           loaded.steer_rebound_speed == settings.steer_rebound_speed &&
                           loaded.sfx_lane_volume[0] == settings.sfx_lane_volume[0] &&
                           loaded.sfx_lane_send[0] == settings.sfx_lane_send[0] &&
                           gamepad_binding_ok &&
                           steering_setup_ok &&
                           mixer_roundtrip;
    std::printf("menutest: settings round-trip %s\n", roundtrip ? "PASS" : "FAIL");
    std::printf("menutest: controller bindings %s\n",
                gamepad_binding_ok ? "PASS" : "FAIL");
    {
        Settings axis_settings;
        Menu axis_menu(axis_settings);
        axis_menu.toggle();
        axis_menu.key(MenuKey::Down);
        axis_menu.key(MenuKey::Down);    // CONTROLLER BINDINGS
        axis_menu.key(MenuKey::Enter);
        for (int i = 0; i < 4; ++i) axis_menu.key(MenuKey::Down);
        axis_menu.key(MenuKey::Enter);   // GAS AXIS
        axis_menu.bind_axis(1, -20000);  // left stick Y, negative direction
        const bool axis_ok = axis_settings.pad_gas_axis == 1 &&
                             axis_settings.pad_gas_axis_invert;
        std::printf("menutest: axis bindings %s\n",
                    axis_ok ? "PASS" : "FAIL");
    }
    {
        Machine course_machine;
        Settings course_settings;
        SteeringState course_state;
        Uint8 keys[SDL_NUM_SCANCODES] = {};
        course_machine.shared[0xE09C] = 0;
        course_machine.shared[0xE09D] = 8;
        course_machine.in_wheel = 0x80;
        keys[SDL_SCANCODE_LEFT] = 1;
        bool gear = false, prev_gear = false;
        apply_input(course_machine, keys, nullptr, gear, prev_gear,
                    course_state, course_settings, false);
        const bool one_step = course_machine.in_wheel == 0x60;
        keys[SDL_SCANCODE_LEFT] = 0;
        apply_input(course_machine, keys, nullptr, gear, prev_gear,
                    course_state, course_settings, false);
        keys[SDL_SCANCODE_LEFT] = 1;
        apply_input(course_machine, keys, nullptr, gear, prev_gear,
                    course_state, course_settings, false);
        const bool two_steps = course_machine.in_wheel == 0x40;
        keys[SDL_SCANCODE_LEFT] = 0;
        apply_input(course_machine, keys, nullptr, gear, prev_gear,
                    course_state, course_settings, false);

        course_machine.in_wheel = 0x80;
        course_state = {};
        keys[SDL_SCANCODE_RIGHT] = 1;
        apply_input(course_machine, keys, nullptr, gear, prev_gear,
                    course_state, course_settings, false);
        const bool one_step_right = course_machine.in_wheel == 0xA0;
        keys[SDL_SCANCODE_RIGHT] = 0;
        apply_input(course_machine, keys, nullptr, gear, prev_gear,
                    course_state, course_settings, false);

        course_settings.racing_wheel = true;
        course_machine.in_wheel = 0x80;
        course_state = {};
        keys[SDL_SCANCODE_LEFT] = 1;
        apply_input(course_machine, keys, nullptr, gear, prev_gear,
                    course_state, course_settings, false);
        const bool no_wheel_nudge = course_machine.in_wheel == 0x80;
        std::printf("menutest: course-select one press per course %s\n",
                    one_step && two_steps && one_step_right ? "PASS" : "FAIL");
        std::printf("menutest: racing-wheel nudge disable %s\n",
                    no_wheel_nudge ? "PASS" : "FAIL");
        steering_setup_ok = steering_setup_ok && one_step && two_steps &&
                            one_step_right && no_wheel_nudge;
    }
    {
        Machine entry_machine;
        Settings entry_settings;
        SteeringState entry_state;
        Uint8 keys[SDL_NUM_SCANCODES] = {};
        entry_machine.shared[0xE09C] = 0;
        entry_machine.shared[0xE09D] = 0x1B;
        entry_machine.in_wheel = 0x80;
        bool gear = false, prev_gear = false;
        keys[SDL_SCANCODE_RIGHT] = 1;
        apply_input(entry_machine, keys, nullptr, gear, prev_gear,
                    entry_state, entry_settings, false);
        const bool carousel_right = entry_machine.in_wheel == 0x86;
        keys[SDL_SCANCODE_RIGHT] = 0;
        apply_input(entry_machine, keys, nullptr, gear, prev_gear,
                    entry_state, entry_settings, false);
        keys[SDL_SCANCODE_RIGHT] = 1;
        apply_input(entry_machine, keys, nullptr, gear, prev_gear,
                    entry_state, entry_settings, false);
        const bool carousel_next = entry_machine.in_wheel == 0x89;
        keys[SDL_SCANCODE_RIGHT] = 0;
        apply_input(entry_machine, keys, nullptr, gear, prev_gear,
                    entry_state, entry_settings, false);
        keys[SDL_SCANCODE_LEFT] = 1;
        apply_input(entry_machine, keys, nullptr, gear, prev_gear,
                    entry_state, entry_settings, false);
        const bool carousel_back = entry_machine.in_wheel == 0x86;
        std::printf("menutest: name-entry carousel %s\n",
                    carousel_right && carousel_next && carousel_back
                        ? "PASS"
                        : "FAIL");
        steering_setup_ok =
            steering_setup_ok && carousel_right && carousel_next &&
            carousel_back;
    }
    std::printf("menutest: steering setup %s\n",
                steering_setup_ok ? "PASS" : "FAIL");

    // The MIXER tab's RESET button restores every trim, send, chorus value and
    // clears solo.
    Settings reset_settings;
    Menu reset_menu(reset_settings);
    reset_menu.toggle();
    reset_settings.ym_volume[0] = Settings::kChannelMax;
    reset_settings.pcm_volume[15] = 0;
    reset_settings.ym_send[7] = 5;
    reset_settings.pcm_send[0] = 50;
    reset_settings.ym_reverb_send[6] = 25;
    reset_settings.pcm_reverb_send[1] = 75;
    reset_settings.sfx_lane_volume[0] = Settings::kChannelMax;
    reset_settings.sfx_lane_send[0] = 60;
    reset_settings.sfx_lane_reverb_send[0] = 70;
    reset_settings.reverb_enabled = true;
    reset_settings.reverb_decay = 90;
    reset_settings.reverb_damping = 80;
    reset_settings.reverb_predelay = 40;
    reset_settings.reverb_mix = 70;
    reset_settings.chorus_enabled = true;
    reset_settings.chorus_rate = 40;
    reset_settings.chorus_depth = 90;
    reset_settings.chorus_delay = 25;
    reset_settings.chorus_feedback = 60;
    reset_settings.chorus_mix = 80;
    reset_settings.solo_mask = (1u << 0) | (1u << (8 + 3));
    reset_menu.key(MenuKey::TabPrev);   // INPUT -> SYSTEM
    reset_menu.key(MenuKey::TabPrev);   // SYSTEM -> MIXER
    reset_menu.key(MenuKey::Enter);     // unlock mixer
    reset_menu.key(MenuKey::Left);      // SFX
    reset_menu.key(MenuKey::Left);      // RESET MIXER
    reset_menu.key(MenuKey::Enter);
    const Settings defaults;
    bool reset_ok = reset_settings.solo_mask == 0 &&
                    reset_settings.ym_send[0] == 0 && reset_settings.pcm_send[0] == 0 &&
                    reset_settings.ym_reverb_send[0] == 0 &&
                    reset_settings.pcm_reverb_send[0] == 0 &&
                    !reset_settings.chorus_enabled && !reset_settings.reverb_enabled &&
                    reset_settings.reverb_decay == defaults.reverb_decay &&
                    reset_settings.reverb_damping == defaults.reverb_damping &&
                    reset_settings.reverb_predelay == defaults.reverb_predelay &&
                    reset_settings.reverb_mix == defaults.reverb_mix &&
                    reset_settings.chorus_rate == defaults.chorus_rate &&
                    reset_settings.chorus_depth == defaults.chorus_depth &&
                    reset_settings.chorus_delay == defaults.chorus_delay &&
                    reset_settings.chorus_feedback == defaults.chorus_feedback &&
                    reset_settings.chorus_mix == defaults.chorus_mix &&
                    reset_settings.sfx_lane_volume[0] == defaults.sfx_lane_volume[0] &&
                    reset_settings.sfx_lane_send[0] == 0 &&
                    reset_settings.sfx_lane_reverb_send[0] == 0;
    for (int i = 0; i < 8; ++i) {
        if (reset_settings.ym_volume[i] != defaults.ym_volume[i]) reset_ok = false;
        if (reset_settings.ym_send[i] != defaults.ym_send[i]) reset_ok = false;
        if (reset_settings.ym_reverb_send[i] != defaults.ym_reverb_send[i])
            reset_ok = false;
    }
    for (int i = 0; i < 16; ++i) {
        if (reset_settings.pcm_volume[i] != defaults.pcm_volume[i]) reset_ok = false;
        if (reset_settings.pcm_send[i] != defaults.pcm_send[i]) reset_ok = false;
        if (reset_settings.pcm_reverb_send[i] != defaults.pcm_reverb_send[i])
            reset_ok = false;
    }
    std::printf("menutest: mixer reset %s\n", reset_ok ? "PASS" : "FAIL");

    {
        // The mixer must be explicitly enabled before arrows/buttons can edit.
        Settings lock_settings;
        lock_settings.audio_preset = 1;
        Menu lock_menu(lock_settings);
        lock_menu.toggle();
        for (int i = 0; i < 4; ++i) lock_menu.key(MenuKey::TabNext);
        const int before = lock_settings.ym_volume[0];
        lock_menu.key(MenuKey::Down);
        lock_menu.key(MenuKey::Up);
        lock_menu.key(MenuKey::Plus);
        lock_menu.key(MenuKey::Solo);
        const bool stayed_locked = lock_settings.ym_volume[0] == before &&
                                   lock_settings.solo_mask == 0;
        lock_menu.key(MenuKey::Enter);
        lock_menu.key(MenuKey::Up);
        const bool unlocked_edits = lock_settings.ym_volume[0] == before + 5;
        std::printf("menutest: mixer safety lock %s\n",
                    (stayed_locked && unlocked_edits) ? "PASS" : "FAIL");
    }

    // Multiple solo: adding another channel keeps the first; each is toggled
    // off individually.
    Settings solo_settings;
    Menu solo_menu(solo_settings);
    solo_menu.toggle();
    solo_menu.key(MenuKey::TabPrev);   // INPUT -> SYSTEM
    solo_menu.key(MenuKey::TabPrev);   // SYSTEM -> MIXER
    solo_menu.key(MenuKey::Enter);     // unlock mixer
    solo_menu.key(MenuKey::Solo);      // YM1
    solo_menu.key(MenuKey::Right);     // YM2
    solo_menu.key(MenuKey::Solo);      // add YM2
    const bool solo_multi =
        solo_settings.solo_mask == ((1u << 0) | (1u << 1));
    solo_menu.key(MenuKey::Solo);      // remove YM2 only
    const bool solo_manual = solo_settings.solo_mask == (1u << 0);
    std::printf("menutest: multi solo %s\n",
                (solo_multi && solo_manual) ? "PASS" : "FAIL");

    // A/B monitor switch and the plate-reverb submenu.
    Settings fx_settings;
    fx_settings.audio_preset = 1;
    Menu fx_menu(fx_settings);
    fx_menu.toggle();
    fx_menu.key(MenuKey::TabPrev);   // INPUT -> SYSTEM
    fx_menu.key(MenuKey::TabPrev);   // SYSTEM -> MIXER
    fx_menu.key(MenuKey::Enter);     // unlock mixer
    fx_menu.key(MenuKey::Left);      // SFX
    fx_menu.key(MenuKey::Left);      // RESET
    fx_menu.key(MenuKey::Left);      // A/B
    fx_menu.key(MenuKey::Enter);
    const bool ab_on = fx_settings.ab_original;
    fx_menu.key(MenuKey::Enter);
    const bool ab_off = !fx_settings.ab_original;
    fx_menu.key(MenuKey::Left);      // REVERB
    fx_menu.key(MenuKey::Enter);     // open reverb page
    fx_menu.key(MenuKey::Right);     // reverb on
    fx_menu.key(MenuKey::Down);
    fx_menu.key(MenuKey::Right);     // decay +5
    const bool rev_on = fx_settings.reverb_enabled &&
                        fx_settings.reverb_decay != Settings{}.reverb_decay;
    fx_menu.key(MenuKey::Back);
    fx_menu.key(MenuKey::SendSelect);   // +/- now targets the reverb send
    fx_menu.key(MenuKey::Plus);
    const bool send2_ok =
        fx_settings.ym_reverb_send[0] == 5 && fx_settings.ym_send[0] == 0;
    std::printf("menutest: mixer A/B and reverb %s\n",
                (ab_on && ab_off && rev_on && send2_ok) ? "PASS" : "FAIL");

    // SOUND TEST: open the Audio tab's track list, pick Poker Face, leave.
    Settings st_settings;
    Menu st_menu(st_settings);
    st_menu.toggle();
    for (int i = 0; i < 3; ++i) st_menu.key(MenuKey::TabNext);   // AUDIO
    for (int i = 0; i < 6; ++i) st_menu.key(MenuKey::Down);      // SOUND TEST
    st_menu.key(MenuKey::Enter);
    const bool st_open = st_menu.sound_test();
    for (int i = 0; i < 4; ++i) st_menu.key(MenuKey::Down);      // POKER FACE (94H)
    const MenuResult play = st_menu.key(MenuKey::Enter);
    st_menu.key(MenuKey::Back);
    const bool st_ok = st_open && !st_menu.sound_test() && play.sound_cmd == 0x94;
    std::printf("menutest: sound test %s\n", st_ok ? "PASS" : "FAIL");

    // Audio mix preset: stock -> Hojo's per-song mixes and back.
    Settings preset_settings;
    Menu preset_menu(preset_settings);
    preset_menu.toggle();
    for (int i = 0; i < 3; ++i) preset_menu.key(MenuKey::TabNext);  // AUDIO
    for (int i = 0; i < 5; ++i) preset_menu.key(MenuKey::Down);     // AUDIO MIX
    preset_menu.key(MenuKey::Right);
    const bool preset_hojo = preset_settings.audio_preset == 1;
    preset_menu.key(MenuKey::Left);
    const bool preset_stock = preset_settings.audio_preset == 0;
    std::printf("menutest: audio mix preset %s\n",
                (preset_hojo && preset_stock) ? "PASS" : "FAIL");

    // Mixer build: two tabs, sound test first, per-song mixes round-trip.
    Settings mix_settings;
    Menu mix_menu(mix_settings, true);
    mix_menu.toggle();
    const bool mix_open = mix_menu.sound_test();
    for (int i = 0; i < 4; ++i) mix_menu.key(MenuKey::Down);
    const MenuResult mix_play = mix_menu.key(MenuKey::Enter);
    mix_menu.key(MenuKey::TabNext);
    const bool mix_tab = !mix_menu.sound_test();
    mix_menu.key(MenuKey::TabPrev);
    const bool mix_back = mix_menu.sound_test();

    Settings saved_mix;
    saved_mix.ym_volume[0] = 250;
    saved_mix.ym_send[7] = 40;
    saved_mix.pcm_volume[0] = 60;
    saved_mix.ym_reverb_send[0] = 35;
    saved_mix.pcm_reverb_send[0] = 25;
    saved_mix.chorus_enabled = true;
    saved_mix.chorus_depth = 80;
    saved_mix.reverb_enabled = true;
    saved_mix.reverb_decay = 85;
    saved_mix.pcm_volume[15] = 88;
    saved_mix.save_mix("work/mix_test.cfg");
    Settings loaded_mix;
    loaded_mix.reset_mixer();
    loaded_mix.sfx_lane_volume[0] = 77;
    loaded_mix.pcm_volume[15] = 88;
    const bool mix_roundtrip =
        loaded_mix.load_mix("work/mix_test.cfg") && loaded_mix.ym_volume[0] == 250 &&
        loaded_mix.ym_send[7] == 40 && loaded_mix.pcm_volume[0] == 60 &&
        loaded_mix.ym_reverb_send[0] == 35 &&
        loaded_mix.pcm_reverb_send[0] == 25 && loaded_mix.chorus_enabled &&
        loaded_mix.chorus_depth == 80 && loaded_mix.reverb_enabled &&
        loaded_mix.reverb_decay == 85 && loaded_mix.sfx_lane_volume[0] == 77 &&
        loaded_mix.pcm_volume[15] == 88;
    const bool mixer_build_ok =
        mix_open && mix_play.sound_cmd == 0x94 && mix_tab && mix_back && mix_roundtrip;
    std::printf("menutest: mixer build %s\n", mixer_build_ok ? "PASS" : "FAIL");

    // Exercise every key on every tab so navigation/value clamping cannot walk
    // off the end of a list.
    Settings fuzz_settings;
    Menu fuzz(fuzz_settings);
    fuzz.toggle();
    for (int i = 0; i < 1000; i++)
        fuzz.key((MenuKey)(i % 12));
    std::vector<uint32_t> fuzz_px((size_t)Machine::kVisibleWidth * Machine::kVisibleLines, 0xFF000000u);
    fuzz.draw(fuzz_px.data(), Machine::kVisibleWidth, Machine::kVisibleLines);
    std::printf("menutest: key fuzz ok\n");
    return 0;
}
