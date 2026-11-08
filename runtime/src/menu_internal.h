#ifndef LIKETHEWIND_MENU_INTERNAL_H
#define LIKETHEWIND_MENU_INTERNAL_H

#include <cstdint>
#include <string>

namespace likethewind {
namespace menu_internal {

std::string fmt(const char *f, int v);
std::string fmt_fps(double v);
std::string fmt_rate(int tenths);
std::string pcm_channel_name(int channel);
std::string sfx_channel_name(int channel);
std::string pad_axis_name(int axis, bool invert = false);

} // namespace menu_internal
} // namespace likethewind

#endif
