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
- [ ] **The "children of a shared object are shared" invariant is unchecked in debug builds.** A violation
  is a shared parent over an unshared child: the child looks like any unshared object, so the check
  belongs where the edge is visible. (1) In `free_object`'s child walk, a shared parent asserts every
  pointer child is shared or immortal — one flag read on a header already loaded. (2) At the cutoff in
  `clj_share` (`continue` on an already-shared node), `clj_debug_all_shared` of that subtree: the one
  place the walk relies on the invariant. Trigger: the next code that stores into an object in place
  (transients, reuse) or the first spawn primitive.
- [ ] **No owner check on the non-atomic path.** "An unshared object is touched only by its allocating
  thread" holds literally today; a debug-only allocating-thread id (side table or debug header
  extension) asserted in the inline retain/release catches the actual cross-thread race regardless of
  how the invariant broke. Handoffs (park/resume, a channel move) will need an explicit
  `clj_debug_reown` at each transfer point, which documents them. Trigger: the first spawn primitive.
- [~] **Share of retain/release on shared objects: 79–83 % with the state in an atom** (bench/RESULTS.md,
  "Atoms"; `clj_debug_rc_ops` counts the plain, shared and immortal paths in debug builds, one relaxed
  atomic add per retain/release, the same process-wide-counter caveat as the live count above). The flag
  is monotone, so the first `reset!` puts the whole domain on the atomic path, ~80 pairs per state tick,
  on the order of 300 ns — the same order as one `swap! assoc` (288–904 ns at 16–100000 keys, the path
  copy of the "Atoms" entry under Builtins). BRC is not taken: the copy path is dominated by the node
  copies, not by their retains (measured while the hand-over existed: 140 in place against 765 copied).
  Trigger: a profile of a real app-state loop where the atomic pairs show next to the interpreter's
  per-node cost.

