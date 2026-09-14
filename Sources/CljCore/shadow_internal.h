// @ai-generated(guided)
#ifndef CLJ_SHADOW_INTERNAL_H
#define CLJ_SHADOW_INTERNAL_H

#include "clj/shadow.h"

// A ring: past the capacity the oldest frame is overwritten, so the innermost ones survive.
typedef struct {
	size_t           depth;
	size_t           mask;
	clj_shadow_frame frames[CLJ_SHADOW_CAPACITY];
} clj_shadow_stack;

// Thread-local, not in a public header: Swift cannot import a _Thread_local.
extern _Thread_local clj_shadow_stack *clj_shadow_tls;

clj_shadow_stack *clj_shadow_stack_init(void);

static inline void clj_shadow_push(const clj_node *fn_node, const clj_node *call_site) {
	clj_shadow_stack *s = clj_shadow_tls;
	if (__builtin_expect(!s, 0)) s = clj_shadow_stack_init();
	s->frames[s->depth & s->mask] = (clj_shadow_frame){fn_node, call_site};
	s->depth++;
}

static inline void clj_shadow_pop(void) { clj_shadow_tls->depth--; }

#endif
