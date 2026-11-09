/*
 * likethewind/menu.h — the in-game settings menu (SDL-free).
 *
 * The host maps keys/controller buttons to MenuKey, calls key(), and draws the
 * overlay into its ARGB framebuffer. Settings live in Settings; the host syncs
 * them to the Machine/audio/video each frame.
 */
#ifndef LIKETHEWIND_MENU_H
#define LIKETHEWIND_MENU_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "likethewind/settings.h"

namespace likethewind {

enum class MenuKey {
    Up,
    Down,
    Left,
    Right,
    Enter,
    TabPrev,
    TabNext,
    Back,
    Plus,
    Minus,
    Solo,
    SendSelect,
};

struct MenuResult {
    bool close = false;
    bool quit = false;
    bool reset = false;
    int sound_cmd = -1;   // sound-board command requested by the sound test
};

class Menu {
public:
    explicit Menu(Settings &settings, bool mixer_mode = false)
        : settings_(settings), mixer_mode_(mixer_mode) {
        if (mixer_mode_) {
            tab_ = 3;
            audio_page_ = 1;
        }
    }

    bool visible() const { return open_; }
    bool sound_test() const { return open_ && tab_ == 3 && audio_page_ == 1; }
    void toggle() {
        open_ = !open_;
        if (open_ && mixer_mode_) {
            tab_ = 3;
            audio_page_ = 1;
            sel_ = 0;
        }
        if (!open_) {
            mixer_page_ = 0;
            mixer_locked_ = true;
            audio_page_ = 0;
            input_page_ = 0;
            binding_ = -1;
        }
    }
    void close() {
        open_ = false;
        mixer_page_ = 0;
        mixer_locked_ = true;
        audio_page_ = 0;
        input_page_ = 0;
        binding_ = -1;
    }

    MenuResult key(MenuKey k);
    void draw(uint32_t *pixels, int width, int height) const;
    void draw_fps(uint32_t *pixels, int width, int height, double fps,
                  double present_fps, int blend_pct) const;
    void draw_gear(uint32_t *pixels, int width, int height, bool high) const;
    void draw_tour_status(uint32_t *pixels, int width, int height,
                          const std::string &title, const std::string &detail) const;
    void draw_tour_notice(uint32_t *pixels, int width, int height,
                          const std::string &text) const;
    void draw_tour_tracker(
        uint32_t *pixels, int width, int height,
        const std::array<std::array<int8_t, 5>, 5> &places,
        const std::array<int8_t, 2> &special_places, int start_course,
        const std::string &stats) const;
    void set_levels(const float *ym_levels, const float *pcm_levels);
    void set_sfx_levels(const float *levels);

    void set_status(long long frame, double fps) {
        frame_ = frame;
        fps_ = fps;
    }
    void set_display_hz(int hz) { display_hz_ = hz > 0 ? hz : 60; }
    void set_present_stats(double fps, int blend_pct) {
        present_fps_ = fps;
        blend_pct_ = blend_pct;
    }
    void set_coverage(uint64_t fallback_steps, uint64_t fallback_pcs, int dispatch_pct,
                      uint64_t native_routines, uint64_t total_routines) {
        coverage_fallback_ = fallback_steps;
        coverage_fallback_pcs_ = fallback_pcs;
        coverage_dispatch_pct_ = dispatch_pct;
        coverage_routines_ = native_routines;
        coverage_routine_total_ = total_routines;
    }
    void set_mix_track(const std::string &name) { mix_track_ = name; }
    void set_selection(int i) { sel_ = i; }
    void set_steer(int value, int wheel) {
        steer_value_ = value;
        wheel_ = wheel;
    }

    // Controller binding capture: the host feeds the next input here.
    int binding_action() const { return binding_; }
    bool binding_axis() const { return binding_ >= 120; }
    void bind_button(int button);
    void bind_axis(int axis, int value);
    void cancel_binding() { binding_ = -1; }
    void set_binding_waiting(bool waiting) { binding_waiting_ = waiting; }

private:
    enum class Kind { Value, Action, Info };
    struct Item {
        std::string label;
        std::string value;
        Kind kind = Kind::Info;
        int id = 0;
        int amount = -1;   // 0..100 for volume sliders
    };

    std::vector<Item> items() const;
    void change(int delta);
    void activate();
    void draw_mixer(uint32_t *pixels, int width, int height, int px0, int py0) const;
    void draw_chorus(uint32_t *pixels, int width, int height, int px0, int py0) const;
    void draw_reverb(uint32_t *pixels, int width, int height, int px0, int py0) const;
    void draw_controls(uint32_t *pixels, int width, int height, int px0, int py0) const;
    void draw_steering(uint32_t *pixels, int width, int height, int px0, int py0) const;
    void draw_sfx(uint32_t *pixels, int width, int height, int px0, int py0) const;

    Settings &settings_;
    bool open_ = false;
    int tab_ = 0;
    int sel_ = 0;
    int mixer_page_ = 0;   // 0 channels, 1 chorus effects, 2 plate reverb
    bool mixer_locked_ = true; // require Enter before channel edits
    int audio_page_ = 0;   // 0 audio settings, 1 sound test
    int input_page_ = 0;   // 0 input settings, 1 controller bindings, 2 steering
    int binding_ = -1;     // action awaiting a controller button, or -1
    bool binding_waiting_ = false; // release held buttons before capturing
    int steer_value_ = 0;  // live value of the selected steering axis
    int wheel_ = 0x80;     // live steering value, 0x20..0xE0
    int send_focus_ = 0;   // 0 chorus send, 1 reverb send
    float ym_level_[8] = {};
    float pcm_level_[16] = {};
    float sfx_level_[Settings::kSfxChannels] = {};
    long long frame_ = 0;
    double fps_ = 0.0;
    int display_hz_ = 60;
    double present_fps_ = 0.0;
    int blend_pct_ = 0;
    bool mixer_mode_ = false;
    std::string mix_track_;
    uint64_t coverage_fallback_ = 0;
    uint64_t coverage_fallback_pcs_ = 0;
    int coverage_dispatch_pct_ = 0;
    uint64_t coverage_routines_ = 0;
    uint64_t coverage_routine_total_ = 0;
};

} // namespace likethewind

#endif
