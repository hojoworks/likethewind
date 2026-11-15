#include "window_rumble.h"

#ifdef LIKETHEWIND_HAVE_SDL
#include "likethewind/audio.h"
#include "likethewind/bytes.h"
#include "likethewind/machine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace likethewind {

void stop_rumble(SDL_GameController *pad, RumbleState &state) {
    if (pad && (state.low || state.high))
        SDL_GameControllerRumble(pad, 0, 0, 0);
    state.low = state.high = 0;
    state.impulse = 0.0f;
    state.corner = 0.0f;
    state.grip_phase = 0.0f;
    state.brake_phase = 0.0f;
    state.prev_brake = 0.0f;
    state.turn_active = false;
    state.turn_hold = 0;
    state.launch_frames = 0;
    state.launch_armed = false;
    state.throttle_bump_frames = 0;
    state.throttle_bump_armed = false;
    state.release_frames = 0;
    state.release_low = 0;
    state.release_high = 0;
    state.reissue_at = 0;
}

// Every gameplay effect goes through this one multiplier. There are no
// per-effect strength overrides.
Uint16 rumble_level(float value, int strength) {
    const float scaled = std::clamp(value, 0.0f, 1.0f) *
                         (std::clamp(strength, 0, 100) / 100.0f);
    return (Uint16)(scaled * 65535.0f);
}

void update_rumble(SDL_GameController *pad, bool supported, const Settings &s,
                   Machine &m, RumbleState &state, bool racing, int mode,
                   bool enabled) {
    static const bool debug = std::getenv("LIKETHEWIND_RUMBLE_DEBUG") != nullptr;
    static int debug_tick = 0;
    const int speed = (m.shared[0xEA12] << 8) | m.shared[0xEA13];
    const uint32_t collisions =
        m.audio() ? m.audio()->collision_events() : 0;
    if (collisions != state.collisions) {
        // A lower count means the audio board was recreated (game reset),
        // not a new collision. Only a real increment should kick the motors.
        if (collisions > state.collisions) {
            if (debug) {
                std::fprintf(stderr,
                             "[rumble] hit sample +%u speed=%d racing=%d\n",
                             (unsigned)(collisions - state.collisions), speed,
                             racing ? 1 : 0);
            }
            if (racing && enabled && supported) {
                state.impulse = 1.0f;
                state.reissue_at = 0;   // send the impact peak immediately
            }
        }
        state.collisions = collisions;
    }

    if (!enabled || !supported || !pad) {
        stop_rumble(pad, state);
        state.prev_speed = speed;
        state.intro_frames = 0;
        state.launch_frames = 0;
        state.launch_armed = false;
        state.last_mode = mode;
        return;
    }

    // Mode 11 is the pre-race stage flyover, and mode 16 is the "STAGE n /
    // START" screen. Fire at both so the revs start early and do not wait
    // until the last fraction of a second before the race.
    if (mode == 11 && state.last_mode != 11) state.intro_frames = 300;
    if (mode < 11 || mode >= 20) state.intro_frames = 0;
    if (mode == 20 && state.last_mode != 20) {
        state.launch_armed = true;
        state.launch_frames = 0;
    }
    if (mode != 20) {
        state.launch_frames = 0;
        state.launch_armed = false;
    }
    state.last_mode = mode;

    // Some hits do not have a mapped voice sample (or roles.cfg is absent).
    // A sudden loss still reads as a collision, and an 8-frame peak catches
    // crashes that scrub speed over several frames instead of one.
    if (state.prev_speed > 60 && speed < state.prev_speed - 18) {
        if (debug) {
            std::fprintf(stderr, "[rumble] speed drop %d -> %d\n",
                         state.prev_speed, speed);
        }
        const float drop = (float)(state.prev_speed - speed) / 120.0f;
        state.impulse = std::max(state.impulse, std::min(1.0f, drop));
        state.reissue_at = 0;
    }
    state.prev_speed = speed;

    state.speed_hist[state.speed_hist_pos] = speed;
    state.speed_hist_pos = (state.speed_hist_pos + 1) % 8;
    int speed_peak = 0;
    for (int v : state.speed_hist) speed_peak = std::max(speed_peak, v);
    if (racing && speed_peak > 80 && speed_peak - speed > 55) {
        if (debug) {
            std::fprintf(stderr, "[rumble] crash peak %d -> %d\n",
                         speed_peak, speed);
        }
        state.impulse = 1.0f;
        state.reissue_at = 0;
        for (int &v : state.speed_hist) v = speed;
        state.speed_hist_pos = 0;
    }

    const bool intro = state.intro_frames > 0;
    if (!racing && !intro) {
        // Race over (or the finish screen): let the last effect trail off
        // instead of leaving the motors latched on.
        state.impulse = 0.0f;
        state.corner = 0.0f;
        state.turn_active = false;
        state.turn_hold = 0;
        state.launch_frames = 0;
        state.launch_armed = false;
        state.throttle_bump_frames = 0;
        state.throttle_bump_armed = false;
        state.intro_frames = 0;
        state.prev_speed = speed;

        if (state.release_frames == 0) {
            if (!state.low && !state.high) {
                stop_rumble(pad, state);
                return;
            }
            state.release_low = state.low;
            state.release_high = state.high;
            state.release_frames = 90;   // 1.5 s at the board's 60 Hz
        }

        const float gain = (float)state.release_frames / 90.0f;
        const Uint16 low = (Uint16)(state.release_low * gain);
        const Uint16 high = (Uint16)(state.release_high * gain);
        if (debug && (debug_tick++ % 15) == 0) {
            std::fprintf(stderr,
                         "[rumble] release=%d low=%u high=%u\n",
                         state.release_frames, (unsigned)low,
                         (unsigned)high);
        }
        if (state.release_frames <= 1 || (low < 256 && high < 256)) {
            stop_rumble(pad, state);
        } else {
            SDL_GameControllerRumble(pad, low, high, 120);
            state.low = low;
            state.high = high;
            state.reissue_at = 0;
            --state.release_frames;
        }
        return;
    }

    state.release_frames = 0;
    state.release_low = 0;
    state.release_high = 0;

    float low = 0.0f;
    float high = 0.0f;
    float yaw_deg = 0.0f;

    if (intro) {
        // Five-second stage-intro cue:
        //   dunn -- dada -- dunndunn
        //   vroom-vroom ... vroom-vroom
        // The heavy motor carries the hits and revs; the small motor adds just
        // enough edge to make each pulse distinct.
        const int elapsed = 300 - state.intro_frames;
        auto pulse = [&](int start, int len, float lo, float hi) {
            if (elapsed < start || elapsed >= start + len) return;
            const float t =
                (float)(elapsed - start) / (float)(len - 1);
            const float env =
                std::sin(std::clamp(t, 0.0f, 1.0f) * 3.14159265f);
            low = std::max(low, lo * env);
            high = std::max(high, hi * env);
        };
        // dunn--dada-dunndunn
        pulse(0, 18, 0.80f, 0.12f);
        pulse(28, 7, 0.50f, 0.08f);
        pulse(39, 7, 0.50f, 0.08f);
        pulse(52, 10, 0.60f, 0.10f);
        pulse(66, 13, 0.70f, 0.12f);
        // vroom-vroom, tiny gap, vroom-vroom
        pulse(90, 21, 0.55f, 0.25f);
        pulse(117, 21, 0.65f, 0.30f);
        pulse(150, 21, 0.55f, 0.25f);
        pulse(177, 21, 0.65f, 0.30f);
        // Fill the last stretch before the countdown instead of leaving a
        // long silent gap.
        pulse(210, 24, 0.55f, 0.25f);
        pulse(245, 12, 0.75f, 0.12f);
        pulse(265, 12, 0.80f, 0.12f);
        --state.intro_frames;
    } else {
        // Use the board's rotation matrix, not the host joystick, so the
        // rumble follows the car's actual turn in the game. The matrix yaw is
        // zero when the camera/road is straight and grows as the car commits
        // to a corner. It cannot be driven by wiggling the stick while the
        // car is crashed or stationary.
        const uint8_t *rot = m.rotation;
        const int32_t dxx = (int32_t)read_be32(rot + 0x3F6 * 2);
        const int32_t dxy = (int32_t)read_be32(rot + 0x3F8 * 2);
        yaw_deg = std::abs((float)std::atan2((double)dxy, (double)dxx)) *
                  57.2957795f;
        const float speed_f = std::clamp(speed / 240.0f, 0.0f, 1.0f);
        const float gas_f =
            std::clamp((float)m.in_gas / 255.0f, 0.0f, 1.0f);

        // Arm the launch burnout at mode 20, but start it only once the
        // speedometer actually leaves zero with the throttle down.
        if (state.launch_armed) {
            if (speed > 0 && gas_f > 0.55f) {
                state.launch_frames = 90;
                state.launch_armed = false;
            } else if (speed > 80) {
                state.launch_armed = false;
            }
        }

        // Hysteresis plus a short sustain requirement: a brief correction
        // must not start the rumble. Release only after the view has been
        // straight for several frames. Suppress cornering entirely during a
        // crash impulse so a spinning car does not fight the impact rumble.
        const bool crashing = state.impulse > 0.05f;
        if (crashing) {
            state.turn_active = false;
            state.turn_hold = 0;
        } else if (!state.turn_active) {
            if (yaw_deg > 5.0f) {
                if (++state.turn_hold >= 6) state.turn_active = true;
            } else {
                state.turn_hold = 0;
            }
        } else if (yaw_deg < 3.0f) {
            if (++state.turn_hold >= 6) {
                state.turn_active = false;
                state.turn_hold = 0;
            }
        } else {
            state.turn_hold = 0;
        }

        // Arm a small extra high-motor "vroom" only after the pedal has been
        // genuinely released. The next time it reaches full throttle while
        // cornering, the top end gets a brief bump.
        if (gas_f < 0.15f) {
            state.throttle_bump_armed = true;
        } else if (gas_f >= 0.92f) {
            if (state.throttle_bump_armed && state.turn_active)
                state.throttle_bump_frames = 8;
            state.throttle_bump_armed = false;
        }

        float corner_target = 0.0f;
        if (state.turn_active) {
            // Once a real corner is confirmed, start with a gentle amount and
            // spread the rest over a wide yaw range so mid-corner has a
            // useful middle rather than snapping between off and full.
            const float ramp =
                0.15f +
                0.85f * std::clamp((yaw_deg - 5.0f) / 7.0f, 0.0f, 1.0f);
            corner_target = ramp * speed_f;
        }
        state.corner += (corner_target - state.corner) * 0.25f;
        if (state.corner < 0.001f) state.corner = 0.0f;

        // The small high-frequency motor is the tire/slip channel and follows
        // the throttle, so easing off the gas softens the cornering buzz.
        // The heavy motor is deliberately modest and slowly "breathes" to
        // suggest grip coming and going instead of a constant shake.
        const float throttle = 0.15f + 0.85f * gas_f;
        high = 0.85f * std::pow(state.corner, 0.8f) * throttle;
        if (state.throttle_bump_frames > 0) {
            high += 0.12f * state.corner *
                    ((float)state.throttle_bump_frames / 8.0f);
            --state.throttle_bump_frames;
        }

        // Brake screech: the rumble API has no frequency control, so pulse
        // the small motor faster as brake pressure rises. This gives the
        // feeling of a rising tire squeal rather than a static buzz.
        const float brake_f =
            std::clamp((float)m.in_brake / 255.0f, 0.0f, 1.0f);
        const float steer_norm = std::clamp(
            std::abs((int)m.in_wheel - 0x80) / 96.0f, 0.0f, 1.0f);
        const float yaw_turn =
            std::clamp((yaw_deg - 1.0f) / 6.0f, 0.0f, 1.0f);
        const float brake_turn =
            std::max(yaw_turn, steer_norm * 0.85f);
        const bool braking =
            brake_f > 0.05f && speed > 10 && !crashing &&
            brake_turn > 0.02f;
        if (braking) {
            if (state.prev_brake <= 0.05f)
                state.brake_phase = 1.5707963f;  // start at full squeal
            const float rate_hz = 6.0f + 4.0f * brake_f;
            state.brake_phase += 6.2831853f * rate_hz / 60.0f;
            if (state.brake_phase >= 6.2831853f)
                state.brake_phase -= 6.2831853f;
            const float mod =
                0.5f + 0.5f * std::sin(state.brake_phase);
            const float brake_corner =
                std::max(state.corner, brake_turn * speed_f);
            // Strong floor so even a quick, light brake is felt; pressure
            // mainly controls how fast the squeal pulses.
            const float brake_pressure = 0.80f + 0.20f * brake_f;
            const float brake_level = std::min(
                1.0f, brake_pressure * brake_corner * 1.5f);
            high = std::max(high, brake_level * (0.55f + 0.45f * mod));
            low += 0.25f * brake_level;
            state.prev_brake = brake_f;
        } else {
            state.brake_phase = 0.0f;
            state.prev_brake = 0.0f;
        }
        state.grip_phase += 0.33f;
        const float grip_a = 0.5f + 0.5f * std::sin(state.grip_phase);
        const float grip_b =
            0.5f + 0.5f * std::sin(state.grip_phase * 1.7f + 1.1f);
        const float grip = 0.65f * grip_a + 0.35f * grip_b;
        low = 0.15f * state.corner * (0.65f + 0.35f * grip);
    }

    // Crash: keep the initial thump but let the high-frequency motor carry
    // most of the energy so it cracks rather than rattling the whole pad.
    low += 0.45f * state.impulse * state.impulse;
    high += 0.90f * state.impulse;

    // Launch burnout: a long, high-motor-led ramp. The small motor swells
    // over the first ~0.45 s, holds, then trails off. The heavy motor only
    // adds a little engine weight underneath.
    if (state.launch_frames > 0) {
        const float gas_f =
            std::clamp((float)m.in_gas / 255.0f, 0.0f, 1.0f);
        const float speed_fade =
            std::clamp(1.0f - (float)speed / 200.0f, 0.0f, 1.0f);
        const float elapsed =
            (float)(90 - state.launch_frames) / 90.0f;
        const float attack = std::clamp(elapsed / 0.30f, 0.0f, 1.0f);
        const float release =
            1.0f - std::clamp((elapsed - 0.65f) / 0.35f, 0.0f, 1.0f);
        const float spin =
            0.92f + 0.08f * std::sin(elapsed * 6.2831853f * 3.0f);
        const float launch = gas_f * speed_fade * attack * release * spin;
        low += 0.10f * launch;
        high += 0.95f * launch;
        --state.launch_frames;
    }

    const Uint16 low_value = rumble_level(low, s.rumble_strength);
    const Uint16 high_value = rumble_level(high, s.rumble_strength);
    if (debug && state.impulse > 0.90f) {
        std::fprintf(stderr, "[rumble] crash low=%u high=%u\n",
                     (unsigned)low_value, (unsigned)high_value);
    }
    state.impulse *= 0.86f;
    if (state.impulse < 0.01f) state.impulse = 0.0f;
    if (debug && (debug_tick++ % 30) == 0) {
        std::fprintf(stderr,
                     "[rumble] mode=%d racing=%d enabled=%d speed=%d gas=%d "
                     "brake=%d wheel=%d yaw=%.1f intro=%d turn=%d hold=%d "
                     "launch=%d bump=%d corner=%.2f str=%d low=%u high=%u\n",
                     mode, racing ? 1 : 0, enabled ? 1 : 0, speed,
                     (int)m.in_gas, (int)m.in_brake, (int)m.in_wheel,
                     yaw_deg, state.intro_frames,
                     state.turn_active ? 1 : 0, state.turn_hold,
                     state.launch_frames, state.throttle_bump_frames,
                     state.corner, s.rumble_strength, (unsigned)low_value,
                     (unsigned)high_value);
    }
    const bool nonzero = low_value != 0 || high_value != 0;
    const Uint32 now = SDL_GetTicks();
    auto send_rumble = [&](Uint16 l, Uint16 h, Uint32 duration) {
        const int rc = SDL_GameControllerRumble(pad, l, h, duration);
        if (rc != 0 && !state.warned) {
            state.warned = true;
            std::fprintf(stderr, "[input] rumble failed: %s\n", SDL_GetError());
        }
    };
    // Reissue at 20 Hz. Updating the motor on every 60 Hz frame makes the
    // controller cancel/restart the effect constantly; at high amplitudes
    // some pads respond poorly to that. Holding each value for 50 ms also
    // makes the steering rumble feel smoother.
    if (now >= state.reissue_at) {
        if (nonzero || state.low || state.high) {
            send_rumble(low_value, high_value, nonzero ? 120 : 0);
            state.low = low_value;
            state.high = high_value;
        }
        state.reissue_at = now + 50;
    }
}

} // namespace likethewind

#endif // LIKETHEWIND_HAVE_SDL
