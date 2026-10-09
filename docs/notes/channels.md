## Channels (Sources/CljCore/chan.c, boot/clojure/core/async.clj, include/clj/chan.h)

- [~] **A channel is a buffer plus two queues of waiter nodes under a `clj_lock`**, a runtime-only short section
  (the audit below): no user code runs inside it — woken nodes are collected and resumed after the unlock, and
  a `put!`/`take!` callback runs on the execution that completed it (the timer thread for a `timeout`'s
  takers; trigger for dispatching callbacks to the pool: a callback that parks long on the timer thread). The
  fast path of `<!`/`>!` needs no waiter: the operation completes under the lock or a waiter is made and
  enqueued in the same lock hold. Buffers: fixed, dropping, sliding (`buffer`, `dropping-buffer`,
  `sliding-buffer` are spec objects `chan` reads); a fixed-buffer take refills from a parked putter; `close!`
  wakes parked takers with nil and leaves parked puts to be taken (the JVM's contract); the pending limit is
  the JVM's 1024 with the JVM's message, counted after purging stale nodes.
- **A channel with a transducer takes a `clj_cmutex` instead** (`chan_lock`: the one `clj_lock_lock` site of
  `chan.c` is behind a `has_xform` test). The transducer's step is user code — it may park (`(map #(<! …))`),
  and the design invariant says none runs under a `clj_lock`; the JVM runs it under the channel's mutex too.
  The step runs inside the section (`put_locked`, `refill`, `step_complete` at `close!`), the reducing fn is the
  channel itself (`chan-rf*`, immortal; the accumulator is the channel, so no cycle), and it refuses a call
  from outside its step (`cm_owner`). What the JVM's `ManyToManyChannel` does and this does: an expanding step
  overfills a fixed buffer (the ring grows past its capacity, the JVM's is a list), a filtering step completes
  the put without a transfer, `reduced` closes the channel and completes every parked put, the completion arity
  runs once at `close!` after the last parked put went through, a step's exception goes to the `ex-handler`
  and its non-nil answer into the buffer (no handler: the uncaught report), and a put from `alts!` takes the
  step under the paired claim. The mutex is not reentrant, so a step touching its own channel is an error
  ("… from inside its own transducer step"), not a deadlock (`AsyncLibTests.transducersOnChannels`). Measured
  (bench/RESULTS.md, "The core.async library layer"): buffered throughput `(chan 1024)` 128 ns per item against
  176 with `(map identity)` — the mutex is the same CAS uncontended, the 48 ns are the step's two interpreted calls
  (`chan-rf*`, then `buffer_add`).
  `promise-chan` is a promise buffer (`CLJ_BUF_PROMISE`): one value served to every taker for ever, the second
  put dropped, `close!` before a value answers nil for ever; with a transducer the step decides what that value is.
- **`alts!`** (`clj_chan_alts`): one waiter, one node per port, the paired claim decides the winner and the
  continuation moves to whoever won; a port that completes immediately claims the waiter under that port's lock,
  and an earlier port's node is then stale. `:priority` keeps the order, otherwise a per-thread xorshift
  shuffles it; `:default` never enqueues. `alt!` is the JVM's `do-alt` expansion (a put clause is `[[ch v]]`).
  Ports may be any sequential collection, which `chan-alts*` copies into a vector: the JVM's `do-alts` reads them
  by `count` and `nth`, and a library passes `(keys m)` (enos's `<!+`, NOTES "Corpus").
- **A parked `alts!` takes its result out of the waiter** (`parked_result`). The waiter outlives the park in
  the stale nodes of the ports that lost, which are dropped only when their queue is next walked; had it kept
  the `port` it was woken on, two `alts!` over `[[c v] d]`, one winning on `c` and the other on `d`, could leave
  `c → stale node → waiter → d → stale node → waiter → c`, invisible to `each_child` (takers' waiters are not
  visited). Measured before: two `chan` objects leaked in 12 of 3000 runs of that race, the CI flake of
  `ChanTests.altsWithDefaultTimeoutAndPriority`; after, none; `ChanStressTests.racingAltsPuttersLeaveNoChannelCycle`
  runs it 300 times under one baseline.
- **`go` spawns the body as an ordinary fn** (`go*`): the channel it returns holds the coroutine handle (for
  `cancel!`), the coroutine holds the channel until it finishes, puts a non-nil result as a fire-and-forget node
  and closes (`deliver_result`), so `(<! (go …))` is a join. `<!!`/`>!!`/`alts!!`/`alt!!` are the same functions;
  `thread` runs on the blocking pool and returns a channel the same way. `<!` works in any function, inside
  `map`, inside a lazy-seq thunk (`ChanTests.colorlessPark`, `CoroTests`). Measured (bench/RESULTS.md): buffered
  throughput 120–130 ns per item, `alts!` over two ports 660 ns per completion, ping-pong 530 ns per round trip.
- [~] **The library layer is core.async's own code over these primitives** (`async.clj`: `pipe`, `mult`/`tap`/`untap`/
  `untap-all`, `pub`/`sub`/`unsub`/`unsub-all`, `mix`/`admix`/`unmix`/`unmix-all`/`toggle`/`solo-mode`, `merge`,
  `take`, `into`, `reduce`, `transduce`, `onto-chan!`/`to-chan!` and the `!!` and deprecated forms, `map`,
  `split`, `pipeline`/`pipeline-blocking`/`pipeline-async`, `promise-chan`, `unblocking-buffer?`, the deprecated
  `map<`/`map>`/`filter<`/…/`unique`/`partition`/`partition-by`), with two shapes changed: `pipeline`'s default
  ex-handler is the uncaught report (`uncaught-report*`), and `map>`/`filter>`/`mapcat>` pipe a front channel
  into the target instead of wrapping it in a write port (the JVM's `WritePort` reify has no counterpart). The
  `go` macro expands to the primitive spawn (`coro-go*`) outside a scope, so a trace through a block shows no
  frame of the library (`CompilerFixtureTests.overflowInsideACoroutineThrowsOnlyThere` pins it); `make api-diff`
  reports 83 of core.async's 87 publics built, the four missing ones its ioc and macro plumbing
  (docs/api-parity.md). `clojure.core.async.impl.buffers` exists for code that
  requires it: the JVM's constructors over the spec objects. `Thread/sleep` resolves because a namespace named
  `Thread` holds a var `sleep` (`install_thread_ns`): the reader gives `Thread/sleep` as a namespace-qualified
  symbol, the analyzer resolves it as any `ns/name`, and the fn parks on the timer thread (`clj_sched_sleep_ms`;
  no `nanosleep` on a carrier). The trigger for the next such shim is a library calling another
  `Class/method` whose whole meaning this runtime has; it has fired once and the answer was a refusal —
  core.async's own tests call `Thread/currentThread`, and an identity that changes carrier at every park is
  not that (design §8, `docs/jvm-differences.md`).

- [ ] **A mult over two transducing taps, merged, deadlocks under load** (found by `make corpus-bench`, the
  `async-broadcast` workload; docs/notes/benchmarks.md). `(let [src (a/chan) m (a/mult src) x (a/chan 32 (filter
  even?)) y (a/chan 32 (map #(* % %)))] (a/tap m x) (a/tap m y) (a/onto-chan! src (range 20000)) (a/<!! (a/reduce + 0
  (a/merge [x y]))))` passes alone, 30 times in a row on a debug build, and on the JVM; with eight such processes
  side by side on an 8-core Intel Mac every one hangs on its first call, all carriers parked, the main thread in
  `chan_take` under `b_take`; a 64-slot source hangs the same. Two plain taps merged, one transducing tap alone
  (`filter` or `map`) and the pipelines without a mult pass under the same load. So it takes two transducing
  channels (`chan_lock`'s cmutex path) fed by the mult's `put!` callbacks and drained by `merge`'s `alts!`: a wake
  lost between them, not yet narrowed further. The first CI run of `make corpus-bench` hung 70 minutes in the
  workload that held this shape (an older `async-pipeline`).
