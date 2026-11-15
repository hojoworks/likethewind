#ifndef LIKETHEWIND_WINDOW_TOUR_H
#define LIKETHEWIND_WINDOW_TOUR_H

#include <array>
#include <cstdint>
#include <string>

namespace likethewind {

class Machine;

// Host-side "Special Tour" state. The board still owns the race simulation;
// this only chooses the course/stage shown on the course-select screen and
// advances after a completed course. Course stages 0-4 are the normal
// championship, and stage 5 is the hidden special stage for that course.
struct SpecialTourState {
    static constexpr int kCourses = 5;
    static constexpr int kStages = 5;

    int phase = 0;   // 0 = A-E, 1 = Afterburner, 2 = Hang-On, 3 = complete
    int start_course = 0;
    int course = 0;
    int stage = 0;
    int courses_done = 0;
    bool started = false;
    bool race_started = false;
    bool race_finished = false;
    int last_mode = -1;
    std::array<std::array<int8_t, kStages>, kCourses> places{};
    std::array<int8_t, 2> special_places{{-1, -1}};
    bool tracker_ready = false;
    bool results_hold = false;   // keep final totals up until a new game
    bool score_baseline_pending = true;
    int last_score_seen = 0;
    uint64_t total_frames = 0;
    uint64_t total_hundredths = 0;
    uint64_t stage_start_hundredths = 0;
    int64_t total_score = 0;
    int64_t race_start_frame = -1;

    SpecialTourState() { reset(); }

    void reset();
    bool course_all_first(int c) const;
};

int player_place(const Machine &machine);
int bcd_value(uint8_t value);
uint64_t game_clock_hundredths(const Machine &machine);
int game_score(const Machine &machine);

// The HUD uses a table of pre-rendered two-character tiles. Resolve a tile
// address back to its numeric pair so the tracker can follow the exact value
// the HUD is showing, including any score bonus that is still pending display.
int hud_pair_value(const Machine &machine, uint16_t tile, bool high_only);
int64_t read_hud_time(const Machine &machine, const uint8_t *sprite_list);
std::string format_tour_clock(uint64_t hundredths);

} // namespace likethewind

#endif // LIKETHEWIND_WINDOW_TOUR_H
