#ifndef LIKETHEWIND_WINDOW_MIX_H
#define LIKETHEWIND_WINDOW_MIX_H

#include <cstdint>

#include "likethewind/settings.h"

namespace likethewind {

class Audio;

// A complete mixer snapshot, so per-song changes can cross-fade instead of
// jumping to the new channel trims. A/B and the Audio Mix preset bypass this
// and apply their target immediately.
struct MixParams {
    int ym_volume[8]{};
    int pcm_volume[16]{};
    int ym_send[8]{};
    int pcm_send[16]{};
    int sfx_volume[Settings::kSfxChannels]{};
    int sfx_send[Settings::kSfxChannels]{};
    int sfx_reverb_send[Settings::kSfxChannels]{};
    int ym_reverb_send[8]{};
    int pcm_reverb_send[16]{};
    bool chorus_enabled = false;
    int chorus_rate = 15;
    int chorus_depth = 50;
    int chorus_delay = 12;
    int chorus_feedback = 0;
    int chorus_mix = 30;
    bool reverb_enabled = false;
    int reverb_decay = 60;
    int reverb_damping = 40;
    int reverb_predelay = 0;
    int reverb_mix = 25;
    uint32_t solo_mask = 0;
};

MixParams stock_mix();
MixParams custom_mix(const Settings &s);
void apply_mix(Audio *audio, const MixParams &p);

} // namespace likethewind

#endif // LIKETHEWIND_WINDOW_MIX_H
