// @ai-generated(guided)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

void clj_fatal(const char *msg) {
	fprintf(stderr, "clj: fatal: %s\n", msg);
	abort();
}

static bool release_reaches_zero(clj_header *h) {
	if (h->flags & CLJ_FLAG_IMMORTAL) return false;
	if (h->flags & CLJ_FLAG_SHARED) {
		// acq_rel rather than release plus an acquire fence on zero: TSan does not model fences.
		uint32_t prev = atomic_fetch_sub_explicit(&h->rc, 1, memory_order_acq_rel);
		CLJ_ASSERT(prev > 0, "release of a freed shared object");
		return prev == 1;
	}
	uint32_t rc = atomic_load_explicit(&h->rc, memory_order_relaxed);
	CLJ_ASSERT(rc > 0, "release of a freed object");
	atomic_store_explicit(&h->rc, rc - 1, memory_order_relaxed);
	return rc == 1;
}

// A dead object's rc+flags become the intrusive worklist link; bit 0 keeps CLJ_FLAG_LARGE for dealloc,
// bits 1 and 2 CLJ_FLAG_META and CLJ_FLAG_SHAPE, which each_child reads (objects are at least 8-byte aligned).
// memcpy rather than a cast to stay clear of aliasing rules; it compiles to a plain store.
static void set_dead_next(clj_header *h, clj_header *next) {
	uintptr_t link = (uintptr_t)next | ((h->flags & CLJ_FLAG_LARGE) ? 1 : 0) | ((h->flags & CLJ_FLAG_META) ? 2 : 0) |
	                 ((h->flags & CLJ_FLAG_SHAPE) ? 4 : 0);
	memcpy((void *)h, &link, sizeof link);
}

static clj_header *get_dead_next(clj_header *h) {
	uintptr_t link;
	memcpy(&link, (void *)h, sizeof link);
	h->flags = ((link & 1) ? CLJ_FLAG_LARGE : 0) | ((link & 2) ? CLJ_FLAG_META : 0) | ((link & 4) ? CLJ_FLAG_SHAPE : 0);
	return (clj_header *)(link & ~(uintptr_t)7);
}

static void release_child(clj_value child, void *ctx) {
	if (!clj_is_ptr(child)) return;
	clj_header **stack = ctx;
	clj_header *h = clj_header_of(child);
	if (release_reaches_zero(h)) {
		set_dead_next(h, *stack);
		*stack = h;
	}
}

// Iterative so a million-element list does not overflow the C stack.
static void free_object(clj_header *dead) {
	clj_header *stack = dead;
	set_dead_next(dead, NULL);
	while (stack) {
		clj_header *h = stack;
		stack = get_dead_next(h);
		const clj_type *t = h->type;
		if (t->each_child) t->each_child(h, release_child, &stack);
		if (t->finalize) t->finalize(h);
		clj_dealloc(h);
	}
}

#if CLJ_DEBUG
_Atomic uint64_t clj_debug_rc_counters[3];

void clj_debug_rc_ops(int64_t out[3]) {
	for (int i = 0; i < 3; i++) out[i] = (int64_t)atomic_load_explicit(&clj_debug_rc_counters[i], memory_order_relaxed);
}
#else
void clj_debug_rc_ops(int64_t out[3]) {
	for (int i = 0; i < 3; i++) out[i] = -1;
}
#endif

void clj_retain_slow(clj_header *h) {
	if (h->flags & CLJ_FLAG_IMMORTAL) return;
	uint32_t prev = atomic_fetch_add_explicit(&h->rc, 1, memory_order_relaxed);
	(void)prev;
	CLJ_ASSERT(prev > 0, "retain of a freed shared object");
}

void clj_release_slow(clj_header *h) {
	if (release_reaches_zero(h)) free_object(h);
}

bool clj_is_unique(clj_value v) {
#ifdef CLJ_NO_REUSE
	// The §7 invariant: nothing outside the RC entry points may depend on the counter, so a build that
	// answers "not unique" everywhere must still pass every suite — a copy, never a wrong result.
	(void)v;
	return false;
#else
	if (!clj_is_ptr(v)) return false;
	clj_header *h = clj_header_of(v);
	if (h->flags & CLJ_FLAG_IMMORTAL) return false;
	// Relaxed is enough: we hold a reference, so an observed 1 means no one else does.
	return atomic_load_explicit(&h->rc, memory_order_relaxed) == 1;
#endif
}

bool clj_reuse_enabled(void) {
#ifdef CLJ_NO_REUSE
	return false;
#else
	return true;
#endif
}

bool clj_is_shared(clj_value v) {
	return clj_is_ptr(v) && (clj_header_of(v)->flags & CLJ_FLAG_SHARED) != 0;
}

typedef struct {
	clj_value *items;
	size_t     count, cap;
} value_stack;

static void stack_push(value_stack *s, clj_value v) {
	if (s->count == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 64;
		s->items = realloc(s->items, s->cap * sizeof *s->items);
		if (!s->items) clj_fatal("out of memory");
	}
	s->items[s->count++] = v;
}

static void share_visit(clj_value child, void *ctx) {
	if (clj_is_ptr(child)) stack_push(ctx, child);
}

void clj_share(clj_value v) {
	if (!clj_is_ptr(v)) return;
	if (clj_header_of(v)->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL)) return;
	value_stack st = {0};
	stack_push(&st, v);
	while (st.count) {
		clj_header *h = clj_header_of(st.items[--st.count]);
		if (h->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL)) continue;
		h->flags |= CLJ_FLAG_SHARED;
		if (h->type->each_child) h->type->each_child(h, share_visit, &st);
	}
	free(st.items);
}

bool clj_debug_all_shared(clj_value v) {
	if (!clj_is_ptr(v)) return true;
	value_stack st = {0};
	stack_push(&st, v);
	bool ok = true;
	while (ok && st.count) {
		clj_header *h = clj_header_of(st.items[--st.count]);
		if (h->flags & CLJ_FLAG_IMMORTAL) continue;
		ok = (h->flags & CLJ_FLAG_SHARED) != 0;
		if (ok && h->type->each_child) h->type->each_child(h, share_visit, &st);
	}
	free(st.items);
	return ok;
}
