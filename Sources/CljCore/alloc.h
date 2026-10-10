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

// Whether an object of size bytes fits the cell: the same pool class. A system-allocated cell has no recorded size.
bool clj_cell_fits(const clj_header *h, size_t size);
// The cell of an object that died at rc 1, its children gone or kept by the caller, as a new object of type: the
// death and the birth at one address for the live counts and the census, no allocation counted. poison: the body is
// the constructor's to write in full (debug builds).
void clj_cell_reborn(clj_header *h, const clj_type *type, size_t size, bool poison);

#if CLJ_DEBUG
// The running execution's owner tag, 0 on a thread that has none yet (coro.c).
uint32_t clj_debug_owner_here(void);
#endif

#endif
