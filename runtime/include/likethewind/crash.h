#ifndef LIKETHEWIND_CRASH_H
#define LIKETHEWIND_CRASH_H

namespace likethewind {

// Install the process-wide crash trap: unhandled exceptions, std::terminate
// and fatal signals write a report next to the executable (crash.log and a
// timestamped copy) with the host stack and the emulated CPU state.
void install_crash_handler();

// Call once per emulated frame.  The watchdog uses this to distinguish a
// normal long run from a hang and writes a diagnostic dump if frames stop.
void crash_heartbeat();

// Append a line to the small ring buffer included in the next crash report.
// Useful for recording mode changes and tour transitions.
void crash_note(const char *fmt, ...);

} // namespace likethewind

#endif
