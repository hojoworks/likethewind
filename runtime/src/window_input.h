#ifndef LIKETHEWIND_WINDOW_INPUT_H
#define LIKETHEWIND_WINDOW_INPUT_H

#ifdef LIKETHEWIND_HAVE_SDL
#include <SDL.h>

#include <string>
#include <vector>

#include "likethewind/machine.h"
#include "likethewind/settings.h"

namespace likethewind {

struct InputDeviceInfo {
    std::string guid;
    std::string name;
    int axes = 0;
    int buttons = 0;
    bool game_controller = false;
    SDL_JoystickID instance = -1;
};

// Opens every SDL joystick, not only devices with a game-controller mapping.
// Wheels, pedals and shifters often appear as raw joysticks, and each binding
// remembers the GUID of the device it came from.
class InputDevices {
public:
    ~InputDevices();
    void open_all();
    void close_all();
    bool empty() const { return devices_.empty(); }
    std::vector<InputDeviceInfo> infos() const;
    const InputDeviceInfo *find_by_instance(SDL_JoystickID instance) const;
    SDL_GameController *primary_controller() const { return primary_; }
    std::string primary_guid() const;
    bool rumble_supported() const;
    bool any_button() const;
    bool button(const std::string &guid, bool raw, int index) const;
    Sint16 axis(const std::string &guid, bool raw, int index) const;
    SDL_Joystick *joystick_for_guid(const std::string &guid) const;

private:
    struct Device {
        InputDeviceInfo info;
        SDL_Joystick *joystick = nullptr;
        SDL_GameController *controller = nullptr;
    };
    const Device *find_by_guid(const std::string &guid) const;
    std::vector<Device> devices_;
    SDL_GameController *primary_ = nullptr;
};

struct PadState {
    bool connected = false;
    Sint16 steer_axis = 0;
    int gas_axis = 0;
    int brake_axis = 0;
    bool steer_left = false;
    bool steer_right = false;
    bool gas = false;
    bool brake = false;
    bool gear = false;
    bool start = false;
    bool coin = false;
};

void read_pad_state(const InputDevices &devices, const Settings &s,
                    PadState &out);
uint8_t pedal_from_signed_axis(int value, bool invert, int dead_pct);

struct SteeringState {
    bool rebound = false;
    bool stick_engaged = false;
    int engage_hold = 0;
    int release_hold = 0;
    bool left_prev = false;
    bool right_prev = false;
    bool entry_active = false;
    int entry_index = 0;
};

int update_wheel(int wheel, SteeringState &state, int ax, bool left, bool right,
                 const Settings &s);
void apply_input(Machine &m, const Uint8 *keys, const PadState &pad,
                 bool &gear, bool &prevGear, SteeringState &steer_state,
                 const Settings &s, bool menu_open);
int inputdevicetest();

} // namespace likethewind

#endif // LIKETHEWIND_HAVE_SDL

#endif
