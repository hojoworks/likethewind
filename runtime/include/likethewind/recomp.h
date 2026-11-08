/* What generated code and the dispatcher need from the runtime. */
#ifndef LIKETHEWIND_RECOMP_H
#define LIKETHEWIND_RECOMP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ng_func_t)(void);
typedef struct {
    uint32_t addr;
    ng_func_t fn;
} ng_func_entry_t;

#ifdef __cplusplus
}
#endif

#endif
