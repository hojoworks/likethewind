#ifndef LIKETHEWIND_MENU_INTERNAL_H
#define LIKETHEWIND_MENU_INTERNAL_H

#include <cstdint>
#include <cstdio>
#include <string>

namespace likethewind {
namespace menu_internal {

// Shared by the menu model and its renderer.
inline constexpr int kTabCount = 6;
inline constexpr const char *kOnOff[2] = {"OFF", "ON"};

inline std::string fmt(const char *f, int v) {
    char b[64];
    std::snprintf(b, sizeof(b), f, v);
    return b;
}

inline std::string fmt_fps(double v) {
    char b[64];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

inline std::string fmt_rate(int tenths) {
    char b[32];
    std::snprintf(b, sizeof(b), "%d.%d HZ", tenths / 10, tenths % 10);
    return b;
}

inline std::string pcm_channel_name(int channel) {
    return "PCM" + std::to_string(channel + 1);
}

inline std::string sfx_channel_name(int channel) {
    if (channel == 16) return "ANNOUNCER";
    if (channel == 17) return "PASSING";
    return "SFX" + std::to_string(channel + 1);
}

} // namespace menu_internal
} // namespace likethewind

#endif // LIKETHEWIND_MENU_INTERNAL_H
