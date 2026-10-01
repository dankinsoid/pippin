## Futures and scopes (Sources/CljCore/chan.c, boot/core.clj, boot/clojure/core/async.clj; design §4)

- **A future and a promise are promise-buffered channels** with a role (`CLJ_CHAN_FUTURE`, `CLJ_CHAN_PROMISE`;
  `chan.h`): `deref` is a take that rethrows the future's cached exception (`ch->error`, kept by `future_done`
  instead of the uncaught report), `(deref x ms timeout-val)` is an `alts!` against a `timeout` with `:priority`,
  `realized?`/`future-done?` read the buffer or the closed flag, `deliver` puts once (nil closes: takes then
  answer nil for ever, and `realized?` is true), `future-cancel` is `cancel!`, and both are `alts!` ports as
  design §4 promised (the JVM cannot). `@f` parks from any function and blocks only a bare thread; a deref
  parked in a coroutine is woken by the coroutine's cancellation like any take (`FutureTests`). A cancelled
  future is done at once (`clj_chan_realized` reads the coroutine's flag: the JVM's `isDone`) and its deref
  throws the cancellation as soon as the body lands. `(deref f ms v)` does not take that short cut: it asks
  whether a value is actually there, since between `future-cancel` and the end of the body's unwinding the
  future answers `realized?` with nothing to give, and the untimed deref behind the short cut would run past
  `ms` (`FutureTests.derefWithTimeoutOfACancelledFutureStillTimesOut` holds the window open with a
  `load-file` blocked on a FIFO, an uncancellable park). `future-call` spawns on the pool with the bindings and the
  output capture conveyed; the trace of a rethrown exception is the future's own frames then the spawner's
  (`thrower`, `future-call`, `spawner`). `pmap` is the JVM's: futures kept `(+ 2 (available-processors*))`
  ahead of consumption, where `available-processors*` is the carrier count; `pcalls`/`pvalues` over it.
- **`go-scoped` is structured spawn through the binding chain** (design §4, "Контекст go"; `scoped*` in
  `async.clj`): the scope is a value in the dynamic var `*scope*`, which `binding` establishes and every spawn
  conveys, so a `go` anywhere in the dynamic extent — in a called function, in a coroutine a child spawned — is
  a child of the same scope; a `go` outside any scope stays unstructured. The scope counts pending children
  (raised before the spawn, so a join that finds zero has nothing to wait for) and the join takes from a
  sliding-buffer channel the last child puts on with an *uncancellable* take: a scope never returns while a
  child runs, even after its own cancellation. A child's uncaught error is the scope's first error: it cancels
  the siblings and the body (`coro-cancel-scope*` on the coroutine running the scope — an implicit one when the
  scope is on a bare thread), both carrying it as the cause the siblings read through `ex-cause`, and is
  rethrown from `go-scoped` after the join; a child that died of a cancellation it was not the target of is a
  failing child, not a compliant one (`(cancelled?*)` in `scope-spawn`); a body error cancels the children
  first; a `cancel!` of the coroutine running the scope reaches the children transitively (each nested scope's
  body is cancelled, cancels its own children, joins); on exit the scope's own cancellation is lifted
  (`coro-uncancel-scope*`), so the caller's coroutine is usable again. `plet` is `async let`: every init in its
  own `go` under one scope, the bindings their values, a failing init cancels the rest and rethrows. Not in the
  JVM's core.async (docs/jvm-differences.md). **The whole exit is shielded** (`shielded*`, below): an
  uncancellable take is not enough, because the join is a `loop` and every turn of it throws through the deadline
  path like any other. Past the unwind budgets of an expired deadline every check throws, and there the scope's
  own bookkeeping ran: the exit returned with children still running, and a child's `scope-child-done!` threw
  before the decrement the join waits for, so the scope hung on a child that had already left (`AsyncLibTests`,
  the `spend` case, is that second one; the first needs a child whose only end is the scope's own cancel). `coro-uncancel-scope*` is in a
  `finally` inside that shield, since a throw from the join would otherwise leave the scope's cancellation
  standing on a coroutine the caller goes on using. The price is the promise itself: a child that never ends —
  one stuck in a synchronous host call, where no check of ours runs — hangs the scope for ever, and no deadline
  of any caller breaks that. Trio accepts the same of a nursery. Measured (bench/RESULTS.md): a scope with 100 children 3.5 µs per
  child against a bare `go` spawn of 1.65 µs (the wrapper fn, two `swap!`s, the set, the done put); `future`
  spawn+deref from a bare thread 2.7 µs, `promise` deliver+deref 128 ns, `pmap` 957 ns per element.

