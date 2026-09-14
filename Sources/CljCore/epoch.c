// @ai-generated(guided)
#include "epoch_internal.h"

_Atomic uint64_t clj_epoch_counter;

uint64_t clj_epoch(void) { return clj_epoch_load(); }

void clj_epoch_bump(void) { atomic_fetch_add_explicit(&clj_epoch_counter, 1, memory_order_seq_cst); }
