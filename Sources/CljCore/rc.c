// @ai-generated(guided)
#include <pthread.h>
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

#if CLJ_DEBUG
_Atomic uint64_t clj_debug_rc_counters[CLJ_RC_KINDS];

void clj_debug_rc_ops(int64_t out[CLJ_RC_KINDS]) {
	for (int i = 0; i < CLJ_RC_KINDS; i++) out[i] = (int64_t)atomic_load_explicit(&clj_debug_rc_counters[i], memory_order_relaxed);
}
#else
void clj_debug_rc_ops(int64_t out[CLJ_RC_KINDS]) {
	for (int i = 0; i < CLJ_RC_KINDS; i++) out[i] = -1;
}
#endif

// ---- the main thread's counts (design §4, «BRC с одним владельцем — главным потоком»)
//
// Every decision about a free is made on the rc word: main changes it only with RMWs, at the edges of an episode
// (main16 0 -> 1 and 1 -> 0), and every other thread decides from what its own fetch_sub returns.

// The main and hand-back paths stay out of line, so the common release and retain are leaves with no frame.
#define COLD __attribute__((noinline, cold))

// The one thread whose main16 counts are live: the main carrier, named by a word only it has. Not a _Thread_local:
// on Darwin its read is a call, which clang hoists above the cheaper tests (+13 % on a pool tick). An answer kept
// across a park stays right, since a pool coroutine never runs on that thread and a main one never leaves it.
static _Atomic uintptr_t brc_owner;

#if defined(__APPLE__) && defined(__x86_64__)
// The thread's TSD base: %gs:0 is its own pthread_self (libpthread, _PTHREAD_TSD_SLOT_PTHREAD_SELF).
static inline uintptr_t thread_word(void) {
	uintptr_t t;
	__asm__ volatile("movq %%gs:0, %0" : "=r"(t));
	return t;
}
#elif defined(__APPLE__) && defined(__aarch64__)
// The thread's TSD base, which libpthread keeps in TPIDRRO_EL0 (_os_tsd_get_base).
static inline uintptr_t thread_word(void) {
	uintptr_t t;
	__asm__ volatile("mrs %0, TPIDRRO_EL0" : "=r"(t));
	return t & ~(uintptr_t)7;
}
#else
static inline uintptr_t thread_word(void) { return (uintptr_t)pthread_self(); }
#endif

static inline bool any_main(void) { return atomic_load_explicit(&brc_owner, memory_order_relaxed) != 0; }
static inline bool here_main(void) { return thread_word() == atomic_load_explicit(&brc_owner, memory_order_relaxed); }
static inline bool on_main(void) { return any_main() && here_main(); }

// main16 at its top moves this much into rc.
enum { MAIN_SPILL = 0x8000 };

static inline int32_t rc_count(uint32_t word) { return (int32_t)word >> 1; }

// Under brc_mu: whether a thread owns main16 now, and the releases it has yet to take.
static pthread_mutex_t brc_mu = PTHREAD_MUTEX_INITIALIZER;
static bool            brc_owned;
static clj_header    **pending;
static size_t          npending, cpending;

void clj_sched_main_wake(void); // sched.c

COLD static void main_retain(clj_header *h) {
	uint16_t m = h->main16;
	if (m == 0) {
		// main retains from a reference it holds, counted in rc while main16 is empty, so rc cannot reach zero here.
		uint32_t old = atomic_fetch_and_explicit(&h->rc, ~CLJ_RC_MERGED, memory_order_relaxed);
		(void)old;
		CLJ_ASSERT((old & CLJ_RC_MERGED) && rc_count(old) > 0, "main retain of an unmerged object with main16 empty");
		CLJ_RC_COUNT(CLJ_RC_MAIN_EDGE_IN);
		h->main16 = 1;
		return;
	}
	if (m == UINT16_MAX) {
		atomic_fetch_add_explicit(&h->rc, MAIN_SPILL * CLJ_RC_ONE, memory_order_relaxed);
		CLJ_RC_COUNT(CLJ_RC_MAIN_SPILL);
		m -= MAIN_SPILL;
	} else {
		CLJ_RC_COUNT(CLJ_RC_MAIN_PLAIN);
	}
	h->main16 = (uint16_t)(m + 1);
}

// main16 just went 1 -> 0. A zero word is a zero sum nobody can retain from, except a registry that counts on
// retain_if_live (a type with unlink): its CAS must meet an RMW here.
static bool main_episode_end(clj_header *h) {
	if (!h->type->unlink && atomic_load_explicit(&h->rc, memory_order_acquire) == 0) {
		CLJ_RC_COUNT(CLJ_RC_MAIN_FREE_IN_PLACE);
		return true;
	}
	uint32_t old = atomic_fetch_or_explicit(&h->rc, CLJ_RC_MERGED, memory_order_acq_rel);
	CLJ_RC_COUNT(CLJ_RC_MAIN_EDGE_OUT);
	// Below zero, another thread released a reference main16 still counts: main16 cannot have been at 1.
	CLJ_ASSERT(!(old & CLJ_RC_MERGED) && rc_count(old) >= 0, "main episode end on a merged or negative rc");
	return old == 0;
}

COLD static bool main_release(clj_header *h) {
	uint16_t m = h->main16;
	if (m > 1) {
		CLJ_RC_COUNT(CLJ_RC_MAIN_PLAIN);
		h->main16 = (uint16_t)(m - 1);
		return false;
	}
	if (m == 0) {
		uint32_t old = atomic_fetch_sub_explicit(&h->rc, CLJ_RC_ONE, memory_order_acq_rel);
		CLJ_RC_COUNT(CLJ_RC_MAIN_MERGED);
		CLJ_ASSERT((old & CLJ_RC_MERGED) && rc_count(old) > 0, "main release of a freed shared object");
		return old == CLJ_RC_INIT;
	}
	h->main16 = 0;
	return main_episode_end(h);
}

// A release that took rc below zero gave up a reference main16 counts: the unit goes back to rc and the reference
// to main, for its next turn. A QUEUED bit set after the fetch_sub could land on an object another such release
// queued and main freed meanwhile. With no main carrier nobody else writes main16, and brc_mu orders this thread
// with the last owner and the next one.
COLD static bool defer_to_main(clj_header *h) {
	atomic_fetch_add_explicit(&h->rc, CLJ_RC_ONE, memory_order_relaxed);
	CLJ_RC_COUNT(CLJ_RC_DEFERRED);
	pthread_mutex_lock(&brc_mu);
	if (brc_owned) {
		if (npending == cpending) {
			cpending = cpending ? cpending * 2 : 64;
			pending = realloc(pending, cpending * sizeof *pending);
			if (!pending) clj_fatal("out of memory");
		}
		pending[npending++] = h;
		bool first = npending == 1;
		pthread_mutex_unlock(&brc_mu);
		if (first) clj_sched_main_wake();
		return false;
	}
	uint16_t m = h->main16;
	CLJ_ASSERT(m > 0, "a reference given back to main16, which holds none");
	h->main16 = (uint16_t)(m - 1);
	bool zero = m == 1 && atomic_fetch_or_explicit(&h->rc, CLJ_RC_MERGED, memory_order_acq_rel) == 0;
	pthread_mutex_unlock(&brc_mu);
	return zero;
}

static bool other_release(clj_header *h) {
	// acq_rel rather than release plus an acquire fence on zero: TSan does not model fences.
	uint32_t old = atomic_fetch_sub_explicit(&h->rc, CLJ_RC_ONE, memory_order_acq_rel);
	if (old == CLJ_RC_INIT) return true;
	if (old & CLJ_RC_MERGED) {
		CLJ_ASSERT(rc_count(old) > 0, "release of a freed shared object");
		return false;
	}
	// Unmerged: main16 holds the rest, and main frees at its episode end.
	if (rc_count(old) > 0) return false;
	return defer_to_main(h);
}

static bool release_reaches_zero(clj_header *h) {
	if (h->flags & CLJ_FLAG_IMMORTAL) return false;
	if (h->flags & CLJ_FLAG_SHARED) {
		// Merged, main16 is empty, and main releases as any thread does: the owner test is for unmerged objects only.
		if (__builtin_expect(any_main(), 0) && !(atomic_load_explicit(&h->rc, memory_order_relaxed) & CLJ_RC_MERGED) &&
		    here_main())
			return main_release(h);
#if CLJ_DEBUG
		if (on_main()) CLJ_RC_COUNT(CLJ_RC_MAIN_MERGED);
#endif
		return other_release(h);
	}
	CLJ_OWNER_CHECK(h);
	uint32_t rc = CLJ_RC_UNSHARED_LOAD(h);
	CLJ_ASSERT(rc >= CLJ_RC_INIT, "release of a freed object");
	CLJ_RC_UNSHARED_STORE(h, rc - CLJ_RC_ONE);
	return rc == CLJ_RC_INIT;
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

void clj_retain_slow(clj_header *h) {
	if (h->flags & CLJ_FLAG_IMMORTAL) return;
	if (__builtin_expect(on_main(), 0)) {
		main_retain(h);
		return;
	}
	uint32_t prev = atomic_fetch_add_explicit(&h->rc, CLJ_RC_ONE, memory_order_relaxed);
	(void)prev;
	// Unmerged, the count may be zero or below: main16 holds the object.
	CLJ_ASSERT(rc_count(prev) > 0 || !(prev & CLJ_RC_MERGED), "retain of a freed shared object");
}

void clj_release_slow(clj_header *h) {
	if (release_reaches_zero(h)) free_object(h);
}

bool clj_rc_unique(clj_header *h) {
	if (h->flags & CLJ_FLAG_IMMORTAL) return false;
	if (h->flags & CLJ_FLAG_SHARED) {
		// Relaxed is enough: we hold a reference, so an observed 1 means no one else does; merged, main16 holds none.
		uint32_t w = atomic_load_explicit(&h->rc, memory_order_relaxed);
		if (w == CLJ_RC_INIT) return true;
		if (w != 0 || !on_main()) return false;
		// Acquire: a reuse writes what other threads read up to their release.
		return h->main16 == 1 && atomic_load_explicit(&h->rc, memory_order_acquire) == 0;
	}
	CLJ_OWNER_CHECK(h);
	return CLJ_RC_UNSHARED_LOAD(h) == CLJ_RC_INIT;
}

bool clj_is_unique(clj_value v) {
#ifdef CLJ_NO_REUSE
	// The §7 invariant: nothing outside the RC entry points may depend on the counter, so a build that
	// answers "not unique" everywhere must still pass every suite — a copy, never a wrong result.
	(void)v;
	return false;
#else
	return clj_is_ptr(v) && clj_rc_unique(clj_header_of(v));
#endif
}

bool clj_retain_if_live(clj_header *h) {
	if (h->flags & CLJ_FLAG_IMMORTAL) return true;
	uint32_t w = atomic_load_explicit(&h->rc, memory_order_relaxed);
	if (!(h->flags & CLJ_FLAG_SHARED)) {
		while (rc_count(w) > 0) {
			if (atomic_compare_exchange_weak_explicit(&h->rc, &w, w + CLJ_RC_ONE, memory_order_acq_rel, memory_order_relaxed)) return true;
		}
		return false;
	}
	if (on_main()) {
		if (h->main16) {
			main_retain(h);
			return true;
		}
		while (rc_count(w) > 0) {
			if (atomic_compare_exchange_weak_explicit(&h->rc, &w, w & ~CLJ_RC_MERGED, memory_order_acq_rel, memory_order_relaxed)) {
				CLJ_RC_COUNT(CLJ_RC_MAIN_EDGE_IN);
				h->main16 = 1;
				return true;
			}
		}
		return false;
	}
	// Unmerged: main16 holds the object, or main is at its episode end, whose RMW this CAS meets (main_episode_end).
	while (rc_count(w) > 0 || !(w & CLJ_RC_MERGED)) {
		if (atomic_compare_exchange_weak_explicit(&h->rc, &w, w + CLJ_RC_ONE, memory_order_acq_rel, memory_order_relaxed)) return true;
	}
	return false;
}

void clj_rc_main_adopt(void) {
	if (on_main()) return;
	pthread_mutex_lock(&brc_mu);
	if (brc_owned) clj_fatal("a second main carrier while the first one holds main16");
	brc_owned = true;
	atomic_store_explicit(&brc_owner, thread_word(), memory_order_relaxed);
	pthread_mutex_unlock(&brc_mu);
}

void clj_rc_main_drain(void) {
	if (!on_main()) clj_fatal("clj_rc_main_drain off the main carrier");
	for (;;) {
		pthread_mutex_lock(&brc_mu);
		clj_header **batch = pending;
		size_t       n = npending;
		pending = NULL;
		npending = cpending = 0;
		pthread_mutex_unlock(&brc_mu);
		if (!n) return;
		for (size_t i = 0; i < n; i++) {
			if (main_release(batch[i])) free_object(batch[i]);
		}
		free(batch);
	}
}

void clj_rc_main_abandon(void) {
	if (!on_main()) return;
	for (;;) {
		clj_rc_main_drain();
		pthread_mutex_lock(&brc_mu);
		if (!npending) {
			brc_owned = false;
			atomic_store_explicit(&brc_owner, 0, memory_order_relaxed);
			pthread_mutex_unlock(&brc_mu);
			return;
		}
		pthread_mutex_unlock(&brc_mu);
	}
}

size_t clj_debug_rc_pending(void) {
	pthread_mutex_lock(&brc_mu);
	size_t n = npending;
	pthread_mutex_unlock(&brc_mu);
	return n;
}

int64_t clj_debug_rc_count(clj_value v) {
	if (!clj_is_ptr(v)) return 0;
	clj_header *h = clj_header_of(v);
	int64_t     n = rc_count(atomic_load_explicit(&h->rc, memory_order_relaxed));
	if ((h->flags & CLJ_FLAG_SHARED) && on_main()) n += h->main16;
	return n;
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

// A count of 1 is the creator's own reference: a store while it fills the object needs no lock yet.
void clj_debug_slot_store_check(const clj_header *owner, clj_value v) {
	clj_debug_slot_check(owner, v);
	if (!owner->type->debug_lock_held || clj_debug_rc_count(clj_from_ptr((void *)owner)) <= 1) return;
	if (owner->type->debug_lock_held(owner)) return;
	char msg[256];
	snprintf(msg, sizeof msg, "store into a slot of %s without its lock (design §4, «Запись в слот»)", owner->type->name);
	clj_fatal(msg);
}
#else
void clj_debug_share_check_every(uint32_t n) { (void)n; }
void clj_debug_slot_store_check(const clj_header *owner, clj_value v) {
	(void)owner;
	(void)v;
}
#define assert_shared_below(v) ((void)0)
#endif

void clj_mark_shared(clj_header *h) {
	if (on_main()) {
		int32_t  c = rc_count(CLJ_RC_UNSHARED_LOAD(h));
		uint16_t m = c <= UINT16_MAX ? (uint16_t)c : MAIN_SPILL;
		h->main16 = m;
		CLJ_RC_UNSHARED_STORE(h, (uint32_t)(c - m) * CLJ_RC_ONE);
	} else {
		h->main16 = 0;
	}
	h->flags |= CLJ_FLAG_SHARED;
}

void clj_mark_shared_merged(clj_header *h) {
	h->main16 = 0;
	h->flags |= CLJ_FLAG_SHARED;
}

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
		clj_mark_shared(h);
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
	uint32_t owner = h->main16, here = clj_debug_owner_here();
	if (owner == here) return;
	char msg[256];
	snprintf(msg, sizeof msg, "unshared %s of execution %u touched by execution %u (NOTES \"RC\", owner check)",
	         h->type->name, owner, here);
	clj_fatal(msg);
}
#endif
