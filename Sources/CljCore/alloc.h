// @ai-generated(solo)
#ifndef CLJ_ALLOC_H
#define CLJ_ALLOC_H

#include "clj/object.h"

// Returns the object's memory; the caller has already released its children.
void clj_dealloc(clj_header *h);
// clj_dealloc in two steps, for a dead object whose cell a candidate buffer still names (cc.c, the zombie): the
// object leaves the live count when it dies, its cell when the buffer lets go.
void clj_dealloc_dead(clj_header *h);
void clj_dealloc_cell(clj_header *h);

#if CLJ_DEBUG
// The running execution's owner tag, 0 on a thread that has none yet (coro.c).
uint32_t clj_debug_owner_here(void);
#endif

#endif
