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

enum { CLJ_CORO_SPAWN_TRACE_MAX = 32, CLJ_CORO_SPAWN_TRACE_INLINE = 4 };

// Why a coroutine is cancelled: cancel!/future-cancel, its deadline's timer, or its go-scoped scope (cleared
// again by the scope's exit, the only kind that is).
enum { CLJ_CANCEL_NONE = 0, CLJ_CANCEL_REQUESTED = 1, CLJ_CANCEL_DEADLINE = 2, CLJ_CANCEL_SCOPE = 3 };

typedef struct clj_timer clj_timer;

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
	clj_shadow_stack *shadow;        // &shadow_hdr; its arrays are in the mapping (or calloc'd for an implicit one)
	clj_shadow_stack  shadow_hdr;
	void             *bindings;      // var.c frames, refcounted and shared with spawned children
	clj_value         pending, pending_trace;
	void             *forcing_top;   // seq.c
	uint32_t          exec_depth;    // clj_exec_run nesting (eval.c)
	uint32_t          host_depth;    // synchronous host calls on this execution: a park under one is an error
	uint32_t          locks_held;    // clj_locks held (lock.h): 0 at every park
	uint32_t          cmutex_held;   // clj_cmutexes held around user code: no suspend parks under one (cmutex.h)
	uint32_t          forcing_held;  // lazy seqs claimed FORCING by this one: readers park on them, so no suspend either (seq.c)
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
	clj_spawn_frame *spawn_trace;    // spawn_inline for a short trace: no malloc per spawn
	uint32_t         nspawn;
	clj_spawn_frame  spawn_inline[CLJ_CORO_SPAWN_TRACE_INLINE];
	uintptr_t        advised_hi;     // the stack tail [stack_lo, advised_hi) already handed back with madvise; 0 none
	// ---- evacuation (coro.c): the live bytes of a cold parked coroutine sit in a heap blob, the mapping is reusable
	void            *evac;           // the blob: [sp, stack_hi) then the ring's live frames; NULL while resident
	bool             evacuated;      // read by the carrier before the switch in (one flag test), written under lock
	bool             linked;         // on the live list (from its first park)
	uint32_t         parks;          // bumped per park: the sweep evacuates a coroutine seen parked twice in one park
	uint32_t         cold_at;        // the parks value the sweep last saw it parked at
	struct clj_coro *live_prev, *live_next; // every spawned coroutine, for the sweep
	// ---- cancellation (sched.c): the kind outlives the stack, so a finished future still answers future-cancelled?
	_Atomic uint8_t  cancel;          // CLJ_CANCEL_*; the shadow's cancelled flag mirrors it for the tick path
	// What made a scope cancel this one; published before `cancel`, read after it, cleared with it and at finish.
	_Atomic clj_value cancel_cause;
	uint64_t         deadline_before; // the deadline a scope cancel replaced with 1, restored by the uncancel
	clj_timer       *deadline_timer;  // the timer that cancels this coroutine at its deadline, NULL when none
	uint64_t         deadline_serial; // bumped by every arm and disarm; a firing timer with a stale serial is a no-op
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
	// An idle pool carrier waits on its own condition, listed under run_mu so a waker pops exactly one.
	struct clj_carrier *idle_next;
	bool                idle;      // on the idle list
	bool                polling;   // its wait is timed: it looks at the queue again by itself
	bool                signaled;  // under park_mu: a waker popped it
	pthread_mutex_t     park_mu;
	pthread_cond_t      park_cv;
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
// clj_resume hands the carrier over (the resumer is about to park: a channel); clj_resume_far queues it for
// any carrier (the resumer keeps running: a lock's unlock).
void clj_resume(clj_waiter *w);
void clj_resume_far(clj_waiter *w);

// Switches the running coroutine out to its carrier (sched.c internals).
void clj_ctx_switch(void **save_sp, void *load_sp);

// The stack region of a pool coroutine (coro.c).
clj_coro *clj_coro_alloc(void);
void      clj_coro_free_stack(clj_coro *c);
void      clj_coro_advise_stack(clj_coro *c);
// Under c->lock at a park: registers the coroutine for the sweep once (c->linked says whether it is done).
void clj_coro_live_link(clj_coro *c);
// Under c->lock with c parked: the live bytes to a blob, the mapping handed back; false when there is nothing to do.
bool clj_coro_evacuate_locked(clj_coro *c);
void      clj_coro_entry(void);
// Runs a coroutine's body on its own stack: called by the carrier loop, returns when it parks or finishes.
void clj_coro_switch_in(clj_carrier *car, clj_coro *c);
void clj_coro_switch_out(clj_coro *c);

// var.c: the running execution's frame chain shared into a child, and its release when the child ends.
void *clj_var_bindings_share(void);
void  clj_var_bindings_release(void *chain);
// runtime.c: the same for the with-out-str captures.
void *clj_output_captures_share(void);
void  clj_output_captures_release(void *chain);
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
// Makes the coroutine runnable (its affinity picks the queue); handoff puts it in the calling carrier's next slot.
void clj_sched_enqueue(clj_coro *c, bool handoff);
// Timers (sched.c): fn(ctx) runs on the timer thread after ns. Cancel returns true when the timer was unlinked
// before it fired (the caller then owns ctx); false once it fired or is firing.
clj_timer *clj_sched_timer(uint64_t ns, void (*fn)(void *ctx), void *ctx);
bool       clj_sched_timer_cancel(clj_timer *t);
// A cancellation of the coroutine with a kind (coro.h's clj_coro_cancel is CLJ_CANCEL_REQUESTED); a kind already
// set is not overwritten except by REQUESTED. Implicit coroutines are cancellable: a thread's job, a scope's body.
void clj_coro_cancel_kind(clj_coro *c, int kind);
// The same, recording why: only the cancel that takes the kind records a cause, and it is shared before it lands.
void clj_coro_cancel_kind_cause(clj_coro *c, int kind, clj_value cause);
// The cause recorded for c's current cancellation, owned; nil when the flag is clear or nothing was recorded.
clj_value clj_coro_cancel_cause(clj_coro *c);
// Clears a CLJ_CANCEL_SCOPE cancellation and restores the deadline; any other kind stays.
void clj_coro_uncancel_scope(clj_coro *c);
// Clears every cancellation and the deadline: a blocking thread's implicit coroutine between two jobs.
void clj_coro_cancel_reset(clj_coro *c);
// Suspension (design §4, "Стек как объект"): a sticky flag beside the cancellation, poisoning the same deadline,
// parking the tick on a gate instead of throwing. true when a live coroutine took the request / had one standing.
bool clj_coro_suspend(clj_coro *c);
bool clj_coro_resume(clj_coro *c);
bool clj_coro_suspended(const clj_coro *c);
// The tick with the poison and the flag set (eval.c): parks until resume! or a cancellation, and answers whether
// the tick must throw after all. A point that holds anything defers the request to the next tick.
bool clj_coro_suspend_point(void);
// True when c's cancellation is the deadline kind, false for requested/scope (clj_throw_cancelled's argument).
bool clj_coro_cancel_is_deadline(const clj_coro *c);
// Arms the deadline timer of c for its shadow's absolute deadline (disarming any earlier one); a cleared deadline
// disarms it and lifts a deadline cancellation.
void clj_coro_deadline_arm(clj_coro *c);
void clj_coro_deadline_cleared(clj_coro *c);
// The deadline the owner sets on itself: kept aside while a cancellation or a suspension holds the ring's own.
void clj_coro_deadline_replace(clj_coro *c, uint64_t deadline);
// The number of carriers the pool has or will have (available-processors*).
size_t clj_sched_carrier_count(void);
// A parking sleep on the timer thread: Thread/sleep for library code. CLJ_THROWN on a cancellation.
clj_value clj_sched_sleep_ms(int64_t ms);
// The blocking pool (sched.c): fn runs on a dedicated thread over a heap copy of ctx's `size` bytes, written back
// after the park; inline on a bare thread.
void clj_blocking(void (*fn)(void *ctx), void *ctx, size_t size);
// fn(ctx) on a blocking thread, the caller continues (thread).
void clj_blocking_detach(void (*fn)(void *ctx), void *ctx);

// Counters for tests and the bench.
uint64_t clj_debug_coro_switches(void);

#endif
