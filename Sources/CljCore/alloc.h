// @ai-generated(solo)
#ifndef CLJ_ALLOC_H
#define CLJ_ALLOC_H

#include "clj/object.h"

// Whether h is unique as the calling thread can see it (clj_is_unique without CLJ_NO_REUSE).
bool clj_rc_unique(clj_header *h);

// Returns the object's memory; the caller has already released its children.
void clj_dealloc(clj_header *h);

#if CLJ_DEBUG
// The running execution's owner tag, 0 on a thread that has none yet (coro.c).
uint32_t clj_debug_owner_here(void);
#endif

#endif
