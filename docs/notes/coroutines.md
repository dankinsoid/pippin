## Coroutines (Sources/CljCore/coro.c, coro_internal.h, include/clj/coro.h; design §4 "core.async")

- **The primitive is a stackful coroutine with a hand-written context switch** (`clj_ctx_switch`, arm64 and
  x86_64 in `coro.c`): callee-saved registers, the frame pointer, the link register and `d8–d15` on a 160-byte
  frame, then `sp` — 26 instructions, no signal mask (`swapcontext` makes a sigprocmask syscall per switch and
  is deprecated on macOS). Measured 14–16 ns per switch (bench/RESULTS.md, "Coroutines and channels": a carrier →
  coroutine → carrier round trip is two). The first switch into a new coroutine "returns" into `clj_coro_entry`
  with an empty frame chain (`x29 = 0`), so a trace walk stops at the coroutine's base.
- **A stack is one `mmap` reserve with no commit**: a guard page (`PROT_NONE`), the stack (512 KB by default,
  `clj_coro_set_stack_size` before the first spawn; the interpreter's 64 KB `STACK_MARGIN` check applies to it
  like to a thread), and the arrays of the execution's shadow ring (`frames[8192]`, then the guard's `overflow[256]`:
  196 KB virtual) starting half a page past the stack's top so that a shallow parked coroutine touches one 16 KB
  page for both. The ring's *header* (`clj_shadow_stack`: depth, bounds, deadline, the cancelled flag, the
  recovery chain) is embedded in the `clj_coro` (`shadow_hdr`), not in the mapping: a canceller writes it while
  the owner is parked, and the mapping of a parked coroutine may be evacuated (below); `frames` and `overflow`
  are pointers in it (one extra load in `clj_shadow_push`, on the header's cache line). Measured: 10 000 parked
  `go` blocks cost 16 KB physical each (`clj_debug_phys_footprint`, `task_info`) against 528 KB virtual — the
  page size is the floor, and a Swift `Task` suspended on a continuation is a few hundred bytes. On a park the
  carrier hands the pages below the saved `sp` back with `MADV_FREE_REUSABLE` once per range
  (`clj_coro_advise_stack`; a run that went deeper and parked at the same depth stays resident until a shallower
  park: the high-water mark of the note in design §4, not the exact tail). Finished coroutines leave their
  mappings in a cache of 256 (`map_keep`/`map_take`): an `mmap`, `mprotect` and `munmap` per spawn cost more
  than the spawn itself. Under ASan a cached mapping is unpoisoned before reuse (the dead frames' red zones), and
  every switch goes through `__sanitizer_start_switch_fiber`/`finish_switch_fiber`, so ASan follows the
  coroutine stacks (`make test` runs the suites on them). The pairing is per thread and every entry owes a
  finish, including the bench's `bounce` body; ASan ignores the hooks on a thread it does not track, so an
  unbalanced one is invisible until something moves that code onto a tracked thread.
- **A parked coroutine's stack is its own** (design §4, "Стек припаркованной корутины — только её"): nothing a
  resumer, a canceller, a timer or a blocking job touches lives in the parker's frames or in its mapping, so the
  live bytes can be copied out and back *to the same addresses* (no pointer fixups: relocation stays refused).
  The audit, every object another thread reads or writes while its owner is parked:

  | touched while the owner is parked | by whom | where it lives | verdict |
  |---|---|---|---|
  | the waiter: claim word, its lock, `value`/`ok`/`port`/`index` | the completing side, a canceller, a timer | `clj_waiter`, malloc'd and refcounted | heap |
  | channel queue nodes (`qnode`) | the completing side | malloc'd, owned by the channel | heap |
  | the `alts!` handler | every port's completer | the one shared waiter; the `order`/`wakes` arrays of `clj_chan_alts` are used before the park only | heap |
  | a `timeout`'s close, `Thread/sleep`, a deadline firing | the timer thread | the channel, the waiter, `deadline_ctx` (`clj_sched_timer` contexts) | heap |
  | cancellation: kind, `deadline_before`, the waiter to claim | `cancel!`, the deadline timer, a scope | `clj_coro` (`cancel`, `waiter`) | heap |
  | the ring header: `cancelled`, `deadline`, `unwinds`, `countdown` | `cancel_locked`, `deadline_fire`, `uncancel_scope` | was the first bytes of the mapping's ring page, shared with the stack's top → **moved** into `clj_coro` (`shadow_hdr`) | fixed |
  | the blocking job's context (`read_job` of `load-file`) | the blocking-pool thread | was the caller's frame (`clj_blocking(fn, &job)`) → **fixed**: `clj_blocking(fn, ctx, size)` copies `size` bytes into the job, the thread works on the copy, the parker copies it back after the wake | fixed |
  | `thread`'s job | the blocking thread | `thread_job`, malloc'd (the spawner never waits) | heap |
  | lot nodes and monitors (`locking`, atoms, forcing) | the unlocker | malloc'd | heap |
  | the output writer's `out_waiter` | the writer thread | malloc'd | heap |
  | scope child tokens, the scope's done channel | children, the scope | Clojure values in the `*scope*` binding | heap |
  | binding frames, `with-out-str` captures | children sharing them | refcounted heap chains | heap |
  | the forcing records (`forcing_top`, seq.c) | nobody: a second forcer waits on the lot keyed by the seq, the chain is read by its owner only | the owner's frames | own |
  | recovery points (`clj_recovery`, sigjmp_buf) | the guard handler of the *running* coroutine only | the owner's frames | own |
  | the saved registers, `sp` | the carrier at the switch | `clj_coro.sp`, the switch frame on the owner's stack | own |

  Two violations, both fixed; the rest is heap by construction (waiters are the unit of parking). `EvacTests`
  is the proof: a coroutine parked in each kind of wait (take, put, `alts!`, `timeout`, the mutex lot, `promise`
  and `future` deref, `Thread/sleep`, a scope's join, a `load-file` blocked on a FIFO) is evacuated and woken with
  the right value; under ASan the evacuated mapping is poisoned, so a resumer that touched a parked frame would
  be a report, not a corruption.
- [~] **Evacuation of cold parked coroutines** (design §4, "Память припаркованной корутины — страница, не модель"; the
  sixteen kilobytes of a parked coroutine are one page, and a page is the floor for a mapping, so the only way
  below it is off the mapping). `clj_coro_evacuate_locked`, under the coroutine's lock and only in state
  `PARKED` (the context is fully saved — the parker switches out holding the lock): the live range `[sp,
  stack_hi)` and the ring's live frames (`min(depth, capacity)` entries; the overflow array's `noverflow`) are
  copied into one malloc'd blob of exact size, the mapping from the first page the park's advise did not cover up
  to its end goes to `MADV_FREE_REUSABLE`, `evacuated` is set. The carrier restores in `clj_coro_switch_in`
  before the switch: `MADV_FREE_REUSE` on the pages about to be written (a re-dirtied reusable page rejoins
  `phys_footprint` otherwise only when the pageout scanner meets it — measured: 0 KB counted after a re-dirty
  without it; the data is safe either way, the scanner treats a referenced or dirtied reusable page as reused),
  two `memcpy`s back, the blob freed. The hot path gains one flag test on the resume, and on the park a `parks++`
  and a `linked` test under the lock the park already holds: a coroutine joins the sweep's live list (16 stripes
  by address under `clj_lock`s) at its *first* park, not at its spawn — a link per spawn and an unlink per finish
  cost the spawn row ~60 ns against twelve finishing carriers, and a coroutine that never parks has nothing to
  sweep. Triggers: (1) the **sweep** — a timer on the timer thread every
  `CLJ_EVAC_SWEEP_MS` (default 250, `clj_coro_set_evac_sweep_ms`, 0 disables), armed while any coroutine lives and
  re-armed by itself; a pass records each parked coroutine's park generation (`cold_at = parks`) and evacuates
  those the previous pass saw in the *same* park, so a coroutine is taken after 250–500 ms parked and a pair
  hopping every microsecond is never touched (bench: 500 pairs under a 2 ms sweep, 0 evacuations); (2) **memory
  pressure** — `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE` (warn and critical) evacuates every parked coroutine at once
  (`clj_coro_evacuate_all`, also the test hook). The default is 250 ms because the cost of a wrong guess is
  bounded and small (a restore is ~1.4 µs plus the page faults; a coroutine parked ≥ 250 ms pays 0.001 % of its
  wait) while pressure handles the emergency, and a period under 100 ms would keep the timer thread busy on a
  phone for nothing. Measured (bench/RESULTS.md, "Evacuation of parked coroutines"): 1.9 KB live per parked
  `(go (<! g))` in release (3.7 KB in debug: two 832-byte `eval_invoke` slot arrays, the entry's `sigjmp_buf`, the
  160-byte switch frame), evacuate + restore 1.4 µs shallow and 6.2 µs for 63 KB, a wake through a restore +2.4
  µs, 10 000 parked 170 MB → 34 MB physical (18 MB of blobs, 16 MB of coroutine, channel, closure and waiter
  objects that were there before too). The brief's gate of 5 MB for 10 000 is not reachable with real C frames:
  the objects alone are 1.6 KB each; the trigger for the next 2× is the interpreter's frame (the two
  `SMALL_SLOTS` arrays) or compiled bodies (a compiled `go` body has no `eval_invoke` frames). Traces: a parked
  coroutine's trace is `clj_coro_parked_trace` (its ring frames and a frame-pointer walk from the saved `x29`,
  read out of the blob with a bias while evacuated; nil for a coroutine that is not parked — an honest refusal);
  the running coroutine's trace is unaffected, since the restore precedes the switch. Deferred, with their
  triggers: the lanes of design §4 (K fixed mappings, a resume pays two memcpy where now it pays none) — a profile
  with the page faults of restores or the VM entry count of 10 000 mappings on top; a smaller live range —
  the interpreter's frame; `MADV_FREE_REUSE` on the park path's own re-dirtying (accounting only).
- **The execution moved off `_Thread_local` into `clj_coro`** (`coro_internal.h`): the shadow ring with its
  stack bounds, deadline, countdown, unwinds and cancel flag; the binding frames (`var.c`); the pending
  exception and its trace (`error.c`); the forcing stack (`seq.c`); the exec nesting, the retired fn roots and
  the host depth (`eval.c`); the `with-out-str` captures (`runtime.c`); the loader's arm; the locks-held count
  (`lock.h`). What stays per thread: the allocator heap, the protocol reader window, the compiled sites' inline
  caches, the rand state, the signal stack. A bare thread (the host's sync entry, a test calling `clj_eval`, a
  blocking-pool thread, the timer thread) runs on an *implicit* coroutine made on first use (`implicit_init`:
  calloc'd, immortal, its shadow ring calloc'd, its stack the thread's) whose park is a `pthread_cond_wait` —
  nothing there ever switches, so every entry point works exactly as before. The switch stores two thread-locals
  (`clj_coro_tls`, and `clj_shadow_tls` as its mirror so `run_body` still pays one TLS load) and `car->current`
  (read by the signal handler through the pthread key). Retired roots are per coroutine, not per carrier as
  the brief said: a parked coroutine keeps its +0 reads across the carriers it migrates over, and a drain
  keyed to another execution's flight would free a root it still borrows.
- [~] **TLS across a park is the one rule every runtime file obeys.** Clang computes a `_Thread_local`'s address
  once per function and keeps it across calls (verified: `held++; park(); held--` reuses `x19`), so a coroutine
  that parks and resumes on another thread reads the *old* thread's slot through the cached address. Every
  `_Thread_local` that code reaching a park can touch is therefore read through a call the compiler cannot
  hoist or fold: `clj_coro_current()` (an external function), `clj_locks_held_slot()` (`noinline` plus an
  `asm volatile` memory clobber so LLVM cannot infer it pure and merge two calls), `clj_deadline_tick()` (a
  function), a compiled loop's tick ring captured once at the loop's entry (`clj_c_tick_ring`), and the
  compiled inline caches behind per-site getters (`CLJC_TLS_IC`, `compiled_internal.h`: `static _Thread_local`
  at file scope with a `noinline` getter; +1 ns on a protocol or keyword site — the trigger for the asm
  alternative that names the TLV symbol directly is a profile where that call shows). The mutex slow path
  runs one attempt per activation (`lock_attempt`) for the same reason. An inline read is fine when it happens
  before any park in the activation and only the *pointer* is used after (`run_body`'s ring, `eval_loop`'s
  ring): the pointer is the execution's own and stays valid; a *re-read* is what goes wrong.
- [~] **The guard page of a coroutine works like a thread's**: the fault lands in `clj_guard_signal` with the
  current execution's ring (the carrier's `current`), the trace is collected from the coroutine's stack and
  the landing is at the execution's innermost recovery point — `clj_coro_entry` pushes one, so an overflow
  inside a coroutine throws "Stack overflow" in that coroutine (its `go` channel closes after the uncaught
  report) and nothing else dies; `CompilerFixtureTests.overflowInsideACoroutineThrowsOnlyThere` runs compiled
  recursion in a `go` in dev and closed mode. A `try` inside the coroutine does not see it, as at the host
  boundary (same trigger as there).
- **A trace is captured flat and materialized late**: `clj_shadow_stack_trace` walks the frames at the throw
  but stores only `{retained shared name, line, col}` in one `clj_trace_type` object, as the spawn trace does;
  the vector of three-key maps is built by `clj_trace_realize`, which `ex-trace`, `clj_coro_report_uncaught`
  and the Swift boundary call (the JVM keeps an opaque backtrace and builds `StackTraceElement[]` in
  `getStackTrace`). It is not memoized into the exception's slot: a shared exception would need a lock for
  that, and building it twice costs less than taking one. `ex-trace` returns what it always did.
- **Traces read through a park**: `clj_trace_collect` walks the coroutine's own stack by frame pointer (its
  bounds are the mapping's), and `clj_shadow_stack_trace` appends the spawner's frames captured at the spawn
  (`clj_coro_capture_spawn_trace`: up to 32 `{name, line, col}` triples, the name symbol retained, no node
  pointers since the spawner's exec may die first; a spawn from a coroutine inherits its parent's triples
  within the same bound). The cheapest form measured: ~35 ns per spawn for the walk and the retains with the
  frames inline in the coroutine (four; malloc beyond), no maps until a throw needs them; why it is not lazy is
  under "Scheduler". The JVM's `go` shows nothing of the spawner. `ChanTests.traceThroughAPark` pins the
  order: the throwing fn, the go body, then `spawner`, then `outer-spawner`.
- **Bindings are conveyed by sharing the frame chain** (`clj_var_bindings_share`): a frame carries an atomic
  refcount, a child holds the spawner's top frame and each frame its `prev`, a var's `thread_bound` count drops
  when the frame dies rather than when it is popped, and the maps are shared once. `set!` on a conveyed binding
  from the child is refused with the JVM's message ("Can't set!: … from non-binding thread"): a frame records
  the execution that pushed it (`owner`), `clj_var_set` finds the frame whose own push holds the var, and only
  its owner writes the box; the child's own `binding` over the same var is its to set (`FutureTests`). The
  `with-out-str` capture is conveyed the same way (`clj_output_captures_share`, NOTES "Scheduler").
- **Cancellation is the deadline's mechanism, and the deadline is a cancellation by timer.** A cancellation sets
  the ring's `cancelled` flag and its deadline to 1, so every loop tick and driver entry throws through the
  deadline path (with its unwind budget), every park point checks the flag before and after the wait, and a
  parked coroutine is woken by claiming its current waiter (`cancel_locked` in `sched.c`; a cancellation that
  lands between the caller's check and its park claims the waiter itself, so the park is skipped, not stranded).
  The *kind* lives on the `clj_coro` and outlives its stack (`CLJ_CANCEL_REQUESTED` from `cancel!`/`future-cancel`,
  `CLJ_CANCEL_DEADLINE` from the timer, `CLJ_CANCEL_SCOPE` from a `go-scoped` failure — the only kind that is
  cleared again, by the scope's exit, restoring the deadline it replaced); every kind throws through
  `clj_throw_cancelled(deadline)` (error.c), a `clj_cancellation` (not an ex-info) whose ex-type is
  `:cancelled` and whose ex-data's `:cancel/kind` is `:deadline` or `:explicit`; the message stays the old
  human text ("Execution timed out" or "Coroutine cancelled") for logs, but nothing catchable dispatches
  on it any more (design §4, "Тип ошибки", "Отмена — не `ex-info`"). The two causeless cancellations are
  built once at `clj_init`, immortal and shared, and thrown through `clj_throw_untraced`: no trace is
  captured (`ex-trace` of one was always nil, and the frames name the park, not the reason), so the throw is
  a pointer store and two plain cancellations are `identical?`. A cancellation from a scope carries the
  failure that caused it: `cancel_locked` records it in the `clj_coro`'s `cancel_cause` (shared, stored
  before the flag's release store, read back through `clj_coro_cancel_cause` after an acquire load of the
  flag), the throw site builds a fresh `clj_cancellation` around it, and `ex-cause` answers it — Go's
  `context.Cause()`. The slot is cleared with the flag (`uncancel_scope`, `cancel_reset`, a cleared
  deadline) and at `finish`, since the cause is an exception that may reach a channel that reaches the
  coroutine. The siblings a scope cancels keep the `REQUESTED` kind and take only the cause
  (`clj_chan_cancel_cause`): `CLJ_CANCEL_SCOPE` is the kind a scope's exit clears, so a sibling running its
  own `go-scoped` would have its cancellation lifted by that scope's exit. `clj_deadline_set_ms` arms a timer on the timer thread
  (`clj_coro_deadline_arm`, a serial per arm so a stale firing is a no-op) whose firing is `cancel_locked` with the
  deadline kind: a coroutine parked past its deadline is woken too, where before only a running one met it at a
  tick; clearing the deadline disarms the timer and lifts a deadline cancellation. A blocking-pool wait is not
  cancellable (`clj_park_uncancellable`): the job's result would have no owner. The flag is sticky: after the
  first throw every later park point throws again, so cleanup that must wait does so in a `catch`. Deadlines are
  per coroutine and conveyed at spawn (a cancelled parent hands down the deadline it had, not its flag).
  `thread` bodies are cancellable through their channel: the channel holds the blocking thread's implicit
  coroutine while the body runs, a `cancel!` before the thread attached sets a flag on the job, and the
  implicit coroutine's cancellation is reset for the thread's next job (`clj_coro_cancel_reset`).
- **`with-deadline` is the construct that imposes a deadline, and the only thing that reads an expiry as
  `:timeout`** (design §4, Trio's `fail_after`; `boot/core.clj`, `clj_deadline_push_ms`/`clj_deadline_pop` in
  eval.c). It lives in core, not in `clojure.core.async`: a deadline over synchronous code must not need
  channels, and it composes with `go-scoped` by nesting either way round instead of being an option on it.
  The ring holds one deadline, so nesting is a minimum and a firing one names no owner: the push installs
  `min(now + ms, the one in force)` and answers whether the installed one is the caller's, and only then may
  the pop read the cancellation as this call's timeout. An outer deadline's expiry, and any explicit cancel,
  leave untranslated as `:cancelled` — being stopped from outside is not my timeout, the subtle half of the
  feature and the one Trio also gets right. `:timeout` is an ex-info (`{:type :timeout :timeout/ms ms}`), so
  `(catch :default e)` catches it: a deadline the code imposed on itself is an ordinary failure of its own
  operation, where the `:cancelled` exclusion exists for cancellation arriving from outside (Go's
  `DeadlineExceeded` vs `Canceled`; Kotlin's timeout *under* `CancellationException` is rejected). Two things
  that look optional and are not. The pop clears the deadline cancellation it translated, or the sticky flag
  throws again at the next park. And the pop reports whether the deadline had expired at all, because a body
  that absorbed the expiry — a `go-scoped` that joined the children its own inherited deadline killed — looks
  like a plain return once the flag is gone; an explicit cancel outranks that, or the timeout would swallow a
  cancellation from outside. Children need no dance of their own: a spawn conveys the parent's deadline
  (`clj_coro_spawn`, with its own timer), so a coroutine started inside the extent meets it on its own stack,
  and joining them is what `go-scoped` is for. `clj_coro_deadline_own` sees through the poison: a cancelled or
  suspended ring reads 1, and pushing a minimum against that would park the real deadline as 1 for good.
- **`shielded*` is the region a cancellation cannot interrupt** (Trio's `CancelScope(shield=True)`;
  `boot/core.clj`, `clj_coro_shield_enter`/`leave`). The shield is the third writer of the ring's one deadline
  word: a poison puts 1 there, a shield puts 0, and the real value waits in `deadline_before` for whichever ends
  last — so inside the region no call, loop turn or driver entry has anything to throw, and the flags keep
  standing for the first check after it to meet. The count is on the `clj_coro`, so a cancellation landing
  mid-region only records itself, and the leave *recomputes* the word from the flags instead of restoring what
  it saved. What it does not touch: `(cancelled?)` still answers true, a child spawned in the region still
  inherits the real deadline (`clj_coro_deadline_own` sees through both), and a park is still cancelled unless
  the operation itself is uncancellable — the shield holds the runtime's own checks, and a waiting operation
  must be made unbreakable on its own account (`chan-take*`'s flag). `shielded*` is a macro that expands
  inline, not a call to a function behind it: under an expired deadline that call is exactly what throws.
  The one consumer is `go-scoped`'s exit ("Futures and scopes"); it is `*`-internal because a region that waits
  for something that never comes hangs for ever and no caller's deadline breaks it.
- **Suspension is the cancellation's mechanism without the throw** (design §4, "Стек как объект", item 3):
  `suspend!` sets a sticky `suspend` flag on the ring and poisons the same deadline with 1, so the branch every
  call already pays is what meets it and nothing is added to the hot path. What differs is the tick's *answer*:
  `deadline_reached` calls `clj_coro_suspend_point` instead of throwing, and that returns "no throw", so the
  interpreter's call and loop checks and the compiler's emitted `if (tick)` go on where a cancellation would
  unwind. The gate is `clj_lot_park` on the coroutine's own address (the lot of `cmutex.c`), and `resume!`
  clears the flag, restores the deadline and `clj_lot_unpark_all`s it; the flag is read under the bucket's lock,
  so a clear racing the park is either seen before the enqueue or found by the unpark. The deadline is the
  *shared* poison: a cancellation and a suspension both replace it with 1 and keep the real one in
  `deadline_before` (`deadline_poison_locked`), which comes back only when neither stands
  (`deadline_unpoison_locked`) — otherwise an uncancelled scope under a standing suspension would hand the ring
  a live deadline of 1, and a resume under a cancellation would lift it. A request lands at the next tick only:
  a coroutine parked on a channel is left parked, it is already not running, and meets the gate after its wake;
  one that stays there is evacuated by the sweep like any cold parked coroutine. **A cancel reaches a suspended
  coroutine**, or it would be a leak: `clj_coro_cancel_kind_cause` clears the suspend flag before the cancel
  lands and releases the gate, so the tick it wakes into finds `cancelled` and throws exactly once and nothing
  parks it again while it unwinds; a later `resume!` is a no-op and answers false. **A suspension is legal only
  where nothing is held**, and an illegal point defers rather than fails — the request came from another
  coroutine, so there is no caller there to fail, and throwing at the target would make `suspend!` a remote
  exception. `clj_coro_suspend_point` refuses `host_depth`, `locks_held` and `cmutex_held`, and sets the
  countdown to 1 so the retry is the next *call*, not the next 1024: a loop of a fixed length meets the check at
  the same instruction every turn, and a phase that falls inside a `swap!` would never park at all (the
  `locking` test caught exactly that). `cmutex_held` counts the cmutexes held *around user code*, not every
  `clj_cmutex_lock`: that one is an inline CAS on the atom hot path and a counter in it is not free. Only three
  holders can span a tick, because only three run user code under the mutex — the atom's `enter`/`leave` (the
  swap fn and its validator), `clj_monitor_enter`/`exit` (`locking`), and `chan_lock`/`chan_unlock` of a channel
  with a transducer. Each already looks the execution up where it takes the mutex, and each release takes it back
  from what it stored (`a->owner`, `ch->cm_owner`, the monitor's `me`) instead of the TLS: a second
  `clj_coro_current()` in the atom's `leave` alone cost 9 ns of `swap! inc`'s 38, where the two field updates cost
  about one (38.4 → 39.7 ns on this machine's bench row). Everything else takes a cmutex across straight-line C,
  where no tick runs, and `make cmutex-audit` fails when that set of sites changes, because a new one added
  without the bump is silent: nothing crashes, a suspended coroutine just keeps an atom or a monitor.
  **A cmutex is not the only thing held across user code.** A lazy seq claimed `FORCING` is the other one: its
  thunk is user code and every other reader of a shared object parks on it in the lot until the publish, so
  `forcing_held` counts the claims (`claim`/`publish`/`unclaim` in seq.c, not the thunk's own frame — the claim
  is wider on both ends) and defers a suspension the same way. `suspend!` takes a **channel**, like `cancel!` through `chan-cancel*`: `go`, `future` and `thread` hand
  out channels and nothing hands out a coroutine. Unlike `cancel!` it does not remember a request that arrives
  before a `thread` body attached (there is no `cancel_early` twin) — a suspension is done to a running body.
  `suspended?` answers the flag, not the park: the body may still be a few calls short of its gate.
- **Uncaught errors**: a coroutine whose body throws reports through `clj_coro_set_uncaught_handler`, by default
  the message and the trace on stderr with `write(2)` (design §4 reserves stderr for fatal and crash; this is
  the JVM's uncaught-exception report and a host replaces it). A `go` channel then closes with nothing put.
  A cancellation is silent only when the dying coroutine's *own* `cancel` flag is set: a `go` that merely
  awaited someone else's cancelled future is a bystander, its flag is clear, and it is reported like any
  other failure (design §4, "Необработанная отмена — не сбой"). `scope-spawn` reads the same flag through
  `(cancelled?*)` before it lets a child comply quietly.
- [ ] Not done, with triggers: `Runtime.eval` from a bare thread that parks blocks that thread (the JVM's `<!!`); the
  host-depth error is raised only under `clj_host_invoke` (`Value.apply`, the trampoline). Trigger for making
  a park in a top-level `Runtime.eval` on the main thread an error: the async bridge, which gives the host the
  alternative. That trigger has fired: the bridge is built ("The async bridge" under "Host bridge"), and only its
  `callBlocking` refuses the main thread. The static `:park` fact and the `:effects` lint are in "Facts";
  `go-scoped` and `future`/`promise` are in "Futures and scopes".

