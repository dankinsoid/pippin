// @ai-generated(guided)
#ifndef CLJ_SHADOW_INTERNAL_H
#define CLJ_SHADOW_INTERNAL_H

#include <stdatomic.h>
#include <stdint.h>

#include "clj/shadow.h"
#include "trace_internal.h"

#define CLJ_TRACE_MAX 256

// A ring, one per execution: past the capacity the oldest frame is overwritten, so the innermost ones survive.
typedef struct {
	size_t           depth;
	size_t           mask;
	char            *stack_limit; // lowest C stack address the interpreter may still use; eval.c fills it on the first call
	char            *stack_lo;    // the stack bounds, for walking the real stack (trace.c, guard.c)
	char            *stack_hi;
	_Atomic uint64_t deadline;    // monotonic ns the running code must not pass, 0 when none (eval.c); 1 once cancelled
	uint32_t         countdown;   // calls and loop turns left before the next clock read
	uint32_t         unwinds;     // unwind budgets a caught timeout may still spend before every check throws
	_Atomic bool     cancelled;   // cancel! on the coroutine: the deadline throw becomes the cancellation
	void            *recovery;    // innermost clj_recovery of the execution, NULL outside the host boundary (guard.c)
	size_t           noverflow;   // the frames the guard handler collected before landing (guard.c)
	clj_trace_frame  overflow[CLJ_TRACE_MAX];
	clj_shadow_frame frames[CLJ_SHADOW_CAPACITY];
} clj_shadow_stack;

// The running execution's ring, mirrored from the coroutine at every switch; not for Swift (_Thread_local).
extern _Thread_local clj_shadow_stack *clj_shadow_tls;

// Makes the thread's implicit coroutine when there is none; returns the running execution's ring.
clj_shadow_stack *clj_shadow_stack_init(void);
// The ring of the calling thread's running execution through the pthread key: async-signal-safe, NULL where none.
const clj_shadow_stack *clj_shadow_stack_current(void);
// Sets the bounds of a ring to the calling thread's stack (shadow.c).
void clj_shadow_stack_bounds(clj_shadow_stack *s);

static inline void clj_shadow_push(const clj_node *fn_node, const clj_node *call_site, const void *sp) {
	clj_shadow_stack *s = clj_shadow_tls;
	if (__builtin_expect(!s, 0)) s = clj_shadow_stack_init();
	s->frames[s->depth & s->mask] = (clj_shadow_frame){fn_node, call_site, sp};
	s->depth++;
}

// The depth left; 0 means no closure runs on the thread.
static inline size_t clj_shadow_pop(void) { return --clj_shadow_tls->depth; }

static inline uint64_t clj_shadow_deadline(const clj_shadow_stack *s) { return atomic_load_explicit(&s->deadline, memory_order_relaxed); }

// Called with the deadline set: true when the deadline throw is now pending (eval.c).
bool clj_eval_deadline_hit(void *shadow_stack);

// The deadline check of a loop turn and of a seq driver's step: true when the timeout throw is pending. On a ring
// the caller captured before its loop: the pointer stays the execution's own across a park, a TLS read would not.
static inline bool clj_deadline_tick_on(clj_shadow_stack *s) {
	return __builtin_expect(clj_shadow_deadline(s) != 0, 0) && clj_eval_deadline_hit(s);
}
// The same through a fresh TLS read (a call: eval.c).
bool clj_deadline_tick(void);

#endif
