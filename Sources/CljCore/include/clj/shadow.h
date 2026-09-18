// @ai-generated(guided)
#ifndef CLJ_SHADOW_H
#define CLJ_SHADOW_H

#include <stdbool.h>
#include <stddef.h>

#include "value.h"

typedef struct clj_node clj_node;

// One per running closure on the thread; natives are leaves and never appear.
typedef struct {
	const clj_node *fn_node;
	const clj_node *call_site; // NULL for a host call or a call from a native
} clj_shadow_frame;

// Beyond this depth the innermost frames are kept and the rest counted as dropped.
#define CLJ_SHADOW_CAPACITY 8192

// Innermost first, at most cap, returns the number written. Async-signal-safe: no allocation, no locks.
size_t clj_shadow_stack_snapshot(clj_shadow_frame *out, size_t cap);
size_t clj_shadow_stack_depth(void);
size_t clj_shadow_stack_dropped(void);

// Owned vector of {:fn sym-or-nil :line n :column n}, innermost first; the fn's own position when the call site is unknown.
clj_value clj_shadow_stack_trace(size_t max);

// Interns the keywords this module otherwise makes on first use; clj_init calls it (runtime.c).
void clj_shadow_intern_keywords(void);

// Test hook: a power of two up to CLJ_SHADOW_CAPACITY, only while the stack is empty.
void clj_debug_shadow_stack_set_capacity(size_t cap);

// Opt-in: a host with its own crash reporter calls clj_shadow_stack_snapshot from that instead.
void clj_crash_handler_install(void);
// Same handler on `sig`, output to `fd`, no re-raise: for a test that must survive the signal.
void clj_crash_handler_install_test(int sig, int fd);

#endif
