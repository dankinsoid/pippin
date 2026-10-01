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
  `CFRunLoopRunInMode`. `(atom x :affinity :main)` checks the carrier on every access (one flag test on the
  fast path): a pool coroutine's `swap!`/`deref` of it is an error with a trace.
- **Blocking pool** (`clj_blocking(fn, ctx, size)`, `clj_blocking_detach`): threads made on demand up to 64, kept
  for ever; a pool coroutine submits the job with a heap copy of its `size`-byte context and parks
  (uncancellable), the thread works on the copy, the parker copies it back after the wake — the parker's frame
  is never written by another thread (the evacuation invariant under "Coroutines"); a bare thread runs the job
  inline on the original. The loader reads files there (`load.c` `read_file`), `thread` runs its body there as
  the thread's implicit coroutine with the spawner's bindings conveyed. **Timers**: one thread, a list sorted by
  deadline (`clj_sched_timer`); `timeout` closes its channel from it. A thread woken out of
  `pthread_cond_timedwait` answers ~0.7 µs later than one woken out of `pthread_cond_wait` (the kernel arms a
  deadline per wait; `(<! (timeout 0))` measured 2.1 → 2.9 µs whenever any far timer was pending — the
  evacuation sweep's timer made that permanent), so a deadline farther than 2 ms is kept by one reprogrammed
  dispatch timer (`far_wait`) that signals the condition, and the thread waits untimed. Trigger for a heap:
  profiles with thousands of live timeouts.
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

