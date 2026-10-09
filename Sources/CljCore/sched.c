// @ai-generated(solo)
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <pthread/qos.h>
#endif

#include "clj/cmutex.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/printer.h"
#include "clj/profile.h"
#include "clj/string.h"
#include "clj/vector.h"
#include "cc_internal.h"
#include "coro_internal.h"
#include "profile_internal.h"

// ---- the run queue and the carriers

static pthread_once_t   init_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t  run_mu = PTHREAD_MUTEX_INITIALIZER;
static clj_coro        *run_head, *run_tail;
static clj_carrier     *carriers;  // the pool, linked through pool_next
static clj_carrier     *idle_head; // carriers waiting on their own condition, most recent first; under run_mu
static size_t           ncarriers, polling_carriers, spinning_carriers;
// A popped carrier has not reached the queue yet (Go's wakep): nobody else is woken until it has.
static bool             woken;

// An idle carrier spins for SPIN_NS in slices, then polls every POLL_NS for POLL_ROUNDS, then sleeps for a wake:
// a burst of work costs one wakeup, not one per item.
enum { SPIN_NS = 20000, SPIN_SLICE_NS = 2000, POLL_NS = 50000, POLL_ROUNDS = 20, MAX_SPINNERS = 2 };
// A next slot younger than this stays with its carrier: a pair handing values back and forth keeps one thread.
enum { NEXT_STEAL_AGE_NS = 5000 };
// Trylocks before blocking on run_mu: a holder is out within ~100 ns, a kernel wait is a syscall each side.
enum { LOCK_TRIES = 64 };

static inline void cpu_relax(void) {
#if defined(__aarch64__)
	__asm__ volatile("yield");
#elif defined(__x86_64__)
	__asm__ volatile("pause");
#endif
}

// Darwin has no pthread_condattr_setclock: a relative wait keeps the deadline off CLOCK_REALTIME, which can step.
static int cond_wait_ns(pthread_cond_t *cv, pthread_mutex_t *mu, uint64_t ns) {
#ifdef __APPLE__
	struct timespec rel = {(time_t)(ns / 1000000000u), (long)(ns % 1000000000u)};
	return pthread_cond_timedwait_relative_np(cv, mu, &rel);
#else
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += (time_t)(ns / 1000000000u);
	ts.tv_nsec += (long)(ns % 1000000000u);
	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000L;
	}
	return pthread_cond_timedwait(cv, mu, &ts);
#endif
}

static void run_lock(void) {
	for (int i = 0; i < LOCK_TRIES; i++) {
		if (pthread_mutex_trylock(&run_mu) == 0) return;
		cpu_relax();
	}
	pthread_mutex_lock(&run_mu);
}

// A plain store beside the spinner's relaxed load is a race in the model, whatever the hardware does.
#define GLANCE_SET(lv, v) __atomic_store_n(&(lv), (v), __ATOMIC_RELAXED)

// A racy glance at the queues from a spinner, confirmed under the lock before anything is taken; a fresh next slot
// is not work for a spinner, or the pair using it would pay a lock contention per hand-off.
static bool work_visible(void) {
	if (__atomic_load_n(&run_head, __ATOMIC_RELAXED)) return true;
	uint64_t now = clj_profile_now();
	// Acquire against the publication of a new carrier: its pool_next is written before it, under run_mu.
	for (clj_carrier *o = __atomic_load_n(&carriers, __ATOMIC_ACQUIRE); o; o = o->pool_next) {
		if (__atomic_load_n(&o->next, __ATOMIC_RELAXED) && now - __atomic_load_n(&o->next_at, __ATOMIC_RELAXED) > NEXT_STEAL_AGE_NS) return true;
	}
	return false;
}
static _Atomic uint64_t spawned;

static void queue_push(clj_coro **head, clj_coro **tail, clj_coro *c) {
	c->next = NULL;
	if (*tail) (*tail)->next = c;
	else GLANCE_SET(*head, c);
	*tail = c;
}

static clj_coro *queue_pop(clj_coro **head, clj_coro **tail) {
	clj_coro *c = *head;
	if (!c) return NULL;
	GLANCE_SET(*head, c->next);
	if (!*head) *tail = NULL;
	c->next = NULL;
	return c;
}

// park locks c->lock on the coroutine's fiber and run_one unlocks it on the carrier's (docs/notes/gates.md, "TSan").
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
void __tsan_mutex_pre_lock(void *addr, unsigned flags);
void __tsan_mutex_post_lock(void *addr, unsigned flags, int recursion);
int  __tsan_mutex_pre_unlock(void *addr, unsigned flags);
void __tsan_mutex_post_unlock(void *addr, unsigned flags);
#define TSAN_LOCK_GIVE(m) ((void)__tsan_mutex_pre_unlock((m), 0), __tsan_mutex_post_unlock((m), 0))
#define TSAN_LOCK_TAKE(m) (__tsan_mutex_pre_lock((m), 0), __tsan_mutex_post_lock((m), 0, 0))
#endif
#endif
#ifndef TSAN_LOCK_GIVE
#define TSAN_LOCK_GIVE(m) ((void)(m))
#define TSAN_LOCK_TAKE(m) ((void)(m))
#endif

static void finish(clj_coro *c);

// After a switch back: the coroutine parked holding its lock (unlocked here, on its carrier) or finished.
static void run_one(clj_carrier *car, clj_coro *c) {
	CLJ_ASSERT(atomic_load_explicit(&c->state, memory_order_acquire) == CLJ_CORO_RUNNABLE, "run of a coroutine that is not runnable");
	clj_coro_switch_in(car, c);
	if (atomic_load_explicit(&c->state, memory_order_acquire) == CLJ_CORO_DONE) {
		finish(c);
		return;
	}
	clj_coro_advise_stack(c);
	TSAN_LOCK_TAKE(&c->lock);
	pthread_mutex_unlock(&c->lock);
}

static void idle_push(clj_carrier *car) {
	car->idle_next = idle_head;
	idle_head = car;
	car->idle = true;
}

static void idle_remove(clj_carrier *car) {
	for (clj_carrier **at = &idle_head; *at; at = &(*at)->idle_next) {
		if (*at == car) {
			*at = car->idle_next;
			break;
		}
	}
	car->idle = false;
}

// Under run_mu: at most one carrier is on its way at a time; it chains the next wake (Go's resetspinning).
static clj_carrier *wake_one_locked(void) {
	if (spinning_carriers || woken || !idle_head) return NULL;
	clj_carrier *car = idle_head;
	idle_head = car->idle_next;
	car->idle = false;
	woken = true;
	return car;
}

// Outside run_mu: the signal is a syscall when the carrier already sleeps in the kernel.
static void carrier_wake(clj_carrier *car) {
	if (!car) return;
	pthread_mutex_lock(&car->park_mu);
	car->signaled = true;
	pthread_cond_signal(&car->park_cv);
	pthread_mutex_unlock(&car->park_mu);
}

// Under run_mu: the carrier's own next slot, else the queue, else another carrier's slot old enough to steal.
static clj_coro *take_work_locked(clj_carrier *car, clj_carrier **wake) {
	clj_coro *c = car->next;
	if (c) {
		GLANCE_SET(car->next, NULL);
		return c;
	}
	c = queue_pop(&run_head, &run_tail);
	if (c) {
		if (run_head) *wake = wake_one_locked();
		return c;
	}
	uint64_t now = clj_profile_now();
	for (clj_carrier *o = carriers; o; o = o->pool_next) {
		if (o->next && now - o->next_at > NEXT_STEAL_AGE_NS) {
			c = o->next;
			GLANCE_SET(o->next, NULL);
			return c;
		}
	}
	return NULL;
}

// A stale signal (a wake that raced a timeout) returns at once; the loop just looks at the queue again.
static void carrier_park(clj_carrier *car, bool polling) {
	pthread_mutex_lock(&car->park_mu);
	if (polling) {
		while (!car->signaled && cond_wait_ns(&car->park_cv, &car->park_mu, POLL_NS) != ETIMEDOUT) {}
	} else {
		while (!car->signaled) pthread_cond_wait(&car->park_cv, &car->park_mu);
	}
	car->signaled = false;
	pthread_mutex_unlock(&car->park_mu);
}

static void *carrier_main(void *arg) {
	(void)arg;
#ifdef __APPLE__
	// USER_INITIATED claims the user is waiting, and one pool holding both a button handler and a background
	// parse cannot claim it: the class costs P-cores for work nobody awaits. DEFAULT is "intent not expressed",
	// which is the truth until a carrier per class exists (design §4, "Рассматривается: QoS носителей").
	pthread_set_qos_class_self_np(QOS_CLASS_DEFAULT, 0);
#endif
	clj_carrier *car = clj_carrier_here();
	car->pooled = true;
	pthread_mutex_init(&car->park_mu, NULL);
	pthread_cond_init(&car->park_cv, NULL);
	pthread_mutex_lock(&run_mu);
	car->pool_next = carriers;
	__atomic_store_n(&carriers, car, __ATOMIC_RELEASE);
	pthread_mutex_unlock(&run_mu);
	for (;;) {
		run_lock();
		clj_coro    *c;
		clj_carrier *wake = NULL;
		int          polls = 0;
		uint64_t     spun = 0;
		while (!(c = take_work_locked(car, &wake))) {
			// Out of work: spin briefly (a burst lands in a spinner within ~100 ns, no signal per item), then poll, then sleep.
			if (spun < SPIN_NS && spinning_carriers < MAX_SPINNERS) {
				spinning_carriers++;
				pthread_mutex_unlock(&run_mu);
				// The lock is taken only for work seen or at the budget's end.
				bool seen = false;
				do {
					uint64_t t0 = clj_profile_now(), t = t0;
					while (t - t0 < SPIN_SLICE_NS && !(seen = work_visible())) {
						for (int i = 0; i < 32; i++) cpu_relax();
						t = clj_profile_now();
					}
					spun += t - t0;
				} while (!seen && spun < SPIN_NS);
				run_lock();
				spinning_carriers--;
				continue;
			}
			bool fresh_next = false;
			for (clj_carrier *o = carriers; o && !fresh_next; o = o->pool_next) fresh_next = o->next != NULL;
			bool polling = fresh_next || polls < POLL_ROUNDS;
			if (polling) polls++;
			car->polling = polling;
			polling_carriers += polling;
			idle_push(car);
			pthread_mutex_unlock(&run_mu);
			carrier_park(car, polling);
			run_lock();
			polling_carriers -= polling;
			// Popped by a waker: this carrier now looks at the queue. Timed out: leave the list.
			if (car->idle) idle_remove(car);
			else woken = false;
		}
		pthread_mutex_unlock(&run_mu);
		carrier_wake(wake);
		run_one(car, c);
	}
	return NULL;
}

static void *seed_carrier_main(void *arg);

static void start_carriers(void) {
	if (clj_sched_seed_on) {
		ncarriers = 1;
		pthread_t      t;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&t, &attr, seed_carrier_main, NULL) != 0) clj_fatal("pthread_create of the seeded carrier failed");
		pthread_attr_destroy(&attr);
		return;
	}
	const char *env = getenv("CLJ_CARRIERS");
	long        n = env ? atol(env) : sysconf(_SC_NPROCESSORS_ONLN);
	if (n < 1) n = 1;
	ncarriers = (size_t)n;
	for (size_t i = 0; i < ncarriers; i++) {
		pthread_t      t;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&t, &attr, carrier_main, NULL) != 0) clj_fatal("pthread_create of a carrier failed");
		pthread_attr_destroy(&attr);
	}
}

void clj_sched_init(void) { pthread_once(&init_once, start_carriers); }

size_t clj_sched_carrier_count(void) {
	clj_sched_init();
	return ncarriers;
}

// ---- the main carrier

static pthread_mutex_t main_mu = PTHREAD_MUTEX_INITIALIZER;
static clj_coro       *main_head, *main_tail;
static clj_carrier    *main_carrier;
#ifdef __APPLE__
static CFRunLoopSourceRef main_source;
static CFRunLoopRef       main_loop;
#endif

void clj_sched_main_pump(void) {
	clj_carrier *car = clj_carrier_here();
	if (car != main_carrier) clj_fatal("clj_sched_main_pump off the main carrier");
	for (;;) {
		pthread_mutex_lock(&main_mu);
		clj_coro *c = queue_pop(&main_head, &main_tail);
		pthread_mutex_unlock(&main_mu);
		if (!c) return;
		run_one(car, c);
	}
}

#ifdef __APPLE__
static void main_perform(void *info) {
	(void)info;
	clj_sched_main_pump();
}

static void main_before_waiting(CFRunLoopObserverRef observer, CFRunLoopActivity activity, void *info) {
	(void)observer;
	(void)activity;
	(void)info;
	clj_cc_main_idle();
}
#endif

void clj_sched_main_install(void) {
	clj_carrier *car = clj_carrier_here();
	car->is_main = true;
	main_carrier = car;
#ifdef __APPLE__
	CFRunLoopSourceContext ctx;
	memset(&ctx, 0, sizeof ctx);
	ctx.perform = main_perform;
	main_source = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx);
	main_loop = CFRunLoopGetCurrent();
	CFRunLoopAddSource(main_loop, main_source, kCFRunLoopCommonModes);
	// The main carrier's cycle candidates are collected only here, when it has nothing else to do (design §7).
	CFRunLoopObserverRef idle = CFRunLoopObserverCreate(kCFAllocatorDefault, kCFRunLoopBeforeWaiting, true, 0, main_before_waiting, NULL);
	CFRunLoopAddObserver(main_loop, idle, kCFRunLoopCommonModes);
	CFRelease(idle);
#endif
}

void clj_debug_sched_main_adopt(void) {
	clj_carrier *car = clj_carrier_here();
	car->is_main = true;
	main_carrier = car;
}

void clj_debug_sched_main_abandon(void) {
	clj_carrier *car = clj_carrier_here();
	if (main_carrier == car) main_carrier = NULL;
	car->is_main = false;
}

static void enqueue_main(clj_coro *c) {
	pthread_mutex_lock(&main_mu);
	queue_push(&main_head, &main_tail, c);
	pthread_mutex_unlock(&main_mu);
#ifdef __APPLE__
	if (main_source) {
		CFRunLoopSourceSignal(main_source);
		CFRunLoopWakeUp(main_loop);
	}
#endif
}

static void seed_enqueue(clj_coro *c);

// From a pooled carrier the coroutine takes the carrier's next slot (an earlier occupant moves to the queue), so a
// resumer that parks right after hands its thread over without waking another; idle carriers are still told.
void clj_sched_enqueue(clj_coro *c, bool handoff) {
	CLJ_ASSERT(atomic_load_explicit(&c->state, memory_order_acquire) == CLJ_CORO_RUNNABLE, "enqueue of a coroutine that is not runnable");
	if (c->affinity == CLJ_AFFINITY_MAIN) {
		enqueue_main(c);
		return;
	}
	if (__builtin_expect(clj_sched_seed_on, 0)) {
		seed_enqueue(c);
		return;
	}
	clj_coro    *me = clj_coro_tls;
	clj_carrier *car = handoff && me && me->carrier && me->carrier->pooled ? me->carrier : NULL;
	run_lock();
	if (car) {
		if (car->next) queue_push(&run_head, &run_tail, car->next);
		GLANCE_SET(car->next, c);
		GLANCE_SET(car->next_at, clj_profile_now());
	} else {
		queue_push(&run_head, &run_tail, c);
	}
	// Pool-internal work leaves the pickup to a poller (a wake is a syscall per item); outside work is latency.
	bool         from_pool = me && me->carrier && me->carrier->pooled;
	clj_carrier *wake = !polling_carriers || !from_pool ? wake_one_locked() : NULL;
	pthread_mutex_unlock(&run_mu);
	carrier_wake(wake);
}

// ---- waiters, park and resume

clj_waiter *clj_waiter_new(clj_coro *c, clj_value callback) {
	clj_waiter *w = calloc(1, sizeof *w);
	if (!w) clj_fatal("out of memory");
	atomic_init(&w->rc, 1);
	clj_lock_init(&w->lock);
	w->coro = c;
	w->callback = clj_retain(callback);
	w->value = CLJ_NIL;
	w->port = CLJ_NIL;
	return w;
}

void clj_waiter_retain(clj_waiter *w) { atomic_fetch_add_explicit(&w->rc, 1, memory_order_relaxed); }

void clj_waiter_release(clj_waiter *w) {
	if (atomic_fetch_sub_explicit(&w->rc, 1, memory_order_acq_rel) != 1) return;
	clj_release(w->callback);
	clj_release(w->value);
	clj_release(w->port);
	free(w);
}

static bool claimed(const clj_waiter *w) { return atomic_load_explicit(&w->claimed, memory_order_acquire); }

bool clj_waiter_claim(clj_waiter *w) {
	clj_lock_lock(&w->lock);
	bool won = !claimed(w);
	if (won) atomic_store_explicit(&w->claimed, 1, memory_order_release);
	clj_lock_unlock(&w->lock);
	return won;
}

// Both locks in address order, so two channels pairing the same two waiters cannot deadlock.
int clj_waiter_claim_pair(clj_waiter *actor, clj_waiter *other) {
	if (!actor && !other) return 0;
	if (!actor) return clj_waiter_claim(other) ? 0 : 2;
	if (!other) return clj_waiter_claim(actor) ? 0 : 1;
	clj_waiter *first = actor < other ? actor : other, *second = actor < other ? other : actor;
	clj_lock_lock(&first->lock);
	if (first != second) clj_lock_lock(&second->lock);
	int r = claimed(actor) ? 1 : claimed(other) ? 2 : 0;
	if (r == 0) {
		atomic_store_explicit(&actor->claimed, 1, memory_order_release);
		atomic_store_explicit(&other->claimed, 1, memory_order_release);
	}
	if (first != second) clj_lock_unlock(&second->lock);
	clj_lock_unlock(&first->lock);
	return r;
}

bool clj_park_allowed(void) {
	clj_coro *c = clj_coro_current();
	if (atomic_load_explicit(&c->shadow->cancelled, memory_order_relaxed)) {
		clj_throw_cancelled(clj_coro_cancel_is_deadline(c));
		return false;
	}
	if (c->host_depth) {
		clj_throw_msg("Cannot park inside a synchronous host call: the host waits for a value now (design §5)");
		return false;
	}
	if (c->locks_held) {
		clj_throw_msg("Cannot park while a runtime lock is held");
		return false;
	}
	return true;
}

static bool seed_block(clj_coro *c);
static void seed_bare_wait_pick(clj_coro *c);
static void seed_bare_ready(clj_coro *c);

// A pool coroutine switches out holding its lock, so a resumer that wants the lock sees it parked or not at all.
// The one switch out of a running body besides its finish (make park-audit).
void clj_park(clj_waiter *w, clj_wake wake) {
	CLJ_ASSERT(wake.kind, "a park without a wake source");
	clj_coro *c = clj_coro_current();
	pthread_mutex_lock(&c->lock);
	bool block = c->implicit || w->blocking;
	if (!wake.cancellable) w = NULL;
	// A cancellation that landed between the caller's check and here found no waiter to wake: the park is skipped
	// by claiming the waiter ourselves (a concurrent completion that won the claim resumes us instead).
	if (w && atomic_load_explicit(&c->shadow->cancelled, memory_order_relaxed) && clj_waiter_claim(w)) {
		pthread_mutex_unlock(&c->lock);
		return;
	}
	if (block) {
		c->waiter = w;
		bool seeded = __builtin_expect(clj_sched_seed_on, 0) && !c->signaled && seed_block(c);
		while (!c->signaled) pthread_cond_wait(&c->cond, &c->lock);
		c->signaled = false;
		c->waiter = NULL;
		pthread_mutex_unlock(&c->lock);
		if (seeded) seed_bare_wait_pick(c);
		return;
	}
	if (c->resume_pending) {
		c->resume_pending = false;
		pthread_mutex_unlock(&c->lock);
		return;
	}
	c->waiter = w;
	c->park_kind = wake.kind;
	if (__builtin_expect(!c->linked, 0)) clj_coro_live_link(c);
	c->parks++;
	atomic_store_explicit(&c->state, CLJ_CORO_PARKED, memory_order_release);
	TSAN_LOCK_GIVE(&c->lock);
	clj_coro_switch_out(c);
	pthread_mutex_lock(&c->lock);
	c->waiter = NULL;
	c->park_kind = 0;
	c->resume_pending = false;
	pthread_mutex_unlock(&c->lock);
}

static void run_callback(clj_waiter *w) {
	if (clj_is_nil(w->callback)) return;
	clj_value r = clj_invoke(w->callback, &w->value, 1);
	if (r == CLJ_THROWN) {
		clj_coro *c = clj_coro_current();
		clj_coro_report_uncaught(c);
		clj_coro_drop_pending(c);
	} else {
		clj_release(r);
	}
}

static void resume(clj_waiter *w, bool handoff) {
	clj_coro *c = w->coro;
	if (!c) {
		run_callback(w);
		return;
	}
	pthread_mutex_lock(&c->lock);
	if (c->implicit || w->blocking) {
		c->signaled = true;
		pthread_cond_signal(&c->cond);
		if (__builtin_expect(clj_sched_seed_on, 0) && c->implicit) seed_bare_ready(c);
		pthread_mutex_unlock(&c->lock);
		return;
	}
	if (atomic_load_explicit(&c->state, memory_order_acquire) == CLJ_CORO_PARKED) {
		// A collection that judged it parked sees the wake (design §7, «Фаза 3»).
		if (__builtin_expect(atomic_load_explicit(&clj_cc_running, memory_order_seq_cst), 0) &&
		    (atomic_load_explicit(&c->h.rc, memory_order_seq_cst) & CLJ_RC_WATCH))
			clj_cc_unwatch(&c->h);
		atomic_store_explicit(&c->state, CLJ_CORO_RUNNABLE, memory_order_relaxed);
		pthread_mutex_unlock(&c->lock);
		clj_sched_enqueue(c, handoff);
		return;
	}
	c->resume_pending = true;
	pthread_mutex_unlock(&c->lock);
}

void clj_resume(clj_waiter *w) { resume(w, true); }

void clj_resume_far(clj_waiter *w) { resume(w, false); }

// ---- spawn and finish

static void (*uncaught_handler)(clj_value ex, clj_value trace);

static void put(const char *s) { (void)!write(2, s, strlen(s)); }

// Diagnostics, not fatal. Only its own cancellation is expected: a bystander's flag is clear (design.md §4).
void clj_coro_report_uncaught(clj_coro *c) {
	clj_value ex = c->threw ? c->result.v : c->pending;
	if (clj_is_cancellation(ex) && atomic_load_explicit(&c->cancel, memory_order_relaxed) != CLJ_CANCEL_NONE) return;
	clj_value trace = c->threw ? clj_ex_trace(ex) : clj_trace_realize(c->pending_trace);
	if (uncaught_handler) {
		uncaught_handler(ex, trace);
		clj_release(trace);
		return;
	}
	clj_value msg = clj_ex_message(ex);
	put("clj: uncaught exception in a coroutine: ");
	if (clj_is_string(msg)) put(clj_string_bytes(msg));
	else {
		clj_value text = clj_pr_str(ex);
		if (text != CLJ_THROWN) {
			put(clj_string_bytes(text));
			clj_release(text);
		}
	}
	put("\n");
	clj_release(msg);
	if (clj_is_vector(trace)) {
		clj_value text = clj_pr_str(trace);
		if (text != CLJ_THROWN) {
			put("  ");
			put(clj_string_bytes(text));
			put("\n");
			clj_release(text);
		}
	}
	clj_release(trace);
}

void clj_coro_set_uncaught_handler(void (*fn)(clj_value ex, clj_value trace)) { uncaught_handler = fn; }

static void deadline_disarm(clj_coro *c);
static void disarm_locked(clj_coro *c);
static void cancel_cause_clear_locked(clj_coro *c);

static void finish(clj_coro *c) {
#if CLJ_DEBUG
	// The epilogue tears down what the finished body owned, on the carrier: a transfer from c, which never runs again.
	uint32_t carrier_owner = clj_debug_owner_assume(c->debug_owner);
#endif
	void *carrier_candidates = clj_cc_local_borrow(c);
	deadline_disarm(c);
	clj_eval_drain_retired(c);
	clj_var_bindings_release(c->bindings);
	c->bindings = NULL;
	clj_output_captures_release(c->captures);
	c->captures = NULL;
	clj_coro_drop_pending(c);
	// The cause may hold a channel that holds this coroutine: the cycle dies here, with the body.
	pthread_mutex_lock(&c->lock);
	cancel_cause_clear_locked(c);
	pthread_mutex_unlock(&c->lock);
	if (c->on_done) c->on_done(c, c->done_ctx);
	else if (c->threw) clj_coro_report_uncaught(c);
	c->on_done = NULL;
	clj_value done = c->done_value.v;
	clj_slot_clear(&c->done_value);
	clj_release(done);
	clj_value fn = c->fn.v;
	clj_slot_clear(&c->fn);
	clj_release(fn);
	for (size_t i = 0; i < c->nargs; i++) clj_release(c->args[i].v);
	c->nargs = 0;
	// Under the lock: a canceller and the sweep read c->shadow and c->map under it, the collector the slots.
	pthread_mutex_lock(&c->lock);
	c->finished = true;
	clj_coro_free_stack(c);
	c->signaled = true;
	pthread_cond_broadcast(&c->cond);
	pthread_mutex_unlock(&c->lock);
	clj_cc_execution_done(c);
	clj_cc_local_return(c, carrier_candidates);
#if CLJ_DEBUG
	clj_debug_owner_assume(carrier_owner);
#endif
	clj_release(clj_from_ptr(c));
}

static clj_value spawn(clj_value f, const clj_value *args, size_t n, int affinity, void (*on_done)(clj_coro *c, void *ctx), void *ctx, clj_value done_value, bool detached) {
	if (affinity == CLJ_AFFINITY_MAIN && !main_carrier) return clj_throw_msg("No main carrier: the host has not installed one (clj_sched_main_install)");
	clj_sched_init();
	clj_coro *parent = clj_coro_current();
	clj_coro *c = clj_coro_alloc();
	clj_slot_store(&c->h, &c->fn, clj_retain(f));
	if (n) {
		c->args = malloc(n * sizeof *c->args);
		if (!c->args) clj_fatal("out of memory");
		for (size_t i = 0; i < n; i++) clj_slot_store(&c->h, &c->args[i], clj_retain(args[i]));
	}
	c->nargs = n;
	if (!clj_is_nil(done_value)) clj_slot_store(&c->h, &c->done_value, clj_retain(done_value));
	c->bindings = detached ? NULL : clj_var_bindings_share();
	c->captures = clj_output_captures_share();
	c->pending = c->pending_trace = CLJ_NIL;
	clj_slot_clear(&c->result);
	// A cancelled parent hands the child its deadline as it was, not the cancel flag: past it the child meets it at once.
	bool parent_cancelled = atomic_load_explicit(&parent->cancel, memory_order_relaxed) != CLJ_CANCEL_NONE;
	// A shield hides the deadline from the parent's own ticks, not from the children spawned there.
	uint64_t deadline = detached ? 0 : parent_cancelled || parent->shield ? parent->deadline_before : clj_shadow_deadline(parent->shadow);
	atomic_store_explicit(&c->shadow->deadline, deadline, memory_order_relaxed);
	c->shadow->countdown = 1024;
	atomic_store_explicit(&c->shadow->unwinds, 64, memory_order_relaxed);
	clj_coro_capture_spawn_trace(c);
	c->affinity = (uint8_t)affinity;
	c->on_done = on_done;
	c->done_ctx = ctx;
	atomic_fetch_add_explicit(&spawned, 1, memory_order_relaxed);
	clj_retain(clj_from_ptr(c));
	clj_coro_deadline_arm(c);
	atomic_store_explicit(&c->state, CLJ_CORO_RUNNABLE, memory_order_release);
	clj_sched_enqueue(c, false);
	return clj_from_ptr(c);
}

clj_value clj_coro_spawn(clj_value f, const clj_value *args, size_t n, int affinity, void (*on_done)(clj_coro *c, void *ctx), void *ctx) {
	return spawn(f, args, n, affinity, on_done, ctx, CLJ_NIL, false);
}

clj_value clj_coro_spawn_detached(clj_value f, void (*on_done)(clj_coro *c, void *ctx), void *ctx) {
	return spawn(f, NULL, 0, CLJ_AFFINITY_POOL, on_done, ctx, CLJ_NIL, true);
}

clj_value clj_coro_spawn_into(clj_value f, int affinity, void (*on_done)(clj_coro *c, void *ctx), clj_value done_value, bool detached) {
	return spawn(f, NULL, 0, detached ? CLJ_AFFINITY_POOL : affinity, on_done, NULL, done_value, detached);
}

clj_value clj_coro_result(clj_value coro, bool *threw) {
	clj_coro *c = clj_coro_of(coro);
	if (threw) *threw = c->threw;
	return c->result.v;
}

bool clj_coro_done(clj_value coro) { return atomic_load_explicit(&clj_coro_of(coro)->state, memory_order_acquire) == CLJ_CORO_DONE; }

// The tick's poison, under c->lock: a cancellation and a suspend request both replace the deadline with 1, so
// neither adds a check of its own to the hot path; the real deadline waits in deadline_before until both are gone.
// @ai-generated(guided)
static void deadline_poison_locked(clj_coro *c) {
	if (c->shield) return;
	uint64_t d = clj_shadow_deadline(c->shadow);
	if (d != 1) c->deadline_before = d;
	atomic_store_explicit(&c->shadow->deadline, 1, memory_order_relaxed);
}

static bool poisoned_locked(const clj_coro *c) {
	return atomic_load_explicit(&c->cancel, memory_order_relaxed) != CLJ_CANCEL_NONE || atomic_load_explicit(&c->shadow->suspend, memory_order_relaxed);
}

static void deadline_poison_if_locked(clj_coro *c) {
	if (c->shield) return;
	if (poisoned_locked(c)) atomic_store_explicit(&c->shadow->deadline, 1, memory_order_relaxed);
}

static void deadline_unpoison_locked(clj_coro *c) {
	if (c->shield) return;
	if (!poisoned_locked(c)) atomic_store_explicit(&c->shadow->deadline, c->deadline_before, memory_order_relaxed);
}

// The ring reads 1 under a poison and 0 inside a shield; the value itself waits in deadline_before.
static uint64_t deadline_own_locked(const clj_coro *c) {
	if (!c->shadow) return 0;
	uint64_t d = clj_shadow_deadline(c->shadow);
	return (d == 1 || c->shield) ? c->deadline_before : d;
}

// Shielding (design §4, Trio's shield): no deadline in the ring, so the tick has nothing to throw.
// @ai-generated(solo)
void clj_coro_shield_enter(clj_coro *c) {
	pthread_mutex_lock(&c->lock);
	if (!c->shield++ && c->shadow) {
		uint64_t d = clj_shadow_deadline(c->shadow);
		if (d != 1) c->deadline_before = d;
		atomic_store_explicit(&c->shadow->deadline, 0, memory_order_relaxed);
	}
	pthread_mutex_unlock(&c->lock);
}

// @ai-generated(solo)
void clj_coro_shield_leave(clj_coro *c) {
	pthread_mutex_lock(&c->lock);
	// Recomputed, not restored: a cancellation may have landed while the region ran, and only the flags say so.
	if (c->shield && !--c->shield && c->shadow) {
		if (poisoned_locked(c)) atomic_store_explicit(&c->shadow->deadline, 1, memory_order_relaxed);
		else atomic_store_explicit(&c->shadow->deadline, c->deadline_before, memory_order_relaxed);
	}
	pthread_mutex_unlock(&c->lock);
}

// Under c->lock: the flag, the tick trigger and the waiter to wake. A running coroutine sees the flag at its next
// tick or park point; a parked one is woken by claiming its waiter (an implicit coroutine's park is its condition).
static clj_waiter *cancel_locked(clj_coro *c, int kind, clj_value cause) {
	uint8_t have = atomic_load_explicit(&c->cancel, memory_order_relaxed);
	if (have == CLJ_CANCEL_NONE || (kind == CLJ_CANCEL_REQUESTED && have != CLJ_CANCEL_REQUESTED)) {
		if (!clj_is_nil(cause) && clj_is_nil(clj_slot_load(&c->cancel_cause, memory_order_relaxed)))
			clj_slot_store_atomic(&c->h, &c->cancel_cause, clj_retain(cause), memory_order_relaxed);
		// Release after the cause: the owner reads the flag first and must then see what came with it.
		atomic_store_explicit(&c->cancel, (uint8_t)kind, memory_order_release);
	}
	atomic_store_explicit(&c->shadow->cancelled, true, memory_order_relaxed);
	deadline_poison_locked(c);
	atomic_store_explicit(&c->shadow->unwinds, 64, memory_order_relaxed);
	clj_waiter *w = c->waiter;
	if (w) clj_waiter_retain(w);
	return w;
}

void clj_coro_cancel_kind_cause(clj_coro *c, int kind, clj_value cause) {
	pthread_mutex_lock(&c->lock);
	if (atomic_load_explicit(&c->state, memory_order_acquire) == CLJ_CORO_DONE || !c->shadow) {
		pthread_mutex_unlock(&c->lock);
		return;
	}
	// A cancellation reaches a suspended coroutine: the flag goes first, so the tick it wakes into throws once
	// instead of parking again. Whoever asked for the suspension gets no resume of its own (NOTES.md).
	bool gated = atomic_load_explicit(&c->shadow->suspend, memory_order_relaxed);
	if (gated) atomic_store_explicit(&c->shadow->suspend, false, memory_order_release);
	clj_waiter *w = cancel_locked(c, kind, cause);
	pthread_mutex_unlock(&c->lock);
	if (gated) clj_lot_unpark_all(c);
	if (!w) return;
	if (clj_waiter_claim(w)) clj_resume_far(w);
	clj_waiter_release(w);
}

void clj_coro_cancel_kind(clj_coro *c, int kind) { clj_coro_cancel_kind_cause(c, kind, CLJ_NIL); }

// Acquire pairs with cancel_locked's release, so a flag this reads carries the cause stored before it.
// @ai-generated(guided)
clj_value clj_coro_cancel_cause(clj_coro *c) {
	if (atomic_load_explicit(&c->cancel, memory_order_acquire) == CLJ_CANCEL_NONE) return CLJ_NIL;
	return clj_retain(clj_slot_load(&c->cancel_cause, memory_order_relaxed));
}

// Under c->lock, with the flag: a cause left behind would surface in a later, unrelated cancellation.
static void cancel_cause_clear_locked(clj_coro *c) {
	clj_value cause = clj_slot_exchange(&c->h, &c->cancel_cause, CLJ_NIL, memory_order_relaxed);
	clj_release(cause);
}

void clj_coro_cancel(clj_value coro) {
	clj_coro *c = clj_coro_of(coro);
	if (c->implicit) return;
	clj_coro_cancel_kind(c, CLJ_CANCEL_REQUESTED);
}

// ---- the cycle collector's view (design §7, «Фаза 3»)

// Under c->lock. queued: the channel queues the collector must find holding the waiter.
static int cc_verdict(clj_coro *c, uint32_t *internal, uint32_t *queued) {
	*internal = *queued = 0;
	int state = atomic_load_explicit(&c->state, memory_order_acquire);
	if (c->implicit) return CLJ_CORO_CC_LIVE;
	if (state == CLJ_CORO_DONE) return c->finished ? CLJ_CORO_CC_DONE : CLJ_CORO_CC_LIVE;
	if (state != CLJ_CORO_PARKED || c->cc_collected || atomic_load_explicit(&c->cancel, memory_order_relaxed) != CLJ_CANCEL_NONE)
		return CLJ_CORO_CC_LIVE;
	// A host waits for its on_done: a waiter RC does not see.
	if (c->on_done && clj_is_nil(c->done_value.v)) return CLJ_CORO_CC_LIVE;
	if (c->park_kind == CLJ_WAKE_CHANNEL) {
		// An uncancellable park leaves no waiter here: a cancel would not end it.
		clj_waiter *w = c->waiter;
		if (!w || claimed(w)) return CLJ_CORO_CC_LIVE;
		*queued = atomic_load_explicit(&w->queued, memory_order_relaxed);
	} else if (c->park_kind == CLJ_WAKE_HANDLE) {
		if (!atomic_load_explicit(&c->shadow->suspend, memory_order_relaxed)) return CLJ_CORO_CC_LIVE;
	} else {
		return CLJ_CORO_CC_LIVE;
	}
	// The execution's own reference, released by finish; and its deadline timer's, whose firing is a cancellation too.
	*internal = 1 + (c->deadline_timer != NULL);
	return CLJ_CORO_CC_PARKED;
}

bool clj_coro_cc_locked(clj_coro *c, void (*inside)(clj_coro *c, int verdict, uint32_t internal, uint32_t queued, void *ctx), void *ctx) {
	if (pthread_mutex_trylock(&c->lock) != 0) return false;
	uint32_t internal, queued;
	int      verdict = cc_verdict(c, &internal, &queued);
	inside(c, verdict, internal, queued, ctx);
	pthread_mutex_unlock(&c->lock);
	return true;
}

// The collection holds a reference and saw no wake since it judged c (resume clears the watch), so c is still parked.
bool clj_coro_cc_cancel(clj_coro *c) {
	pthread_mutex_lock(&c->lock);
	bool take = !c->cc_collected && atomic_load_explicit(&c->state, memory_order_acquire) == CLJ_CORO_PARKED;
	if (take) c->cc_collected = true;
	pthread_mutex_unlock(&c->lock);
	if (take) clj_coro_cancel_kind(c, CLJ_CANCEL_REQUESTED);
	return take;
}

void clj_coro_uncancel_scope(clj_coro *c) {
	pthread_mutex_lock(&c->lock);
	if (atomic_load_explicit(&c->cancel, memory_order_relaxed) == CLJ_CANCEL_SCOPE && c->shadow) {
		cancel_cause_clear_locked(c);
		atomic_store_explicit(&c->cancel, CLJ_CANCEL_NONE, memory_order_relaxed);
		atomic_store_explicit(&c->shadow->cancelled, false, memory_order_relaxed);
		deadline_unpoison_locked(c);
		c->shadow->countdown = 1024;
		atomic_store_explicit(&c->shadow->unwinds, 64, memory_order_relaxed);
	}
	pthread_mutex_unlock(&c->lock);
}

void clj_coro_cancel_reset(clj_coro *c) {
	pthread_mutex_lock(&c->lock);
	disarm_locked(c);
	cancel_cause_clear_locked(c);
	// The next job starts unshielded even if the last one died where its pop could not run.
	c->shield = 0;
	atomic_store_explicit(&c->cancel, CLJ_CANCEL_NONE, memory_order_relaxed);
	atomic_store_explicit(&c->shadow->cancelled, false, memory_order_relaxed);
	atomic_store_explicit(&c->shadow->suspend, false, memory_order_relaxed);
	atomic_store_explicit(&c->shadow->deadline, 0, memory_order_relaxed);
	// A spent budget left countdown at 1: the next job, cancelled before it starts, would throw outside its try.
	c->shadow->countdown = 1024;
	atomic_store_explicit(&c->shadow->unwinds, 64, memory_order_relaxed);
	pthread_mutex_unlock(&c->lock);
}

// ---- suspension (design §4, "Стек как объект", item 3): the cancellation's mechanism without the throw

// The gate is the coroutine's own address in the lot. Read under the bucket's lock, so a flag cleared before an
// unpark is either seen here (no park) or found by the unpark (woken): the futex protocol, no wake is lost.
// @ai-generated(guided)
static bool suspend_wait_if(const void *key, void *ctx) {
	const clj_coro *c = key;
	(void)ctx;
	return atomic_load_explicit(&c->shadow->suspend, memory_order_acquire) && !atomic_load_explicit(&c->shadow->cancelled, memory_order_relaxed);
}

// Sticky: the request stands until resume! or a cancellation, and the coroutine meets it at its next tick — a
// parked one is left parked, it is already not running. A cancelled coroutine takes no request: its tick throws.
// @ai-generated(guided)
bool clj_coro_suspend(clj_coro *c) {
	pthread_mutex_lock(&c->lock);
	bool live = atomic_load_explicit(&c->state, memory_order_acquire) != CLJ_CORO_DONE && c->shadow && atomic_load_explicit(&c->cancel, memory_order_relaxed) == CLJ_CANCEL_NONE;
	if (live) {
		atomic_store_explicit(&c->shadow->suspend, true, memory_order_release);
		deadline_poison_locked(c);
	}
	pthread_mutex_unlock(&c->lock);
	return live;
}

bool clj_coro_resume(clj_coro *c) {
	pthread_mutex_lock(&c->lock);
	bool was = c->shadow && atomic_load_explicit(&c->shadow->suspend, memory_order_relaxed);
	if (was) {
		atomic_store_explicit(&c->shadow->suspend, false, memory_order_release);
		deadline_unpoison_locked(c);
	}
	pthread_mutex_unlock(&c->lock);
	// Outside the lock: the wake enqueues the coroutine, which a carrier may pick up at once.
	if (was) clj_lot_unpark_all(c);
	return was;
}

// Under the lock: the carrier that finishes c clears c->shadow under it.
bool clj_coro_suspended(clj_coro *c) {
	pthread_mutex_lock(&c->lock);
	bool s = c->shadow && atomic_load_explicit(&c->shadow->suspend, memory_order_relaxed);
	pthread_mutex_unlock(&c->lock);
	return s;
}

// Legal only where nothing is held (design §4): a cmutex or a claimed lazy seq would stay held until resume!.
// It defers rather than fails — the request came from another coroutine, there is no caller here to fail.
// @ai-generated(guided)
bool clj_coro_suspend_point(void) {
	clj_coro *c = clj_coro_current();
	if (c->host_depth || c->locks_held || c->cmutex_held || c->forcing_held) {
		// Every call until the hold is over, not one per 1024: a loop of a fixed length would otherwise meet the
		// check at the same instruction every turn, and a phase that falls inside the section would never park.
		c->shadow->countdown = 1;
		return atomic_load_explicit(&c->shadow->cancelled, memory_order_relaxed);
	}
	clj_lot_park(c, suspend_wait_if, NULL, clj_wake_handle());
	// The gate is left by a resume! (the deadline is restored: no throw) or by a cancellation (it stays poisoned).
	return atomic_load_explicit(&c->shadow->cancelled, memory_order_relaxed);
}

bool clj_coro_cancel_is_deadline(const clj_coro *c) {
	return atomic_load_explicit(&c->cancel, memory_order_relaxed) == CLJ_CANCEL_DEADLINE;
}

bool clj_coro_cancelled(clj_value coro) {
	clj_coro *c = clj_coro_of(coro);
	return atomic_load_explicit(&c->cancel, memory_order_relaxed) != CLJ_CANCEL_NONE;
}

// A caller with no clj_value for its own execution (nothing has published one without a spawn/publish race).
bool clj_coro_current_cancelled(void) {
	clj_coro *c = clj_coro_current();
	return atomic_load_explicit(&c->cancel, memory_order_relaxed) != CLJ_CANCEL_NONE;
}

static bool seed_turn_give(clj_coro *me);
static void seed_turn_take(clj_coro *me);

void clj_coro_join_blocking(clj_value coro) {
	clj_coro *c = clj_coro_of(coro);
	clj_coro *me = clj_coro_current();
	if (me == c) clj_fatal("a coroutine cannot join itself");
	bool seeded = __builtin_expect(clj_sched_seed_on, 0) && seed_turn_give(me);
	pthread_mutex_lock(&c->lock);
	while (!c->signaled) pthread_cond_wait(&c->cond, &c->lock);
	pthread_mutex_unlock(&c->lock);
	if (seeded) seed_turn_take(me);
}

// ---- timers: one thread, a list sorted by deadline

struct clj_timer {
	uint64_t when;
	void (*fn)(void *ctx);
	void      *ctx;
	clj_timer *next;
};

static pthread_mutex_t timer_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  timer_cv = PTHREAD_COND_INITIALIZER;
static clj_timer      *timers;
static pthread_once_t  timer_once = PTHREAD_ONCE_INIT;
// Timers with a context, pending or firing, under timer_mu: what they hold dies only when they fire or are cancelled.
static size_t          timers_held;
// A callback running, the callbacks finished and the threads waiting for one to finish; under timer_mu.
static bool            timer_firing;
static uint64_t        timer_fires;
static size_t          timer_quiescers;
static pthread_cond_t  timer_fired_cv = PTHREAD_COND_INITIALIZER;

// A timed wait wakes ~0.7 µs later than an untimed one: far deadlines go to a dispatch timer, the wait is untimed.
enum { FAR_NS = 2000000 };

#ifdef __APPLE__
static dispatch_source_t far_timer;
static uint64_t          far_when; // under timer_mu

static void far_fire(void *ctx) {
	(void)ctx;
	pthread_mutex_lock(&timer_mu);
	far_when = 0;
	pthread_cond_signal(&timer_cv);
	pthread_mutex_unlock(&timer_mu);
}

// Under timer_mu: (re)arms the one dispatch timer for `when`; a stale firing is a spurious wake the loop absorbs.
static bool far_wait(uint64_t when, uint64_t wait) {
	if (!far_timer) {
		// Higher than the carriers on purpose: a deadline must fire on time whatever the class of the work it ends,
		// and firing it is a wake, not the work.
		far_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, DISPATCH_TIMER_STRICT, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));
		if (!far_timer) return false;
		dispatch_source_set_event_handler_f(far_timer, far_fire);
		dispatch_resume(far_timer);
	}
	if (far_when != when) {
		dispatch_source_set_timer(far_timer, dispatch_time(DISPATCH_TIME_NOW, (int64_t)wait), DISPATCH_TIME_FOREVER, 0);
		far_when = when;
	}
	pthread_cond_wait(&timer_cv, &timer_mu);
	return true;
}
#else
static bool far_wait(uint64_t when, uint64_t wait) {
	(void)when;
	(void)wait;
	return false;
}
#endif

static void *timer_main(void *arg) {
	(void)arg;
	pthread_mutex_lock(&timer_mu);
	for (;;) {
		if (!timers) {
			pthread_cond_wait(&timer_cv, &timer_mu);
			continue;
		}
		uint64_t now = clj_profile_now();
		if (timers->when > now) {
			uint64_t wait = timers->when - now;
			if (wait > FAR_NS && far_wait(timers->when, wait)) continue;
			cond_wait_ns(&timer_cv, &timer_mu, wait);
			continue;
		}
		clj_timer *t = timers;
		timers = t->next;
		bool held = t->ctx != NULL;
		timer_firing = true;
		pthread_mutex_unlock(&timer_mu);
		t->fn(t->ctx);
		free(t);
		pthread_mutex_lock(&timer_mu);
		timers_held -= held;
		timer_firing = false;
		timer_fires++;
		if (timer_quiescers) pthread_cond_broadcast(&timer_fired_cv);
	}
	return NULL;
}

// Returns once the callback running at the call, if any, has returned: what it popped before a cancel is done.
static void timer_quiesce(void) {
	pthread_mutex_lock(&timer_mu);
	uint64_t seen = timer_fires;
	timer_quiescers++;
	while (timer_firing && timer_fires == seen) pthread_cond_wait(&timer_fired_cv, &timer_mu);
	timer_quiescers--;
	pthread_mutex_unlock(&timer_mu);
}

static void start_timer(void) {
	pthread_t      t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&t, &attr, timer_main, NULL) != 0) clj_fatal("pthread_create of the timer thread failed");
	pthread_attr_destroy(&attr);
}

// The thread is signalled only when the new timer is the earliest: the others are behind one it already waits for.
static void seed_timer_added(void);

clj_timer *clj_sched_timer(uint64_t ns, void (*fn)(void *ctx), void *ctx) {
	bool seeded = __builtin_expect(clj_sched_seed_on, 0);
	// Seeded, the carrier fires the timers itself, on the virtual clock.
	if (seeded) clj_sched_init();
	else pthread_once(&timer_once, start_timer);
	clj_timer *t = malloc(sizeof *t);
	if (!t) clj_fatal("out of memory");
	t->when = clj_sched_now() + ns;
	t->fn = fn;
	t->ctx = ctx;
	pthread_mutex_lock(&timer_mu);
	clj_timer **at = &timers;
	while (*at && (*at)->when <= t->when) at = &(*at)->next;
	t->next = *at;
	*at = t;
	timers_held += ctx != NULL;
	bool first = timers == t;
	pthread_mutex_unlock(&timer_mu);
	if (seeded) seed_timer_added();
	else if (first) pthread_cond_signal(&timer_cv);
	return t;
}

bool clj_sched_timer_cancel(clj_timer *t) {
	pthread_mutex_lock(&timer_mu);
	clj_timer **at = &timers;
	while (*at && *at != t) at = &(*at)->next;
	bool found = *at == t;
	if (found) {
		*at = t->next;
		timers_held -= t->ctx != NULL;
	}
	pthread_mutex_unlock(&timer_mu);
	if (found) free(t);
	return found;
}

// ---- the deadline as a cancellation by timer (eval.h clj_deadline_set_ms): one mechanism with cancel!

typedef struct {
	clj_coro *coro; // retained
	uint64_t  serial;
} deadline_ctx;

static void deadline_fire(void *arg) {
	deadline_ctx *d = arg;
	clj_coro     *c = d->coro;
	pthread_mutex_lock(&c->lock);
	// The timer thread frees this timer when the call returns, live or not: no disarm may read it after.
	if (c->deadline_timer && c->deadline_timer->ctx == d) c->deadline_timer = NULL;
	bool live = d->serial == c->deadline_serial && atomic_load_explicit(&c->state, memory_order_acquire) != CLJ_CORO_DONE && c->shadow;
	clj_waiter *w = NULL;
	if (live) w = cancel_locked(c, CLJ_CANCEL_DEADLINE, CLJ_NIL);
	pthread_mutex_unlock(&c->lock);
	if (w) {
		if (clj_waiter_claim(w)) clj_resume_far(w);
		clj_waiter_release(w);
	}
	clj_release(clj_from_ptr(c));
	free(d);
}

// Under c->lock: the timer's ctx is freed here when it never fired, by the firing otherwise.
static void disarm_locked(clj_coro *c) {
	c->deadline_serial++;
	clj_timer *t = c->deadline_timer;
	c->deadline_timer = NULL;
	if (!t) return;
	deadline_ctx *d = t->ctx;
	if (clj_sched_timer_cancel(t)) {
		clj_release(clj_from_ptr(d->coro));
		free(d);
	}
}

static void deadline_disarm(clj_coro *c) {
	pthread_mutex_lock(&c->lock);
	disarm_locked(c);
	pthread_mutex_unlock(&c->lock);
}

void clj_coro_deadline_arm(clj_coro *c) {
	pthread_mutex_lock(&c->lock);
	disarm_locked(c);
	uint64_t deadline = c->shield ? c->deadline_before : (c->shadow ? clj_shadow_deadline(c->shadow) : 0);
	if (deadline > 1) {
		deadline_ctx *d = malloc(sizeof *d);
		if (!d) clj_fatal("out of memory");
		d->coro = c;
		d->serial = c->deadline_serial;
		clj_retain(clj_from_ptr(c));
		uint64_t now = clj_sched_now();
		c->deadline_timer = clj_sched_timer(deadline > now ? deadline - now : 0, deadline_fire, d);
	}
	pthread_mutex_unlock(&c->lock);
}

void clj_coro_deadline_cleared(clj_coro *c) {
	pthread_mutex_lock(&c->lock);
	disarm_locked(c);
	if (atomic_load_explicit(&c->cancel, memory_order_relaxed) == CLJ_CANCEL_DEADLINE && c->shadow) {
		cancel_cause_clear_locked(c);
		atomic_store_explicit(&c->cancel, CLJ_CANCEL_NONE, memory_order_relaxed);
		atomic_store_explicit(&c->shadow->cancelled, false, memory_order_relaxed);
		deadline_unpoison_locked(c);
	}
	pthread_mutex_unlock(&c->lock);
}

// The owner setting its own deadline while a poison stands: the new value waits in deadline_before with the old
// one, or the ring would stop meeting the flag that poisoned it (eval.c's deadline_apply).
// @ai-generated(guided)
void clj_coro_deadline_replace(clj_coro *c, uint64_t deadline) {
	pthread_mutex_lock(&c->lock);
	c->deadline_before = deadline;
	if (!c->shield) {
		atomic_store_explicit(&c->shadow->deadline, deadline, memory_order_relaxed);
		deadline_poison_if_locked(c);
	}
	pthread_mutex_unlock(&c->lock);
}

uint64_t clj_coro_deadline_own(clj_coro *c) {
	pthread_mutex_lock(&c->lock);
	uint64_t d = deadline_own_locked(c);
	pthread_mutex_unlock(&c->lock);
	return d;
}

// ---- Thread/sleep: a park on the timer thread, cancellable

static void sleep_fire(void *ctx) {
	clj_waiter *w = ctx;
	if (clj_waiter_claim(w)) clj_resume_far(w);
	clj_waiter_release(w);
}

clj_value clj_sched_sleep_ms(int64_t ms) {
	if (!clj_park_allowed()) return CLJ_THROWN;
	clj_coro   *c = clj_coro_current();
	clj_waiter *w = clj_waiter_new(c, CLJ_NIL);
	clj_waiter_retain(w);
	clj_sched_timer(ms < 0 ? 0 : (uint64_t)ms * 1000000u, sleep_fire, w);
	clj_park(w, clj_wake_timer());
	clj_waiter_release(w);
	if (atomic_load_explicit(&c->shadow->cancelled, memory_order_relaxed)) return clj_throw_cancelled(clj_coro_cancel_is_deadline(c));
	return CLJ_NIL;
}

// ---- the blocking pools: threads made on demand, retired after a minute idle

typedef struct job {
	void (*fn)(void *ctx);
	void       *ctx;  // the caller's copy for a parking caller (ctx_copy), the caller's own for a detached job
	clj_waiter *w;
	struct job *next;
	char        ctx_copy[];
} job;

typedef struct {
	pthread_mutex_t mu;
	pthread_cond_t  cv;
	job            *head, *tail;
	size_t          threads, idle, queued;
	size_t          held; // queued or running
	size_t          max;  // 0: no cap
} pool;

// The loader's file reads never wait on one another: a capped pool may queue them.
static pool jobs_pool = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, .max = 64};
// `thread` bodies wait on one another, as JVM threads may: a thread each at once, no cap (the JVM's cached pool).
static pool bodies_pool = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, .max = 0};

static _Atomic uint64_t keep_alive_ns = 60000000000u;

// Under p->mu. False when the thread retires: it uncounts itself in the same hold that found nothing queued.
static bool await_job(pool *p) {
	uint64_t since = clj_profile_now();
	p->idle++;
	while (!p->head) {
		uint64_t keep = atomic_load_explicit(&keep_alive_ns, memory_order_relaxed), idle = clj_profile_now() - since;
		if (idle >= keep) break;
		cond_wait_ns(&p->cv, &p->mu, keep - idle);
	}
	p->idle--;
	if (p->head) return true;
	p->threads--;
	return false;
}

static void *blocking_main(void *arg) {
	pool *p = arg;
	pthread_mutex_lock(&p->mu);
	while (await_job(p)) {
		job *j = p->head;
		p->head = j->next;
		if (!p->head) p->tail = NULL;
		p->queued--;
		pthread_mutex_unlock(&p->mu);
#if CLJ_DEBUG
		// A job with a waiter works for its parked caller, which touches nothing until the wake: a transfer both ways.
		uint32_t own = j->w ? clj_debug_owner_assume(j->w->coro->debug_owner) : 0;
#endif
		void *own_candidates = j->w ? clj_cc_local_borrow(j->w->coro) : NULL;
		j->fn(j->ctx);
		if (j->w) clj_cc_local_return(j->w->coro, own_candidates);
#if CLJ_DEBUG
		if (j->w) clj_debug_owner_assume(own);
#endif
		clj_waiter *w = j->w;
		if (w) {
			// The claim publishes the copy; the parker reads it back and frees the job, so j is not touched past here.
			if (clj_waiter_claim(w)) clj_resume_far(w);
			clj_waiter_release(w);
		} else {
			free(j);
		}
		pthread_mutex_lock(&p->mu);
		p->held--;
	}
	pthread_mutex_unlock(&p->mu);
	// The thread's exit frees its implicit coroutine, which a deadline fire popped before the last job's disarm reads.
	timer_quiesce();
	return NULL;
}

static job *submit(pool *p, void (*fn)(void *ctx), void *ctx, size_t copy, clj_waiter *w) {
	job *j = malloc(sizeof *j + copy);
	if (!j) clj_fatal("out of memory");
	j->fn = fn;
	j->ctx = ctx;
	if (copy) {
		memcpy(j->ctx_copy, ctx, copy);
		j->ctx = j->ctx_copy;
	}
	j->w = w;
	j->next = NULL;
	pthread_mutex_lock(&p->mu);
	if (p->tail) p->tail->next = j;
	else p->head = j;
	p->tail = j;
	p->held++;
	p->queued++;
	// One idle thread serves one queued job: a job must never wait behind thread bodies that block on its output.
	bool spawn_thread = p->queued > p->idle && (!p->max || p->threads < p->max);
	if (spawn_thread) p->threads++;
	pthread_mutex_unlock(&p->mu);
	if (spawn_thread) {
		pthread_t      t;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&t, &attr, blocking_main, p) != 0) clj_fatal("pthread_create of a blocking thread failed");
		pthread_attr_destroy(&attr);
	}
	pthread_cond_signal(&p->cv);
	return j;
}

// The job works on a heap copy of ctx, written back here: the parker's frame is nobody else's (design §4).
void clj_blocking(void (*fn)(void *ctx), void *ctx, size_t size) {
	clj_coro *c = clj_coro_current();
	// Seeded, the job is done in place: a pool thread's completion would land in the schedule at a real moment.
	if (c->implicit || c->host_depth || c->locks_held || clj_sched_seed_on) {
		fn(ctx);
		return;
	}
	clj_waiter *w = clj_waiter_new(c, CLJ_NIL);
	clj_waiter_retain(w);
	job *j = submit(&jobs_pool, fn, ctx, size, w);
	clj_park(w, clj_wake_thread());
	memcpy(ctx, j->ctx_copy, size);
	free(j);
	clj_waiter_release(w);
}

void clj_blocking_detach(void (*fn)(void *ctx), void *ctx) { submit(&bodies_pool, fn, ctx, 0, NULL); }

uint64_t clj_debug_coro_spawned(void) { return atomic_load_explicit(&spawned, memory_order_relaxed); }

size_t clj_debug_timers_held(void) {
	pthread_mutex_lock(&timer_mu);
	size_t n = timers_held;
	pthread_mutex_unlock(&timer_mu);
	return n;
}

static size_t pool_read(pool *p, const size_t *field) {
	pthread_mutex_lock(&p->mu);
	size_t n = *field;
	pthread_mutex_unlock(&p->mu);
	return n;
}

size_t clj_debug_blocking_threads(void) { return pool_read(&bodies_pool, &bodies_pool.threads); }

size_t clj_debug_blocking_held(void) { return pool_read(&jobs_pool, &jobs_pool.held) + pool_read(&bodies_pool, &bodies_pool.held); }

void clj_debug_ticks_spend(void) {
	clj_shadow_stack *s = clj_coro_current()->shadow;
	s->countdown = 1;
	atomic_store_explicit(&s->unwinds, 0, memory_order_relaxed);
}

void clj_debug_blocking_keep_alive_ms(uint64_t ms) {
	atomic_store_explicit(&keep_alive_ns, (ms ? ms : 60000) * 1000000u, memory_order_relaxed);
	// Idle threads wait out the keep-alive they read: a wake makes them read it again.
	pool *pools[] = {&jobs_pool, &bodies_pool};
	for (size_t i = 0; i < 2; i++) {
		pthread_mutex_lock(&pools[i]->mu);
		pthread_cond_broadcast(&pools[i]->cv);
		pthread_mutex_unlock(&pools[i]->mu);
	}
}

static bool runtime_idle(size_t coros) {
	return clj_debug_live_coros() <= coros && clj_debug_timers_held() == 0 && clj_debug_blocking_held() == 0;
}

// A coroutine's count drops at its finalize, before its children are freed: the object count must hold still too.
static bool runtime_settle(size_t coros, uint64_t ms) {
	enum { COLLECT_EVERY_NS = 20000000 };
	uint64_t deadline = clj_profile_now() + ms * 1000000u, collect_at = 0;
	for (;;) {
		if (runtime_idle(coros)) {
			clj_cc_collect();
			clj_output_flush();
			int64_t objects = clj_debug_live_objects();
			usleep(1000);
			if (runtime_idle(coros) && clj_debug_live_objects() == objects) return true;
		} else if (clj_debug_live_coros() > coros && clj_profile_now() >= collect_at) {
			// A coroutine parked on garbage leaves only when a collection cancels it (design §7, «Фаза 3»).
			clj_cc_collect();
			collect_at = clj_profile_now() + COLLECT_EVERY_NS;
		}
		if (clj_profile_now() > deadline) return false;
		usleep(200);
	}
}

bool clj_debug_runtime_settle(size_t coros, uint64_t ms) {
	clj_sched_seed_settling(1);
	bool r = runtime_settle(coros, ms);
	clj_sched_seed_settling(-1);
	return r;
}

// Test hook: the dev backstop that refuses a park while a clj_lock is held.
bool clj_debug_park_under_lock_is_error(void) {
	static clj_lock probe = CLJ_LOCK_INIT;
	clj_lock_lock(&probe);
	bool allowed = clj_park_allowed();
	clj_lock_unlock(&probe);
	if (allowed) return false;
	clj_value ex = clj_take_pending();
	clj_value msg = clj_ex_message(ex);
	bool ok = clj_is_string(msg) && strcmp(clj_string_bytes(msg), "Cannot park while a runtime lock is held") == 0;
	clj_release(msg);
	clj_release(ex);
	return ok;
}

size_t clj_debug_sched_sleeping(void) {
	pthread_mutex_lock(&run_mu);
	size_t n = 0;
	for (clj_carrier *o = idle_head; o; o = o->idle_next) n += !o->polling;
	pthread_mutex_unlock(&run_mu);
	return n;
}

size_t clj_debug_sched_carriers(void) {
	clj_sched_init();
	return ncarriers;
}

static void seed_dump(void);

// Debug: the scheduler's state on stderr (a watchdog's view of a hang).
void clj_debug_sched_dump(void) {
	pthread_mutex_lock(&run_mu);
	size_t queued = 0;
	for (clj_coro *c = run_head; c; c = c->next) queued++;
	size_t idle = 0;
	for (clj_carrier *o = idle_head; o; o = o->idle_next) idle++;
	fprintf(stderr, "sched: spawned %llu live %zu queued %zu idle %zu/%zu polling %zu spinning %zu woken %d\n", (unsigned long long)atomic_load(&spawned), clj_debug_live_coros(), queued, idle, ncarriers, polling_carriers, spinning_carriers, woken);
	if (clj_sched_seed_on) seed_dump();
	for (clj_carrier *o = carriers; o; o = o->pool_next) {
		fprintf(stderr, "  carrier %p next %p current %p idle %d polling %d\n", (void *)o, (void *)o->next, (void *)__atomic_load_n(&o->current, __ATOMIC_RELAXED), o->idle, o->polling);
	}
	pthread_mutex_unlock(&run_mu);
}

// ---- the seeded mode (design §3 «Корректность реализации», item 4; NOTES "Scheduler")

bool clj_sched_seed_on;

enum {
	// What one deadline tick (1024 calls or loop turns) stands for on the virtual clock.
	SEED_TICK_NS = 100000,
	// Runs an entry waits for at most before it begins: a pool that never runs dry still lets the host in.
	SEED_DRAIN_MAX = 100000,
};
// Real quiet before a host waiting outside the runtime lets virtual time pass.
static const uint64_t SEED_GRACE_NS = 50000000;
// Real time every execution sat parked with no timer pending before the report.
static const uint64_t SEED_STUCK_NS = 1000000000;
// A fixed origin: nano-time reads the same in every run of a seed.
static const uint64_t SEED_CLOCK_ORIGIN = 1000000000000ull;

static pthread_mutex_t seed_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  seed_cv = PTHREAD_COND_INITIALIZER;
static uint64_t        seed_value;
static uint64_t        sched_rng, prog_rng; // under seed_mu
static uint32_t        yield_shift;         // a preemption point yields with probability 2^-yield_shift
static clj_coro      **bag;                 // the runnable, bare threads among them; under seed_mu
static size_t          bag_n, bag_cap;
static clj_carrier    *seed_car;            // written once by the carrier before it runs anything
// Under seed_mu: bare threads running inside an evaluation, parked inside one, waiting to begin one; settles.
static uint32_t running_bare, parked_bare, entering, settling;
static bool     carrier_idle = true; // the carrier is waiting, so a bare thread may take the turn
static uint64_t drain_steps;         // coroutine runs since the last bare thread left its evaluation
static bool     blocked_warned;
static _Atomic uint64_t vclock;
static uint64_t         wall_origin_ms;
// A yielding coroutine's waiter, left for the carrier it switches out to: one carrier, one at a time.
static clj_waiter *yielder;

static uint64_t splitmix(uint64_t *s) {
	uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

static void reseed_locked(uint64_t seed) {
	uint64_t s = seed;
	sched_rng = splitmix(&s);
	prog_rng = splitmix(&s);
	yield_shift = 1 + (uint32_t)(splitmix(&s) % 4);
	clj_chan_serial_reset();
}

void clj_sched_seed_configure(void) {
	const char *env = getenv("CLJ_SCHED_SEED");
	if (!env || !*env) return;
	char *end;
	errno = 0;
	unsigned long long v = strtoull(env, &end, 0);
	if (*end || errno) clj_fatal("CLJ_SCHED_SEED is not a number");
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	wall_origin_ms = (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
	atomic_store_explicit(&vclock, SEED_CLOCK_ORIGIN, memory_order_relaxed);
	seed_value = v;
	reseed_locked(v);
	clj_sched_seed_on = true;
}

bool clj_sched_seeded(uint64_t *seed) {
	if (seed) *seed = seed_value;
	return clj_sched_seed_on;
}

uint64_t clj_sched_now(void) { return clj_sched_seed_on ? atomic_load_explicit(&vclock, memory_order_relaxed) : clj_profile_now(); }

uint64_t clj_sched_tick_now(void) {
	if (!clj_sched_seed_on) return clj_profile_now();
	return atomic_fetch_add_explicit(&vclock, SEED_TICK_NS, memory_order_relaxed) + SEED_TICK_NS;
}

uint64_t clj_sched_wall_ms(void) {
	if (!clj_sched_seed_on) {
		struct timespec t;
		clock_gettime(CLOCK_REALTIME, &t);
		return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
	}
	return wall_origin_ms + (atomic_load_explicit(&vclock, memory_order_relaxed) - SEED_CLOCK_ORIGIN) / 1000000u;
}

uint64_t clj_sched_seed_random(void) {
	pthread_mutex_lock(&seed_mu);
	uint64_t r = splitmix(&prog_rng);
	pthread_mutex_unlock(&seed_mu);
	return r;
}

static void bag_push_locked(clj_coro *c) {
	if (bag_n == bag_cap) {
		bag_cap = bag_cap ? bag_cap * 2 : 64;
		bag = realloc(bag, bag_cap * sizeof *bag);
		if (!bag) clj_fatal("out of memory");
	}
	bag[bag_n++] = c;
}

static clj_coro *bag_pick_locked(void) {
	size_t    i = (size_t)(splitmix(&sched_rng) % bag_n);
	clj_coro *c = bag[i];
	bag[i] = bag[--bag_n];
	return c;
}

static bool coin_locked(void) { return (splitmix(&sched_rng) & ((1ull << yield_shift) - 1)) == 0; }

static void seed_enqueue(clj_coro *c) {
	pthread_mutex_lock(&seed_mu);
	bag_push_locked(c);
	if (carrier_idle) pthread_cond_broadcast(&seed_cv);
	pthread_mutex_unlock(&seed_mu);
}

// No seed_mu: the caller may hold a coroutine's lock, which nests outside it. A missed wake waits for a timed one.
static void seed_timer_added(void) { pthread_cond_broadcast(&seed_cv); }

static bool seed_timer_next(uint64_t *when) {
	pthread_mutex_lock(&timer_mu);
	bool any = timers != NULL;
	if (any) *when = timers->when;
	pthread_mutex_unlock(&timer_mu);
	return any;
}

// The timer thread's step without the thread: the earliest timer, if due, popped and run on the carrier.
static bool seed_timer_fire(uint64_t now) {
	pthread_mutex_lock(&timer_mu);
	clj_timer *t = timers;
	if (!t || t->when > now) {
		pthread_mutex_unlock(&timer_mu);
		return false;
	}
	timers = t->next;
	bool held = t->ctx != NULL;
	timer_firing = true;
	pthread_mutex_unlock(&timer_mu);
	t->fn(t->ctx);
	free(t);
	pthread_mutex_lock(&timer_mu);
	timers_held -= held;
	timer_firing = false;
	timer_fires++;
	if (timer_quiescers) pthread_cond_broadcast(&timer_fired_cv);
	pthread_mutex_unlock(&timer_mu);
	return true;
}

// Under seed_mu, on the carrier: waiting is what lets a bare thread begin an evaluation.
static void seed_wait_locked(uint64_t ns) {
	carrier_idle = true;
	pthread_cond_broadcast(&seed_cv);
	if (ns) cond_wait_ns(&seed_cv, &seed_mu, ns);
	else pthread_cond_wait(&seed_cv, &seed_mu);
}

static void seed_report_stuck(void) {
	char line[256];
	snprintf(line, sizeof line, "clj: seeded scheduler (CLJ_SCHED_SEED=%llu): every execution is parked and no timer is pending; waiting for an event from outside the runtime\n", (unsigned long long)seed_value);
	put(line);
}

static void *seed_carrier_main(void *arg) {
	(void)arg;
	clj_carrier *car = clj_carrier_here();
	car->pooled = true;
	pthread_mutex_lock(&run_mu);
	car->pool_next = carriers;
	__atomic_store_n(&carriers, car, __ATOMIC_RELEASE);
	pthread_mutex_unlock(&run_mu);
	__atomic_store_n(&seed_car, car, __ATOMIC_RELEASE);
	uint64_t quiet_since = 0, stuck_since = 0;
	bool     collected = false, reported = false;
	pthread_mutex_lock(&seed_mu);
	for (;;) {
		// An entry begins at a drained pool, so its state is the schedule's, not the host's timing.
		if (running_bare || (entering && (!bag_n || drain_steps >= SEED_DRAIN_MAX))) {
			seed_wait_locked(0);
			continue;
		}
		carrier_idle = false;
		uint64_t now = atomic_load_explicit(&vclock, memory_order_relaxed);
		pthread_mutex_unlock(&seed_mu);
		bool fired = seed_timer_fire(now);
		pthread_mutex_lock(&seed_mu);
		if (fired) continue;
		if (bag_n) {
			clj_coro *c = bag_pick_locked();
			quiet_since = stuck_since = 0;
			collected = reported = false;
			if (c->implicit) {
				// The bare thread goes on; the carrier waits for it to park or leave its evaluation.
				c->seed_picked = true;
				running_bare++;
				pthread_cond_broadcast(&seed_cv);
				continue;
			}
			drain_steps++;
			pthread_mutex_unlock(&seed_mu);
			run_one(car, c);
			clj_waiter *w = yielder;
			if (w) {
				yielder = NULL;
				if (clj_waiter_claim(w)) clj_resume_far(w);
				clj_waiter_release(w);
			}
			pthread_mutex_lock(&seed_mu);
			continue;
		}
		// Only the clock or the collector can move the model: at once for a waiter inside it, after quiet for the host.
		uint64_t real = clj_profile_now();
		if (!quiet_since) quiet_since = real;
		bool waited = parked_bare || settling || real - quiet_since >= SEED_GRACE_NS;
		if (!waited) {
			seed_wait_locked(quiet_since + SEED_GRACE_NS - real);
			continue;
		}
		uint64_t when;
		pthread_mutex_unlock(&seed_mu);
		bool any = seed_timer_next(&when);
		pthread_mutex_lock(&seed_mu);
		if (any) {
			// A tick may have moved the clock past it already.
			if (when > atomic_load_explicit(&vclock, memory_order_relaxed)) atomic_store_explicit(&vclock, when, memory_order_relaxed);
			continue;
		}
		if (!collected) {
			// A parked coroutine the collector judges garbage is cancelled, which may wake what waits on the model.
			collected = true;
			pthread_mutex_unlock(&seed_mu);
			clj_cc_seed_collect();
			pthread_mutex_lock(&seed_mu);
			continue;
		}
		if (parked_bare) {
			if (!stuck_since) stuck_since = real;
			else if (!reported && real - stuck_since >= SEED_STUCK_NS) {
				reported = true;
				seed_report_stuck();
			}
		}
		seed_wait_locked(SEED_STUCK_NS);
	}
	return NULL;
}

// Under seed_mu: the carrier waits once the pool is drained, and the bare thread then holds the turn.
static void turn_take_locked(clj_coro *me) {
	entering++;
	pthread_cond_broadcast(&seed_cv);
	while (!carrier_idle) pthread_cond_wait(&seed_cv, &seed_mu);
	entering--;
	running_bare++;
	me->seed_in = true;
}

void clj_sched_seed_enter(clj_coro *c) {
	// The carrier's own thread runs timer callbacks and the collector between coroutines: the turn is its already.
	if (!c->implicit || c->seed_in || c->carrier == __atomic_load_n(&seed_car, __ATOMIC_ACQUIRE)) return;
	pthread_mutex_lock(&seed_mu);
	turn_take_locked(c);
	pthread_mutex_unlock(&seed_mu);
}

void clj_sched_seed_leave(clj_coro *c) {
	if (!c->seed_in) return;
	pthread_mutex_lock(&seed_mu);
	c->seed_in = false;
	running_bare--;
	drain_steps = 0;
	pthread_cond_broadcast(&seed_cv);
	pthread_mutex_unlock(&seed_mu);
}

// Under c->lock. A pool coroutine blocking here holds the only carrier: only a waker outside the pool frees it.
static bool seed_block(clj_coro *c) {
	if (!c->implicit) {
		if (!blocked_warned) {
			blocked_warned = true;
			put("clj: seeded scheduler: a coroutine blocks the only carrier (a park under a host call or a runtime lock)\n");
		}
		return false;
	}
	if (!c->seed_in) return false;
	pthread_mutex_lock(&seed_mu);
	c->seed_in = false;
	c->seed_parked = true;
	running_bare--;
	parked_bare++;
	pthread_cond_broadcast(&seed_cv);
	pthread_mutex_unlock(&seed_mu);
	return true;
}

// Under c->lock, from the resume: the woken bare thread runs again when the pick names it, not at once.
static void seed_bare_ready(clj_coro *c) {
	pthread_mutex_lock(&seed_mu);
	if (c->seed_parked) {
		c->seed_parked = false;
		parked_bare--;
		bag_push_locked(c);
		if (carrier_idle) pthread_cond_broadcast(&seed_cv);
	}
	pthread_mutex_unlock(&seed_mu);
}

static void seed_bare_wait_pick(clj_coro *c) {
	pthread_mutex_lock(&seed_mu);
	while (!c->seed_picked) pthread_cond_wait(&seed_cv, &seed_mu);
	c->seed_picked = false;
	c->seed_in = true;
	pthread_mutex_unlock(&seed_mu);
}

// A bare thread's wait outside clj_park (a join): the turn goes to the pool and is taken back as at an entry.
static bool seed_turn_give(clj_coro *me) {
	if (!me->seed_in) return false;
	pthread_mutex_lock(&seed_mu);
	me->seed_in = false;
	running_bare--;
	parked_bare++;
	pthread_cond_broadcast(&seed_cv);
	pthread_mutex_unlock(&seed_mu);
	return true;
}

static void seed_turn_take(clj_coro *me) {
	pthread_mutex_lock(&seed_mu);
	parked_bare--;
	turn_take_locked(me);
	pthread_mutex_unlock(&seed_mu);
}

void clj_sched_seed_settling(int delta) {
	if (!clj_sched_seed_on) return;
	pthread_mutex_lock(&seed_mu);
	settling = (uint32_t)((int)settling + delta);
	pthread_cond_broadcast(&seed_cv);
	pthread_mutex_unlock(&seed_mu);
}

// Where parking is refused the execution goes on, as it would have; a cancelled one meets its cancel at the park.
void clj_sched_point_slow(void) {
	clj_coro *c = clj_coro_tls;
	if (!c || c->locks_held) return;
	if (c->implicit) {
		if (!c->seed_in) return;
		pthread_mutex_lock(&seed_mu);
		// An empty bag has nothing to put first; before the first spawn there is no carrier to hand the turn to.
		if (bag_n && coin_locked()) {
			c->seed_in = false;
			running_bare--;
			bag_push_locked(c);
			pthread_cond_broadcast(&seed_cv);
			while (!c->seed_picked) pthread_cond_wait(&seed_cv, &seed_mu);
			c->seed_picked = false;
			c->seed_in = true;
		}
		pthread_mutex_unlock(&seed_mu);
		return;
	}
	if (c->carrier != __atomic_load_n(&seed_car, __ATOMIC_ACQUIRE) || c->affinity != CLJ_AFFINITY_POOL || c->host_depth ||
	    atomic_load_explicit(&c->cancel, memory_order_relaxed) != CLJ_CANCEL_NONE)
		return;
	pthread_mutex_lock(&seed_mu);
	bool yield = coin_locked();
	pthread_mutex_unlock(&seed_mu);
	if (!yield) return;
	clj_waiter *w = clj_waiter_new(c, CLJ_NIL);
	clj_waiter_retain(w);
	yielder = w;
	clj_park(w, clj_wake_yield());
	clj_waiter_release(w);
}

void clj_debug_sched_reseed(uint64_t seed) {
	if (!clj_sched_seed_on) return;
	clj_sched_init();
	pthread_mutex_lock(&seed_mu);
	entering++;
	pthread_cond_broadcast(&seed_cv);
	while (!carrier_idle) pthread_cond_wait(&seed_cv, &seed_mu);
	entering--;
	reseed_locked(seed);
	drain_steps = 0;
	pthread_mutex_unlock(&seed_mu);
}

static void seed_dump(void) {
	pthread_mutex_lock(&seed_mu);
	fprintf(stderr, "sched: seeded (CLJ_SCHED_SEED=%llu, yield 1/%u): runnable %zu, bare running %u parked %u entering %u, carrier %s, virtual clock +%llu ms\n",
	        (unsigned long long)seed_value, 1u << yield_shift, bag_n, running_bare, parked_bare, entering, carrier_idle ? "idle" : "busy",
	        (unsigned long long)((atomic_load_explicit(&vclock, memory_order_relaxed) - SEED_CLOCK_ORIGIN) / 1000000u));
	pthread_mutex_unlock(&seed_mu);
}
