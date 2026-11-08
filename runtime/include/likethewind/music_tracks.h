#ifndef LIKETHEWIND_MUSIC_TRACKS_H
#define LIKETHEWIND_MUSIC_TRACKS_H

#include <cstdint>

namespace likethewind {

struct MusicTrack {
    const char *name;
    uint8_t cmd;
};

// Music track commands used by the Y-Board sound driver. 90H is the ending /
// name-entry theme ("Diversity"); 91H is the unknown/unused loop. The rest are
// the course and opening themes.
inline constexpr MusicTrack kMusicTracks[] = {
    {"DIVERSITY", 0x90},       {"UNKNOWN LOOP", 0x91},
    {"ARTISTIC TRAPS", 0x92},  {"ADJUSTMENT MIND", 0x93},
    {"POKER FACE", 0x94},      {"SILENT LANGUAGE", 0x95},
    {"SIDE STREET", 0x96},     {"LIKE THE WIND", 0x97},
};

inline constexpr int kMusicTrackCount =
    (int)(sizeof(kMusicTracks) / sizeof(kMusicTracks[0]));

inline const char *music_track_name(uint8_t cmd) {
    for (const MusicTrack &t : kMusicTracks)
        if (t.cmd == cmd) return t.name;
    return "";
}

} // namespace likethewind

#endif
