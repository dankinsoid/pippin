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

// Live spawned coroutines (the implicit ones of threads are not counted).
size_t   clj_debug_live_coros(void);
// Waits up to ms for the live count to fall to target: a test's quiesce before its live-object check.
bool clj_debug_coro_settle(size_t target, uint64_t ms);
bool clj_debug_park_under_lock_is_error(void);
void clj_debug_sched_dump(void);
uint64_t clj_debug_coro_switches(void);
uint64_t clj_debug_coro_spawned(void);
// Replaces the stderr report of an error a coroutine did not catch (a go block without a handler).
void clj_coro_set_uncaught_handler(void (*fn)(clj_value ex, clj_value trace));
// Bench: n round trips carrier → coroutine → carrier on the calling thread; ns per switch.
double clj_bench_switch_ns(size_t n);

// Waits until the output writer has written everything queued so far (runtime.c).
void clj_output_flush(void);

#endif
