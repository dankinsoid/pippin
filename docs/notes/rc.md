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
- **Reference types are checked at the store, not below a cutoff** (`clj_type.mutable_children`, `clj_slot_store`).
  A coroutine, an atom, a volatile, a channel, a var, a namespace, a lazy seq and an array replace their
  children after publication under their own lock or owner, and release the old ones there. The cutoff walk
  read those slots without that lock: `finish` cleared and released `c->fn` on a carrier while a spawn on the
  test thread walked a channel → coroutine → fn edge, an ASan heap-use-after-free in `shared_below`
  (`AsyncLibTests.goScoped`, arm64 `make test`, run 36921714691).
  `ChanStressTests.theShareCheckStopsAtAFinishingCoroutine` checks every cutoff of its thread
  (`clj_debug_share_check_every`) over a chain of 20k go blocks, each capturing the last one's channel, and meets
  that use-after-free under ASan on x86_64 when the walk enters a coroutine. The walk reads a flagged node's own
  flags and does not descend below it; every store into one of its slots goes through `clj_slot_store` (or its
  atomic forms), which shares the value when the owner is shared and then checks, in debug builds
  (`clj_debug_slot_store_check`), that a shared owner got a shared value and that the running execution holds the
  owner's lock where `clj_type.debug_lock_held` answers cheaply: an atom's cmutex owner, a channel's section (its
  plain lock's holder is tracked in debug builds, `lock_owner`). A count of 1 skips the lock check: that is the
  creator filling the object before anyone else can reach it. A namespace is immortal, so no walk ever entered it;
  it is born shared, so its stores publish like any other. Every other slot is fixed at publication
  or replaced only in place under `clj_is_unique`, which no other holder can see; a deftype descriptor's
  protocol tables are read inside a reader window and freed after it closes; the exception's `trace`, the one
  write-once slot, is a release CAS read with acquire. The check at free (1) is not affected: at a count of
  zero no writer holds the object. `clj_debug_all_shared` still descends through every slot: its callers are
  tests on values no other thread writes.
- **Slots: an edge of a heap object is written through a primitive, enforced by type** (`object.h`,
  `scripts/slot-audit.py`; design §4 «Запись в слот»). A field or slot-array element `each_child` visits is a
  `clj_slot` (`clj_atomic_slot` where it is read and replaced with atomics), so `obj->f = v` does not compile and
  every write is `clj_slot_store` (publishes `v` when the owner is shared), `clj_slot_init` (a fresh object, no flag
  test; debug builds check that an owner born shared gets only shared values), `clj_slot_clear` or the atomic
  forms; `clj_root_store` is the C-global form and publishes always. A value an object keeps for its own execution,
  which `each_child` does not visit (a coroutine's pending exception and retired roots), is a `clj_private_value`.
  An atom, a channel, a coroutine, a var and a namespace are born shared, so their stores publish without a
  `clj_share` of their own; a channel's put is published on every path in — the ring, a parked putter's node, a
  woken taker's node (`add_wake`). `make slot-audit` (in `make gates`) fails on a plain `clj_value` field of a
  heap struct and on a write to `.v`, or its address taken, outside `object.h`. Holes, all on the side of a value
  already published: a move between slots (`memmove` of a node, a struct copy of a slot) compiles, and the slot
  pointer of an untyped buffer (an object array's `data`, the trailing meta word) is a cast the audit cannot see.
  Swift reads slots through `.v` and is outside the audit; it never writes one. Release builds compile the
  primitives to the old code: in the A/B of bench/RESULTS.md («Slot primitive») every collection internal is
  byte-identical, and what differs is the intended change (born-shared stores test the flag).
- **The owner check: an unshared object is touched only by the execution that owns it** (debug builds;
  `object.h`, `rc.c`, `coro.c`). The owner is the execution — a `clj_coro`, a bare thread's implicit one
  included — not the thread: a coroutine that parks and resumes on another carrier owns what it owned (design
  §4, «Смена потока — не триггер»). Each `clj_coro` takes a 16-bit tag at creation (`debug_owner`), `clj_alloc`
  writes the running execution's tag into the top 16 bits of `flags` (`CLJ_OWNER_SHIFT`; release builds leave
  them 0, so the header is the same in both; design §4's BRC for the main thread, measured and not taken, would
  put its counter there), and the non-atomic paths compare it with the running execution's: `clj_retain`/`clj_release`
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
  BRC with the main thread as the one owner is implemented on branch `brc-main` and not taken: on arm64 the
  main tick costs 20–30 % more with it (bench/RESULTS.md, "Main-thread BRC"). Trigger: a profile of a real app
  where main contends with the pool on one state.

- **The cycle collector** (`cc.c`, `include/clj/cc.h`; design §7 «Сборщик циклов: как он устроен», which holds the
  why). Header bits `CLJ_FLAG_MUTABLE`, `CLJ_FLAG_REACH`, `CLJ_FLAG_REACH_LOCAL` (5–7); rc bits `CLJ_RC_WATCH`,
  `CLJ_RC_BUFFERED` (31, 30), the count in 30 bits. `REACH` is born with an atom, a channel, a volatile and an object
  array, and is OR'd into a non-`MUTABLE` owner by `clj_slot_init`/`clj_slot_store` (`clj_reach_from`); a `memcpy`
  clone copies it (`fn_with_meta`, `clj_view_with_meta`). A lazy seq is `MUTABLE` with the bits of its thunk only, a
  var is immortal and has none, an exec has none because its root is not a slot. The inline release takes the slow
  path for `REACH_LOCAL`; a nonzero release of a `REACH`/`REACH_LOCAL` object without `BUFFERED` files an entry
  (`clj_cc_shared_candidate`: the decrement and the bit in one CAS; the local one: a plain store, the entry in the
  running execution's `clj_coro.cc_local`). An entry holds no reference: a buffered object that reaches zero is torn
  down at once (`bury` sets it aside instead of making its header a link) and its cell stays a zombie
  (`CLJ_RC_ZOMBIE`, out of the live count by `clj_dealloc_dead`) until its entry gives it back (`clj_dealloc_cell`).
  The local collection runs in the owner — at 256 entries off the main carrier and only with no `clj_lock` or cmutex
  held, at finish (the epilogue borrows the finished coroutine's buffer, `clj_cc_local_borrow`; so does a blocking
  job for its parked caller), in `clj_cc_collect`, and on the main carrier at `kCFRunLoopBeforeWaiting` with a 1 ms
  budget in slices of 64 (`clj_cc_main_idle`); past 4096 the main carrier hands one entry per new candidate to the
  background by `clj_share`. The shared collection runs on a detached thread woken by the first entry and collecting
  500 ms later or at 1024 entries; `clj_cc_collect` runs one on the caller under the same mutex. While it runs a
  shared object reaching zero is deferred whole (`clj_cc_defer_free`) and freed by the collector at the end. A
  store into a shared `MUTABLE` owner calls `clj_cc_note_store` after the store and before the old value goes (a
  barrier, then the watch check; no barrier for a type read under its own lock, `cc_locked`: an atom's cmutex, a
  channel's section, whose entry also clears the watch since any section may move a value out). `clj_debug_cc_stats`
  counts collections, objects freed, candidates, hand-offs, nodes visited and roots put back after interference;
  `CLJ_CC=0` keeps the bits and files no entry, the control of a cost measurement. `runtimeSettled` collects first,
  so a test baseline is after collection. `scripts/tsan.supp` names `visit_lockfree`, the frame that reads a
  volatile's, an array's or a lazy seq's slots without their writer's lock.
- [ ] **What the collector does not see.** A type descriptor (the `users` registry holds it without a reference), an
  exec (the specializer's dependents index resurrects one through `retain_if_live`) and a coroutine (design §7,
  Phase 3) are never entered: their references count as external, so a cycle through one stays. A cycle through
  Swift or an ObjC object (a host box shows no children; a reify instance keeps its fns past `each_child`) is design
  §7's boundary. A lazy seq realized into a value that refers back to it through a global (`(def s (lazy-seq (cons
  1 s)))`, then a redefinition) is not collected: realization adds no reach bit, or every cell of a `map` pipeline
  would be a candidate. Trigger: a leak report naming one of these.
