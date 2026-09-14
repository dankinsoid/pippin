// @ai-generated(guided)
#ifndef CLJ_EPOCH_INTERNAL_H
#define CLJ_EPOCH_INTERNAL_H

#include <stdatomic.h>

#include "clj/epoch.h"

// The counter behind clj_epoch, for the call path: an inline read where a call would be pure overhead.
// Not in a public header: Swift cannot import an _Atomic.
extern _Atomic uint64_t clj_epoch_counter;

static inline uint64_t clj_epoch_load(void) { return atomic_load_explicit(&clj_epoch_counter, memory_order_seq_cst); }

#endif
