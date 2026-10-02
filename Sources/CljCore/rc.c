// @ai-generated(guided)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "alloc.h"

// CLJ_CRASH_EXIT: a plain exit, since a wedged crash reporter can leave the aborting process unkillable (NOTES "Guard").
void clj_fatal(const char *msg) {
	fprintf(stderr, "clj: fatal: %s\n", msg);
	fflush(stderr);
	const char *e = getenv("CLJ_CRASH_EXIT");
	if (e && *e && strcmp(e, "0") != 0) _exit(134);
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
	CLJ_OWNER_CHECK(h);
	uint32_t rc = atomic_load_explicit(&h->rc, memory_order_relaxed);
	CLJ_ASSERT(rc > 0, "release of a freed object");
	atomic_store_explicit(&h->rc, rc - 1, memory_order_relaxed);
	return rc == 1;
}

#if CLJ_DEBUG
static void fatal_unshared_child(const char *where, const clj_header *parent, clj_value child) {
	char msg[256];
	snprintf(msg, sizeof msg, "%s: shared %s over unshared %s", where, parent ? parent->type->name : "object",
	         clj_type_of(child)->name);
	clj_fatal(msg);
}

static void assert_child_shared(clj_value child, void *ctx) {
	if (clj_is_ptr(child) && !(clj_header_of(child)->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL)))
		fatal_unshared_child("free", ctx, child);
}

// The header is still whole here; set_dead_next overwrites the flags right after. Mutable slots are safe to read:
// at a count of zero no writer holds the object.
static void assert_children_shared(clj_header *h) {
	if ((h->flags & CLJ_FLAG_SHARED) && h->type->each_child) h->type->each_child(h, assert_child_shared, h);
}
#else
#define assert_children_shared(h) ((void)0)
#endif

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
		assert_children_shared(h);
		if (h->type->unlink) h->type->unlink(h);
		set_dead_next(h, *stack);
		*stack = h;
	}
}

// Iterative so a million-element list does not overflow the C stack.
static void free_object(clj_header *dead) {
	clj_header *stack = dead;
	assert_children_shared(dead);
	if (dead->type->unlink) dead->type->unlink(dead);
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
	if (!(h->flags & CLJ_FLAG_SHARED)) CLJ_OWNER_CHECK(h);
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

enum { STACK_INLINE = 32 };

// The share walk's pending children: a closure and its captures fit inline, no malloc per spawn.
typedef struct {
	clj_value *items;
	size_t     count, cap;
	clj_value  inline_items[STACK_INLINE];
} value_stack;

#define VALUE_STACK_INIT(name) value_stack name = {.items = name.inline_items, .cap = STACK_INLINE}

static void stack_push(value_stack *s, clj_value v) {
	if (s->count == s->cap) {
		bool heap = s->items != s->inline_items;
		s->cap *= 2;
		clj_value *grown = realloc(heap ? s->items : NULL, s->cap * sizeof *s->items);
		if (!grown) clj_fatal("out of memory");
		if (!heap) memcpy(grown, s->inline_items, s->count * sizeof *s->items);
		s->items = grown;
	}
	s->items[s->count++] = v;
}

static void stack_free(value_stack *s) {
	if (s->items != s->inline_items) free(s->items);
}

static void share_visit(clj_value child, void *ctx) {
	if (clj_is_ptr(child)) stack_push(ctx, child);
}

// A walk below a shared node: at most `reads` children's flags are read, `room` shared ones descended into.
typedef struct {
	value_stack       st;
	size_t            reads, room;
	bool              into_mutable;
	const clj_header *parent;     // whose children are being visited
	const clj_header *bad_parent; // the edge that breaks the invariant, when bad_child is set
	clj_value         bad_child;
} below_walk;

static void below_visit(clj_value child, void *ctx) {
	below_walk *w = ctx;
	if (!clj_is_ptr(child) || !clj_is_nil(w->bad_child) || !w->reads) return;
	w->reads--;
	uint32_t flags = clj_header_of(child)->flags;
	if (flags & CLJ_FLAG_IMMORTAL) return;
	if (!(flags & CLJ_FLAG_SHARED)) {
		w->bad_parent = w->parent;
		w->bad_child = child;
	} else if (w->room) {
		w->room--;
		stack_push(&w->st, child);
	}
}

// v is shared. Other threads replace and release a mutable_children node's slots: only into_mutable reads them.
static bool shared_below(clj_value v, size_t reads, size_t room, bool into_mutable, below_walk *w) {
	*w = (below_walk){.st = {.cap = STACK_INLINE}, .reads = reads, .room = room, .into_mutable = into_mutable, .bad_child = CLJ_NIL};
	w->st.items = w->st.inline_items;
	stack_push(&w->st, v);
	while (w->st.count && clj_is_nil(w->bad_child) && w->reads) {
		clj_header *h = clj_header_of(w->st.items[--w->st.count]);
		if (h->type->mutable_children && !w->into_mutable) continue;
		w->parent = h;
		if (h->type->each_child) h->type->each_child(h, below_visit, w);
	}
	stack_free(&w->st);
	return clj_is_nil(w->bad_child);
}

#if CLJ_DEBUG
// A check pays the width of every node it visits, and each_child cannot stop early: a spawn capturing a channel
// with 100k buffered values re-read them all, ChanStressTests.stressSpawn 0.3 -> 40 s, and 1.6 s at one cutoff in
// 64. So one in 256 per thread, 256 children read and 64 descended into at most; the check at free sees every edge.
enum { SHARE_CUTOFF_READS = 256, SHARE_CUTOFF_ROOM = 64, SHARE_CUTOFF_EVERY = 256 };

static _Thread_local uint32_t cutoff_every = SHARE_CUTOFF_EVERY;

void clj_debug_share_check_every(uint32_t n) { cutoff_every = n ? n : SHARE_CUTOFF_EVERY; }

static void assert_shared_below(clj_value v) {
	static _Thread_local uint32_t cutoffs;
	if (cutoffs++ % cutoff_every) return;
	below_walk w;
	if (!shared_below(v, SHARE_CUTOFF_READS, SHARE_CUTOFF_ROOM, false, &w)) fatal_unshared_child("share cutoff", w.bad_parent, w.bad_child);
}

void clj_debug_slot_check(const clj_header *owner, clj_value v) {
	if ((owner->flags & CLJ_FLAG_SHARED) && clj_is_ptr(v) && !(clj_header_of(v)->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL)))
		fatal_unshared_child("store", owner, v);
}
#else
void clj_debug_share_check_every(uint32_t n) { (void)n; }
#define assert_shared_below(v) ((void)0)
#endif

void clj_share(clj_value v) {
	if (!clj_is_ptr(v)) return;
	uint32_t flags = clj_header_of(v)->flags;
	if (flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL)) {
		if (!(flags & CLJ_FLAG_IMMORTAL)) assert_shared_below(v);
		return;
	}
	VALUE_STACK_INIT(st);
	stack_push(&st, v);
	while (st.count) {
		clj_value   cur = st.items[--st.count];
		clj_header *h = clj_header_of(cur);
		if (h->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL)) {
			if (!(h->flags & CLJ_FLAG_IMMORTAL)) assert_shared_below(cur);
			continue;
		}
		// The flag is a plain write: only the owner may publish.
		CLJ_OWNER_CHECK(h);
		h->flags |= CLJ_FLAG_SHARED;
		if (h->type->each_child) h->type->each_child(h, share_visit, &st);
	}
	stack_free(&st);
}

bool clj_debug_all_shared(clj_value v) {
	if (!clj_is_ptr(v) || (clj_header_of(v)->flags & CLJ_FLAG_IMMORTAL)) return true;
	if (!(clj_header_of(v)->flags & CLJ_FLAG_SHARED)) return false;
	below_walk w;
	return shared_below(v, SIZE_MAX, SIZE_MAX, true, &w);
}

#if CLJ_DEBUG
void clj_debug_owner_check(const clj_header *h) {
	uint32_t owner = h->flags >> CLJ_OWNER_SHIFT, here = clj_debug_owner_here();
	if (owner == here) return;
	char msg[256];
	snprintf(msg, sizeof msg, "unshared %s of execution %u touched by execution %u (NOTES \"RC\", owner check)",
	         h->type->name, owner, here);
	clj_fatal(msg);
}
#endif
