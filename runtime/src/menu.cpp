/*
 * menu.cpp - the in-game settings menu: input, DIP, video, audio, mixer, system.
 *
 * The overlay is drawn straight into the host's ARGB framebuffer with a small
 * built-in 5x7 font, so it needs no SDL_ttf or external assets.
 */
#include "likethewind/menu.h"

#include "likethewind/music_tracks.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace likethewind {

namespace {


constexpr int kTabCount = 6;

const char *const kCoinage[16] = {
    "FREE PLAY N/A", "1C/1C 2/3", "1C/1C 4/5", "1C/1C 5/6",
    "2C/1C 4/3",     "2C/1C 3/2", "2C/3C",     "4C/1C",
    "3C/1C",         "2C/1C",     "7C/1C",     "6C/1C",
    "5C/1C",         "1C/3C",     "1C/2C",     "1C/1C",
};
const char *const kCabinet[4] = {"UNUSED", "MINI UPRIGHT", "UPRIGHT", "MOVING"};
const char *const kCredits[4] = {"2/2", "1/1", "3/2", "2/1"};
const char *const kDifficulty[4] = {"HARDEST", "HARD", "EASY", "NORMAL"};
const char *const kOnOff[2] = {"OFF", "ON"};
const char *const kAspect[6] = {"4:3", "16:9", "21:9", "18:9",
                                "19:9", "20:9"};
// Display order: keep existing saved aspect values while inserting new ratios.
const int kAspectOrder[6] = {0, 1, 3, 4, 5, 2};
const char *const kGearCorner[4] = {"TOP LEFT", "TOP RIGHT", "BOTTOM LEFT", "BOTTOM RIGHT"};
const char *const kPadButtons[] = {
    "A", "B", "X", "Y", "BACK", "GUIDE", "START", "L3", "R3", "LB", "RB",
    "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT", "MISC1",
    "PADDLE 1", "PADDLE 2", "PADDLE 3", "PADDLE 4", "TOUCHPAD",
};

const char *const kPadAxes[] = {"LEFT X", "LEFT Y", "RIGHT X",
                                "RIGHT Y", "LEFT TRIGGER", "RIGHT TRIGGER"};

constexpr int kPadButtonCount = (int)(sizeof(kPadButtons) / sizeof(kPadButtons[0]));
constexpr int kPadAxisCount = (int)(sizeof(kPadAxes) / sizeof(kPadAxes[0]));

std::string pcm_channel_name(int channel) {
    return "PCM" + std::to_string(channel + 1);
}

std::string sfx_channel_name(int channel) {
    if (channel == 16) return "ANNOUNCER";
    if (channel == 17) return "PASSING";
    return "SFX" + std::to_string(channel + 1);
}

const char *pad_button_name(int button) {
    if (button < 0 || button >= kPadButtonCount) return "UNBOUND";
    return kPadButtons[button];
}

std::string pad_axis_name(int axis, bool invert = false) {
    if (axis < 0 || axis >= kPadAxisCount) return "UNBOUND";
    return std::string(kPadAxes[axis]) + (invert ? "-" : "");
}

std::string fmt(const char *f, int v) {
    char b[64];
    std::snprintf(b, sizeof(b), f, v);
    return b;
}

std::string fmt_ll(long long v) {
    char b[64];
    std::snprintf(b, sizeof(b), "%lld", v);
    return b;
}

std::string fmt_fps(double v) {
    char b[64];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

std::string fmt_rate(int tenths) {
    char b[32];
    std::snprintf(b, sizeof(b), "%d.%d HZ", tenths / 10, tenths % 10);
    return b;
}

int wrap(int v, int lo, int hi, int delta) {
    const int span = hi - lo + 1;
    int r = (v - lo + delta) % span;
    if (r < 0) r += span;
    return lo + r;
}


} // namespace

std::vector<Menu::Item> Menu::items() const {
    std::vector<Item> out;
    switch (tab_) {
    case 0:
        if (input_page_ == 2) {
            out.push_back({"STEER DEADZONE", fmt("%d%%", settings_.steer_deadzone),
                           Kind::Value, 0});
            out.push_back({"DIGITAL STEER RATE", fmt("%d", settings_.steer_rate),
                           Kind::Value, 2});
            out.push_back({"ANALOG STEER RATE",
                           settings_.pad_steer_rate == 0
                               ? "DIRECT"
                               : fmt("%d", settings_.pad_steer_rate),
                           Kind::Value, 7});
            out.push_back({"CENTRE RATE", fmt("%d", settings_.centre_rate), Kind::Value, 3});
            out.push_back({"STEER REBOUND", fmt("%d", settings_.steer_rebound),
                           Kind::Value, 4});
            out.push_back({"REBOUND SPEED", fmt("%d", settings_.steer_rebound_speed),
                           Kind::Value, 8});
        } else if (input_page_ == 1) {
            // Keep each physical control's digital and analog bindings
            // together instead of listing every button before every axis.
            out.push_back({"STEER LEFT", pad_button_name(settings_.pad_steer_left),
                           Kind::Action, 100});
            out.push_back({"STEER RIGHT", pad_button_name(settings_.pad_steer_right),
                           Kind::Action, 101});
            out.push_back({"STEER AXIS", pad_axis_name(settings_.pad_steer_axis),
                           Kind::Action, 120});
            out.push_back({"ACCELERATE", pad_button_name(settings_.pad_gas),
                           Kind::Action, 102});
            out.push_back({"GAS AXIS",
                           pad_axis_name(settings_.pad_gas_axis,
                                         settings_.pad_gas_axis_invert),
                           Kind::Action, 121});
            out.push_back({"BRAKE", pad_button_name(settings_.pad_brake),
                           Kind::Action, 103});
            out.push_back({"BRAKE AXIS",
                           pad_axis_name(settings_.pad_brake_axis,
                                         settings_.pad_brake_axis_invert),
                           Kind::Action, 122});
            out.push_back({"PEDAL DEADZONE",
                           fmt("%d%%", settings_.pedal_deadzone),
                           Kind::Value, 1});
            out.push_back({"GEAR", pad_button_name(settings_.pad_gear),
                           Kind::Action, 104});
            out.push_back({"START", pad_button_name(settings_.pad_start),
                           Kind::Action, 105});
            out.push_back({"COIN", pad_button_name(settings_.pad_coin),
                           Kind::Action, 106});
            out.push_back({"RESET DEFAULTS", "", Kind::Action, 110});
            out.push_back({"BACK", "", Kind::Action, 111});
        } else {
            out.push_back({"PUSH START", kOnOff[settings_.push_start ? 1 : 0], Kind::Value, 5});
            out.push_back({"RACING WHEEL", kOnOff[settings_.racing_wheel ? 1 : 0],
                           Kind::Value, 12});
            out.push_back({"AUTOMATIC GEARBOX", kOnOff[settings_.auto_gear ? 1 : 0],
                           Kind::Value, 13});
            out.push_back({"CONTROLLER BINDINGS", "", Kind::Action, 10});
            out.push_back({"STEERING SETUP", "", Kind::Action, 11});
        }
        break;
    case 1:
        out.push_back({"COIN A", kCoinage[settings_.coin_a], Kind::Value, 0});
        out.push_back({"COIN B", kCoinage[settings_.coin_b], Kind::Value, 1});
        out.push_back({"CABINET", kCabinet[settings_.cabinet], Kind::Value, 2});
        out.push_back({"DEMO SOUNDS", settings_.demo_sounds ? "OFF" : "ON", Kind::Value, 3});
        out.push_back({"CREDITS START/CONT", kCredits[settings_.credits], Kind::Value, 4});
        out.push_back({"ALLOW CONTINUE", settings_.allow_continue ? "YES" : "NO", Kind::Value, 5});
        out.push_back({"DIFFICULTY", kDifficulty[settings_.difficulty], Kind::Value, 6});
        out.push_back({"", "", Kind::Info, 0});
        out.push_back({"COIN A/B = RATES  CREDITS = START", "", Kind::Info, 0});
        out.push_back({"FREE PLAY IS NOT USED BY THIS REV", "", Kind::Info, 0});
        out.push_back({"DIPS APPLY ON GAME RESET", "", Kind::Info, 0});
        break;
    case 2: {
        const char *rate = settings_.frame_rate == 60
                               ? "60 FPS"
                               : (settings_.frame_rate == 144 ? "144 FPS INTERP"
                                                              : "120 FPS INTERP");
        out.push_back({"ASPECT RATIO", kAspect[settings_.aspect], Kind::Value, 0});
        out.push_back({"SCALE", fmt("%dx", settings_.scale), Kind::Value, 1});
        out.push_back({"FRAME RATE", rate, Kind::Value, 2});
        out.push_back({"SHOW FPS", kOnOff[settings_.show_fps ? 1 : 0], Kind::Value, 3});
        out.push_back({"GEAR INDICATOR", kOnOff[settings_.show_gear ? 1 : 0], Kind::Value, 4});
        out.push_back({"GEAR CORNER", kGearCorner[settings_.gear_corner], Kind::Value, 5});
        out.push_back({"FULLSCREEN", kOnOff[settings_.fullscreen ? 1 : 0],
                       Kind::Value, 6});
        out.push_back({"", "", Kind::Info, 0});
        out.push_back({"DISPLAY", fmt("%d HZ", display_hz_), Kind::Info, 0});
        out.push_back({"PRESENT",
                       present_fps_ > 0.0 ? fmt("%d FPS", (int)(present_fps_ + 0.5)) : "--",
                       Kind::Info, 0});
        out.push_back({"BLEND", present_fps_ > 0.0 ? fmt("%d%%", blend_pct_) : "--",
                       Kind::Info, 0});
        if (settings_.frame_rate > display_hz_)
            out.push_back({"MODE LIMITED BY DISPLAY", "", Kind::Info, 0});
        if (settings_.frame_rate > 60 && display_hz_ <= 60)
            out.push_back({"INTERP NEEDS OVER 60 HZ", "", Kind::Info, 0});
        break;
    }
    case 3:
        if (audio_page_ == 1) {
            for (const MusicTrack &t : kMusicTracks) {
                char id[8];
                std::snprintf(id, sizeof(id), "%02XH", t.cmd);
                out.push_back({t.name, id, Kind::Action, t.cmd});
            }
            out.push_back({"STOP MUSIC", "", Kind::Action, 0x100});
            if (!mixer_mode_) out.push_back({"BACK", "", Kind::Action, 0x101});
            break;
        }
        out.push_back({"MUSIC VOLUME", fmt("%d%%", settings_.music_volume), Kind::Value, 0,
                       settings_.music_volume});
        out.push_back({"SFX VOLUME", fmt("%d%%", settings_.sfx_volume), Kind::Value, 1,
                       settings_.sfx_volume});
        out.push_back({"ENGINE VOLUME", fmt("%d%%", settings_.engine_volume), Kind::Value, 2,
                       settings_.engine_volume});
        out.push_back({"MASTER VOLUME", fmt("%d%%", settings_.volume), Kind::Value, 3,
                       settings_.volume});
        out.push_back({"MUTE", kOnOff[settings_.muted ? 1 : 0], Kind::Value, 4});
        out.push_back({"AUDIO MIX",
                       settings_.audio_preset == 0 ? "STOCK" : "HOJO'S MIX",
                       Kind::Value, 5});
        out.push_back({"RESTORE EMILY / LUCY VOICES",
                       kOnOff[settings_.restore_female_voices ? 1 : 0],
                       Kind::Value, 7});
        out.push_back({"ADDITIONAL RESTORED VOICES",
                       kOnOff[settings_.restore_additional_voices ? 1 : 0],
                       Kind::Value, 8});
        out.push_back({"SOUND TEST", "", Kind::Action, 6});
        break;
    case 4:
        if (mixer_page_ == 0) {
            const bool stock = settings_.audio_preset == 0;
            for (int i = 0; i < 8; ++i)
                out.push_back({"YM" + std::to_string(i + 1),
                               std::to_string(stock ? 100 : settings_.ym_volume[i]) + "/" +
                                   std::to_string(stock ? 0 : settings_.ym_send[i]),
                               Kind::Value, i, stock ? 100 : settings_.ym_volume[i]});
            for (int i = 0; i < 16; ++i)
                out.push_back({pcm_channel_name(i),
                               std::to_string(stock ? 100 : settings_.pcm_volume[i]) + "/" +
                                   std::to_string(stock ? 0 : settings_.pcm_send[i]),
                               Kind::Value, i + 8, stock ? 100 : settings_.pcm_volume[i]});
            out.push_back({"CHORUS", "", Kind::Action, 31});
            out.push_back({"REVERB", "", Kind::Action, 34});
            out.push_back({"A/B", settings_.ab_original ? "ORIG" : "MIX",
                           Kind::Action, 33});
            out.push_back({"RESET", "", Kind::Action, 30});
            out.push_back({"SFX", "", Kind::Action, 35});
        } else if (mixer_page_ == 3) {
            const bool stock = settings_.audio_preset == 0;
            for (int i = 0; i < Settings::kSfxChannels; ++i)
                out.push_back({sfx_channel_name(i),
                               std::to_string(stock ? 100 : settings_.sfx_lane_volume[i]),
                               Kind::Value, 300 + i,
                               stock ? 100 : settings_.sfx_lane_volume[i]});
            out.push_back({"BACK", "", Kind::Action, 36});
        } else if (mixer_page_ == 1) {
            out.push_back({"CHORUS", kOnOff[settings_.chorus_enabled ? 1 : 0],
                           Kind::Value, 24});
            out.push_back({"RATE", fmt_rate(settings_.chorus_rate), Kind::Value, 25});
            out.push_back({"DEPTH", fmt("%d%%", settings_.chorus_depth), Kind::Value, 26});
            out.push_back({"DELAY", fmt("%d MS", settings_.chorus_delay), Kind::Value, 27});
            out.push_back({"FEEDBACK", fmt("%d%%", settings_.chorus_feedback),
                           Kind::Value, 28});
            out.push_back({"RETURN", fmt("%d%%", settings_.chorus_mix), Kind::Value, 29});
            out.push_back({"BACK", "", Kind::Action, 32});
        } else {
            out.push_back({"REVERB", kOnOff[settings_.reverb_enabled ? 1 : 0],
                           Kind::Value, 40});
            out.push_back({"DECAY", fmt("%d%%", settings_.reverb_decay), Kind::Value, 41});
            out.push_back({"DAMPING", fmt("%d%%", settings_.reverb_damping),
                           Kind::Value, 42});
            out.push_back({"PREDELAY", fmt("%d MS", settings_.reverb_predelay),
                           Kind::Value, 43});
            out.push_back({"RETURN", fmt("%d%%", settings_.reverb_mix), Kind::Value, 44});
            out.push_back({"BACK", "", Kind::Action, 32});
        }
        break;
    case 5:
    default:
        out.push_back({"RESET GAME", "", Kind::Action, 0});
        out.push_back({"CLOSE MENU", "", Kind::Action, 1});
        out.push_back({"QUIT", "", Kind::Action, 2});
        out.push_back({"RUN IN BACKGROUND", kOnOff[settings_.run_in_menu ? 1 : 0],
                       Kind::Value, 3});
        out.push_back({"AUTOPLAY DEMO", kOnOff[settings_.autoplay ? 1 : 0],
                       Kind::Value, 4});
        out.push_back({"SPECIAL TOUR MODE", kOnOff[settings_.special_tour ? 1 : 0],
                       Kind::Value, 5});
        out.push_back({"", "", Kind::Info, 0});
        out.push_back({"FRAME", fmt_ll(frame_), Kind::Info, 0});
        out.push_back({"FPS", fmt_fps(fps_), Kind::Info, 0});
        out.push_back({"FALLBACK", fmt_ll((long long)coverage_fallback_), Kind::Info, 0});
        out.push_back({"FALLBACK PCS", fmt_ll((long long)coverage_fallback_pcs_),
                       Kind::Info, 0});
        out.push_back({"NATIVE DISPATCH", fmt("%d%%", coverage_dispatch_pct_),
                       Kind::Info, 0});
        out.push_back({"NATIVE RTNS",
                       std::to_string(coverage_routines_) + "/" +
                           std::to_string(coverage_routine_total_),
                       Kind::Info, 0});
        out.push_back({"F9/F10", "RESET/COPY FALLBACK", Kind::Info, 0});
        {
            char b[32];
            std::snprintf(b, sizeof(b), "SWA %02X SWB %02X",
                          settings_.dip_a(), settings_.dip_b());
            out.push_back({b, "", Kind::Info, 0});
        }
        break;
    }
    return out;
}

void Menu::change(int delta) {
    const std::vector<Item> list = items();
    if (sel_ < 0 || sel_ >= (int)list.size()) return;
    const Item &it = list[sel_];
    if (it.kind != Kind::Value) return;

    switch (tab_) {
    case 0:
        if (it.id == 0) settings_.steer_deadzone = std::clamp(settings_.steer_deadzone + delta, 0, 40);
        else if (it.id == 1) settings_.pedal_deadzone = std::clamp(settings_.pedal_deadzone + delta, 0, 20);
        else if (it.id == 2) settings_.steer_rate = std::clamp(settings_.steer_rate + delta, 1, 24);
        else if (it.id == 3) settings_.centre_rate = std::clamp(settings_.centre_rate + delta, 0, 10);
        else if (it.id == 4) settings_.steer_rebound = std::clamp(settings_.steer_rebound + delta, 0, 32);
        else if (it.id == 5) settings_.push_start = delta > 0;
        else if (it.id == 12) settings_.racing_wheel = delta > 0;
        else if (it.id == 13) settings_.auto_gear = delta > 0;
        else if (it.id == 7) settings_.pad_steer_rate = std::clamp(settings_.pad_steer_rate + delta, 0, 24);
        else if (it.id == 8) settings_.steer_rebound_speed = std::clamp(settings_.steer_rebound_speed + delta, 1, 10);
        break;
    case 1:
        if (it.id == 0) settings_.coin_a = (uint8_t)wrap(settings_.coin_a, 0, 15, delta);
        else if (it.id == 1) settings_.coin_b = (uint8_t)wrap(settings_.coin_b, 0, 15, delta);
        else if (it.id == 2) settings_.cabinet = (uint8_t)wrap(settings_.cabinet, 0, 3, delta);
        else if (it.id == 3) settings_.demo_sounds = (uint8_t)wrap(settings_.demo_sounds, 0, 1, delta);
        else if (it.id == 4) settings_.credits = (uint8_t)wrap(settings_.credits, 0, 3, delta);
        else if (it.id == 5) settings_.allow_continue = (uint8_t)wrap(settings_.allow_continue, 0, 1, delta);
        else if (it.id == 6) settings_.difficulty = (uint8_t)wrap(settings_.difficulty, 0, 3, delta);
        break;
    case 2:
        if (it.id == 0) {
            int index = 0;
            for (int i = 0; i < (int)(sizeof(kAspectOrder) / sizeof(kAspectOrder[0])); ++i)
                if (kAspectOrder[i] == settings_.aspect) index = i;
            const int count = (int)(sizeof(kAspectOrder) / sizeof(kAspectOrder[0]));
            index = (index + (delta > 0 ? 1 : count - 1)) % count;
            settings_.aspect = kAspectOrder[index];
        } else if (it.id == 1) settings_.scale = wrap(settings_.scale, 1, 5, delta);
        else if (it.id == 2) {
            const int rates[3] = {60, 120, 144};
            int i = 0;
            while (i < 3 && rates[i] != settings_.frame_rate) ++i;
            if (i == 3) i = 1;
            i = (i + (delta > 0 ? 1 : 2)) % 3;
            settings_.frame_rate = rates[i];
        } else if (it.id == 3) settings_.show_fps = delta > 0;
        else if (it.id == 4) settings_.show_gear = delta > 0;
        else if (it.id == 5) settings_.gear_corner = wrap(settings_.gear_corner, 0, 3, delta);
        else if (it.id == 6) settings_.fullscreen = delta > 0;
        break;
    case 3:
        if (it.id == 0)
            settings_.music_volume =
                std::clamp(settings_.music_volume + delta * 5, 0, Settings::kBusMax);
        else if (it.id == 1)
            settings_.sfx_volume =
                std::clamp(settings_.sfx_volume + delta * 5, 0, Settings::kBusMax);
        else if (it.id == 2)
            settings_.engine_volume =
                std::clamp(settings_.engine_volume + delta * 5, 0, Settings::kBusMax);
        else if (it.id == 3)
            settings_.volume =
                std::clamp(settings_.volume + delta * 5, 0, Settings::kBusMax);
        else if (it.id == 4) settings_.muted = delta > 0;
        else if (it.id == 5) {
            settings_.audio_preset = delta > 0 ? 1 : 0;
            settings_.ab_original = false;
        }
        else if (it.id == 7) settings_.restore_female_voices = delta > 0;
        else if (it.id == 8) settings_.restore_additional_voices = delta > 0;
        break;
    case 4: {
        if (settings_.audio_preset == 0) break; // Stock bypasses channel trims.
        if (it.id >= 300 && it.id < 300 + Settings::kSfxChannels) {
            settings_.sfx_lane_volume[it.id - 300] =
                std::clamp(settings_.sfx_lane_volume[it.id - 300] + delta * 5,
                           0, Settings::kChannelMax);
            break;
        }
        if (it.id >= 40) {
            const int c = it.id - 40;
            if (c == 0) settings_.reverb_enabled = delta > 0;
            else if (c == 1) settings_.reverb_decay = std::clamp(settings_.reverb_decay + delta * 5, 0, 100);
            else if (c == 2) settings_.reverb_damping = std::clamp(settings_.reverb_damping + delta * 5, 0, 100);
            else if (c == 3) settings_.reverb_predelay = std::clamp(settings_.reverb_predelay + delta, 0, 50);
            else if (c == 4) settings_.reverb_mix = std::clamp(settings_.reverb_mix + delta * 5, 0, 100);
            break;
        }
        if (it.id >= 24) {
            const int c = it.id - 24;
            if (c == 0) settings_.chorus_enabled = delta > 0;
            else if (c == 1) settings_.chorus_rate = std::clamp(settings_.chorus_rate + delta, 1, 50);
            else if (c == 2) settings_.chorus_depth = std::clamp(settings_.chorus_depth + delta * 5, 0, 100);
            else if (c == 3) settings_.chorus_delay = std::clamp(settings_.chorus_delay + delta, 5, 30);
            else if (c == 4) settings_.chorus_feedback = std::clamp(settings_.chorus_feedback + delta * 5, 0, 90);
            else if (c == 5) settings_.chorus_mix = std::clamp(settings_.chorus_mix + delta * 5, 0, 100);
            break;
        }
        if (it.id < 8)
            settings_.ym_volume[it.id] =
                std::clamp(settings_.ym_volume[it.id] + delta * 5, 0, Settings::kChannelMax);
        else
            settings_.pcm_volume[it.id - 8] =
                std::clamp(settings_.pcm_volume[it.id - 8] + delta * 5, 0,
                           Settings::kChannelMax);
        break;
    }
    case 5:
        if (it.id == 3) settings_.run_in_menu = delta > 0;
        else if (it.id == 4) settings_.autoplay = delta > 0;
        else if (it.id == 5) settings_.special_tour = delta > 0;
        else if (it.id == 6)
            settings_.tour_start_course =
                (settings_.tour_start_course + (delta > 0 ? 1 : 4)) % 5;
        break;
    default:
        break;
    }
    settings_.clamp();
}

void Menu::activate() {
    const std::vector<Item> list = items();
    if (sel_ < 0 || sel_ >= (int)list.size()) return;
    const Item &it = list[sel_];
    if (it.kind != Kind::Value) return;

    // Toggles flip; everything else advances one step.
    const bool toggle = (tab_ == 0 && (it.id == 5 || it.id == 12 || it.id == 13)) ||
                        (tab_ == 2 && (it.id == 3 || it.id == 4 || it.id == 6)) ||
                        (tab_ == 3 && it.id == 4) ||
                        (tab_ == 3 && it.id == 5) ||
                        (tab_ == 3 && (it.id == 7 || it.id == 8)) ||
                        (tab_ == 4 && mixer_page_ == 1 && it.id == 24) ||
                        (tab_ == 4 && mixer_page_ == 2 && it.id == 40) ||
                        (tab_ == 5 && (it.id == 3 || it.id == 4 || it.id == 5));
    if (toggle) {
        if (tab_ == 0 && it.id == 5) settings_.push_start = !settings_.push_start;
        else if (tab_ == 0 && it.id == 12)
            settings_.racing_wheel = !settings_.racing_wheel;
        else if (tab_ == 0 && it.id == 13)
            settings_.auto_gear = !settings_.auto_gear;
        else if (tab_ == 2 && it.id == 3) settings_.show_fps = !settings_.show_fps;
        else if (tab_ == 2 && it.id == 4) settings_.show_gear = !settings_.show_gear;
        else if (tab_ == 2 && it.id == 6) settings_.fullscreen = !settings_.fullscreen;
        else if (tab_ == 3 && it.id == 4) settings_.muted = !settings_.muted;
        else if (tab_ == 3 && it.id == 5) {
            settings_.audio_preset = settings_.audio_preset ? 0 : 1;
            settings_.ab_original = false;
        }
        else if (tab_ == 3 && it.id == 7)
            settings_.restore_female_voices = !settings_.restore_female_voices;
        else if (tab_ == 3 && it.id == 8)
            settings_.restore_additional_voices = !settings_.restore_additional_voices;
        else if (tab_ == 5 && it.id == 3) settings_.run_in_menu = !settings_.run_in_menu;
        else if (tab_ == 5 && it.id == 4) settings_.autoplay = !settings_.autoplay;
        else if (tab_ == 5 && it.id == 5) settings_.special_tour = !settings_.special_tour;
        else if (tab_ == 4 && it.id == 24)
            settings_.chorus_enabled = !settings_.chorus_enabled;
        else if (tab_ == 4 && it.id == 40)
            settings_.reverb_enabled = !settings_.reverb_enabled;
    } else {
        change(+1);
    }
}

MenuResult Menu::key(MenuKey k) {
    MenuResult r;
    if (binding_ >= 0) {
        if (k == MenuKey::Back) binding_ = -1;
        return r;
    }
    if (tab_ == 4 && mixer_locked_) {
        if (k == MenuKey::Enter) {
            mixer_locked_ = false;
            return r;
        }
        if (k != MenuKey::Back && k != MenuKey::TabPrev &&
            k != MenuKey::TabNext)
            return r;
    }
    const int n = (int)items().size();
    const bool mixer_channels = (tab_ == 4 && mixer_page_ == 0);
    const bool sfx_channels = (tab_ == 4 && mixer_page_ == 3);
    auto move_selection = [&](int delta) {
        const std::vector<Item> list = items();
        if (list.empty()) return;
        const int count = (int)list.size();
        const int start = std::clamp(sel_, 0, count - 1);
        for (int step = 1; step <= count; ++step) {
            const int i = ((start + delta * step) % count + count) % count;
            if (list[i].kind != Kind::Info) {
                sel_ = i;
                return;
            }
        }
    };
    switch (k) {
    case MenuKey::Up:
        if (mixer_channels || sfx_channels) change(+1);
        else move_selection(-1);
        break;
    case MenuKey::Down:
        if (mixer_channels || sfx_channels) change(-1);
        else move_selection(+1);
        break;
    case MenuKey::Left:
        if (mixer_channels || sfx_channels) {
            if (n) sel_ = (sel_ + n - 1) % n;
        } else {
            change(-1);
        }
        break;
    case MenuKey::Right:
        if (mixer_channels || sfx_channels) {
            if (n) sel_ = (sel_ + 1) % n;
        } else {
            change(+1);
        }
        break;
    case MenuKey::Enter: {
        if (tab_ == 3 && audio_page_ == 1) {
            const std::vector<Item> list = items();
            if (sel_ >= 0 && sel_ < (int)list.size()) {
                const int id = list[sel_].id;
                if (id >= 0x90 && id <= 0x97) r.sound_cmd = id;
                else if (id == 0x100) r.sound_cmd = 0xFF;
                else if (id == 0x101) {
                    audio_page_ = 0;
                    sel_ = 0;
                }
            }
            break;
        }
        const std::vector<Item> list = items();
        if (sel_ >= 0 && sel_ < (int)list.size() && list[sel_].kind == Kind::Action) {
            const int id = list[sel_].id;
            if (id == 0) r.reset = true;
            else if (id == 1) r.close = true;
            else if (id == 2) r.quit = true;
            else if (id == 10 && tab_ == 0) {
                input_page_ = 1;
                sel_ = 0;
            } else if (id == 11 && tab_ == 0) {
                input_page_ = 2;
                sel_ = 0;
            } else if (tab_ == 0 && input_page_ == 1 &&
                       ((id >= 100 && id <= 106) || (id >= 120 && id <= 122))) {
                binding_ = id;
            } else if (tab_ == 0 && input_page_ == 1 && id == 110) {
                settings_.reset_gamepad();
            } else if (tab_ == 0 && input_page_ == 1 && id == 111) {
                input_page_ = 0;
                sel_ = 0;
            } else if (id == 6 && tab_ == 3) {
                audio_page_ = 1;
                sel_ = 0;
            } else if (id == 30)
                settings_.reset_mixer(settings_.audio_preset == 0);
            else if (id == 31) {
                mixer_page_ = 1;
                sel_ = 0;
            } else if (id == 32) {
                mixer_page_ = 0;
                sel_ = 0;
            } else if (id == 33) {
                if (mixer_mode_ || settings_.audio_preset != 0)
                    settings_.ab_original = !settings_.ab_original;
            } else if (id == 34) {
                mixer_page_ = 2;
                sel_ = 0;
            } else if (id == 35) {
                mixer_page_ = 3;
                sel_ = 0;
            } else if (id == 36) {
                mixer_page_ = 0;
                sel_ = 0;
            }
        } else if (!mixer_channels) {
            activate();
        }
        break;
    }
    case MenuKey::TabPrev:
        if (mixer_mode_)
            tab_ = (tab_ == 3) ? 4 : 3;
        else
            tab_ = (tab_ + kTabCount - 1) % kTabCount;
        sel_ = 0;
        mixer_page_ = 0;
        mixer_locked_ = true;
        input_page_ = 0;
        audio_page_ = (mixer_mode_ && tab_ == 3) ? 1 : 0;
        break;
    case MenuKey::TabNext:
        if (mixer_mode_)
            tab_ = (tab_ == 3) ? 4 : 3;
        else
            tab_ = (tab_ + 1) % kTabCount;
        sel_ = 0;
        mixer_page_ = 0;
        mixer_locked_ = true;
        input_page_ = 0;
        audio_page_ = (mixer_mode_ && tab_ == 3) ? 1 : 0;
        break;
    case MenuKey::Back:
        if (mixer_mode_ && tab_ == 3) {
            tab_ = 4;
            sel_ = 0;
            audio_page_ = 0;
            mixer_page_ = 0;
        } else if (tab_ == 3 && audio_page_ == 1) {
            audio_page_ = 0;
            sel_ = 0;
        } else if (tab_ == 0 && input_page_ != 0) {
            input_page_ = 0;
            sel_ = 0;
        } else if (tab_ == 4 && mixer_page_ != 0) {
            mixer_page_ = 0;
            sel_ = 0;
        } else {
            r.close = true;
        }
        break;
    case MenuKey::Plus:
        if (mixer_channels && sel_ >= 0 && sel_ < 24) {
            const bool is_ym = sel_ < 8;
            const int i = is_ym ? sel_ : sel_ - 8;
            int *v = is_ym
                         ? (send_focus_ == 0 ? &settings_.ym_send[i]
                                             : &settings_.ym_reverb_send[i])
                         : (send_focus_ == 0 ? &settings_.pcm_send[i]
                                             : &settings_.pcm_reverb_send[i]);
            *v = std::clamp(*v + 5, 0, 100);
        } else {
            change(+1);
        }
        break;
    case MenuKey::Minus:
        if (mixer_channels && sel_ >= 0 && sel_ < 24) {
            const bool is_ym = sel_ < 8;
            const int i = is_ym ? sel_ : sel_ - 8;
            int *v = is_ym
                         ? (send_focus_ == 0 ? &settings_.ym_send[i]
                                             : &settings_.ym_reverb_send[i])
                         : (send_focus_ == 0 ? &settings_.pcm_send[i]
                                             : &settings_.pcm_reverb_send[i]);
            *v = std::clamp(*v - 5, 0, 100);
        } else {
            change(-1);
        }
        break;
    case MenuKey::Solo:
        if (settings_.audio_preset == 0) break;
        if (mixer_channels && sel_ >= 0 && sel_ < 24) {
            const int bit = sel_ < 8 ? sel_ : 8 + (sel_ - 8);
            settings_.solo_mask ^= (1u << bit);
        } else if (sfx_channels && sel_ >= 0 && sel_ < Settings::kSfxChannels) {
            settings_.solo_mask ^= (1u << (24 + sel_));
        }
        break;
    case MenuKey::SendSelect:
        if (mixer_channels && settings_.audio_preset != 0)
            send_focus_ = 1 - send_focus_;
        break;
    }
    const std::vector<Item> final_items = items();
    const int m = (int)final_items.size();
    if (m && sel_ >= m) sel_ = m - 1;
    if (m && sel_ < 0) sel_ = 0;
    if (m && final_items[sel_].kind == Kind::Info) move_selection(+1);
    if (r.close) {
        open_ = false;
        mixer_locked_ = true;
        mixer_page_ = 0;
        audio_page_ = 0;
        input_page_ = 0;
    }
    return r;
}

void Menu::bind_button(int button) {
    if (binding_ < 100 || binding_ > 106) return;
    if (button < 0 || button >= kPadButtonCount) {
        binding_ = -1;
        return;
    }
    switch (binding_) {
    case 100: settings_.pad_steer_left = button; break;
    case 101: settings_.pad_steer_right = button; break;
    case 102: settings_.pad_gas = button; break;
    case 103: settings_.pad_brake = button; break;
    case 104: settings_.pad_gear = button; break;
    case 105: settings_.pad_start = button; break;
    case 106: settings_.pad_coin = button; break;
    default: break;
    }
    binding_ = -1;
}

void Menu::bind_axis(int axis, int value) {
    if (binding_ < 120 || binding_ > 122) return;
    if (axis < 0 || axis >= kPadAxisCount) {
        binding_ = -1;
        return;
    }
    switch (binding_) {
    case 120:
        settings_.pad_steer_axis = axis;
        break;
    case 121:
        settings_.pad_gas_axis = axis;
        settings_.pad_gas_axis_invert = value < 0;
        break;
    case 122:
        settings_.pad_brake_axis = axis;
        settings_.pad_brake_axis_invert = value < 0;
        break;
    default:
        break;
    }
    binding_ = -1;
}

void Menu::set_levels(const float *ym_levels, const float *pcm_levels) {
    if (ym_levels)
        for (int i = 0; i < 8; ++i) ym_level_[i] = std::clamp(ym_levels[i], 0.0f, 1.0f);
    if (pcm_levels)
        for (int i = 0; i < 16; ++i) pcm_level_[i] = std::clamp(pcm_levels[i], 0.0f, 1.0f);
}

void Menu::set_sfx_levels(const float *levels) {
    if (levels)
        for (int i = 0; i < Settings::kSfxChannels; ++i)
            sfx_level_[i] = std::clamp(levels[i], 0.0f, 1.0f);
}
namespace menu_internal {

std::string fmt(const char *f, int v) { return ::likethewind::fmt(f, v); }
std::string fmt_fps(double v) { return ::likethewind::fmt_fps(v); }
std::string fmt_rate(int tenths) { return ::likethewind::fmt_rate(tenths); }
std::string pcm_channel_name(int channel) {
    return ::likethewind::pcm_channel_name(channel);
}
std::string sfx_channel_name(int channel) {
    return ::likethewind::sfx_channel_name(channel);
}
std::string pad_axis_name(int axis, bool invert) {
    return ::likethewind::pad_axis_name(axis, invert);
}

} // namespace menu_internal

} // namespace likethewind
