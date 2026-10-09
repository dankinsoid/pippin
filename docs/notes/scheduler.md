## Scheduler (Sources/CljCore/sched.c)

- **Carriers are our own pthreads**, one per core (`CLJ_CARRIERS` overrides), `QOS_CLASS_USER_INITIATED`,
  detached, never exiting; not GCD, which offers no thread count and no promise that a coroutine's stack stays
  off foreign queues. One global run queue under a pthread mutex (`run_mu`), plus a **next slot per carrier**: a
  resume from a pool coroutine puts the woken coroutine in the resumer's own slot (Go's `runnext`), so a pair
  handing values back and forth stays on one thread with two switches per hop and no wakeup of anything; an
  occupant already there moves to the queue. Idle carriers steal a slot only once it is 5 µs old (a fresh one
  belongs to its pair). An idle carrier first spins for 20 µs in 2 µs slices (at most two spinners; a spinner
  takes `run_mu` only for work it has seen or at the budget's end), then polls the queue every 50 µs for 1 ms,
  then sleeps. Measured (bench/RESULTS.md, "Coroutines and channels", "One wake per burst"): ping-pong round
  trip ~500 ns on the pool against 461 on one carrier; `(<! (timeout 0))` 1.9 µs (the timer thread's wake and
  the carrier's); a `go` from the main thread 535 ns spawn + finish, ~560 in bursts of 100 (was 1.65 µs).
- **The wake protocol (Go's `wakep`)**: every idle carrier waits on *its own* mutex and condition
  (`park_mu`/`park_cv`), listed on `idle_head` under `run_mu`, most recent first. An enqueue pushes under
  `run_mu` and pops at most one carrier to wake, and only when nobody spins and no popped carrier is still on its
  way (`woken`); the signal is sent after `run_mu` is released. The popped carrier clears `woken` when it
  reaches `run_mu` again, takes its item, and if the queue still holds more pops the next carrier itself
  (Go's `resetspinning` chain), so the enqueuer pays one wake per burst and the pool grows one carrier per wake
  latency while work waits. Work placed from inside the pool (a hand-off, a lock's unlock) wakes nobody while a
  poller exists: a poller looks within 50 µs and a wake is a syscall per item; work from outside the pool (a
  timer, the main thread, a blocking job) is latency and wakes. **The invariant — no runnable coroutine sits in
  the queue while every carrier sleeps** — holds because a carrier sleeps only after `take_work_locked` found
  nothing and it pushed itself on `idle_head` in the same `run_mu` hold, and an enqueue is ordered after that
  by the same mutex: it sees the carrier on the list and either pops it (the signal follows the unlock), or
  finds a spinner or a popped carrier that has not yet run `take_work_locked` under `run_mu` after this push,
  or finds no idle carrier at all — every running carrier returns to `take_work_locked` before it can sleep. No
  seq_cst pair is needed: the mutex orders the push against the sleep decision; the spinner's racy glance is an
  optimization confirmed under the lock. A poller whose timeout raced its wake returns once with a stale
  `signaled` and simply looks at the queue again. Measured on macOS: the cost of a `go` from main was not the
  `pthread_cond_signal` (~175 ns of 1.65 µs) but `run_mu` itself — every enqueue woke another carrier, twelve
  woken carriers queued on the mutex in the kernel, and once one waiter is in the kernel every unlock is a
  `__psynch_mutexdrop` syscall (~850 ns of the 1.65 µs went to `__psynch_mutexwait`/`mutexdrop`). Hence the
  per-carrier conditions (a wake never touches `run_mu`), the trylock spin before blocking (`run_lock`, 64
  tries: a holder is out within ~100 ns), and the spinner that locks only for work it has seen. The 4-carrier
  `locking` row moved 365 → 405 ns: the resumer no longer pays a kernel wait per resume, so the four coroutines
  contend on the monitor harder; `swap!` under four carriers is unchanged.
- **A spawn starts eagerly, and that is the decision, not an accident.** Measured with a `go` that reads a counter
  its spawner then increments a thousand times: the block sees 0–15 of 1000 here, and 1, 70, 76, 106 and 1000 of 1000
  on JVM core.async 1.6.681 (600–4000 of a million, so the JVM's head start is a time, not a count of the spawner's
  work). Neither host orders the first step against the spawner's next form, so the ASYNC-127 block of
  `async_test.clj` passes there on dispatch latency alone (NOTES "Corpus"). Spawn locality — a spawned coroutine not
  starting before its spawner's nearest park — is refused in design §8: a spawner that never parks (a UI handler
  returns to the run loop) would start none of its children, and "not yet runnable" would make the wake invariant
  above conditional on another execution's state, so a carrier blocked under `host_depth` would hold its children
  too. It would buy the order of the first step and nothing beyond it.
- **The spawn trace stays eager** (`clj_coro_capture_spawn_trace`, ~35 ns for the walk plus the retains; the
  frames live inline in the coroutine up to four, malloc beyond). Materializing it at the first throw from the
  spawner's ring position is not possible without losing it: the ring's slots are rewritten as soon as the
  spawner returns and calls again (a UI handler returns to the run loop long before its `go` throws), the
  spawner's real stack — where the compiled frames are read from — is gone by then, and the ring itself is
  freed or recycled when the spawner finishes. An epoch per frame would only tell us the trace is lost.
- **Park and resume** (`clj_park`, `clj_resume`, `clj_waiter`): a park takes the coroutine's own pthread mutex,
  stores `PARKED`, and switches out *holding it* — the carrier unlocks after the switch on its own stack — so a
  resumer that acquires the mutex finds the context fully saved or the coroutine not yet parked; in the second
  case it sets `resume_pending` and the park returns at once. A waiter is the unit of parking: refcounted,
  malloc'd, shared by every queue it sits in (an `alts!` puts one in each port), claimed once under its own
  `clj_lock` (`clj_waiter_claim`; `clj_waiter_claim_pair` claims the two sides of a hand-off together under both
  locks in address order, so two channels pairing the same waiters cannot deadlock — core.async's
  `lock`/`active?`/`commit` in one word). A stale node (its waiter claimed elsewhere) is dropped when a queue
  meets it. A bare thread's implicit coroutine parks on its condition; a pool coroutine under `host_depth > 0`
  (a `locking` or a lazy seq forced inside a synchronous host call) blocks its carrier the same way (the hybrid
  of design §4) — the channel operations refuse instead (`clj_park_allowed`: an error with a trace to the wait,
  never a block). The dev backstops: a park with a `clj_lock` held, a park under `host_depth`, and a park of a
  cancelled coroutine are errors (`CoroTests`, `ChanTests`). A cmutex held and a lazy seq being forced are not:
  both wake the waiters when the holder finishes, so the wait ends. `suspend!` is the one that never does, and
  it defers on all four (`clj_coro_suspend_point`, `forcing_held` above).
- **Main carrier**: `clj_sched_main_install` on the main thread adds a version-0 `CFRunLoopSource`; a
  main-affinity coroutine (`go-main`, `CLJ_AFFINITY_MAIN`) is queued separately and the source signalled, and
  each turn of the run loop runs what is queued (`clj_sched_main_pump`). Without an installed carrier a
  `go-main` is an error, not a silent pool run; a test adopts the calling thread (`clj_debug_sched_main_adopt`)
  and pumps by hand, and `CoroTests.mainRunLoopSource` runs the real source from a `@MainActor` test turning
  `CFRunLoopRunInMode`. Only coroutines have an affinity; an atom is correct from any carrier, and its
  `:affinity` option is ignored (design §4 «Атомы»).
- [~] **Blocking pool** (`clj_blocking(fn, ctx, size)`, `clj_blocking_detach`): two pools of one code (`pool`
  in `sched.c`), threads made on demand and retired after a minute idle (the JVM's cached pool;
  `clj_debug_blocking_keep_alive_ms` shortens it for a test). Internal jobs (`clj_blocking`: the loader's
  `read_file`) never wait on one another, so their pool is capped at 64 threads and queues past the cap.
  `thread` bodies wait on one another as JVM threads may, so theirs has no cap: a body takes an idle thread or a
  new one at once and never queues (design §3 «Инвариант: язык не меняется»); one pool with the cap would let 64
  bodies blocked on a 65th deadlock, and would make a `require` from a go block wait behind bodies
  (`ChanStressTests.threadBodiesNeverQueue`: 100 bodies, each waiting on the next). A pool spawns while its
  queued jobs outnumber its idle threads: an idle thread stays counted until it wakes, so "spawn only when none
  is idle" let two submits share one idle thread and queue the second behind `thread` bodies blocked on its
  output (`AsyncLibTests.pipelines` on CI, runs 36927938282, 36985871664, 36987070315). A thread retires in the
  same hold of the pool's mutex that found nothing queued, so no job counts on it. Its exit frees what was per
  thread: the implicit coroutine and its carrier, the signal stack (`thread_exit` in `coro.c`), and the
  allocator heap, which goes whole to the next thread that needs one (NOTES "Allocator"), as its protocol
  reader slot does (NOTES "Type descriptor"): `clj_proto_wait_readers`, on every atom commit, scans them all. What could still
  reach the freed coroutine is waited for: a `cancel!` that read the body's coroutine off its channel lands
  before `thread_run` resets the cancellation (`cancels` on the channel; past the reset it would cancel the
  thread's next job), and a deadline fire the timer thread popped before the last disarm is waited out
  (`timer_quiesce`). A binding frame names its owner by the execution's id, never reused, not by its address,
  which the next thread's implicit coroutine is likely to get: `set!` from a body conveyed a frame of a retired
  thread's body would pass the owner check (`ChanStressTests.idlePoolThreadsRetire` met it). A pool coroutine
  submits the job with a heap copy of its `size`-byte context and parks (uncancellable), the thread works on
  the copy, the parker copies it back after the wake — the parker's frame is never written by another thread
  (the evacuation invariant under "Coroutines"); a bare thread runs the job inline on the original. `thread`
  runs its body as the thread's implicit coroutine with the spawner's bindings conveyed. **Timers**: one thread, a list sorted by
  deadline (`clj_sched_timer`); `timeout` closes its channel from it. A thread woken out of
  `pthread_cond_timedwait` answers ~0.7 µs later than one woken out of `pthread_cond_wait` (the kernel arms a
  deadline per wait; `(<! (timeout 0))` measured 2.1 → 2.9 µs whenever any far timer was pending — the
  evacuation sweep's timer made that permanent), so a deadline farther than 2 ms is kept by one reprogrammed
  dispatch timer (`far_wait`) that signals the condition, and the thread waits untimed. The dispatch timer is
  `DISPATCH_TIMER_STRICT`: a leeway of 0 alone still let the OS coalesce it, and 10 and 20 ms timeouts fired 25–35%
  late (12.5 and 25–27 ms on an Intel Mac, 10.07 and 20.1 strict), which a corpus library's 10 ms producer against
  its 20 ms read timeout turned into a lost item (enos, NOTES "Corpus"; `ChanTests.aFarTimeoutFiresOnItsDeadline`).
  A deadline's timer is cleared from its coroutine by its own firing, live or not: the timer thread frees it once
  the callback returns, and a disarm that read it after — a coroutine finishing as its deadline fired — was a
  use-after-free, which the on-time timer made frequent enough for ASan to catch (run 37833744893).
  Trigger for a heap: profiles with thousands of live timeouts.
- **The timers' clock stops while the device sleeps.** `clj_profile_now` is `CLOCK_UPTIME_RAW` (Darwin's
  `mach_absolute_time`) and `far_wait` arms its dispatch timer off `DISPATCH_TIME_NOW`: neither advances across
  a device sleep, so a `timeout` armed before the screen locked fires that long after the wake rather than at
  once — taken as the semantics (design §4, "Часы таймеров": a timeout behaves as a budget, and no burst of
  expired timers lands on a resume). The waits left to the condition — the sub-`FAR_NS` timer waits and the
  carriers' poll — take `pthread_cond_timedwait_relative_np` on Apple (`cond_wait_ns` in `sched.c`), which takes
  the relative timespec and reads no clock at all, since Darwin has no `pthread_condattr_setclock`. Its
  non-Apple branch still computes an absolute `CLOCK_REALTIME` deadline, a clock a `settime` step can move; a
  port where every deadline waits on the condition must not leave it that way (docs/portability.md, `sched.c`).
- **Output** (`runtime.c`): `clj_output` copies the bytes into a bounded queue (1 MB) drained by one writer thread
  that calls the host's `out_fn` or `fwrite`; a printer that finds the queue full parks (blocks on a bare
  thread) until the writer drains below the limit; `clj_output_flush` waits for an empty queue and an idle
  writer, `clj_set_output` flushes before swapping the hook, `atexit` flushes. `with-out-str` captures are per
  execution and bypass the queue; a spawned coroutine (`go`, `thread`, `future`) shares its spawner's top capture
  (the JVM conveys `*out*`): the capture is refcounted and its buffer under a `clj_lock` (a `write` is a
  `memcpy`, runtime only), the string is what was written when the capture pops, and a child still holding it
  writes into a buffer nobody reads (`FutureTests.withOutStrIsConveyedToAGoBlock`).

- **Seeded mode** (`CLJ_SCHED_SEED=<n>`, read by `clj_init`; design §3 «Корректность реализации», item 4): a run is
  a function of the seed. One carrier (`seed_carrier_main`) runs the pool and takes the next runnable out of a bag
  by a SplitMix64 pick; the queue, the `next` slot, stealing and spinning are not used. A spawn, a channel
  operation and an atom write are preemption points (`clj_sched_point` in the builtins: a global load and a
  predicted branch otherwise), and so is a coroutine's tick: every ring of a seeded run holds the poison
  (`poisoned_locked` answers true), the real deadline waits in `deadline_before`, and the tick's slow path asks
  `clj_sched_seed_tick` — cancelled, suspended, past the deadline, else a seeded yield, after which a cancel that
  came meanwhile is met at the next tick. At a point, with probability 2^-k, k from the seed, a coroutine parks
  with `clj_wake_yield()` and the carrier puts it straight back in the bag; where a park is refused
  (`host_depth`, a `clj_lock`, a cancel) it goes on. A bare thread holds the turn from its outermost evaluation's
  entry (`clj_eval` before the analysis, whose macros run code too; `exec_depth` 0 → 1 in `exec_run_at`,
  `clj_host_invoke`, `clj_eval_top_enter`) to its leave or a landing past it; the carrier waits while one runs and
  works while it is parked, and a woken bare thread is put in the bag and goes on only when the pick names it.
  While the host is outside, the carrier drains the bag for at most `SEED_DRAIN_MAX` (64) runs and then waits for
  it, or for 20 ms of quiet; an entry begins at that point, so its starting state does not depend on how long the
  host stayed outside, and a coroutine that never ends does not tick the clock away meanwhile. Timers and
  deadlines (`clj_sched_now`), `nano-time*`, `System/nanoTime` and `System/currentTimeMillis` read a virtual clock
  from a fixed origin; it jumps to the next timer when the bag is empty and a bare thread is parked (in an
  evaluation or not) or a settle runs, and after 20 ms of real quiet when the host is outside (a Swift polling
  loop); a tick adds 100 µs per 1024 calls, so a runaway loop under a deadline ends the same way every run.
  `thread` and `send-off` bodies are coroutines, `clj_blocking` jobs run in place, the cycle collector has no
  thread and collects when the model is stuck or a settle asks, the evacuation sweep is off, `alts!` order and
  `rand` draw from a second PRNG stream, and a channel hashes by a serial. `BootedTrait` reseeds before each test
  from the process seed and the test's id (and restarts the test thread's ticks), so a failed test replays alone;
  `make test-seeded` runs the concurrency suites over `SEEDS` (16 by default), one process each, prints the seed and
  the replay command of a failure, and checks that `SeededTests` printed the same lines under every seed. Outside
  the model, as the design lists: host threads (Swift `async`, a `Thread`, dispatch), `go-main`, the writer's
  timing, a park only an outside thread ends (the next timer fires first), a bare thread's busy wait inside an
  evaluation (it keeps the carrier waiting), a coroutine blocked under `host_depth` (stderr says so), identity
  hashes other than a channel's, two bare threads inside evaluations at once. Tests of the pool itself or of an
  outside thread are disabled under it (`outsideTheSeededModel`).
