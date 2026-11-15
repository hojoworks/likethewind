#include "window_mix.h"

#include "likethewind/audio.h"

#include <algorithm>
#include <iterator>

namespace likethewind {

MixParams stock_mix() {
    MixParams p;
    std::fill(std::begin(p.ym_volume), std::end(p.ym_volume), 100);
    std::fill(std::begin(p.pcm_volume), std::end(p.pcm_volume), 100);
    std::fill(std::begin(p.sfx_volume), std::end(p.sfx_volume), 100);
    return p;
}

MixParams custom_mix(const Settings &s) {
    MixParams p;
    std::copy(std::begin(s.ym_volume), std::end(s.ym_volume), std::begin(p.ym_volume));
    std::copy(std::begin(s.pcm_volume), std::end(s.pcm_volume), std::begin(p.pcm_volume));
    std::copy(std::begin(s.ym_send), std::end(s.ym_send), std::begin(p.ym_send));
    std::copy(std::begin(s.pcm_send), std::end(s.pcm_send), std::begin(p.pcm_send));
    std::copy(std::begin(s.sfx_lane_volume), std::end(s.sfx_lane_volume),
              std::begin(p.sfx_volume));
    std::copy(std::begin(s.sfx_lane_send), std::end(s.sfx_lane_send),
              std::begin(p.sfx_send));
    std::copy(std::begin(s.sfx_lane_reverb_send),
              std::end(s.sfx_lane_reverb_send), std::begin(p.sfx_reverb_send));
    std::copy(std::begin(s.ym_reverb_send), std::end(s.ym_reverb_send),
              std::begin(p.ym_reverb_send));
    std::copy(std::begin(s.pcm_reverb_send), std::end(s.pcm_reverb_send),
              std::begin(p.pcm_reverb_send));
    p.chorus_enabled = s.chorus_enabled;
    p.chorus_rate = s.chorus_rate;
    p.chorus_depth = s.chorus_depth;
    p.chorus_delay = s.chorus_delay;
    p.chorus_feedback = s.chorus_feedback;
    p.chorus_mix = s.chorus_mix;
    p.reverb_enabled = s.reverb_enabled;
    p.reverb_decay = s.reverb_decay;
    p.reverb_damping = s.reverb_damping;
    p.reverb_predelay = s.reverb_predelay;
    p.reverb_mix = s.reverb_mix;
    p.solo_mask = s.solo_mask;
    return p;
}

void apply_mix(Audio *audio, const MixParams &p) {
    if (!audio) return;
    audio->set_channel_volumes(p.ym_volume, p.pcm_volume);
    audio->set_sfx_volumes(p.sfx_volume, p.sfx_send, p.sfx_reverb_send);
    audio->set_chorus_sends(p.ym_send, p.pcm_send);
    audio->set_reverb_sends(p.ym_reverb_send, p.pcm_reverb_send);
    audio->set_solo_mask(p.solo_mask);
    audio->set_chorus(p.chorus_enabled, p.chorus_rate, p.chorus_depth, p.chorus_delay,
                      p.chorus_feedback, p.chorus_mix);
    audio->set_reverb(p.reverb_enabled, p.reverb_decay, p.reverb_damping,
                      p.reverb_predelay, p.reverb_mix);
}

} // namespace likethewind
