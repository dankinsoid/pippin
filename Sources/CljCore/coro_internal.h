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

// What can wake a park (design §7, «Фаза 3»): the cycle collector judges a parked coroutine by it. A value is made
// only by the clj_wake_* constructors below, never as a literal (make park-audit).
enum {
	CLJ_WAKE_CHANNEL = 1, // an operation on a channel whose queue holds the waiter, or a cancel
	CLJ_WAKE_HANDLE,      // resume! or cancel! through a handle on the coroutine: the suspend gate
	CLJ_WAKE_TIMER,       // the timer thread, after which the body goes on: Thread/sleep
	CLJ_WAKE_THREAD,      // a runtime thread working for the parker: a blocking job, the output writer
	CLJ_WAKE_HOLDER,      // the execution holding what it waits for: a cmutex, a lazy seq's forcing claim
	CLJ_WAKE_YIELD,       // the seeded scheduler itself: a preemption, runnable again at once
};

typedef struct {
	uint8_t kind;
	bool    cancellable; // a cancel claims the waiter and wakes the park
} clj_wake;

static inline clj_wake clj_wake_channels(void) { return (clj_wake){CLJ_WAKE_CHANNEL, true}; }
// A scope's join: the channel alone ends it, a cancel does not.
static inline clj_wake clj_wake_channels_uncancellable(void) { return (clj_wake){CLJ_WAKE_CHANNEL, false}; }
static inline clj_wake clj_wake_handle(void) { return (clj_wake){CLJ_WAKE_HANDLE, false}; }
static inline clj_wake clj_wake_timer(void) { return (clj_wake){CLJ_WAKE_TIMER, true}; }
static inline clj_wake clj_wake_thread(void) { return (clj_wake){CLJ_WAKE_THREAD, false}; }
static inline clj_wake clj_wake_holder(void) { return (clj_wake){CLJ_WAKE_HOLDER, false}; }
static inline clj_wake clj_wake_yield(void) { return (clj_wake){CLJ_WAKE_YIELD, false}; }

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
	void    *tsan_fiber; // TSan's fiber for this stack: its vector clock and its shadow stack of entry/exit pairs
	// ---- state that was _Thread_local
	clj_shadow_stack *shadow;        // &shadow_hdr; its arrays are in the mapping (or calloc'd for an implicit one)
	clj_shadow_stack  shadow_hdr;
	void             *bindings;      // var.c frames, refcounted and shared with spawned children
	clj_private_value pending, pending_trace;
	uint8_t           equals_dropped; // CLJ_CANCEL_* of a cancellation equals/hash dropped (error.h), NONE otherwise
	const char       *refused;        // a hash/equals refusal nobody threw yet (error.h, clj_refuse), immortal
	void             *forcing_top;   // seq.c
	uint32_t          exec_depth;    // clj_exec_run nesting (eval.c)
	uint32_t          host_depth;    // synchronous host calls on this execution: a park under one is an error
	uint32_t          locks_held;    // clj_locks held (lock.h): 0 at every park
	uint32_t          cmutex_held;   // clj_cmutexes held around user code: no suspend parks under one (cmutex.h)
	uint32_t          forcing_held;  // lazy seqs claimed FORCING by this one: readers park on them, so no suspend either (seq.c)
	clj_private_value *retired;   // fn roots a def replaced while this execution was in flight (eval.c)
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
	// Implicit, seeded mode, under the scheduler's seed_mu (sched.c): inside an evaluation and holding the turn;
	// chosen by the seeded pick to run again; parked (SEED_PARKED_*), which lets the virtual clock move.
	bool             seed_in, seed_picked;
	uint8_t          seed_parked;
	pthread_mutex_t  lock;           // guards state, waiter and the park/resume handshake
	pthread_cond_t   cond;           // implicit: what the thread blocks on
	clj_waiter      *waiter;         // the park it is in, NULL while running and in an uncancellable park
	uint8_t          park_kind;      // CLJ_WAKE_* of the park it is in, 0 while running
	struct clj_coro *next;           // run-queue link
	clj_carrier     *carrier;        // the carrier running it now
	clj_slot         fn;             // the body, shared at spawn
	clj_slot        *args;
	size_t           nargs;
	clj_slot         result;         // the body's value, or the thrown value with `threw`
	bool             threw;
	bool             finished;       // finish ran, under lock: the slots hold still for the collector
	void (*on_done)(struct clj_coro *c, void *ctx); // runs on the carrier after the body returned
	void            *done_ctx;
	// on_done's own value (a go or future channel), released after it: an edge, so the collector sees it. A
	// coroutine with an on_done and no value has a host waiting for it, which no walk sees.
	clj_slot         done_value;
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
	clj_atomic_slot  cancel_cause;
	uint64_t         deadline_before; // the deadline a scope cancel replaced with 1, restored by the uncancel
	uint32_t         shield;          // shielded regions the owner is inside: the ring shows no deadline in one
	clj_timer       *deadline_timer;  // the timer that cancels this coroutine at its deadline, NULL when none
	uint64_t         deadline_serial; // bumped by every arm and disarm; a firing timer with a stale serial is a no-op
	// Never reused, unlike the address: a freed execution's successor at its address is not its owner.
	uint64_t         id;
	// ---- the cycle collector (cc.c): candidates among the unshared objects this execution owns
	void            *cc_local;
	bool             cc_collecting;
	bool             cc_collected;   // cancelled as garbage (design §7, «Фаза 3»): never judged again
	uint32_t         cc_park;        // the park the sweep last saw, and the passes to its next filing
	uint16_t         cc_wait, cc_left;
#if CLJ_DEBUG
	uint32_t         debug_owner;     // the tag its unshared objects carry (object.h, CLJ_OWNER_SHIFT)
#endif
};

// A thread that runs coroutines: a pool thread, the main thread, or any bare thread with its implicit one.
struct clj_carrier {
	clj_coro *current;   // the running execution
	clj_coro *implicit;  // the thread's own
	void     *altstack;  // the alternate signal stack (guard.c)
	bool      is_main;
	bool      pooled;
	void     *asan_fake; // the sanitizer's handle for the carrier's own stack while a coroutine runs
	void     *tsan_fiber; // the thread's own TSan fiber, read back at each switch in
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

#if CLJ_DEBUG
// The running execution acts as the owner of `tag` until a second call restores the returned tag: a finished
// coroutine's epilogue on its carrier, a blocking job for its parked caller.
uint32_t clj_debug_owner_assume(uint32_t tag);
#endif

// The carrier of the calling thread through the pthread key: async-signal-safe, NULL where none was made.
const clj_carrier *clj_carrier_current(void);
clj_carrier       *clj_carrier_here(void);

// ---- the seeded scheduler (design §3 «Корректность реализации», item 4; NOTES "Scheduler")

// Set once by clj_init from CLJ_SCHED_SEED, before any runtime thread exists.
extern bool clj_sched_seed_on;
void        clj_sched_seed_configure(void);
// The clock of timers and deadlines: virtual in seeded mode.
uint64_t clj_sched_now(void);
// The deadline tick's read: in seeded mode it moves the virtual clock by the work the tick stands for.
uint64_t clj_sched_tick_now(void);
// A bare thread's evaluation begins and ends (exec_depth 0 -> 1 and back): the turn is taken and given back.
void clj_sched_seed_enter(clj_coro *c);
void clj_sched_seed_leave(clj_coro *c);
// The program's randomness in seeded mode (alts! order, rand), a stream apart from the schedule's.
uint64_t clj_sched_seed_random(void);
// System/currentTimeMillis: the real start plus the virtual time since, in seeded mode.
uint64_t clj_sched_wall_ms(void);
// A test's settle in progress: seeded, the clock and the collector move the model without waiting for quiet.
void clj_sched_seed_settling(int delta);
void clj_sched_point_slow(void);
// Channel identity hashes count from here again (chan.c): a reseeded run hashes as a fresh process does.
void clj_chan_serial_reset(void);

// A preemption point after a spawn, a channel operation or an atom write: in seeded mode the execution may yield.
static inline void clj_sched_point(void) {
	if (__builtin_expect(clj_sched_seed_on, 0)) clj_sched_point_slow();
}

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
	const void      *wait_chan; // the channel it was last queued on, for clj_debug_coro_dump; never dereferenced else
	// Channel queues holding a node of it, for a coroutine's: the collector finds that many or judges it live.
	// Exact while unclaimed, since only a claim or the channel's death unlinks such a node.
	_Atomic uint32_t queued;
};

clj_waiter *clj_waiter_new(clj_coro *c, clj_value callback);
void        clj_debug_chan_describe(const void *chan, char *buf, size_t n);
void        clj_waiter_retain(clj_waiter *w);
void        clj_waiter_release(clj_waiter *w);
// true once, for the caller that wins.
bool clj_waiter_claim(clj_waiter *w);
// Claims both or neither: 0 both claimed, 1 actor already claimed, 2 other already claimed. NULL is "no claim needed".
int clj_waiter_claim_pair(clj_waiter *actor, clj_waiter *other);
// Ask before enqueueing w: a park is illegal under a host call, a clj_lock or a cancellation (exception pending).
bool clj_park_allowed(void);
// The one way to park: wake names what ends it. An uncancellable one is invisible to cancel!, for a wait whose
// other side still uses the parker's stack (a blocking job) or must outlast a cancellation (a scope's join).
void clj_park(clj_waiter *w, clj_wake wake);
// A one-shot wait on any address; wait_if runs under the bucket's lock and false returns at once (cmutex.c).
void clj_lot_park(const void *key, bool (*wait_if)(const void *key, void *ctx), void *ctx, clj_wake wake);
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
bool clj_coro_suspended(clj_coro *c);
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
// A shielded region meets no deadline check, so a cancellation over it waits for the region's end. Nests.
void clj_coro_shield_enter(clj_coro *c);
void clj_coro_shield_leave(clj_coro *c);
// c's deadline with the poison and the shield seen through: the ring reads 1 or 0, the real one deadline_before.
uint64_t clj_coro_deadline_own(clj_coro *c);
// The number of carriers the pool has or will have (available-processors*).
size_t clj_sched_carrier_count(void);
// A parking sleep on the timer thread: Thread/sleep for library code. CLJ_THROWN on a cancellation.
clj_value clj_sched_sleep_ms(int64_t ms);
// The blocking pool (sched.c): fn runs on a dedicated thread over a heap copy of ctx's `size` bytes, written back
// after the park; inline on a bare thread.
void clj_blocking(void (*fn)(void *ctx), void *ctx, size_t size);
// fn(ctx) on a blocking thread, the caller continues (thread).
void clj_blocking_detach(void (*fn)(void *ctx), void *ctx);

// The body f on a pool or main coroutine whose on_done gets done_value back in c->done_value (go, future): the
// collector sees that edge. Detached: neither bindings nor deadline conveyed.
clj_value clj_coro_spawn_into(clj_value f, int affinity, void (*on_done)(clj_coro *c, void *ctx), clj_value done_value, bool detached);

// The cycle collector's view of a coroutine (cc.c, design §7 «Фаза 3»). LIVE: running, queued, or parked where a
// collection cannot judge it — black. PARKED: in a collectable park, its self and deadline references internal and
// `queued` channel queues to find. DONE: finished, an ordinary node.
enum { CLJ_CORO_CC_LIVE, CLJ_CORO_CC_PARKED, CLJ_CORO_CC_DONE };
// inside runs under c->lock; false without running when the lock is busy.
bool clj_coro_cc_locked(clj_coro *c, void (*inside)(clj_coro *c, int verdict, uint32_t internal, uint32_t queued, void *ctx), void *ctx);
// A white parked coroutine is cancelled once; true when this call did it.
bool clj_coro_cc_cancel(clj_coro *c);
// Files every parked coroutine as a cycle candidate (clj_cc_collect).
void clj_coro_cc_file_parked(void);

// Counters for tests and the bench.
uint64_t clj_debug_coro_switches(void);

#endif
