#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace likethewind {

// Z80 sound board: 4 MHz Z80, YM2151 (OPM) and 315-5218 Sega PCM.
class Audio {
public:
    Audio();
    ~Audio();

    void reset(const std::vector<uint8_t> &z80_rom,
               const std::vector<uint8_t> &pcm_rom);
    void sound_latch(uint8_t value);
    void run_frame();
    void set_trace(bool on);
    // Optional passive logger for every PCM lane start.
    using SampleLogCallback = void (*)(void *user, int voice, int group,
                                       uint8_t sample);
    void set_sample_log(SampleLogCallback callback, void *user);
    void set_volumes(int music_percent, int sfx_percent, int engine_percent);
    void set_voice_normalizer(bool enabled);
    void set_attract_active(bool active);
    void set_race_active(bool active);
    void set_channel_volumes(const int *ym_percent, const int *pcm_percent);
    void set_sfx_volumes(const int *volumes, const int *sends,
                         const int *reverb_sends);
    void set_chorus_sends(const int *ym_percent, const int *pcm_percent);
    void set_reverb_sends(const int *ym_percent, const int *pcm_percent);
    void set_solo_mask(uint32_t mask);
    void set_chorus(bool enabled, int rate_tenths, int depth_percent,
                    int delay_ms, int feedback_percent, int mix_percent);
    void set_reverb(bool enabled, int decay_percent, int damping_percent,
                    int predelay_ms, int mix_percent);

    // Current post-trim activity for the channel mixer's VU meters.
    void get_levels(float *ym_levels, float *pcm_levels) const;
    void get_sfx_levels(float *levels) const;

    const int16_t *samples() const;
    size_t sample_count() const;

    // Crash-report snapshot of the sound CPU.
    uint16_t z80_pc() const;
    uint64_t z80_cycles() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace likethewind
