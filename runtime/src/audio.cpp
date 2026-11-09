/*
 * audio.cpp — Y-Board sound board.
 *
 * 4.027 MHz Z80, YM2151 (OPM) and 315-5218 Sega PCM. The Z80 memory map and
 * register layout follow MAME's segaybd.cpp / segapcm.cpp. Output is 48 kHz
 * stereo; the native PCM and OPM rates are resampled with linear interpolation.
 */
#include "likethewind/audio.h"

#include "ymfm_opm.h"

extern "C" {
#include "z80.h"
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace likethewind {

namespace {

constexpr double kOutRate = 48000.0;
constexpr int kOutFrames = 800;          // 48000 / 60
constexpr float kVoiceHighPassHz = 175.0f;
constexpr float kAnnouncerHighPassHz = 250.0f;
constexpr float kAnnouncerPostGainDb = -1.0f;
// The PCM voices were recorded for a 31.46 kHz DAC. Keep the speech band but
// roll off the top end before the 48 kHz resampler so high-frequency images
// do not turn into aliasing noise. High enough to leave consonants intact.
constexpr float kVoiceLowPassHz = 12000.0f;
constexpr float kVoiceLowPassQ = 0.70710678f;
constexpr float kVoiceHighPassAlpha =
    1.0f / (1.0f + 6.28318530718f * kVoiceHighPassHz / (float)kOutRate);
constexpr float kAnnouncerHighPassAlpha =
    1.0f / (1.0f + 6.28318530718f * kAnnouncerHighPassHz / (float)kOutRate);
constexpr int kChannelVolumeMax = 400;   // per-channel mixer headroom
constexpr int kBusVolumeMax = 200;       // master/music/SFX/engine boost
constexpr int kChorusSize = 2048;        // ~43 ms at 48 kHz
// Sega Y Board sound clock: 32.2159 MHz / 8 for the Z80, YM2151 and 315-5218.
constexpr int kSoundClock = 32215900;
constexpr int kZ80Clock = kSoundClock / 8;
constexpr int kPcmRate = kZ80Clock / 128;  // 315-5218: 16 voices x 8
constexpr int kZ80PerFrame = kZ80Clock / 60;

inline int16_t clamp16(int32_t v) {
    return (int16_t)std::clamp(v, -32768, 32767);
}

struct PcmVoice {
    uint32_t addr = 0xFFFF00;
    uint16_t loop = 0xFFFF;
    uint8_t end = 0xFF;
    uint8_t freq = 0xFF;
    uint8_t lvol = 0xFF;
    uint8_t rvol = 0xFF;
    uint8_t ctrl = 0xFF;
    int group = 0;   // 0 music, 1 sfx, 2 engine
    int32_t lout = 0;
    int32_t rout = 0;
};

// Transposed direct-form II biquad used as the voice low-pass.
struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    Biquad(float hz, float q, float sample_rate) {
        const float w0 = 6.28318530718f * hz / sample_rate;
        const float cw = std::cos(w0);
        const float sw = std::sin(w0);
        const float alpha = sw / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f - cw) * 0.5f / a0;
        b1 = (1.0f - cw) / a0;
        b2 = b0;
        a1 = -2.0f * cw / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float process(float x, float &z1, float &z2) const {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

const Biquad kVoiceLowPass(kVoiceLowPassHz, kVoiceLowPassQ,
                           (float)kOutRate);

// Software PCM voices. The Y-Board has only 16 hardware voices and the Z80
// driver lets effects reuse music voices, cutting notes off. These lanes keep
// music and effects in separate pools so an effect can finish while the
// reused hardware voice returns to its music note.
struct VirtualVoice {
    PcmVoice pcm;
    int group = 0;
    int source = 0;
    int channel = 0;   // music/SFX strip, or 14/15 for engine
    bool active = false;
    uint64_t serial = 0;
    uint8_t sample_id = 0;
    bool voice_effect = false;
    float voice_gain = 1.0f;
    float hp_alpha = kVoiceHighPassAlpha;
    float hp_l_x = 0.0f;
    float hp_l_y = 0.0f;
    float hp_r_x = 0.0f;
    float hp_r_y = 0.0f;
    float lp_l_z1 = 0.0f;
    float lp_l_z2 = 0.0f;
    float lp_r_z1 = 0.0f;
    float lp_r_z2 = 0.0f;
    float agc_env = 0.0f;
    float agc_gain = 1.0f;
};

constexpr int kMusicLanes = 14;
constexpr int kSfxLanes = 64;
constexpr int kEngineLanes = 2;
constexpr int kFlexLanes = 2;
constexpr int kLaneCount =
    kMusicLanes + kSfxLanes + kEngineLanes + kFlexLanes;

// Known effect sample ids. Everything else on the 14 music voices is treated
// as music: a broad 1A-4F effect range incorrectly swallowed music samples
// such as 1A, 3B/3C, 4A and 4D/4E when individual songs were loaded.
constexpr uint8_t kSfxSampleIds[] = {
    0x0E, 0x1B, 0x1C, 0x1D, 0x22, 0x23, 0x24, 0x26,
    0x27, 0x2D, 0x2F, 0x30, 0x33, 0x3A, 0x43, 0x47,
    0x49, 0x4B,
    // Driver voices. These are also classified in samples/roles.cfg, but
    // keeping them here means the restored lines still route to SFX if the
    // optional roles file is missing.
    0x51, 0x54, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0x62, 0x64,
};
constexpr int kSfxSampleCount =
    (int)(sizeof(kSfxSampleIds) / sizeof(kSfxSampleIds[0]));

// YM key-on signatures captured from the pure-SFX attract screen.  A channel
// key-on matching one of these is routed to the SFX bus for the life of that
// note; everything else stays with the music bus.  The signature covers the
// channel, key code/fraction and all operator patch registers.
constexpr uint32_t kYmSfxSignatures[] = {
    0xC14BCA21u, 0xDE4D1CFBu, 0xB613E8B2u, 0xA592E1C2u,
    0x61E7AF0Bu, 0x3978B19Bu, 0x43EEEC52u,
};

// Three-note YM overtake figure. Its patch signature changes with the music
// bank loaded for the current course, so identify it by the fixed channel/key
// codes instead: channels 6/7 playing 45, 5E then 65. These codes are unique
// to the chime; the coin-insert note is the same 45 key-on, which is why the
// chime is muted only in attract and trimmed only during a race.
constexpr int kYmPassingChimeTrim = 40;  // percent, about -8 dB

bool is_ym_sfx_signature(uint32_t sig) {
    for (uint32_t known : kYmSfxSignatures)
        if (known == sig) return true;
    return false;
}

bool is_ym_chime_keycode(int ch, uint8_t keycode) {
    if (ch != 6 && ch != 7) return false;
    return keycode == 0x45 || keycode == 0x5E || keycode == 0x65;
}

bool is_ym_sfx_keycode(int ch, uint8_t keycode) {
    if (ch != 6 && ch != 7) return false;
    switch (keycode) {
    case 0x2C:
    case 0x45:
    case 0x5E:
    case 0x65:
        return true;
    default:
        return false;
    }
}

// 0 = no override, 1 = music, 2 = SFX, 3 = engine. Audio::reset() loads
// samples/roles.cfg when present.
uint8_t g_sample_role[256] = {};
// 0 = unset, 1 = male, 2 = female, 3 = announcer, 4 = enemy. Announcer and
// enemy lines are never swapped by the selected driver's gender.
uint8_t g_sample_gender[256] = {};
// 0 = none, 1 = passing, 2 = wall hit, 3 = enemy hit.
uint8_t g_sample_event[256] = {};

int sample_override(uint8_t sample_id) {
    return sample_id ? g_sample_role[sample_id] : 0;
}

// Voice/impact samples get a slow automatic gain control so announcers,
// passing drivers and hit reactions stay audible without making tyre/engine
// loops pump the whole mix.
bool is_voice_effect(uint8_t sample_id) {
    return sample_id && (g_sample_gender[sample_id] != 0 ||
                         g_sample_event[sample_id] != 0);
}

void load_sample_roles(const char *path) {
    std::memset(g_sample_role, 0, sizeof(g_sample_role));
    std::memset(g_sample_gender, 0, sizeof(g_sample_gender));
    std::memset(g_sample_event, 0, sizeof(g_sample_event));
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        int id = 0;
        try {
            id = std::stoi(line.substr(0, eq), nullptr, 16);
        } catch (...) {
            continue;
        }
        if (id <= 0 || id > 255) continue;
        std::string value = line.substr(eq + 1);
        value.erase(std::remove_if(value.begin(), value.end(),
                                   [](unsigned char c) {
                                       return std::isspace(c);
                                   }),
                    value.end());
        for (char &c : value) c = (char)std::toupper((unsigned char)c);
        std::string role = value;
        std::vector<std::string> extra;
        size_t comma = value.find(',');
        if (comma != std::string::npos) {
            role = value.substr(0, comma);
            size_t begin = comma + 1;
            while (begin <= value.size()) {
                const size_t end = value.find(',', begin);
                extra.push_back(value.substr(
                    begin, end == std::string::npos ? std::string::npos : end - begin));
                if (end == std::string::npos) break;
                begin = end + 1;
            }
        }
        if (role == "MUSIC") g_sample_role[id] = 1;
        else if (role == "SFX") g_sample_role[id] = 2;
        else if (role == "ENGINE") g_sample_role[id] = 3;
        for (const std::string &token : extra) {
            if (token == "MALE" || token == "M") g_sample_gender[id] = 1;
            else if (token == "FEMALE" || token == "F") g_sample_gender[id] = 2;
            else if (token == "ANNOUNCER" || token == "ANN" || token == "A")
                g_sample_gender[id] = 3;
            else if (token == "ENEMY" || token == "EN" || token == "E")
                g_sample_gender[id] = 4;
            else if (token == "PASSING" || token == "PASS" || token == "P")
                g_sample_event[id] = 1;
            else if (token == "WALLHIT" || token == "WALL_HIT" ||
                     token == "WALL-HIT" || token == "WALL" || token == "WH" ||
                     token == "W")
                g_sample_event[id] = 2;
            else if (token == "ENEMYHIT" || token == "ENEMY_HIT" ||
                     token == "ENEMY-HIT" || token == "EH" || token == "X")
                g_sample_event[id] = 3;
            else if (token == "HIT" || token == "H")
                g_sample_event[id] = 2;  // legacy generic hit
        }
    }
}

// Small Freeverb-style plate. Stereo output, mono send input, pre-delay and
// damping controls. Runs at the 48 kHz output rate.
class PlateReverb {
public:
    void init(double sample_rate) {
        sample_rate_ = sample_rate;
        const double scale = sample_rate / 44100.0;
        static const int comb_tune[8] = {1116, 1188, 1277, 1356,
                                         1422, 1491, 1557, 1617};
        static const int ap_tune[4] = {556, 441, 341, 225};
        for (int i = 0; i < 8; ++i) {
            comb_l_[i].buf.assign((size_t)std::max(1, (int)(comb_tune[i] * scale)), 0.0f);
            comb_r_[i].buf.assign(
                (size_t)std::max(1, (int)((comb_tune[i] + 23) * scale)), 0.0f);
        }
        for (int i = 0; i < 4; ++i) {
            ap_l_[i].buf.assign((size_t)std::max(1, (int)(ap_tune[i] * scale)), 0.0f);
            ap_r_[i].buf.assign(
                (size_t)std::max(1, (int)((ap_tune[i] + 23) * scale)), 0.0f);
        }
        pre_.assign((size_t)std::max(2, (int)(0.05 * sample_rate) + 2), 0.0f);
        clear();
    }

    void clear() {
        for (int i = 0; i < 8; ++i) {
            std::fill(comb_l_[i].buf.begin(), comb_l_[i].buf.end(), 0.0f);
            std::fill(comb_r_[i].buf.begin(), comb_r_[i].buf.end(), 0.0f);
            comb_l_[i].idx = comb_r_[i].idx = 0;
            comb_l_[i].store = comb_r_[i].store = 0.0f;
        }
        for (int i = 0; i < 4; ++i) {
            std::fill(ap_l_[i].buf.begin(), ap_l_[i].buf.end(), 0.0f);
            std::fill(ap_r_[i].buf.begin(), ap_r_[i].buf.end(), 0.0f);
            ap_l_[i].idx = ap_r_[i].idx = 0;
        }
        std::fill(pre_.begin(), pre_.end(), 0.0f);
        pre_pos_ = 0;
    }

    void set_params(bool enabled, int decay_pct, int damping_pct, int predelay_ms) {
        if (enabled != enabled_) {
            clear();
            enabled_ = enabled;
        }
        feedback_ = 0.70f + 0.28f * (std::clamp(decay_pct, 0, 100) / 100.0f);
        damp_ = 0.40f * (std::clamp(damping_pct, 0, 100) / 100.0f);
        pre_samples_ = (int)(std::clamp(predelay_ms, 0, 50) * 0.001 * sample_rate_);
        if (pre_samples_ > (int)pre_.size() - 1)
            pre_samples_ = (int)pre_.size() - 1;
    }

    // in_l/in_r are 16-bit-scale send levels; out_l/out_r are the wet return.
    void process(float in_l, float in_r, float &out_l, float &out_r) {
        out_l = out_r = 0.0f;
        if (!enabled_) return;

        const float in = (in_l + in_r) * 0.5f;
        float feed = in;
        if (pre_samples_ > 0) {
            int r = pre_pos_ - pre_samples_;
            if (r < 0) r += (int)pre_.size();
            feed = pre_[(size_t)r];
        }
        pre_[(size_t)pre_pos_] = in;
        if (++pre_pos_ >= (int)pre_.size()) pre_pos_ = 0;

        const float x = feed * (1.0f / 32768.0f) * 0.015f;
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < 8; ++i) {
            l += comb_l_[i].process(x, feedback_, damp_);
            r += comb_r_[i].process(x, feedback_, damp_);
        }
        for (int i = 0; i < 4; ++i) {
            l = ap_l_[i].process(l);
            r = ap_r_[i].process(r);
        }
        out_l = l * 32768.0f * 2.5f;
        out_r = r * 32768.0f * 2.5f;
    }

private:
    struct Comb {
        std::vector<float> buf;
        int idx = 0;
        float store = 0.0f;
        float process(float in, float feedback, float damp) {
            const float out = buf[(size_t)idx];
            store = out * (1.0f - damp) + store * damp;
            buf[(size_t)idx] = in + store * feedback;
            if (++idx >= (int)buf.size()) idx = 0;
            return out;
        }
    };
    struct Allpass {
        std::vector<float> buf;
        int idx = 0;
        float process(float in) {
            const float b = buf[(size_t)idx];
            buf[(size_t)idx] = in + b * 0.5f;
            if (++idx >= (int)buf.size()) idx = 0;
            return b - in;
        }
    };

    double sample_rate_ = 48000.0;
    bool enabled_ = false;
    float feedback_ = 0.84f;
    float damp_ = 0.16f;
    int pre_samples_ = 0;
    int pre_pos_ = 0;
    Comb comb_l_[8], comb_r_[8];
    Allpass ap_l_[4], ap_r_[4];
    std::vector<float> pre_;
};

// The sound driver reuses the same 16 PCM voices for music, effects and the
// engine loop. Voices 0-13 are the 14 music channels; voices 14/15 are always
// the engine. Only an explicitly known effect sample is pulled out into an
// extra SFX channel. Everything else stays music, so a missing sample can
// never make a song lose notes.
int pcm_sample_group(uint8_t sample_id) {
    const int override = sample_override(sample_id);
    if (override == 2) return 1;
    if (override == 1) return 0;
    for (uint8_t id : kSfxSampleIds)
        if (id == sample_id) return 1;
    return 0;
}

int pcm_group_for_voice(int voice, uint8_t sample_id) {
    const int override = sample_override(sample_id);
    if (voice >= 14 &&
        (override == 3 || (sample_id == 0xFF && override != 1)))
        return 2;
    return pcm_sample_group(sample_id);
}

int sfx_category(uint8_t sample_id) {
    if (g_sample_gender[sample_id] == 3) return 1;  // announcer
    if (g_sample_event[sample_id] == 1 || sample_id == 0x47) return 2;  // passing
    return 0;
}

constexpr int kAnnouncerSfxChannel = 16;
constexpr int kPassingSfxChannel = 17;
constexpr int kSfxChannelCount = 18;
constexpr int kSfxCategoryLanes = 16;
constexpr int kSfxReservedLanes = kSfxCategoryLanes * 2;
constexpr int kSfxGeneralLanes = kSfxLanes - kSfxReservedLanes;

int sfx_strip_for_sample(uint8_t sample_id) {
    const int category = sfx_category(sample_id);
    if (category == 1) return kAnnouncerSfxChannel;
    if (category == 2) return kPassingSfxChannel;
    for (int i = 0; i < kSfxSampleCount; ++i)
        if (kSfxSampleIds[i] == sample_id) return i & 15;
    return sample_id & 15;
}

} // namespace

struct Audio::Impl {
    const std::vector<uint8_t> *z80_rom = nullptr;
    const std::vector<uint8_t> *pcm_rom = nullptr;

    z80 cpu{};
    uint8_t latch = 0;
    uint8_t sound_ram[0x800] = {};
    uint8_t pcm_ram[0x100] = {};
    PcmVoice voices[16];
    VirtualVoice lanes[kLaneCount];
    // Keep a separate current lane per role, so music controls do not point at
    // an effect that happens to share the same hardware source voice.
    int current_lane[3][16] = {};
    uint8_t last_sample[16] = {};
    uint64_t lane_serial = 0;
    Audio::SampleLogCallback sample_log = nullptr;
    void *sample_log_user = nullptr;

    struct OpmIntf : ymfm::ymfm_interface {
        Impl *owner = nullptr;
        uint64_t timer_end[2] = {};
        bool timer_active[2] = {};

        void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override {
            if (tnum > 1) return;
            if (owner && owner->trace)
                std::fprintf(stderr, "[opm] timer%d set %d clocks at z80 cyc %llu\n",
                             tnum, duration_in_clocks,
                             (unsigned long long)(owner ? owner->cpu.cyc : 0));
            if (duration_in_clocks < 0) {
                timer_active[tnum] = false;
            } else {
                timer_active[tnum] = true;
                timer_end[tnum] = (owner ? owner->cpu.cyc : 0) +
                                  (uint64_t)(uint32_t)duration_in_clocks;
            }
        }

        void ymfm_update_irq(bool asserted) override {
            if (owner && owner->trace)
                std::fprintf(stderr, "[opm] irq %d at z80 cyc %llu\n",
                             asserted ? 1 : 0,
                             (unsigned long long)(owner ? owner->cpu.cyc : 0));
            if (asserted && owner) z80_gen_int(&owner->cpu, 0xFF);
        }

        void timer_expired(uint32_t tnum) {
            if (m_engine) m_engine->engine_timer_expired(tnum);
        }
    };

    OpmIntf intf;
    ymfm::ym2151 opm;
    bool trace = false;
    uint8_t opm_addr = 0;
    uint8_t last_latch = 0;
    uint64_t last_latch_cyc = 0;
    uint8_t ym_keycode[8] = {};
    uint8_t ym_kf[8] = {};
    uint8_t ym_regs[256] = {};
    bool ym_route_sfx[8] = {};
    std::vector<uint32_t> ym_music_signatures;
    bool ym_music_profile_loaded = false;

    void load_ym_music_signatures(const char *path) {
        ym_music_signatures.clear();
        ym_music_profile_loaded = false;
        std::ifstream f(path);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            try {
                ym_music_signatures.push_back(
                    (uint32_t)std::stoul(line, nullptr, 16));
            } catch (...) {
                // Ignore malformed profile lines.
            }
        }
        std::sort(ym_music_signatures.begin(), ym_music_signatures.end());
        ym_music_profile_loaded = !ym_music_signatures.empty();
    }

    bool is_ym_music_signature(uint32_t sig) const {
        return std::binary_search(ym_music_signatures.begin(),
                                  ym_music_signatures.end(), sig);
    }

    uint32_t ym_signature(int ch) const {
        uint32_t h = 2166136261u;
        auto mix = [&h](uint8_t b) {
            h = (h ^ b) * 16777619u;
        };
        mix((uint8_t)ch);
        mix(ym_keycode[ch]);
        mix(ym_kf[ch]);
        mix(ym_regs[0x20 + ch]);
        for (int slot = 0; slot < 4; ++slot) {
            const int op = ch * 4 + slot;
            mix(ym_regs[0x40 + op]);
            mix(ym_regs[0x60 + op]);
            mix(ym_regs[0x80 + op]);
            mix(ym_regs[0xA0 + op]);
            mix(ym_regs[0xC0 + op]);
            mix(ym_regs[0xE0 + op]);
        }
        return h;
    }
    // Z80 driver split, from the sound ROM:
    //   PCM voices 0-7   music samples
    //   PCM voices 8-13  sound effects
    //   PCM voices 14-15 engine/continuous loop
    //   YM2151           music
    int music_volume = 100;
    int sfx_volume = 100;
    int engine_volume = 100;
    bool voice_normalizer = false;
    bool attract_active = false;
    bool race_active = false;
    int ym_volume[8] = {100, 100, 100, 100, 100, 100, 100, 100};
    bool ym_passing_chime[8] = {};
    int pcm_volume[16] = {100, 100, 100, 100, 100, 100, 100, 100,
                          100, 100, 100, 100, 100, 100, 100, 100};
    int ym_send[8] = {100, 100, 100, 100, 100, 100, 100, 100};
    int pcm_send[16] = {100, 100, 100, 100, 100, 100, 100, 100,
                        100, 100, 100, 100, 100, 100, 100, 100};
    int sfx_lane_volume[kSfxChannelCount] = {
        100, 100, 100, 100, 100, 100, 100, 100, 100,
        100, 100, 100, 100, 100, 100, 100, 100, 100};
    int sfx_lane_send[kSfxChannelCount] = {};
    int sfx_lane_reverb_send[kSfxChannelCount] = {};
    int solo_mask = 0;
    float ym_level[8] = {};
    float pcm_level[16] = {};
    float sfx_level[kSfxChannelCount] = {};
    float sfx_frame_peak[kSfxChannelCount] = {};
    float ym_frame_peak[8] = {};
    float pcm_frame_peak[16] = {};
    bool chorus_enabled = false;
    int chorus_rate = 15;       // tenths of a Hz
    int chorus_depth = 50;      // percent
    int chorus_delay = 12;      // milliseconds
    int chorus_feedback = 0;    // percent
    int chorus_mix = 30;        // percent
    std::vector<float> chorus_buf[2];
    size_t chorus_pos = 0;
    double chorus_phase = 0.0;

    bool reverb_enabled = false;
    int reverb_decay = 60;      // percent
    int reverb_damping = 40;    // percent
    int reverb_predelay = 0;    // milliseconds
    int reverb_mix = 25;        // percent
    int ym_reverb_send[8] = {};
    int pcm_reverb_send[16] = {};
    PlateReverb reverb;

    // Resampler state, kept between frames.
    double pcm_phase = 0.0;
    int32_t pcm_prev_l = 0, pcm_prev_r = 0;
    int32_t pcm_cur_l = 0, pcm_cur_r = 0;
    int32_t pcm_send_prev_l = 0, pcm_send_prev_r = 0;
    int32_t pcm_send_cur_l = 0, pcm_send_cur_r = 0;
    int32_t pcm_rev_prev_l = 0, pcm_rev_prev_r = 0;
    int32_t pcm_rev_cur_l = 0, pcm_rev_cur_r = 0;
    double ym_phase = 0.0;
    int32_t ym_prev_l = 0, ym_prev_r = 0;
    int32_t ym_cur_l = 0, ym_cur_r = 0;
    int32_t ym_send_prev_l = 0, ym_send_prev_r = 0;
    int32_t ym_send_cur_l = 0, ym_send_cur_r = 0;
    int32_t ym_rev_prev_l = 0, ym_rev_prev_r = 0;
    int32_t ym_rev_cur_l = 0, ym_rev_cur_r = 0;
    double ym_step = 1.0;

    std::vector<int16_t> out;

    Impl() : opm(intf) {
        intf.owner = this;
        chorus_buf[0].assign(kChorusSize, 0.0f);
        chorus_buf[1].assign(kChorusSize, 0.0f);
        reverb.init(kOutRate);
    }

    uint8_t read8(uint16_t a) {
        if (a <= 0xEFFF) {
            return z80_rom && a < z80_rom->size() ? (*z80_rom)[a] : 0xFF;
        }
        if (a >= 0xF000 && a < 0xF800) return pcm_read((uint8_t)(a & 0xFF));
        if (a >= 0xF800) return sound_ram[a - 0xF800];
        return 0xFF;
    }

    void write8(uint16_t a, uint8_t v) {
        if (a <= 0xEFFF) return;
        if (a >= 0xF000 && a < 0xF800) { pcm_write((uint8_t)(a & 0xFF), v); return; }
        if (a >= 0xF800) { sound_ram[a - 0xF800] = v; return; }
    }

    static int lane_base(int group) {
        return group == 0 ? 0 : group == 1 ? kMusicLanes : kMusicLanes + kSfxLanes;
    }
    static int lane_count(int group) {
        return group == 0 ? kMusicLanes : group == 1 ? kSfxLanes : kEngineLanes;
    }

    VirtualVoice *active_lane(int source, int group) {
        const int idx = current_lane[group][source];
        if (idx < 0 || !lanes[idx].active || lanes[idx].group != group ||
            lanes[idx].source != source)
            return nullptr;
        return &lanes[idx];
    }

    void start_lane(int source, int group, uint8_t sample) {
        // PCM1-14 are the 14 hardware music voices, so every music sample a
        // song uses has a stable channel. Effects overlap in a separate pool;
        // engine keeps two lanes.
        if (group != 1) {
            const int old = current_lane[group][source];
            if (old >= 0 && lanes[old].active) lanes[old].active = false;
        }

        int slot;
        int channel;
        if (group == 0) {
            channel = source;
            slot = source < kMusicLanes
                       ? source
                       : kMusicLanes + kSfxLanes + kEngineLanes +
                             (source - kMusicLanes);
        } else if (group == 2) {
            channel = 14 + (source >= 15 ? 1 : 0);
            slot = lane_base(2) + (source >= 15 ? 1 : 0);
        } else {
            const int base = lane_base(1);
            const int category = sfx_category(sample);
            if (category != 0) {
                // Announcer and passing voices have a large dedicated lane
                // pool so overlapping countdowns or rapid passes do not cut
                // each other off. All lanes feed the same category strip.
                channel = category == 1 ? kAnnouncerSfxChannel
                                        : kPassingSfxChannel;
                const int cat_base =
                    base + kSfxGeneralLanes +
                    (category - 1) * kSfxCategoryLanes;
                slot = -1;
                for (int i = 0; i < kSfxCategoryLanes; ++i) {
                    if (!lanes[cat_base + i].active) {
                        slot = cat_base + i;
                        break;
                    }
                }
                if (slot < 0) {
                    slot = cat_base;
                    for (int i = 1; i < kSfxCategoryLanes; ++i) {
                        if (lanes[cat_base + i].serial < lanes[slot].serial)
                            slot = cat_base + i;
                    }
                }
            } else {
                channel = sfx_strip_for_sample(sample);
                slot = -1;
                for (int i = 0; i < kSfxGeneralLanes; ++i) {
                    if (!lanes[base + i].active) {
                        slot = base + i;
                        break;
                    }
                }
                if (slot < 0) {
                    slot = base;
                    for (int i = 1; i < kSfxGeneralLanes; ++i) {
                        if (lanes[base + i].serial < lanes[slot].serial)
                            slot = base + i;
                    }
                }
            }
        }

        VirtualVoice &v = lanes[slot];
        if (v.active && current_lane[v.group][v.source] == slot)
            current_lane[v.group][v.source] = -1;
        v.pcm = voices[source];
        v.group = group;
        v.source = source;
        v.channel = channel;
        v.active = true;
        v.serial = ++lane_serial;
        v.sample_id = sample;
        v.voice_effect = (group == 1 && is_voice_effect(sample));
        const bool enemy_passing =
            g_sample_gender[sample] == 4 && g_sample_event[sample] == 1;
        const float gain_db = g_sample_gender[sample] == 1 ? 3.0f
                              : enemy_passing            ? 4.0f
                              : g_sample_gender[sample] == 3
                                  ? 2.0f + kAnnouncerPostGainDb
                                  : 2.0f;
        v.voice_gain = v.voice_effect
                           ? std::pow(10.0f, gain_db / 20.0f)
                           : 1.0f;
        v.hp_alpha = g_sample_gender[sample] == 3
                         ? kAnnouncerHighPassAlpha
                         : kVoiceHighPassAlpha;
        v.hp_l_x = v.hp_l_y = v.hp_r_x = v.hp_r_y = 0.0f;
        v.lp_l_z1 = v.lp_l_z2 = v.lp_r_z1 = v.lp_r_z2 = 0.0f;
        v.agc_env = 0.0f;
        v.agc_gain = 1.0f;
        current_lane[group][source] = slot;
        if (trace &&
            (g_sample_gender[sample] == 3 ||
             g_sample_event[sample] == 1 || sample == 0x47)) {
            std::fprintf(stderr,
                         "[sfxvol] sample=%02X source=%d slot=%d channel=%d "
                         "lvol=%02X rvol=%02X\n",
                         sample, source, slot, channel, voices[source].lvol,
                         voices[source].rvol);
        }
        if (sample_log) sample_log(sample_log_user, source, group, sample);
    }

    uint8_t pcm_read(uint8_t offset) {
        const int reg = offset & 0x7F;
        const int voice = (reg >> 3) & 0x0F;
        const int r = reg & 7;
        PcmVoice &x = voices[voice];
        if (offset & 0x80) {
            if (r == 4) return (uint8_t)(x.addr >> 8);
            if (r == 5) return (uint8_t)(x.addr >> 16);
            if (r == 6) return x.ctrl;
        } else {
            if (r == 2) return x.lvol;
            if (r == 3) return x.rvol;
            if (r == 4) return (uint8_t)x.loop;
            if (r == 5) return (uint8_t)(x.loop >> 8);
            if (r == 6) return x.end;
            if (r == 7) return x.freq;
        }
        return pcm_ram[offset];
    }

    void pcm_write(uint8_t offset, uint8_t v) {
        const int reg = offset & 0x7F;
        const int voice = (reg >> 3) & 0x0F;
        const int r = reg & 7;
        PcmVoice &x = voices[voice];
        const int sample = pcm_ram[voice * 8];
        const int group = pcm_group_for_voice(voice, (uint8_t)sample);
        VirtualVoice *lane = active_lane(voice, group);

        if (offset & 0x80) {
            if (r == 4) {
                x.addr = (x.addr & 0xFFFF00u) | ((uint32_t)v << 8);
                if (lane) lane->pcm.addr = x.addr;
            } else if (r == 5) {
                x.addr = (x.addr & 0x00FFFFu) | ((uint32_t)v << 16);
                if (lane) lane->pcm.addr = x.addr;
            } else if (r == 6) {
                const uint8_t old_ctrl = x.ctrl;
                x.ctrl = v;
                x.group = group;
                if (lane) lane->pcm.ctrl = v;
                if ((v & 1) == 0 && sample != 0 && sample != 0xFF &&
                    (!lane || (old_ctrl & 1) != 0 ||
                     sample != last_sample[voice])) {
                    start_lane(voice, group, (uint8_t)sample);
                } else if ((v & 1) != 0 && lane) {
                    lane->active = false;
                }
                last_sample[voice] = (uint8_t)sample;
                if (trace) {
                    const uint32_t bank = ((uint32_t)(x.ctrl & 0xF8)) << 13;
                    std::fprintf(stderr,
                                 "[pcm] cyc=%llu voice=%d sample=%02X ctrl=%02X "
                                 "freq=%02X group=%d addr=%06X end=%02X "
                                 "loop=%04X bank=%02X\n",
                                 (unsigned long long)cpu.cyc, voice, sample, v,
                                 x.freq, x.group, x.addr, x.end, x.loop,
                                 (unsigned)(bank >> 13));
                }
            } else pcm_ram[offset] = v;
        } else {
            if (r == 2) {
                x.lvol = v;
                if (lane) lane->pcm.lvol = v;
            } else if (r == 3) {
                x.rvol = v;
                if (lane) lane->pcm.rvol = v;
            } else if (r == 4) {
                x.loop = (x.loop & 0xFF00u) | v;
                if (lane) lane->pcm.loop = x.loop;
            } else if (r == 5) {
                x.loop = (x.loop & 0x00FFu) | ((uint16_t)v << 8);
                if (lane) lane->pcm.loop = x.loop;
            } else if (r == 6) {
                x.end = v;
                if (lane) lane->pcm.end = v;
            } else if (r == 7) {
                x.freq = v;
                if (lane) lane->pcm.freq = v;
                if (trace)
                    std::fprintf(stderr,
                                 "[pcmfreq] cyc=%llu voice=%d sample=%02X "
                                 "freq=%02X group=%d\n",
                                 (unsigned long long)cpu.cyc, voice, sample, v,
                                 x.group);
            } else pcm_ram[offset] = v;
        }
    }

    uint8_t port_in(uint8_t p) {
        if ((p & 0xFE) == 0x00) {
            // The sound driver polls the OPM status; the real timer A IRQ
            // supplies the music tick.
            return opm.read(p & 1);
        }
        // Sound latch: 0x40 with the low six address bits mirrored.
        if ((p & 0x3F) == 0x00) return latch;
        return 0xFF;
    }

    void port_out(uint8_t p, uint8_t v) {
        if ((p & 0xFE) == 0x00) {
            if ((p & 1) == 0) opm_addr = v;
            if ((p & 1) == 1) ym_regs[opm_addr] = v;
            if ((p & 1) == 1 && opm_addr >= 0x28 && opm_addr <= 0x2F)
                ym_keycode[opm_addr - 0x28] = v;
            if ((p & 1) == 1 && opm_addr >= 0x30 && opm_addr <= 0x37)
                ym_kf[opm_addr - 0x30] = v;
            if (trace && ((opm_addr >= 0x10 && opm_addr <= 0x14) || opm_addr == 0x27))
                std::fprintf(stderr, "[opm] write %02X = %02X at z80 cyc %llu\n",
                             opm_addr, v, (unsigned long long)cpu.cyc);
            if (opm_addr == 0x08 && (p & 1) == 1) {
                const int ch = v & 7;
                const int mask = (v >> 3) & 0x0F;
                const uint32_t sig = ym_signature(ch);
                if (mask != 0) {
                    const bool known_sfx =
                        is_ym_sfx_signature(sig) ||
                        is_ym_sfx_keycode(ch, ym_keycode[ch]);
                    const bool known_music =
                        ym_music_profile_loaded && is_ym_music_signature(sig);
                    const bool unknown_shared =
                        ym_music_profile_loaded && !known_music &&
                        (ch == 6 || ch == 7);
                    ym_route_sfx[ch] = known_sfx || unknown_shared;
                    ym_passing_chime[ch] =
                        is_ym_chime_keycode(ch, ym_keycode[ch]);
                    if (trace && ym_passing_chime[ch]) {
                        const int trim = attract_active
                                             ? 0
                                             : (race_active
                                                    ? kYmPassingChimeTrim
                                                    : 100);
                        std::fprintf(
                            stderr,
                            "[ymchime] cyc=%llu ch=%d kc=%02X sig=%08X "
                            "attract=%d race=%d trim=%d\n",
                            (unsigned long long)cpu.cyc, ch, ym_keycode[ch],
                            sig, attract_active ? 1 : 0, race_active ? 1 : 0,
                            trim);
                    }
                }
                if (trace) {
                    uint16_t stack[4] = {};
                    for (int i = 0; i < 4; ++i)
                        stack[i] =
                            (uint16_t)read8((uint16_t)(cpu.sp + i * 2)) |
                            ((uint16_t)read8((uint16_t)(cpu.sp + i * 2 + 1))
                             << 8);
                    std::fprintf(
                        stderr,
                        "[ymkey] cyc=%llu pc=%04X sp=%04X "
                        "stack=%04X,%04X,%04X,%04X "
                        "ch=%d mask=%X kc=%02X kf=%02X sig=%08X "
                        "route=%s last=%02X delta=%llu\n",
                        (unsigned long long)cpu.cyc, cpu.pc, cpu.sp, stack[0],
                        stack[1], stack[2], stack[3], ch, mask,
                        ym_keycode[ch], ym_kf[ch], sig,
                        ym_route_sfx[ch] ? "sfx" : "music", last_latch,
                        (unsigned long long)(cpu.cyc - last_latch_cyc));
                }
            }
            opm.write(p & 1, v);
        }
    }

    bool tick_voice(PcmVoice &x, int channel, int group, bool mix,
                    int32_t &l, int32_t &r, int32_t &sl, int32_t &sr,
                    int32_t &rl, int32_t &rr, VirtualVoice *lane = nullptr) {
        x.lout = x.rout = 0;
        if (x.ctrl & 1) {
            x.addr &= 0xFFFF00u;
            return false;
        }
        if ((x.addr >> 16) == ((x.end + 1) & 0xFF)) {
            // The PCM control bit is the hardware loop-disable flag.  Do not
            // force virtual SFX channels to one-shot: crowd/ambient effects
            // deliberately leave looping enabled.
            if (x.ctrl & 2) {
                x.ctrl |= 1;
                return false;
            }
            x.addr = (uint32_t)x.loop << 8;
        }
        if (!mix) {
            x.addr = (x.addr + x.freq) & 0xFFFFFF;
            return true;
        }

        const uint32_t bank = ((uint32_t)(x.ctrl & 0xF8)) << 13;
        const uint32_t at = (bank + ((x.addr >> 8) & 0x1FFFFF)) & 0x1FFFFF;
        int v = 0;
        if (pcm_rom && at < pcm_rom->size()) v = (int)(*pcm_rom)[at] - 0x80;
        const int bus = group == 1 ? sfx_volume
                        : group == 2 ? engine_volume
                                     : music_volume;
        const int *volume = group == 1 ? sfx_lane_volume : pcm_volume;
        const int *send = group == 1 ? sfx_lane_send : pcm_send;
        const int *rev_send = group == 1 ? sfx_lane_reverb_send : pcm_reverb_send;
        const int solo_bit = group == 1 ? 24 + channel : 8 + channel;
        const bool solo_mute =
            solo_mask != 0 && !(solo_mask & (1u << solo_bit));
        const int gain = solo_mute ? 0 : bus * volume[channel] / 100;
        x.lout = v * (x.lvol & 0x7F) * gain / 100;
        x.rout = v * (x.rvol & 0x7F) * gain / 100;
        if (voice_normalizer && lane && lane->voice_effect && gain > 0) {
            // Keep only the speech band in the normalizer and remove the
            // chest-heavy low end that the original samples carry.
            const float in_l = (float)x.lout;
            const float in_r = (float)x.rout;
            const float hp_l =
                lane->hp_alpha * (lane->hp_l_y + in_l - lane->hp_l_x);
            const float hp_r =
                lane->hp_alpha * (lane->hp_r_y + in_r - lane->hp_r_x);
            lane->hp_l_x = in_l;
            lane->hp_l_y = hp_l;
            lane->hp_r_x = in_r;
            lane->hp_r_y = hp_r;

            // Roll off the top of the speech band before the resampler.
            const float lp_l = kVoiceLowPass.process(
                hp_l, lane->lp_l_z1, lane->lp_l_z2);
            const float lp_r = kVoiceLowPass.process(
                hp_r, lane->lp_r_z1, lane->lp_r_z2);

            // Per-voice AGC: bring quiet announcer/passing/hit lines up and
            // trim the loudest ones. Only tagged voice effects use this, so
            // tyre screech, engine and music keep their original dynamics.
            const float peak =
                std::max(std::abs(lp_l), std::abs(lp_r)) / 32768.0f;
            lane->agc_env = std::max(peak, lane->agc_env * 0.995f);
            float target_gain = 1.0f;
            if (lane->agc_env > 0.01f)
                target_gain =
                    std::clamp(0.16f / lane->agc_env, 0.5f, 4.0f);
            // Fast but not instantaneous attack avoids a gain-step click at
            // the start of a syllable; the slow release avoids pumping
            // between words.
            if (target_gain < lane->agc_gain)
                lane->agc_gain +=
                    (target_gain - lane->agc_gain) * 0.15f;
            else
                lane->agc_gain +=
                    (target_gain - lane->agc_gain) * 0.002f;
            const float g = lane->agc_gain * lane->voice_gain;
            // Soft-knee limit the voice path instead of hard-clipping it.
            // This prevents the crackle that already-loud announcer samples
            // pick up when makeup gain pushes them into the PCM ceiling.
            const auto soft_limit = [](float sample) {
                constexpr float knee = 0.82f;
                const float magnitude = std::abs(sample) / 32768.0f;
                if (magnitude <= knee) return sample;
                const float excess = (magnitude - knee) / (1.0f - knee);
                const float shaped =
                    knee + (1.0f - knee) * std::tanh(excess);
                return std::copysign(shaped * 32768.0f, sample);
            };
            const float out_l = soft_limit(lp_l * g);
            const float out_r = soft_limit(lp_r * g);
            x.lout = std::clamp((int32_t)std::lround(out_l), -32768, 32767);
            x.rout = std::clamp((int32_t)std::lround(out_r), -32768, 32767);
        }
        // Meter the audible channel contribution: bus and channel trim are
        // already folded into lout/rout.  A muted bus therefore stops the VU.
        const float raw =
            std::max(std::abs(x.lout), std::abs(x.rout)) / 16384.0f;
        if (group == 1)
            sfx_frame_peak[channel] = std::max(sfx_frame_peak[channel], raw);
        else
            pcm_frame_peak[channel] = std::max(pcm_frame_peak[channel], raw);
        x.addr = (x.addr + x.freq) & 0xFFFFFF;
        l += x.lout;
        r += x.rout;
        sl += x.lout * send[channel] / 100;
        sr += x.rout * send[channel] / 100;
        rl += x.lout * rev_send[channel] / 100;
        rr += x.rout * rev_send[channel] / 100;
        return true;
    }

    void pcm_tick() {
        int32_t l = 0, r = 0, sl = 0, sr = 0, rl = 0, rr = 0;

        // The hardware voices still tick so the Z80 driver can read their
        // addresses back; their audio is replaced by the virtual lanes below.
        for (int vi = 0; vi < 16; ++vi)
            tick_voice(voices[vi], vi, voices[vi].group, false, l, r, sl, sr,
                       rl, rr);

        for (int i = 0; i < kLaneCount; ++i) {
            VirtualVoice &v = lanes[i];
            if (!v.active) continue;
            if (!tick_voice(v.pcm, v.channel, v.group, true, l, r, sl, sr, rl,
                            rr, &v))
                v.active = false;
            if (!v.active && current_lane[v.group][v.source] == i)
                current_lane[v.group][v.source] = -1;
        }

        pcm_cur_l = std::clamp(l, -32768, 32767);
        pcm_cur_r = std::clamp(r, -32768, 32767);
        pcm_send_cur_l = std::clamp(sl, -32768, 32767);
        pcm_send_cur_r = std::clamp(sr, -32768, 32767);
        pcm_rev_cur_l = std::clamp(rl, -32768, 32767);
        pcm_rev_cur_r = std::clamp(rr, -32768, 32767);
    }

    float chorus_read(int ch, float delay) const {
        float pos = (float)chorus_pos - delay;
        while (pos < 0.0f) pos += kChorusSize;
        while (pos >= kChorusSize) pos -= kChorusSize;
        const int i0 = (int)pos;
        const int i1 = (i0 + 1) % kChorusSize;
        const float frac = pos - i0;
        return chorus_buf[ch][i0] * (1.0f - frac) + chorus_buf[ch][i1] * frac;
    }

    void chorus_wet(float in_l, float in_r, float &wet_l, float &wet_r) {
        wet_l = wet_r = 0.0f;
        if (chorus_enabled) {
            const float base = (chorus_delay / 1000.0f) * (float)kOutRate;
            const float depth = 0.008f * (float)kOutRate * (chorus_depth / 100.0f);
            const float lfo_l = 0.5f + 0.5f * (float)std::sin(chorus_phase);
            const float lfo_r = 0.5f + 0.5f * (float)std::sin(chorus_phase + 1.57079632679);
            wet_l = chorus_read(0, base + depth * lfo_l);
            wet_r = chorus_read(1, base + depth * lfo_r);
        }
        const float fb = chorus_feedback / 100.0f;
        chorus_buf[0][chorus_pos] =
            std::clamp(in_l + wet_l * fb, -32768.0f, 32767.0f);
        chorus_buf[1][chorus_pos] =
            std::clamp(in_r + wet_r * fb, -32768.0f, 32767.0f);
        chorus_pos = (chorus_pos + 1) % kChorusSize;
        chorus_phase += 2.0 * 3.141592653589793 * (chorus_rate / 10.0) / kOutRate;
        if (chorus_phase >= 2.0 * 3.141592653589793)
            chorus_phase -= 2.0 * 3.141592653589793;
    }

    void ym_tick() {
        ymfm::ym2151::output_data data[8];
        opm.generate_channels(data, 1);
        int32_t l = 0, r = 0, sl = 0, sr = 0, rl = 0, rr = 0;
        for (int ch = 0; ch < 8; ++ch) {
            const bool solo_mute =
                solo_mask != 0 && !(solo_mask & (1u << ch));
            const int bus = ym_route_sfx[ch] ? sfx_volume : music_volume;
            const int trim =
                ym_passing_chime[ch]
                    ? (attract_active ? 0
                                      : (race_active ? kYmPassingChimeTrim
                                                     : 100))
                    : 100;
            const int32_t cl = solo_mute
                                   ? 0
                                   : data[ch].data[0] * ym_volume[ch] / 100 *
                                         bus / 100 * trim / 100;
            const int32_t cr = solo_mute
                                   ? 0
                                   : data[ch].data[1] * ym_volume[ch] / 100 *
                                         bus / 100 * trim / 100;
            l += cl;
            r += cr;
            sl += cl * ym_send[ch] / 100;
            sr += cr * ym_send[ch] / 100;
            rl += cl * ym_reverb_send[ch] / 100;
            rr += cr * ym_reverb_send[ch] / 100;
            const float raw =
                std::max(std::abs(cl), std::abs(cr)) / 32768.0f;
            ym_frame_peak[ch] = std::max(ym_frame_peak[ch], raw);
        }
        ym_cur_l = std::clamp(l, -32768, 32767);
        ym_cur_r = std::clamp(r, -32768, 32767);
        ym_send_cur_l = std::clamp(sl, -32768, 32767);
        ym_send_cur_r = std::clamp(sr, -32768, 32767);
        ym_rev_cur_l = std::clamp(rl, -32768, 32767);
        ym_rev_cur_r = std::clamp(rr, -32768, 32767);
    }

    static uint8_t z80_read_cb(void *ud, uint16_t a) {
        return static_cast<Impl *>(ud)->read8(a);
    }
    static void z80_write_cb(void *ud, uint16_t a, uint8_t v) {
        static_cast<Impl *>(ud)->write8(a, v);
    }
    static uint8_t z80_in_cb(z80 *z, uint8_t p) {
        return static_cast<Impl *>(z->userdata)->port_in(p);
    }
    static void z80_out_cb(z80 *z, uint8_t p, uint8_t v) {
        static_cast<Impl *>(z->userdata)->port_out(p, v);
    }

    void run() {
        out.assign((size_t)kOutFrames * 2, 0);
        std::fill(std::begin(ym_frame_peak), std::end(ym_frame_peak), 0.0f);
        std::fill(std::begin(pcm_frame_peak), std::end(pcm_frame_peak), 0.0f);
        std::fill(std::begin(sfx_frame_peak), std::end(sfx_frame_peak), 0.0f);
        const uint64_t start = cpu.cyc;
        const uint64_t end = start + kZ80PerFrame;
        auto run_z80_to = [&](uint64_t target) {
            // Never let a corrupt/overflowed target turn one sample into a
            // multi-minute sound-CPU run. Four emulated frames is already far
            // beyond the normal per-sample slice.
            const uint64_t max_target =
                cpu.cyc + (uint64_t)kZ80PerFrame * 4 + 4096;
            if (target > max_target) target = max_target;
            int guard = 0;
            while (cpu.cyc < target) {
                // YM timers are expressed in 4 MHz clocks, the same time base
                // as the Z80 T-state counter.
                for (int t = 0; t < 2; t++) {
                    if (intf.timer_active[t] && cpu.cyc >= intf.timer_end[t]) {
                        intf.timer_active[t] = false;
                        intf.timer_expired((uint32_t)t);
                    }
                }
                const uint64_t before = cpu.cyc;
                z80_step(&cpu);
                // Failsafe for a stuck driver wait loop: never let one output
                // sample consume the process for an unbounded time.
                if (cpu.cyc == before || ++guard > 2000000) break;
            }
        };

        for (int i = 0; i < kOutFrames; i++) {
            // Advance the sound CPU in step with the output samples instead of
            // running a whole Z80 frame and then rendering its final state.
            const uint64_t sample_end =
                start + ((uint64_t)(i + 1) * kZ80PerFrame) / kOutFrames;
            run_z80_to(sample_end);

            pcm_phase += (double)kPcmRate / kOutRate;
            while (pcm_phase >= 1.0) {
                pcm_prev_l = pcm_cur_l;
                pcm_prev_r = pcm_cur_r;
                pcm_send_prev_l = pcm_send_cur_l;
                pcm_send_prev_r = pcm_send_cur_r;
                pcm_rev_prev_l = pcm_rev_cur_l;
                pcm_rev_prev_r = pcm_rev_cur_r;
                pcm_tick();
                pcm_phase -= 1.0;
            }
            ym_phase += ym_step;
            while (ym_phase >= 1.0) {
                ym_prev_l = ym_cur_l;
                ym_prev_r = ym_cur_r;
                ym_send_prev_l = ym_send_cur_l;
                ym_send_prev_r = ym_send_cur_r;
                ym_rev_prev_l = ym_rev_cur_l;
                ym_rev_prev_r = ym_rev_cur_r;
                ym_tick();
                ym_phase -= 1.0;
            }

            int32_t pl = pcm_prev_l +
                         (int32_t)((pcm_cur_l - pcm_prev_l) * pcm_phase);
            int32_t pr = pcm_prev_r +
                         (int32_t)((pcm_cur_r - pcm_prev_r) * pcm_phase);
            int32_t psl = pcm_send_prev_l +
                          (int32_t)((pcm_send_cur_l - pcm_send_prev_l) * pcm_phase);
            int32_t psr = pcm_send_prev_r +
                          (int32_t)((pcm_send_cur_r - pcm_send_prev_r) * pcm_phase);
            int32_t prl = pcm_rev_prev_l +
                          (int32_t)((pcm_rev_cur_l - pcm_rev_prev_l) * pcm_phase);
            int32_t prr = pcm_rev_prev_r +
                          (int32_t)((pcm_rev_cur_r - pcm_rev_prev_r) * pcm_phase);
            const int32_t yl = ym_prev_l +
                               (int32_t)((ym_cur_l - ym_prev_l) * ym_phase);
            const int32_t yr = ym_prev_r +
                               (int32_t)((ym_cur_r - ym_prev_r) * ym_phase);
            const int32_t ysl = ym_send_prev_l +
                                (int32_t)((ym_send_cur_l - ym_send_prev_l) * ym_phase);
            const int32_t ysr = ym_send_prev_r +
                                (int32_t)((ym_send_cur_r - ym_send_prev_r) * ym_phase);
            const int32_t yrl = ym_rev_prev_l +
                                (int32_t)((ym_rev_cur_l - ym_rev_prev_l) * ym_phase);
            const int32_t yrr = ym_rev_prev_r +
                                (int32_t)((ym_rev_cur_r - ym_rev_prev_r) * ym_phase);
            // PCM and YM channel gains are already applied. YM channels can
            // individually belong to the music or SFX bus.
            int16_t dry_l = clamp16((pl * 7 + yl * 3) / 10);
            int16_t dry_r = clamp16((pr * 7 + yr * 3) / 10);
            const float send_l = (psl * 7 + ysl * 3) / 10.0f;
            const float send_r = (psr * 7 + ysr * 3) / 10.0f;
            const float rev_send_l = (prl * 7 + yrl * 3) / 10.0f;
            const float rev_send_r = (prr * 7 + yrr * 3) / 10.0f;
            float wet_l = 0.0f, wet_r = 0.0f;
            chorus_wet(send_l, send_r, wet_l, wet_r);
            const float ret = chorus_mix / 100.0f;
            reverb.set_params(reverb_enabled, reverb_decay, reverb_damping,
                              reverb_predelay);
            float rev_l = 0.0f, rev_r = 0.0f;
            reverb.process(rev_send_l, rev_send_r, rev_l, rev_r);
            const float rev_ret = reverb_mix / 100.0f;
            const int16_t l = clamp16((int32_t)std::lround(dry_l + wet_l * ret +
                                                          rev_l * rev_ret));
            const int16_t r = clamp16((int32_t)std::lround(dry_r + wet_r * ret +
                                                          rev_r * rev_ret));
            out[(size_t)i * 2 + 0] = l;
            out[(size_t)i * 2 + 1] = r;
        }

        for (int ch = 0; ch < 8; ++ch)
            ym_level[ch] = std::max(ym_frame_peak[ch], ym_level[ch] * 0.82f);
        for (int v = 0; v < 16; ++v)
            pcm_level[v] = std::max(pcm_frame_peak[v], pcm_level[v] * 0.82f);
        for (int v = 0; v < kSfxChannelCount; ++v)
            sfx_level[v] = std::max(sfx_frame_peak[v], sfx_level[v] * 0.82f);
    }
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {}
Audio::~Audio() = default;

void Audio::reset(const std::vector<uint8_t> &z80_rom,
                  const std::vector<uint8_t> &pcm_rom) {
    impl_->z80_rom = &z80_rom;
    impl_->pcm_rom = &pcm_rom;
    load_sample_roles("samples/roles.cfg");
    impl_->load_ym_music_signatures("samples/ym_music_signatures.cfg");
    std::memset(impl_->sound_ram, 0, sizeof(impl_->sound_ram));
    std::memset(impl_->pcm_ram, 0, sizeof(impl_->pcm_ram));
    for (auto &v : impl_->voices) v = PcmVoice{};
    for (auto &v : impl_->lanes) v = VirtualVoice{};
    for (auto &role : impl_->current_lane)
        for (int &v : role) v = -1;
    std::memset(impl_->last_sample, 0, sizeof(impl_->last_sample));
    impl_->lane_serial = 0;
    impl_->latch = 0;
    impl_->last_latch = 0;
    impl_->last_latch_cyc = 0;
    std::memset(impl_->ym_keycode, 0, sizeof(impl_->ym_keycode));
    std::memset(impl_->ym_kf, 0, sizeof(impl_->ym_kf));
    std::memset(impl_->ym_regs, 0, sizeof(impl_->ym_regs));
    std::memset(impl_->ym_route_sfx, 0, sizeof(impl_->ym_route_sfx));
    std::memset(impl_->ym_passing_chime, 0, sizeof(impl_->ym_passing_chime));
    impl_->solo_mask = 0;
    std::fill(std::begin(impl_->sfx_lane_volume), std::end(impl_->sfx_lane_volume), 100);
    std::fill(std::begin(impl_->sfx_lane_send), std::end(impl_->sfx_lane_send), 0);
    std::fill(std::begin(impl_->sfx_lane_reverb_send),
              std::end(impl_->sfx_lane_reverb_send), 0);
    impl_->intf.timer_active[0] = impl_->intf.timer_active[1] = false;
    impl_->intf.timer_end[0] = impl_->intf.timer_end[1] = 0;

    z80_init(&impl_->cpu);
    impl_->cpu.userdata = impl_.get();
    impl_->cpu.read_byte = Impl::z80_read_cb;
    impl_->cpu.write_byte = Impl::z80_write_cb;
    impl_->cpu.port_in = Impl::z80_in_cb;
    impl_->cpu.port_out = Impl::z80_out_cb;

    impl_->opm.reset();
    impl_->ym_step = (double)impl_->opm.sample_rate(kZ80Clock) / kOutRate;
    if (impl_->ym_step <= 0.0) impl_->ym_step = 1.0;
    impl_->pcm_phase = 0.0;
    impl_->ym_phase = 0.0;
    impl_->pcm_prev_l = impl_->pcm_prev_r = 0;
    impl_->pcm_cur_l = impl_->pcm_cur_r = 0;
    impl_->pcm_send_prev_l = impl_->pcm_send_prev_r = 0;
    impl_->pcm_send_cur_l = impl_->pcm_send_cur_r = 0;
    impl_->ym_prev_l = impl_->ym_prev_r = 0;
    impl_->ym_cur_l = impl_->ym_cur_r = 0;
    impl_->ym_send_prev_l = impl_->ym_send_prev_r = 0;
    impl_->ym_send_cur_l = impl_->ym_send_cur_r = 0;
    impl_->pcm_rev_prev_l = impl_->pcm_rev_prev_r = 0;
    impl_->pcm_rev_cur_l = impl_->pcm_rev_cur_r = 0;
    impl_->ym_rev_prev_l = impl_->ym_rev_prev_r = 0;
    impl_->ym_rev_cur_l = impl_->ym_rev_cur_r = 0;
    for (int &v : impl_->ym_reverb_send) v = 0;
    for (int &v : impl_->pcm_reverb_send) v = 0;
    impl_->reverb_enabled = false;
    impl_->reverb_decay = 60;
    impl_->reverb_damping = 40;
    impl_->reverb_predelay = 0;
    impl_->reverb_mix = 25;
    impl_->reverb.clear();
    impl_->chorus_pos = 0;
    impl_->chorus_phase = 0.0;
    std::fill(impl_->chorus_buf[0].begin(), impl_->chorus_buf[0].end(), 0.0f);
    std::fill(impl_->chorus_buf[1].begin(), impl_->chorus_buf[1].end(), 0.0f);
}

void Audio::sound_latch(uint8_t value) {
    if (impl_->trace)
        std::fprintf(stderr, "[latch] cyc=%llu value=%02X\n",
                     (unsigned long long)impl_->cpu.cyc, value);
    impl_->latch = value;
    impl_->last_latch = value;
    impl_->last_latch_cyc = impl_->cpu.cyc;
    z80_gen_nmi(&impl_->cpu);
}

void Audio::run_frame() { impl_->run(); }

void Audio::set_trace(bool on) { impl_->trace = on; }

void Audio::set_sample_log(SampleLogCallback callback, void *user) {
    impl_->sample_log = callback;
    impl_->sample_log_user = user;
}

void Audio::set_volumes(int music_percent, int sfx_percent, int engine_percent) {
    impl_->music_volume = std::clamp(music_percent, 0, kBusVolumeMax);
    impl_->sfx_volume = std::clamp(sfx_percent, 0, kBusVolumeMax);
    impl_->engine_volume = std::clamp(engine_percent, 0, kBusVolumeMax);
}

void Audio::set_voice_normalizer(bool enabled) {
    impl_->voice_normalizer = enabled;
}

void Audio::set_attract_active(bool active) {
    impl_->attract_active = active;
}

void Audio::set_race_active(bool active) {
    impl_->race_active = active;
}

void Audio::set_chorus_sends(const int *ym_percent, const int *pcm_percent) {
    if (ym_percent)
        for (int i = 0; i < 8; ++i)
            impl_->ym_send[i] = std::clamp(ym_percent[i], 0, 100);
    if (pcm_percent)
        for (int i = 0; i < 16; ++i)
            impl_->pcm_send[i] = std::clamp(pcm_percent[i], 0, 100);
}

void Audio::set_solo_mask(uint32_t mask) {
    impl_->solo_mask = (int)(mask & 0xFFFFFFFFu);
}

void Audio::set_sfx_volumes(const int *volumes, const int *sends,
                            const int *reverb_sends) {
    if (volumes)
        for (int i = 0; i < kSfxChannelCount; ++i)
            impl_->sfx_lane_volume[i] = std::clamp(volumes[i], 0, kChannelVolumeMax);
    if (sends)
        for (int i = 0; i < kSfxChannelCount; ++i)
            impl_->sfx_lane_send[i] = std::clamp(sends[i], 0, 100);
    if (reverb_sends)
        for (int i = 0; i < kSfxChannelCount; ++i)
            impl_->sfx_lane_reverb_send[i] = std::clamp(reverb_sends[i], 0, 100);
}

void Audio::set_chorus(bool enabled, int rate_tenths, int depth_percent,
                       int delay_ms, int feedback_percent, int mix_percent) {
    impl_->chorus_enabled = enabled;
    impl_->chorus_rate = std::clamp(rate_tenths, 1, 50);
    impl_->chorus_depth = std::clamp(depth_percent, 0, 100);
    impl_->chorus_delay = std::clamp(delay_ms, 5, 30);
    impl_->chorus_feedback = std::clamp(feedback_percent, 0, 90);
    impl_->chorus_mix = std::clamp(mix_percent, 0, 100);
}

void Audio::set_reverb_sends(const int *ym_percent, const int *pcm_percent) {
    if (ym_percent)
        for (int i = 0; i < 8; ++i)
            impl_->ym_reverb_send[i] = std::clamp(ym_percent[i], 0, 100);
    if (pcm_percent)
        for (int i = 0; i < 16; ++i)
            impl_->pcm_reverb_send[i] = std::clamp(pcm_percent[i], 0, 100);
}

void Audio::set_reverb(bool enabled, int decay_percent, int damping_percent,
                       int predelay_ms, int mix_percent) {
    impl_->reverb_enabled = enabled;
    impl_->reverb_decay = std::clamp(decay_percent, 0, 100);
    impl_->reverb_damping = std::clamp(damping_percent, 0, 100);
    impl_->reverb_predelay = std::clamp(predelay_ms, 0, 50);
    impl_->reverb_mix = std::clamp(mix_percent, 0, 100);
}

void Audio::set_channel_volumes(const int *ym_percent, const int *pcm_percent) {
    if (ym_percent)
        for (int i = 0; i < 8; ++i)
            impl_->ym_volume[i] = std::clamp(ym_percent[i], 0, kChannelVolumeMax);
    if (pcm_percent)
        for (int i = 0; i < 16; ++i)
            impl_->pcm_volume[i] = std::clamp(pcm_percent[i], 0, kChannelVolumeMax);
}

void Audio::get_levels(float *ym_levels, float *pcm_levels) const {
    if (ym_levels)
        for (int i = 0; i < 8; ++i)
            ym_levels[i] = impl_ ? std::clamp(impl_->ym_level[i], 0.0f, 1.0f) : 0.0f;
    if (pcm_levels)
        for (int i = 0; i < 16; ++i)
            pcm_levels[i] = impl_ ? std::clamp(impl_->pcm_level[i], 0.0f, 1.0f) : 0.0f;
}

void Audio::get_sfx_levels(float *levels) const {
    if (levels)
        for (int i = 0; i < kSfxChannelCount; ++i)
            levels[i] = impl_ ? std::clamp(impl_->sfx_level[i], 0.0f, 1.0f) : 0.0f;
}

const int16_t *Audio::samples() const { return impl_->out.data(); }
size_t Audio::sample_count() const { return impl_->out.size(); }

uint16_t Audio::z80_pc() const { return impl_ ? impl_->cpu.pc : 0; }
uint64_t Audio::z80_cycles() const {
    return impl_ ? impl_->cpu.cyc : 0;
}

} // namespace likethewind
