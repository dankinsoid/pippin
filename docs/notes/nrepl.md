## nREPL (Sources/CljNREPL, Sources/clj-nrepl; design §5c)

- **A session is a fresh coroutine per eval, not one long-lived coroutine per session.** The brief's own reading
  ("a session must be a coroutine") was one persistent coroutine looping on a work queue for the session's life,
  with `interrupt` cancelling just the in-flight eval. That runs into the binding-ownership rule head-on (NOTES
  "Coroutines": "a child cannot `set!` a binding it does not own"): if the persistent coroutine pushes the
  frame once and a *child* coroutine runs each eval, `(in-ns ...)`/`(set! *1 ...)` inside that child is refused,
  because the child does not own the frame the parent pushed. Keeping eval on the *same* coroutine that pushed
  the binding fixes that, but then `interrupt`'s cancellation (sticky, NOTES "Coroutines") would permanently
  wedge the session's own loop — every future park on that coroutine throws forever, and the only cure is
  `clj_coro_cancel_reset`, which is `coro_internal.h`, not public. Revisited for the whole-frame pass below and
  still the right call: nothing about carrying more vars changes the ownership or the sticky-cancellation
  problem, since both are per-coroutine facts, not per-var ones. The deviation: spawn one coroutine per eval
  request (`spawnCoroutine`, `CljNREPL/CoroBridge.swift`), let it push the session's whole frame as its own
  (so `set!` on anything in it succeeds), and let it die with the coroutine when the eval finishes —
  cancellation only ever needs to reach a coroutine that is about to end anyway, so the stock, unmodified
  `clj_coro_cancel` is enough for that part. `Session` (a plain Swift class, not a coroutine) holds the frame
  as a `Value` (a persistent map, `ReplVars.swift`) between calls and serializes a session's evals through a
  small queue (`scheduleEval`/`finishEval`) so at most one is in flight — this is what "a session must be a
  coroutine" was really protecting against (two evals racing the same frame), and Swift-side sequencing gets
  it without needing the coroutine identity to persist.
- **The session's whole frame, not just `*ns*`, round-trips through `clj_var_push_bindings`/
  `clj_var_get_thread_bindings`.** `Session.currentFrame` is a map var → value (`ReplVars.defaultFrame`: `*ns*`
  plus the REPL history vars below); `Evaluator.run` pushes it once, lets `set!`/`in-ns` mutate the live boxes
  through the eval, and — unless the eval was cancelled — captures the whole thread's bindings back with
  `clj_var_get_thread_bindings` and stores that as the session's new frame. Generic on purpose: whatever vars
  end up in the frame persist, without `Evaluator` naming them, which is how `*print-length*`, `*print-level*`,
  `*file*` and `*in*` joined it later at no cost here. **A frame
  captured from a cancelled eval is dropped, not stored**: the `defer` that captures only fires when a `pushed`
  flag is set (a failed push has nothing of ours to capture) and a `cancelled` flag is clear (`Tests
  sessionCarriesItsWholeBindingFrame`, the case an interrupted `(<!! (chan))` must not disturb `*1`). This is a
  conservative choice, not a proven-necessary one: a cancellation unwinds through `try`/`finally` like any
  other throw (NOTES "Coroutines": "cleanup that must wait does so in a catch"), so the frame at the point of
  cancellation is very likely already consistent; dropping it anyway costs nothing (the user retries the form)
  and removes the need to reason about the unwind budget (`shadow->unwinds`, 64) being exhausted mid-cleanup.
- **`interrupt` is `clj_coro_cancel` on that one eval's own coroutine handle, unmodified; telling a cancellation
  apart from a real error needed one new public C function.** `Session.interrupt` matches the request's
  `interrupt-id` against the in-flight eval's id (nREPL lets a client target a specific pending eval; with one
  eval per session in flight this is just a sanity check) and calls `clj_coro_cancel`. The eval loop, running
  *inside* that same coroutine, cannot call `clj_coro_cancelled(coro)` on itself: nothing publishes its own
  `clj_value` handle to it without a race (`clj_coro_spawn` can start running the body on another carrier
  before the spawning thread has anywhere to store the handle it returns — an unsynchronized write raced
  against a read, not a timing coincidence that happens to work). The original version compared the thrown
  message against a hand-copied `"Coroutines cancelled"` literal, which does not follow `CLJ_CANCELLED_MESSAGE`
  (eval.h) if that ever changes. Added `clj_coro_current_cancelled(void)` (`coro.h`/`sched.c`, next to the
  existing `clj_coro_cancelled(clj_value)`, same one-line body against `clj_coro_current()` instead of a
  looked-up handle) so the eval loop asks its own execution directly — no message text involved at all, and
  the alternative (a box the spawning closure fills in after `clj_coro_spawn` returns) was rejected because it
  is exactly that race, not a matter of style. `(cancelled?)` (`cancelled?*`, builtins.c;
  `clojure.core.async/cancelled?`, core/async.clj) exposes the same function to Clojure code that wants to
  poll its own cancellation without waiting for the next park point.
- **Output is streamed, one `:out` per write, not one per form.** The hook is on the capture struct itself, as
  the earlier note predicted: `clj_output_push_stream` (runtime.c) pushes the same `with-out-str` capture with a
  callback on it, keeps nothing, and hands every write over as it comes; `Evaluator.run` pushes one stream for
  the whole eval and pops it in a `defer`, so each `println` reaches the client while the form still runs
  (`Tests outputArrivesBeforeTheFormReturns`: a form that prints, then parks 150 ms, sends its `:out` first).
  **The chunking policy is per write because a write already is a print call** — `print_line` (builtins.c)
  builds the whole string, newline included, and calls `clj_output` once — which lands on the same granularity
  the JVM reaches from `*flush-on-newline*` plus nREPL's 1024-byte `CallbackBufferedOutputStream`, with no
  partial line stranded. Rejected: per N bytes (strands the tail of a progress line until the next write, the
  same bug one buffer later), on a park (needs a park hook that does not exist, and a printing compute loop that
  never parks stays silent), on a timer (a timer per eval, and reordering against `:value`). The callback runs
  under the capture's own `clj_lock`, so two carriers sharing the capture cannot interleave one message's bytes;
  it may not park, which a C function pointer into Swift cannot anyway. A nested `with-out-str` pushes a plain
  capture on top and still keeps its own output. The stream's context is refcounted with the capture, so a child
  coroutine holding it after the form returns writes to the client rather than into a buffer nobody reads. The
  two guarantees the old code recorded hold unchanged: the pop is a `defer` on a push that cannot fail, and a
  cancelled eval still drops its frame.
- [~] **Headless: no main carrier is ever installed.** A run loop has nothing to pump in a bare server process, so
  `Server` never calls `clj_sched_main_install`; `go-main` code evaluated over nREPL gets the
  existing "No main carrier" error immediately, the same as any other headless entry point, rather than hanging
  or silently running on the pool. Trigger: a `--install-main-carrier` flag that adopts the process's own
  thread and pumps `CFRunLoopRunInMode` on a loop, for code that legitimately wants `:main` from the REPL (a UI
  test harness, say) — not needed for CIDER/Calva/Conjure, which never send such code.
- **Frame persistence, not `Runtime.eval`.** `Runtime.eval` deliberately resets `*ns*` after each call (like
  `load`, NOTES on `Runtime.swift`), which is wrong for a REPL: `(ns foo)` on one eval must affect the next.
  `Evaluator.run` pushes the session's frame itself and never pops it — the coroutine finishing releases the
  whole binding chain regardless (NOTES "Coroutines": "a var's `thread_bound` count drops when the frame dies
  rather than when it is popped") — and (see above) captures it back into `Session` when the eval was not
  cancelled, so the next eval (a new coroutine) starts where this one left off. `ns`'s own `overrideNamespace`
  parameter (the `eval` op's optional `"ns"` field) replaces just the `*ns*` entry of the frame it pushes,
  leaving the rest — `*1 *2 *3 *e` — intact.
- **`*1`, `*2`, `*3` and `*e` are core.clj vars, not nREPL-private state**, matching real Clojure: they are
  defined in `boot/core.clj` next to `*print-length*`/`*print-level*` (`(def ^:dynamic *1 nil)` etc.), because
  the language ships them and a host REPL sets them, the same division as those two vars already had. Seeded
  into `ReplVars.defaultFrame` at nil and shifted by `ReplVars.recordValue`/`recordError` after each form
  (`*3 ← *2 ← *1 ← value`, or `*e ← thrown`), the JVM's own order. `make boot` regenerated the embedded and
  compiled core after the addition (`core_clj.inc`, `boot/core.c`); nothing outside `CljNREPL` reads them.
- **A message's bindings stay the message's: print options, `*file*`, `*in*`.** `*print-length*` and
  `*print-level*` are seeded into `ReplVars.defaultFrame` at nil, so a session can `set!` or `binding` them at
  all (the JVM's `with-bindings` set carries them for the same reason), and a request's own limits are assoc'd
  over the frame for that eval, then put back the way the session had them when the frame is captured
  (`ReplVars.MessageBindings`, one mechanism for all three). Per request, not sticky, because that is nREPL's
  own scoping — the options ride one message and the next message without them prints unlimited again. Where
  they ride was read off the protocol, not guessed: nREPL carries them in the
  `nrepl.middleware.print/options` **map** (nrepl.org's op reference; `nrepl/util/print.clj` maps that map's
  `:print-length` onto `*print-length*` by adding the stars), and the eval op has no top-level `print-length`
  field at all; cider-nrepl's pprint fns spell the same two limits `:length`/`:level`, so both spellings are
  read. A session's frame binds `*repl*` true, as `clojure.main/with-bindings` does under every JVM REPL, and a
  `load-file` binds `*source-path*` to the file's last segment through load.c. The value in the reply goes through `clj_pr_str_dynamic`, so a client that asks for a limit gets the
  elided value too, which is the whole point of the JVM's print middleware. `load-file` binds `*file*` from
  `file-path` (nREPL's source-path-relative name, the one the JVM's `load-file` passes `Compiler/load` as the
  source path), falling back to `file-name`; positions in the loaded code and `*file*` reads inside it are right
  for the load's extent and nil again afterwards. `*in*` is built per eval because the `need-input` fn it
  carries names that eval's id — keeping it out of the captured frame also breaks the cycle it would otherwise
  make (session → frame → fn → session).
- **`clj_map_assoc` consumes its map** (map.h: "map is consumed (+1 in) and may be updated in place when
  unique"), and every Swift `Value` copy shares one release box, so `Value(owning: clj_map_assoc(frame.raw, …))`
  on a frame the `Session` still holds was a double free: unique in place returned the same pointer, then two
  boxes released it. It showed as "retain of a freed object" on the first eval that bound anything; the same line
  had been there for the `eval` op's `ns` field, where no test sent one. Frame assoc/dissoc now retains first
  (`clj_map_assoc(clj_retain(m.raw), …)`, the idiom `MapTests` uses).
- **`clone` copies the parent's frame, not a fresh default.** `Session(frame:)` takes the parent session's
  `currentFrame` value directly — free, since it is a persistent map and the clone's later `set!`s build new
  versions rather than mutating the parent's (`Tests sessionCarriesItsWholeBindingFrame`: a clone that changes
  its own `*1` leaves the parent's untouched). A `clone` naming an unknown session, or none, gets
  `ReplVars.defaultFrame(namespace: "user")`, the nREPL server's own root state, not the JVM's `user` var
  bindings (there are none to inherit outside a session).
- **`complete`/`info` are Clojure, not a second namespace walker in Swift.** `ns-map`, `ns-publics`, `ns-aliases`,
  `ns-resolve`, `meta` and `resolve` are already C builtins with the exact resolution rules (aliases, refers,
  privacy) a REPL needs; `NReplHelpers` loads a small `pippin.nrepl.util` namespace once and calls its two
  functions as ordinary `Value.apply`s. The op shapes follow the older, pre-`cider-nrepl` `complete`/`info`
  convention (`{:candidate :ns}`, `{:ns :name :doc :arglists-str :file :line :column :macro}`) that Conjure and
  plain `nrepl.el` still speak natively; CIDER's own richer `completions`/`eldoc` ops are `cider-nrepl`
  middleware this runtime does not implement, so CIDER's completion is plainer than in JVM Clojure, but eval,
  interrupt and the rest of the session protocol are unaffected — CIDER connects and evaluates.
- [~] **`*in*` is a map, `read-line` is Clojure, and the wait is a channel take.** `stdin` feeds a per-session
  channel of whole lines (`Session.acceptInput`); `*in*` (core.clj, next to the print family) holds
  `{:lines <channel> :request <fn>}`, and `read-line` polls the channel, calling `:request` and parking on
  `chan-take*` only when it is empty. The two-part value is what the protocol forces: nREPL wants a `need-input`
  status on the eval's own id *before* the reader waits, and the wait must be a park — a Swift blocking wait
  would be a park under `host_depth` (an error, design §5), and parking in C would put nREPL's line protocol in
  the core. So the signal is a native fn the eval's own coroutine calls (a native fn called from Clojure does not
  raise `host_depth`; only `clj_host_invoke` does) and the wait is an ordinary take, which cancellation already
  reaches: an `interrupt` of a form parked in `read-line` ends `interrupted` like any other park. Lines, not
  bytes: `put!` from the connection's reader thread never parks, a trailing partial line waits in
  `Session.partialLine` for its newline, and nREPL's EOF (an empty `stdin` payload — `session.clj`'s `addEof`)
  flushes it and closes the channel, so `read-line` answers nil at end of input as on the JVM. `read-line` is
  named in `facts.c` `park_names` rather than `io_names` (which subsumes it: park is `ANY | PARK`), because its
  body is gone under `-DCLJ_COMPILED_CORE` and the `:park` fact must not depend on how the core was built. A
  session whose connection dies closes its channel (`Server.removeSession` → `Session.endInput`), so a parked
  reader unparks with nil instead of holding a coroutine for good. Deviation: a client answering `need-input`
  without a trailing newline is not asked again, where the JVM's char-level reader would ask; every real client
  sends the newline. Trigger: such a client — `acceptInput` then has to re-send the request when a chunk
  completed no line. `make api-diff` lists `*in*` under "dynamic mismatches" and stays that way: the JVM's own
  meta says `:dynamic false` because `RT` sets the flag on the var rather than through metadata, while ours is
  `^:dynamic`, which is what `binding` needs (`with-in-str` binds it on the JVM too).
- [ ] **`readLineParksUntilStdinArrives` asked for input three times twice.** Seen on a standalone
  `make test-compiled` and once on CI, in the `test` gate's shard 1 (run 37318497402, arm64), never
  reproduced on asking: the test feeds `"one\ntwo\n"` to
  `[(read-line) (read-line) (read-line)]` and expects two `need-input` requests, because the second line
  comes out of the first chunk. A third request means the second `read-line` polled an empty channel.
  `Session.acceptInput` puts a chunk's lines one `put!` at a time, and the first `put!` unparks the waiter
  at once, so a second read that polls between the two puts asks again — a hypothesis that fits the design
  above, not a confirmed trace. The second sighting was the trigger, so the fix is due: a chunk's lines go in
  as one batch before any waiter runs, which `clj_chan_put_cb` cannot do — `flush_wakes` unparks the taker
  inside the first put — so it needs a batched put in chan.c, and the test then checks that rather than timing.
- **One `Runtime` per process, shared by every session** — the same sharing JVM nREPL gets from one JVM: a
  `def` from one editor buffer's session is visible from another's, deliberately.
- **The bencode codec and the socket layer are a separate library target, `CljNREPL`**, not folded into the
  `clj-nrepl` executable's sources: `Tests/PippinTests/NReplTests.swift` depends on it directly and drives a
  real `Server` over a real loopback socket (its own `TCPConnection`/`Bencode`, not a mock), matching how
  `CljCompiler` already sits between `clj-compile` and the test suite.
- [~] **Pointing a real editor at it**: build `clj-nrepl` (`swift build --product clj-nrepl` or via `make`, once
  the target is wired into a Makefile recipe — none exists yet, run the binary directly from `.build/debug/` or
  `.build/release/`), run it from the directory a `deps.edn`/project root would occupy. It writes `.nrepl-port`
  there (`--no-port-file` to skip, `--port-file PATH` to redirect, `--port N` to fix the port, `--bind HOST` for
  other than loopback) and prints `nREPL server started on port N on host 127.0.0.1 - nrepl://127.0.0.1:N`, the
  exact line CIDER (`cider-connect`), Calva ("Connect to a running REPL server") and Conjure
  (`:ConjureConnect 127.0.0.1 N` or auto-detected from `.nrepl-port`) all look for. `(require 'clojure.string)`
  or any embedded lib works out of the box; a third-party dependency needs `Runtime.loadPath` wiring this
  executable does not expose yet (trigger: a `--load-path DIR` flag mirroring `clj-compile`'s).
- [ ] Not done, with triggers: TLS/`nrepl.el` `x-clojure-refresh`-style middleware extension points
  (trigger: a client that needs one); a `Makefile` target for `clj-nrepl` alongside `clj-compile`'s (trigger:
  someone other than an editor plugin wanting a one-line launch).

