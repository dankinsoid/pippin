// @ai-generated(solo)
#include <stdlib.h>

#include "clj/cmutex.h"
#include "clj/error.h"
#include "clj/lock.h"
#include "coro_internal.h"

// ---- the parking lot: waiters keyed by address in locked buckets, as parking_lot and futexes do

typedef struct lot_node {
	clj_waiter      *w;
	const void      *key;
	struct lot_node *next;
} lot_node;

typedef struct {
	clj_lock  lock;
	lot_node *head, *tail;
} lot_bucket;

enum { LOT_BUCKETS = 256 };

static lot_bucket buckets[LOT_BUCKETS];

static lot_bucket *bucket_of(const void *key) {
	uintptr_t h = (uintptr_t)key;
	h ^= h >> 17;
	h *= 0x9E3779B97F4A7C15ull;
	return &buckets[(h >> 40) & (LOT_BUCKETS - 1)];
}

// A wait on a bare thread or under a host call blocks the thread (the hybrid of design §4); never cancellable.
static clj_waiter *waiter_here(void) {
	clj_coro   *c = clj_coro_current();
	clj_waiter *w = clj_waiter_new(c, CLJ_NIL);
	w->blocking = c->host_depth > 0;
	return w;
}

static void enqueue_locked(lot_bucket *b, clj_waiter *w, const void *key) {
	lot_node *n = malloc(sizeof *n);
	if (!n) clj_fatal("out of memory");
	clj_waiter_retain(w);
	n->w = w;
	n->key = key;
	n->next = NULL;
	if (b->tail) b->tail->next = n;
	else b->head = n;
	b->tail = n;
}

// The first node for key, unlinked; the caller owns the queue's reference on its waiter.
static lot_node *dequeue_locked(lot_bucket *b, const void *key, bool *more) {
	lot_node *found = NULL, **at = &b->head, *prev = NULL;
	*more = false;
	while (*at) {
		lot_node *n = *at;
		if (n->key == key) {
			if (!found) {
				found = n;
				*at = n->next;
				if (b->tail == n) b->tail = prev;
				continue;
			}
			*more = true;
			break;
		}
		prev = n;
		at = &n->next;
	}
	return found;
}

void clj_lot_park(const void *key, bool (*wait_if)(const void *key, void *ctx), void *ctx) {
	lot_bucket *b = bucket_of(key);
	clj_lock_lock(&b->lock);
	if (!wait_if(key, ctx)) {
		clj_lock_unlock(&b->lock);
		return;
	}
	clj_waiter *w = waiter_here();
	enqueue_locked(b, w, key);
	clj_lock_unlock(&b->lock);
	clj_park_uncancellable(w);
	clj_waiter_release(w);
}

void clj_lot_unpark_all(const void *key) {
	lot_bucket *b = bucket_of(key);
	for (;;) {
		clj_lock_lock(&b->lock);
		bool      more;
		lot_node *n = dequeue_locked(b, key, &more);
		clj_lock_unlock(&b->lock);
		if (!n) return;
		if (clj_waiter_claim(n->w)) clj_resume(n->w);
		clj_waiter_release(n->w);
		free(n);
	}
}

// ---- the mutex

enum { SPINS = 64 };

static inline void pause(void) {
#if defined(__aarch64__)
	__asm__ volatile("yield");
#elif defined(__x86_64__)
	__asm__ volatile("pause");
#endif
}

// The holder is usually done within tens of ns, cheaper to wait for than a park and a resume.
void clj_cmutex_lock_slow(clj_cmutex *m) {
	for (int i = 0; i < SPINS; i++) {
		uint32_t expected = 0;
		if (atomic_load_explicit(&m->state, memory_order_relaxed) == 0 &&
		    atomic_compare_exchange_weak_explicit(&m->state, &expected, 1, memory_order_acquire, memory_order_relaxed))
			return;
		pause();
	}
	lot_bucket *b = bucket_of(m);
	for (;;) {
		clj_lock_lock(&b->lock);
		uint32_t st = atomic_load_explicit(&m->state, memory_order_relaxed);
		if (st == 0) {
			uint32_t expected = 0;
			bool     won = atomic_compare_exchange_strong_explicit(&m->state, &expected, 1, memory_order_acquire, memory_order_relaxed);
			clj_lock_unlock(&b->lock);
			if (won) return;
			continue;
		}
		if (st == 1) {
			uint32_t expected = 1;
			if (!atomic_compare_exchange_strong_explicit(&m->state, &expected, 2, memory_order_relaxed, memory_order_relaxed)) {
				clj_lock_unlock(&b->lock);
				continue;
			}
		}
		clj_waiter *w = waiter_here();
		enqueue_locked(b, w, m);
		clj_lock_unlock(&b->lock);
		// The unlocker hands the lock over: on return the mutex is ours, state 1 or 2.
		clj_park_uncancellable(w);
		clj_waiter_release(w);
		return;
	}
}

void clj_cmutex_unlock_slow(clj_cmutex *m) {
	lot_bucket *b = bucket_of(m);
	clj_lock_lock(&b->lock);
	bool      more;
	lot_node *n = dequeue_locked(b, m, &more);
	if (!n) {
		atomic_store_explicit(&m->state, 0, memory_order_release);
		clj_lock_unlock(&b->lock);
		return;
	}
	atomic_store_explicit(&m->state, more ? 2 : 1, memory_order_release);
	clj_lock_unlock(&b->lock);
	if (clj_waiter_claim(n->w)) clj_resume(n->w);
	clj_waiter_release(n->w);
	free(n);
}

// ---- monitors for locking: one record per object while anyone holds or waits for it

typedef struct monitor {
	uintptr_t       key;
	clj_cmutex      m;
	clj_coro       *owner;
	uint32_t        count;
	uint32_t        users;
	struct monitor *next;
} monitor;

enum { MONITOR_BUCKETS = 64 };

static clj_lock       mon_lock = CLJ_LOCK_INIT;
static monitor       *mon_buckets[MONITOR_BUCKETS];
static _Atomic size_t live_monitors;

static monitor **mon_slot(uintptr_t key) {
	uintptr_t h = key;
	h ^= h >> 17;
	h *= 0x9E3779B97F4A7C15ull;
	return &mon_buckets[(h >> 40) & (MONITOR_BUCKETS - 1)];
}

static monitor *mon_find_locked(uintptr_t key, bool create) {
	monitor **slot = mon_slot(key);
	for (monitor *mo = *slot; mo; mo = mo->next) {
		if (mo->key == key) return mo;
	}
	if (!create) return NULL;
	monitor *mo = calloc(1, sizeof *mo);
	if (!mo) clj_fatal("out of memory");
	mo->key = key;
	clj_cmutex_init(&mo->m);
	mo->next = *slot;
	*slot = mo;
	atomic_fetch_add_explicit(&live_monitors, 1, memory_order_relaxed);
	return mo;
}

static void mon_drop_locked(monitor *mo) {
	if (--mo->users) return;
	monitor **at = mon_slot(mo->key);
	while (*at != mo) at = &(*at)->next;
	*at = mo->next;
	free(mo);
	atomic_fetch_sub_explicit(&live_monitors, 1, memory_order_relaxed);
}

clj_value clj_monitor_enter(clj_value x) {
	clj_coro *me = clj_coro_current();
	clj_lock_lock(&mon_lock);
	monitor *mo = mon_find_locked((uintptr_t)x, true);
	mo->users++;
	clj_lock_unlock(&mon_lock);
	if (mo->owner == me) {
		mo->count++;
		return CLJ_NIL;
	}
	clj_cmutex_lock(&mo->m);
	mo->owner = me;
	mo->count = 1;
	return CLJ_NIL;
}

clj_value clj_monitor_exit(clj_value x) {
	clj_coro *me = clj_coro_current();
	clj_lock_lock(&mon_lock);
	monitor *mo = mon_find_locked((uintptr_t)x, false);
	if (!mo || mo->owner != me) {
		clj_lock_unlock(&mon_lock);
		return clj_throw_msg("monitor-exit of an object this execution does not hold");
	}
	clj_lock_unlock(&mon_lock);
	if (--mo->count == 0) {
		mo->owner = NULL;
		clj_cmutex_unlock(&mo->m);
	}
	clj_lock_lock(&mon_lock);
	mon_drop_locked(mo);
	clj_lock_unlock(&mon_lock);
	return CLJ_NIL;
}

size_t clj_debug_live_monitors(void) { return atomic_load_explicit(&live_monitors, memory_order_relaxed); }
