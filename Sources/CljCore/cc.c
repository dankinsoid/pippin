// @ai-generated(solo)
// Trial deletion over the candidates RC leaves: design §7, «Сборщик циклов: как он устроен».
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

#include "alloc.h"
#include "cc_internal.h"
#include "clj/coro.h"
#include "clj/compiled.h"
#include "clj/eval.h"
#include "clj/ice.h"
#include "clj/map.h"
#include "clj/seq.h"
#include "profile_internal.h"
#include "clj/proto.h"
#include "coro_internal.h"
#include "ice_internal.h"

enum {
	SHARED_THRESHOLD = 1024,
	SHARED_DELAY_MS = 500,
	LOCAL_THRESHOLD = 256,
	MAIN_BOUND = 4096,
	MAIN_IDLE_SLICE = 64,
	COLLECT_ROUNDS = 16,
	// A deep collection waits DEEP_DUTY times its own length: a reload loop gets an eighth of a utility-QoS core.
	DEEP_DUTY = 7,
	DEEP_MAX_NODES = 1 << 20,
	DEEP_RETRY_MAX_SHIFT = 6,
	// Past it the checked table starts over: entries of dead objects pile up there, and a reset costs one walk.
	CHECKED_MAX = 1 << 17,
	// Direct-mapped by address beside the table: a release of a marked node is answered without a probe of it.
	CHECKED_RECENT = 1024,
	AUDIT_EVERY = 16,
	AUDIT_NODES = 1024,
	// The unshared objects a parked coroutine's walk enters, under the coroutine's lock; a power of two.
	PRIVATE_MAX_NODES = 256,
};
static const uint64_t MAIN_IDLE_BUDGET_NS = 1000000;
static const uint64_t DEEP_RETRY_BASE_NS = 1000000000;

typedef struct {
	clj_header **items;
	size_t       n, cap;
} cand_vec;

static void vec_push(cand_vec *v, clj_header *h) {
	if (v->n == v->cap) {
		v->cap = v->cap ? v->cap * 2 : 64;
		clj_header **grown = realloc(v->items, v->cap * sizeof *grown);
		if (!grown) clj_fatal("out of memory");
		v->items = grown;
	}
	v->items[v->n++] = h;
}

static _Atomic int64_t stats[CLJ_CC_STAT_COUNT];

static void stat_add(int i, int64_t n) { atomic_fetch_add_explicit(&stats[i], n, memory_order_relaxed); }

static uint64_t wall_ns(void) {
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static struct timespec wall_at(uint64_t ns) {
	return (struct timespec){.tv_sec = (time_t)(ns / 1000000000u), .tv_nsec = (long)(ns % 1000000000u)};
}

// Registries hold execs and descriptors without a reference (design §7).
static bool opaque(const clj_type *t) { return t->unlink || t == &clj_type_type; }

// Set with the kill switch, before the first object (ice.c): a plain read on the paths that give REACH_LOCAL.
__attribute__((used)) uint32_t clj_cc_kill_oncycle;

// CLJ_CC=0 keeps the bits and drops every candidate: the control of a cost measurement.
static int enabled_state;

bool clj_cc_enabled(void) {
	if (__builtin_expect(!enabled_state, 0)) {
		const char *e = getenv("CLJ_CC");
		enabled_state = e && strcmp(e, "0") == 0 ? 2 : 1;
	}
	return enabled_state == 1;
}

// ---- the shared buffer and the background thread

// Chunks, not one array: a push from the main carrier never copies a grown buffer under the mutex.
enum { CHUNK = 1022 };
typedef struct chunk {
	struct chunk *next;
	size_t        n;
	clj_header   *items[CHUNK];
} chunk;

// A pthread mutex rather than a clj_lock: the background thread waits on a condition paired with it.
static pthread_mutex_t buf_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  buf_cv = PTHREAD_COND_INITIALIZER;
static chunk          *shared_head, *spare;
static size_t          shared_n;
static size_t          shared_fresh; // candidates since the last take, retries not counted
static pthread_once_t  thread_once = PTHREAD_ONCE_INIT;

// The deep entries (clj_cc_deep_release), each owning its object's BUFFERED bit and no reference, as a shared one.
static chunk   *deep_head;
static size_t   deep_n;
static uint64_t deep_first_at;   // wall time of the waiting batch's first entry
static uint64_t deep_not_before; // wall time the duty cycle lets the next deep collection start

// A ring a deep walk found alive, held by a reference until a later walk frees it or finds it gone.
typedef struct {
	clj_header *h;
	uint64_t    at;
	uint32_t    attempt;
} retry;
typedef struct {
	retry *items;
	size_t n, cap;
} retry_vec;
static retry_vec retries;

static void retry_push(retry_vec *v, retry r) {
	if (v->n == v->cap) {
		v->cap = v->cap ? v->cap * 2 : 16;
		retry *grown = realloc(v->items, v->cap * sizeof *grown);
		if (!grown) clj_fatal("out of memory");
		v->items = grown;
	}
	v->items[v->n++] = r;
}

static int64_t collect_shared(void);
static int64_t collect_deep(bool force);

// Under buf_mu. The wall time the waiting deep work is due, UINT64_MAX for none: a batch after the delay that lets
// a reload's defs gather, a retry at its time, neither before the duty cycle allows.
static uint64_t deep_due_at(void) {
	uint64_t due = deep_n ? deep_first_at + (uint64_t)SHARED_DELAY_MS * 1000000u : UINT64_MAX;
	for (size_t i = 0; i < retries.n; i++) {
		if (retries.items[i].at < due) due = retries.items[i].at;
	}
	return due != UINT64_MAX && due < deep_not_before ? deep_not_before : due;
}

static void *cc_main(void *arg) {
	(void)arg;
#ifdef __APPLE__
	// Below the carriers' default: a collection yields the cpu to the mutators whose garbage it collects.
	pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
	pthread_mutex_lock(&buf_mu);
	for (;;) {
		if (shared_fresh) {
			struct timespec at = wall_at(wall_ns() + (uint64_t)SHARED_DELAY_MS * 1000000u);
			while (shared_fresh && shared_fresh < SHARED_THRESHOLD) {
				if (pthread_cond_timedwait(&buf_cv, &buf_mu, &at) != 0) break;
			}
		}
		uint64_t due = deep_due_at();
		bool     regular = shared_fresh != 0, deep = due <= wall_ns();
		if (!regular && !deep) {
			if (due == UINT64_MAX) {
				pthread_cond_wait(&buf_cv, &buf_mu);
			} else {
				struct timespec at = wall_at(due);
				pthread_cond_timedwait(&buf_cv, &buf_mu, &at);
			}
			continue;
		}
		pthread_mutex_unlock(&buf_mu);
		if (regular) collect_shared();
		if (deep) collect_deep(false);
		pthread_mutex_lock(&buf_mu);
	}
	return NULL;
}

static void start_thread(void) {
	// Seeded, collections happen where the schedule puts them (sched.c), never at a real moment.
	if (clj_sched_seed_on) return;
	pthread_t      t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&t, &attr, cc_main, NULL) != 0) clj_fatal("pthread_create of the cycle collector failed");
	pthread_attr_destroy(&attr);
}

// Under buf_mu.
static void chunk_push(chunk **head, clj_header *h) {
	if (!*head || (*head)->n == CHUNK) {
		chunk *c = spare;
		spare = NULL;
		if (!c && !(c = malloc(sizeof *c))) clj_fatal("out of memory");
		c->n = 0;
		c->next = *head;
		*head = c;
	}
	(*head)->items[(*head)->n++] = h;
}

// Moves a taken list's entries into out; one emptied chunk stays as the spare.
static void chunks_drain(chunk *taken, cand_vec *out) {
	while (taken) {
		chunk *c = taken;
		taken = c->next;
		for (size_t i = 0; i < c->n; i++) vec_push(out, c->items[i]);
		pthread_mutex_lock(&buf_mu);
		if (!spare) {
			spare = c;
			c = NULL;
		}
		pthread_mutex_unlock(&buf_mu);
		free(c);
	}
}

// fresh: a new candidate, which may wake the thread; a retry waits for the next fresh one.
static void shared_push(clj_header *h, bool fresh) {
	pthread_once(&thread_once, start_thread);
	pthread_mutex_lock(&buf_mu);
	chunk_push(&shared_head, h);
	shared_n++;
	if (fresh && (++shared_fresh == 1 || shared_fresh == SHARED_THRESHOLD)) pthread_cond_signal(&buf_cv);
	pthread_mutex_unlock(&buf_mu);
}

// ---- the RC hooks

void clj_cc_unwatch(clj_header *h) { atomic_fetch_and_explicit(&h->rc, ~CLJ_RC_WATCH, memory_order_seq_cst); }


// The decrement and the bit in one CAS: an entry the collector takes before the decrement would count the
// releaser's reference as external, and nothing would file the object again.
bool clj_cc_shared_candidate(clj_header *h) {
	if (!clj_cc_enabled()) return false;
	uint32_t cur = atomic_load_explicit(&h->rc, memory_order_relaxed);
	for (;;) {
		if ((cur & CLJ_RC_BUFFERED) || (cur & CLJ_RC_COUNT_MASK) <= 1) return false;
		if (atomic_compare_exchange_weak_explicit(&h->rc, &cur, ((cur - 1) | CLJ_RC_BUFFERED) & ~CLJ_RC_WATCH,
		                                          memory_order_seq_cst, memory_order_relaxed))
			break;
	}
	stat_add(CLJ_CC_STAT_CANDIDATES, 1);
	stat_add(CLJ_CC_STAT_SHARED_FILED, 1);
	shared_push(h, true);
	return true;
}

static void deep_push(clj_header *h) {
	pthread_once(&thread_once, start_thread);
	pthread_mutex_lock(&buf_mu);
	chunk_push(&deep_head, h);
	if (deep_n++ == 0) {
		deep_first_at = wall_ns();
		pthread_cond_signal(&buf_cv);
	}
	pthread_mutex_unlock(&buf_mu);
}

// The entry owns the bit and holds no reference, as one a release files.
void clj_cc_file(clj_header *h) {
	if (!clj_cc_enabled()) return;
	uint32_t cur = atomic_load_explicit(&h->rc, memory_order_relaxed);
	while (!(cur & CLJ_RC_BUFFERED) && (cur & CLJ_RC_COUNT_MASK)) {
		if (atomic_compare_exchange_weak_explicit(&h->rc, &cur, cur | CLJ_RC_BUFFERED, memory_order_seq_cst, memory_order_relaxed)) {
			stat_add(CLJ_CC_STAT_CANDIDATES, 1);
			stat_add(CLJ_CC_STAT_SHARED_FILED, 1);
			shared_push(h, true);
			return;
		}
	}
}

static _Thread_local int64_t deep_filed_here;

// The decrement and the bit in one CAS, as for a shared candidate. An object an entry already names is left to that
// entry, though it may be a pruned one: an entry holding a reference would keep the object past its last release.
int clj_cc_deep_release(clj_header *h) {
	uint32_t f = h->flags;
	if ((f & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL)) != CLJ_FLAG_SHARED || !(f & (CLJ_FLAG_REACH | CLJ_FLAG_LAZY)) ||
	    opaque(h->type) || !clj_cc_enabled())
		return -1;
	uint32_t cur = atomic_load_explicit(&h->rc, memory_order_relaxed);
	for (;;) {
		CLJ_ASSERT(cur & CLJ_RC_COUNT_MASK, "release of a freed shared object");
		if (cur & CLJ_RC_BUFFERED) return -1;
		if ((cur & CLJ_RC_COUNT_MASK) == 1) {
			// seq_cst: the collection-flag load after a zero pairs with the collector's store, as in rc.c.
			if (atomic_compare_exchange_weak_explicit(&h->rc, &cur, (cur - 1) & ~CLJ_RC_WATCH, memory_order_seq_cst,
			                                          memory_order_relaxed))
				return 1;
			continue;
		}
		if (atomic_compare_exchange_weak_explicit(&h->rc, &cur, ((cur - 1) | CLJ_RC_BUFFERED) & ~CLJ_RC_WATCH,
		                                          memory_order_seq_cst, memory_order_relaxed)) {
			stat_add(CLJ_CC_STAT_DEEP_FILED, 1);
			deep_filed_here++;
			deep_push(h);
			return 0;
		}
	}
}

// While set, a shared object at zero stays whole: a pointer read without a lock never meets returned memory.
_Atomic bool clj_cc_running;
static pthread_mutex_t defer_mu = PTHREAD_MUTEX_INITIALIZER;
static cand_vec        deferred;

// Bit 0 of a deferred entry: its teardown is a deep one (clj_rc_free).
bool clj_cc_defer_free(clj_header *h, bool deep) {
	if (!atomic_load_explicit(&clj_cc_running, memory_order_seq_cst)) return false;
	pthread_mutex_lock(&defer_mu);
	bool on = atomic_load_explicit(&clj_cc_running, memory_order_relaxed);
	if (on) vec_push(&deferred, (clj_header *)((uintptr_t)h | deep));
	pthread_mutex_unlock(&defer_mu);
	return on;
}

// The barrier pairs with the collector's activate-then-read: design §7, the lock-free protocol. Reading `active`
// first keeps a contended owner's header (an atom swapped by many threads) out of its critical section.
void clj_cc_note_store(clj_header *owner, uint32_t flags) {
	if (!(flags & CLJ_FLAG_CC_LOCKED)) atomic_thread_fence(memory_order_seq_cst);
	if (__builtin_expect(atomic_load_explicit(&clj_cc_running, memory_order_seq_cst), 0) &&
	    (atomic_load_explicit(&owner->rc, memory_order_seq_cst) & CLJ_RC_WATCH))
		clj_cc_unwatch(owner);
}

static void activate(void) {
	atomic_store_explicit(&clj_cc_running, true, memory_order_seq_cst);
	atomic_thread_fence(memory_order_seq_cst);
}

void clj_cc_zombie(clj_header *h) {
	clj_dealloc_dead(h);
	atomic_store_explicit(&h->rc, CLJ_RC_ZOMBIE, memory_order_release);
}

static void deactivate(void) {
	pthread_mutex_lock(&defer_mu);
	atomic_store_explicit(&clj_cc_running, false, memory_order_seq_cst);
	cand_vec dead = deferred;
	deferred = (cand_vec){0};
	pthread_mutex_unlock(&defer_mu);
	for (size_t i = 0; i < dead.n; i++) {
		uintptr_t e = (uintptr_t)dead.items[i];
		clj_rc_free((clj_header *)(e & ~(uintptr_t)1), e & 1);
	}
	free(dead.items);
}

// ---- the acyclic marks (design §7, «Метка «ацикличен»»)

// Moved by a guarded store with no running execution to name: it voids the marks of every execution.
static _Atomic uint64_t acyclic_epoch;

// The addresses an execution marked in one epoch. An entry of a dead object stays: its cell is born without the bit.
typedef struct {
	clj_header **slots; // open addressing, NULL empty
	clj_header **recent; // a subset of slots, emptied with them
	uint32_t     cap, n;
	uint64_t     epoch;
} checked_set;

static uint32_t ck_slot(uint32_t cap, const clj_header *h) {
	uint64_t x = (uint64_t)(uintptr_t)h >> 4;
	x *= 0x9e3779b97f4a7c15ull;
	return (uint32_t)(x >> 32) & (cap - 1);
}

static bool ck_has(const checked_set *s, const clj_header *h) {
	if (!s->n) return false;
	for (uint32_t i = ck_slot(s->cap, h);; i = (i + 1) & (s->cap - 1)) {
		if (s->slots[i] == h) return true;
		if (!s->slots[i]) return false;
	}
}

static uint32_t ck_recent_slot(const clj_header *h) { return (uint32_t)((uintptr_t)h >> 5) & (CHECKED_RECENT - 1); }

static bool ck_has_recent(checked_set *s, const clj_header *h) {
	if (!s->n) return false;
	uint32_t i = ck_recent_slot(h);
	if (s->recent[i] == h) return true;
	if (!ck_has(s, h)) return false;
	s->recent[i] = (clj_header *)h;
	return true;
}

static void ck_reset(checked_set *s, uint64_t epoch) {
	if (s->n) memset(s->recent, 0, CHECKED_RECENT * sizeof *s->recent);
	if (s->n) memset(s->slots, 0, s->cap * sizeof *s->slots);
	s->n = 0;
	s->epoch = epoch;
}

static void ck_put(checked_set *s, clj_header *h) {
	if (s->n >= CHECKED_MAX) ck_reset(s, s->epoch);
	if ((s->n + 1) * 2 > s->cap) {
		uint32_t     cap = s->cap ? s->cap * 2 : 1024;
		clj_header **grown = calloc(cap, sizeof *grown);
		if (!grown) clj_fatal("out of memory");
		for (uint32_t i = 0; i < s->cap; i++) {
			if (!s->slots[i]) continue;
			uint32_t j = ck_slot(cap, s->slots[i]);
			while (grown[j]) j = (j + 1) & (cap - 1);
			grown[j] = s->slots[i];
		}
		free(s->slots);
		s->slots = grown;
		s->cap = cap;
		if (!s->recent && !(s->recent = calloc(CHECKED_RECENT, sizeof *s->recent))) clj_fatal("out of memory");
	}
	uint32_t i = ck_slot(s->cap, h);
	while (s->slots[i]) {
		if (s->slots[i] == h) return;
		i = (i + 1) & (s->cap - 1);
	}
	s->slots[i] = h;
	s->n++;
}

// What an execution keeps for its local collections; the transfers of clj_cc_local_borrow carry it whole, so the
// state of the owner of every unshared object a store reaches is the running one.
typedef struct {
	cand_vec    buf;
	checked_set ck;
	uint64_t    guard_hits; // guarded stores while this state ran
	int64_t     unfiled;    // not yet in stats: every release of a marked node counts here
} local_state;

void clj_cc_guard_hit(void) {
	clj_coro *c = clj_coro_here();
	if (c && c->cc_local) ((local_state *)c->cc_local)->guard_hits++;
	else atomic_fetch_add_explicit(&acyclic_epoch, 1, memory_order_relaxed);
	stat_add(CLJ_CC_STAT_EPOCHS, 1);
}

// NULL when the kill switch is on; a table of an older epoch is emptied first. Both counts only grow, so the sum
// stands still only while neither moves.
static checked_set *ck_current(local_state *ls) {
	if (clj_opt_off(CLJ_NO_OPT_ACYCLIC)) return NULL;
	uint64_t e = ls->guard_hits + atomic_load_explicit(&acyclic_epoch, memory_order_relaxed);
	if (ls->ck.epoch != e) ck_reset(&ls->ck, e);
	return &ls->ck;
}

static bool ck_valid(checked_set *s, const clj_header *h) {
	return s && (h->flags & CLJ_FLAG_ACYCLIC) && ck_has_recent(s, h);
}

// A reference type takes HEAPREF at its first edge from the heap: a store into it closes a cycle only after one,
// and one without REACH_LOCAL lies on no cycle a local collection could free. flags are the owner's before the store,
// with a MUTABLE owner's DIRTY and GUARDED already handled (object.h).
void clj_cc_reach_local(clj_header *owner, uint32_t flags, clj_header *c, uint32_t f) {
	if ((f & (CLJ_FLAG_MUTABLE | CLJ_FLAG_HEAPREF)) == CLJ_FLAG_MUTABLE) c->flags = f | CLJ_FLAG_HEAPREF;
	if (flags & (CLJ_FLAG_MUTABLE | CLJ_FLAG_IMMORTAL)) {
		// After the guard hit, so a mark the walk would skip is void. An immutable owner is edited only while unique
		// from an owned root down, and a path back to it would make that root shared (design §7).
		if ((flags & CLJ_FLAG_MUTABLE) && ((flags & CLJ_FLAG_HEAPREF) || c == owner)) clj_cc_store_check(owner, c);
		return;
	}
	uint32_t add = (f & (CLJ_FLAG_REACH | CLJ_FLAG_LAZY)) | CLJ_FLAG_REACH_LOCAL | clj_cc_kill_oncycle;
	if (!(add & ~flags)) return;
	CLJ_ASSERT(!(flags & CLJ_FLAG_SHARED) || atomic_load_explicit(&owner->rc, memory_order_relaxed) == 1,
	           "reach bits into a published owner another reference can read");
	// c == owner: the HEAPREF just written stays.
	owner->flags |= add;
}

// ---- the local buffer of an execution

static _Thread_local bool as_main;

static bool on_main(const clj_coro *c) { return as_main || (c->carrier && c->carrier->is_main); }

static local_state *local_of(clj_coro *c) {
	if (!c->cc_local) {
		c->cc_local = calloc(1, sizeof(local_state));
		if (!c->cc_local) clj_fatal("out of memory");
	}
	return c->cc_local;
}

static cand_vec *buf_of(const clj_coro *c) { return c->cc_local ? &((local_state *)c->cc_local)->buf : NULL; }

// The main carrier never walks mid-work: past the bound a candidate is published to the background (design §7).
// One at zero but not yet a zombie is torn down further up this thread's stack: the shared buffer waits for it.
static void hand_off(clj_header *h, void (*share)(clj_value v)) {
	uint32_t rc = atomic_load_explicit(&h->rc, memory_order_acquire);
	if (rc == CLJ_RC_ZOMBIE) {
		clj_dealloc_cell(h);
		return;
	}
	if (rc & CLJ_RC_COUNT_MASK) share(clj_from_ptr(h));
	stat_add(CLJ_CC_STAT_HANDOFFS, 1);
	shared_push(h, true);
}

static int64_t collect_local(clj_coro *c, size_t max, bool mark);

// An entry as a release's, with no collection nor hand-off from here.
static void file_local(clj_coro *c, clj_header *h) {
	uint32_t rc = CLJ_RC_UNSHARED_LOAD(h);
	if (rc & CLJ_RC_BUFFERED) return;
	CLJ_RC_UNSHARED_STORE(h, rc | CLJ_RC_BUFFERED);
	vec_push(&local_of(c)->buf, h);
	stat_add(CLJ_CC_STAT_CANDIDATES, 1);
}

void clj_cc_local_candidate(clj_header *h) {
	clj_coro *c = clj_coro_current();
	cand_vec *b = &local_of(c)->buf;
	vec_push(b, h);
	stat_add(CLJ_CC_STAT_CANDIDATES, 1);
	if (on_main(c)) {
		if (b->n > MAIN_BOUND) {
			clj_header *old = b->items[0];
			b->items[0] = b->items[--b->n];
			uint64_t t0 = clj_profile_now();
			hand_off(old, clj_share);
			int64_t took = (int64_t)(clj_profile_now() - t0);
			int64_t seen = atomic_load_explicit(&stats[CLJ_CC_STAT_HANDOFF_MAX_NS], memory_order_relaxed);
			while (took > seen && !atomic_compare_exchange_weak_explicit(&stats[CLJ_CC_STAT_HANDOFF_MAX_NS], &seen, took,
			                                                             memory_order_relaxed, memory_order_relaxed)) {}
		}
	} else if (b->n >= LOCAL_THRESHOLD && !c->cc_collecting && !c->locks_held && !c->cmutex_held) {
		// No lock held: a cycle's teardown frees whatever it held, and a finalizer may want a lock the caller has.
		collect_local(c, SIZE_MAX, true);
	}
}

// ---- the graph of one collection

// N_BUFFERED: an entry elsewhere names it, so a white one stays as a zombie for that entry. N_ROOT: an entry that
// owns the BUFFERED bit names it; N_HELD: a retry, which holds a reference instead. N_CUT: a child was left out at the
// deep cap; N_CYCLIC: on a ring of the deep graph. N_HOT: no mark may stand above it; N_TAINT: a hot node below;
// N_MARK: marked acyclic by this collection; N_REACHED: below such a mark (mark_acyclic).
enum {
	N_ROOT = 1, N_LOST = 2, N_BLACK = 4, N_WHITE = 8, N_BUFFERED = 16, N_HELD = 32, N_CUT = 64, N_CYCLIC = 128,
	N_HOT = 256, N_TAINT = 512, N_MARK = 1024, N_REACHED = 2048,
	N_OLD = 4096, // a parked coroutine past the stale-park report's threshold in a park not yet reported
};
#define NONE UINT32_MAX

typedef struct {
	clj_header *h;
	uint32_t    count;    // the count when watched, the collector's own references included
	uint32_t    internal; // recorded edges into it, and a parked coroutine's own references
	uint32_t    ours;     // references the collector holds
	uint32_t    edge_lo, edge_n;
	uint32_t    queued, woken; // a parked coroutine: the channel queues holding its waiter, and those found
	uint32_t    park;          // an N_OLD coroutine's park
	uint16_t    flags;
	uint8_t     attempt; // a retry's attempt + 1, 0 when no retry names it
	uint8_t     coro;    // CLJ_CORO_CC_* of a coroutine node
} node;

typedef struct {
	bool      shared;
	bool      deep; // descends through LAZY as well as REACH: the walk of a replaced var root
	bool      cut;  // a deep walk reached DEEP_MAX_NODES
	node     *nodes;
	uint32_t  n, cap;
	uint32_t *edges;
	size_t    ne, ecap;
	uint32_t *table; // node index + 1, 0 empty
	uint32_t  tcap;
	uint32_t *work;
	uint32_t  nw, wcap;
	uint32_t  cur;        // whose children are being visited
	bool      lockfree;   // cur is a mutable object read without its lock
	bool      cur_failed; // a child of cur could not be held: cur's edges are incomplete
	// Wake edges, channel → parked coroutine, as pairs: uncounted, and blackness crosses them both ways.
	uint32_t *wakes;
	size_t    nwakes, wakes_cap;
	bool      kept; // the white set holds a parked coroutine: cancelled, nothing freed (design §7, «Фаза 3»)
	checked_set *ck; // a local collection's marks; NULL: none read, none set
	bool         mark;
	int64_t      skipped;
	uint64_t     report_ns, now; // the stale-park report's threshold, 0 off, and this collection's clock
} graph;

static uint32_t slot_of(const graph *g, const clj_header *h) {
	uint64_t x = (uint64_t)(uintptr_t)h >> 4;
	x *= 0x9e3779b97f4a7c15ull;
	return (uint32_t)(x >> 32) & (g->tcap - 1);
}

static uint32_t find(const graph *g, const clj_header *h) {
	if (!g->tcap) return NONE;
	for (uint32_t s = slot_of(g, h);; s = (s + 1) & (g->tcap - 1)) {
		uint32_t e = g->table[s];
		if (!e) return NONE;
		if (g->nodes[e - 1].h == h) return e - 1;
	}
}

static void table_put(graph *g, uint32_t i) {
	uint32_t s = slot_of(g, g->nodes[i].h);
	while (g->table[s]) s = (s + 1) & (g->tcap - 1);
	g->table[s] = i + 1;
}

static void *grow(void *p, uint32_t *cap, size_t elem) {
	*cap = *cap ? *cap * 2 : 64;
	void *q = realloc(p, *cap * elem);
	if (!q) clj_fatal("out of memory");
	return q;
}

static void work_push(graph *g, uint32_t i) {
	if (g->nw == g->wcap) g->work = grow(g->work, &g->wcap, sizeof *g->work);
	g->work[g->nw++] = i;
}

static uint32_t add_node(graph *g, clj_header *h, uint32_t ours, uint16_t flags) {
	if (g->n == g->cap) g->nodes = grow(g->nodes, &g->cap, sizeof *g->nodes);
	if ((g->n + 1) * 2 > g->tcap) {
		g->tcap = g->tcap ? g->tcap * 2 : 128;
		free(g->table);
		g->table = calloc(g->tcap, sizeof *g->table);
		if (!g->table) clj_fatal("out of memory");
		for (uint32_t i = 0; i < g->n; i++) table_put(g, i);
	}
	uint32_t i = g->n++;
	g->nodes[i] = (node){.h = h, .ours = ours, .flags = flags};
	table_put(g, i);
	work_push(g, i);
	return i;
}

static void edge_push(graph *g, uint32_t to) {
	if (g->ne == g->ecap) {
		g->ecap = g->ecap ? g->ecap * 2 : 256;
		g->edges = realloc(g->edges, g->ecap * sizeof *g->edges);
		if (!g->edges) clj_fatal("out of memory");
	}
	g->edges[g->ne++] = to;
	g->nodes[to].internal++;
}

static void wake_push(graph *g, uint32_t from, uint32_t to) {
	if (g->nwakes + 2 > g->wakes_cap) {
		g->wakes_cap = g->wakes_cap ? g->wakes_cap * 2 : 64;
		g->wakes = realloc(g->wakes, g->wakes_cap * sizeof *g->wakes);
		if (!g->wakes) clj_fatal("out of memory");
	}
	g->wakes[g->nwakes++] = from;
	g->wakes[g->nwakes++] = to;
	g->nodes[to].woken++;
}

static void graph_free(graph *g) {
	free(g->nodes);
	free(g->edges);
	free(g->table);
	free(g->work);
	free(g->wakes);
}

static bool enterable(const graph *g, const clj_header *c) {
	uint32_t f = c->flags;
	if (f & CLJ_FLAG_IMMORTAL) return false;
	// A coroutine has no REACH: the shared walk enters it by type (design §7, «Фаза 3»), the deep one never.
	if (c->type == &clj_coro_type) return g->shared && !g->deep;
	if (g->deep ? !(f & CLJ_FLAG_SHARED) || !(f & (CLJ_FLAG_REACH | CLJ_FLAG_LAZY))
	    : g->shared ? (f & (CLJ_FLAG_SHARED | CLJ_FLAG_REACH)) != (CLJ_FLAG_SHARED | CLJ_FLAG_REACH)
	                : (f & (CLJ_FLAG_SHARED | CLJ_FLAG_REACH_LOCAL)) != CLJ_FLAG_REACH_LOCAL)
		return false;
	return !opaque(c->type);
}

// Edited in place at any count (map.c, flat_editable): no uniqueness check clears a mark above it.
static bool unshared_transient(const clj_header *h) {
	return h->type == &clj_map_type && !(h->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL)) &&
	       clj_map_is_transient(clj_from_ptr((void *)h));
}

// No mark may stand above it.
static bool hot(const clj_header *h) {
	uint32_t f = h->flags;
	if (f & CLJ_FLAG_MUTABLE)
		return (f & CLJ_FLAG_DIRTY) || (h->type == &clj_lazy_seq_type && !clj_lazy_seq_realized(clj_from_ptr((void *)h)));
	return unshared_transient(h);
}

#if CLJ_DEBUG
static _Thread_local uint32_t audit_every = AUDIT_EVERY, audit_tick;

void clj_debug_cc_audit_every(uint32_t n) { audit_every = n ? n : AUDIT_EVERY; }

// What a valid mark promises below x (design §7, «Метка «ацикличен»»), over AUDIT_NODES nodes at most.
typedef struct {
	const clj_header *x;
	clj_header       *seen[2 * AUDIT_NODES];
	clj_header       *stack[AUDIT_NODES];
	uint32_t          nseen, nstack;
} audit_walk;

static void audit_fail(const char *what, const clj_header *x, const clj_header *at) {
	clj_ice("cc/acyclic-mark", NULL, "a marked %s %s %s", x->type->name, what, at->type->name);
}

static void audit_visit(clj_value v, void *ctx) {
	audit_walk *a = ctx;
	if (!clj_is_ptr(v) || a->nseen >= AUDIT_NODES) return;
	clj_header *h = clj_header_of(v);
	if (h == a->x) audit_fail("reaches itself through", a->x, a->x);
	if (unshared_transient(h)) audit_fail("reaches a transient", a->x, h);
	if (!enterable(&(graph){0}, h)) return;
	uint32_t i = ck_slot(2 * AUDIT_NODES, h);
	while (a->seen[i]) {
		if (a->seen[i] == h) return;
		i = (i + 1) & (2 * AUDIT_NODES - 1);
	}
	a->seen[i] = h;
	a->nseen++;
	if (hot(h)) audit_fail("reaches a hot", a->x, h);
	if ((h->flags & CLJ_FLAG_MUTABLE) && !(h->flags & CLJ_FLAG_GUARDED)) audit_fail("reaches an unguarded", a->x, h);
	a->stack[a->nstack++] = h;
}

static void audit_skip(const clj_header *x) {
	if (++audit_tick % audit_every) return;
	if ((x->flags & CLJ_FLAG_MUTABLE) && !(x->flags & CLJ_FLAG_GUARDED)) audit_fail("is an unguarded", x, x);
	audit_walk *a = calloc(1, sizeof *a);
	if (!a) clj_fatal("out of memory");
	a->x = x;
	a->stack[a->nstack++] = (clj_header *)x;
	while (a->nstack) {
		clj_header *h = a->stack[--a->nstack];
		if (h->type->each_child) h->type->each_child(h, audit_visit, a);
	}
	free(a);
}
#else
void clj_debug_cc_audit_every(uint32_t n) { (void)n; }
#define audit_skip(x) ((void)0)
#endif

// On no cycle now, so no member of a garbage one; a cycle closed through it later voids the mark first, and the
// release that leaves that cycle garbage finds the mark void (design §7, «Метка «ацикличен»»).
bool clj_cc_local_unfiled(clj_header *h) {
	if (clj_opt_off(CLJ_NO_OPT_UNFILED)) return false;
	clj_coro *c = clj_coro_current();
	// A collection's table must not start over under it: a release inside its teardown files as before.
	local_state *ls = c->cc_local;
	if (!ls || c->cc_collecting || !ck_valid(ck_current(ls), h)) return false;
	audit_skip(h);
	if (++ls->unfiled == 1024) {
		stat_add(CLJ_CC_STAT_UNFILED, ls->unfiled);
		ls->unfiled = 0;
	}
	return true;
}

// A child read without its owner's lock: alive (frees are deferred) but maybe at zero, so a CAS from a nonzero count.
static bool try_retain(clj_header *c) {
	uint32_t cur = atomic_load_explicit(&c->rc, memory_order_relaxed);
	do {
		if (!(cur & CLJ_RC_COUNT_MASK)) return false;
	} while (!atomic_compare_exchange_weak_explicit(&c->rc, &cur, cur + 1, memory_order_seq_cst, memory_order_relaxed));
	return true;
}

static void visit_child(clj_value v, void *ctx) {
	graph *g = ctx;
	if (!clj_is_ptr(v) || g->cur_failed) return;
	clj_header *c = clj_header_of(v);
	if (!enterable(g, c)) {
		// Its edit takes the bit with no store a mark above would see (design §7, «Метка «ацикличен»»).
		if (g->mark && unshared_transient(c)) g->nodes[g->cur].flags |= N_HOT;
		return;
	}
	// On no cycle: a holder outside the graph, as a node without the bit is.
	if (ck_valid(g->ck, c)) {
		g->skipped++;
		audit_skip(c);
		return;
	}
	uint32_t i = find(g, c);
	if (i == NONE) {
		if (g->deep && g->n >= DEEP_MAX_NODES) {
			g->cut = true;
			g->nodes[g->cur].flags |= N_CUT;
			g->cur_failed = true;
			return;
		}
		if (g->shared) {
			if (g->lockfree) {
				clj_header *owner = g->nodes[g->cur].h;
				if (!try_retain(c)) {
					g->cur_failed = true;
					return;
				}
				// The owner kept its watch past the retain: the slot still held c, so no one holds c uniquely.
				if (!(atomic_load_explicit(&owner->rc, memory_order_seq_cst) & CLJ_RC_WATCH)) {
					clj_rc_drop(c, g->deep);
					g->cur_failed = true;
					return;
				}
			} else {
				atomic_fetch_add_explicit(&c->rc, 1, memory_order_relaxed);
			}
		}
		i = add_node(g, c, g->shared ? 1 : 0, 0);
	}
	edge_push(g, i);
}

static void watch(graph *g, uint32_t i) {
	node    *nd = &g->nodes[i];
	uint32_t w;
	if (nd->flags & N_ROOT) {
		// The buffer's bit goes now: a mutator's release from here on makes a candidate for the next collection.
		w = atomic_load_explicit(&nd->h->rc, memory_order_relaxed);
		while (!atomic_compare_exchange_weak_explicit(&nd->h->rc, &w, (w & ~CLJ_RC_BUFFERED) | CLJ_RC_WATCH,
		                                              memory_order_seq_cst, memory_order_relaxed)) {}
	} else {
		w = atomic_fetch_or_explicit(&nd->h->rc, CLJ_RC_WATCH, memory_order_seq_cst);
	}
	nd->count = w & CLJ_RC_COUNT_MASK;
}

// A coroutine parked on the object being visited: an uncounted edge to it. One that cannot be held is not recorded,
// and then its waiter's queues are not all found, which blackens it.
static void visit_wake(void *coro, void *ctx) {
	graph      *g = ctx;
	clj_header *k = coro;
	if (g->cur_failed || !enterable(g, k)) return;
	uint32_t i = find(g, k);
	if (i == NONE) {
		if (!try_retain(k)) return;
		i = add_node(g, k, 1, 0);
	}
	wake_push(g, g->cur, i);
}

static void visit_locked(void *self, void *ctx) {
	graph *g = ctx;
	watch(g, g->cur);
	clj_header *h = self;
	if (h->type->each_child) h->type->each_child(h, visit_child, g);
	if (h->type->cc_held) h->type->cc_held(h, visit_child, g, false);
	if (h->type->cc_wakes) h->type->cc_wakes(h, visit_wake, g);
}

// A parked coroutine's own references: those its frames own, and those of its unshared objects, which nothing but
// its frames reaches (design §7, «Фаза 3», «Кадры и частные объекты»). Each unshared object is entered once, so no
// slot is read twice; one past the cap is left out, and an edge left out reads as held from outside.
typedef struct {
	graph             *g;
	const clj_coro    *c;
	const clj_header **seen; // open addressing, 2 * PRIVATE_MAX_NODES
	const clj_header **stack;
	uint32_t           n, entered;
} private_walk;

static void private_enter(private_walk *p, clj_header *h) {
	// A registry reaches an opaque type without a reference: its slots are not the coroutine's alone.
	if (opaque(h->type) || p->entered >= PRIVATE_MAX_NODES) return;
	uint32_t i = ck_slot(2 * PRIVATE_MAX_NODES, h);
	while (p->seen[i]) {
		if (p->seen[i] == h) return;
		i = (i + 1) & (2 * PRIVATE_MAX_NODES - 1);
	}
#if CLJ_DEBUG
	if ((h->flags >> CLJ_OWNER_SHIFT) != p->c->debug_owner)
		clj_ice("cc/private-owner", NULL, "a parked coroutine reaches an unshared %s it does not own", h->type->name);
#endif
	p->seen[i] = h;
	p->stack[p->n++] = h;
	p->entered++;
}

static void private_child(clj_value v, void *ctx) {
	private_walk *p = ctx;
	if (!clj_is_ptr(v)) return;
	clj_header *h = clj_header_of(v);
	if (h->flags & CLJ_FLAG_IMMORTAL) return;
	if (h->flags & CLJ_FLAG_SHARED) visit_child(v, p->g);
	else private_enter(p, h);
}

// A reference a frame owns: an edge to a shared object, an unshared one entered.
static void frame_owned(private_walk *p, clj_value v) { private_child(v, p); }

// A reference a frame lends: only an unshared object's slots are the coroutine's.
static void frame_lent(private_walk *p, clj_value v) {
	if (clj_is_ptr(v) && !(clj_header_of(v)->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL))) private_enter(p, clj_header_of(v));
}

// n values at v, those whose bit is set, or every one past 64; v as the frame sees it.
static void read_owned(private_walk *p, const clj_value *v, uint64_t owned, size_t n) {
	if (n > 64) owned = UINT64_MAX;
	else if (n < 64) owned &= ((uint64_t)1 << n) - 1;
	if (!owned) return;
	size_t           top = n > 64 ? n : 64 - (size_t)__builtin_clzll(owned);
	const clj_value *at = clj_coro_parked_bytes(p->c, v, top * sizeof *at);
	if (!at) return;
	if (n > 64) {
		for (size_t i = 0; i < n; i++) frame_owned(p, at[i]);
		return;
	}
	for (uint64_t m = owned; m; m &= m - 1) frame_owned(p, at[__builtin_ctzll(m)]);
}

// Bounds only a walk of memory gone wrong: the chains are the parked execution's own.
enum { FRAME_READS_MAX = 1 << 16 };

// Owned slots, every one from 64 slots on (eval.c, slots_alloc), and the buffers of evaluations under way.
static void read_frame(private_walk *p, const clj_frame *at, uint32_t *budget) {
	const clj_frame *fr = clj_coro_parked_bytes(p->c, at, sizeof *fr);
	if (!fr || !*budget) return;
	--*budget;
	if (fr->owned == UINT64_MAX) {
		const clj_value *n = clj_coro_parked_bytes(p->c, fr->slots - 1, sizeof *n);
		if (n) read_owned(p, fr->slots, UINT64_MAX, (size_t)*n);
	} else if (fr->owned) {
		read_owned(p, fr->slots, fr->owned, 64);
	}
	for (const clj_argbuf *b = fr->args; b && *budget; --*budget) {
		const clj_argbuf *buf = clj_coro_parked_bytes(p->c, b, sizeof *buf);
		if (!buf) return;
		read_owned(p, buf->v, buf->owned, 64);
		if (buf->frame) read_frame(p, buf->frame, budget);
		b = buf->prev;
	}
}

// A wrapped ring holds entries of returned frames below its depth: none is read (design §7, «Фаза 3», «Кольцо»).
static void interp_frames(private_walk *p) {
	const clj_shadow_stack *s = p->c->shadow;
	if (s->wrapped) return;
	uint32_t budget = FRAME_READS_MAX;
	for (size_t i = 0; i < s->depth; i++) {
		const clj_shadow_frame *sf = clj_coro_parked_bytes(p->c, &s->frames[i], sizeof *sf);
		if (sf && sf->frame) read_frame(p, sf->frame, &budget);
	}
}

// A compiled frame's array (clj_cframe): its owned slots, every one past 64.
static void read_cframe(private_walk *p, const clj_cframe *at, size_t arr) {
	const clj_cframe *fr = clj_coro_parked_bytes(p->c, at, sizeof *fr);
	if (fr && arr) read_owned(p, fr->slots, fr->owned, arr);
}

// Each word an owned reference, a tagged address, or a kind and its two operands (shadow_internal.h).
static void read_frefs(private_walk *p, const clj_frefs *fl) {
	for (uint32_t i = 0; i < fl->n; i++) {
		clj_value w = fl->v[i];
		if ((w & CLJ_TAG_MASK) == CLJ_FREF_ADDR) {
			const clj_value *v = clj_coro_parked_bytes(p->c, (const void *)(w & ~CLJ_TAG_MASK), sizeof *v);
			if (v) frame_owned(p, *v);
		} else if ((w == CLJ_FREF_SPAN || w == CLJ_FREF_ARRAY || w == CLJ_FREF_CFRAME) && i + 2 < fl->n) {
			clj_value a = fl->v[i + 1], b = fl->v[i + 2];
			i += 2;
			if (w == CLJ_FREF_CFRAME) {
				read_cframe(p, (const clj_cframe *)a, (size_t)clj_fixnum_val(b));
			} else if (w == CLJ_FREF_ARRAY) {
				read_owned(p, (const clj_value *)a, UINT64_MAX, (size_t)clj_fixnum_val(b));
			} else {
				clj_value *const *base = clj_coro_parked_bytes(p->c, (const void *)a, sizeof *base);
				const size_t     *n = clj_coro_parked_bytes(p->c, (const void *)b, sizeof *n);
				if (base && n) read_owned(p, *base, UINT64_MAX, *n);
			}
		} else {
			frame_owned(p, w);
		}
	}
}

// Under its lock, which keeps it parked: its frames are still there and its owner touches nothing of its own.
static void visit_frames(graph *g, const clj_coro *c) {
	const clj_header *seen[2 * PRIVATE_MAX_NODES] = {0}, *stack[PRIVATE_MAX_NODES];
	private_walk      p = {.g = g, .c = c, .seen = seen, .stack = stack};
	read_frefs(&p, &c->shadow->frefs);
	interp_frames(&p);
	if (c->waiter) frame_lent(&p, c->waiter->ports);
	while (p.n) {
		const clj_header *u = stack[--p.n];
		if (u->type->each_child) u->type->each_child((void *)u, private_child, &p);
	}
}

// Under the coroutine's lock, sched.c judging it: a live one is black and its edges are not read.
static void visit_coro_locked(clj_coro *c, int verdict, uint32_t internal, uint32_t queued, void *ctx) {
	graph *g = ctx;
	node  *nd = &g->nodes[g->cur];
	watch(g, g->cur);
	nd->coro = (uint8_t)verdict;
	if (verdict == CLJ_CORO_CC_LIVE) {
		nd->flags |= N_LOST;
		return;
	}
	nd->internal += internal;
	nd->queued = queued;
	clj_coro_type.each_child(c, visit_child, g);
	if (verdict != CLJ_CORO_CC_PARKED) return;
	visit_frames(g, c);
	if (g->report_ns && c->cc_park == c->parks && c->cc_park_ns && g->now >= c->cc_park_ns + g->report_ns && c->cc_reported != c->parks + 1) {
		nd->flags |= N_OLD;
		nd->park = c->parks;
	}
}

// Its own frame, the one scripts/tsan.supp names: the slots are read while their writers store without a lock.
__attribute__((noinline)) static void visit_lockfree(graph *g, uint32_t i) {
	clj_header *h = g->nodes[i].h;
	watch(g, i);
	g->lockfree = true;
	if (h->type->each_child) h->type->each_child(h, visit_child, g);
}

static void visit(graph *g, uint32_t i) {
	clj_header     *h = g->nodes[i].h;
	const clj_type *t = h->type;
	g->cur = i;
	g->cur_failed = false;
	g->lockfree = false;
	g->nodes[i].edge_lo = (uint32_t)g->ne;
	if (!g->shared) {
		uint32_t rc = CLJ_RC_UNSHARED_LOAD(h);
		if (g->nodes[i].flags & N_ROOT) CLJ_RC_UNSHARED_STORE(h, rc & ~CLJ_RC_BUFFERED);
		else if (rc & CLJ_RC_BUFFERED) g->nodes[i].flags |= N_BUFFERED;
		g->nodes[i].count = rc & CLJ_RC_COUNT_MASK;
		if (g->mark && hot(h)) g->nodes[i].flags |= N_HOT;
		if (t->each_child) t->each_child(h, visit_child, g);
	} else if (t == &clj_coro_type) {
		if (!clj_coro_cc_locked((clj_coro *)h, visit_coro_locked, g)) {
			g->nodes[i].flags |= N_LOST;
			if (g->nodes[i].flags & N_ROOT) atomic_fetch_and_explicit(&h->rc, ~CLJ_RC_BUFFERED, memory_order_seq_cst);
		}
	} else if (t->cc_locked) {
		if (!t->cc_locked(h, visit_locked, g)) {
			g->nodes[i].flags |= N_LOST;
			if (g->nodes[i].flags & N_ROOT) atomic_fetch_and_explicit(&h->rc, ~CLJ_RC_BUFFERED, memory_order_seq_cst);
		}
	} else if (h->flags & CLJ_FLAG_MUTABLE) {
		visit_lockfree(g, i);
	} else {
		watch(g, i);
		if (t->each_child) t->each_child(h, visit_child, g);
	}
	if (g->cur_failed) g->nodes[i].flags |= N_LOST;
	g->nodes[i].edge_n = (uint32_t)(g->ne - g->nodes[i].edge_lo);
	stat_add(CLJ_CC_STAT_VISITED, 1);
}

// A node whose word moved since its watch is black: a mutator holds it, or its slots changed.
static void validate(graph *g) {
	for (uint32_t i = 0; i < g->n; i++) {
		node    *nd = &g->nodes[i];
		uint32_t w = atomic_fetch_and_explicit(&nd->h->rc, ~CLJ_RC_WATCH, memory_order_seq_cst);
		if (!(w & CLJ_RC_WATCH) || (w & CLJ_RC_COUNT_MASK) != nd->count) nd->flags |= N_LOST;
		if (w & CLJ_RC_BUFFERED) nd->flags |= N_BUFFERED;
	}
}

// A parked coroutine with a channel queue outside the graph has a wake source the walk cannot judge.
static void judge_wakes(graph *g) {
	for (uint32_t i = 0; i < g->n; i++) {
		node *nd = &g->nodes[i];
		if (nd->coro == CLJ_CORO_CC_PARKED && nd->woken != nd->queued) nd->flags |= N_LOST;
	}
}

static void blacken_mark(graph *g, uint32_t j) {
	if (g->nodes[j].flags & N_BLACK) return;
	g->nodes[j].flags |= N_BLACK;
	work_push(g, j);
}

// The wake pairs as adjacency both ways: a live channel can wake the coroutine, a live coroutine read the channel.
static void wake_adjacency(const graph *g, uint32_t **lo, uint32_t **adj) {
	*lo = *adj = NULL;
	if (!g->nwakes) return;
	uint32_t *wlo = calloc(g->n + 1, sizeof *wlo), *wadj = malloc(g->nwakes * sizeof *wadj);
	uint32_t *fill = malloc((g->n + 1) * sizeof *fill);
	if (!wlo || !wadj || !fill) clj_fatal("out of memory");
	for (size_t k = 0; k < g->nwakes; k++) wlo[g->wakes[k] + 1]++;
	for (uint32_t i = 0; i < g->n; i++) wlo[i + 1] += wlo[i];
	memcpy(fill, wlo, (g->n + 1) * sizeof *fill);
	for (size_t k = 0; k < g->nwakes; k += 2) {
		uint32_t a = g->wakes[k], b = g->wakes[k + 1];
		wadj[fill[a]++] = b;
		wadj[fill[b]++] = a;
	}
	free(fill);
	*lo = wlo;
	*adj = wadj;
}

static void blacken(graph *g) {
	uint32_t *wlo, *wadj;
	wake_adjacency(g, &wlo, &wadj);
	g->nw = 0;
	for (uint32_t i = 0; i < g->n; i++) {
		node *nd = &g->nodes[i];
		if ((nd->flags & N_LOST) || nd->count > nd->internal + nd->ours) blacken_mark(g, i);
	}
	while (g->nw) {
		uint32_t v = g->work[--g->nw];
		for (uint32_t e = 0; e < g->nodes[v].edge_n; e++) blacken_mark(g, g->edges[g->nodes[v].edge_lo + e]);
		if (wlo) {
			for (uint32_t e = wlo[v]; e < wlo[v + 1]; e++) blacken_mark(g, wadj[e]);
		}
	}
	free(wlo);
	free(wadj);
}

static void release_outside(clj_value v, void *ctx) {
	graph *g = ctx;
	if (!clj_is_ptr(v)) return;
	uint32_t i = find(g, clj_header_of(v));
	if (i != NONE && (g->nodes[i].flags & N_WHITE)) return;
	clj_release(v);
}

// Every member's finalize runs while every member is still whole; children outside the cycle go first, as on the
// acyclic path (design §7, finalization in a cycle).
static int64_t free_whites(graph *g) {
	int64_t freed = 0;
	for (uint32_t i = 0; i < g->n; i++) {
		if (!(g->nodes[i].flags & N_BLACK)) {
			g->nodes[i].flags |= N_WHITE;
			freed++;
			if (g->nodes[i].coro == CLJ_CORO_CC_PARKED) g->kept = true;
		}
	}
	if (!freed) return 0;
	// Its frames may read any white object through a borrowed reference: nothing is freed, the coroutine is cancelled
	// and its unwind lets go through RC (design §7, «Фаза 3»). The collector's references hold the set meanwhile.
	if (g->kept) {
		for (uint32_t i = 0; i < g->n; i++) {
			node *nd = &g->nodes[i];
			if ((nd->flags & N_WHITE) && nd->coro == CLJ_CORO_CC_PARKED && clj_coro_cc_cancel((clj_coro *)nd->h))
				stat_add(CLJ_CC_STAT_COROUTINES, 1);
		}
		return 0;
	}
	for (uint32_t i = 0; i < g->n; i++) {
		clj_header *h = g->nodes[i].h;
		if (!(g->nodes[i].flags & N_WHITE)) continue;
		if (h->type->each_child) h->type->each_child(h, release_outside, g);
		if (h->type->cc_held) h->type->cc_held(h, release_outside, g, true);
	}
	for (uint32_t i = 0; i < g->n; i++) {
		clj_header *h = g->nodes[i].h;
		if ((g->nodes[i].flags & N_WHITE) && h->type->finalize) h->type->finalize(h);
	}
	for (uint32_t i = 0; i < g->n; i++) {
		node *nd = &g->nodes[i];
		if (!(nd->flags & N_WHITE)) continue;
		if (nd->flags & N_BUFFERED) clj_cc_zombie(nd->h);
		else clj_dealloc(nd->h);
	}
	stat_add(CLJ_CC_STAT_FREED, freed);
	return freed;
}

// The collector's references on the survivors. A shared root black only because a mutator touched it mid-collection
// is filed again: the touch may have been its last outside reference going. A deep one is a retry (retry_deep).
// A white root kept for a cancelled coroutine is filed again too: what of its set outlives the unwind is judged anew.
static void release_survivors(graph *g) {
	for (uint32_t i = 0; i < g->n; i++) {
		node *nd = &g->nodes[i];
		bool  white = nd->flags & N_WHITE;
		if (white && !g->kept) continue;
		bool lost = (nd->flags & (N_ROOT | N_LOST)) == (N_ROOT | N_LOST);
		if (g->shared && !g->deep && (lost || (white && (nd->flags & N_ROOT)))) {
			uint32_t cur = atomic_load_explicit(&nd->h->rc, memory_order_relaxed);
			while (!(cur & CLJ_RC_BUFFERED)) {
				if (atomic_compare_exchange_weak_explicit(&nd->h->rc, &cur, cur | CLJ_RC_BUFFERED, memory_order_seq_cst,
				                                          memory_order_relaxed)) {
					if (lost) stat_add(CLJ_CC_STAT_INTERFERED, 1);
					shared_push(nd->h, white);
					break;
				}
			}
		}
		for (uint32_t k = nd->ours; k; k--) clj_rc_drop(nd->h, g->deep);
	}
}

// Tarjan's components over the black nodes, iteratively: a long realized chain is deeper than the C stack.
// Each component once, after every component it reaches.
static void black_components(graph *g, void (*component)(graph *g, const uint32_t *members, uint32_t n, void *ctx),
                             void *ctx) {
	typedef struct {
		uint32_t v, e;
	} call;
	uint32_t  n = g->n;
	uint32_t *index = malloc(n * sizeof *index), *low = malloc(n * sizeof *low), *stack = malloc(n * sizeof *stack);
	call     *calls = malloc(n * sizeof *calls);
	uint8_t  *on = calloc(n, 1);
	if (!index || !low || !stack || !calls || !on) clj_fatal("out of memory");
	for (uint32_t i = 0; i < n; i++) index[i] = NONE;
	uint32_t next = 0, sp = 0, cp = 0;
	for (uint32_t s = 0; s < n; s++) {
		if (!(g->nodes[s].flags & N_BLACK) || index[s] != NONE) continue;
		index[s] = low[s] = next++;
		stack[sp++] = s;
		on[s] = 1;
		calls[cp++] = (call){s, 0};
		while (cp) {
			call    *c = &calls[cp - 1];
			uint32_t v = c->v;
			if (c->e < g->nodes[v].edge_n) {
				// blacken spreads along recorded edges: a black node's edges lead to black nodes.
				uint32_t w = g->edges[g->nodes[v].edge_lo + c->e++];
				if (index[w] == NONE) {
					index[w] = low[w] = next++;
					stack[sp++] = w;
					on[w] = 1;
					calls[cp++] = (call){w, 0};
				} else if (on[w] && index[w] < low[v]) {
					low[v] = index[w];
				}
				continue;
			}
			cp--;
			if (cp && low[v] < low[calls[cp - 1].v]) low[calls[cp - 1].v] = low[v];
			if (low[v] != index[v]) continue;
			uint32_t from = sp;
			do from--;
			while (stack[from] != v);
			component(g, stack + from, sp - from, ctx);
			for (uint32_t k = from; k < sp; k++) on[stack[k]] = 0;
			sp = from;
		}
	}
	free(index);
	free(low);
	free(stack);
	free(calls);
	free(on);
}

static bool self_edge(const graph *g, uint32_t v) {
	const node *nd = &g->nodes[v];
	for (uint32_t e = 0; e < nd->edge_n; e++) {
		if (g->edges[nd->edge_lo + e] == v) return true;
	}
	return false;
}

// Sinks first, so what a component reaches is judged. A cycle taints too: garbage below a skipped node goes by RC.
static void mark_component(graph *g, const uint32_t *members, uint32_t n, void *ctx) {
	(void)ctx;
	bool taint = n > 1 || self_edge(g, members[0]);
	for (uint32_t k = 0; k < n && !taint; k++) {
		const node *nd = &g->nodes[members[k]];
		taint = nd->flags & N_HOT;
		for (uint32_t e = 0; e < nd->edge_n && !taint; e++) taint = g->nodes[g->edges[nd->edge_lo + e]].flags & N_TAINT;
	}
	if (taint) {
		for (uint32_t k = 0; k < n; k++) g->nodes[members[k]].flags |= N_TAINT;
	} else {
		g->nodes[members[0]].flags |= N_MARK;
	}
}

// A live cycle of a local graph is flagged: the check of a store past its budget leaves its owner to this walk.
static void local_component(graph *g, const uint32_t *members, uint32_t n, void *ctx) {
	if (n > 1 || self_edge(g, members[0])) {
		for (uint32_t k = 0; k < n; k++) g->nodes[members[k]].h->flags |= CLJ_FLAG_ONCYCLE;
		stat_add(CLJ_CC_STAT_STORE_FLAGGED, n);
	}
	if (g->mark) mark_component(g, members, n, ctx);
}

// After blacken, over the black nodes of a local graph (design §7, «Метка «ацикличен»»).
static void mark_acyclic(graph *g) {
	black_components(g, local_component, NULL);
	if (!g->mark) return;
	g->nw = 0;
	for (uint32_t i = 0; i < g->n; i++) {
		if (!(g->nodes[i].flags & N_MARK)) continue;
		g->nodes[i].flags |= N_REACHED;
		work_push(g, i);
	}
	while (g->nw) {
		const node *nd = &g->nodes[g->work[--g->nw]];
		if (nd->h->flags & CLJ_FLAG_MUTABLE) nd->h->flags |= CLJ_FLAG_GUARDED;
		for (uint32_t e = 0; e < nd->edge_n; e++) {
			uint32_t w = g->edges[nd->edge_lo + e];
			if (g->nodes[w].flags & N_REACHED) continue;
			g->nodes[w].flags |= N_REACHED;
			work_push(g, w);
		}
	}
	int64_t marked = 0;
	for (uint32_t i = 0; i < g->n; i++) {
		const node *nd = &g->nodes[i];
		if (nd->flags & N_MARK) {
			nd->h->flags |= CLJ_FLAG_ACYCLIC;
			ck_put(g->ck, nd->h);
			marked++;
		}
		// A quiet period starts: a store before the next walk makes this node hot again.
		if (nd->flags & N_BLACK) nd->h->flags &= ~CLJ_FLAG_DIRTY;
	}
	stat_add(CLJ_CC_STAT_MARKED, marked);
}

// ---- the stale-park report (design §7, «Фаза 3», «Отчёт о брошенном парке»; NOTES "Coroutines")

// UINT64_MAX until CLJ_PARK_REPORT_S is read; 0 off.
static _Atomic uint64_t park_report_ns = UINT64_MAX;

static uint64_t park_report_threshold(void) {
	uint64_t ns = atomic_load_explicit(&park_report_ns, memory_order_relaxed);
	if (__builtin_expect(ns != UINT64_MAX, 1)) return ns;
	const char *e = getenv("CLJ_PARK_REPORT_S");
	uint64_t    want = e && *e ? strtoull(e, NULL, 10) * 1000000000ull : 0, expected = UINT64_MAX;
	atomic_compare_exchange_strong_explicit(&park_report_ns, &expected, want, memory_order_relaxed, memory_order_relaxed);
	return atomic_load_explicit(&park_report_ns, memory_order_relaxed);
}

uint64_t clj_debug_cc_park_report_ms(uint64_t ms) {
	uint64_t old = park_report_threshold();
	atomic_store_explicit(&park_report_ns, ms * 1000000ull, memory_order_relaxed);
	return old / 1000000ull;
}

// Blackness that does not pass through the coroutine stops short of each of its wait channels: only a cancel ends
// the park, whether a holder of its handle keeps it or a reference the collector cannot read.
static void park_report(graph *g) {
	uint32_t *wlo = NULL, *wadj = NULL;
	uint8_t  *mark = NULL;
	for (uint32_t k = 0; k < g->n; k++) {
		const node *nk = &g->nodes[k];
		if ((nk->flags & (N_OLD | N_BLACK | N_LOST)) != (N_OLD | N_BLACK) || !nk->queued || nk->woken != nk->queued) continue;
		if (!mark) {
			wake_adjacency(g, &wlo, &wadj);
			mark = malloc(g->n);
			if (!mark) clj_fatal("out of memory");
		}
		memset(mark, 0, g->n);
		g->nw = 0;
		for (uint32_t i = 0; i < g->n; i++) {
			const node *nd = &g->nodes[i];
			if (i == k || !((nd->flags & N_LOST) || nd->count > nd->internal + nd->ours)) continue;
			mark[i] = 1;
			work_push(g, i);
		}
		while (g->nw) {
			uint32_t v = g->work[--g->nw];
			for (uint32_t e = 0; e < g->nodes[v].edge_n; e++) {
				uint32_t w = g->edges[g->nodes[v].edge_lo + e];
				if (w == k || mark[w]) continue;
				mark[w] = 1;
				work_push(g, w);
			}
			for (uint32_t e = wlo ? wlo[v] : 0; wlo && e < wlo[v + 1]; e++) {
				uint32_t w = wadj[e];
				if (w == k || mark[w]) continue;
				mark[w] = 1;
				work_push(g, w);
			}
		}
		bool reached = false;
		for (size_t p = 0; p < g->nwakes && !reached; p += 2) reached = g->wakes[p + 1] == k && mark[g->wakes[p]];
		if (reached || !clj_coro_cc_report_once((clj_coro *)nk->h, nk->park)) continue;
		stat_add(CLJ_CC_STAT_PARK_REPORTS, 1);
		fprintf(stderr, "pippin: coroutine %llu parked over %llu s on channels only it reaches: nothing but a cancel ends the park\n",
		        (unsigned long long)((const clj_coro *)nk->h)->id, (unsigned long long)(g->report_ns / 1000000000ull));
	}
	free(mark);
	free(wlo);
	free(wadj);
}

#if CLJ_DEBUG
// An overcount frees what is held (design §7, «Фаза 3», «Почему верно»); only a lost node's edges may be stale.
static void audit_overcount(const graph *g) {
	for (uint32_t i = 0; i < g->n; i++) {
		if (g->nodes[i].flags & N_LOST) return;
	}
	for (uint32_t i = 0; i < g->n; i++) {
		const node *nd = &g->nodes[i];
		if (nd->internal + nd->ours > nd->count)
			clj_ice("cc/overcount", NULL, "%u edges and %u of the collector's own into a %s of %u references", nd->internal, nd->ours, nd->h->type->name,
			        nd->count);
	}
}
#endif

static int64_t run(graph *g, clj_header **roots, size_t n) {
	if (g->shared && !g->deep && (g->report_ns = park_report_threshold())) g->now = clj_sched_now();
	for (size_t r = 0; r < n; r++) {
		uint32_t i = find(g, roots[r]);
		if (i == NONE) add_node(g, roots[r], g->shared ? 1 : 0, N_ROOT);
		else if (g->shared) g->nodes[i].ours++;
	}
	while (g->nw) visit(g, g->work[--g->nw]);
	judge_wakes(g);
	if (g->shared) validate(g);
#if CLJ_DEBUG
	audit_overcount(g);
#endif
	blacken(g);
	if (g->report_ns) park_report(g);
	if (!g->shared) mark_acyclic(g);
	int64_t freed = free_whites(g);
	release_survivors(g);
	stat_add(CLJ_CC_STAT_COLLECTIONS, 1);
	if (g->skipped) stat_add(CLJ_CC_STAT_SKIPPED, g->skipped);
	return freed;
}

// ---- the audit of a release that files nothing

#if CLJ_DEBUG
static _Thread_local uint32_t unfiled_tick;

// A cycle with a buffered member is a check past its budget, or a candidate: its collection flags the rest.
static void audit_component(graph *g, const uint32_t *members, uint32_t n, void *ctx) {
	(void)ctx;
	if (n == 1 && !self_edge(g, members[0])) return;
	for (uint32_t k = 0; k < n; k++) {
		if (g->nodes[members[k]].flags & N_BUFFERED) return;
	}
	for (uint32_t k = 0; k < n; k++) {
		const clj_header *h = g->nodes[members[k]].h;
		if (!(h->flags & CLJ_FLAG_ONCYCLE))
			clj_ice("cc/store-check-missed", NULL, "a %s on a cycle of %u unshared objects without CLJ_FLAG_ONCYCLE", h->type->name, n);
	}
}

// The walk a candidate made before the store check, without freeing: every live cycle it meets must be flagged.
void clj_cc_audit_unfiled(clj_header *h) {
	if (clj_opt_off(CLJ_NO_OPT_STORE_CHECK) || !clj_cc_enabled() || opaque(h->type) || ++unfiled_tick % audit_every) return;
	clj_coro *c = clj_coro_here();
	// A teardown runs: its whites are being finalized.
	if (!c || c->cc_collecting) return;
	graph g = {.shared = false};
	add_node(&g, h, 0, 0);
	while (g.nw && g.n < AUDIT_NODES) visit(&g, g.work[--g.nw]);
	for (uint32_t i = 0; i < g.n; i++) g.nodes[i].flags |= N_BLACK;
	black_components(&g, audit_component, NULL);
	graph_free(&g);
}
#else
void clj_cc_audit_unfiled(clj_header *h) { (void)h; }
#endif

int clj_debug_cc_on_cycle(clj_value v) { return clj_is_ptr(v) && (clj_header_of(v)->flags & CLJ_FLAG_ONCYCLE) != 0; }

// ---- the cycle check at a store (design §7, «Проверка цикла в момент записи»)

// Past either the owner waits for the next local collection, which walks without a bound. A realization's value
// reaches what its thunk walked, a whole index for a seq over one: its budget is small (bench/RESULTS.md).
enum { STORE_NODES = 256, STORE_READS = 4096, LAZY_NODES = 8, LAZY_READS = 64, STORE_TABLE = 4 * STORE_NODES };

// The nodes the walk from the value entered, 0 the owner (never entered), and the edges among them.
typedef struct {
	clj_header  *nodes[STORE_NODES + 1];
	uint64_t     table[STORE_TABLE]; // gen << 32 | index + 1
	uint32_t     work[STORE_NODES + 1];
	uint32_t    *from, *to;
	uint32_t     n, nw, ne, ecap, cur, reads, gen, max_nodes, max_reads;
	bool         over, found;
	checked_set *ck;
} store_walk;

static _Thread_local store_walk *store_w;

static uint32_t store_index(store_walk *w, clj_header *h) {
	for (uint32_t s = ck_slot(STORE_TABLE, h);; s = (s + 1) & (STORE_TABLE - 1)) {
		uint64_t e = w->table[s];
		if ((uint32_t)(e >> 32) != w->gen) {
			if (w->n == w->max_nodes + 1) {
				w->over = true;
				return NONE;
			}
			w->table[s] = (uint64_t)w->gen << 32 | (w->n + 1);
			w->nodes[w->n] = h;
			w->work[w->nw++] = w->n;
			return w->n++;
		}
		if (w->nodes[(uint32_t)e - 1] == h) return (uint32_t)e - 1;
	}
}

static bool local_enterable(const clj_header *c) { return enterable(&(graph){0}, c); }

static void store_visit(clj_value v, void *ctx) {
	store_walk *w = ctx;
	if (!clj_is_ptr(v) || w->over) return;
	if (++w->reads > w->max_reads) {
		w->over = true;
		return;
	}
	clj_header *c = clj_header_of(v);
	uint32_t    i = 0;
	if (c == w->nodes[0]) {
		w->found = true;
	} else {
		// A valid mark: nothing below lies on a cycle, and the store into a guarded owner voided every mark first.
		if (!local_enterable(c) || ck_valid(w->ck, c)) return;
		if ((i = store_index(w, c)) == NONE) return;
	}
	if (w->ne == w->ecap) {
		w->ecap = w->ecap ? w->ecap * 2 : 1024;
		w->from = realloc(w->from, w->ecap * sizeof *w->from);
		w->to = realloc(w->to, w->ecap * sizeof *w->to);
		if (!w->from || !w->to) clj_fatal("out of memory");
	}
	w->from[w->ne] = w->cur;
	w->to[w->ne++] = i;
}

// Every node with a path to the owner, backwards from it: with the new edge they and the owner are one component.
static uint32_t store_flag(store_walk *w) {
	uint32_t n = w->n, lo[STORE_NODES + 2] = {0}, adj_n = w->ne;
	uint32_t *adj = malloc(adj_n * sizeof *adj);
	if (!adj) clj_fatal("out of memory");
	for (uint32_t e = 0; e < w->ne; e++) lo[w->to[e] + 1]++;
	for (uint32_t i = 0; i < n; i++) lo[i + 1] += lo[i];
	uint32_t fill[STORE_NODES + 1];
	memcpy(fill, lo, n * sizeof *fill);
	for (uint32_t e = 0; e < w->ne; e++) adj[fill[w->to[e]]++] = w->from[e];
	bool     seen[STORE_NODES + 1] = {0};
	uint32_t stack[STORE_NODES + 1], sp = 0, flagged = 0;
	seen[0] = true;
	stack[sp++] = 0;
	while (sp) {
		uint32_t v = stack[--sp];
		w->nodes[v]->flags |= CLJ_FLAG_ONCYCLE;
		flagged++;
		for (uint32_t k = lo[v]; k < lo[v + 1]; k++) {
			if (seen[adj[k]]) continue;
			seen[adj[k]] = true;
			stack[sp++] = adj[k];
		}
	}
	free(adj);
	return flagged;
}

void clj_cc_store_check(clj_header *x, clj_header *y) {
	if (clj_opt_off(CLJ_NO_OPT_STORE_CHECK) || !clj_cc_enabled()) return;
	if (x == y) {
		x->flags |= CLJ_FLAG_ONCYCLE;
		stat_add(CLJ_CC_STAT_STORE_CYCLES, 1);
		return;
	}
	clj_coro *c = clj_coro_current();
	if (!store_w && !(store_w = calloc(1, sizeof *store_w))) clj_fatal("out of memory");
	store_walk *w = store_w;
	if (++w->gen == 0) {
		memset(w->table, 0, sizeof w->table);
		w->gen = 1;
	}
	w->n = w->nw = w->ne = w->reads = 0;
	w->over = w->found = false;
	bool lazy = x->type == &clj_lazy_seq_type;
	w->max_nodes = lazy ? LAZY_NODES : STORE_NODES;
	w->max_reads = lazy ? LAZY_READS : STORE_READS;
	w->ck = c->cc_local ? ck_current(c->cc_local) : NULL;
	w->nodes[w->n++] = x;
	store_index(w, y);
	while (w->nw && !w->over) {
		w->cur = w->work[--w->nw];
		clj_header *h = w->nodes[w->cur];
		if (h->type->each_child) h->type->each_child(h, store_visit, w);
	}
	stat_add(CLJ_CC_STAT_STORE_CHECKS, 1);
	stat_add(CLJ_CC_STAT_STORE_NODES, w->n - 1);
	if (w->over) {
		// The slot is not written yet: the owner is filed, never collected here.
		stat_add(CLJ_CC_STAT_STORE_DEFERRED, 1);
		file_local(c, x);
	} else if (w->found) {
		stat_add(CLJ_CC_STAT_STORE_CYCLES, 1);
		stat_add(CLJ_CC_STAT_STORE_FLAGGED, store_flag(w));
	}
}

// ---- collections

static pthread_mutex_t collect_mu = PTHREAD_MUTEX_INITIALIZER;

// An entry of the shared buffer holds no reference: a zombie gives its cell back, an object on its way to zero waits
// for the next round, a live one is held by the collector for the walk. Under `active`, so its cell stays put.
static bool shared_intake(clj_header *h, cand_vec *later) {
	if (atomic_load_explicit(&h->rc, memory_order_acquire) == CLJ_RC_ZOMBIE) {
		clj_dealloc_cell(h);
		return false;
	}
	if (!try_retain(h)) {
		vec_push(later, h);
		return false;
	}
	if ((h->flags & CLJ_FLAG_IMMORTAL) || opaque(h->type)) {
		atomic_fetch_and_explicit(&h->rc, ~CLJ_RC_BUFFERED, memory_order_seq_cst);
		clj_rc_drop(h, false);
		return false;
	}
	return true;
}

static int64_t collect_shared(void) {
	pthread_mutex_lock(&collect_mu);
	pthread_mutex_lock(&buf_mu);
	chunk *taken = shared_head;
	shared_head = NULL;
	shared_n = 0;
	shared_fresh = 0;
	pthread_mutex_unlock(&buf_mu);
	cand_vec entries = {0};
	chunks_drain(taken, &entries);
	int64_t  freed = 0;
	cand_vec later = {0};
	if (entries.n) {
		activate();
		size_t n = 0;
		for (size_t i = 0; i < entries.n; i++) {
			if (shared_intake(entries.items[i], &later)) entries.items[n++] = entries.items[i];
		}
		graph g = {.shared = true};
		if (n) freed = run(&g, entries.items, n);
		graph_free(&g);
		deactivate();
	}
	for (size_t i = 0; i < later.n; i++) shared_push(later.items[i], false);
	free(later.items);
	free(entries.items);
	pthread_mutex_unlock(&collect_mu);
	return freed;
}

// ---- the deep walk of a replaced var root (design §7, «Сборщик циклов: как он устроен», the var's root)

static uint64_t retry_period(uint32_t attempt) {
	return DEEP_RETRY_BASE_NS << (attempt < DEEP_RETRY_MAX_SHIFT ? attempt : DEEP_RETRY_MAX_SHIFT);
}

// A root of the deep graph holds one collector reference: the intake's try-retain, or the entry's own.
static void deep_root(graph *g, clj_header *h, uint8_t flag, uint32_t attempt) {
	uint32_t i = find(g, h);
	if (i == NONE) {
		i = add_node(g, h, 1, flag);
	} else {
		g->nodes[i].ours++;
		g->nodes[i].flags |= flag;
	}
	if (attempt > g->nodes[i].attempt) g->nodes[i].attempt = (uint8_t)(attempt < UINT8_MAX ? attempt : UINT8_MAX);
}

static int cmp_ptr(const void *a, const void *b) {
	uintptr_t x = (uintptr_t)*(clj_header *const *)a, y = (uintptr_t)*(clj_header *const *)b;
	return x < y ? -1 : x > y;
}

static bool is_waiting(clj_header *h, clj_header **waiting, size_t n) {
	return n && bsearch(&h, waiting, n, sizeof *waiting, cmp_ptr);
}

static void retry_node(graph *g, uint32_t i, retry_vec *out) {
	atomic_fetch_add_explicit(&g->nodes[i].h->rc, 1, memory_order_relaxed);
	retry_push(out, (retry){.h = g->nodes[i].h, .attempt = g->nodes[i].attempt});
	stat_add(CLJ_CC_STAT_DEEP_RETRIES, 1);
}

// A black component with a cycle: no release files it when its last outside reference goes (no REACH), a retry does.
static void ring_found(graph *g, const uint32_t *members, uint32_t n, clj_header **waiting, size_t nwaiting, retry_vec *out) {
	const node *first = &g->nodes[members[0]];
	bool        cyclic = n > 1;
	for (uint32_t e = 0; !cyclic && e < first->edge_n; e++) cyclic = g->edges[first->edge_lo + e] == members[0];
	if (!cyclic) return;
	uint32_t best = members[0];
	bool     skip = false, unseen = false;
	for (uint32_t k = 0; k < n; k++) {
		node *nd = &g->nodes[members[k]];
		nd->flags |= N_CYCLIC;
		if ((nd->flags & N_CUT) || is_waiting(nd->h, waiting, nwaiting)) skip = true;
		// A ring all REACH files itself on its last outside release; the retry's reference would only delay a broken one.
		if (!(nd->h->flags & CLJ_FLAG_REACH)) unseen = true;
		if (nd->attempt > g->nodes[best].attempt) best = members[k];
	}
	if (unseen && !skip) retry_node(g, best, out);
}

typedef struct {
	clj_header **waiting;
	size_t       nwaiting;
	retry_vec   *out;
} rings_ctx;

static void ring_component(graph *g, const uint32_t *members, uint32_t n, void *ctx) {
	rings_ctx *r = ctx;
	ring_found(g, members, n, r->waiting, r->nwaiting, r->out);
}

static void retry_rings(graph *g, clj_header **waiting, size_t nwaiting, retry_vec *out) {
	rings_ctx r = {waiting, nwaiting, out};
	black_components(g, ring_component, &r);
}

// While the collector's references hold every black node. A root a mutator touched mid-walk is filed again, with the
// bit and no reference, as the shared collection does: the touch may have been its last outside reference going.
static void retry_deep(graph *g, clj_header **waiting, size_t nwaiting, retry_vec *out, cand_vec *refile) {
	retry_rings(g, waiting, nwaiting, out);
	for (uint32_t i = 0; i < g->n; i++) {
		const node *nd = &g->nodes[i];
		if (!(nd->flags & (N_ROOT | N_HELD)) || !(nd->flags & N_LOST) || (nd->flags & (N_CUT | N_CYCLIC))) continue;
		uint32_t cur = atomic_load_explicit(&nd->h->rc, memory_order_relaxed);
		while (!(cur & CLJ_RC_BUFFERED)) {
			if (atomic_compare_exchange_weak_explicit(&nd->h->rc, &cur, cur | CLJ_RC_BUFFERED, memory_order_seq_cst,
			                                          memory_order_relaxed)) {
				stat_add(CLJ_CC_STAT_INTERFERED, 1);
				vec_push(refile, nd->h);
				break;
			}
		}
	}
}

// One graph for the batch: what a reload's defs share is walked once.
static int64_t collect_deep(bool force) {
	pthread_mutex_lock(&collect_mu);
	pthread_mutex_lock(&buf_mu);
	chunk *taken = deep_head;
	deep_head = NULL;
	deep_n = 0;
	deep_first_at = 0;
	uint64_t     now = wall_ns();
	retry_vec    due = {0};
	clj_header **waiting = retries.n ? malloc(retries.n * sizeof *waiting) : NULL;
	size_t       nwaiting = 0, kept = 0;
	if (retries.n && !waiting) clj_fatal("out of memory");
	for (size_t i = 0; i < retries.n; i++) {
		retry r = retries.items[i];
		if (force || r.at <= now) {
			retry_push(&due, r);
		} else {
			waiting[nwaiting++] = r.h;
			retries.items[kept++] = r;
		}
	}
	retries.n = kept;
	pthread_mutex_unlock(&buf_mu);
	cand_vec entries = {0};
	chunks_drain(taken, &entries);
	int64_t   freed = 0;
	cand_vec  later = {0};
	retry_vec found = {0};
	if (entries.n || due.n) {
		uint64_t t0 = clj_profile_now();
		activate();
		graph g = {.shared = true, .deep = true};
		for (size_t i = 0; i < entries.n; i++) {
			if (shared_intake(entries.items[i], &later)) deep_root(&g, entries.items[i], N_ROOT, 0);
		}
		for (size_t i = 0; i < due.n; i++) deep_root(&g, due.items[i].h, N_HELD, due.items[i].attempt + 1);
		if (nwaiting) qsort(waiting, nwaiting, sizeof *waiting, cmp_ptr);
		while (g.nw) visit(&g, g.work[--g.nw]);
		validate(&g);
		blacken(&g);
		retry_deep(&g, waiting, nwaiting, &found, &later);
		freed = free_whites(&g);
		release_survivors(&g);
		stat_add(CLJ_CC_STAT_COLLECTIONS, 1);
		if (g.cut) stat_add(CLJ_CC_STAT_DEEP_CUT, 1);
		graph_free(&g);
		deactivate();
		uint64_t took = clj_profile_now() - t0;
		pthread_mutex_lock(&buf_mu);
		uint64_t end = wall_ns();
		deep_not_before = end + DEEP_DUTY * took;
		for (size_t i = 0; i < found.n; i++) {
			found.items[i].at = end + retry_period(found.items[i].attempt);
			retry_push(&retries, found.items[i]);
		}
		// The background thread may sleep with nothing due: a forced collection on another thread filed these.
		if (found.n) pthread_cond_signal(&buf_cv);
		pthread_mutex_unlock(&buf_mu);
	}
	for (size_t i = 0; i < later.n; i++) deep_push(later.items[i]);
	free(later.items);
	free(entries.items);
	free(due.items);
	free(found.items);
	free(waiting);
	pthread_mutex_unlock(&collect_mu);
	return freed;
}

// Up to max of the running execution's candidates, the most recent first. One published since it was buffered
// goes to the shared buffer, which takes over its reference.
// mark: the execution goes on, so marks this collection sets may serve a later one.
static int64_t collect_local(clj_coro *c, size_t max, bool mark) {
	cand_vec *b = buf_of(c);
	if (!b || !b->n || c->cc_collecting) return 0;
	checked_set *ck = ck_current(c->cc_local);
	size_t       k = b->n < max ? b->n : max;
	clj_header **roots = malloc(k * sizeof *roots);
	if (!roots) clj_fatal("out of memory");
	size_t  n = 0;
	int64_t skipped = 0;
	for (size_t i = 0; i < k; i++) {
		clj_header *h = b->items[--b->n];
		uint32_t    rc = atomic_load_explicit(&h->rc, memory_order_acquire);
		if (rc == CLJ_RC_ZOMBIE) {
			clj_dealloc_cell(h);
		} else if ((h->flags & CLJ_FLAG_SHARED) || !(rc & CLJ_RC_COUNT_MASK)) {
			shared_push(h, true);
		} else if ((h->flags & CLJ_FLAG_IMMORTAL) || opaque(h->type)) {
			CLJ_RC_UNSHARED_STORE(h, rc & ~CLJ_RC_BUFFERED);
		} else if (ck_valid(ck, h)) {
			// On no cycle, so no member of a garbage one.
			CLJ_RC_UNSHARED_STORE(h, rc & ~CLJ_RC_BUFFERED);
			audit_skip(h);
			skipped++;
		} else {
			roots[n++] = h;
		}
	}
	if (skipped) stat_add(CLJ_CC_STAT_SKIPPED, skipped);
	int64_t freed = 0;
	if (n) {
		c->cc_collecting = true;
		graph g = {.shared = false, .ck = ck, .mark = mark && ck};
		freed = run(&g, roots, n);
		graph_free(&g);
		c->cc_collecting = false;
	}
	free(roots);
	return freed;
}

static void hand_off_all(clj_coro *c) {
	cand_vec *b = buf_of(c);
	while (b && b->n) hand_off(b->items[--b->n], clj_share);
}

int64_t clj_cc_collect(void) {
	clj_coro *c = clj_coro_current();
	int64_t   freed = 0;
	clj_coro_cc_file_parked();
	for (int round = 0; round < COLLECT_ROUNDS; round++) {
		int64_t got = 0;
		while (buf_of(c) && buf_of(c)->n && !c->cc_collecting) got += collect_local(c, SIZE_MAX, true);
		got += collect_shared();
		got += collect_deep(true);
		freed += got;
		// A round that frees nothing made no new candidates; what is left are roots a mutator holds, retried later.
		if (!got) break;
	}
	return freed;
}

void clj_cc_seed_collect(void) {
	pthread_mutex_lock(&buf_mu);
	bool pending = shared_n || deep_n || retries.n;
	pthread_mutex_unlock(&buf_mu);
	if (pending) clj_cc_collect();
}

void clj_cc_main_idle(void) {
	clj_coro *c = clj_coro_here();
	if (!c || !c->cc_local || c->cc_collecting) return;
	uint64_t start = clj_profile_now();
	while (buf_of(c)->n && clj_profile_now() - start < MAIN_IDLE_BUDGET_NS) collect_local(c, MAIN_IDLE_SLICE, true);
}

// ---- the store check's census (design §7, «Проверка цикла в момент записи»), -DCLJ_STATS builds only

#if CLJ_STATS
enum { SC_CAP = 1024, SC_LONG = 64, SC_SEEN = 4 * SC_CAP };

typedef struct {
	const clj_header *x;
	struct {
		const clj_header *h;
		uint32_t          gen;
	} seen[SC_SEEN];
	clj_header *stack[SC_CAP];
	uint32_t    gen, nstack, nodes;
	bool        found, capped;
} sc_walk;

static _Thread_local sc_walk *sc_w;

static bool sc_enterable(const clj_header *h) {
	return (h->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL | CLJ_FLAG_REACH_LOCAL)) == CLJ_FLAG_REACH_LOCAL && !opaque(h->type);
}

static bool sc_first_visit(sc_walk *w, const clj_header *h) {
	for (uint32_t i = ck_slot(SC_SEEN, h);; i = (i + 1) & (SC_SEEN - 1)) {
		if (w->seen[i].gen != w->gen) {
			w->seen[i].h = h;
			w->seen[i].gen = w->gen;
			return true;
		}
		if (w->seen[i].h == h) return false;
	}
}

static void sc_visit(clj_value v, void *ctx) {
	sc_walk *w = ctx;
	if (!clj_is_ptr(v) || w->capped) return;
	clj_header *c = clj_header_of(v);
	if (c == w->x) {
		w->found = true;
		return;
	}
	if (!sc_enterable(c) || !sc_first_visit(w, c)) return;
	if (w->nodes + w->nstack >= SC_CAP) {
		w->capped = true;
		return;
	}
	w->stack[w->nstack++] = c;
}

// The walk a check would run, with no acyclic mark skipped and to the end: the whole SCC of the owner is flagged.
void clj_cc_store_census(const clj_header *owner, uint32_t oflags, clj_header *child, uint32_t cflags) {
	if (oflags & CLJ_FLAG_SHARED) {
		if (oflags & CLJ_FLAG_MUTABLE) {
			CLJ_STAT(CLJ_STAT_SC_SHARED);
			if (cflags & CLJ_FLAG_REACH) CLJ_STAT(CLJ_STAT_SC_SHARED_REACH);
		}
		return;
	}
	if ((oflags | cflags) & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL)) return;
	CLJ_STAT(CLJ_STAT_SC_EDGES);
	if (!(cflags & CLJ_FLAG_HEAPREF)) CLJ_STAT(CLJ_STAT_SC_HEAPREF_SET);
	if (oflags & CLJ_FLAG_MUTABLE) CLJ_STAT(CLJ_STAT_SC_MUT);
	bool self = child == owner;
	if (!(oflags & CLJ_FLAG_HEAPREF) && !self) return;
	CLJ_STAT((oflags & CLJ_FLAG_MUTABLE) ? CLJ_STAT_SC_MUT_HEAPREF : CLJ_STAT_SC_IMM_HEAPREF);
	// The check's own rule: an immutable owner is edited unique (a transient map's edits are left out here).
	if (!(oflags & CLJ_FLAG_MUTABLE) || !(cflags & CLJ_FLAG_REACH_LOCAL) || opaque(child->type)) return;
	CLJ_STAT(CLJ_STAT_SC_WALKS);
	if (owner->type == &clj_lazy_seq_type) CLJ_STAT(CLJ_STAT_SC_WALKS_LAZY);
	if (!sc_w && !(sc_w = calloc(1, sizeof *sc_w))) clj_fatal("out of memory");
	sc_walk *w = sc_w;
	w->x = owner;
	w->gen++;
	w->nstack = w->nodes = 0;
	w->found = self;
	w->capped = false;
	if (!self) {
		sc_first_visit(w, child);
		w->stack[w->nstack++] = child;
	}
	while (w->nstack && !w->capped) {
		clj_header *h = w->stack[--w->nstack];
		w->nodes++;
		if (h->type->each_child) h->type->each_child(h, sc_visit, w);
	}
	CLJ_STAT_ADD(CLJ_STAT_SC_WALK_NODES, w->nodes);
	if (w->nodes > SC_LONG) CLJ_STAT(CLJ_STAT_SC_WALK_LONG);
	if (w->capped) CLJ_STAT(CLJ_STAT_SC_WALK_CAPPED);
	if (w->found) CLJ_STAT(CLJ_STAT_SC_CYCLES);
}
#endif

// ---- executions: transfers, ends

void *clj_cc_local_borrow(clj_coro *owner) {
	clj_coro *host = clj_coro_current();
	void     *saved = host->cc_local;
	host->cc_local = owner->cc_local;
	owner->cc_local = NULL;
	return saved;
}

void clj_cc_local_return(clj_coro *owner, void *saved) {
	clj_coro *host = clj_coro_current();
	owner->cc_local = host->cc_local;
	host->cc_local = saved;
}

// Runs with the finished execution's buffer borrowed by the carrier's own.
void clj_cc_execution_done(clj_coro *c) {
	(void)c;
	clj_coro *host = clj_coro_current();
	if (on_main(host)) {
		hand_off_all(host);
		return;
	}
	for (int round = 0; round < COLLECT_ROUNDS && buf_of(host) && buf_of(host)->n; round++)
		collect_local(host, SIZE_MAX, false);
	hand_off_all(host);
}

void clj_cc_execution_free(clj_coro *c) {
	local_state *ls = c->cc_local;
	if (!ls) return;
	while (ls->buf.n) hand_off(ls->buf.items[--ls->buf.n], clj_share_unowned);
	free(ls->buf.items);
	stat_add(CLJ_CC_STAT_UNFILED, ls->unfiled);
	free(ls->ck.slots);
	free(ls->ck.recent);
	free(ls);
	c->cc_local = NULL;
}

// ---- debug and tests

void clj_debug_cc_stats(int64_t out[CLJ_CC_STAT_COUNT]) {
	for (int i = 0; i < CLJ_CC_STAT_COUNT; i++) out[i] = atomic_load_explicit(&stats[i], memory_order_relaxed);
	clj_coro *c = clj_coro_here();
	if (c && c->cc_local) out[CLJ_CC_STAT_UNFILED] += ((local_state *)c->cc_local)->unfiled;
}

int64_t clj_debug_cc_pending_local(void) {
	clj_coro *c = clj_coro_current();
	return buf_of(c) ? (int64_t)buf_of(c)->n : 0;
}

int64_t clj_debug_cc_pending_shared(void) {
	pthread_mutex_lock(&buf_mu);
	int64_t n = (int64_t)shared_n;
	pthread_mutex_unlock(&buf_mu);
	return n;
}

int64_t clj_debug_cc_deep_filed_here(void) { return deep_filed_here; }

int64_t clj_debug_cc_deep_retries(void) {
	pthread_mutex_lock(&buf_mu);
	int64_t n = (int64_t)retries.n;
	pthread_mutex_unlock(&buf_mu);
	return n;
}

void clj_debug_cc_as_main(int on) { as_main = on != 0; }

int64_t clj_debug_cc_main_idle(void) {
	int64_t before = clj_debug_cc_pending_local();
	clj_cc_main_idle();
	return before - clj_debug_cc_pending_local();
}

int64_t clj_debug_cc_collect_local(void) { return collect_local(clj_coro_current(), SIZE_MAX, true); }

void clj_debug_cc_file_local(clj_value v) {
	if (!clj_is_ptr(v)) return;
	clj_header *h = clj_header_of(v);
	if ((h->flags & (CLJ_FLAG_SHARED | CLJ_FLAG_IMMORTAL | CLJ_FLAG_REACH_LOCAL)) != CLJ_FLAG_REACH_LOCAL) return;
	uint32_t rc = CLJ_RC_UNSHARED_LOAD(h);
	if (rc & CLJ_RC_BUFFERED) return;
	CLJ_RC_UNSHARED_STORE(h, rc | CLJ_RC_BUFFERED);
	clj_cc_local_candidate(h);
}

int clj_debug_cc_acyclic(clj_value v) {
	clj_coro *c = clj_coro_current();
	if (!clj_is_ptr(v) || !c->cc_local) return 0;
	return ck_valid(ck_current(c->cc_local), clj_header_of(v));
}
