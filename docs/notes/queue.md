## Queue (Sources/CljCore/queue.c)

- **Clojure's shape: a front seq and a rear vector**, with `count` stored. `conj` grows the rear in place when
  the queue and the vector are unique (a shared owner shares the new child), `pop` is the front's `next`, and
  when that runs out the rear becomes the front through `seq` (a vector-seq view, so the old vector lives on
  behind it) and the rear starts over. A single vector with an offset would keep every popped element until
  the queue emptied; this drops them as they go. Invariant: `count > 0` means the front is non-empty, so
  `peek` and `pop` never read the rear, and the empty singleton pops to itself as on the JVM.
- **`seq` is eager**: the front's items followed by the rear's as one list (`queue_seq`), which `=`, `hash`
  and printing walk; `first` is the front's first and `reduce` walks front then rear without the copy.
  Trigger for a lazy view: a large queue seq'd in a profile.
- **The JVM's spelling resolves**: `clojure.lang.PersistentQueue` and `PersistentQueue` name the type in core,
  and a namespace `clojure.lang.PersistentQueue` holds the var `EMPTY`, so `clojure.lang.PersistentQueue/EMPTY`
  reads as any `ns/var` does (medley's `queue`). It shows in `all-ns`. `peek`/`pop` in core.clj branch on
  `(instance? PersistentQueue coll)` ahead of `list?`, which is true for a queue as it is on the JVM
  (`IPersistentList`); `pop` reaches `queue-pop*`.

