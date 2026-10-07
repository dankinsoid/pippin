// @ai-generated(solo)
// Trial deletion over the candidates RC leaves: design §7, «Сборщик циклов: как он устроен».
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "alloc.h"
#include "cc_internal.h"
#include "clj/coro.h"
#include "profile_internal.h"
#include "clj/proto.h"
#include "coro_internal.h"

enum {
	SHARED_THRESHOLD = 1024,
	SHARED_DELAY_MS = 500,
	LOCAL_THRESHOLD = 256,
	MAIN_BOUND = 4096,
	MAIN_IDLE_SLICE = 64,
	COLLECT_ROUNDS = 16,
};
static const uint64_t MAIN_IDLE_BUDGET_NS = 1000000;

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

static int64_t collect_shared(void);

static void *cc_main(void *arg) {
	(void)arg;
	pthread_mutex_lock(&buf_mu);
	for (;;) {
		while (!shared_fresh) pthread_cond_wait(&buf_cv, &buf_mu);
		struct timespec at;
		clock_gettime(CLOCK_REALTIME, &at);
		at.tv_nsec += (long)SHARED_DELAY_MS * 1000000L;
		at.tv_sec += at.tv_nsec / 1000000000L;
		at.tv_nsec %= 1000000000L;
		while (shared_fresh && shared_fresh < SHARED_THRESHOLD) {
			if (pthread_cond_timedwait(&buf_cv, &buf_mu, &at) != 0) break;
		}
		pthread_mutex_unlock(&buf_mu);
		collect_shared();
		pthread_mutex_lock(&buf_mu);
	}
	return NULL;
}

static void start_thread(void) {
	pthread_t      t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&t, &attr, cc_main, NULL) != 0) clj_fatal("pthread_create of the cycle collector failed");
	pthread_attr_destroy(&attr);
}

// fresh: a new candidate, which may wake the thread; a retry waits for the next fresh one.
static void shared_push(clj_header *h, bool fresh) {
	pthread_once(&thread_once, start_thread);
	pthread_mutex_lock(&buf_mu);
	if (!shared_head || shared_head->n == CHUNK) {
		chunk *c = spare;
		spare = NULL;
		if (!c && !(c = malloc(sizeof *c))) clj_fatal("out of memory");
		c->n = 0;
		c->next = shared_head;
		shared_head = c;
	}
	shared_head->items[shared_head->n++] = h;
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
	shared_push(h, true);
	return true;
}

// While set, a shared object at zero stays whole: a pointer read without a lock never meets returned memory.
_Atomic bool clj_cc_running;
static pthread_mutex_t defer_mu = PTHREAD_MUTEX_INITIALIZER;
static cand_vec        deferred;

bool clj_cc_defer_free(clj_header *h) {
	if (!atomic_load_explicit(&clj_cc_running, memory_order_seq_cst)) return false;
	pthread_mutex_lock(&defer_mu);
	bool on = atomic_load_explicit(&clj_cc_running, memory_order_relaxed);
	if (on) vec_push(&deferred, h);
	pthread_mutex_unlock(&defer_mu);
	return on;
}

// The barrier pairs with the collector's activate-then-read: design §7, the lock-free protocol. Reading `active`
// first keeps a contended owner's header (an atom swapped by many threads) out of its critical section.
void clj_cc_note_store(clj_header *owner) {
	if (!owner->type->cc_locked) atomic_thread_fence(memory_order_seq_cst);
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
	for (size_t i = 0; i < dead.n; i++) clj_rc_free(dead.items[i]);
	free(dead.items);
}

// ---- the local buffer of an execution

static _Thread_local bool as_main;

static bool on_main(const clj_coro *c) { return as_main || (c->carrier && c->carrier->is_main); }

static cand_vec *local_of(clj_coro *c) {
	if (!c->cc_local) {
		c->cc_local = calloc(1, sizeof(cand_vec));
		if (!c->cc_local) clj_fatal("out of memory");
	}
	return c->cc_local;
}

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

static int64_t collect_local(clj_coro *c, size_t max);

void clj_cc_local_candidate(clj_header *h) {
	clj_coro *c = clj_coro_current();
	cand_vec *b = local_of(c);
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
		collect_local(c, SIZE_MAX);
	}
}

// ---- the graph of one collection

// N_BUFFERED: an entry elsewhere names it, so a white one stays as a zombie for that entry.
enum { N_ROOT = 1, N_LOST = 2, N_BLACK = 4, N_WHITE = 8, N_BUFFERED = 16 };
#define NONE UINT32_MAX

typedef struct {
	clj_header *h;
	uint32_t    count;    // the count when watched, the collector's own references included
	uint32_t    internal; // recorded edges into it
	uint32_t    ours;     // references the collector holds
	uint32_t    edge_lo, edge_n;
	uint8_t     flags;
} node;

typedef struct {
	bool      shared;
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

static uint32_t add_node(graph *g, clj_header *h, uint32_t ours, uint8_t flags) {
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

static void graph_free(graph *g) {
	free(g->nodes);
	free(g->edges);
	free(g->table);
	free(g->work);
}

// Registries hold execs and descriptors without a reference; a coroutine's frames are invisible (design §7).
static bool opaque(const clj_type *t) { return t->unlink || t == &clj_type_type || t == &clj_coro_type; }

static bool enterable(const graph *g, const clj_header *c) {
	uint32_t f = c->flags;
	if (f & CLJ_FLAG_IMMORTAL) return false;
	if (g->shared ? (f & (CLJ_FLAG_SHARED | CLJ_FLAG_REACH)) != (CLJ_FLAG_SHARED | CLJ_FLAG_REACH)
	              : (f & (CLJ_FLAG_SHARED | CLJ_FLAG_REACH_LOCAL)) != CLJ_FLAG_REACH_LOCAL)
		return false;
	return !opaque(c->type);
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
	if (!enterable(g, c)) return;
	uint32_t i = find(g, c);
	if (i == NONE) {
		if (g->shared) {
			if (g->lockfree) {
				clj_header *owner = g->nodes[g->cur].h;
				if (!try_retain(c)) {
					g->cur_failed = true;
					return;
				}
				// The owner kept its watch past the retain: the slot still held c, so no one holds c uniquely.
				if (!(atomic_load_explicit(&owner->rc, memory_order_seq_cst) & CLJ_RC_WATCH)) {
					clj_rc_drop(c);
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

static void visit_locked(void *self, void *ctx) {
	graph *g = ctx;
	watch(g, g->cur);
	clj_header *h = self;
	if (h->type->each_child) h->type->each_child(h, visit_child, g);
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
		if (t->each_child) t->each_child(h, visit_child, g);
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

static void blacken(graph *g) {
	g->nw = 0;
	for (uint32_t i = 0; i < g->n; i++) {
		node *nd = &g->nodes[i];
		if ((nd->flags & N_LOST) || nd->count > nd->internal + nd->ours) {
			nd->flags |= N_BLACK;
			work_push(g, i);
		}
	}
	while (g->nw) {
		node *nd = &g->nodes[g->work[--g->nw]];
		for (uint32_t e = 0; e < nd->edge_n; e++) {
			uint32_t j = g->edges[nd->edge_lo + e];
			if (!(g->nodes[j].flags & N_BLACK)) {
				g->nodes[j].flags |= N_BLACK;
				work_push(g, j);
			}
		}
	}
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
		}
	}
	if (!freed) return 0;
	for (uint32_t i = 0; i < g->n; i++) {
		clj_header *h = g->nodes[i].h;
		if ((g->nodes[i].flags & N_WHITE) && h->type->each_child) h->type->each_child(h, release_outside, g);
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
// is filed again: the touch may have been its last outside reference going.
static void release_survivors(graph *g) {
	for (uint32_t i = 0; i < g->n; i++) {
		node *nd = &g->nodes[i];
		if (nd->flags & N_WHITE) continue;
		if (g->shared && (nd->flags & (N_ROOT | N_LOST)) == (N_ROOT | N_LOST)) {
			uint32_t cur = atomic_load_explicit(&nd->h->rc, memory_order_relaxed);
			while (!(cur & CLJ_RC_BUFFERED)) {
				if (atomic_compare_exchange_weak_explicit(&nd->h->rc, &cur, cur | CLJ_RC_BUFFERED, memory_order_seq_cst,
				                                          memory_order_relaxed)) {
					stat_add(CLJ_CC_STAT_INTERFERED, 1);
					shared_push(nd->h, false);
					break;
				}
			}
		}
		for (uint32_t k = nd->ours; k; k--) clj_rc_drop(nd->h);
	}
}

static int64_t run(graph *g, clj_header **roots, size_t n) {
	for (size_t r = 0; r < n; r++) {
		uint32_t i = find(g, roots[r]);
		if (i == NONE) add_node(g, roots[r], g->shared ? 1 : 0, N_ROOT);
		else if (g->shared) g->nodes[i].ours++;
	}
	while (g->nw) visit(g, g->work[--g->nw]);
	if (g->shared) validate(g);
	blacken(g);
	int64_t freed = free_whites(g);
	release_survivors(g);
	stat_add(CLJ_CC_STAT_COLLECTIONS, 1);
	return freed;
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
		clj_rc_drop(h);
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
	while (taken) {
		chunk *c = taken;
		taken = c->next;
		for (size_t i = 0; i < c->n; i++) vec_push(&entries, c->items[i]);
		pthread_mutex_lock(&buf_mu);
		if (!spare) {
			spare = c;
			c = NULL;
		}
		pthread_mutex_unlock(&buf_mu);
		free(c);
	}
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

// Up to max of the running execution's candidates, the most recent first. One published since it was buffered
// goes to the shared buffer, which takes over its reference.
static int64_t collect_local(clj_coro *c, size_t max) {
	cand_vec *b = c->cc_local;
	if (!b || !b->n || c->cc_collecting) return 0;
	size_t       k = b->n < max ? b->n : max;
	clj_header **roots = malloc(k * sizeof *roots);
	if (!roots) clj_fatal("out of memory");
	size_t n = 0;
	for (size_t i = 0; i < k; i++) {
		clj_header *h = b->items[--b->n];
		uint32_t    rc = atomic_load_explicit(&h->rc, memory_order_acquire);
		if (rc == CLJ_RC_ZOMBIE) clj_dealloc_cell(h);
		else if ((h->flags & CLJ_FLAG_SHARED) || !(rc & CLJ_RC_COUNT_MASK)) shared_push(h, true);
		else if ((h->flags & CLJ_FLAG_IMMORTAL) || opaque(h->type)) CLJ_RC_UNSHARED_STORE(h, rc & ~CLJ_RC_BUFFERED);
		else roots[n++] = h;
	}
	int64_t freed = 0;
	if (n) {
		c->cc_collecting = true;
		graph g = {.shared = false};
		freed = run(&g, roots, n);
		graph_free(&g);
		c->cc_collecting = false;
	}
	free(roots);
	return freed;
}

static void hand_off_all(clj_coro *c) {
	cand_vec *b = c->cc_local;
	while (b && b->n) hand_off(b->items[--b->n], clj_share);
}

int64_t clj_cc_collect(void) {
	clj_coro *c = clj_coro_current();
	int64_t   freed = 0;
	for (int round = 0; round < COLLECT_ROUNDS; round++) {
		int64_t got = 0;
		while (c->cc_local && ((cand_vec *)c->cc_local)->n && !c->cc_collecting) got += collect_local(c, SIZE_MAX);
		got += collect_shared();
		freed += got;
		pthread_mutex_lock(&buf_mu);
		bool more = shared_n > 0;
		pthread_mutex_unlock(&buf_mu);
		if (!got && !more && !(c->cc_local && ((cand_vec *)c->cc_local)->n)) break;
	}
	return freed;
}

void clj_cc_main_idle(void) {
	clj_coro *c = clj_coro_tls;
	if (!c || !c->cc_local || c->cc_collecting) return;
	uint64_t start = clj_profile_now();
	while (((cand_vec *)c->cc_local)->n && clj_profile_now() - start < MAIN_IDLE_BUDGET_NS) collect_local(c, MAIN_IDLE_SLICE);
}

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
	for (int round = 0; round < COLLECT_ROUNDS && host->cc_local && ((cand_vec *)host->cc_local)->n; round++)
		collect_local(host, SIZE_MAX);
	hand_off_all(host);
}

void clj_cc_execution_free(clj_coro *c) {
	cand_vec *b = c->cc_local;
	if (!b) return;
	while (b->n) hand_off(b->items[--b->n], clj_share_unowned);
	free(b->items);
	free(b);
	c->cc_local = NULL;
}

// ---- debug and tests

void clj_debug_cc_stats(int64_t out[CLJ_CC_STAT_COUNT]) {
	for (int i = 0; i < CLJ_CC_STAT_COUNT; i++) out[i] = atomic_load_explicit(&stats[i], memory_order_relaxed);
}

int64_t clj_debug_cc_pending_local(void) {
	clj_coro *c = clj_coro_current();
	return c->cc_local ? (int64_t)((cand_vec *)c->cc_local)->n : 0;
}

int64_t clj_debug_cc_pending_shared(void) {
	pthread_mutex_lock(&buf_mu);
	int64_t n = (int64_t)shared_n;
	pthread_mutex_unlock(&buf_mu);
	return n;
}

void clj_debug_cc_as_main(int on) { as_main = on != 0; }

int64_t clj_debug_cc_main_idle(void) {
	int64_t before = clj_debug_cc_pending_local();
	clj_cc_main_idle();
	return before - clj_debug_cc_pending_local();
}
