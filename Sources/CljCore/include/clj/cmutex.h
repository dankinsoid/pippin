// @ai-generated(solo)
#ifndef CLJ_CMUTEX_H
#define CLJ_CMUTEX_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "value.h"

// The mutex user code holds (design §4, "Два лока"): a CAS uncontended, a park contended, legal across a park.
typedef struct {
	_Atomic uint32_t state;
} clj_cmutex;

#define CLJ_CMUTEX_INIT {0}

static inline void clj_cmutex_init(clj_cmutex *m) { atomic_init(&m->state, 0); }

void clj_cmutex_lock_slow(clj_cmutex *m);
void clj_cmutex_unlock_slow(clj_cmutex *m);

static inline void clj_cmutex_lock(clj_cmutex *m) {
	uint32_t expected = 0;
	if (__builtin_expect(atomic_compare_exchange_strong_explicit(&m->state, &expected, 1, memory_order_acquire, memory_order_relaxed), 1)) return;
	clj_cmutex_lock_slow(m);
}

static inline bool clj_cmutex_trylock(clj_cmutex *m) {
	uint32_t expected = 0;
	return atomic_compare_exchange_strong_explicit(&m->state, &expected, 1, memory_order_acquire, memory_order_relaxed);
}

static inline void clj_cmutex_unlock(clj_cmutex *m) {
	uint32_t expected = 1;
	if (__builtin_expect(atomic_compare_exchange_strong_explicit(&m->state, &expected, 0, memory_order_release, memory_order_relaxed), 1)) return;
	clj_cmutex_unlock_slow(m);
}

// A one-shot wait on any address; wait_if runs under the bucket's lock and false returns at once.
void clj_lot_park(const void *key, bool (*wait_if)(const void *key, void *ctx), void *ctx);
// Resumes every waiter parked on key.
void clj_lot_unpark_all(const void *key);

// (locking x ...): a reentrant monitor per object, looked up by identity; monitor-exit* by a non-owner throws.
clj_value clj_monitor_enter(clj_value x);
clj_value clj_monitor_exit(clj_value x);
size_t    clj_debug_live_monitors(void);

#endif
