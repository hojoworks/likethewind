#include "window_tour.h"

#include "likethewind/bytes.h"
#include "likethewind/machine.h"

#include <algorithm>
#include <cstdio>

namespace likethewind {

void SpecialTourState::reset() {
    phase = 0;
    course = std::clamp(start_course, 0, 4);
    stage = 0;
    courses_done = 0;
    started = false;
    race_started = false;
    race_finished = false;
    last_mode = -1;
    total_hundredths = 0;
    stage_start_hundredths = 0;
    total_score = 0;
    race_start_frame = -1;
    for (auto &row : places) row.fill(-1);
    special_places.fill(-1);
    tracker_ready = false;
    results_hold = false;
    score_baseline_pending = true;
    last_score_seen = 0;
}

bool SpecialTourState::course_all_first(int c) const {
    if (c < 0 || c >= kCourses) return false;
    for (int8_t place : places[static_cast<std::size_t>(c)])
        if (place != 0) return false;
    return true;
}

int player_place(const Machine &machine) {
    // $CE620 points at the player object; $26(a0) is the race position
    // (0 = first, 11 = last). The finish handler reads the same field.
    const uint32_t ptr = read_be32(machine.shared + 0xE620);
    if (ptr < 0xC0000u || ptr >= 0xD0000u) return -1;
    const size_t off = (size_t)(ptr - 0xC0000u) + 0x26;
    if (off >= sizeof(machine.shared)) return -1;
    return machine.shared[off];
}

static int bcd_value(uint8_t value) {
    return ((value >> 4) * 10) + (value & 0x0F);
}

uint64_t game_clock_hundredths(const Machine &machine) {
    // The race clock is the three-byte BCD value at CE8A0: minutes,
    // seconds, hundredths. The HUD builds its TIME line from these bytes.
    const int minutes = bcd_value(machine.shared[0xE8A0]);
    const int seconds = bcd_value(machine.shared[0xE8A1]);
    const int hundredths = bcd_value(machine.shared[0xE8A2]);
    return (uint64_t)minutes * 6000 + (uint64_t)seconds * 100 +
           (uint64_t)hundredths;
}

int game_score(const Machine &machine) {
    // The live score is a 32-bit packed BCD value at CFF60. The HUD score
    // line is generated from it by the sub CPU.
    const uint32_t raw = read_be32(machine.shared + 0xFF60);
    uint32_t value = raw;
    uint32_t result = 0;
    uint32_t scale = 1;
    for (int i = 0; i < 8; ++i) {
        const uint32_t digit = value & 0x0F;
        if (digit > 9) return (int)raw;  // not BCD; keep the raw value
        result += digit * scale;
        scale *= 10;
        value >>= 4;
    }
    return (int)result;
}

// The HUD uses a table of pre-rendered two-character tiles. Resolve a tile
// address back to its numeric pair so the tracker can follow the exact value
// the HUD is showing, including any score bonus that is still pending display.
static int hud_pair_value(const Machine &machine, uint16_t tile, bool high_only) {
    const uint8_t *rom = machine.suby_rom();
    if (!rom || !tile) return -1;
    const int count = high_only ? 10 : 100;
    const int base = high_only ? 0x3F28 : 0x3F48;
    for (int value = 0; value < count; ++value) {
        const int bcd = high_only ? value : ((value / 10) << 4) | (value % 10);
        const uint16_t def = read_be16(rom + base + bcd * 2);
        if (!def) continue;
        if (read_be16(rom + def + 2) == tile) return value;
    }
    return -1;
}

int64_t read_hud_time(const Machine &machine, const uint8_t *sprite_list) {
    int pair[3] = {-1, -1, -1};
    for (int entry = 0; entry < 256; ++entry) {
        const uint8_t *d = sprite_list + entry * 16;
        const uint16_t w0 = read_be16(d);
        const int top = w0 & 0xFF;
        const int bottom = w0 >> 8;
        if (top != 51 || bottom != 57) continue;
        const int screen_x = (read_be16(d + 2) & 0x01FF) - 0xBD;
        int slot = -1;
        if (screen_x == 34) slot = 0;
        else if (screen_x == 52) slot = 1;
        else if (screen_x == 70) slot = 2;
        if (slot < 0) continue;
        int value = hud_pair_value(machine, read_be16(d + 6), true);
        if (value < 0) value = hud_pair_value(machine, read_be16(d + 6), false);
        pair[slot] = value;
    }
    if (pair[1] >= 0 && pair[2] >= 0) {
        const int minutes = pair[0] >= 0 ? pair[0] : 0;
        return (int64_t)minutes * 6000 + pair[1] * 100 + pair[2];
    }
    return -1;
}

std::string format_tour_clock(uint64_t hundredths) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu:%02llu.%02llu",
                  (unsigned long long)(hundredths / 6000),
                  (unsigned long long)((hundredths / 100) % 60),
                  (unsigned long long)(hundredths % 100));
    return buf;
}

} // namespace likethewind
