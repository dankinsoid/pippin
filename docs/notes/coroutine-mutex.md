## Coroutine mutex (Sources/CljCore/cmutex.c, include/clj/cmutex.h)

- [~] **One word, a parking lot behind it**: 0 free, 1 locked, 2 locked with waiters, 3 free with waiters (the
  waiters bit outlives an unlock so a barging locker cannot strand the queue). Uncontended a lock is one CAS
  and an unlock one CAS, the same as `os_unfair_lock` — the `swap! inc` row did not move (42.6 → 38 ns). Contended
  the locker spins 64 `yield`s (a holder is usually done in tens of ns), then queues a waiter in the lot's
  bucket for the mutex's address (256 buckets under `clj_lock`s, as parking_lot and futexes do) and parks; the
  unlock frees the word and wakes one waiter, who competes again (Go's normal mode). The brief's hand-over to
  the first waiter was tried and dropped: with four carriers contending, every acquisition then goes through a
  park and a resume (a convoy), 2.9 µs per `swap! inc` against 60–110 ns without it. Trigger for a starvation
  mode (Go's after 1 ms): a profile showing a waiter that never wins. A waiter enqueues only once the word says
  "locked with waiters" under the bucket lock (the first cut enqueued behind a state that a concurrent unlock
  had just freed, and that waiter was never popped: the hang the bench found). A mutex wait is not cancellable
  and a wait from a bare thread or under a host call blocks the thread.
- **Holders**: every atom (`enter`/`leave` with the owner being the execution, the nested-swap trap unchanged),
  `locking` (a reentrant monitor per object in a table keyed by identity, records living while held or waited
  on: `monitor-enter*`/`monitor-exit*`, `clj_debug_live_monitors`), and a lazy seq being forced (a one-shot wait
  on the lot with `FORCING_WAITED` as the "someone parked" state — the `sched_yield` spin is gone; "Recursive
  realization" stays). A channel with a transducer holds it around its step ("Channels"); `promise`/`future` are plain promise-buffered channels, a runtime-only section under a `clj_lock`.
- **Lock audit** (design §4, "under `clj_lock` runs neither user code nor IO"; every `clj_lock_lock` at the time
  of this task, 58 sites in 12 files; 82 in 15 after it, the new ones in `chan.c`, `cmutex.c`, `sched.c`,
  `coro.c`; 72 in 16 after the library layer, `chan.c` down to one site behind `chan_lock` and `runtime.c` up by the
  capture buffer — all runtime-only sections):

  | file | sites | what runs under the lock | verdict |
  |---|---:|---|---|
  | `atom.c` | 2 | `f`, the validator | **violated** → the atom's lock is a `clj_cmutex`; `deref` takes no lock at all |
  | `load.c` | 12 | registries, the failures vector; one `fopen` per load-path root | **violated** (IO) → the roots are copied out and probed with `access` unlocked; the file is read on the blocking pool |
  | `callers.c` | 7 | the caller index | runtime only |
  | `ns.c` | 12 | the namespace tables | runtime only |
  | `shape.c` | 6 | the shape tree | runtime only |
  | `proto.c` | 6 | tables, the reify registry, `wait_readers` spinning on windows that hold no user code | runtime only |
  | `specialize.c` | 4 | the facts pass over existing trees (no macroexpansion) — long, no user code | runtime only |
  | `profile.c` | 3 | the profile table | runtime only |
  | `reader.c` | 2 | the feature set | runtime only |
  | `trace.c` | 2 | the frame tables | runtime only |
  | `eval.c` | 1 | a keyword site's fill | runtime only |
  | `keyword.c` | 1 | the intern table | runtime only |
  | `chan.c` (new) | 1 | `chan_lock`: the buffer and the queues of a plain channel; wakes after the unlock; a channel with a transducer takes a `clj_cmutex` instead ("Channels") | runtime only |
  | `runtime.c` (new) | 2 | a shared `with-out-str` capture's buffer: a `memcpy` | runtime only |
  | `cmutex.c` (new) | 7 | a lot bucket, the monitor table; the waiter's own claim lock | runtime only |
  | `sched.c` (new) | 4 | a waiter's claim, the paired claim | runtime only |
  | `coro.c` (new) | 3 | the stack-mapping cache; the live list of coroutines (the evacuation sweep's snapshot: retains only) | runtime only |

  Every syscall that could block a carrier is off its path: `fopen`/`fread` (the loader) on the blocking pool,
  `fwrite(stdout)`/`out_fn` on the writer thread with backpressure, the lazy-seq `sched_yield` spin replaced by
  a park; `fprintf(stderr)`/`write(2)` remain for fatal, crash and the uncaught report. The backstop is live:
  a park with a `clj_lock` held throws, and the guard handler still refuses to convert an overflow under one.

