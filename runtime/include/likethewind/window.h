#ifndef LIKETHEWIND_WINDOW_H
#define LIKETHEWIND_WINDOW_H

namespace likethewind {
class Machine;
struct Settings;

// Returns false if SDL is unavailable or the window could not be created.
bool run_window(Machine &machine, Settings &settings, int max_frames,
                bool save_settings = true, bool open_menu = false);
}

#endif
