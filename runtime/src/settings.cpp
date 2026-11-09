/*
 * settings.cpp — load/save and range clamping for the tab menu options.
 */
#include "likethewind/settings.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

namespace likethewind {

static int as_int(const std::string &s) {
    return (int)std::strtol(s.c_str(), nullptr, 10);
}

void Settings::clamp() {
    coin_a = (uint8_t)std::clamp<int>(coin_a, 0, 15);
    coin_b = (uint8_t)std::clamp<int>(coin_b, 0, 15);
    cabinet = (uint8_t)std::clamp<int>(cabinet, 0, 3);
    demo_sounds = (uint8_t)std::clamp<int>(demo_sounds, 0, 1);
    credits = (uint8_t)std::clamp<int>(credits, 0, 3);
    allow_continue = (uint8_t)std::clamp<int>(allow_continue, 0, 1);
    difficulty = (uint8_t)std::clamp<int>(difficulty, 0, 3);
    scale = std::clamp(scale, 1, 5);
    if (frame_rate != 60 && frame_rate != 120 && frame_rate != 144) frame_rate = 60;
    gear_corner = std::clamp(gear_corner, 0, 3);
    audio_preset = std::clamp(audio_preset, 0, 1);
    volume = std::clamp(volume, 0, kBusMax);
    music_volume = std::clamp(music_volume, 0, kBusMax);
    sfx_volume = std::clamp(sfx_volume, 0, kBusMax);
    engine_volume = std::clamp(engine_volume, 0, kBusMax);
    for (int &v : ym_volume) v = std::clamp(v, 0, kChannelMax);
    for (int &v : pcm_volume) v = std::clamp(v, 0, kChannelMax);
    for (int &v : ym_send) v = std::clamp(v, 0, 100);
    for (int &v : pcm_send) v = std::clamp(v, 0, 100);
    for (int &v : sfx_lane_volume) v = std::clamp(v, 0, kChannelMax);
    // SFX are dry: chorus and plate reverb are music-only effects.
    for (int &v : sfx_lane_send) v = 0;
    for (int &v : sfx_lane_reverb_send) v = 0;
    for (int &v : ym_reverb_send) v = std::clamp(v, 0, 100);
    for (int &v : pcm_reverb_send) v = std::clamp(v, 0, 100);
    chorus_rate = std::clamp(chorus_rate, 1, 50);
    chorus_depth = std::clamp(chorus_depth, 0, 100);
    chorus_delay = std::clamp(chorus_delay, 5, 30);
    chorus_feedback = std::clamp(chorus_feedback, 0, 90);
    chorus_mix = std::clamp(chorus_mix, 0, 100);
    reverb_decay = std::clamp(reverb_decay, 0, 100);
    reverb_damping = std::clamp(reverb_damping, 0, 100);
    reverb_predelay = std::clamp(reverb_predelay, 0, 50);
    reverb_mix = std::clamp(reverb_mix, 0, 100);
    solo_mask &= 0xFFFFFFFFu;
    aspect = std::clamp(aspect, 0, 5);
    tour_start_course = std::clamp(tour_start_course, 0, 4);
    steer_deadzone = std::clamp(steer_deadzone, 0, 40);
    pedal_deadzone = std::clamp(pedal_deadzone, 0, 20);
    steer_rate = std::clamp(steer_rate, 1, 24);
    pad_steer_rate = std::clamp(pad_steer_rate, 0, 24);
    centre_rate = std::clamp(centre_rate, 0, 10);
    steer_rebound = std::clamp(steer_rebound, 0, 32);
    steer_rebound_speed = std::clamp(steer_rebound_speed, 1, 10);
    pad_steer_left = std::clamp(pad_steer_left, -1, 20);
    pad_steer_right = std::clamp(pad_steer_right, -1, 20);
    pad_gas = std::clamp(pad_gas, -1, 20);
    pad_brake = std::clamp(pad_brake, -1, 20);
    pad_gear = std::clamp(pad_gear, -1, 20);
    pad_start = std::clamp(pad_start, -1, 20);
    pad_coin = std::clamp(pad_coin, -1, 20);
    pad_steer_axis = std::clamp(pad_steer_axis, -1, 5);
    pad_gas_axis = std::clamp(pad_gas_axis, -1, 5);
    pad_brake_axis = std::clamp(pad_brake_axis, -1, 5);
}

void Settings::reset_gamepad() {
    const Settings defaults;
    pad_steer_left = defaults.pad_steer_left;
    pad_steer_right = defaults.pad_steer_right;
    pad_gas = defaults.pad_gas;
    pad_brake = defaults.pad_brake;
    pad_gear = defaults.pad_gear;
    pad_start = defaults.pad_start;
    pad_coin = defaults.pad_coin;
    pad_steer_axis = defaults.pad_steer_axis;
    pad_gas_axis = defaults.pad_gas_axis;
    pad_gas_axis_invert = defaults.pad_gas_axis_invert;
    pad_brake_axis = defaults.pad_brake_axis;
    pad_brake_axis_invert = defaults.pad_brake_axis_invert;
    racing_wheel = defaults.racing_wheel;
}

void Settings::reset_mixer(bool flat) {
    // Defaults are the initial values in settings.h. Sends return to zero, so
    // a reset mixer is fully dry. The Audio tab's bus volumes (music/SFX/
    // engine/master) are deliberately left alone.
    const Settings defaults;
    for (int i = 0; i < 8; ++i) {
        ym_volume[i] = defaults.ym_volume[i];
        ym_send[i] = defaults.ym_send[i];
        ym_reverb_send[i] = defaults.ym_reverb_send[i];
    }
    for (int i = 0; i < 16; ++i) {
        pcm_volume[i] = defaults.pcm_volume[i];
        pcm_send[i] = defaults.pcm_send[i];
        pcm_reverb_send[i] = defaults.pcm_reverb_send[i];
    }
    for (int i = 0; i < Settings::kSfxChannels; ++i) {
        sfx_lane_volume[i] = defaults.sfx_lane_volume[i];
        sfx_lane_send[i] = defaults.sfx_lane_send[i];
        sfx_lane_reverb_send[i] = defaults.sfx_lane_reverb_send[i];
    }
    chorus_enabled = defaults.chorus_enabled;
    chorus_rate = defaults.chorus_rate;
    chorus_depth = defaults.chorus_depth;
    chorus_delay = defaults.chorus_delay;
    chorus_feedback = defaults.chorus_feedback;
    chorus_mix = defaults.chorus_mix;
    reverb_enabled = defaults.reverb_enabled;
    reverb_decay = defaults.reverb_decay;
    reverb_damping = defaults.reverb_damping;
    reverb_predelay = defaults.reverb_predelay;
    reverb_mix = defaults.reverb_mix;
    solo_mask = defaults.solo_mask;
    if (flat) {
        for (int &v : ym_volume) v = 100;
        for (int &v : pcm_volume) v = 100;
        for (int &v : sfx_lane_volume) v = 100;
        for (int &v : ym_send) v = 0;
        for (int &v : pcm_send) v = 0;
        for (int &v : sfx_lane_send) v = 0;
        for (int &v : ym_reverb_send) v = 0;
        for (int &v : pcm_reverb_send) v = 0;
        for (int &v : sfx_lane_reverb_send) v = 0;
    }
}

void Settings::reset_music_mixer() {
    // Per-song mixes cover YM and the eight PCM music lanes only. SFX lanes
    // 9-14 and the two engine lanes are global and must survive a song change.
    const Settings defaults;
    for (int i = 0; i < 8; ++i) {
        ym_volume[i] = defaults.ym_volume[i];
        ym_send[i] = defaults.ym_send[i];
        ym_reverb_send[i] = defaults.ym_reverb_send[i];
    }
    for (int i = 0; i < 16; ++i) {
        pcm_volume[i] = defaults.pcm_volume[i];
        pcm_send[i] = defaults.pcm_send[i];
        pcm_reverb_send[i] = defaults.pcm_reverb_send[i];
    }
    chorus_enabled = defaults.chorus_enabled;
    chorus_rate = defaults.chorus_rate;
    chorus_depth = defaults.chorus_depth;
    chorus_delay = defaults.chorus_delay;
    chorus_feedback = defaults.chorus_feedback;
    chorus_mix = defaults.chorus_mix;
    reverb_enabled = defaults.reverb_enabled;
    reverb_decay = defaults.reverb_decay;
    reverb_damping = defaults.reverb_damping;
    reverb_predelay = defaults.reverb_predelay;
    reverb_mix = defaults.reverb_mix;
    solo_mask = 0;
}

bool Settings::load(const std::string &path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    bool have_frame_rate = false;
    while (std::getline(f, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        size_t start = key.find_first_not_of(" \t");
        key = (start == std::string::npos) ? "" : key.substr(start);
        const int n = as_int(val);

        if (key == "coin_a") coin_a = (uint8_t)n;
        else if (key == "coin_b") coin_b = (uint8_t)n;
        else if (key == "cabinet") cabinet = (uint8_t)n;
        else if (key == "demo_sounds") demo_sounds = (uint8_t)n;
        else if (key == "credits") credits = (uint8_t)n;
        else if (key == "allow_continue") allow_continue = (uint8_t)n;
        else if (key == "difficulty") difficulty = (uint8_t)n;
        else if (key == "aspect") aspect = n;
        else if (key == "widescreen") aspect = n != 0 ? 1 : 0;
        else if (key == "scale") scale = n;
        else if (key == "fullscreen") fullscreen = n != 0;
        else if (key == "frame_rate") {
            frame_rate = n;
            have_frame_rate = true;
        } else if (key == "interpolate" && !have_frame_rate) {
            // Legacy key from builds with the 60/120 toggle.
            frame_rate = n != 0 ? 120 : 60;
        } else if (key == "show_fps") show_fps = n != 0;
        else if (key == "show_gear") show_gear = n != 0;
        else if (key == "gear_corner") gear_corner = n;
        else if (key == "audio_preset") audio_preset = n;
        else if (key == "volume") volume = n;
        else if (key == "music_volume") music_volume = n;
        else if (key == "sfx_volume") sfx_volume = n;
        else if (key == "engine_volume") engine_volume = n;
        else if (key.size() > 11 && key.compare(0, 11, "ym_rev_send") == 0) {
            const int i = std::atoi(key.c_str() + 11) - 1;
            if (i >= 0 && i < 8) ym_reverb_send[i] = n;
        } else if (key.size() > 12 && key.compare(0, 12, "pcm_rev_send") == 0) {
            const int i = std::atoi(key.c_str() + 12) - 1;
            if (i >= 0 && i < 16) pcm_reverb_send[i] = n;
        } else if (key.size() > 16 && key.compare(0, 16, "sfxlane_rev_send") == 0) {
            const int i = std::atoi(key.c_str() + 16) - 1;
            if (i >= 0 && i < Settings::kSfxChannels) sfx_lane_reverb_send[i] = n;
        } else if (key.size() > 12 && key.compare(0, 12, "sfxlane_send") == 0) {
            const int i = std::atoi(key.c_str() + 12) - 1;
            if (i >= 0 && i < Settings::kSfxChannels) sfx_lane_send[i] = n;
        } else if (key.size() > 7 && key.compare(0, 7, "sfxlane") == 0) {
            const int i = std::atoi(key.c_str() + 7) - 1;
            if (i >= 0 && i < Settings::kSfxChannels) sfx_lane_volume[i] = n;
        } else if (key.size() > 7 && key.compare(0, 7, "ym_send") == 0) {
            const int i = std::atoi(key.c_str() + 7) - 1;
            if (i >= 0 && i < 8) ym_send[i] = n;
        } else if (key.size() > 8 && key.compare(0, 8, "pcm_send") == 0) {
            const int i = std::atoi(key.c_str() + 8) - 1;
            if (i >= 0 && i < 16) pcm_send[i] = n;
        } else if (key.size() > 2 && key.compare(0, 2, "ym") == 0) {
            const int i = std::atoi(key.c_str() + 2) - 1;
            if (i >= 0 && i < 8) ym_volume[i] = n;
        } else if (key.size() > 3 && key.compare(0, 3, "pcm") == 0) {
            const int i = std::atoi(key.c_str() + 3) - 1;
            if (i >= 0 && i < 16) pcm_volume[i] = n;
        } else if (key == "muted") muted = n != 0;
        else if (key == "chorus") chorus_enabled = n != 0;
        else if (key == "chorus_rate") chorus_rate = n;
        else if (key == "chorus_depth") chorus_depth = n;
        else if (key == "chorus_delay") chorus_delay = n;
        else if (key == "chorus_feedback") chorus_feedback = n;
        else if (key == "chorus_mix") chorus_mix = n;
        else if (key == "reverb") reverb_enabled = n != 0;
        else if (key == "reverb_decay") reverb_decay = n;
        else if (key == "reverb_damping") reverb_damping = n;
        else if (key == "reverb_predelay") reverb_predelay = n;
        else if (key == "reverb_mix") reverb_mix = n;
        else if (key == "run_in_menu") run_in_menu = n != 0;
        else if (key == "restore_female_voices") restore_female_voices = n != 0;
        else if (key == "restore_additional_voices") restore_additional_voices = n != 0;
        else if (key == "special_tour") special_tour = n != 0;
        else if (key == "tour_start_course") tour_start_course = n;
        else if (key == "push_start") push_start = n != 0;
        else if (key == "steer_deadzone") steer_deadzone = n;
        else if (key == "pedal_deadzone") pedal_deadzone = n;
        else if (key == "steer_rate") steer_rate = n;
        else if (key == "pad_steer_rate") pad_steer_rate = n;
        else if (key == "centre_rate") centre_rate = n;
        else if (key == "steer_rebound") steer_rebound = n;
        else if (key == "steer_rebound_speed") steer_rebound_speed = n;
        else if (key == "pad_steer_left") pad_steer_left = n;
        else if (key == "pad_steer_right") pad_steer_right = n;
        else if (key == "pad_gas") pad_gas = n;
        else if (key == "pad_brake") pad_brake = n;
        else if (key == "pad_gear") pad_gear = n;
        else if (key == "pad_start") pad_start = n;
        else if (key == "pad_coin") pad_coin = n;
        else if (key == "pad_steer_axis") pad_steer_axis = n;
        else if (key == "pad_gas_axis") pad_gas_axis = n;
        else if (key == "pad_gas_axis_invert") pad_gas_axis_invert = n != 0;
        else if (key == "pad_brake_axis") pad_brake_axis = n;
        else if (key == "pad_brake_axis_invert") pad_brake_axis_invert = n != 0;
        else if (key == "racing_wheel") racing_wheel = n != 0;
        else if (key == "auto_gear") auto_gear = n != 0;
    }
    clamp();
    return true;
}

void Settings::save(const std::string &path) const {
    std::ofstream f(path);
    if (!f) return;
    f << "# Power Drift: Recompiled settings\n"
      << "coin_a=" << (int)coin_a << "\n"
      << "coin_b=" << (int)coin_b << "\n"
      << "cabinet=" << (int)cabinet << "\n"
      << "demo_sounds=" << (int)demo_sounds << "\n"
      << "credits=" << (int)credits << "\n"
      << "allow_continue=" << (int)allow_continue << "\n"
      << "difficulty=" << (int)difficulty << "\n"
      << "aspect=" << aspect << "\n"
      << "scale=" << scale << "\n"
      << "fullscreen=" << (fullscreen ? 1 : 0) << "\n"
      << "frame_rate=" << frame_rate << "\n"
      << "show_fps=" << (show_fps ? 1 : 0) << "\n"
      << "show_gear=" << (show_gear ? 1 : 0) << "\n"
      << "gear_corner=" << gear_corner << "\n"
      << "audio_preset=" << audio_preset << "\n"
      << "volume=" << volume << "\n"
      << "music_volume=" << music_volume << "\n"
      << "sfx_volume=" << sfx_volume << "\n"
      << "engine_volume=" << engine_volume << "\n";
    for (int i = 0; i < 8; ++i) f << "ym" << (i + 1) << "=" << ym_volume[i] << "\n";
    for (int i = 0; i < 16; ++i) f << "pcm" << (i + 1) << "=" << pcm_volume[i] << "\n";
    for (int i = 0; i < 8; ++i) f << "ym_send" << (i + 1) << "=" << ym_send[i] << "\n";
    for (int i = 0; i < 16; ++i) f << "pcm_send" << (i + 1) << "=" << pcm_send[i] << "\n";
    for (int i = 0; i < 8; ++i)
        f << "ym_rev_send" << (i + 1) << "=" << ym_reverb_send[i] << "\n";
    for (int i = 0; i < 16; ++i)
        f << "pcm_rev_send" << (i + 1) << "=" << pcm_reverb_send[i] << "\n";
    for (int i = 0; i < Settings::kSfxChannels; ++i)
        f << "sfxlane" << (i + 1) << "=" << sfx_lane_volume[i] << "\n";
    for (int i = 0; i < Settings::kSfxChannels; ++i)
        f << "sfxlane_send" << (i + 1) << "=" << sfx_lane_send[i] << "\n";
    for (int i = 0; i < Settings::kSfxChannels; ++i)
        f << "sfxlane_rev_send" << (i + 1) << "=" << sfx_lane_reverb_send[i] << "\n";
    f << "muted=" << (muted ? 1 : 0) << "\n"
      << "chorus=" << (chorus_enabled ? 1 : 0) << "\n"
      << "chorus_rate=" << chorus_rate << "\n"
      << "chorus_depth=" << chorus_depth << "\n"
      << "chorus_delay=" << chorus_delay << "\n"
      << "chorus_feedback=" << chorus_feedback << "\n"
      << "chorus_mix=" << chorus_mix << "\n"
      << "reverb=" << (reverb_enabled ? 1 : 0) << "\n"
      << "reverb_decay=" << reverb_decay << "\n"
      << "reverb_damping=" << reverb_damping << "\n"
      << "reverb_predelay=" << reverb_predelay << "\n"
      << "reverb_mix=" << reverb_mix << "\n"
      << "run_in_menu=" << (run_in_menu ? 1 : 0) << "\n"
      << "restore_female_voices=" << (restore_female_voices ? 1 : 0) << "\n"
      << "restore_additional_voices=" << (restore_additional_voices ? 1 : 0) << "\n"
      << "special_tour=" << (special_tour ? 1 : 0) << "\n"
      << "tour_start_course=" << tour_start_course << "\n"
      << "push_start=" << (push_start ? 1 : 0) << "\n"
      << "steer_deadzone=" << steer_deadzone << "\n"
      << "pedal_deadzone=" << pedal_deadzone << "\n"
      << "steer_rate=" << steer_rate << "\n"
      << "pad_steer_rate=" << pad_steer_rate << "\n"
      << "centre_rate=" << centre_rate << "\n"
      << "steer_rebound=" << steer_rebound << "\n"
      << "steer_rebound_speed=" << steer_rebound_speed << "\n"
      << "pad_steer_left=" << pad_steer_left << "\n"
      << "pad_steer_right=" << pad_steer_right << "\n"
      << "pad_gas=" << pad_gas << "\n"
      << "pad_brake=" << pad_brake << "\n"
      << "pad_gear=" << pad_gear << "\n"
      << "pad_start=" << pad_start << "\n"
      << "pad_coin=" << pad_coin << "\n"
      << "pad_steer_axis=" << pad_steer_axis << "\n"
      << "pad_gas_axis=" << pad_gas_axis << "\n"
      << "pad_gas_axis_invert=" << (pad_gas_axis_invert ? 1 : 0) << "\n"
      << "pad_brake_axis=" << pad_brake_axis << "\n"
      << "pad_brake_axis_invert=" << (pad_brake_axis_invert ? 1 : 0) << "\n"
      << "racing_wheel=" << (racing_wheel ? 1 : 0) << "\n"
      << "auto_gear=" << (auto_gear ? 1 : 0) << "\n";
}

bool Settings::load_mix(const std::string &path) {
    Settings tmp;
    if (!tmp.load(path)) return false;
    for (int i = 0; i < 8; ++i) {
        ym_volume[i] = tmp.ym_volume[i];
        ym_send[i] = tmp.ym_send[i];
        ym_reverb_send[i] = tmp.ym_reverb_send[i];
    }
    for (int i = 0; i < 16; ++i) {
        pcm_volume[i] = tmp.pcm_volume[i];
        pcm_send[i] = tmp.pcm_send[i];
        pcm_reverb_send[i] = tmp.pcm_reverb_send[i];
    }
    chorus_enabled = tmp.chorus_enabled;
    chorus_rate = tmp.chorus_rate;
    chorus_depth = tmp.chorus_depth;
    chorus_delay = tmp.chorus_delay;
    chorus_feedback = tmp.chorus_feedback;
    chorus_mix = tmp.chorus_mix;
    reverb_enabled = tmp.reverb_enabled;
    reverb_decay = tmp.reverb_decay;
    reverb_damping = tmp.reverb_damping;
    reverb_predelay = tmp.reverb_predelay;
    reverb_mix = tmp.reverb_mix;
    solo_mask = 0;
    clamp();
    return true;
}

void Settings::save_mix(const std::string &path) const {
    std::ofstream f(path);
    if (!f) return;
    f << "# LikeTheWind per-song mix\n";
    for (int i = 0; i < 8; ++i) f << "ym" << (i + 1) << "=" << ym_volume[i] << "\n";
    for (int i = 0; i < 16; ++i) f << "pcm" << (i + 1) << "=" << pcm_volume[i] << "\n";
    for (int i = 0; i < 8; ++i) f << "ym_send" << (i + 1) << "=" << ym_send[i] << "\n";
    for (int i = 0; i < 16; ++i)
        f << "pcm_send" << (i + 1) << "=" << pcm_send[i] << "\n";
    for (int i = 0; i < 8; ++i)
        f << "ym_rev_send" << (i + 1) << "=" << ym_reverb_send[i] << "\n";
    for (int i = 0; i < 16; ++i)
        f << "pcm_rev_send" << (i + 1) << "=" << pcm_reverb_send[i] << "\n";
    f << "chorus=" << (chorus_enabled ? 1 : 0) << "\n"
      << "chorus_rate=" << chorus_rate << "\n"
      << "chorus_depth=" << chorus_depth << "\n"
      << "chorus_delay=" << chorus_delay << "\n"
      << "chorus_feedback=" << chorus_feedback << "\n"
      << "chorus_mix=" << chorus_mix << "\n"
      << "reverb=" << (reverb_enabled ? 1 : 0) << "\n"
      << "reverb_decay=" << reverb_decay << "\n"
      << "reverb_damping=" << reverb_damping << "\n"
      << "reverb_predelay=" << reverb_predelay << "\n"
      << "reverb_mix=" << reverb_mix << "\n";
}

} // namespace likethewind
