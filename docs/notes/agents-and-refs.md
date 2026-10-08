## Agents and refs (boot/core.clj, chan.c's `spawn-detached*`; design §4 «Агенты и ref'ы»)

- **An agent is a `deftype` over four atoms**: the state, the queue `{:q actions :error e}` whose head is the
  running action, the config (validator, watches, error handler and mode) and the meta. An action is
  `[f args executor bindings]`; `agent-enqueue` conjoins it and executes it when the queue was empty and the
  agent not failed, and the run (`agent-run`) pops itself when it ends and executes the next on that action's
  own executor, as `Agent.doRun` does. The pop is `shielded*`: an action cancelled while parked still leaves
  the queue, or every later send would stall behind it. Every expectation of `AgentTests` is what JVM Clojure
  1.12.6 answers for the same forms, the two watch calls of one `send` plus `await` included (the counting action
  of `await` is an action too).
- **`spawn-detached*` runs an action with no binding frame and no deadline of the spawner's**
  (`clj_coro_spawn_detached`, `chan_thread`'s `detached`): the action pushes its sender's bindings, captured by
  `get-thread-bindings` at the send, on an empty chain. Pushed on the runner's own frame instead, an action queued
  behind another would see the first sender's bindings wherever its own sender bound nothing
  (`AgentTests.anActionRunsUnderItsSendersBindings`), and an action spawned under `with-deadline` would be
  cancelled by a deadline that is not the agent's. The output capture is still conveyed: the JVM conveys `*out*`
  with the bindings, and a capture is not a var here.
- **The sends an action makes are held in `*held-sends*`, `[owner volatile]`**, and held only while the running
  coroutine is the owner (`coro-current*`): the binding is conveyed to a `future` spawned inside the action, whose
  sends must go out at once as on the JVM, where the hold is thread-local. A failed action's held sends are dropped
  and the error handler runs with the hold unbound, so its own sends go out — `Agent.doRun`'s `nested.set(null)`.
- **`seque` is the JVM's text over a channel**: `chan-offer*` for `BlockingQueue.offer`, `chan-take*` for `take`,
  two `volatile!` cells as the nil and end sentinels a channel needs, and an error that could not be offered kept as
  `{::seque-error e}` in the agent's state for the next fill to report. `n-or-q` is a size or a channel.
- [ ] **Sends are not checked against a shut-down pool**: `shutdown-agents` is a no-op (docs/jvm-differences.md),
  so nothing refuses a send after it. Trigger: a host that must stop agent work at exit — then the runtime needs a
  shutdown of its own, for every spawn and not for agents alone.
- **`send` and `send-off` dispatch through two private atoms** that `set-agent-send-executor!` and
  `set-agent-send-off-executor!` reset, holding `send-via`'s kind of executor; `await` sends, so it goes through
  the replaced one too, as on the JVM. A non-fn is refused at the set, where the JVM refuses it at the field's type.
- **A ref is a `deftype` of an id and three atoms** (the committed value, the config, the meta). The id comes
  from a counter and orders the locks; the state atom is the monitor a transaction holds the ref by, not the ref
  itself, so a user's `locking` on a ref does not meet a transaction. A transaction is a map of volatiles in
  `*tx*` with its owner coroutine: `locked` (a set), `top` (the highest id held), `vals`, `commuted`, `wanted`
  (the ref a try refused), `sends` and `doomed`. `tx-lock` waits (`monitor-enter*`) above `top` and only tries
  (`monitor-try-enter*`, the cmutex's `trylock` behind the reentrant monitor) at or below it; a refusal marks the
  transaction doomed and throws `{::retry tx}`, and `run-in-transaction*` retries with every ref touched so far
  taken in id order before the body runs again. Every `tx-lock` of a doomed transaction throws again, and a body
  that ends doomed retries, so swallowing the signal changes nothing. The commit validates every written value,
  then writes them all under `shielded*`; the unlock is shielded too. Watches and the held agent sends run after
  the unlock, as `LockingTransaction.run`'s `finally` does. Not benchmarked; 8 futures × 200 transactions
  over two refs taken in opposite orders converge (`RefTests.transactionsAreAtomicUnderContention`).
- [ ] **A lock waited for in id order has no timeout**, so a transaction that waits for a second one it started
  itself on the same refs never ends (docs/jvm-differences.md). Trigger: a program that needs it; the fix is a
  timed park in `monitor-enter*` whose expiry retries, as the JVM's `LOCK_WAIT_MSECS` does.
