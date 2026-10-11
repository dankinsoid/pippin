// @ai-generated(solo)
#ifndef CLJ_ICE_INTERNAL_H
#define CLJ_ICE_INTERNAL_H

#include <stdatomic.h>

#include "clj/ice.h"

extern _Atomic uint32_t clj_no_opt_bits;

// One relaxed load: clj_is_unique reads it on every in-place candidate.
static inline bool clj_opt_off(uint32_t bit) { return (atomic_load_explicit(&clj_no_opt_bits, memory_order_relaxed) & bit) != 0; }

// clj_init's first step: CLJ_NO_OPT into the bits, and the bits into the switches they own.
void clj_no_opt_configure(void);

#endif
