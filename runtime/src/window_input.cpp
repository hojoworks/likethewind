#include "window_input.h"

#ifdef LIKETHEWIND_HAVE_SDL
#include <algorithm>
#include <cmath>

namespace likethewind {

uint8_t clamp_wheel(int v) {
    return (uint8_t)std::clamp(v, 0x20, 0xE0);
}

// Analog triggers rest at 0 and reach 32767. MAME's Power Drift pedals rest at
// 0x00 and fully press at 0xFF, so scale with the configured dead zone.
uint8_t pedal_from_axis(Sint16 v, int dead_pct) {
    const int dead = std::clamp(dead_pct, 0, 20) * 32767 / 100;
    if (v <= dead) return 0;
    return (uint8_t)std::min(255, (v - dead) * 255 / (32767 - dead));
}

Sint16 controller_axis(SDL_GameController *pad, int axis) {
    if (!pad || axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX) return 0;
    return SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)axis);
}

uint8_t pedal_from_controller_axis(SDL_GameController *pad, int axis, bool invert,
                                   int dead_pct) {
    int value = controller_axis(pad, axis);
    if (invert) value = -value;
    if (value <= 0) return 0;
    return pedal_from_axis((Sint16)std::min(value, 32767), dead_pct);
}

bool pad_button(SDL_GameController *pad, int button) {
    return pad && button >= 0 && button < SDL_CONTROLLER_BUTTON_MAX &&
           SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)button);
}

constexpr int kStickEngageFrames = 3;
constexpr int kStickReleaseFrames = 3;
// The game's internal speed word. 240 corresponds to the 160 km/h shift point.
constexpr uint16_t kAutoGearSpeed = 240;
constexpr uint16_t kAutoGearAddress = 0xEA12;

uint8_t auto_gear_bit(const Machine &m) {
    const uint16_t speed =
        (uint16_t)((m.shared[kAutoGearAddress] << 8) |
                   m.shared[kAutoGearAddress + 1]);
    return speed >= kAutoGearSpeed ? 0x20 : 0;
}

// High-score name entry uses the full steering range as a carousel. The
// carousel zones are not evenly spaced: the first and last symbols are much
// wider than the middle ones, so a fixed wheel nudge either gets stuck at the
// ends or skips letters near the centre. These are the centre positions of
// each measured zone (MAME paddle 0x20..0xE0).
constexpr int kCourseTargets[] = {0x40, 0x60, 0x80, 0xA0, 0xC0};

int course_index_from_wheel(int wheel) {
    if (wheel < 0x58) return 0;
    if (wheel < 0x68) return 1;
    if (wheel < 0x98) return 2;
    if (wheel < 0xA8) return 3;
    return 4;
}

// Measured zone centres around the name-entry carousel. The alphabet wraps
// through P..Z, the symbols and END, then A..O, so the first and last letter
// zones are much wider than the narrow A/B and Q zones. Using the centre of
// each zone avoids skipping A going right or Q going left.
constexpr int kNameEntryTargets[] = {
    0x36, 0x49, 0x4D, 0x50, 0x53, 0x56, 0x5A, 0x5F,
    0x62, 0x65, 0x68, 0x6B, 0x6F, 0x74, 0x77, 0x7A,
    0x83, 0x86, 0x89, 0x8C, 0x8F, 0x92, 0x96, 0x9B,
    0x9E, 0xA1, 0xA4, 0xA7, 0xAB, 0xB0, 0xB3, 0xB6,
};

int update_wheel(int wheel, SteeringState &state, int ax, bool left, bool right,
                 const Settings &s) {
    const int dead = s.steer_deadzone * 32767 / 100;
    const bool outside = std::abs(ax) > dead;
    bool holding_stick = false;

    if (state.stick_engaged) {
        if (outside) {
            state.release_hold = 0;
        } else if (++state.release_hold >= kStickReleaseFrames) {
            state.stick_engaged = false;
            state.release_hold = 0;
            state.engage_hold = 0;
        } else {
            holding_stick = true;
        }
    } else if (outside) {
        if (++state.engage_hold >= kStickEngageFrames) {
            state.stick_engaged = true;
            state.engage_hold = 0;
            state.release_hold = 0;
        } else {
            holding_stick = true;
        }
    } else {
        state.engage_hold = 0;
    }

    // Keyboard/button steering wins immediately; a wobbling stick must be
    // deliberately held outside the dead zone before it can cancel rebound.
    if (holding_stick && !left && !right)
        return clamp_wheel(wheel);
    if (left || right) {
        state.stick_engaged = false;
        state.engage_hold = state.release_hold = 0;
    }

    if (state.stick_engaged) {
        const long long range = (long long)ax * 0x60 / 32767LL;
        const int target = clamp_wheel(0x80 + (int)range);
        if (s.pad_steer_rate <= 0) {
            wheel = target;
        } else {
            const int delta = target - wheel;
            const int step = s.pad_steer_rate;
            wheel += std::abs(delta) <= step ? delta : (delta > 0 ? step : -step);
        }
        state.rebound = false;
    } else if (left && !right) {
        wheel -= s.steer_rate;
        state.rebound = false;
    } else if (right && !left) {
        wheel += s.steer_rate;
        state.rebound = false;
    } else if (wheel != 0x80 &&
               (state.rebound ? s.steer_rebound_speed : s.centre_rate) > 0) {
        // Exponential return; once it crosses centre, add a counter-steer
        // pulse. The rebound has its own speed so the pulse can be tuned
        // independently of the normal centring.
        const int rate = state.rebound ? s.steer_rebound_speed : s.centre_rate;
        const int offset = wheel - 0x80;
        const int step = std::max(1, (std::abs(offset) * rate + 7) / 16);
        if (std::abs(offset) <= step) {
            if (!state.rebound && s.steer_rebound > 0) {
                wheel = 0x80 - (offset > 0 ? s.steer_rebound : -s.steer_rebound);
                state.rebound = true;
            } else {
                wheel = 0x80;
                state.rebound = false;
            }
        } else {
            wheel -= offset > 0 ? step : -step;
        }
    } else if (wheel == 0x80) {
        state.rebound = false;
    }
    return clamp_wheel(wheel);
}

void apply_input(Machine &m, const Uint8 *keys, SDL_GameController *pad,
                 bool &gear, bool &prevGear, SteeringState &steer_state,
                 const Settings &s, bool menu_open) {
    if (menu_open) {
        // The menu keys should not drive the car. Keep the gear selection,
        // release the pedals and centre the wheel while the game runs behind
        // the menu.
        steer_state = {};
        m.in_wheel = 0x80;
        m.in_gas = 0;
        m.in_brake = 0;
        uint8_t g = 0xDF;
        if (s.auto_gear) gear = (auto_gear_bit(m) != 0);
        if (gear) g |= 0x20;
        m.in_general = g;
        // Don't let the Space/Enter used to close the menu also toggle gear
        // on the first frame after it closes.
        prevGear = keys[SDL_SCANCODE_SPACE] ||
                   (pad && (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y) ||
                            SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)));
        return;
    }

    // Steering: left stick (analog, with dead zone), d-pad or A/D/arrows
    // (slew), and a self-centre when nothing is held. The cabinet wheel range
    // is 0x20..0xE0 with 0x80 centred.
    const Sint16 ax = controller_axis(pad, s.pad_steer_axis);
    const bool left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A] ||
                      pad_button(pad, s.pad_steer_left);
    const bool right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D] ||
                       pad_button(pad, s.pad_steer_right);
    const int mode = (m.shared[0xE09C] << 8) | m.shared[0xE09D];
    // Mode 0x1B is the measured high-score name-entry screen. Modes 0x18/0x19
    // are the continue/yes-no screens and must not use the letter carousel.
    const bool name_entry = (mode == 0x1B);
    const bool wheel_menu = (mode == 8) || (mode >= 0x18 && mode <= 0x1B);
    // Course select and the post-race name-entry screens use the wheel as a
    // discrete left/right chooser rather than a continuously centring axis.
    if (wheel_menu) {
        // Analog movement slews toward the requested position for fine control;
        // a real wheel takes the axis position directly.
        const int dead = std::clamp(s.steer_deadzone, 0, 40) * 32767 / 100;
        if (std::abs(ax) > dead) {
            steer_state.entry_active = false;
            const long long range = (long long)ax * 0x60 / 32767LL;
            const int target = clamp_wheel(0x80 + (int)range);
            if (s.racing_wheel || s.pad_steer_rate <= 0) {
                m.in_wheel = (uint8_t)target;
            } else {
                const int step = std::max(1, s.pad_steer_rate);
                const int delta = target - (int)m.in_wheel;
                m.in_wheel = (uint8_t)clamp_wheel(
                    (int)m.in_wheel + std::clamp(delta, -step, step));
            }
        } else if (!s.racing_wheel) {
            if (name_entry) {
                // Treat the wheel as a carousel: one edge press advances one
                // measured symbol zone, regardless of how wide that zone is.
                if (!steer_state.entry_active) {
                    // Name entry always opens on END; the old wheel position
                    // must not decide whether the first right press lands on A.
                    steer_state.entry_index = 16;
                    steer_state.entry_active = true;
                }
                constexpr int kEntryTargetCount =
                    (int)(sizeof(kNameEntryTargets) /
                          sizeof(kNameEntryTargets[0]));
                if (left && !steer_state.left_prev &&
                    steer_state.entry_index > 0)
                    --steer_state.entry_index;
                if (right && !steer_state.right_prev &&
                    steer_state.entry_index + 1 < kEntryTargetCount)
                    ++steer_state.entry_index;
                m.in_wheel = (uint8_t)kNameEntryTargets[steer_state.entry_index];
            } else if (mode == 8) {
                // Jump to the next/previous canonical course zone. A fixed
                // nudge can miss a zone when the wheel stopped between two
                // detents after a stage.
                int index = course_index_from_wheel(m.in_wheel);
                if (left && !steer_state.left_prev && index > 0) --index;
                if (right && !steer_state.right_prev && index < 4) ++index;
                m.in_wheel = (uint8_t)kCourseTargets[index];
            } else {
                constexpr int kCourseNudge = 0x20;
                if (left && !steer_state.left_prev)
                    m.in_wheel =
                        (uint8_t)clamp_wheel((int)m.in_wheel - kCourseNudge);
                if (right && !steer_state.right_prev)
                    m.in_wheel =
                        (uint8_t)clamp_wheel((int)m.in_wheel + kCourseNudge);
            }
        }
    } else if (s.racing_wheel && pad) {
        // A wheel is an absolute steering device: do not auto-centre or add
        // slew/rebound between the axis and the game.
        const int dead = std::clamp(s.steer_deadzone, 0, 40) * 32767 / 100;
        if (std::abs(ax) > dead) {
            const long long range = (long long)ax * 0x60 / 32767LL;
            m.in_wheel = (uint8_t)clamp_wheel(0x80 + (int)range);
        } else {
            m.in_wheel = 0x80;
        }
    } else {
        m.in_wheel =
            (uint8_t)update_wheel(m.in_wheel, steer_state, ax, left, right, s);
    }
    if (!name_entry) steer_state.entry_active = false;
    steer_state.left_prev = left;
    steer_state.right_prev = right;

    // Pedals: assigned axes and buttons, plus Up/W and Down/S.
    // MAME's defaults are 0x00 released, 0xFF fully pressed.
    int gas = (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) ? 255 : 0;
    int brake = (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) ? 255 : 0;
    if (pad) {
        if (pad_button(pad, s.pad_gas)) gas = 255;
        if (pad_button(pad, s.pad_brake)) brake = 255;
        gas = std::max(gas, (int)pedal_from_controller_axis(
                                pad, s.pad_gas_axis, s.pad_gas_axis_invert,
                                s.pedal_deadzone));
        brake = std::max(brake, (int)pedal_from_controller_axis(
                                    pad, s.pad_brake_axis,
                                    s.pad_brake_axis_invert, s.pedal_deadzone));
    }
    m.in_gas = (uint8_t)gas;
    m.in_brake = (uint8_t)brake;

    // General port: active-low start/coin/service/test. Bit 5 is the gear
    // switch, which rests low (MAME's GENERAL default is 0xDF).
    uint8_t g = 0xDF;
    const bool start_down = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_1] ||
                            pad_button(pad, s.pad_start);
    const bool pass_start = m.start_button(start_down);
    if (pass_start) g &= (uint8_t)~0x08;                                        // start
    if (keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_5] ||
        pad_button(pad, s.pad_coin))
        g &= (uint8_t)~0x40;                                                    // coin
    if (keys[SDL_SCANCODE_F1]) g &= (uint8_t)~0x04;                            // service
    if (keys[SDL_SCANCODE_F2]) g &= (uint8_t)~0x02;                            // test

    const bool gear_pressed = keys[SDL_SCANCODE_SPACE] ||
                              pad_button(pad, s.pad_gear);
    if (s.auto_gear)
        gear = (auto_gear_bit(m) != 0);
    else if (gear_pressed && !prevGear)
        gear = !gear;
    prevGear = gear_pressed;
    if (gear) g |= 0x20;   // active high on Power Drift
    m.in_general = g;
}

bool menu_key_from_sym(SDL_Keycode sym, MenuKey &k) {
    switch (sym) {
    case SDLK_UP: k = MenuKey::Up; return true;
    case SDLK_DOWN: k = MenuKey::Down; return true;
    case SDLK_LEFT: k = MenuKey::Left; return true;
    case SDLK_RIGHT: k = MenuKey::Right; return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE: k = MenuKey::Enter; return true;
    case SDLK_q:
    case SDLK_PAGEUP: k = MenuKey::TabPrev; return true;
    case SDLK_e:
    case SDLK_PAGEDOWN: k = MenuKey::TabNext; return true;
    case SDLK_BACKSPACE: k = MenuKey::Back; return true;
    case SDLK_PLUS:
    case SDLK_EQUALS:
    case SDLK_KP_PLUS: k = MenuKey::Plus; return true;
    case SDLK_MINUS:
    case SDLK_KP_MINUS: k = MenuKey::Minus; return true;
    case SDLK_s: k = MenuKey::Solo; return true;
    case SDLK_r: k = MenuKey::SendSelect; return true;
    default: return false;
    }
}

bool menu_key_from_button(Uint8 b, MenuKey &k) {
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: k = MenuKey::Up; return true;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: k = MenuKey::Down; return true;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: k = MenuKey::Left; return true;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: k = MenuKey::Right; return true;
    case SDL_CONTROLLER_BUTTON_A: k = MenuKey::Enter; return true;
    case SDL_CONTROLLER_BUTTON_B: k = MenuKey::Back; return true;
    case SDL_CONTROLLER_BUTTON_X: k = MenuKey::SendSelect; return true;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: k = MenuKey::TabPrev; return true;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: k = MenuKey::TabNext; return true;
    default: return false;
    }
}

} // namespace likethewind
#endif // LIKETHEWIND_HAVE_SDL
