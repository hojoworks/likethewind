#ifndef LIKETHEWIND_FALLBACK_H
#define LIKETHEWIND_FALLBACK_H

namespace likethewind {
void fallback_init();
bool fallback_step();   // one guest instruction in Musashi; uses the active CPU state
}

#endif
