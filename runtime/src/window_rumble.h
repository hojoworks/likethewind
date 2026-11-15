#ifndef LIKETHEWIND_WINDOW_RUMBLE_H
#define LIKETHEWIND_WINDOW_RUMBLE_H

#ifdef LIKETHEWIND_HAVE_SDL
#include <SDL.h>

#include "likethewind/settings.h"

namespace likethewind {

class Machine;

// Gamepad force feedback. The small right-hand (high-frequency) motor carries
// steering/tire load, the large left-hand (low-frequency) motor carries the
// heavy stage-intro revs and crash impacts, and there is no idle engine hum.
struct RumbleState {
    float impulse = 0.0f;
    float corner = 0.0f;
    float grip_phase = 0.0f;
    float brake_phase = 0.0f;
    float prev_brake = 0.0f;
    int prev_speed = 0;
    int speed_hist[8] = {};
    int speed_hist_pos = 0;
    int intro_frames = 0;
    int launch_frames = 0;
    int throttle_bump_frames = 0;
    int release_frames = 0;
    int last_mode = -1;
    int turn_hold = 0;
    bool launch_armed = false;
    bool throttle_bump_armed = false;
    uint32_t collisions = 0;
    Uint16 low = 0;
    Uint16 high = 0;
    Uint16 release_low = 0;
    Uint16 release_high = 0;
    Uint32 reissue_at = 0;
    bool turn_active = false;
    bool warned = false;
};

void stop_rumble(SDL_GameController *pad, RumbleState &state);
Uint16 rumble_level(float value, int strength);
void update_rumble(SDL_GameController *pad, bool supported, const Settings &s,
                   Machine &m, RumbleState &state, bool racing, int mode,
                   bool enabled);

} // namespace likethewind

#endif // LIKETHEWIND_HAVE_SDL
#endif // LIKETHEWIND_WINDOW_RUMBLE_H
