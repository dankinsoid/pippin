## Sorted (Sources/CljCore/sorted.c)

- **A left-leaning red-black tree, not a B-tree.** Sedgewick's LLRB is the smallest balanced tree that
  still has a deletion: red links lean left, so insert and delete have one rebalance case where the
  classic algorithm has two, and `fix_up` is three lines shared by both. Path copying is one node per
  level (48 bytes, one pool size class) and depth is at most 2·log₂(n+1); a B-tree with 16–32 slots per
  node would be shallower but copy the whole node on every `assoc` and share less of the old version,
  which is the operation this runtime optimizes for. The tree is checked by
  `clj_debug_sorted_valid` (no right-leaning red link, no two reds in a row, equal black height, keys in
  order, count matches) after every step of a randomized insert/delete run in SortedTests.
- **Sorted map and sorted set are two descriptors over one wrapper and one tree** (`clj_sorted`): the set
  stores its element as the key and leaves the value nil, so `clj_sorted_assoc`/`dissoc`/`conj` serve both
  and only the seq, reduce and print sites branch on which type it is. The bits are the hash map's and the
  hash set's (`CLJ_CORE_MAP`/`CLJ_CORE_SET`, `IPersistentMap`/`IPersistentSet`), so `assoc dissoc get
  contains? conj disj count seq reduce reduce-kv keys vals into empty` and the consuming intrinsics reach
  them unchanged. `clj_type` grew two slots for that: `assoc` and `dissoc`, consuming self like `conj`; a
  set's `dissoc` slot is its `disj`, since the operation is the same.
- **A unique collection is edited in place, as map.c and set.c do.** `node_own` copies only when
  `clj_is_unique` is false, the wrapper hands its own root reference down, and a child is taken out of
  its slot (`take_child`) before the recursive call, so a comparator that throws mid-descent leaves
  nothing dangling. Rotations own both nodes they touch, so a rotation over a shared child copies it.
- [~] **A nil comparator means `clj_compare`** (compare.h), which is what `sorted-map`/`sorted-set` build with,
  so the default collection compares in C: a `get` is 3–8× the hash map's and an `assoc` 2–4×
  (bench/RESULTS.md), where passing `clojure.core/compare` as the fn made every tree level a Swift↔C
  transition of ~70 ns and the ratios 10–83×. `sorted-map-by`/`sorted-set-by` take a fn comparator and go
  through `clj_call_invoke` per comparison; it may answer a number or, as a predicate, logical true when its
  first argument sorts first, the way `AFunction.compare` reads a fn comparator. `empty`, `assoc`, `dissoc`
  and `with-meta` carry the comparator over, and `(sorted-map)` is therefore still not a singleton. Trigger
  for a cheaper fn comparator: a `sorted-map-by` in a profile.
- [ ] **A key nothing compares is accepted**: `(sorted-map () 1)` answers `{() 1}` and `(sorted-set ())`
  answers `#{()}`, because the insert into an empty tree compares nothing, where Clojure's
  `PersistentTreeMap` refuses the key ("Default comparator requires nil, Number, or Comparable"). The
  comparator itself is right — `(compare () 1)` throws here — so the missing step is validating the key of
  the one-element case. Trigger fired: `clojure.test-clojure.data-structures/test-sorted-map-keys` and
  `test-sorted-set` (docs/jvm-differences.md, a **Fix** row).
- [ ] **`dissoc` walks the tree twice**: `node_find` first, because the LLRB deletion is only correct for a
  key that is present and because the count must not move when it is not. Trigger: a delete-heavy profile.
- **Equality and hash cross representations**: `(= (sorted-map :a 1) {:a 1})` and the reverse are true and
  the hashes match, so a sorted map and a hash map of the same content are the same key in a third map.
  `map_equals`/`set_equals` now test the `CLJ_CORE_MAP`/`CLJ_CORE_SET` bit instead of the concrete type and
  look a foreign representation's entries up through `clj_equals_lookup`, which drops a comparator's
  exception — `clj_equals` cannot throw.
- [ ] **`seq` is an eager list** as the hash map's and hash set's are, so `first` on a big sorted map builds
  the whole list; `subseq`/`rsubseq` go through `sorted-seq-from*`, which prunes the subtrees outside the
  bound and is O(log n + k), and then `take-while` in core.clj as Clojure does it. `rseq` walks the tree
  in reverse. Trigger for an O(1) view: a `first`/`next` walk of a sorted map in a profile; then a stack
  of parents per seq object.
- [ ] **No sorted collection reaches the tree codec** (node_data.c): there is no literal for one, so it can
  never be a constant in an analyzed tree. Trigger: a compiler that wants to emit one.

