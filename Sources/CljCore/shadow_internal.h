// @ai-generated(guided)
#ifndef CLJ_SHADOW_INTERNAL_H
#define CLJ_SHADOW_INTERNAL_H

#include <stdint.h>

#include "clj/shadow.h"

// A ring: past the capacity the oldest frame is overwritten, so the innermost ones survive.
typedef struct {
	size_t           depth;
	size_t           mask;
	char            *stack_limit; // lowest C stack address the interpreter may still use; eval.c fills it on the first call
	char            *stack_lo;    // the thread's stack bounds, for walking the real stack (trace.c, guard.c)
	char            *stack_hi;
	uint64_t         deadline;    // monotonic ns the running code must not pass, 0 when none (eval.c)
	uint32_t         countdown;   // calls and loop turns left before the next clock read
	uint32_t         unwinds;     // unwind budgets a caught timeout may still spend before every check throws
	void            *altstack;    // the alternate signal stack of the thread (guard.c)
	void            *recovery;    // innermost clj_recovery of the thread, NULL outside the host boundary (guard.c)
	size_t           noverflow;   // the frames the guard handler collected before landing (guard.c)
	void            *overflow;    // clj_trace_frame[CLJ_TRACE_MAX]
	clj_shadow_frame frames[CLJ_SHADOW_CAPACITY];
} clj_shadow_stack;

#define CLJ_TRACE_MAX 256

// Thread-local, not in a public header: Swift cannot import a _Thread_local.
extern _Thread_local clj_shadow_stack *clj_shadow_tls;

clj_shadow_stack *clj_shadow_stack_init(void);
// The stack of the calling thread through the pthread key: async-signal-safe, NULL where none was made.
const clj_shadow_stack *clj_shadow_stack_current(void);

static inline void clj_shadow_push(const clj_node *fn_node, const clj_node *call_site, const void *sp) {
	clj_shadow_stack *s = clj_shadow_tls;
	if (__builtin_expect(!s, 0)) s = clj_shadow_stack_init();
	s->frames[s->depth & s->mask] = (clj_shadow_frame){fn_node, call_site, sp};
	s->depth++;
}

// The depth left; 0 means no closure runs on the thread.
static inline size_t clj_shadow_pop(void) { return --clj_shadow_tls->depth; }

// Called with the deadline set: true when the deadline throw is now pending (eval.c).
bool clj_eval_deadline_hit(void *shadow_stack);

// The deadline check of a loop turn and of a seq driver's step: true when the timeout throw is pending.
static inline bool clj_deadline_tick(void) {
	clj_shadow_stack *s = clj_shadow_tls;
	return s && __builtin_expect(s->deadline != 0, 0) && clj_eval_deadline_hit(s);
}

#endif
