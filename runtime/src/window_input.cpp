#include "window_input.h"

#ifdef LIKETHEWIND_HAVE_SDL
#include "menu_sdl_keys.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

uint8_t pedal_from_signed_axis(int value, bool invert, int dead_pct) {
    if (invert) value = -value;
    if (value <= 0) return 0;
    return pedal_from_axis((Sint16)std::min(value, 32767), dead_pct);
}

InputDevices::~InputDevices() { close_all(); }

void InputDevices::close_all() {
    for (Device &device : devices_) {
        if (device.controller)
            SDL_GameControllerClose(device.controller);
        else if (device.joystick)
            SDL_JoystickClose(device.joystick);
    }
    devices_.clear();
    primary_ = nullptr;
}

void InputDevices::open_all() {
    close_all();
    const int count = SDL_NumJoysticks();
    for (int i = 0; i < count; ++i) {
        Device device;
        if (SDL_IsGameController(i)) {
            device.controller = SDL_GameControllerOpen(i);
            if (!device.controller) continue;
            device.joystick = SDL_GameControllerGetJoystick(device.controller);
            device.info.game_controller = true;
            if (!primary_) primary_ = device.controller;
        } else {
            device.joystick = SDL_JoystickOpen(i);
            if (!device.joystick) continue;
        }
        const char *name = SDL_JoystickName(device.joystick);
        device.info.name = name ? name : "Joystick";
        const SDL_JoystickGUID guid = SDL_JoystickGetGUID(device.joystick);
        char guid_text[33] = {};
        SDL_JoystickGetGUIDString(guid, guid_text, sizeof(guid_text));
        device.info.guid = guid_text;
        device.info.axes = SDL_JoystickNumAxes(device.joystick);
        device.info.buttons = SDL_JoystickNumButtons(device.joystick);
        device.info.instance = SDL_JoystickInstanceID(device.joystick);
        std::fprintf(stderr, "[input] device %d: %s (%s, %d axes, %d buttons)\n",
                     i, device.info.name.c_str(),
                     device.info.game_controller ? "game controller"
                                                 : "raw joystick",
                     device.info.axes, device.info.buttons);
        devices_.push_back(device);
    }
    if (devices_.empty())
        std::fprintf(stderr, "[input] no SDL joystick devices found\n");
}

std::vector<InputDeviceInfo> InputDevices::infos() const {
    std::vector<InputDeviceInfo> out;
    out.reserve(devices_.size());
    for (const Device &device : devices_) out.push_back(device.info);
    return out;
}

const InputDeviceInfo *InputDevices::find_by_instance(
    SDL_JoystickID instance) const {
    for (const Device &device : devices_)
        if (device.info.instance == instance) return &device.info;
    return nullptr;
}

std::string InputDevices::primary_guid() const {
    if (!primary_) return "";
    for (const Device &device : devices_)
        if (device.controller == primary_) return device.info.guid;
    return "";
}

bool InputDevices::rumble_supported() const {
    if (!primary_) return false;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    return SDL_GameControllerHasRumble(primary_) == SDL_TRUE;
#elif SDL_VERSION_ATLEAST(2, 0, 9)
    return true;
#else
    return false;
#endif
}

bool InputDevices::any_button() const {
    for (const Device &device : devices_) {
        if (device.controller) {
            for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
                if (SDL_GameControllerGetButton(
                        device.controller, (SDL_GameControllerButton)b))
                    return true;
        } else if (device.joystick) {
            const int count = SDL_JoystickNumButtons(device.joystick);
            for (int b = 0; b < count; ++b)
                if (SDL_JoystickGetButton(device.joystick, b)) return true;
        }
    }
    return false;
}

const InputDevices::Device *InputDevices::find_by_guid(
    const std::string &guid) const {
    for (const Device &device : devices_)
        if (device.info.guid == guid) return &device;
    return nullptr;
}

bool InputDevices::button(const std::string &guid, bool raw, int index) const {
    if (index < 0) return false;
    const Device *device = guid.empty() ? nullptr : find_by_guid(guid);
    if (raw) {
        return device && device->joystick && !device->controller &&
               index < SDL_JoystickNumButtons(device->joystick) &&
               SDL_JoystickGetButton(device->joystick, index) != 0;
    }
    if (device && device->controller)
        return index < SDL_CONTROLLER_BUTTON_MAX &&
               SDL_GameControllerGetButton(
                   device->controller, (SDL_GameControllerButton)index) != 0;
    if (guid.empty() && primary_)
        return index < SDL_CONTROLLER_BUTTON_MAX &&
               SDL_GameControllerGetButton(
                   primary_, (SDL_GameControllerButton)index) != 0;
    return false;
}

SDL_Joystick *InputDevices::joystick_for_guid(
    const std::string &guid) const {
    const Device *device = find_by_guid(guid);
    return device ? device->joystick : nullptr;
}

Sint16 InputDevices::axis(const std::string &guid, bool raw, int index) const {
    if (index < 0) return 0;
    const Device *device = guid.empty() ? nullptr : find_by_guid(guid);
    if (raw) {
        if (!device || !device->joystick || device->controller ||
            index >= SDL_JoystickNumAxes(device->joystick))
            return 0;
        return SDL_JoystickGetAxis(device->joystick, index);
    }
    if (device && device->controller)
        return index < SDL_CONTROLLER_AXIS_MAX
                   ? SDL_GameControllerGetAxis(
                         device->controller, (SDL_GameControllerAxis)index)
                   : 0;
    if (guid.empty() && primary_)
        return index < SDL_CONTROLLER_AXIS_MAX
                   ? SDL_GameControllerGetAxis(
                         primary_, (SDL_GameControllerAxis)index)
                   : 0;
    return 0;
}

void read_pad_state(const InputDevices &devices, const Settings &s,
                    PadState &out) {
    SDL_JoystickUpdate();
    out = {};
    out.connected = !devices.empty();
    const std::string primary = devices.primary_guid();
    auto bound_guid = [&](int action) {
        std::string guid = s.pad_device[action];
        if (guid.empty() && !s.pad_raw[action]) guid = primary;
        return guid;
    };
    auto enabled = [&](int action) {
        const std::string guid = bound_guid(action);
        return guid.empty() || s.device_enabled(guid);
    };
    if (enabled(PadBindSteerAxis))
        out.steer_axis = devices.axis(
            s.pad_device[PadBindSteerAxis], s.pad_raw[PadBindSteerAxis] != 0,
            s.pad_steer_axis);
    if (enabled(PadBindGasAxis))
        out.gas_axis = devices.axis(s.pad_device[PadBindGasAxis],
                                    s.pad_raw[PadBindGasAxis] != 0,
                                    s.pad_gas_axis);
    if (enabled(PadBindBrakeAxis))
        out.brake_axis = devices.axis(s.pad_device[PadBindBrakeAxis],
                                      s.pad_raw[PadBindBrakeAxis] != 0,
                                      s.pad_brake_axis);
    if (enabled(PadBindSteerLeft))
        out.steer_left = devices.button(s.pad_device[PadBindSteerLeft],
                                        s.pad_raw[PadBindSteerLeft] != 0,
                                        s.pad_steer_left);
    if (enabled(PadBindSteerRight))
        out.steer_right = devices.button(s.pad_device[PadBindSteerRight],
                                         s.pad_raw[PadBindSteerRight] != 0,
                                         s.pad_steer_right);
    if (enabled(PadBindGas))
        out.gas = devices.button(s.pad_device[PadBindGas],
                                 s.pad_raw[PadBindGas] != 0, s.pad_gas);
    if (enabled(PadBindBrake))
        out.brake = devices.button(s.pad_device[PadBindBrake],
                                   s.pad_raw[PadBindBrake] != 0, s.pad_brake);
    if (enabled(PadBindGear))
        out.gear = devices.button(s.pad_device[PadBindGear],
                                  s.pad_raw[PadBindGear] != 0, s.pad_gear);
    if (enabled(PadBindStart))
        out.start = devices.button(s.pad_device[PadBindStart],
                                   s.pad_raw[PadBindStart] != 0, s.pad_start);
    if (enabled(PadBindCoin))
        out.coin = devices.button(s.pad_device[PadBindCoin],
                                  s.pad_raw[PadBindCoin] != 0, s.pad_coin);
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

void apply_input(Machine &m, const Uint8 *keys, const PadState &pad,
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
        prevGear = keys[SDL_SCANCODE_SPACE] || (pad.connected && pad.gear);
        return;
    }

    // Steering: left stick (analog, with dead zone), d-pad or A/D/arrows
    // (slew), and a self-centre when nothing is held. The cabinet wheel range
    // is 0x20..0xE0 with 0x80 centred.
    const Sint16 ax = pad.steer_axis;
    const bool left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A] ||
                      pad.steer_left;
    const bool right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D] ||
                       pad.steer_right;
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
    } else if (s.racing_wheel && pad.connected) {
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
    if (pad.connected) {
        if (pad.gas) gas = 255;
        if (pad.brake) brake = 255;
        gas = std::max(gas, (int)pedal_from_signed_axis(
                                pad.gas_axis, s.pad_gas_axis_invert,
                                s.pedal_deadzone));
        brake = std::max(brake, (int)pedal_from_signed_axis(
                                    pad.brake_axis, s.pad_brake_axis_invert,
                                    s.pedal_deadzone));
    }
    m.in_gas = (uint8_t)gas;
    m.in_brake = (uint8_t)brake;

    // General port: active-low start/coin/service/test. Bit 5 is the gear
    // switch, which rests low (MAME's GENERAL default is 0xDF).
    uint8_t g = 0xDF;
    const bool start_down = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_1] ||
                            pad.start;
    const bool pass_start = m.start_button(start_down);
    if (pass_start) g &= (uint8_t)~0x08;                                        // start
    if (keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_5] || pad.coin)
        g &= (uint8_t)~0x40;                                                    // coin
    if (keys[SDL_SCANCODE_F1]) g &= (uint8_t)~0x04;                            // service
    if (keys[SDL_SCANCODE_F2]) g &= (uint8_t)~0x02;                            // test

    const bool gear_pressed = keys[SDL_SCANCODE_SPACE] || pad.gear;
    if (s.auto_gear)
        gear = (auto_gear_bit(m) != 0);
    else if (gear_pressed && !prevGear)
        gear = !gear;
    prevGear = gear_pressed;
    if (gear) g |= 0x20;   // active high on Power Drift
    m.in_general = g;
}

int inputdevicetest() {
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER |
                 SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "inputdevicetest: SDL_Init failed: %s\n",
                     SDL_GetError());
        return 1;
    }
    const int wheel_index =
        SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_WHEEL, 2, 8, 0);
    const int pedals_index =
        SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN, 3, 4, 0);
    const int shifter_index =
        SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_UNKNOWN, 1, 2, 0);
    SDL_VirtualJoystickDesc controller_desc{};
    controller_desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    controller_desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    controller_desc.naxes = 6;
    controller_desc.nbuttons = 15;
    controller_desc.nhats = 1;
    controller_desc.name = "Virtual Game Controller";
    const int controller_index =
        SDL_JoystickAttachVirtualEx(&controller_desc);
    if (wheel_index < 0 || pedals_index < 0 || shifter_index < 0 ||
        controller_index < 0) {
        std::fprintf(stderr, "inputdevicetest: virtual attach failed: %s\n",
                     SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_PumpEvents();

    InputDevices input;
    input.open_all();
    const std::vector<InputDeviceInfo> infos = input.infos();
    auto find_raw = [&](int axes, int buttons) -> const InputDeviceInfo * {
        for (const InputDeviceInfo &info : infos)
            if (!info.game_controller && info.axes == axes &&
                info.buttons == buttons &&
                info.name.find("Virtual") != std::string::npos)
                return &info;
        return nullptr;
    };
    const InputDeviceInfo *wheel_info = find_raw(2, 8);
    const InputDeviceInfo *pedals_info = find_raw(3, 4);
    const InputDeviceInfo *shifter_info = find_raw(1, 2);
    const InputDeviceInfo *controller_info = nullptr;
    for (const InputDeviceInfo &info : infos)
        if (info.game_controller && info.name == "Virtual Game Controller")
            controller_info = &info;

    SDL_Joystick *wheel =
        wheel_info ? input.joystick_for_guid(wheel_info->guid) : nullptr;
    SDL_Joystick *pedals =
        pedals_info ? input.joystick_for_guid(pedals_info->guid) : nullptr;
    SDL_Joystick *shifter =
        shifter_info ? input.joystick_for_guid(shifter_info->guid) : nullptr;
    SDL_Joystick *controller =
        controller_info ? input.joystick_for_guid(controller_info->guid)
                        : nullptr;
    const bool discovered = infos.size() >= 4 && wheel_info && pedals_info &&
                            shifter_info && controller_info && wheel && pedals &&
                            shifter && controller;
    if (discovered) {
        SDL_JoystickSetVirtualAxis(wheel, 0, -30000);
        SDL_JoystickSetVirtualAxis(pedals, 0, 32000);
        SDL_JoystickSetVirtualAxis(pedals, 1, 16000);
        SDL_JoystickSetVirtualButton(shifter, 0, 1);
        SDL_JoystickSetVirtualAxis(controller, 0, 20000);
        SDL_JoystickSetVirtualButton(controller, 0, 1);
        SDL_PumpEvents();
        SDL_JoystickUpdate();
    }

    Settings s;
    s.racing_wheel = true;
    s.pad_device[PadBindSteerAxis] = wheel_info ? wheel_info->guid : "";
    s.pad_raw[PadBindSteerAxis] = 1;
    s.pad_steer_axis = 0;
    s.pad_device[PadBindGasAxis] = pedals_info ? pedals_info->guid : "";
    s.pad_raw[PadBindGasAxis] = 1;
    s.pad_gas_axis = 0;
    s.pad_device[PadBindBrakeAxis] = pedals_info ? pedals_info->guid : "";
    s.pad_raw[PadBindBrakeAxis] = 1;
    s.pad_brake_axis = 1;
    s.pad_device[PadBindGear] = shifter_info ? shifter_info->guid : "";
    s.pad_raw[PadBindGear] = 1;
    s.pad_gear = 0;
    PadState raw_pad;
    read_pad_state(input, s, raw_pad);
    const bool raw_ok = discovered && raw_pad.steer_axis == -30000 &&
                        raw_pad.gas_axis == 32000 &&
                        raw_pad.brake_axis == 16000 && raw_pad.gear;
    if (wheel_info) s.set_device_enabled(wheel_info->guid, false);
    PadState disabled_pad;
    read_pad_state(input, s, disabled_pad);
    const bool disable_ok = discovered && disabled_pad.steer_axis == 0 &&
                            disabled_pad.gas_axis == 32000;
    if (wheel_info) s.set_device_enabled(wheel_info->guid, true);

    Settings controller_settings;
    if (controller_info) {
        controller_settings.pad_device[PadBindSteerAxis] = controller_info->guid;
        controller_settings.pad_steer_axis = 0;
        controller_settings.pad_device[PadBindGas] = controller_info->guid;
        controller_settings.pad_gas = 0;
    }
    PadState controller_pad;
    read_pad_state(input, controller_settings, controller_pad);
    const bool controller_ok = discovered && controller_pad.connected &&
                               controller_pad.steer_axis == 20000 &&
                               controller_pad.gas;

    Settings menu_settings;
    Menu menu(menu_settings);
    menu.toggle();
    menu.set_selection(2);   // CONTROLLER BINDINGS
    menu.key(MenuKey::Enter);
    menu.key(MenuKey::TabPrev); // pedals subtab
    menu.set_selection(0);   // GAS AXIS
    menu.key(MenuKey::Enter);
    if (pedals_info)
        menu.bind_axis(0, 20000, pedals_info->guid, true, pedals_info->name);
    const bool menu_ok =
        menu_settings.pad_gas_axis == 0 &&
        menu_settings.pad_raw[PadBindGasAxis] == 1 &&
        pedals_info && menu_settings.pad_device[PadBindGasAxis] == pedals_info->guid;

    input.close_all();
    SDL_Quit();

    std::printf("inputdevicetest: discovery %s\n", discovered ? "PASS" : "FAIL");
    std::printf("inputdevicetest: separate wheel/pedals/buttons %s\n",
                raw_ok ? "PASS" : "FAIL");
    std::printf("inputdevicetest: device disable mask %s\n",
                disable_ok ? "PASS" : "FAIL");
    std::printf("inputdevicetest: default game controller %s\n",
                controller_ok ? "PASS" : "FAIL");
    std::printf("inputdevicetest: raw binding menu %s\n",
                menu_ok ? "PASS" : "FAIL");
    return discovered && raw_ok && disable_ok && controller_ok && menu_ok ? 0 : 1;
}

} // namespace likethewind
#endif // LIKETHEWIND_HAVE_SDL
