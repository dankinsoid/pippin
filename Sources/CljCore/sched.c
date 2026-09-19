// @ai-generated(solo)
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <pthread/qos.h>
#endif

#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/printer.h"
#include "clj/profile.h"
#include "clj/string.h"
#include "clj/vector.h"
#include "coro_internal.h"
#include "profile_internal.h"

// ---- the run queue and the carriers

static pthread_once_t   init_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t  run_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   run_cv = PTHREAD_COND_INITIALIZER;
static clj_coro        *run_head, *run_tail;
static clj_carrier     *carriers; // the pool, linked through pool_next
static size_t           ncarriers, idle_carriers;
static _Atomic uint64_t spawned;

// A next slot younger than this stays with its carrier: a pair handing values back and forth keeps one thread.
enum { NEXT_STEAL_AGE_NS = 5000 };

static void queue_push(clj_coro **head, clj_coro **tail, clj_coro *c) {
	c->next = NULL;
	if (*tail) (*tail)->next = c;
	else *head = c;
	*tail = c;
}

static clj_coro *queue_pop(clj_coro **head, clj_coro **tail) {
	clj_coro *c = *head;
	if (!c) return NULL;
	*head = c->next;
	if (!*head) *tail = NULL;
	c->next = NULL;
	return c;
}

static void finish(clj_coro *c);

// After a switch back: the coroutine parked holding its lock (unlocked here, on its carrier) or finished.
static void run_one(clj_carrier *car, clj_coro *c) {
	clj_coro_switch_in(car, c);
	if (atomic_load_explicit(&c->state, memory_order_acquire) == CLJ_CORO_DONE) {
		finish(c);
		return;
	}
	clj_coro_advise_stack(c);
	pthread_mutex_unlock(&c->lock);
}

// Under run_mu: the carrier's own next slot, else the queue, else another carrier's slot old enough to steal.
static clj_coro *take_work_locked(clj_carrier *car) {
	clj_coro *c = car->next;
	if (c) {
		car->next = NULL;
		return c;
	}
	c = queue_pop(&run_head, &run_tail);
	if (c) return c;
	uint64_t now = clj_profile_now();
	for (clj_carrier *o = carriers; o; o = o->pool_next) {
		if (o->next && now - o->next_at > NEXT_STEAL_AGE_NS) {
			c = o->next;
			o->next = NULL;
			return c;
		}
	}
	return NULL;
}

static void *carrier_main(void *arg) {
	(void)arg;
#ifdef __APPLE__
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif
	clj_carrier *car = clj_carrier_here();
	car->pooled = true;
	pthread_mutex_lock(&run_mu);
	car->pool_next = carriers;
	carriers = car;
	pthread_mutex_unlock(&run_mu);
	for (;;) {
		pthread_mutex_lock(&run_mu);
		clj_coro *c;
		while (!(c = take_work_locked(car))) {
			// A fresh next slot elsewhere may age into stealable: look again after a short wait.
			bool            fresh_next = false;
			for (clj_carrier *o = carriers; o && !fresh_next; o = o->pool_next) fresh_next = o->next != NULL;
			idle_carriers++;
			if (fresh_next) {
				struct timespec ts;
				clock_gettime(CLOCK_REALTIME, &ts);
				ts.tv_nsec += 50000;
				if (ts.tv_nsec >= 1000000000L) {
					ts.tv_sec++;
					ts.tv_nsec -= 1000000000L;
				}
				pthread_cond_timedwait(&run_cv, &run_mu, &ts);
			} else {
				pthread_cond_wait(&run_cv, &run_mu);
			}
			idle_carriers--;
		}
		pthread_mutex_unlock(&run_mu);
		run_one(car, c);
	}
	return NULL;
}

static void start_carriers(void) {
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

// From a pooled carrier the coroutine takes the carrier's next slot (an earlier occupant moves to the queue), so a
// resumer that parks right after hands its thread over without waking another; idle carriers are still told.
void clj_sched_enqueue(clj_coro *c) {
	if (c->affinity == CLJ_AFFINITY_MAIN) {
		enqueue_main(c);
		return;
	}
	clj_coro    *me = clj_coro_tls;
	clj_carrier *car = me && me->carrier && me->carrier->pooled ? me->carrier : NULL;
	pthread_mutex_lock(&run_mu);
	if (car) {
		if (car->next) queue_push(&run_head, &run_tail, car->next);
		car->next = c;
		car->next_at = clj_profile_now();
	} else {
		queue_push(&run_head, &run_tail, c);
	}
	if (idle_carriers) pthread_cond_signal(&run_cv);
	pthread_mutex_unlock(&run_mu);
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
		clj_throw_msg(CLJ_CANCELLED_MESSAGE);
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

// A pool coroutine switches out holding its lock, so a resumer that wants the lock sees it parked or not at all.
static void park(clj_waiter *w, bool cancellable) {
	clj_coro *c = clj_coro_current();
	pthread_mutex_lock(&c->lock);
	bool block = c->implicit || w->blocking;
	if (!cancellable) w = NULL;
	if (block) {
		c->waiter = w;
		while (!c->signaled) pthread_cond_wait(&c->cond, &c->lock);
		c->signaled = false;
		c->waiter = NULL;
		pthread_mutex_unlock(&c->lock);
		return;
	}
	if (c->resume_pending) {
		c->resume_pending = false;
		pthread_mutex_unlock(&c->lock);
		return;
	}
	c->waiter = w;
	atomic_store_explicit(&c->state, CLJ_CORO_PARKED, memory_order_release);
	clj_coro_switch_out(c);
	pthread_mutex_lock(&c->lock);
	c->waiter = NULL;
	c->resume_pending = false;
	pthread_mutex_unlock(&c->lock);
}

void clj_park(clj_waiter *w) { park(w, true); }

void clj_park_uncancellable(clj_waiter *w) { park(w, false); }

static void run_callback(clj_waiter *w) {
	clj_value r = clj_invoke(w->callback, &w->value, 1);
	if (r == CLJ_THROWN) {
		clj_coro *c = clj_coro_current();
		clj_coro_report_uncaught(c);
		clj_coro_drop_pending(c);
	} else {
		clj_release(r);
	}
}

void clj_resume(clj_waiter *w) {
	clj_coro *c = w->coro;
	if (!c) {
		run_callback(w);
		return;
	}
	pthread_mutex_lock(&c->lock);
	if (c->implicit || w->blocking) {
		c->signaled = true;
		pthread_cond_signal(&c->cond);
		pthread_mutex_unlock(&c->lock);
		return;
	}
	if (atomic_load_explicit(&c->state, memory_order_acquire) == CLJ_CORO_PARKED) {
		atomic_store_explicit(&c->state, CLJ_CORO_RUNNABLE, memory_order_relaxed);
		pthread_mutex_unlock(&c->lock);
		clj_sched_enqueue(c);
		return;
	}
	c->resume_pending = true;
	pthread_mutex_unlock(&c->lock);
}

// ---- spawn and finish

static void (*uncaught_handler)(clj_value ex, clj_value trace);

static void put(const char *s) { (void)!write(2, s, strlen(s)); }

// Diagnostics, not fatal: the default handler writes the message and the trace to stderr, as the JVM's does.
void clj_coro_report_uncaught(clj_coro *c) {
	clj_value ex = c->threw ? c->result : c->pending, trace = c->threw ? clj_ex_trace(ex) : clj_retain(c->pending_trace);
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

static void finish(clj_coro *c) {
	clj_eval_drain_retired(c);
	clj_var_bindings_release(c->bindings);
	c->bindings = NULL;
	clj_coro_drop_pending(c);
	if (c->on_done) c->on_done(c, c->done_ctx);
	else if (c->threw) clj_coro_report_uncaught(c);
	c->on_done = NULL;
	clj_value fn = c->fn;
	c->fn = CLJ_NIL;
	clj_release(fn);
	for (size_t i = 0; i < c->nargs; i++) clj_release(c->args[i]);
	c->nargs = 0;
	clj_coro_free_stack(c);
	pthread_mutex_lock(&c->lock);
	c->signaled = true;
	pthread_cond_broadcast(&c->cond);
	pthread_mutex_unlock(&c->lock);
	clj_release(clj_from_ptr(c));
}

clj_value clj_coro_spawn(clj_value f, const clj_value *args, size_t n, int affinity, void (*on_done)(clj_coro *c, void *ctx), void *ctx) {
	if (affinity == CLJ_AFFINITY_MAIN && !main_carrier) return clj_throw_msg("No main carrier: the host has not installed one (clj_sched_main_install)");
	clj_sched_init();
	clj_coro *parent = clj_coro_current();
	clj_coro *c = clj_coro_alloc();
	clj_share(f);
	c->fn = clj_retain(f);
	if (n) {
		c->args = malloc(n * sizeof *c->args);
		if (!c->args) clj_fatal("out of memory");
		for (size_t i = 0; i < n; i++) {
			clj_share(args[i]);
			c->args[i] = clj_retain(args[i]);
		}
	}
	c->nargs = n;
	c->bindings = clj_var_bindings_share();
	c->pending = c->pending_trace = c->result = CLJ_NIL;
	atomic_store_explicit(&c->shadow->deadline, clj_shadow_deadline(parent->shadow), memory_order_relaxed);
	c->shadow->countdown = 1024;
	c->shadow->unwinds = 64;
	clj_coro_capture_spawn_trace(c);
	c->affinity = (uint8_t)affinity;
	c->on_done = on_done;
	c->done_ctx = ctx;
	atomic_fetch_add_explicit(&spawned, 1, memory_order_relaxed);
	clj_retain(clj_from_ptr(c));
	atomic_store_explicit(&c->state, CLJ_CORO_RUNNABLE, memory_order_release);
	clj_sched_enqueue(c);
	return clj_from_ptr(c);
}

clj_value clj_coro_result(clj_value coro, bool *threw) {
	clj_coro *c = clj_coro_of(coro);
	if (threw) *threw = c->threw;
	return c->result;
}

bool clj_coro_done(clj_value coro) { return atomic_load_explicit(&clj_coro_of(coro)->state, memory_order_acquire) == CLJ_CORO_DONE; }

void clj_coro_cancel(clj_value coro) {
	clj_coro *c = clj_coro_of(coro);
	pthread_mutex_lock(&c->lock);
	if (atomic_load_explicit(&c->state, memory_order_acquire) == CLJ_CORO_DONE || c->implicit) {
		pthread_mutex_unlock(&c->lock);
		return;
	}
	atomic_store_explicit(&c->shadow->cancelled, true, memory_order_relaxed);
	atomic_store_explicit(&c->shadow->deadline, 1, memory_order_relaxed);
	c->shadow->unwinds = 64;
	clj_waiter *w = c->waiter;
	if (w) clj_waiter_retain(w);
	pthread_mutex_unlock(&c->lock);
	if (!w) return;
	if (clj_waiter_claim(w)) clj_resume(w);
	clj_waiter_release(w);
}

bool clj_coro_cancelled(clj_value coro) {
	clj_coro *c = clj_coro_of(coro);
	return c->shadow && atomic_load_explicit(&c->shadow->cancelled, memory_order_relaxed);
}

void clj_coro_join_blocking(clj_value coro) {
	clj_coro *c = clj_coro_of(coro);
	if (clj_coro_current() == c) clj_fatal("a coroutine cannot join itself");
	pthread_mutex_lock(&c->lock);
	while (!c->signaled) pthread_cond_wait(&c->cond, &c->lock);
	pthread_mutex_unlock(&c->lock);
}

// ---- timers: one thread, a list sorted by deadline

typedef struct timer {
	uint64_t when;
	void (*fn)(void *ctx);
	void         *ctx;
	struct timer *next;
} timer;

static pthread_mutex_t timer_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  timer_cv = PTHREAD_COND_INITIALIZER;
static timer          *timers;
static pthread_once_t  timer_once = PTHREAD_ONCE_INIT;

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
			uint64_t        wait = timers->when - now;
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += (time_t)(wait / 1000000000u);
			ts.tv_nsec += (long)(wait % 1000000000u);
			if (ts.tv_nsec >= 1000000000L) {
				ts.tv_sec++;
				ts.tv_nsec -= 1000000000L;
			}
			pthread_cond_timedwait(&timer_cv, &timer_mu, &ts);
			continue;
		}
		timer *t = timers;
		timers = t->next;
		pthread_mutex_unlock(&timer_mu);
		t->fn(t->ctx);
		free(t);
		pthread_mutex_lock(&timer_mu);
	}
	return NULL;
}

static void start_timer(void) {
	pthread_t      t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&t, &attr, timer_main, NULL) != 0) clj_fatal("pthread_create of the timer thread failed");
	pthread_attr_destroy(&attr);
}

void clj_sched_timer(uint64_t ns, void (*fn)(void *ctx), void *ctx) {
	pthread_once(&timer_once, start_timer);
	timer *t = malloc(sizeof *t);
	if (!t) clj_fatal("out of memory");
	t->when = clj_profile_now() + ns;
	t->fn = fn;
	t->ctx = ctx;
	pthread_mutex_lock(&timer_mu);
	timer **at = &timers;
	while (*at && (*at)->when <= t->when) at = &(*at)->next;
	t->next = *at;
	*at = t;
	pthread_mutex_unlock(&timer_mu);
	pthread_cond_signal(&timer_cv);
}

// ---- the blocking pool: threads made on demand, kept for ever, capped

typedef struct job {
	void (*fn)(void *ctx);
	void       *ctx;
	clj_waiter *w;
	struct job *next;
} job;

enum { BLOCKING_MAX_THREADS = 64 };

static pthread_mutex_t job_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  job_cv = PTHREAD_COND_INITIALIZER;
static job            *job_head, *job_tail;
static size_t          blocking_threads, blocking_idle;

static void *blocking_main(void *arg) {
	(void)arg;
	for (;;) {
		pthread_mutex_lock(&job_mu);
		blocking_idle++;
		while (!job_head) pthread_cond_wait(&job_cv, &job_mu);
		blocking_idle--;
		job *j = job_head;
		job_head = j->next;
		if (!job_head) job_tail = NULL;
		pthread_mutex_unlock(&job_mu);
		j->fn(j->ctx);
		if (j->w) {
			if (clj_waiter_claim(j->w)) clj_resume(j->w);
			clj_waiter_release(j->w);
		}
		free(j);
	}
	return NULL;
}

static void submit(void (*fn)(void *ctx), void *ctx, clj_waiter *w) {
	job *j = malloc(sizeof *j);
	if (!j) clj_fatal("out of memory");
	j->fn = fn;
	j->ctx = ctx;
	j->w = w;
	j->next = NULL;
	pthread_mutex_lock(&job_mu);
	if (job_tail) job_tail->next = j;
	else job_head = j;
	job_tail = j;
	bool spawn_thread = blocking_idle == 0 && blocking_threads < BLOCKING_MAX_THREADS;
	if (spawn_thread) blocking_threads++;
	pthread_mutex_unlock(&job_mu);
	if (spawn_thread) {
		pthread_t      t;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&t, &attr, blocking_main, NULL) != 0) clj_fatal("pthread_create of a blocking thread failed");
		pthread_attr_destroy(&attr);
	}
	pthread_cond_signal(&job_cv);
}

// The job's context lives on the caller's stack, so a cancellation may not wake the caller before the job ends.
void clj_blocking(void (*fn)(void *ctx), void *ctx) {
	clj_coro *c = clj_coro_current();
	if (c->implicit || c->host_depth || c->locks_held) {
		fn(ctx);
		return;
	}
	clj_waiter *w = clj_waiter_new(c, CLJ_NIL);
	clj_waiter_retain(w);
	submit(fn, ctx, w);
	clj_park_uncancellable(w);
	clj_waiter_release(w);
}

void clj_blocking_detach(void (*fn)(void *ctx), void *ctx) { submit(fn, ctx, NULL); }

uint64_t clj_debug_coro_spawned(void) { return atomic_load_explicit(&spawned, memory_order_relaxed); }

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
