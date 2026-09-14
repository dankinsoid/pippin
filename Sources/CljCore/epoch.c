// @ai-generated(guided)
#include <stdatomic.h>

#include "clj/epoch.h"

static _Atomic uint64_t epoch;

uint64_t clj_epoch(void) { return atomic_load_explicit(&epoch, memory_order_acquire); }

void clj_epoch_bump(void) { atomic_fetch_add_explicit(&epoch, 1, memory_order_release); }
