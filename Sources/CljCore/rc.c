// @ai-generated(guided)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clj/object.h"

const clj_type clj_type_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "type",
};

#if CLJ_DEBUG
// One process-wide counter, contended across threads; debug-only, so acceptable until profiles say otherwise.
static _Atomic int64_t live_objects;
int64_t clj_debug_live_objects(void) { return atomic_load(&live_objects); }
#define LIVE_ADD(n) atomic_fetch_add_explicit(&live_objects, (n), memory_order_relaxed)
#else
int64_t clj_debug_live_objects(void) { return -1; }
#define LIVE_ADD(n) ((void)0)
#endif

void clj_fatal(const char *msg) {
	fprintf(stderr, "clj: fatal: %s\n", msg);
	abort();
}

// calloc stands in for the size-class pool (design §4); callers depend only on this signature.
void *clj_alloc(const clj_type *type, size_t size) {
	clj_header *h = calloc(1, size);
	if (!h) clj_fatal("out of memory");
	atomic_init(&h->rc, 1);
	h->type = type;
	LIVE_ADD(1);
	return h;
}

void *clj_realloc(void *obj, size_t size) {
	clj_header *h = obj;
	CLJ_ASSERT(!(h->flags & CLJ_FLAG_IMMORTAL) && atomic_load_explicit(&h->rc, memory_order_relaxed) == 1,
	           "realloc of a non-unique object");
	h = realloc(h, size);
	if (!h) clj_fatal("out of memory");
	return h;
}

static void dealloc(clj_header *h) {
	LIVE_ADD(-1);
	free(h);
}

static bool release_reaches_zero(clj_header *h) {
	if (h->flags & CLJ_FLAG_IMMORTAL) return false;
	if (h->flags & CLJ_FLAG_SHARED) {
		uint32_t prev = atomic_fetch_sub_explicit(&h->rc, 1, memory_order_release);
		CLJ_ASSERT(prev > 0, "release of a freed shared object");
		if (prev != 1) return false;
		atomic_thread_fence(memory_order_acquire);
		return true;
	}
	uint32_t rc = atomic_load_explicit(&h->rc, memory_order_relaxed);
	CLJ_ASSERT(rc > 0, "release of a freed object");
	atomic_store_explicit(&h->rc, rc - 1, memory_order_relaxed);
	return rc == 1;
}

// A dead object's rc+flags become the intrusive worklist link.
// memcpy rather than a cast to stay clear of aliasing rules; it compiles to a plain store.
static void set_dead_next(clj_header *h, clj_header *next) {
	uintptr_t link = (uintptr_t)next;
	memcpy((void *)h, &link, sizeof link);
}

static clj_header *get_dead_next(clj_header *h) {
	uintptr_t link;
	memcpy(&link, (void *)h, sizeof link);
	return (clj_header *)link;
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
		dealloc(h);
	}
}

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
	if (!clj_is_ptr(v)) return false;
	clj_header *h = clj_header_of(v);
	if (h->flags & CLJ_FLAG_IMMORTAL) return false;
	// Relaxed is enough: we hold a reference, so an observed 1 means no one else does.
	return atomic_load_explicit(&h->rc, memory_order_relaxed) == 1;
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
