// @ai-generated(solo)
#ifndef CLJ_CORO_INTERNAL_H
#define CLJ_CORO_INTERNAL_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "clj/coro.h"
#include "clj/lock.h"
#include "load_internal.h"
#include "shadow_internal.h"

typedef struct clj_carrier clj_carrier;
typedef struct clj_waiter  clj_waiter;

enum {
	CLJ_CORO_NEW,
	CLJ_CORO_RUNNABLE,
	CLJ_CORO_RUNNING,
	CLJ_CORO_PARKED,
	CLJ_CORO_DONE,
};

enum { CLJ_CORO_SPAWN_TRACE_MAX = 32 };

// One frame of the spawner's trace, kept as names and numbers: the nodes may die before the child throws.
typedef struct {
	clj_value name; // symbol or nil, retained
	uint32_t  line, col;
} clj_spawn_frame;

// The execution: what was per thread and follows the code across a park; a bare thread runs on an implicit one.
struct clj_coro {
	clj_header h;
	// ---- the switch
	void    *sp;         // saved stack pointer while not running
	void    *map;        // the mmap'd region: guard page, stack, shadow stack; NULL for an implicit coroutine
	size_t   map_size;
	void    *asan_fake;  // the sanitizer's fake stack handle across a switch
	// ---- state that was _Thread_local
	clj_shadow_stack *shadow;
	void             *bindings;      // var.c frames, refcounted and shared with spawned children
	clj_value         pending, pending_trace;
	void             *forcing_top;   // seq.c
	uint32_t          exec_depth;    // clj_exec_run nesting (eval.c)
	uint32_t          host_depth;    // synchronous host calls on this execution: a park under one is an error
	uint32_t          locks_held;    // clj_locks held (lock.h): 0 at every park
	clj_value        *retired;       // fn roots a def replaced while this execution was in flight (eval.c)
	size_t            nretired, cretired;
	void             *captures;      // with-out-str buffers (runtime.c)
	clj_load_arm      load_arm;
	bool              load_analysis_failed;
	// ---- scheduling
	_Atomic int      state;
	uint8_t          affinity;
	bool             implicit;      // a bare thread's own execution
	bool             resume_pending; // resumed before it parked: the park returns at once
	bool             signaled;       // implicit: the block was released
	pthread_mutex_t  lock;           // guards state, waiter and the park/resume handshake
	pthread_cond_t   cond;           // implicit: what the thread blocks on
	clj_waiter      *waiter;         // the park it is in, NULL while running
	struct clj_coro *next;           // run-queue link
	clj_carrier     *carrier;        // the carrier running it now
	clj_value        fn;             // the body, shared at spawn
	clj_value       *args;
	size_t           nargs;
	clj_value        result;         // the body's value, or the thrown value with `threw`
	bool             threw;
	void (*on_done)(struct clj_coro *c, void *ctx); // runs on the carrier after the body returned
	void            *done_ctx;
	clj_spawn_frame *spawn_trace;
	uint32_t         nspawn;
	uintptr_t        advised_lo, advised_hi; // the stack tail already handed back with madvise
};

// A thread that runs coroutines: a pool thread, the main thread, or any bare thread with its implicit one.
struct clj_carrier {
	clj_coro *current;   // the running execution
	clj_coro *implicit;  // the thread's own
	void     *altstack;  // the alternate signal stack (guard.c)
	bool      is_main;
	bool      pooled;
	void     *asan_fake; // the sanitizer's handle for the carrier's own stack while a coroutine runs
	void     *return_sp; // where the carrier's stack continues when a coroutine switches out
	clj_coro *next;      // the coroutine this carrier runs next, ahead of the run queue (Go's runnext); under run_mu
	uint64_t  next_at;   // when it was placed: an idle carrier steals it only once it has waited a while
	struct clj_carrier *pool_next; // the pool's list of carriers, for stealing
};

// The running execution; NULL until the thread's first use. Not for Swift: a _Thread_local does not import.
extern _Thread_local clj_coro *clj_coro_tls;

// The running execution, making the thread's implicit one on first use.
clj_coro *clj_coro_current(void);
// The carrier of the calling thread through the pthread key: async-signal-safe, NULL where none was made.
const clj_carrier *clj_carrier_current(void);
clj_carrier       *clj_carrier_here(void);

// The unit of parking, shared by every queue it sits in (alts!): the claim winner resumes, stale nodes are dropped.
struct clj_waiter {
	_Atomic uint32_t rc;
	clj_lock         lock;    // the claim: alone, or paired with the counterparty's under both locks (chan.c)
	_Atomic uint32_t claimed;
	clj_coro        *coro;     // NULL for a callback waiter (put!/take! with a fn)
	clj_value        callback; // fn or nil, retained
	clj_value        value;    // what the resumer hands over, owned by the waiter
	clj_value        port;     // the channel that completed it (alts!), retained
	uint32_t         index;
	bool             ok;
	bool             blocking; // set before enqueueing: the park blocks the thread instead of switching (host_depth > 0)
};

clj_waiter *clj_waiter_new(clj_coro *c, clj_value callback);
void        clj_waiter_retain(clj_waiter *w);
void        clj_waiter_release(clj_waiter *w);
// true once, for the caller that wins.
bool clj_waiter_claim(clj_waiter *w);
// Claims both or neither: 0 both claimed, 1 actor already claimed, 2 other already claimed. NULL is "no claim needed".
int clj_waiter_claim_pair(clj_waiter *actor, clj_waiter *other);
// Ask before enqueueing w: a park is illegal under a host call, a clj_lock or a cancellation (exception pending).
bool clj_park_allowed(void);
void clj_park(clj_waiter *w);
// The same, invisible to cancel!: for a wait whose other side still uses the parker's stack (a blocking job).
void clj_park_uncancellable(clj_waiter *w);
// Makes the coroutine of a claimed waiter runnable; a callback waiter runs its fn with w->value on the caller.
void clj_resume(clj_waiter *w);

// Switches the running coroutine out to its carrier (sched.c internals).
void clj_ctx_switch(void **save_sp, void *load_sp);

// The stack region of a pool coroutine (coro.c).
clj_coro *clj_coro_alloc(void);
void      clj_coro_free_stack(clj_coro *c);
void      clj_coro_advise_stack(clj_coro *c);
void      clj_coro_entry(void);
// Runs a coroutine's body on its own stack: called by the carrier loop, returns when it parks or finishes.
void clj_coro_switch_in(clj_carrier *car, clj_coro *c);
void clj_coro_switch_out(clj_coro *c);

// var.c: the running execution's frame chain shared into a child, and its release when the child ends.
void *clj_var_bindings_share(void);
void  clj_var_bindings_release(void *chain);
// eval.c: a finished coroutine releases everything it retired.
void clj_eval_drain_retired(clj_coro *c);

// The pending exception slot of an execution (error.c).
void clj_coro_drop_pending(clj_coro *c);
// The spawner's frames, retained into c (shadow.c).
void clj_coro_capture_spawn_trace(clj_coro *c);
void clj_coro_free_spawn_trace(clj_coro *c);
// Appends c's spawn trace to a trace vector (shadow.c).
clj_value clj_coro_append_spawn_trace(clj_value trace, const clj_coro *c);

// The uncaught-error report of a coroutine: message and trace to stderr (sched.c).
void clj_coro_report_uncaught(clj_coro *c);

void clj_sched_init(void);
// Makes the coroutine runnable (its affinity picks the queue).
void clj_sched_enqueue(clj_coro *c);
// Timers (sched.c): fn(ctx) runs on the timer thread after ns.
void clj_sched_timer(uint64_t ns, void (*fn)(void *ctx), void *ctx);
// The blocking pool (sched.c): fn(ctx) runs on a dedicated thread while the caller parks; inline on a bare thread.
void clj_blocking(void (*fn)(void *ctx), void *ctx);
// fn(ctx) on a blocking thread, the caller continues (thread).
void clj_blocking_detach(void (*fn)(void *ctx), void *ctx);

// Counters for tests and the bench.
uint64_t clj_debug_coro_switches(void);

#endif
