#ifndef LIKETHEWIND_WINDOW_INPUT_H
#define LIKETHEWIND_WINDOW_INPUT_H

#ifdef LIKETHEWIND_HAVE_SDL
#include <SDL.h>

#include "likethewind/machine.h"
#include "likethewind/menu.h"
#include "likethewind/settings.h"

namespace likethewind {

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

Sint16 controller_axis(SDL_GameController *pad, int axis);
int update_wheel(int wheel, SteeringState &state, int ax, bool left, bool right,
                 const Settings &s);
void apply_input(Machine &m, const Uint8 *keys, SDL_GameController *pad,
                 bool &gear, bool &prevGear, SteeringState &steer_state,
                 const Settings &s, bool menu_open);
bool menu_key_from_sym(SDL_Keycode sym, MenuKey &k);
bool menu_key_from_button(Uint8 b, MenuKey &k);

} // namespace likethewind

#endif // LIKETHEWIND_HAVE_SDL

#endif
