## RC (Sources/CljCore/rc.c, object.h)
- **`-DCLJ_NO_REUSE` makes `clj_is_unique` always false** (`make test-noreuse`, `clj_reuse_enabled()` says
  which build this is), and every suite passes in it: the §7 invariant that nothing outside the RC entry
  points depends on the counter is now enforced, not just written down. Off, the flag costs nothing — one
  `#ifdef` arm. Four tests assert that an address survives an in-place step and are gated on
  `clj_reuse_enabled()`; the rest degrade to a copy with the same values, which is what the mode checks.
- **A type's `unlink` slot runs as the last reference drops, before the header becomes the worklist link**
  (`free_object` and `release_child`; `finalize` runs later, after the children). It is for a registry that
  holds objects *without* a reference: the specialize index of execs per var (`dependents`) is the one. The
  race it closes: a closure finishing on a carrier dropped its exec's last reference, `set_dead_next`
  overwrote the header, and a `def` on the main thread walking that var's dependents under the specialize
  lock retained the exec — `clj_retain` read a zero count and died ("retain of a freed object", a
  `make test-compiled` run of `EvacTests`). `exec_unlink` removes the exec under that lock while the count
  still reads, and `push_work` retains an exec only from a count above zero (`retain_if_live`): one that reads
  0 is on its way to `exec_unlink`, which waits for the lock the walk holds.
- [ ] **Live-object counter is one process-wide atomic** (debug only). Trigger: debug builds visibly slow
  under many threads. Fix: per-thread counters summed on read.
- [ ] **Copy path retains every child and then replaces one slot**: one spare retain/release pair per
  level. Trigger: profiling the "all versions kept" benchmark scenario.
- **"Children of a shared object are shared" is checked in debug builds** (`rc.c`). A violation is a shared
  parent over an unshared child, invisible from the child, so both checks sit where the edge is. (1) A shared
  object whose count reaches zero walks its children before its header becomes the worklist link
  (`assert_children_shared`, in `free_object` and `release_child`) and dies on one neither shared nor immortal:
  every edge of every shared object that dies is seen once, at the price of a second `each_child` pass over it.
  (2) Each cutoff of `clj_share` — a root already shared, and the `continue` on a shared node — is the one place
  the walk relies on the invariant, and checks below that node (`assert_shared_below`): one cutoff in 256 per
  thread, at most 256 children read and 64 descended into. `each_child` cannot stop early, so a check pays the
  width of every node it visits, again at every cutoff (`ChanStressTests.stressSpawn`, measured while the walk
  entered a channel's 100k-value buffer: 0.34 s unchecked, 40 s with every cutoff checked 64 nodes deep, 1.6 s
  at one in 64, 0.56 s at one in 256), and an unbounded walk would not end on a cycle through a ref type. The sample leaves (2) an early warning; (1) is the exhaustive one. `clj_debug_all_shared` is the same
  walk without bounds. Both this check and the owner check below compile out of release builds; together they
  cost the debug suite less than its run-to-run noise (`swift test` on the pool, 650 tests with the corpus, an
  Intel i9 under other load: 54.8 and 53.3 s against 70.6 and 54.2 s without them), stressSpawn above being the
  one test that shows them.
- **Reference types are checked at the store, not below a cutoff** (`clj_type.mutable_children`, `CLJ_SLOT_CHECK`).
  A coroutine, an atom, a volatile, a channel, a var, a namespace, a lazy seq and an array replace their
  children after publication under their own lock or owner, and release the old ones there. The cutoff walk
  read those slots without that lock: `finish` cleared and released `c->fn` on a carrier while a spawn on the
  test thread walked a channel → coroutine → fn edge, an ASan heap-use-after-free in `shared_below`
  (`AsyncLibTests.goScoped`, arm64 `make test`, run 36921714691).
  `ChanStressTests.theShareCheckStopsAtAFinishingCoroutine` checks every cutoff of its thread
  (`clj_debug_share_check_every`) over a chain of 20k go blocks, each capturing the last one's channel, and meets
  that use-after-free under ASan on x86_64 when the walk enters a coroutine. The walk reads a flagged node's own
  flags and does not descend below it; every store into one of its slots shares the value first and then
  checks, in debug builds, that a shared owner got a shared value — atom (`commit`, watches, validator, meta), volatile
  (`clj_volatile_reset`), channel (`buffer_add`, the parked putters, `coro`, `error`), coroutine (`fn`, args,
  `result`, `cancel_cause`), var (root, meta), lazy seq (`publish`), array (`clj_array_set`). A namespace is
  immortal, so no walk ever entered it, and its `store` shares unconditionally. The audit found one store before
  its share: `clj_coro_entry` wrote `c->result` and shared it after. Every other slot is fixed at publication
  or replaced only in place under `clj_is_unique`, which no other holder can see; a deftype descriptor's
  protocol tables are read inside a reader window and freed after it closes; the exception's `trace`, the one
  write-once slot, is a release CAS read with acquire. The check at free (1) is not affected: at a count of
  zero no writer holds the object. `clj_debug_all_shared` still descends through every slot: its callers are
  tests on values no other thread writes.
- **The owner check: an unshared object is touched only by the execution that owns it** (debug builds;
  `object.h`, `rc.c`, `coro.c`). The owner is the execution — a `clj_coro`, a bare thread's implicit one
  included — not the thread: a coroutine that parks and resumes on another carrier owns what it owned (design
  §4, «Смена потока — не триггер»). Each `clj_coro` takes a 16-bit tag at creation (`debug_owner`), `clj_alloc`
  writes the running execution's tag into the top 16 bits of `flags` (`CLJ_OWNER_SHIFT`; release builds leave
  them 0, so the header is the same in both; design §4 gives them to the main thread's counter of its BRC, and
  the tag moves out of the header when that lands), and the non-atomic paths compare it with the running execution's: `clj_retain`/`clj_release`
  inline, `release_reaches_zero` (a child released by a dying parent), `clj_is_unique`, and `clj_share` before
  it marks a node (the plain write of the flag must be the owner's). The comparison is an out-of-line call
  (`clj_debug_owner_check`), never a TLS address cached across a park. Gaps, both on the side of silence: tag 0
  is unowned and never checked — what a thread allocates before its first execution exists, the boot's ~31k
  objects against the 9M+ a suite run allocates; tags wrap after 65535 executions, and two executions sharing
  one hide each other's touches. Swift's view of the inline paths has no `CLJ_DEBUG`, so the Swift side is
  checked only inside the C calls it makes. An execution that works for another takes on that one's tag for the
  work (`clj_debug_owner_assume`), and these are the only transfers of unshared objects: a finished coroutine's
  epilogue on its carrier (`finish`: the binding frames it pushed and did not pop, its captures, its pending
  exception, its retired roots, `on_done`), and a blocking-pool job working for its parked caller
  (`blocking_main`). Everything else that crosses executions is shared before it does: spawn (fn, args, conveyed
  binding frames), a channel put and a `put!`/`take!` callback, `thread`'s fn and channel, a coroutine's result
  and thrown value (`clj_coro_entry`, so `clj_coro_result` is shared), a cancellation's cause, every `Value`
  (the Swift bridge: `callAsync`, async closures, nREPL session frames; a host fn runs on the execution that
  calls it). The reduce and fusion drivers never leave their execution. A registry that any execution reads
  under its lock is a publication (design §4) and shares what it holds: namespaces (as they did), the keyword
  table, the reify-type registry, the specializer's dependents index, the profiler's table and the loader's
  failures, and a compiled unit's constant pool (`clj_c_publish`). Four of those were bugs the check found, not
  just its noise: a reify site's type was retained and
  released outside the registry lock by every coroutine running the site
  (`CoroTests.aReifySiteRunsOnManyCoroutines`); a `def` re-deriving an exec retained and released the exec of a
  form still running on another thread (`SpecializeTests.aRedefReachesAFormRunningOnAnotherThread`, which rebinds
  the root to the fn it already holds: a fresh fn would free the old one under the running form's +0 read, the
  race NOTES "Analyzer and evaluator" accepts as "Concurrent `def`", and ASan met it on arm64);
  `clj_profile_stop` released fn nodes recorded by other executions (`ProfileTests.aFnProfiledOnAnotherThread`);
  every compiled unit's `K[]`, filled by the loading execution and retained by every one running the code, which
  `make test-compiled` met at its first core constant touched from a second test thread and arm64's `make test`
  at the objc fixture's `[1 2]`, retained by a reified method running on an `NSThread` the runtime had never seen
  (`CompilerFixtureTests.aUnitsConstantsAreReadByOtherExecutions`: a `go`, a `future`, a `thread` and such a
  callback, interpreted and compiled); each a non-atomic count
  touched from two threads. The keyword table and the loader's failures were only
  lock-ordered, never racy, and are shared for the check's model.
- [~] **Share of retain/release on shared objects: 79–83 % with the state in an atom** (bench/RESULTS.md,
  "Atoms"; `clj_debug_rc_ops` counts the plain, shared and immortal paths in debug builds, one relaxed
  atomic add per retain/release, the same process-wide-counter caveat as the live count above). The flag
  is monotone, so the first `reset!` puts the whole domain on the atomic path, ~80 pairs per state tick,
  on the order of 300 ns — the same order as one `swap! assoc` (288–904 ns at 16–100000 keys, the path
  copy of the "Atoms" entry under Builtins). General BRC is not taken: the copy path is dominated by the
  node copies, not by their retains (measured while the hand-over existed: 140 in place against 765 copied).
  Not done: design §4 «Представление значений» takes BRC with the main thread as the one owner, since reads
  of UI state on main are where the atomic pairs are the whole cost; the rc-share run on main is its
  before/after.

