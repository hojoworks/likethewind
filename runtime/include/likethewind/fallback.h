#ifndef LIKETHEWIND_FALLBACK_H
#define LIKETHEWIND_FALLBACK_H

namespace likethewind {
void fallback_init();
void fallback_step();   // one guest instruction in Musashi; uses the active CPU state
} // namespace likethewind

#endif // LIKETHEWIND_FALLBACK_H
