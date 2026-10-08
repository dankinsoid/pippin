// @ai-generated(solo)
// The forcing claim a lazy seq and a lazy def share (seq.c): one park kind for waiting on another forcer.
#ifndef CLJ_FORCE_INTERNAL_H
#define CLJ_FORCE_INTERNAL_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "clj/value.h"

// WAITED: someone parked in the lot (cmutex.c) and the publisher unparks. FAILED is a lazy def's alone.
enum { CLJ_FORCE_UNFORCED = 0, CLJ_FORCE_FORCING = 1, CLJ_FORCE_FORCED = 2, CLJ_FORCE_WAITED = 3, CLJ_FORCE_FAILED = 4 };

typedef struct clj_forcing {
	clj_value           obj;
	struct clj_forcing *prev;
} clj_forcing;

// This execution is forcing obj: a frame of its stack names it.
bool clj_force_here(clj_value obj);
// One wait for obj's forcer: back once *state has left FORCING and WAITED; the caller reloads and decides again.
void clj_force_wait(clj_value obj, _Atomic uint32_t *state);
// Publishes st and wakes whoever parked on obj.
void clj_force_set(clj_value obj, _Atomic uint32_t *state, uint32_t st);
// A claim held by this execution: readers park on it, so no suspend! while it is (sched.c, forcing_held).
void clj_force_claimed(void);
void clj_force_unclaimed(void);
void clj_force_push(clj_forcing *f, clj_value obj);
void clj_force_pop(const clj_forcing *f);

#endif
