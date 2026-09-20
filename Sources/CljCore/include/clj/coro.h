// @ai-generated(solo)
#ifndef CLJ_CORO_H
#define CLJ_CORO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "object.h"

// A stackful execution the scheduler moves between carrier threads (design §4); any function may park.
typedef struct clj_coro clj_coro;

extern const clj_type clj_coro_type;

enum { CLJ_AFFINITY_POOL = 0, CLJ_AFFINITY_MAIN = 1 };

// Owned handle; fn and args are shared once, bindings and deadline conveyed; on_done runs on the carrier after the body.
clj_value clj_coro_spawn(clj_value f, const clj_value *args, size_t n, int affinity, void (*on_done)(clj_coro *c, void *ctx), void *ctx);
// The body's result of a finished coroutine, borrowed; threw says whether it is the thrown value.
clj_value clj_coro_result(clj_value coro, bool *threw);
bool      clj_coro_done(clj_value coro);
// Sets the cancel flag: every park point and deadline check of the coroutine throws from now on.
void clj_coro_cancel(clj_value coro);
bool clj_coro_cancelled(clj_value coro);
// The running execution is a pool or main coroutine, not a bare thread's implicit one.
bool clj_coro_in_coroutine(void);
bool clj_coro_on_main_carrier(void);

static inline bool      clj_is_coro(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_coro_type; }
static inline clj_coro *clj_coro_of(clj_value v) { return (clj_coro *)clj_to_ptr(v); }

// Blocks the calling bare thread until the coroutine finished (tests, the host's opt-in); an error from a coroutine.
void clj_coro_join_blocking(clj_value coro);

// A CFRunLoop source on the calling (main) thread; main-affinity coroutines run when its run loop turns.
void clj_sched_main_install(void);
void clj_sched_main_pump(void);
// Test hook: the calling thread becomes the main carrier without a run loop; pump by hand.
void clj_debug_sched_main_adopt(void);
void clj_debug_sched_main_abandon(void);

// Stack reserve per coroutine (virtual; committed lazily), before the first spawn.
void   clj_coro_set_stack_size(size_t bytes);
size_t clj_coro_stack_size(void);
// Physical footprint of the process in bytes (Apple: phys_footprint), for the parked-coroutine measurement.
size_t clj_debug_phys_footprint(void);

// Evacuation of cold parked coroutines (design §4 "Память припаркованной корутины — страница, не модель"): the
// sweep runs every `ms` on the timer thread while coroutines live and evacuates a coroutine found parked in the
// same park twice (parked between ms and 2·ms); 0 disables it. CLJ_EVAC_SWEEP_MS sets the default (250).
void     clj_coro_set_evac_sweep_ms(uint64_t ms);
uint64_t clj_coro_evac_sweep_ms(void);
// Evacuates every parked coroutine now (the memory-pressure handler's action); returns how many.
size_t clj_coro_evacuate_all(void);
// Evacuates one parked coroutine (nothing when running, finished or already evacuated); tests and the bench.
bool clj_debug_coro_evacuate(clj_value coro);
bool clj_debug_coro_evacuated(clj_value coro);
// Restores one evacuated parked coroutine in place without resuming it (the cycle's cost, bench).
bool clj_debug_coro_restore(clj_value coro);
// Evacuated coroutines now, the bytes their blobs hold, and the evacuations and restores so far.
size_t   clj_debug_coro_evacuated_count(void);
size_t   clj_debug_coro_evacuated_bytes(void);
uint64_t clj_debug_coro_evacuations(void);
uint64_t clj_debug_coro_restores(void);
// The trace of a parked coroutine, from its ring and stack (or its blob): nil when it is not parked.
clj_value clj_coro_parked_trace(clj_value coro);

// Live spawned coroutines (the implicit ones of threads are not counted).
size_t   clj_debug_live_coros(void);
// Waits up to ms for the live count to fall to target: a test's quiesce before its live-object check.
bool clj_debug_coro_settle(size_t target, uint64_t ms);
bool clj_debug_park_under_lock_is_error(void);
void clj_debug_sched_dump(void);
// Carriers in the untimed wait (not spinning, not polling) and the pool's size: a test's "the pool is cold".
size_t clj_debug_sched_sleeping(void);
size_t clj_debug_sched_carriers(void);
uint64_t clj_debug_coro_switches(void);
uint64_t clj_debug_coro_spawned(void);
// Replaces the stderr report of an error a coroutine did not catch (a go block without a handler).
void clj_coro_set_uncaught_handler(void (*fn)(clj_value ex, clj_value trace));
// Bench: n round trips carrier → coroutine → carrier on the calling thread; ns per switch.
double clj_bench_switch_ns(size_t n);

// Waits until the output writer has written everything queued so far (runtime.c).
void clj_output_flush(void);
// Test hooks for the writer's backpressure: the queue limit (0 restores the default) and how often a printer parked.
void     clj_debug_output_set_limit(size_t bytes);
uint64_t clj_debug_output_waits(void);

#endif
