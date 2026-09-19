# Engineering notes

Known simplifications in the runtime, each with the event that makes it worth fixing.
Delete an entry when it is done. Architecture-level decisions live in docs/design.md.

## Allocator (Sources/CljCore/alloc.c)

- **Abandoned slabs.** A thread's slabs are never reclaimed after it exits; freed cells in them are
  lost. Trigger: first code that creates short-lived threads (core.async, GCD workers). Fix as mimalloc:
  pthread_key destructor moves the heap's slabs to a global abandoned list; heaps take from it before
  mapping a new slab.
- **Empty slabs are never returned to the OS.** Peak memory stays resident. Trigger: first run on a
  device (jetsam). Fix: `madvise(MADV_FREE)`/`munmap` when empty slabs per class exceed a threshold,
  plus a memory-warning hook.
- **Foreign free list drains only when the local list is empty.** With a producer thread allocating
  from bump cells and a consumer freeing, cells pile up unused until the slab is exhausted. Not a leak,
  a delay. Trigger: multi-threaded benchmark showing extra resident memory in producer/consumer runs.
- **Linear search for a slab with room** when the current one is full. Trigger: a profile showing it;
  unlikely.

## Type descriptor (object.h, coll.c, seq.c)

- **Builtin descriptors stay `const`; their protocol tables live in a side table** (proto.c) keyed by
  descriptor pointer, while a `deftype`/`reify` descriptor owns its table in `user_protos`. Both are
  immutable snapshots: `extend` builds the next one under the protocol mutex, publishes it with a
  seq_cst store, bumps the definition epoch (`clj_epoch()`) and frees the old one after every reader's
  dispatch window (a per-thread flag, Dekker-ordered with the publish) has closed. Per-thread reader
  slots are never freed; a retired snapshot's impls are released, so a redefinition leaks nothing. The core-interface
  slots of every type are write-once: a builtin's are static, a `deftype`/`reify` fills its own at
  creation from the interfaces its form names (next item); `(extend-type String ISeq ...)` and
  `(extend-type MyType ISeq ...)` are refused alike.
- **`deftype`/`reify` implement core interfaces through slot trampolines.** `deftype*` takes every
  `interface method-map` pair of the form: a core interface fills the descriptor's slots with C
  trampolines into the method fns (`core_fns` on `clj_user_type`, visited by `type_each_child`) and
  ORs its bits into `core_bits`; a protocol goes through `extend`. The canonical table (JVM method
  names, so Clojure code reads as is):

  | interface | methods (`this` first) | slot | bits |
  |---|---|---|---|
  | `Seqable` | `seq` | seq | SEQABLE |
  | `ISeq` | `seq first next more`/`rest` `cons count equiv` | seq first next rest conj count equals | SEQ, SEQABLE, COLL |
  | `Sequential` | none (marker) | equals/hash default to the ASeq trait | SEQUENTIAL |
  | `IPersistentCollection` | `seq cons count equiv` | seq conj count equals | COLL, SEQABLE |
  | `Counted` | `count` | count | COUNTED |
  | `ILookup` | `valAt` (2 and 3 args) | lookup | LOOKUP |
  | `IFn` | `invoke` (any arities) | invoke | FN |
  | `IHashEq` | `hasheq` | hash | HASHEQ |
  | `IEquiv` | `equiv` | equals | EQUIV |
  | `IExceptionInfo` | `ex-message ex-data ex-cause` (`getMessage getData getCause`) | ex_message ex_data ex_cause | ERROR |
  | `IMeta` | `meta` | meta | META |
  | `IObj` | `meta withMeta` | meta with_meta | OBJ, META |
  | `IReduceInit` | `reduce` (`[this f init]`) | reduce | REDUCE |

  `count` under `ISeq` fills the slot without the Counted bit; `equiv`/`hasheq` given anywhere set
  EQUIV/HASHEQ (bits only user types carry: `(satisfies? IHashEq [1])` is false). A declared
  interface keeps its slot even without the method, and that slot throws "No implementation of
  method" when reached — the JVM refuses the form at compile time; ex-* default to nil as
  `Throwable.getMessage` does; `next` missing but `more` given derives next as `(seq (more x))`.
  `(get x k)` reaches a `valAt` that has only the 2-arity, a not-found needs the 3-arity (a `reify`
  trampoline accepts any arity, so its `valAt` always gets 3 args). The trampolines type-check what
  comes back (`seq`/`next` a seq or nil, `more` a seq, `count` a non-negative integer, `meta` a map
  or nil, ex-* their field types) and throw otherwise; `withMeta` may return anything. Limitations:
  `empty` and `applyTo` have no slot and are refused by name; `Associative`, `Indexed`,
  `IPersistentMap/Vector/List` cannot be implemented (the `assoc`/`dissoc` slots sorted.c added are
  C-only, there is no `nth` slot, and no trampoline names them — a record fills the map slots from C
  instead, see "Records"; trigger: the first user vector type); `Object` methods
  (`equals`/`hashCode`/`toString`) are not accepted (use `IEquiv`/`IHashEq`; no print slot); an
  arity error inside a method says `fn` and counts `this`, except `IFn`'s, which the trampoline
  checks first and reports with the type name; no chunked seqs, so every element of a user seq
  walked by `map`/`filter` costs two Clojure calls (`first`, `next`) and usually an instance
  allocation, and `count` without `Counted` walks it; `reduce` over a user type goes through its
  `IReduceInit` slot when it declares one (the trampoline seeds the 2-arity with `(f)`, as
  `CollReduce`'s extension to `IReduceInit` does on the JVM) and otherwise through `clj_seq_iter`.
- **Protocol dispatch is cached per call site** (eval.c, `proto_ic`; bench/RESULTS.md, "Call-site
  caches"): an INVOKE node whose head evaluates to a method fn keeps, in its exec's side array, the
  method's serial (a counter on `clj_method_ctx`, never reused), the definition epoch of the fill and
  up to four `{receiver type, impl, arity}` entries; past four the oldest is replaced. The impls are
  *borrowed* from the tables, not retained: a retained impl would pin whatever the last call reached
  (a user type through `map`'s site in core.clj, which never dies) and cycle through a recursive
  method (impl → exec → site → impl). What makes borrowing safe is that only an epoch bump retires a
  table — `extend`, and now a dying `deftype` descriptor (`type_finalize` bumps) — and a hit checks the
  epoch inside a dispatch window before retaining the impl for the call, so a concurrent `extend`
  either bumped first (miss) or waits for the window. A miss dispatches as before (`impl_of` under its
  window) and records the result only when the epoch read before the lookup still holds. Several
  threads run one exec, so a fill takes a seqlock (`seq` odd while writing; a losing filler gives up,
  a reader that sees a change takes the generic path). The method's declared arity is checked before
  the cache, so the error still names the method. Cost of a hit: the serial and type loads, the
  seqlock reads, two seq_cst stores for the window, the epoch load, an atomic retain/release pair on
  the shared impl, then the closure entry; ~9 ns less than the table walk. Not cached: a method
  called through `apply` or from a native (no INVOKE node), a nil impl (the error path). Trigger for
  a megamorphic cutoff: a site cycling through more than four receivers in a profile. A compiled site has
  its own cells — direct arms from the receiver fact and a per-thread cache ("Compiler", protocol calls).
- **No `.-field` access, no protocol inheritance.** A deftype's fields are positional
  slots read through `field*`, visible as locals inside its own method bodies only; from outside
  there is no accessor. A deftype carries meta only by implementing `IObj` itself (a field for it);
  a record has the slot (see "Records").
  A protocol cannot extend another. `extend-type` on a core interface as the *type*
  (`(extend-type ISeq P ...)`) covers every type with those bits, on the concrete type missing; a
  user protocol cannot be a type designator. Trigger: the first `(.-x o)`.
- **`reify` expands to data and var references only**: `(new* (reify-type* 'reify__N '[m ...] P {:m 0}
  ...) closures...)`. `reify-type*` makes the type on the site's first evaluation and keeps it in a
  process-wide registry under the gensym'd name (its own mutex, taken before the protocol one), so a
  later evaluation is a lookup and a tree that went through `to_data`/`from_data` reaches the same
  type; the method closures are made per evaluation, the instance's fields hold them and the type's
  slots and tables hold trampolines into those fields. A site's type is immortal like a var: a test
  that counts live objects runs every site once before its baseline. The registry key is only
  unique within one process; a loaded tree from elsewhere that reuses a name with another field count
  is refused ("already exists with a different shape"), the same count with other protocols is not
  detected. Trigger: a tree cache on disk / AOT; then a key from the defining namespace and a site
  hash. The hit path still evaluates the protocol var references (a retain/release each) and holds
  the mutex. `deftype` keeps calling `deftype*` at run time under its `def`. `deftype` methods may
  shadow a field with a param, as in Clojure; fields a body names are bound at the top of that body
  (one `field*` call each), whether or not the reference is under a `quote`.
- **Builtin type names are vars in clojure.core** (`String`, `Long`/`Integer`, `Double`, `Boolean`,
  `Character`, `Keyword`, `Symbol`, `PersistentVector`, `PersistentHashMap`, `PersistentHashSet`, `PersistentList`, `Cons`,
  `EmptyList`, `LazySeq`, `Range`, `Fn`, `Var`, `Namespace`, `ExceptionInfo`, `HostError`,
  `Protocol`, `Type`, `Reduced`, `Volatile`, `Object`; the core interfaces `Seqable ISeq Sequential
  IPersistentCollection Counted ILookup Associative Indexed IFn IHashEq IEquiv IMeta IObj
  IReduceInit IPersistentList IPersistentVector IPersistentMap IPersistentSet IExceptionInfo`) holding descriptors; `(type x)` reaches every
  other one and `nil` is the literal.
  A user `(def String ...)` shadows the name. `Long` is the fixnum's descriptor and the boxed long's, one
  value. `Number` does not exist: long and double are two descriptors, extend both.
- **`clj_seq_iter` walks builtin seq types inline** (cons, (), vector, string, the seq.h types) and
  everything else through its slots: a seqable that is no seq is `seq`'d, a seq's `first`/`next`
  hand out owned values the iterator holds (`held`, `item`) until the next step or
  `clj_seq_iter_close`, which a walk that stops early must call (`nth`, `clj_seq_equals`, the
  printer's frames, `is_do_form`, `macro_var` do). Items of the inline path stay borrowed from the
  walked value; once a slot yielded one (`it.slots`) `clj_seq_items` retains every item and hands
  back a vector as `keep`, and the analyzer holds such vectors (`analyzer.keeps`) until the
  analysis ends. Trigger for a faster path: a user seq in a profile (chunking, or a slot walk that
  batches).
- **`reduce` is a `reduce` slot on the descriptor** (`IReduceInit`; reduce.h): `(*reduce)(self, f, init)`
  walks the elements calling `(f acc x)` through a `clj_call` prepared once (eval.h: a closure's
  arity resolved and its body entered without `clj_invoke`, a plain native called directly), stops
  at a `reduced` result and returns it unwrapped, or `CLJ_THROWN`. `init == CLJ_UNBOUND` is the
  2-arity: the first element seeds, `(f)` answers an empty coll. Only step results are checked for
  `reduced`: a reduced init or first element reaches `f` as an ordinary value and comes back as is
  over an empty coll, as on the JVM. Slots: vector and vector-seq (leaf by leaf), range (arithmetic),
  map (`[k v]` vectors built per entry, and `reduce-kv` on the trie in place; `reduce-kv` on a
  vector passes the index), set (elements in trie order), `()`, array and array-seq (boxing per element),
  and cons / lazy-seq / string / string-seq through
  `clj_reduce_iter`, which is `clj_seq_iter` closed on the early stop. The `CLJ_CORE_REDUCE` bit
  (`satisfies? IReduceInit`) sits on vector, vector-seq, range, map and user types; cons, `()`,
  string and lazy-seq have the slot without the bit, as string has `lookup` without `ILookup`.
  Anything else (a `reify ISeq`, a `Seqable` deftype) is `seq`'d and walked by the iterator: two
  Clojure calls per element. A walk holds the head: `(reduce + (map inc (range n)))` keeps the
  realized chain alive until it returns, as the caller's argument array holds the lazy seq (the
  JVM clears the local). Not reducible through the slot: a map's `seq` is still the eager entry
  list, so `(reduce f (seq m))` builds it first; `reduce-kv` on a list throws. Trigger for
  `IKVReduce`/`IReduce` as distinct interfaces: a deftype that needs `reduce-kv`.
- **No chunked seqs.** `seq` on a vector is a view that allocates one 32-byte object per `next`
  (bench/RESULTS.md: 37 ns per element interpreted, 3.5 ns through the iterator); `reduce` and
  `transduce` over a vector or range take the reduce slot and allocate nothing per element, so
  chunking only matters for the lazy `map`/`filter`/`first`/`next` walks. Trigger: seq
  walks of big vectors in a profile; Clojure's chunked seqs batch 32 elements per allocation and
  need `chunk-first`/`chunk-rest` in `map`/`filter`.
- **`clj_equals`/`clj_hash` cannot throw**, so a lazy seq whose thunk throws compares unequal /
  hashes what it yielded and the exception is dropped (`drop_thrown` in coll.c); a deftype `equiv`
  that throws compares unequal and a `hasheq` that throws or yields a non-integer hashes 0, the
  same way. Clojure throws out of `=`. Trigger: user code relying on that exception. Fix: fallible
  equals/hash slots.
- **`apply` spreads its whole last argument** (`clj_seq_items`), so `(apply f infinite-seq)` never
  returns even for a variadic f; Clojure hands the rest seq to a variadic fn lazily. core.clj avoids
  `(apply concat ...)` for that reason (`mapcat`). Trigger: a library doing `(apply concat (map ...))`
  on a lazy source. Fix: `clj_apply` passing a seq as the rest argument of a variadic closure.
- **Forcing a shared lazy seq spins** (`sched_yield`) while another thread runs the thunk; a thunk
  reaching its own object throws "Recursive realization" (thread-local forcing stack). Trigger: a
  thunk that blocks for long with other threads waiting; then park on a condition variable.
- **Metadata is any IPersistentMap**, a sorted map included, and so is `ex-info`'s data map. The three
  places that read a flag out of metadata with `clj_map_get` — a form's reader position, `def`'s
  `:dynamic`, a var's `:private` — first test the hash-map representation and treat any other as absent;
  nothing but the reader and `def` ever writes those keys. Trigger for reading them generically: a library
  that puts a position or `:private` in a sorted map.
- **Meta lives in per-type fields, not the header** (design, "Дескриптор типа"): symbol, vector, map
  and fn have a `meta` field; a cons or `()` grows a trailing word under `CLJ_FLAG_META` (the flag
  survives the dead-link in rc.c so the free path still visits it), so only with-meta'd and
  reader-produced lists pay 8 bytes; a var has an atomic `meta`. `with-meta` on a unique root sets
  the field in place, on a shared one copies the root (a fn copy shares code and env; a copy of a
  native-with-context fn keeps the original alive through `code` and borrows its context, since a
  context has one release callback). `conj`/`assoc`/`dissoc`/`pop` keep a vector's or map's meta;
  `conj` on a cons drops it (Clojure's `PersistentList` keeps it, `Cons` does not — the `clj_list`
  wrapper below fixes that too). Equality, hash and the printer ignore meta (no `*print-meta*`).
- **The seq views carry no meta slot**: `with-meta` on a vector-seq, string-seq, range or lazy-seq
  throws "does not support metadata", where Clojure's `IObj` seqs copy themselves with the map.
  Trigger: a library calling `(with-meta (seq x) ...)` or `(vary-meta (lazy-seq ...) ...)`. Fix: a
  meta field on each view, or the CLJ_FLAG_META trailing word as for cons.
- **`nth` special-cases strings by type** rather than a slot: a string has `lookup`/`count` slots
  but no ILookup/Indexed bits, as `RT.get`/`RT.nth` special-case `String`.

## Locks (include/clj/lock.h)

- **Every mutex of the core is a `clj_lock`**: `os_unfair_lock` under `__APPLE__`, `pthread_mutex_t`
  elsewhere, one interface (`clj_lock_init/lock/unlock/destroy`, `CLJ_LOCK_INIT`), the one `#ifdef` of its
  kind (measured on an M3 Pro, a lock+unlock pair: 2.1 vs 4.6 ns, 4 vs 64 bytes; unfair passes priority to
  the owner under contention). Holders: the keyword table, the namespace registry, the protocol tables and
  the reify registry (proto.c), the profiler table, and every atom. Not recursive, so a path that reaches
  the same lock twice deadlocks (an atom's `swap!` from inside its own `f` is detected before the lock,
  see "Atoms" under Builtins). No rwlock anywhere, by design §4: a read lock is an RMW on the shared
  count, so readers contend like writers; reads in the core go through immutability and the epoch instead.
  Trigger for `os_unfair_lock_trylock`/a fair variant: a profile showing a starved thread on one lock.

## RC (Sources/CljCore/rc.c, object.h)
- **`-DCLJ_NO_REUSE` makes `clj_is_unique` always false** (`make test-noreuse`, `clj_reuse_enabled()` says
  which build this is), and every suite passes in it: the §7 invariant that nothing outside the RC entry
  points depends on the counter is now enforced, not just written down. Off, the flag costs nothing — one
  `#ifdef` arm. Four tests assert that an address survives an in-place step and are gated on
  `clj_reuse_enabled()`; the rest degrade to a copy with the same values, which is what the mode checks.
- **Live-object counter is one process-wide atomic** (debug only). Trigger: debug builds visibly slow
  under many threads. Fix: per-thread counters summed on read.
- **Copy path retains every child and then replaces one slot**: one spare retain/release pair per
  level. Trigger: profiling the "all versions kept" benchmark scenario.
- **The "children of a shared object are shared" invariant is unchecked in debug builds.** A violation
  is a shared parent over an unshared child: the child looks like any unshared object, so the check
  belongs where the edge is visible. (1) In `free_object`'s child walk, a shared parent asserts every
  pointer child is shared or immortal — one flag read on a header already loaded. (2) At the cutoff in
  `clj_share` (`continue` on an already-shared node), `clj_debug_all_shared` of that subtree: the one
  place the walk relies on the invariant. Trigger: the next code that stores into an object in place
  (transients, reuse) or the first spawn primitive.
- **No owner check on the non-atomic path.** "An unshared object is touched only by its allocating
  thread" holds literally today; a debug-only allocating-thread id (side table or debug header
  extension) asserted in the inline retain/release catches the actual cross-thread race regardless of
  how the invariant broke. Handoffs (park/resume, a channel move) will need an explicit
  `clj_debug_reown` at each transfer point, which documents them. Trigger: the first spawn primitive.
- **Share of retain/release on shared objects: 79–83 % with the state in an atom** (bench/RESULTS.md,
  "Atoms"; `clj_debug_rc_ops` counts the plain, shared and immortal paths in debug builds, one relaxed
  atomic add per retain/release, the same process-wide-counter caveat as the live count above). The flag
  is monotone, so the first `reset!` puts the whole domain on the atomic path, ~80 pairs per state tick,
  on the order of 300 ns — the same order as one `swap! assoc` (288–904 ns at 16–100000 keys, the path
  copy of the "Atoms" entry under Builtins). BRC is not taken: the copy path is dominated by the node
  copies, not by their retains (measured while the hand-over existed: 140 in place against 765 copied).
  Trigger: a profile of a real app-state loop where the atomic pairs show next to the interpreter's
  per-node cost.

## Map (Sources/CljCore/map.c)

- **`clj_debug_hash_override` is checked on every `clj_hash`** even in release (one global load +
  branch). Trigger: it shows in a profile.

- **`clj_map_of` stays in map.h**: core.c reads the hash slot through it and MapTests checks root
  identity. Trigger: a second map representation (shapes) behind the same functions; then replace both
  uses with accessors like `clj_debug_vector_root`.

## Set (Sources/CljCore/set.c)

- **A set is a wrapper over a map** (`clj_set.impl`, element → element), as Clojure's PersistentHashSet
  over PersistentHashMap: two objects per set, 16 bytes per element in the trie for a value nobody
  reads. `conj`/`disj` hand the wrapper's own trie reference to `clj_map_assoc`/`dissoc`, so a unique
  set edits its trie in place (the consuming `disj` intrinsic and the reduce drivers reach it as they
  reach `conj`); a present element is kept as it is (`(conj #{[1]} [1])` returns the same set). `seq`
  is an eager list of the elements, printing collects them into an array, `get` returns the stored
  element. Triggers: the ≤8-element linear-array set of the design ("Представление по наблюдению";
  the same trigger as the array map: small literal sets in a profile); a set-shaped trie without the
  value slots (memory of big sets); `clojure.set` as a namespace (the `ns`
  form, see core.clj).

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
- **A nil comparator means `clj_compare`** (compare.h), which is what `sorted-map`/`sorted-set` build with,
  so the default collection compares in C: a `get` is 3–8× the hash map's and an `assoc` 2–4×
  (bench/RESULTS.md), where passing `clojure.core/compare` as the fn made every tree level a Swift↔C
  transition of ~70 ns and the ratios 10–83×. `sorted-map-by`/`sorted-set-by` take a fn comparator and go
  through `clj_call_invoke` per comparison; it may answer a number or, as a predicate, logical true when its
  first argument sorts first, the way `AFunction.compare` reads a fn comparator. `empty`, `assoc`, `dissoc`
  and `with-meta` carry the comparator over, and `(sorted-map)` is therefore still not a singleton. Trigger
  for a cheaper fn comparator: a `sorted-map-by` in a profile.
- **`dissoc` walks the tree twice**: `node_find` first, because the LLRB deletion is only correct for a
  key that is present and because the count must not move when it is not. Trigger: a delete-heavy profile.
- **Equality and hash cross representations**: `(= (sorted-map :a 1) {:a 1})` and the reverse are true and
  the hashes match, so a sorted map and a hash map of the same content are the same key in a third map.
  `map_equals`/`set_equals` now test the `CLJ_CORE_MAP`/`CLJ_CORE_SET` bit instead of the concrete type and
  look a foreign representation's entries up through `clj_equals_lookup`, which drops a comparator's
  exception — `clj_equals` cannot throw.
- **`seq` is an eager list** as the hash map's and hash set's are, so `first` on a big sorted map builds
  the whole list; `subseq`/`rsubseq` go through `sorted-seq-from*`, which prunes the subtrees outside the
  bound and is O(log n + k), and then `take-while` in core.clj as Clojure does it. `rseq` walks the tree
  in reverse. Trigger for an O(1) view: a `first`/`next` walk of a sorted map in a profile; then a stack
  of parents per seq object.
- **No sorted collection reaches the tree codec** (node_data.c): there is no literal for one, so it can
  never be a constant in an analyzed tree. Trigger: a compiler that wants to emit one.

## Records (Sources/CljCore/record.c)

- **A record type is the first half of design §4's shapes**: a `clj_user_type` descriptor with the basis
  field keywords in a trailing C array, the hash map's `core_bits` (minus `IEditableCollection`) and the
  map slots filled from C. `clj_record_type_new` allocates the longer descriptor and hands it to
  `clj_user_type_init`, the half of `clj_user_type_new` that fills a descriptor the caller allocated, so a
  record gets the deftype name, field vector and `user_protos` without a second copy of that code. What is
  still missing for a shape is the sharing (one descriptor per key set, not per name), the transition tree
  and the call-site cache; the layout and the write path are the same.
- **An instance is one allocation**: header, the basis values inline, then `extmap` and `meta`. The basis
  comes first on purpose — that prefix *is* a `clj_instance`, so `field*` and the whole `deftype` macro
  body machinery (`method-map`, the groups, the `let` over `field*`) read a record's fields unchanged and
  `defrecord` shares `field-wrap` with `deftype` in core.clj. `clj_is_instance` is therefore true for a
  record; only `new*` has to tell them apart, by the `CLJ_CORE_RECORD` bit, because the sizes differ.
- **Lookup is a linear pointer scan over the interned basis keywords**, which is what `condp identical?`
  compiles to in Clojure's own `defrecord`; `assoc` of a basis key writes the slot in place when
  `clj_is_unique`, like the hash map's consuming path, and of any other key goes to the extmap.
  bench/RESULTS.md, "Records": 1.8 ns for the first field, 2.8 for the third, 4.8 for a unique `assoc`.
  ≤8 fields is the design's expectation; past that the scan is the cost and the fix is the inline cache,
  not a hash.
- **An empty extmap is normalized to nil**, so two records of equal content are equal whatever route built
  them: `(= (dissoc (assoc r :z 1) :z) r)`. Equality needs the same descriptor pointer and compares the
  basis slots and the extmaps; `(= record map)` is false in both directions, which map.c and sorted.c
  enforce by rejecting a record in their own `equals`.
- **`hash` is the map hash of the same content**, not Clojure's `(bit-xor (hash classname) (mapHasheq
  this))`. Equal hashes across representations cost collisions only — `=` still separates them — and the
  entry mix comes from map.c (`clj_map_entry_hash`) so the two cannot drift. There is no hash cache on a
  record yet; trigger: records as map keys in a profile.
- **`dissoc` of a basis key gives up the shape** and answers a plain hash map of everything, with the
  record's meta, as the JVM's generated `without` does; of an ext key it stays a record. `empty` throws
  "Can't create empty", `conj`/`merge`/`reduce-kv`/`into`/`select-keys` behave as for a map, and
  `select-keys` returns a map because it builds onto `{}`.
- **The body may implement protocols only.** A core interface in a `defrecord` form is refused by name:
  every slot behind one — `seq`, `count`, `valAt`, `invoke`, `meta`, `reduce`, `hasheq`, `equiv` — is the
  record's own, and a trampoline over it would break the map contract that `record?` promises. A record
  reaches `IDeref` and every other `defprotocol` exactly as a deftype does, through `extend`. Trigger for
  opening it up: a library that puts `IExceptionInfo` or `IFn` on a record.
- **`record*` is a builtin call in the expansion, so the descriptor is runtime state**, never a node
  constant: a record instance is as unserializable as a deftype instance (node_data.c refuses both), and a
  macro that embeds one fails the same way. `#ns.Name{…}` prints but does not read back
  (docs/jvm-differences.md).
- **`IEditableCollection` is a bit, not interop.** medley's `editable?` is
  `(instance? clojure.lang.IEditableCollection coll)`; `CLJ_CORE_EDITABLE` sits on the hash map, the
  vector and the hash set — exactly the types the JVM answers true for — and the interface is bound under
  both its bare name and the dotted one libraries spell out. Transients are the persistent operations
  here, so the bit answers the predicate and nothing else.

## Arrays (Sources/CljCore/array.c, builtins_array.c)

- **Elements live inline, after the header**, not behind a pointer: `{rc, flags, type, kind, count, data[]}`,
  one allocation, one load to reach an element and a fixed offset from the object to the first byte. An array
  is fixed-length and never `clj_realloc`'d, so that address is stable for its life, which is what a zero-copy
  handoff needs — a `u8` or `f32` array can become a `Data`/`UnsafeBufferPointer` over `clj_array_data`
  without a copy. The cost of inline is that the whole object goes through the size classes, so a big array
  lands in the system allocator (`CLJ_FLAG_LARGE`) and there is no resize and no page-aligned buffer; an
  external buffer would give both and cost an indirection on every `aget`. Trigger for the pointer form:
  a Metal buffer that must be page-aligned, or `MTLBuffer`-backed storage the array only views.
- **Ten element kinds, one type.** `i8 u8 i16 i32 i64 f32 f64 bool char object`; Clojure's constructors name
  seven of them (`byte short int long float double` plus `boolean`/`char`/`object`) and `u8` exists for the
  bridge. A kind is named by keyword in either spelling (`:int` and `:i32`), and `make-array`/`into-array`
  take that keyword where the JVM takes `Integer/TYPE` — a class object is interop and has no representation
  here. `char` elements are 4-byte Unicode scalars, not UTF-16 units, so a `char-array` is twice the JVM's.
- **Reads box, writes range-check.** `aget` returns a fixnum, a double, a bool, a char or the stored value;
  an `i64` past the fixnum range promotes to a bigint, as the rest of the tower does. `aset` casts the way
  `RT.byteCast`/`intCast`/`floatCast`/`booleanCast` do — an integer kind truncates a double toward zero and
  refuses what leaves its range ("Value out of range for byte: 300"), `bool` takes truthiness, `char` takes
  a char or a scalar. `aget`/`aset`/`alength` are intrinsics (intrinsics.h) and impure ones: an array is
  mutable, so the optimizer may not fold them.
- **An array is mutable, so it is outside the reuse analysis.** `clj_is_unique` is never consulted: `aset` is
  a plain write into the object the caller already holds, and `aclone` is the only copy. An `:object` slot
  retains what goes in and releases what it replaces, and a write into a *shared* array shares the new value
  first, keeping the invariant that every child of a shared object is shared. Two threads writing one shared
  array race, as they do on the JVM; nothing in the core makes that safe.
- **Seqable and nothing else.** `core_bits` is `CLJ_CORE_SEQABLE` alone, as a JVM array is no
  `IPersistentCollection`: `(coll? a)`, `(counted? a)`, `(indexed? a)` and `(sequential? a)` are all false,
  while `count`, `lookup` and `reduce` are slots without their bits and `nth`/`contains?` special-case the
  type by hand, the way `RT.get`/`RT.nth`/`RT.contains` special-case `String`. `seq` is an O(1) view
  (`clj_array_seq`, 32 bytes per `next`) that the iterator walks through its slots, not inline. `=` and
  `hash` are identity, as on the JVM, so an array is a map key by address and never equal to its clone.
- **Printing writes the elements**, `#array[:int 1 2 3]`, where the JVM prints `#object["[I" 0x… "[I@…"]`
  (docs/jvm-differences.md). The printer boxes every element into a frame of owned entries, so printing a
  big array allocates the whole row; and an `:object` array that holds itself prints forever, the same
  hazard a self-referential lazy seq already has (no `*print-length*`).
- **`vector-of` is a normal persistent vector** whose elements went through the kind's cast, so
  `(vector-of :byte 300)` throws and `(vector-of :float 0.1)` holds `0.10000000149011612` as on the JVM, but
  the storage is boxed `clj_value`s and `conj` onto it forgets the kind. An unboxed persistent vector needs
  the trie to carry an element kind and every leaf to be typed, which is the "elements kinds" item of design
  §4; the typed array is the piece that item stands on. Trigger: a `vector-of` in a profile, or the first
  code that wants `(vector-of :f32)` handed to Metal.
- **No multi-dimensional arrays**: `make-array` takes one dimension and `aget`/`aset` one index, where the
  JVM nests. An array of arrays is written out by hand. Trigger: a library indexing `(aget m i j)`.
- **`aset-int` and its siblings are aliases of `aset`**: the array's kind decides the cast, so `aset-int`
  into a `double-array` stores a double where the JVM would refuse the array type at compile time.

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

## Vector (Sources/CljCore/vector.c)

- **`clj_vector_from_array` is a conj loop**: the leaf grows through `clj_realloc` one slot at a time,
  ~13 size-class moves per 32 elements. Trigger: reader or `vec` on large inputs showing in a profile.
  Fix: build full leaves directly and push them.
- **`conj` is 10× a mutable `Array` append** (bench/RESULTS.md): wrapper and tail ownership checks,
  a retain, and a `clj_realloc` that moves at every size-class boundary. Trigger: a conj loop in a
  profile. Fix: transients (one owner, no checks), or a tail allocated at slack capacity.
- **No identity short-circuit in `assoc`**: storing the element already there still copies the path
  when shared and resets the hash cache. Clojure does the same.
- **Index and count are `uint32_t`**; a negative index from a higher layer must be rejected there.

## List (Sources/CljCore/list.c, cons.c)

- **PersistentList and Cons are two descriptors over one 32-byte cell** (`clj_list_type`,
  `clj_cons_type`), differing only in name, the `CLJ_CORE_LIST` bit and `conj`. `list?` reads the bit, so
  `(list? (cons 1 '()))` is false while `seq?` stays true, and `(peek (cons 1 '()))` throws as on the JVM.
  Reader lists, `list`, `list*`'s tail, `reverse`, `rest` of a list and `(cons x nil)` (RT.cons's own rule)
  are PersistentLists; `cons` onto a seq, a lazy seq and `conj` on any other seq give a Cons.
  `conj` on a list or on `()` carries the collection's meta onto the new head, as `PersistentList.cons`
  does, and `pop` of the last cell hands the empty list that meta; `ASeq.cons` (a Cons, a lazy seq) does
  not. A Cons constant is not foldable: the codec reads it back as a list, and a fold must not change a type.
- **A cons chain has no count slot** (`count` walks it) and no hash cache, so hashing a list walks it
  every time. Trigger: lists as map keys or `count` on long lists in a profile. Fix: a count and a hash
  cache on the list cell, as Clojure's PersistentList has.
- **Hash and equality recurse on nesting depth** (`clj_hash` → element hash). Reading and printing are
  iterative, so a 200k-deep literal reads and prints but crashes when hashed. Trigger: untrusted input
  used as a map key. Fix: an explicit stack in `clj_seq_hash`/`clj_seq_equals`, or a depth cap.

## Reader (Sources/CljCore/reader.c)

- **Tagged literals run their tag fn at read time, inside `clj_read`** (`F_TAG`, `read_tagged`): the tag symbol
  and then the form arrive through `push_value`, and the frame applies `clj_reader.read_tag` to them, so the
  value a literal reads as is a constant to the analyzer like any other. The hook is the runtime's
  (`clj_reader_read_tag`, runtime.c): a dotted tag is a record constructor (`#ns.Name{…}` through
  `clj_record_from_map`, `#ns.Name[…]` positional, LispReader's CtorReader order), then `*data-readers*`, then
  the built-in `inst` and `uuid` (`clj_default_data_reader`, which a reader with no hook also gets), then
  `*default-data-reader-fn*`, else "No reader function for tag". A throw out of the fn is a reader error with
  the exception's message at the literal's position. `default-data-readers` is a plain map of the two native
  fns (`read-inst*`, `read-uuid*`), so a library can merge its own into `*data-readers*`. Not here:
  `tagged-literal`/`reader-conditional` values and `#=` (docs/jvm-differences.md).
- **`#uuid` and `#inst` are value types** (uuid.c, inst.c): a boxed pair of signed longs with `UUID.hashCode`,
  `fromString`'s lenient grouping for `parse-uuid` and `arc4random_buf` for `random-uuid`; a boxed
  millisecond count named `Date` with `Date.hashCode`, read by clojure.instant's grammar (every field
  range-checked, the fraction taken as nanoseconds) through proleptic-Gregorian day arithmetic
  (`days_from_civil`) and printed `yyyy-MM-ddTHH:mm:ss.SSS-00:00` in UTC as the JVM does. Both compare, so
  they sort, and both are node constants: the codec reads them back from their printed form. `str` of a Date
  is that text, not `Date.toString` (docs/jvm-differences.md).
- **Namespaced maps** `#:ns{…}`, `#::{…}` and `#::alias{…}` (`F_NS_MAP`, `read_ns_map_prefix`): the prefix
  token is read, a `{` must follow after optional whitespace, and the map's keys are rewritten as it closes
  (a keyword or symbol with no namespace takes `ns`, one in `_` loses its own, the rest stay); `#::` resolves
  through `resolve_ns` like `::kw`. A collision after the rewrite is "Duplicate key".
- **`#"..."` keeps its text verbatim** (`read_regex`): the string escapes are *not* applied, so a backslash
  reaches the pattern as written and only `\"` fails to close the literal, as LispReader's RegexReader does.
  The pattern compiles at read time, so a syntax error is a reader error at the literal's position.
- **`#(...)` rewrites its body after the list is read** (`fn_literal`): `%`, `%N` (1–20), `%&` become
  `p1__N#`/`rest__N#` params of a `fn*`, with one recursive walk over the literal's own nesting (the
  reader is otherwise iterative). Nested `#(` is refused, as LispReader does.
- **An unselected `#?` branch reads as data whatever it contains** (`in_unselected_branch`, `F_SUPPRESSED`):
  a tagged literal there reads as nil without running its tag fn (the tag's form is read and dropped), a
  `#:ns{}` map as a plain one and a `#=` as nil, instead of ending the file, as Clojure's suppressed read
  does; in the selected branch a tag runs and `#=` is the error it is outside one. A `#"..."` there reads as the plain string of its text and is never compiled, so a pattern
  meant for another runtime's engine cannot fail the read. Which branch is selected is known while reading: the body list's items
  so far are on the value stack, features at the even indexes. Suppression follows the enclosing conditionals,
  so a selected inner branch inside an unselected outer one is suppressed too. Numbers need no suppression:
  every literal the reader takes now reads in either branch (numeric tower).
- **Reader conditionals** `#?`/`#?@` select the first branch whose feature is in `clj_reader.features`
  (a set of keywords copied from the process-wide `clj_reader_set_features` at init; `:default` always
  matches; nil means `:default` alone). No branch → the form reads as nothing (an EOF at top level, a
  missing item inside a collection); `#?@` splices only into an enclosing collection. Which key names
  this runtime is still open (Open decisions); the corpus harness sets `#{:clj}` per library.
- **A keyword's parts may start with a digit** (`:0`, `:1/2`), a symbol's may not: Clojure reads and prints
  both keywords, and the suite's `(keyword "0")` round-trip needs it.
- **`::kw` and `::alias/kw`** resolve through `clj_reader.resolve_ns` (`clj_reader_resolve_ns`: the
  current namespace or one of its aliases); with the hook NULL they are reader errors, an unknown alias
  is "Invalid token". Hex, octal and `NrDDD` radix integers read into longs, or into bigints past 64 bits
  (numeric tower).
- **Every non-empty list read costs a `{:line :column}` map** (map wrapper plus one node) on its head
  cons, as Clojure attaches positions to lists only; `'x`, `@x`, `#'x` and the syntax-quote output
  are built by the reader without one. Syntax-quote drops the meta of the forms it rebuilds where
  LispReader keeps everything but the position keys. Trigger: `^:once`-style meta inside a
  syntax-quoted template. Fix: `sq_pop` wrapping the rebuilt collection in `with-meta` when the source
  had non-position keys.
- **Syntax-quote resolves through `clj_syntax_quote_resolve` in the thread's current namespace** (`*ns*`),
  not the `clj_env.ns` the host later analyzes in; `resolve_ctx` is unused. The two agree because every
  loader evaluates with `env.ns` nil (the current one) and reads one form before evaluating it, so an
  `in-ns` governs the forms after it; a host that reads a whole file first (`Value.readAll`) resolves
  everything in the namespace current at read time. `alias/x` is rewritten to the aliased namespace,
  a qualified symbol whose prefix is no alias stays as written; no Java class heuristic (`foo.Bar` is
  treated as a namespace prefix and left alone).
- **`~`/`~@` outside syntax-quote are reader errors**, where Clojure reads `(clojure.core/unquote x)`
  and fails later. A literal `(clojure.core/unquote x)` inside a syntax-quote is still an unquote.
- **Only lists carry positions**, so an error on a bare symbol or vector reports the innermost
  enclosing list, or `form_line`/`form_col` of the top-level form for a top-level symbol; a form
  built by a macro reports the list the macro call sat in. Clojure does the same.
- **Input is not validated as UTF-8** except inside a character literal; malformed bytes pass through
  into strings and symbols, and a column counts every non-continuation byte. Trigger: a non-Swift host
  feeding raw bytes.
- **`strtod`/`snprintf` in reader and printer follow the C locale**, which the runtime never changes;
  a host calling `setlocale` with a comma decimal point would break doubles.

## Regex (Sources/CljCore/regex.c)

- **One file, no library, a java.util.regex subset.** `clj_regex_new` parses the pattern into an
  instruction array once (`re_prog`) and `re_run` walks it with an explicit backtrack stack, so nesting
  a quantifier costs heap, not C stack. Only a lookaround or an atomic group recurses into `re_run`, and
  the pattern's own nesting bounds that depth.
- **Jumps are relative to the instruction's own index**, which is what makes the compiler simple: a
  quantifier inserts its `SPLIT` *before* the block it repeats (`prog_insert`) and a counted repeat copies
  the block with `memcpy` (`repeat_block`), and neither has to fix up a target. `{n,m}` expands to `n`
  copies plus `m - n` optional ones, capped at `RE_MAX_REP` and `RE_MAX_PROG` so a pattern cannot ask for
  an unbounded program.
- **The matcher works on code points, not bytes** (`re_text`: the input decoded once into a code point
  array plus a byte offset per index), as `subs` and `index-of` do. A matcher object keeps its `re_text`,
  so `re-seq` over one input is linear in it rather than quadratic; `split` and `replace` keep one
  `re_ctx` across the matches of a scan, so only the first match allocates (bench/RESULTS.md).
- **A group's bounds live in an int32 slot array with an undo log.** Every `SAVE` records the old value,
  and a backtrack point holds the log's height, so restoring a group is popping the log. The same array
  holds the loop marks an unbounded quantifier needs: `MARK`/`PROGRESS` refuse an iteration that consumed
  nothing, which is what keeps `(a*)*` from spinning. Mark slots sit past the group bounds, whose count is
  only known once the pattern is parsed, so the compiler patches their indexes at the end.
- **Case-insensitivity is folded inside the range test**, not applied to the class's answer: `(?i)[^a]`
  must still refuse `A`, which an OR over both cases would accept. ASCII only, as the rest of the runtime's
  case handling is (docs/jvm-differences.md).
- **A negated predefined class is a nested class, a positive one is merged**: `\d` adds its ranges to the
  enclosing class, `\D` hangs off it as a sub-class with `negate`, and `&&` makes the rest of the class
  body the intersection operand, so `[a-z&&[^aeiou]]` is one recursive `class_member` call.
- **Catastrophic backtracking is the host's deadline, not a memo table**: `re_run` reads the clock once per
  4096 backtracks through `clj_deadline_expired` and unwinds with "Execution timed out". Without a deadline
  set, `#"(a+)+b"` against a long string of `a`s runs until the host gives up. A memo table would bound the
  work instead, at the cost of a table per match; trigger is a host that cannot set a deadline.
- **`=` and `hash` read the pattern text**, so `#"ab"` equals `#"ab"` and the two are one set element,
  where the JVM compares `Pattern` by identity (docs/jvm-differences.md). That also makes a pattern a
  serializable node constant: it prints as `#"..."` and reads back equal, so `node_data.c` needs no arm
  of its own for it.
- **`re-find` scans every start position**, since the program carries no first-character filter; the
  leftmost-first rule and `re-matches`' whole-input rule are the same `re_run` with one flag. Trigger for a
  first-set bitmap: a `re-find` in a profile's inner loop (bench/RESULTS.md).
- **`nth` on a matcher is its group**, as `RT.nth` special-cases `Matcher`; the type carries `lookup` for
  the same reason and no core interface bit, so it is not a collection.
- **The `$`-expansion follows `appendReplacement`**: the first digit is always the group and the following
  digits extend it while the group exists, `${name}` names one, and a backslash quotes the next character.
  A group the pattern does not have is an error, not an empty string.
- **The syntax the parser takes**: literals and the escapes `\\ \t \n \r \f \a \e \0nnn \xhh \x{...}
  \uXXXX` and `\Q...\E`, `.`, classes with ranges, negation, nesting and `&&`, `\d \D \w \W \s \S \b \B`,
  `\A \z \Z`, `^ $`, groups (capturing, `(?:)`, `(?<name>)`, `(?=) (?!) (?<=) (?<!)`, atomic `(?>)`),
  alternation, `* + ? {n} {n,} {n,m}` greedy, lazy `?` and possessive `+`, backreferences `\1` and
  `\k<name>`, a dozen `\p{...}` names, and the flags `i s m x` inline and scoped. Everything else is a
  compile error naming the offset, in an `ex-info` whose data carries `:pattern` and `:offset`
  (docs/jvm-differences.md lists what is missing).
- **`re-seq` is a lazy seq over one matcher**, as Clojure's is, so two consumers of the same seq share its
  position; `clojure.string/split` and `replace` scan in C instead (`re-split*`, `re-replace*`), which keeps
  the literal-separator fast path of builtins_string.c untouched for a string or char separator.

## Analyzer and evaluator (Sources/CljCore/analyzer.c, eval.c, fn.c, node_data.c)

- **A node is the program, `clj_exec` its execution state.** `clj_node` carries no interpreter field and
  is `const` to eval.c and fn.c; the analyzer numbers a finished tree in pre-order (`id`, `nnodes` = subtree
  size, so a subtree's ids are contiguous). `clj_exec_new` builds `exec_node[nnodes]` in one walk when a
  tree first runs and every child dispatch goes through `frame->exec->nodes[id]`: one extra indirection
  per node, 1–3 % on the seq benchmarks. The table holds the eval pointer and a hit counter (16 bytes per
  node). Trigger for widening it further: the var inline cache from the design.
- **Exec rewrites: `clj_exec_count` is the first.** It swaps every node's eval for a wrapper that bumps
  `exec_node.hits` and calls `clj_node_eval_fn(kind)`, and back; off, nothing in `eval_child` changes.
  Only the mechanism and a test exist: nothing reads the counters yet (the PGO / hot-branch data source
  of the design). A rewrite while the tree runs takes effect at the next child dispatch.
- **Every node carries `line`/`col`** of the innermost enclosing list the reader positioned (0 when none:
  a list a macro rebuilt reports the list the macro call sat in); the codec writes them as a trailing
  `line column` pair and omits them when unknown. Only traces and the profiler read them.
- **A closure retains its whole top-level tree** through the exec, not only its fn subtree: a fn defined
  inside a large top-level `let` keeps every sibling constant alive, and tests that count live objects
  across a redefinition must repeat the exact defining form. Trigger: memory of a large loaded program;
  then a per-fn exec sliced by the fn's id range.
- **Ownership in the evaluator is a runtime rule, not an analysis** (design, "Конвенция счётчиков"). A
  node evaluates to an owned value, except that `eval_borrowed` reads a local, captured or constant
  node at +0 where the consumer only needs it for a call the frame outlives: the fn position and the
  arguments of an invoke, vector and map literal items, the test of `if`, non-last items of `do`.
  `eval_all` returns a `uint64_t` mask of the owned results (more than 64: all owned) and only those
  are released, on the throw-midway path too. A closure frame borrows its fixed params and the self
  slot from the caller's argument array, alive for the whole call by the +0 convention (`eval_invoke`'s
  buffer, a native's stack array, `clj_apply`'s `all[]`); the variadic rest list is built and owned.
  `clj_frame.owned` has one bit per slot: `slot_set` (let, loop, recur, catch) releases the old value
  only when its bit is set and marks the new owned one, teardown releases owned slots only; a frame
  with more than 64 slots retains every param at entry and treats every slot as owned; the top-level
  frame starts with none. Whatever lands in the heap or is returned is retained as before: `let`/`recur`
  inits, closure capture, a body whose tail is a local. A var read in a borrowed position is +0 when
  its root is immortal (every root bound by boot, next entry) or a fn (next entry but one); a data
  root is retained, since `clj_var_bind_root` releases the old root at once and that +1 is what keeps
  it alive through the call. Measured effect of borrowing locals alone was nil (a non-shared pair is
  five plain instructions); borrowing the core roots removed the atomic pair every call through a
  core var paid, borrowing user fn roots the same pair on every user call (bench/RESULTS.md). A local's
  last use is the one read that hands the frame's reference over instead (the last-use entry below).
- **`clj_node_to_data`/`clj_node_from_data` cover every node kind** (grammar in node_data.c); constants
  are limited to what prints and reads back: nil, booleans, numbers, chars, strings, keywords, symbols and
  vectors/maps/lists/seqs of those (a seq reads back as a list; symbol meta and the reader positions on
  constant lists are dropped, the node's own position is kept). Anything else — a fn or protocol a macro embedded as a constant, a deftype descriptor, a host
  value — makes `to_data` throw "not serializable: <type>". Vars travel as qualified symbols and are
  interned on read; `from_data` checks the shape and slot bounds, not that `recur` sits in a tail
  position. Trigger: a tree cache on disk / AOT; then a binary form and a `recur` placement check.
- **A literal's reader metadata is a `with-meta` call**, as Clojure's `MetaExpr` is: `^:foo [1]` analyzes
  to an invoke of `clojure.core/with-meta` over the vector node and the analyzed metadata map, so
  `^{:a x}` sees the local `x` and the value is rebuilt per evaluation. Folding the metadata into the
  constant would break the codec, which writes a constant through `pr-str` and reads it back: `pr-str`
  does not write metadata. `with-meta` is not a pure intrinsic, so the call is never folded either.
  Vectors, maps, sets and `()` carry it; `^m` on a quote form lands on the quote form, which the analyzer
  consumes, exactly as on the JVM.
- **Every core.clj form and every type-macro expansion must serialize** (`CoreSerializableTests`):
  each top-level form is analyzed in `clojure.core` and round-tripped through `to_data`, `pr-str`,
  read, `from_data`; the test pins the form count so an empty run cannot pass. core.clj defines
  `defprotocol`/`deftype`/`extend-type`/`extend-protocol`/`reify` without using them, so their
  expansions are checked on user forms in the same test. A macro that needs a runtime object must
  emit a var reference or a builtin call that finds it at run time (`reify-type*`), never the object.
- **Macros expand in the analyzer, in `analyze_list`**, not in a separate pass: a list whose head
  resolves to a macro var (and is not a local or a special form) is expanded until it is not, then
  analyzed. `&env` is always nil: locals are slot indices, not a map. Trigger: a macro that inspects
  `&env` (`clojure.tools.macro`-style, `binding`-aware macros). Arity errors count `&form`/`&env`
  (`Wrong number of args (2)` for `(when)`); Clojure subtracts 2.
- **`let`/`loop`/`fn` are core.clj macros over `let*`/`loop*`/`fn*`**, as in Clojure, so
  `macroexpand-1` of `(let ...)` yields `let*` and syntax-quote qualifies them to `clojure.core/let`.
  The analyzer's messages for the starred forms still say `let`/`loop` (`(let* [a] a)` reports
  "let requires an even number of forms"); Clojure says "Bad binding form". Trigger: nobody.
- **`defmacro` emits `clojure.core/fn` once that macro exists, `fn*` before** (`macro_fn_symbol`):
  macros defined in core.clj above the `fn` macro (`when`, `cond`, ...) cannot destructure their
  params. Trigger: a `[bindings & body]`-style macro that wants `[[x y] & body]` up there; move it
  below `fn` or write the `first`/`second` by hand.
- **Var meta follows Clojure minus `:file`**, and `:ns` is the namespace's *symbol*, not a Namespace
  object (there is no `ns-name`; `(str (:ns m))` prints the same). `def` evaluates the symbol's meta
  map as a form, so `^{:tag String}` resolves `String` to the descriptor and an unresolvable symbol in
  it is an analysis error, as in Clojure. The C builtins (`first`, `meta`, ...) carry no `:doc` or
  `:arglists`: `(doc first)` prints only the name. Trigger: a doc browser; then a doc column in the
  `entries` table of builtins.c.
- **Privacy is a resolve-time rule only.** `^:private` hides a var from the unqualified fallback into
  clojure.core, refuses a qualified reference from another namespace (through an alias too) and is
  refused by `refer` ("x is not public"); `(var ns/x)`, `#'ns/x`, `resolve` of the qualified symbol and
  a raw `clj_ns_refer` still reach it.
- **Exceptions unwind by return code, not by `longjmp`**: `try` sees `CLJ_THROWN` from its body and
  takes the pending value; every C frame in between releases its own temporaries on the way out.
  `clj_throw` captures the shadow stack (below) as a vector of `{:fn :line :column}` maps, innermost
  first, at most 256 frames: an `ex-info` stores it at its first throw and keeps it through catch and
  rethrow (`ex-trace`, `ClojureError.trace`); any other thrown value (a string, a host error, a deftype
  error) carries it only in the pending state, so a handler that rethrows it records the handler's
  frames. A frame's position is the call site that entered the fn, or the fn's own position when the
  call came from a native or the host (`apply`, `map`, a Swift `callAsFunction`). The trace is built
  at throw time, so a throw from deep in a loop allocates a vector and a map per frame; nothing is
  captured for exceptions that are never thrown. `clj_take_pending` drops the pending trace: a host
  that wants it takes it first (`clj_take_pending_trace`), as `ClojureError.takePending` does.
- **Shadow stack** (shadow.c): a per-thread ring of `{fn node, call site, sp}` pushed and popped around
  every interpreted closure body (natives are leaves; a compiled fn pushes nothing and is found on the real
  stack instead — "Compiler", frames and traces), 8192 frames, calloc'd on the thread's first push (128 KB)
  and freed when the thread exits. Deeper than that, the innermost frames are kept and
  `clj_shadow_stack_dropped` counts the outermost ones overwritten; a test shrinks the capacity with
  `clj_debug_shadow_stack_set_capacity` since Swift Testing's stacks overflow the C stack long before
  8192 (the main thread of a release build can reach it). `clj_shadow_stack_snapshot` is
  async-signal-safe: it reads through a pthread key rather than the `_Thread_local`, because a first
  touch of a `_Thread_local` on a thread that never ran Clojure allocates under dyld. The stack guard's
  limit lives in the same struct, so a call pays one TLS load for both. Cost per call: that load, a
  null check, three stores (`sp` is the frame's address, what orders it among compiled frames), an
  increment and a decrement with a compare, plus one load and branch on the instrumentation byte
  (bench/RESULTS.md: within the run-to-run noise of the closure-call scenario). The C stack is still what
  limits recursion depth; the shadow stack does not replace it. `clj_shadow_stack_trace` is the merged
  trace (trace.c), not the ring alone.
- **Crash handler** (`clj_crash_handler_install`) is opt-in: a host with its own crash reporter
  (Crashlytics, MetricKit) must not have its handlers replaced, and calls `clj_shadow_stack_snapshot`
  from its own instead. Installed, it writes the frames with `write(2)` only (names are borrowed from
  the fn node's symbol, numbers formatted by hand) and re-raises with the default disposition. The
  frames are the merged trace walked from the signal's context (trace.c), on the alternate signal stack
  every thread that ran Clojure has (guard.c); for SIGSEGV and SIGBUS the guard page check runs first, so
  a stack overflow in compiled code is an error, not a report. Tested on SIGUSR1 through a pipe on a
  plain pthread: `raise` on a dispatch worker thread cannot `pthread_kill` itself and delivers the
  signal to whichever thread has it unblocked.
- **Signposts** (`clj_signposts_enable`, `Runtime.signposts`) are Apple-only and process-wide: an
  `os_signpost` interval named `invoke` with the fn name per closure call, off by default; elsewhere
  the call is a no-op. Enabling it costs a signpost id and two `os_signpost` calls per invocation.
- **Fn profiler** (`profile-start!`/`profile-stop!`, the `profile` macro): inclusive wall time and
  call count per fn node, aggregated at pop into one global table under a mutex, reported as
  `{:fns [...]}` sorted by time. It does not measure natives (`+`, `first`, a Swift fn: they are
  leaves without frames), self time, or a call already running when it starts. Macro expansion runs
  closures, so a form analyzed while the profiler is on shows core.clj's macros in the report; the
  `profile` macro expands to a `let`, not a top-level `do`, so its body is analyzed before
  `profile-start!` runs. Entries retain their fn node until the stop. Trigger for a sampling
  profiler: a workload where the mutex per return shows.
- **Debug live counts are per type as well** (`clj_debug_live_objects_of`, `clj_debug_live_report`
  prints `type: count` for non-zero types): a 1024-slot table keyed by descriptor pointer, slots
  never freed, so a dead deftype descriptor keeps its slot and a reused address inherits its count.
- **`catch` knows five class names and no hierarchy**: `:default`, `Throwable`, `Exception` and
  `Object` take every thrown value, `ExceptionInfo` takes values whose type has `CLJ_CORE_ERROR`;
  anything else is "Unable to resolve classname". Trigger: catching a host error by its Swift type
  (`(catch MyError e ...)`); that needs a class registry mapping names to descriptors or host
  metatypes, and `isa?`-style ordering of the clauses.
- **`throw` accepts any value** (CLJS semantics): no implicit wrapping of a string or map into an
  ex-info, and no runtime check. `ex-message` of a thrown string is the string itself (CLJS says nil),
  so a `:default` handler reads `(throw "m")` like an ex-info; a string is still no error for
  `ExceptionInfo` or `ex-data`. Trigger: the analyzer's `:strict` mode, which should warn on "throw of
  a non-error value" (JVM/Swift strictness as a lint, not a runtime rule).
- **Namespaces** (ns.c, builtins_ns.c, the tail of core.clj). `*ns*` is a dynamic var in clojure.core whose
  root is `user`; `clj_ns_current`/`clj_ns_set_current` read and write the thread's binding when it has
  one, else the root, so `in-ns` inside a load moves only that load. `Runtime.eval`, `load-file`,
  `load-string` and `require` push `{*ns* (current) *file* path}` around their forms, as Clojure's `load`
  does; `cljEval` in the tests does not, and a test that moves must come back (`inUser`). A namespace
  holds mappings, refers, aliases and an `excludes` set: unqualified resolution is mappings → refers →
  clojure.core minus its private vars and the excludes (so `:refer-clojure :exclude/:only/:rename` are
  the excludes plus refers under the new names, and core stays implicitly visible: a core var defined
  later is visible too, where Clojure's refer snapshot would miss it); a qualified symbol resolves its
  prefix through the aliases first, then the registry, and reads the target's own mappings only (a var
  referred into `b` is no `b/x`). `require` (core.clj `load-libs`) takes symbols, `[lib :as a :refer
  [..] :refer :all :as-alias a]`, prefix lists and the `:reload`/`:reload-all` flags (both reload the
  one lib: no dependency tracking), looks the lib up as `a/b_c.cljc` then `.clj` under the roots of
  `clj_load_path_set` / `Runtime.loadPath` after the embedded libs (`<embedded>/clojure/set.clj` and
  friends, `libs_clj.inc`), records it in `*loaded-libs*` (an atom, not a ref) after a successful load,
  and fails with "namespace 'x' not found after loading" when the file defines no such ns. `ns` handles
  `:refer-clojure`, `:require`, `:use`; `:import` and `:gen-class` name JVM classes and expand to nothing,
  so a class shows up as "Unable to resolve symbol" where it is used; `:load` throws. No ns metadata
  (the docstring and attr-map are dropped), no `ns-unalias`, no `remove-ns`, no `*loaded-libs*` as a
  sorted set, no `load` of a classpath resource by path. A load error is rethrown as
  "Syntax error compiling at (file:line:col). <message>" with `{:file :line :column}` data and the original
  as the cause, like CompilerException. Namespaces are immortal like vars: tests create theirs before
  taking a baseline.
- **Var meta carries `:file`** when `*file*` is bound (a load); the host's `eval` and the tests bind none.
- **`set!` is a rewrite**, not a node: `(set! sym v)` becomes `(clojure.core/var-set (var sym) v)` in the
  analyzer, so it serializes as an invoke. A local target is "Cannot assign to non-mutable"; deftype
  fields are not assignable (no mutable fields).
- **Lenient loading** (`clj_load_set_lenient`) is the corpus harness's mode: a top-level form that fails to
  read or evaluate is recorded (`clj_load_take_failures`: `{:file :line :column :name :message}`) and
  skipped, so one missing function does not hide the rest of a library's gaps. Never on for a user.
- **`defmacro` on a failing body still interns the var** (analysis creates it before the fn is
  analyzed), as `def` does: the name resolves afterwards to an unbound var. Same as Clojure.
- **No hoisting.** A file is analyzed one top-level form at a time, so a forward reference is
  "Unable to resolve symbol" (design: pre-pass registering `def` names at file load).
- **`def` is eager and vars are plain roots.** No lazy thunk state (design §4 "Var и ленивые def").
  Trigger: the first ns whose load-time cost shows.
- **Dynamic vars** (var.c): a per-thread stack of frames, each a persistent map var → box (a volatile)
  merged with the frame below, pushed by `push-thread-bindings` and popped by `pop-thread-bindings`
  (`binding` is the `try`/`finally` pair over them, `with-bindings*`, `bound-fn*` and `with-redefs-fn` are
  core.clj). `clj_var.thread_bound` counts live bindings across all threads, so a deref of a dynamic var
  looks a frame up only while someone binds it; a non-dynamic var's deref is unchanged except for one byte
  load and a predicted branch in `eval_borrowed` (bench: counting loop and closure call within noise,
  bench/RESULTS.md). Binding a non-dynamic var throws "Can't dynamically bind non-dynamic var: ns/x",
  `set!` on a var without a thread binding "Can't change/establish root binding of: ns/x with set". A
  binding's value is stored unshared: the frame belongs to one thread. Deviations: `with-redefs` swaps
  roots process-wide with no lock, as Clojure's does; the bound value is not shared, so a binding handed
  to another thread through `bound-fn` shares it only when that thread's frame stores it (a value
  published this way must be treated as shared by the caller — trigger: `bound-fn` across threads with a
  mutable graph, then `clj_share` in `push_entry`). No `*print-length*`, `*out*`, `*assert*`, `*flush-on-newline*`
  or the other printer vars; `with-out-str` captures the output hook per thread instead.
- **Concurrent `def` against `deref` is unsafe**, and `alter-meta!`/`reset-meta!` against `meta` the
  same way: `clj_var_root`/`clj_var_meta` return a borrowed pointer and a racing writer releases the
  old value, so a reader may retain a freed one (`alter-meta!` is a CAS loop, so its `f` may run
  more than once under contention, as Clojure's); a fn root read at +0 by a call on one thread while
  another thread's `def` replaces it is the same race (the parked-root list is per thread). Redefinition
  is a dev-time operation; evaluate on one thread at a time while defining. Concurrent *calls* of one
  fn from several threads are fine, `extend` against them included (the protocol cache is built for
  it, `concurrentDispatchWhileExtending`). Same for `clj_ns_current` vs `clj_init` ordering: call
  `clj_init` before any evaluation.
- **A side cell of an exec node is a shared mutable cell** (any slot written at run time: an inline
  cache, a cached transducer composition, specialization state, profile counters). It must hold an
  immortal value (filled once via CAS, `CLJ_FLAG_IMMORTAL` set before publishing, the loser freed before
  publishing; the leak is bounded by the number of forms, as with vars), be per-thread, or hold a shared
  value released through the epoch. An ordinary object with an ordinary release there is the concurrent
  `def`/`deref` race again. INTRINSIC keeps a retained (immortal) var and reads the root at evaluation;
  the protocol cache (type descriptor entry above) is the third kind: borrowed impls that only an epoch
  bump retires, a seqlock around the fill. The fusion pass (below) keeps nothing in a side cell: its
  per-form work measured too small to cache. Trigger: the var inline cache of the design.
- **Var lookup is a root load on every evaluation** of a var node (an acquire load; an intrinsic's guard
  is a relaxed one), no inline cache, and no closure cache either: a closure's fn node carries its arity
  table, so the call path reads `fixed[nargs]` off the live closure — one load — where a cache keyed on
  the fn would have to be validated by that same load (and a retained key pins captures or cycles
  through recursion: a site in core.clj's `map` would hold the last `f`, a site in `f` its own exec).
  What a var cache could still save is the acquire load and the immortal/fn check, ~1 ns; trigger: that
  showing in a profile.
- **The call path** (eval.c, `eval_invoke`, `run_frame`; bench/RESULTS.md, "Call-site caches"): a
  closure with a fixed arity for the call and a frame of at most `SMALL_SLOTS` (16) has its arguments
  evaluated straight into the frame on `eval_invoke`'s stack — no argument buffer, no copy — and the
  owned mask of that evaluation is the frame's, so a `recur` over a param releases what it should and
  teardown releases the rest. The guard reads its limit from the shadow stack (the one thread-local a
  call touches; computed on the thread's first call), then the shadow push, the instrumentation byte,
  the body, the pop (which drains parked fn roots at depth zero). ~7 ns per call including the
  dispatch of a one-intrinsic body; the guard, the shadow frame and the instrumentation byte are each
  within the run-to-run noise now. The generic path (`closure_run`) takes a variadic arity (the rest
  list is built from the buffer), a frame past 16 slots (heap) or 64 (every param retained), a native,
  a protocol method (its cache) and every call from a native or the host (`clj_closure_invoke_at`).
  A native that calls one fn per element (`reduce` and the slots behind it) prepares a `clj_call`
  once — a closure's arity, or a plain native's function pointer after its arity check — and enters
  `closure_run` per element without the type dispatch, the fn-kind switch and the arity search; a
  fn that does not take the count still goes through `clj_invoke`, which reports it (measured
  against `clj_invoke` per element in bench/RESULTS.md, "IReduce").
  A plain native (`CLJ_FN_NATIVE`) at the head — a builtin in a local, a captured slot, a param or a
  user var — is called from the site's borrowed argument buffer after the arity check `fn_invoke`
  would make (`call_native`): no protocol probe first, no `clj_invoke`, no type slot, no kind switch
  (~2 ns of the ~9 such a call cost; bench/RESULTS.md, "Direct native call"). Natives are leaves of
  the shadow stack, so a throw inside reports the same frames either way. A native with a context (a
  host fn, the fusion drivers' reducing fn), a keyword, a map or a vector at the head still go through
  `clj_invoke`; measured from the site too, the context native gained nothing, and an intrinsic-by-value
  variant (a reverse index from the builtin fn object to its table entries, the fixed-arity C function
  called without the `(args, n)` convention) was within noise or worse — the builtins already forward
  in one call. `apply` and every call from a native or the host are unchanged (`clj_invoke`). A let/loop-bound fn
  called only as a head takes none of these paths (next entry).
  Debug builds count per site the calls that took a fast path (`clj_debug_exec_ic_hits`) against the
  generic ones (`..._misses`); release builds count nothing. The site array is indexed by
  `clj_node.site`, the node's ordinal among the tree's INVOKE nodes, assigned with the ids (it fills
  the padding after `col`, so a node grew by nothing; `from_data` renumbers it too).
- **Direct local fns** (optimizer.c `direct_pass`, eval.c `eval_direct_call`; design §6b item 7;
  bench/RESULTS.md, "Direct local fns"): a `let*`/`loop*`-bound `fn*` whose binding is referenced only
  as the head of INVOKE nodes — in the body, in later inits of the same binding vector, inside inner
  *direct* fn bodies (through the static link), and through its own name inside its arities — becomes
  a `DIRECT_FN` node: the let stores nil in its slot, every such INVOKE becomes a `DIRECT_CALL` that
  evaluates its arguments straight into a fresh frame (as the closure fast path does), links the frame
  to the defining one (`clj_frame.outer`, `depth` links up from the caller: 0 from the let body, 1 from
  the fn's own body, deeper from nested direct fns) and runs the body through the same `run_body` as a
  closure — guard, shadow frame (the DIRECT_FN node, so traces and the profiler name it as before),
  instrumentation, recur loop, owned-slot release. The body's free variables were analyzed as
  captures; the rewrite turns each `CAPTURED` read into an `OUTER {depth, slot}` read of the defining
  frame (a captured value of the definer stays `CAPTURED`: the direct frame shares its environment) and
  remaps the capture sources of closures made inside the body the same way (`clj_capture` has a kind
  and a depth). Decided in the optimizer on the analyzed tree, not on forms: macros decide what a use
  is (`(m (f 1))` may expand to `(map f ...)`), and not in the analyzer, which would have to analyze the
  init before seeing the uses. Escapes, each keeping the closure and its behaviour: the binding as an
  argument, a return value, a recur argument, a later init's value, a `loop*` slot any recur of that
  loop rebinds, a capture of an inner closure (including a `lazy-seq` thunk), a call with an argument
  count the fn has no fixed arity for (the runtime arity error stays), a variadic fn. Frame model: a
  frame per activation with a static link, chosen over "params as extra slots of the enclosing frame"
  because that grows the enclosing arity past 16 slots (heap frame, no direct entry for the enclosing
  closure) or 64 (every param retained) and needs a save/restore of the helper's slot range around
  every recursive call; the link costs one pointer per frame and an indirection per free-variable read,
  and recursion gets its own slots for free. An `OUTER` read is owned (retain/release), not borrowed:
  a fifth case in `eval_borrowed` made the switch a jump table in every inlined copy, +3 ns on every
  row. Serialized as `[:direct-fn name [arity+]]` (only as a let/loop init), `[:direct-call [slot
  depth] args*]` resolved by the decoder through a chain of binding frames, `[:outer [depth slot]]`;
  `from_data` checks the link depth and the slot against the chain's frames. In core.clj only `psig`
  in the `fn` macro qualifies; the `step` helpers of `drop`, `drop-while`, `mapcat`, `map` (4+ colls)
  and `sequence` are called inside a `lazy-seq` thunk (an inner closure captures them) or reference
  themselves from one, and `destructure`'s `pvec`/`pmap` are passed `pb` as an argument and called from
  inside it, so all stay closures. Debug builds count direct calls (`clj_debug_direct_calls`).
  Triggers: a variadic helper in a profile (build the rest list from a buffer as `closure_run` does);
  a `letfn` (falls out as a `let*` of direct fns once forward references are allowed in the scan);
  a self-referencing `step` under `lazy-seq` in a profile (the thunk would need to reach the frame,
  which it outlives — that is a real closure).
  Decision: kept although no core.clj helper qualifies today (the gain is on synthetic rows only).
  Revisit when `for`/`doseq`/`letfn` exist: if their helpers fail the escape rule as the `step`s do,
  roll it back — the pass, the three node kinds and the frame link are self-contained (optimizer.c
  `direct_pass`, eval.c `eval_direct_call`/`eval_outer`, the codec forms), so the rollback is cheap.
- **The definition epoch** (epoch.h) is one process-wide counter bumped by every root bind (`def`,
  `defmacro`, boot, a host bind), every `extend`, every type creation (`deftype`, a reify site's first
  evaluation) and every `deftype` descriptor's death; `protocol-epoch*` returns it. The protocol call
  cache keys on it, and one bump anywhere invalidates every such cache in the process (they rewarm in
  microseconds, so there is no per-var epoch). Its load and bump are seq_cst so that a read inside a
  dispatch window pairs with a writer's bump-then-wait (Dekker), the same way the window pairs with
  the table publish. Meta changes do not bump it.
- **Immortal core roots.** Once core.clj is loaded, `clj_init` sets `CLJ_FLAG_IMMORTAL` on the root of
  every `clojure.core` var (natives, closures, protocols; type descriptors are skipped because
  `clj_is_user_type` reads the flag as "builtin"). Retain and release on them are no-ops, so
  `eval_borrowed` reads such a var at +0. A later `(def map ...)` in clojure.core "releases" the old
  root as a no-op — a bounded leak per redefinition, accepted — and binds an ordinary root, which reads
  owned. Roots bound after boot (user vars, a core var rebound from the REPL) are never immortalized.
- **A replaced fn root is released once the thread is idle** (`clj_eval_retire_root`, eval.c). A fn
  root is read at +0, so `clj_var_bind_root` cannot release the old fn while a body on this thread
  may still be running it or holding it as a borrowed argument: while a closure frame is up (shadow
  depth) or a `clj_exec_run` is active, the old fn is parked on a per-thread list, drained when the
  last of the two returns to zero (one compare on the pop path; a throw unwinds through the same
  exit). At the top level of a form the def is inside `clj_exec_run`, so `(f (def f ...))` parks too.
  Consequences: the parking is unbounded while the thread stays in flight — `(dotimes [i 1e6] (def f
  (fn [] i)))` inside a closure holds 1e6 fns until it returns; a thread that exits mid-flight leaks
  its list; a `def` on another thread against a running call is the unsafe race the "Concurrent
  `def`" entry describes, unchanged (the list is per thread, there is no reader window on calls).
  Trigger for a bounded variant: a def loop showing in memory; then a drain at every closure return
  whose frame holds no parked root, or the epoch-based reclamation the design names.
- **The optimizer pass** (optimizer.c, `clj_optimize`) runs inside `clj_analyze` after analysis and before
  numbering: it is where "immutable after analysis" begins, so what it produces is what serializes and
  what every evaluator and emitter sees; `clj_node_from_data` does not run it (its input is already
  optimized). Two rewrites: `INVOKE(VAR core-var, args...)` becomes `INTRINSIC {op, var, args}`
  when the head resolved to the var an intrinsics entry names and the arity is listed, and a consumer
  over a nest of lazy stages becomes a FUSED node (the fusion entry below). Both are keyed by the
  var, so a local `(let [+ -] ...)`, a user namespace's own `+` or `(apply + ...)` are untouched; a
  `(clojure.core/+ a b)` anywhere is rewritten. Then constant folding and the last-use marks (the two
  entries after the intrinsics table).
- **Intrinsics table** (intrinsics.h/.c): `{qualified name, arity, kind INTRINSIC_1/2/3, C function, pure,
  consume}` for `+ - * /` (2 args), `inc dec`, `< <= > >= = not= identical?` (2 args), `not nil? zero? pos?
  neg? even? odd?`, the type predicates, `empty? first rest next seq count`, `cons get(2,3) nth(2,3) conj(2)
  assoc(3) dissoc(2) disj(2) with-meta(2) contains? set?`; a side table resolved at boot holds each entry's var and
  native fn. The rule, kept by structure: the builtin bound to the same var calls the same function —
  single-arity builtins forward, variadic ones fold (`b_add` is a loop over `clj_add`), and the five that
  consume their collection at the core (`conj`, `assoc`, `dissoc`, `disj`, `with-meta`) list that core as their
  `consume` form, the table function being the same call after one retain (the builtin fn object points
  back at its consuming entry, `clj_fn.u.native.consuming`, so `clj_call_prepare` finds it in one load: `swap!`
  prepares per call). Consequences: `(+ a b c)` boxes a double at every step where the
  old accumulator did not, and a fixnum fold that overflows mid-way throws where the old one could
  recover (`(+ MAX MAX (- MAX))`); Clojure promotes both. `IntrinsicsTests` crosses every entry with
  sample values of every type against `clj_invoke` and pins that core.clj rebinds none of them. `==`
  is not an entry because no builtin exists; `str`, `hash`, `second`, `meta`, `apply` are left out
  (variadic with no fixed core, throw on unhashable types, composed, or not one function).
- **Intrinsic guard.** `eval_intrinsic` compares the var's root (relaxed load) with the boot fn from the
  side table before every call; on a mismatch it derefs the var and goes through `clj_invoke`, so
  `(def + ...)` in clojure.core or a host rebind is semantically invisible, and binding the boot fn back
  restores the fast path. The epoch would cost the same load and needs a cache to compare against; the
  guard needs none. Cost per intrinsic call: the arg evaluation, two loads and a compare, one indirect
  call — no frame, no arity table, no var deref, plus a load and a branch on the entry's `consume` form.
- **The specialized arithmetic node** (specialize.c, eval.c `eval_fix_*`, `eval_dbl_*`, `eval_fd_*`/`eval_df_*`;
  design §6b item 8, the first self-optimizing node; bench/RESULTS.md, "Specialized arithmetic" and "Specialized
  arithmetic over doubles"). `clj_exec_new` ends by deriving its tree under the process-wide dev store
  (`clj_specialize_store`: summaries with the caller join on, one lock, made on first use) and rewriting the exec
  entry of every INTRINSIC `+ - * / inc dec < <= > >= = zero? pos? neg?` by the kinds of its argument facts. Every
  argument int64 — fixnum or boxed long, never "fixnum" alone, which no loop variable is (the Facts entry above) —
  gives the fixnum entry: the fixnum tag of every argument and the boot-root guard are the whole check, then the
  operation runs inline on the untagged values with `arith2`'s overflow check and `clj_long_new`'s canonical re-tag
  (no `/`: an integer quotient may be a ratio). Every argument double gives the double entry: `clj_is_double` of
  every argument, then IEEE arithmetic into a fresh `clj_double_new` — `(/ 1.0 0.0)` is `##Inf`, every comparison
  with a NaN false, `=` included, exactly `arith2`, `compare2` and `double_equals`. A fixnum beside a double, the
  case the `Numbers` ladder makes a double, gives a mixed entry for `+ - * / < <= > >=`: one check of both tags,
  the fixnum converted as `arith2` converts; `=` between them is false by type and stays generic. Anything else,
  a boxed long included, takes `intrinsic_apply`, the generic path the plain entry runs. Two guards, each for a
  reason: the tag check makes a wrong or stale fact *slower, never wrong* — the host passes a double to a fn whose
  recorded callers pass fixnums, a caller at the REPL does, a `def` moved a join a moment ago — and the root guard
  keeps `(with-redefs [+ -] …)` visible, exactly as the plain entry does. A node whose operator var is rebound at
  derivation time takes the generic entry outright: its guard would fail on every call. The epoch the fact was
  derived under lives on the exec (`clj_exec_derivation_valid`: every var epoch the table read and every
  caller join it took, `clj_facts_valid`'s check) and is what re-derivation is *decided* by, not what a call
  checks: a call pays the tag bit and the root compare, nothing per epoch. The derivation also records the
  tree's fn-body sites in the reverse index; a var whose join that moved has its root closure's exec — or
  the recording exec itself, when it defines the var and the def has not run yet, or calls it recursively —
  re-derived from a worklist, at most 3 times per exec and 64 per trigger (the incremental interprocedural
  fixpoint: `(defn f [n] (f (dec n)))` settles in two, fixnum then int64), and the re-derivation writes
  every arithmetic entry afresh, which is how a stale specialization goes back to the generic entry: the
  fixture is a fn whose callers pass fixnums until a later `def` adds one passing a double, and
  `SpecializeTests` checks the entry and the results either side. **A root rebind pushes the same way**: the
  derivation indexes the exec under every var whose root it read (the dependents index, var → execs, kept with the
  derivation and dropped with it), and `clj_var_bind_root` ends with `clj_exec_root_rebound`, which queues the
  dependents on the same worklist under the same budget — so `(def inc …)` takes the entries of every user of
  `inc` back at the def, the boot fn bound again gives them back, and a redefined callee's callers read its new
  summary at once. `with-redefs` of an operator is two rebinds, so two pushes, each bounded by the 64 (a rebind of
  `+` under an interpreted core has hundreds of dependents; the rest keep their guarded entries). A dynamic var is
  not indexed: a thread binding is invisible to its epoch, so its root is no fact to rest on, and `*ns*` is rebound
  by every `ns` form. Cost of the push: +0.3 ms on the interpreted boot, nothing measurable on the corpus load.
  A rewrite from a re-derivation lands in a running exec as `clj_exec_count`'s does, at the next child dispatch,
  and `clj_exec_count(off)` puts the specialized entries back. Cost: the facts pass per exec, +3–4 ms on the 15 ms
  interpreted boot (the compiled core has no execs and pays nothing), under a second on the 26 s pool test suite;
  `clj_specialize_enable` turns it off (`CLJ_BENCH_NO_SPECIALIZE=1` is the bench's control). Measured, interpreted:
  the counting loop 15.3–16.2 → 12.6 ns per iteration with `(inc i)` alone specialized — `n` comes from the host
  and has no fact — and → 10.3 with the bound known from a def'd caller (`(< i n)` too); the accumulating loop
  24.9–25.2 → 18.0 and → 15.1; `swap! inc` 42 → 38 (the loop's `(inc i)`); a loop accumulating a double 36 → 28,
  what remains over the int64 loop being the `clj_double_new` per iteration a double result always is here; the
  accumulating loop with its bound from `(count v)` 26 → 16 (a count is a fixnum fact). `reduce +` does not move
  (5.6 ns): the reducer calls `clj_add` from C, there is no node. A dot product over two vectors through `nth`
  moves only by its counter: `nth` answers ⊤, the products are generic and their sum "a number" — trigger: an
  element fact for `nth`, which the lattice does not carry. What remains per iteration is the dispatch and the
  frame work the design names; the tag check is ~1 ns of the ~3 an intrinsic call cost.
- **Constant folding** (optimizer.c `fold_intrinsic`/`fold_if`; design §6b item 4): after the intrinsic
  rewrite, children first, an INTRINSIC whose entry is `pure`, whose arguments are all CONST and whose var
  still holds the boot fn is called at analysis and becomes a CONST; an IF whose test is a CONST becomes
  its taken branch (the branch's contents move into the IF node, which the parent already points at; a
  missing else is nil). Every accessor entry is pure (`empty? first rest next seq count cons get nth conj
  assoc contains?`): on the data a fold admits they realize nothing and consume nothing (`conj`/`assoc`
  see a constant at rc ≥ 2 and copy). What folds is bounded by the codec twice over: the arguments must be
  values the codec reads back as the same type (`clj_node_foldable`: nil, booleans, numbers, chars,
  strings, keywords, symbols, vectors, maps and lists of those — a lazy seq, a vector seq, a fn or a var
  embedded by a macro is no input), and so must the result (`(seq [1 2])`, `(rest [1 2])`, `(seq "ab")`
  keep the call: a vector seq or a string seq would read back as a list). A fold that throws (`(/ 1 0)`,
  `(nth [1] 5)`, `(+ 1 "a")`, an overflow) drops the exception and leaves the node, so the program throws
  at run time from the same node with the same message. A var rebound *before* analysis keeps the guarded
  INTRINSIC; a rebind *after* it does not unfold, the same speculation Clojure's `:inline` makes and the
  guard on the unfolded calls does not cover. `with-meta` is not pure (its result carries meta the codec
  drops, and on a unique value it is the value). The var meta of `(def x (+ 1 2))` and the frames of a
  runtime error come from nodes folding never touches (FoldingTests). Trigger for more: a fold rule over
  `str`, `list`, `vector` (variadic builtins are not intrinsics), or `let`-bound constants (needs a
  substitution pass, not a local rewrite).
- **Last-use reuse** (optimizer.c, the liveness pass; eval.c `eval_borrowed`/`eval_local`; design §6b
  item 4, the auto-transient; bench/RESULTS.md, "Last-use reuse" and "Growing a collection per step"): the
  last pass of `clj_optimize` flags a LOCAL read after which its slot is dead on every path
  (`clj_node.u.local.last`, serialized `[:local slot :last]`), and the evaluator then hands the frame's own
  reference to the consumer instead of borrowing it: the value enters the argument array with its owned
  bit set, the slot is niled and its owned bit cleared, so teardown, a recur's rebind and a debug reader
  see nothing there; a slot the frame only borrows (a fixed param, the self slot) reads as before. A
  consuming intrinsic whose collection the site owns — a last-use local, or a nested result such as the
  inner `(conj (conj v 1) 2)` — calls the entry's `consume` form and drops the bit from the mask, so
  `clj_conj`/`clj_assoc_owned`/`clj_dissoc_owned`/`clj_disj_owned`/`clj_with_meta` see rc 1 and update in place (this is
  the first in-place store reachable from interpreted code: the RC entry's unchecked "children of a shared
  object are shared" trigger has fired). Liveness is backward over the evaluation order of one frame (the
  top level, each fn arity, each direct fn arity), on bitsets of the first 64 slots (a higher slot is
  never marked), with these rules: a `loop` body and a fn body with a `recur` are a fixpoint, a recur's
  live-out being the body's live-in minus the slots it rebinds, so a loop var is a last use where nothing
  reads it later on its path — the recur arguments included, `(recur (conj v x) (inc i))` — and a local
  bound outside the loop is live across the recur and dead only on the exit path; `if` branches are
  separate paths (`(if t (conj v 1) v)`: both last); a `let` kills its slot before its init (a shadowing
  `let` is another slot); a closure capture is a read at the closure's creation and its body a frame of
  its own (a captured value is at rc ≥ 2 by the time the definer's last use runs, so the core copies); a
  direct fn body reads the definer's slots through the static link whenever it is called, so every such
  slot is pinned live for the whole defining frame (the definer never hands one over, `(let [v [1] f (fn
  [] (count v))] (let [w (conj v 2)] [(f) w]))`); a `try` body keeps everything its handlers and `finally`
  read live at every point, since any point may throw, and the catch slot is a frame slot like any other;
  a direct local operand of a call or a literal is borrowed until the call completes, so nothing inside a
  later operand may hand that slot over (`(assoc acc i (conj (nth acc i) x))` reads `acc` at +0 in the
  first operand and must not free it in the third: the slot is *held*, not live, so the sets stay exact
  and only the mark is withheld). Only reads whose consumer can own the value are marked: the collection
  of a consuming intrinsic, a let init, a recur argument, a body's value; an `inc` argument, a literal
  item, an `if` test or a call argument is marked nothing, since the hand-over costs the slot write, the
  mask bit and the release loop for a +1 nobody uses (measured on the counting loop: every last use marked
  cost +2.5 ns per iteration, call arguments alone ~2 ns per call). The mark is a flag inside the LOCAL
  case of `eval_borrowed`, force-inlined in optimized builds: a node kind of its own dispatched through
  the exec table cost +3 ns per iteration, and the flag with the inlining left to the compiler +5.5 (it
  stopped inlining and emitted a call per borrowed read). Marking is one final pass per frame with the
  converged sets; each loop's fixpoint re-runs the loops inside it, ~3^depth passes over a body.
  The drivers: `clj_call_prepare` records the consuming entry when the fn is its boot builtin, and
  `clj_reducer_step`/`step_kv` and the fusion bottom hand their own +1 to it and take the result back as
  the new one (`(reduce conj [] xs)`, `(reduce-kv assoc {} m)`, `(reduce conj [] (map f xs))` fused), so
  the accumulator grows in place from the second element on (the init and a seed are shared with whoever
  passed them: one copy). `(into to xform coll)` runs through the `fused-into*` driver under `[xform]`, so
  it matches `(vec (map ...))`. Still copied, each with a reason: a user fn as the reducing fn (its param
  is borrowed from the reducer: `(reduce (fn [a x] (conj a x)) [] xs)` copies every step — a hand-over
  there would need the callee's frame to own the param, the call-argument variant above); `conj` through
  `apply` or any native other than the builtin itself (`clj_apply`'s `all[]` is +0); a collection held by a
  var (`(def v [1])`, `(conj v 2)`: the var's root is read owned at rc ≥ 2); a `(conj v x)` inside a `try`
  whose handler reads `v`; a call argument (`(f v)` then `(conj x 1)` in `f`: the param is borrowed); the
  `to` of a fused `(into to P)` (the driver retains it once: one copy per form, then in place); a
  captured or var-held value, by rc. Triggers: a profile with a collection built through a helper fn per
  element (mark call arguments, ~2 ns per call); `transduce` with a user rf over a collection (the same
  +0 rule); a frame past 64 slots growing a collection (the bitset).
- **The fusion pass** (optimizer.c, fusion.c, `CLJ_NODE_FUSED`; bench/RESULTS.md, "Fusion"): `(reduce
  f [init] P)`, `(into to P)`, `(vec P)` and `(count P)`, where `P` is a nest of `map keep filter
  remove take drop take-while drop-while mapcat map-indexed keep-indexed interpose dedupe distinct` calls — each
  at its lazy arity, `map`/`mapcat` with one coll, every head resolved to the `clojure.core` var — over
  any source, become one FUSED node: the argument expressions (the consumer's, then each stage's own
  from the consumer outwards, then the source) evaluated once in the original order into a frame of
  their own, a guard, and two programs over those locals — the fused one calls a driver native with
  the stages' transducer arities in a vector literal (`(fused-reduce* f coll [(map g) (filter p)])`,
  `fused-into*`, `fused-count*`; `vec` is `fused-into*` onto `[]`), the original one is the consumer
  call as written. Serialized as `[:fused [vars] [args] fused original]`; the two programs' slots are
  bounds-checked against the args, not the enclosing frame. The guard: every core var the two programs
  name (consumer, stages, driver) still holds the root it had when core.clj finished loading, recorded
  by `clj_fusion_install` in a static table the node points into, as an INTRINSIC points into the
  intrinsics table — relaxed loads, no exec-side state, no epoch, and nothing to recompute for an exec
  built after a rebind; `(def map ...)` in clojure.core or a host bind sends the site down the original
  program, binding the boot root back fuses it again. The drivers keep the accumulator in C and hand
  the transducers `nil` as `result` (and `(into to xform coll)` runs the same driver, above), so the seq rules of `reduce` hold exactly: a 2-arity seeds with
  the first *output* and answers `(f)` when there is none (an `eduction` would seed with `(f)` and
  break `(reduce (fn [a x] ...) (map ...))`), a reduced init or first element is data, only `f`'s own
  reduced result stops the walk (a `(reduced nil)` the stack passes up); `fused-into*` conj's an
  accumulator only it holds, so a vector grows in place. Deviations from the lazy form, both in
  `FusionTests`: the driver seqs the source up front, so `(reduce + (take 0 5))` throws where the lazy
  `take` never touched the `5`; and `partition-all` is not a stage, because its transducer emits
  vectors where the lazy arity emits seqs and `conj` on a 2-arity seed tells them apart (trigger: a
  `(map seq)` tail behind it once `counted?` and the type name in error messages may differ). Not
  fused: a multi-coll `map`/`mapcat`, a head that is a local or another namespace's var, a pipeline
  consumed by anything else (`first`, `seq`, `doall`, a value position), a consumer whose coll is not
  a stage call; a pipeline as the source of another is fused on its own. core.clj itself is analyzed
  before the table exists, so nothing inside it is fused (the validator re-analyzes it after boot and
  sees FUSED nodes; they round-trip). No composition cache: the transducer stack is rebuilt per
  evaluation and the whole per-form cost is ~0.3 µs at n = 10, of which building `(map g)` is ~15 ns —
  the `(xf rf)` application must be fresh per run anyway (stateful transducers) — so the design's
  exec-cell cache (CAS fill, immortal winner) has nothing worth its guard. Triggers: a profile with
  fused forms in a hot loop over tiny collections (the per-form cost); consumers `some`/`every?`/
  `run!`/`doseq` (a reduce with early exit); the barriers `sort`/`group-by` (cut a pipeline today);
  multi-coll `map` (a multi-source driver); `partition-all` (above).
- **C stack per Clojure call is large.** A call is several C frames with slot and argument buffers on
  the stack (the direct path inlines the frame setup into `eval_invoke`, whose 16-slot buffer is the
  callee's frame; the generic path adds `closure_run` with its own 16 slots): on the order of 0.6 KB
  in a debug build, ~2.3 KB under ASan, ~3.7 KB under UBSan, measured before the direct path. On Swift
  Testing's 512 KB threads that is ~600 / ~170 / ~100 nested non-tail calls before the guard throws
  "Stack overflow" (the guard reads the thread's real bounds on Apple platforms; elsewhere it assumes
  512 KB). Fix: frames on the heap and fewer C frames per call (the shadow stack records frames, it
  does not hold them). Tests keep non-tail recursion depth ≤ 50.
- **The stack guard has no host fallback.** `pthread_get_stackaddr_np` is Apple/BSD; other platforms
  get a fixed 512 KB assumption measured from the first call, and the guard page of compiled code
  (guard.c, `getsectiondata`, the Mach-O `__cljframe` section) is Apple-only outright: elsewhere a
  compiled overflow is a plain crash and traces carry no compiled frames. Trigger: a Linux port.
- **Vars are immortal.** Every `def` of a new name leaks a var, its name symbol and string for the
  life of the process, as do namespaces; tests declare their vars before taking live-object baselines.
- **Analysis error messages are capped at 512 bytes** (`fail` formats into a fixed buffer): a huge
  unresolved form is truncated in the message.
- **Nodes are pool objects with a 14-arm union**, so a `const` node pays for the fn arity table.
  Trigger: memory of a large loaded program. Fix: per-kind sizes via `clj_alloc(size)`.

## Facts (Sources/CljCore/facts.c, summary.c, include/clj/facts.h, summary.h)

- **A side table, built on request, never on the way through.** `clj_facts_of(root)` walks the analyzer's
  optimized tree and returns `clj_facts`, `nnodes` entries indexed by node id exactly as `clj_exec` is.
  Nothing calls it: `clj_analyze`, `clj_exec_new` and the compiler are untouched, so the pass costs zero
  until a consumer asks. The tree stays `const` — no fact is written into a `clj_node` — and the table is
  plain `malloc`, not a pool object, so building facts never perturbs the allocator a test is counting.
  The root is retained, because a singleton fact borrows the constant it names.
- **Pure by construction.** `clj_facts_of` reads no var root, no epoch, no protocol table and no thread state,
  so the same tree always yields the same table (`pureAndSideTableOnly`). That is what makes a fact cacheable
  beside a serialized tree later, and it is also the one rule that decides every "⊤ or not" question below.
  `clj_facts_of_with(root, store)` is the same walk with a summary store consulted at call sites and var reads
  (the bullets from "Summaries" on); such a table is runtime state, records every var it rested on with its
  epoch, and `clj_facts_valid` answers false once any of them was rebound.
- **The lattice.** A type fact is a set of 27 kinds — nil, bool, fixnum, boxed long, bigint, ratio, decimal,
  double, char, string, keyword, symbol, seq, vector, map, set, sorted-map, sorted-set, record, array, fn,
  var, atom, uuid, inst, regex, host — plus an optional singleton (the constant itself), an array element
  kind and a record/host descriptor. ⊤ is every bit, ⊥ none; `join` is `|`, `meet` is `&`. Sorted maps and
  sets are separate bits so that the false branch of `(map? x)` may subtract exactly what `map?` answers
  true for. A set of more than four members widens to ⊤ (design §3, "полиморфизм ≤4 shape'ов"), the six
  numeric kinds counting as one member — the design's ladder is int/double/number/⊤, so `(+ a b)` must be
  able to say "a number" rather than collapse.
- **Nullability is its own two-bit lattice** (`never`, `always`, `maybe` = ⊤, ⊥), joined and met alongside
  the type. It is not derived from the nil bit because the union cap destroys that bit: `(if x …)` leaves
  the true branch with ⊤ minus nil, which is 26 kinds and widens straight back to ⊤, while the separate
  nullability survives as `never`. Over library code the type is known for 50 % of value nodes and
  nullability for 51 %, and the second number is the one that holds up where the first does not.
- **Escaping is per local slot, per frame**, three values joined by max: `local`, `captured`, `escapes`.
  A frame is the top level, one fn arity, one direct fn arity or a fused node's argument frame; the table
  records each with its body's id range, so `clj_facts_frame_of(id)` finds the innermost. A slot escapes when
  it is an argument of any call the signature table does not mark as storing nothing, an item of a vector,
  map or set literal, a `def` init, a thrown value, a fused argument, or the value of its frame's body
  (returned). A closure capture marks it `captured`, and so does a read through the static link (an `OUTER`
  node in a direct fn body, or a closure made there capturing an outer slot): the slot is read by address,
  which is what bars its promotion to a C variable, so the definer's frame is charged, `depth` links up.
  `(let [b a] …)` records an alias edge and the escape of `b` flows back to `a` at the end of the frame.
  Unknown is `escapes`. The three levels are joined by max, so a consumer cannot tell "the value leaves"
  from "the slot is read by address" once both happened; a bitset would, and the trigger for it is the
  compiler needing the distinction (it does not today: `escapes` bars promotion as well).
- **Sources of a type fact.** Constants (a singleton of their kind, by pointer identity, so two literals the
  reader made separately stay two singletons, exactly as the codec keeps them); vector/map/set literals and
  fn literals; the signature table below, for intrinsics and for named C builtins; `let` bindings; `loop`
  variables (the widened join of the init and every recur argument); `try` (the join of the body and every
  handler); a variadic rest parameter (a seq or nil); a closure capture (the fact of the captured slot at
  the moment the closure is made, which is sound because a capture happens once); the self slot of a
  named fn (a fn). Everything else is ⊤.
- **Refinement is on predicates, not on `if`** (design §3, occurrence typing). A predicate call over a local
  yields a `refinement` — the target slot, the narrowed fact and whether the predicate is true for *exactly*
  that set, which is what lets the false branch subtract rather than only the true branch meet. `nil?`,
  `some?`, `string?`, `map?`, `set?`, `vector?`, `number?`, `integer?`, `fn?`, `record?` and the rest carry
  one; `not` flips it; `=` against a constant pins the singleton; `instance?` refines against the canonical
  builtin type names of proto.c. A bare local as the test refines on truthiness alone (only nil and false
  are falsy). `when`, `cond`, `and`, `or`, `if-let`, `when-let` are macros over `if` and `let*`, so they need
  no rule of their own — with one catch that cost a bug: `(and (map? x) …)` expands to `(let [t (map? x)]
  (if t …))`, so the test of the `if` is the temporary. A `let` binding therefore records the refinement its
  init carries, and a test that reads such a slot applies it; rebinding a slot retires every refinement taken
  over its old value.
- **Loops: fixpoint, then widening after N = 3 rounds.** A loop variable starts at its init and joins every
  recur argument; the body is re-run with recording off until nothing changes. N is 3 because the height a
  variable can climb without widening is singleton → one kind → two → three → four → ⊤, and the loops that
  matter settle in one: `(loop [i 0] … (recur (inc i)))` reaches fixnum|long on the first round and stops on
  the second. Three rounds leave one round of headroom for a join that alternates, and bound the work at
  three passes over a body — the same order as the liveness fixpoint next door in optimizer.c. Anything still
  moving after the third round goes straight to ⊤, which terminates by construction. Over the whole corpus
  the rule fires on nothing; the test that exercises it rotates four values of four kinds through one loop.
- **What pass 1 alone leaves at ⊤, and why.** Every `INVOKE` whose head is not a core var the signature
  table names, every `DIRECT_CALL`, every var read, every `OUTER` read, every fn parameter — pass 1 has no
  function summaries and may not read a var's root. The summaries below take the calls, the var reads and the
  direct calls (over library code known types go 50 → 69 % of value nodes, 30 → 57 % of computed ones,
  docs/facts-coverage.md). What stays ⊤ after them: a **parameter's own fact** — a summary constrains a
  parameter by requirement (what the body needs), never by what callers pass, so `(inc n)` on a parameter is
  "a number", not a fixnum; the caller join below is what closes that from the other side. `OUTER` reads,
  captured slots of a callee (⊤ inside its body), a `(:k m)` on a parameter, and everything behind `deref`.
- **⊥ means one of two things, and they are told apart.** A meet that contradicts bumps `clj_facts_conflicts`
  and the branch below it is marked `unreachable` on every node with the cause (`clj_dead`): `CLJ_DEAD_LITERAL`
  when the test decides on a pinned value (`literal_test`: the slot it reads or refines holds a singleton or is
  exactly nil — a let-bound literal, an `and`/`or` temporary, a var whose root is nil —, or the test is `(= x
  <const>)`), `CLJ_DEAD_REFINED` when two refinements alone exclude each other; a node is legitimately ⊥ when a
  `throw` or a `recur` is the only way out of it. Anything else — a ⊥ value node that is neither in a literal's
  dead branch nor behind an exit — is a wrong signature in this file: `make facts-report` counts it per library
  ("Dead branches" in docs/facts-coverage.md, the unexplained column computed by the pass's own class, not by a
  list of known sites) and fails on any, as does `FactsTests.noContradictionOverCore`. That counter is what
  found the two bugs this pass shipped with: the complement of a `maybe` nullability subtracted everything, and
  a loop's fixpoint rounds counted conflicts against variables not yet widened. Over the corpus the 64 conflicts
  are all `dead-branch` (`(and nil true)`, `(when-let [x [0 1 2]] …)`, `(ratio? x)` after `(= 1 x)`, `(or
  *assertion-pos* …)` with the root nil).
- **Signatures live in facts.c, not in the intrinsics table.** `clj_intrinsic` is the C-call contract the
  compiler emits against and the differential test crosses; hanging a lattice column on it would tie the ABI
  to the fact kinds and force every future fact into that struct. More decisively, half of what is worth
  annotating — `str`, `keys`, `vec`, `re-pattern`, `int-array` — has no intrinsics entry at all, so only a
  table of its own can hold both. It is keyed by the unqualified name in `clojure.core` plus the arity, with
  rules for arithmetic (`fixnum + fixnum` is fixnum|long, since an overflow throws rather than promoting;
  a ratio operation may normalize back to an integer; `/` on integers may answer a ratio), for `conj`/`assoc`
  (the collection's own kind, `nil` included), and for the identity-on-type calls (`into`, `with-meta`,
  `empty`). Matching on the *var* rather than its root is a speculation: `(def str …)` in `clojure.core` would
  make `(str x)` no longer a string, where an intrinsic node has a runtime guard and this has none. Nothing
  reads the table yet, so the speculation costs nothing today; trigger for fixing it: the first consumer that
  changes generated code on a builtin signature, which then needs the same boot-root guard `eval_intrinsic`
  makes.
- **The table's shape and cost.** 24 bytes per node (type set, nullability, array element kind, unreachable
  flag, singleton, descriptor) plus one byte per frame slot and one `clj_fact` per loop variable: 337 KB for
  all 264 forms of core.clj, 13 KB for its largest single form, 262 KB for the largest form in the corpus
  (a `deftest` with hundreds of assertions). Building it costs 0.31× the analysis of the same forms for
  core.clj and 0.10× for medley (bench/RESULTS.md, "Facts pass"); the ratio falls as forms grow because
  analysis pays for macroexpansion and the facts pass does not.
- **Summaries (summary.c): pass 1 bottom-up over var roots.** A `clj_summary` is one arity of one function:
  what the body *requires* of each parameter, the result fact, the effect set, and where each requirement
  came from (the line and column of the use, for the two-position message). It is computed by walking the
  arity's body once with the parameters at ⊤ (`clj_facts_walk_arity`, the same walk with recording off, so
  nested closures are not entered and nothing is stored) and is keyed by the var for a var-bound fn — the root
  is read, and a closure root carries its fn node — and by the arity's node identity for a direct fn (a
  let-bound fn only ever called). A native without an annotation has no summary; a compiled closure
  (`-DCLJ_COMPILED_CORE`) is a native too, so under the compiled core every core fn's summary is its annotation
  alone. Summaries are borrowed from the store and valid until the next call into it; direct-fn entries are
  dropped at the end of every table (`clj_summaries_forget_arities`), because a pointer into a tree must not
  outlive the table built over that tree.
- **Requirements: the meet of the uses along a path, the join across branches.** The environment carries a
  `req` per slot beside the fact. A call whose callee has a requirement for the position meets it into the
  argument's slot; `(if t a b)` joins the two branches' requirements, so `(if flag (inc x) (name x))` requires
  "a number or an ident or a string" of x and `(defn f [x] (if (string? x) (subs x 1) (inc x)))` requires
  nothing that the refinement did not already prove — the join, not the meet, is what keeps a use inside one
  branch from becoming a false conflict for the other. A `try` body requires nothing past the try (a throw
  skips the rest), a loop body's requirements on the outer slots hold (it runs at least once), a rebound slot
  forgets. A requirement keeps every kind it names — `clj_fact_meet_wide`, no union cap — because "seqable"
  is ten kinds and would widen to ⊤ as a fact; it is never stored on a node, only met against one, and the
  meet's result is capped as usual. So `(count 1)` is a proven conflict although no node can hold "seqable".
- **The fixpoint and its bound.** A call of a var whose entry is being computed answers the entry's optimistic
  value (result ⊥, no requirement, no effects) and marks it recursive; the root of the cycle then re-walks
  until the summary stops changing, at most N = 3 rounds, then widens (parameters ⊤, result ⊤, effects
  everything). N is the loop rule's N for the loop rule's reason: the height a fact can climb is singleton →
  one kind → two → three → four → ⊤, so one round settles the common recursion (`(fact (dec n))`: the result
  is a number on round one and stays), and three bound the work at three walks of a body. Entries computed
  *above* a fixpoint in flight (mutual recursion: `odd?` inside `even?`'s round) rest on an optimistic value
  and are transient — not cached, recomputed by the root's next round — or `odd?` would be cached as
  "always false" from the round that saw `even?` at ⊥. A finished summary never answers ⊥ (a body that never
  returns normally is ⊤ at call sites, so the ⊥ watchdog keeps meaning "the lattice is wrong") and never a
  singleton (it would borrow a constant of the callee's tree, which only the callee's root keeps alive).
- **Budget: the walk depth, not only the nesting.** A nested summary walk shares the C stack with the walk that
  asked, and a sanitizer build's frames are large (UBSan overflowed at ~45 nested nodes over the 512 KB of a
  test thread). So: every var a tree calls is summarized *before* its walk, at the top of the stack
  (`warm_summaries`), at most 6 summary computations nest, and a summary walk stops descending past 40 nodes
  of total depth (`CLJ_FACTS_MAX_WALK_DEPTH`) and answers ⊤ for the subtree — the recording walk is never cut, and
  neither are its loop-fixpoint rounds nor the self walk of the caller join (`pass.bounded` marks the store's walks
  alone; the first version keyed the cut on `!record`, which cut a recording walk's own rounds at depth 40).
  A summary can therefore depend on how deep it was first asked for; the warm phase makes the first ask
  shallow for everything a tree names directly.
- **Pass 2: at every call site the caller's fact meets the callee's requirement.** The meet is the argument
  node's fact from then on (and the slot's, when the argument is a local), which is how a receiver, a
  `(keys m)` and a `(zero? n)` narrow the parameter behind them for the rest of the body. A meet down to ⊥ is
  a *proven conflict*: recorded on the table as a `clj_diagnostic` with both positions — the argument at the
  call site and the use inside the callee that imposed the requirement (or "requires", when it came from a
  declaration or a protocol table) — rendered by `clj_diagnostic_message` as "user/sum-need uses argument 0
  as nil|map|sorted-map|record at 3:14, vector is passed at 1:8". The argument keeps the caller's fact:
  storing ⊥ would trip the watchdog, and the watchdog is worth more.
- **Diagnostics: the ladder of design §3 "Строгость", by strength of knowledge.** One struct, two severities.
  *Errors:* a call-site ⊥ (`CLJ_DIAG_CALL_CONFLICT`) and a `:=>` declaration the body contradicts
  (`CLJ_DIAG_DECL_CONFLICT`) — a runtime failure shown early, in every mode. One refinement of the rule: a
  site ⊥ inside a `try` body with a handler is the failure the code expects, not one it suffers (`(is
  (thrown? (keys 1)))` is the corpus's whole population, 35 sites), so it is reported at warning severity with
  `caught` set. *Warnings, on by default, off per namespace:* ⊤ meeting a *declared* requirement
  (`CLJ_DIAG_TOP_INTO_DECL`) and a body answering ⊤ where its declaration promises something
  (`CLJ_DIAG_TOP_RESULT`) — a declaration is an explicit ask to be told this. ⊤ against an *inferred*
  requirement is silent (`:strict` would turn it on; not built). The switch is the namespace's meta:
  `{:facts/warnings false}`, which the `ns` form's attr-map sets (`(ns app.core {:facts/warnings false} …)`)
  and `alter-meta!`/`reset-meta!` on a namespace object change; namespaces got a meta slot for it, as they
  have in Clojure. Over the corpus the declarations on 17 core vars raise 82 such warnings — `(inc x)` on a
  parameter is "nothing is known about what is passed" — which is the cost the design names of declaring a
  builtin: the ask is the core author's, the warning lands on the caller; the per-ns switch is the answer it
  gives. Nothing halts on any of this: `make facts-report` exits non-zero on an error (the corpus gate) and
  that is all; wiring an error to stop a load or a compile is a later trigger, once the gate has been green
  long enough to trust the lattice. The table's `clj_facts_nerrors` and the store's `clj_summaries_nerrors`
  are what a consumer would gate on.
- **The signature table must be sound, not merely precise: a false ⊥ is now a false error.** Audited with
  that eye, these entries were weakened: `list*` answers seq or nil (`(list* nil)` is nil); `map`, `mapcat`
  and `partition-all` are seqs at two arguments or more only (one argument is a transducer, a fn); `into`
  follows the conj rule (`(into nil xs)` is a list, not nil); `dissoc` on a record answers record or map (a
  basis key drops the record); `empty` answers nil for anything that is not a collection, a string included;
  `assoc-in` answers a map or a vector; `int?` is exact on fixnum and long only (`(int? 1N)` is false, so the
  false branch must not subtract bigint); `sequential?` and `coll?` are no longer exact (a queue is both and
  is kind seq). Everything else stood: the numeric rules already promote through ratio and decimal, `/` on
  integers may answer a ratio, and the kind of a deftype that implements `IFn` is host, not fn, so `fn?`'s
  exact refinement holds. The corpus gate (`make facts-report`, `noContradictionOverCoreWithSummaries`) is
  what keeps this true from here on: zero errors over core.clj, the embedded libs and both corpora.
- **Var reads and roots, guarded by a var epoch.** `clj_var` counts its root binds (`clj_var_epoch`, 0 while
  never bound, bumped by `clj_var_bind_root`); the summary layer reads roots and metas freely and records
  every var it read, with the epoch it saw, on the entry being computed and on every entry in flight below it
  (a caller's summary rests on what its callees read), and on the facts table. `clj_summary_of_var` recomputes
  an entry when any of its recorded vars moved; `clj_facts_valid` answers false for a table in the same case.
  A var read in a walk with a store answers the *kind* of its root and its descriptor, never the singleton —
  the root may be rebound and only the epoch guards it, and a consumer that constant-folds a root is a bigger
  speculation than the design's inline-cache-with-epoch. What invalidation does today, exactly: a `def` bumps
  the var's epoch and the process epoch, nothing else; no store is told, no table is told. The next lookup of
  that var (or of a summary that read it) recomputes that entry alone; a table built before is still readable
  and `clj_facts_valid` is what a consumer must check before trusting it. The interpreter therefore stays
  incremental (a `def` costs one increment), and there is no whole-program recompute anywhere yet — the
  compiler's closed world would run the same store over the whole set once. Past 24 recorded vars an entry
  falls back to "valid while the process epoch stands", which every `def` moves; a protocol method's entry
  rests on the process epoch by construction.
- **Protocol receivers: the requirement is the join of the implementors' kinds.** A protocol method's summary
  requires of its receiver the join of the kinds of every type in the protocol's tables: the immortal ones are
  enumerated from proto.c's side table (`clj_proto_each_immortal`), and user types are two counters on the
  protocol (`user_types`, `user_records`, bumped by every extend of a deftype or a record, defrecord's initial
  extends included; record.c sets the record bit before those extends so they count right) — the live ones
  are also enumerable (`clj_proto_each_user`, the compiler's CHA), but the counters stay the summary's source,
  since a dead type's extends still say what the protocol was written for. One deftype implementor is therefore "host", one kind: known. `Object` or a core interface
  extended, or nothing found (a reify-only protocol: its types are user types too, but the counter is what
  keeps them), makes the requirement ⊤. The corpus's 26 receiver sites all go known this way, 16 of them
  `defmethod` expansions where the receiver is the multimethod's var (a var read: host, MultiFn's descriptor)
  and the rest parameters narrowed by the method's requirement (`(-add-method mf …)` inside a `defmulti` helper).
  `clj_facts_kind_of_type` says host for any deftype whatever interfaces it implements: `(fn? x)` is false on
  a deftype with `IFn`, so it must not be a fn.
- **Records: `(new* T …)` and `(record-map* T m)` on a type's var answer the kind with the descriptor.** The
  constructor bodies `defrecord` and `deftype` expand to; with a store the walk reads the var's root, and when
  it is a user type the result is record (or host) with `desc` set. `->Foo`'s summary is thus "a Foo", and
  `(let [m (->Foo 1)] (:k m))` sits on a known record (`SummaryTests.recordConstructorResult`). The corpus has
  no such lookup — its 69 `(:k m)` sites are 58 on locals (parameters, constrained by requirement only, and
  `(:k m)` requires nothing) and 11 below derefs and other calls — so the report's record row stays 0 → 0 for
  want of a site, not of a mechanism.
- **Effects, as far as they fall out.** Four bits, `alloc`, `throw`, `io`, `atom` (atom-write), joined up the
  walk: a vector, map, set or fn literal allocates; `throw` throws; a `def` is everything (registration); a
  known core call takes `clj_facts_core_effects` — nothing for a predicate, `not`, `identity`, `boolean`,
  `meta`, `type`; io for `print`/`println`/`slurp`/…; atom for `swap!`/`reset!`/`alter-var-root`/…;
  alloc|throw for the rest of the named list; everything for anything unnamed — and a callee with a summary
  takes the summary's. A closure body's effects are its own, not its definer's. Nothing reads them yet; they
  cost one `|=` per node.
- **Declarations: `:=>` meta on the var, the vocabulary of design §3, one mechanism.** A var may carry
  `{:=> [:=> [:cat arg-schema …] ret-schema]}` in its meta — the value is a complete Malli function schema, the
  same data `m/=>` takes, spelled either in a defn's attr-map (`(defn vec {:=> [:=> [:cat :any] :vector]}
  [coll] …)`, three of core.clj's defns carry one) or set by `alter-meta!` for a builtin that has no defn
  (the one table at the end of core.clj, 14 vars). `clj_fact_of_schema` is each tag's abstract
  interpretation: `:int` → {fixnum, long, bigint}, `:number` all six, `:map` map|sorted-map|record, `:set`
  both sets, `:seq` seq, `:boolean`, `:string`, `:keyword`, `:symbol`, `:vector`, `:fn`, `:nil`, `:any` ⊤;
  `[:maybe X]` X ∪ nil, `[:or …]` join, `[:and …]` meet, `[:= x]` and `[:enum …]` singletons (kept only for
  an immediate or a keyword, which no tree owns), `[:vector …]`/`[:tuple …]`/`[:map …]`/`[:set …]` their kind
  (the children describe elements no fact holds yet), `[:fn pred]` ⊤, `[:=> …]` fn; a properties map after the
  tag is skipped; `[:* X]` in the `:cat` ends the fixed arguments. An unknown tag is ⊤ and never an error,
  so a schema the vocabulary outgrows degrades, it does not break. The declaration meets the inferred
  summary; the inferred one is still computed and, for a visible body, decides — for an opaque one (a native,
  a compiled closure) the declaration is trusted. `clj_fact_to_schema` is the total embedding back (a kind
  set as `[:or …]` of the widest tags that fit, nullability as `[:maybe …]`, a singleton as `[:= x]`), and
  `signatureTableRoundTrips` projects every entry of the signature table through it and back: 154 of the
  201 entries round-trip, 16 are transfer functions (`+ - * / quot rem inc dec conj assoc into with-meta vary-meta
  dissoc disj empty`: the result is computed from the arguments, which is a function in the result position
  of `:=>` — out of scope here, the trigger is the comptime evaluator of design §3), and the rest name the
  **vocabulary gaps**, kinds without a tag: *array* (the twelve array constructors, `aclone`, `to-array`,
  and the reason `count`, `nth`, `next`, `rest`, `seq`, `vec` declare `:any` where they mean "seqable" — a
  narrower declaration would be a false error on `(count (int-array 3))`); *fixnum alone* (`count`, `hash`,
  `compare`, `alength`: `:int` reads back as three kinds, so the unboxing prize cannot be declared, only
  inferred); *a plain map or set alone* (`hash-map`, `zipmap`, `frequencies`, `group-by`, `hash-set`, `set`:
  `:map` and `:set` read back with the sorted kinds and records); *sorted-map*, *sorted-set*, *atom*, *char*,
  *regex*, *uuid* — 31 entries. Each is a tag the vocabulary would need, not a second mechanism. Measured
  (docs/facts-coverage.md, "declarations alone"): they move no known-type percentage, they take pass 2's
  narrowed arguments from 9 to 99 and its proven throws from 0 to 35, because inference alone has no
  requirements at the leaves.
- **The reverse index and the caller join (callers.c): the closed-world direction as a dev-mode fact under
  an epoch** (design §3 "Проход 2"). The recording walk lists every call site of a var with the facts of what
  it passes (`clj_facts_site`, before the callee's requirement narrows them) and every read of a var as a
  value (`clj_facts_value_read`: an argument, a capture, `#'f`, the fn of `apply`), each flagged `in_fn` when
  it sits in a fn body. A consumer puts them into the process-wide index under an *owner* — the exec of the
  tree for the interpreter, a token per form for the report tool — and the owner's death takes them out
  (`clj_callers_forget`, from the exec's finalizer), so the index is what the *live program* passes: a tree
  that ran once and died is gone with its sites, a redefined fn's old body with its. The interpreter records
  only `in_fn` sites: a call at the top level of a form runs once, like a call from the host, and a fact
  about the program must not flip with every REPL line (the first version recorded everything, and reading
  `f` as a value at the REPL de-specialized `f` for good). Every change bumps the var's **callers epoch**
  (`clj_var_callers_epoch`), the second epoch beside the root epoch: redefining `f` bumps its root epoch,
  which invalidates every summary and table that read `f`'s root; adding or losing a caller of `f` bumps its
  callers epoch, which invalidates only the join `f`'s own body was derived under. A table built with a store
  that has `clj_summaries_use_callers` on enters the parameters of a `(def f (fn …))` at the **join** over
  the recorded sites that resolve to that arity (`clj_callers_join`, the evaluator's own arity choice: a
  site with n arguments feeds the fixed arity n, else the variadic one, whose rest parameter takes nothing),
  kinds and nullability only — no singleton, which would borrow a caller's constant, and no descriptor,
  which may die — and records the join with the epoch it was read under (`clj_facts_join_at`, checked by
  `clj_facts_valid` beside the var deps). **A fn's own site is no site of the index: it enters the entry's own
  join.** `(defn fact [n] (if (<= n 1) 1 (* n (fact (dec n)))))` recorded as any other site would put `n` at ⊤ into
  its own join before any caller exists — "a number" — and every later join would rest on that, which is exactly the
  poison the first version had (`fact` never got a worker). So a call of the def'd var from the arity's own frame,
  resolving to that same arity (`self_site`: same var, `in_fn` at the frame's depth, `clj_facts_arity_for`; nested
  fns, direct fns and fused programs are other frames and stay recorded), is dropped from the table's site list in
  every mode, and `enter_join` closes the join over it: `callers` is what the index answered over the external
  sites, `params` starts there and, when some position is narrower than ⊤ and the body has such a site
  (`scan_self_sites`), takes in what the self-sites pass under it — the body walked again with the parameters at the
  join, recording nothing, entering no nested fn (`self_sites_join`, a `pass` with `self_join` set) — until nothing
  moves; a position still moving after `WIDEN_ROUNDS` = 3 goes to ⊤ and every round past that widens what moved, so
  the loop ends within `nparams` more rounds (`self_fixpoint`; `self_rounds`, `self_widened` on the join). `fact`:
  the external join is a fixnum, the self-site under it passes int64, under int64 it passes int64 — two walks. A
  self-site passing ⊤ (`(f @box)`) makes the position ⊤ and the reason `CLJ_JOIN_TOP_ARG`, as a recorded ⊤ site
  would; a self-site that climbs kind by kind (`(f (str x))`, `(f (keyword x))`, …) widens. An arity calling the
  fn's *other* arity (`(defn sum ([n] (sum n 0)) ([n acc] …))`) is a caller like any other: recorded with the facts
  the one-parameter arity's own join gave it, so the two-parameter arity reaches int64 through the re-derivation
  the new site queues (specialize.c), not inside one table. The consumers compare the index against `callers`
  (`enqueue_if_stale_in`, `clj_exec_derivation_valid`) and enter and emit against `params`. The self walk is the
  recording walk's, so the depth cut of a summary walk does not apply to it (`pass.bounded`, the budget bullet
  above). The **⊤ rules**, each a reason the join reports: *no site* recorded (a fn nobody calls yet: unknown,
  not ⊥, or every use in its body would be a false error); a site
  passing *⊤ at that position* (the other positions keep their join); the var *read as a value* anywhere
  live (it may be called from a place the index cannot see); `^:dynamic` (a binding may put anything
  behind the var). The host boundary is a fifth, unrecorded caller, which is why no consumer trusts the join
  without a runtime check (the tag checks of the two consumers below); the join's job is to decide *where*
  a fast path is worth emitting. Over the corpus (`make facts-report`, which records every site of every
  library first and then re-derives every table for 3 rounds, so a parameter narrowed by its callers narrows
  the sites in its own body for the next round) 865 arities asked, 140 came back narrower than ⊤ at some
  parameter, 147 of 1102 parameters narrowed and every one to a single kind; the reasons over every ask:
  576 no site, 444 a ⊤ site, 1221 first-class (`map`, `partial`, `comp`, `concat`, `min`, `max` and the
  `deftest` vars are the population: a fn passed around is called from anywhere), 0 dynamic, 354 clean.
  Arithmetic over library code: 18.8 → 22.4 % of sites with every argument fixnum and 44.7 → 49.4 % with
  every argument int64 (medley 6.7 → 26.7 %). The gap between the two shares was never parameters: a
  fixnum stays exactly a fixnum only until the first `inc`, since the signature table answers fixnum|long
  for arithmetic (an overflow past the 63-bit tag boxes), so every loop variable is int64 and not fixnum,
  and the 22 sites between the shares are loop variables and vars holding a boxed long. What the join
  moves are the parameters, which sat at ⊤ or "a number" in neither share; what it leaves (43 of 85 sites):
  18 with an argument that is "a number" and no narrower — a parameter whose callers pass mixed numerics or
  are unrecorded, a captured parameter (⊤ inside a closure body: no join reaches a capture), the result of
  `quot`/`rem`/`/` or of a fn whose summary says number —, 14 with a parameter the ⊤ rules left at ⊤ (a
  helper used first-class, no live caller), 2 comparisons against a double. **A conflict inside a joined
  frame is a warning**, `CLJ_DIAG_CALLERS_CONFLICT`, never an error: `(name s)` with every recorded caller
  passing a fixnum says no recorded call takes that path, not that a call throws — the site itself already
  carries the site diagnostic, and a join-derived error would be a false one for a caller the index does
  not see. So the join adds no ⊥ to any table (the entry meet is skipped when it would be ⊥ and the site
  diagnostics stand) and the corpus gate stays at zero errors.
- **Summaries specialized to a call's arguments: domains.** `clj_summary_of_var_at(store, var, nargs, domains)` walks
  the arity with each parameter entered at its domain — int64 (fixnum|long), double, or ⊤ — instead of ⊤, and caches
  the entry beside the generic one (the key is the argument count under the domains in base 3, at most 8 arguments),
  under the same validity, fixpoint and budget rules; a native, a protocol method and an annotation-only var answer
  NULL. Pass 2 asks it at every call whose argument has a domain (`specialized_of`, `clj_domain_of` on the argument's
  fact) and takes its *result* for the site node; the requirements and the effects stay the generic entry's, so no
  diagnostic moves — the result of `(sq i)` with `i` int64 is int64, and the loop accumulating it stays typed where the
  generic summary said "a number". This is the transfer function of design §3 for a body the analyzer sees, sound on
  the site's own argument facts alone (no join is involved): the parameters enter at exactly what the site passes. A
  declared result meets the specialized one; the declaration's own diagnostics are the generic entry's. It is what the
  compiler's primitive entry reads on both sides of a call (the Compiler entry). A recursive call inside the
  specialized walk asks the specialized entry again (`specialized_of` keys on the site's own argument facts, which
  rest on the domain parameters), finds it running and takes its optimistic ⊥ — the rule below — so `fact` at int64
  is fixnum after the first round and int64 after the second, `fib` int64 in two, and `(defn halve-n [x n] (if (zero?
  n) x (halve-n (/ x 2.0) (dec n))))` at (double, int64) double; the generic entry, computed inside the first round
  with the parameters at ⊤, stays "a number". Over the corpus the store holds 987 entries instead of 743 and no
  coverage percentage moves: the corpus has few numeric helpers called from typed loops.
- **⊥ through a call in a summary walk.** A summary walk (`pass.summary`) answers ⊥ for a call whose argument is ⊥:
  the optimistic value of a fixpoint in flight, or a branch a refinement killed — the call is not reached, so its
  result joins nothing, and `(defn fact [n] (if (zero? n) 1 (* n (fact (dec n)))))` at an int64 argument climbs from
  fixnum to fixnum|long instead of stopping at "a number" (`arith_result` reads a ⊥ operand as any number, which is
  right for a stored fact and wrong for an optimistic one). Recording walks are untouched: a stored ⊥ would trip the
  watchdog, and the dead-branch classes are theirs.
- **`quot` and `rem` are arithmetic transfer rules** (`RA`, the `+` rule): int64 × int64 is fixnum|long (a quotient
  never grows, and the one overflow, `INT64_MIN` by −1, throws), a double operand makes a double, ratio and decimal as
  for `+`. They joined the intrinsics table too (`clj_quot`, `clj_rem`, the builtins' own bodies), so the compiler sees
  an INTRINSIC node; the round trip counts 16 transfer functions now. Over the corpus the refinement conflicts rise
  64 → 174, every one a dead branch: `(let [r (quot 10 3)] (and (int? r) …))` in the quot, rem and mod tests now has a
  known `r`, so the false branch of `int?` is dead by the literal; ⊥ value nodes, errors and the unexplained count
  stand.
- **Deliberately not here, each with its trigger.** No shape facts (the design's key sets) — trigger: a
  record fact reaching a consumer, which the constructor summaries now make possible. No ownership, thread
  affinity or the rest of the design's fact kinds — each is a field and a transfer rule on the shared walk;
  trigger: a consumer. No refinement on `CAPTURED` or `OUTER` reads, only on frame slots — trigger: a profile
  where a closure body re-tests what its definer already knew. No `case`/`condp` refinement beyond what their
  expansion into `if` gives. A `DIRECT_FN` node is recorded as a fn although its slot holds nil at run time:
  nothing reads that slot as a value, and the useful fact is that the name denotes a function.

## Numeric tower (bigint.c, ratio.c, decimal.c, number.c, builtins_number.c)

- **Six kinds, one ladder.** `clj_num_kind_of` answers fixnum < long < bigint < ratio < decimal < double, the
  order of Clojure's `Ops.combine`, and the larger kind of a pair decides the result (`clj_num_arith`,
  `clj_num_cmp` in number.c). The fixnum/fixnum and double-with-fixnum paths stay in builtins.c ahead of
  it, so `+` on two fixnums is still two tag checks and an overflow-checked add (bench/RESULTS.md: the
  counting loop, `reduce +` and `swap! inc` rows did not move). A bigint is sign + magnitude in
  little-endian base 2^32 limbs (bigint.c, no external library); a ratio holds two bigints, gcd-reduced
  with a positive denominator and never an integer; a decimal is a bigint unscaled value and an `int32`
  scale, as `java.math.BigDecimal`.
- **JVM promotion rules, exactly.** A bigint result stays a bigint (`(type (+' 1N 1))` is BigInt), so
  `(= 1N 1)` and `(== 1N 1)` are true and `(hash 1N)` equals `(hash 1)` — map keys of the two agree.
  Plain `+ - * inc dec` still throw "integer overflow" on a fixnum overflow; only `+' -' *' inc' dec'`
  promote. `=` keeps Clojure's category rule (integer / ratio / decimal / floating never equal across
  kinds), while `==` and `compare` compare numerically across all five.
- **What the reader takes**: `0N`, `1/2`, `0.0M`, `1M`, `1e10M`, and any integer literal in every radix
  (`0x7FFFFFFFFFFFFFFF`, `-0x8000000000000000`, `2r1011`, octal, `NrDDD`) — a long while the digits fit 64
  bits, a bigint past them, with the `N` suffix forcing a bigint. A ratio is normalised at read through the same divide as `/`, so `12/12` reads
  as `1` and `0/2` as `0`, as LispReader's `reduceBigInt` does; `1/0` is a read error.
- **`range` and every index argument span the whole int64** (`clj_range`, `clj_index_arg` in coll.h): a
  range's bounds and step are `int64_t`, its elements come out of `clj_long_new`, and the step is guarded by
  `__builtin_add_overflow` so `(range Long/MAX_VALUE)` and `(take 3 (range (- Long/MAX_VALUE 1) Long/MAX_VALUE))`
  walk instead of wrapping; `count` divides an unsigned span, so `Long/MIN_VALUE`..`Long/MAX_VALUE` does not
  overflow, and a count past `Long/MAX_VALUE` throws rather than lying. The iterator's range fast path owns a
  boxed element (`it->item`, `it->slots`) where a fixnum element is an immediate. An index argument is a fixnum
  or a box widened to the same `intptr_t`: a box is out of bounds for every collection, so the bounds check
  that follows reports the index error the JVM reports instead of a cast error. A bigint bound names itself
  ("range bound outside the 64-bit long"), as `range*` takes a long.
- **The 63-bit fixnum is a representation, not the contract** (long.c). A value outside it is a boxed
  `int64_t` of kind `CLJ_NUM_LONG`, so `9223372036854775807` reads as an integer, `(int? Long/MAX_VALUE)`
  is true, `(+ Long/MAX_VALUE 1)` throws "integer overflow", `(long 9223372036854775807)` returns it and
  the bit ops cover all 64 bits (`(bit-shift-left 1 63)` is `Long/MIN_VALUE`). **The box is canonical**:
  `clj_long_new` hands back a fixnum whenever one fits and `clj_long_box` asserts the value is outside the
  range, so `=`, `hash` and `clj_compare` never cross-check the two representations; the box's hash is the
  fixnum hash widened to 64 bits, so a fixnum, a box and a bigint of one value agree. `clj_int64_of` is the
  accessor that takes either. One descriptor serves both, named `long`, so `(type 1)` and
  `(type Long/MAX_VALUE)` are one value and `Long` is bound to it.
- **`unchecked-add`/`-subtract`/`-multiply`/`-inc`/`-dec`/`-negate` wrap at 64 bits**, as on the JVM:
  the arithmetic is done in `uint64_t` and `clj_long_new` is the one range check on the way out, boxing
  when the result leaves the fixnum. `(unchecked-inc 4611686018427387903)` is `4611686018427387904`,
  `(unchecked-inc Long/MAX_VALUE)` is `Long/MIN_VALUE`.
- **No float and no `*math-context*`.** `float` range-checks against `Float` and narrows through it
  (`(float Double/MIN_VALUE)` is `0.0`) but returns a double box, so `(double? (float 0.0))` is true where
  the JVM says false. `with-precision` and rounding modes do not exist: decimal `+ - *` are exact, and `/`
  succeeds only when the quotient terminates, else it throws "Non-terminating decimal expansion;
  with-precision is not supported". `(/ 1M 3M)` is that throw; `(/ 1M 2M)` is `0.5M`.
- **Division is shift-subtract**, quadratic in the bit length (`mag_divmod`), and `gcd` is Euclid over it.
  Every bigint the runtime meets is a few limbs, so the constant factors never showed. Trigger: a profile
  with `quot`/`rem`/`gcd` on thousand-bit values; the fix is Knuth D and a binary gcd.
- **Printing follows `print-method`, not `toString`**: `pr-str` appends the tag (`1N`, `1.5M`, `1/2`), `str`
  does not (`"1"`, `"1.5"`, `"1/2"`), and `str` of a non-finite double is Java's `Infinity`/`-Infinity`/`NaN`
  where `pr-str` writes `##Inf`. A decimal prints by `BigDecimal.toString`'s rule (plain notation while the
  scale is non-negative and the adjusted exponent is above -7, scientific otherwise), so `1e10M` prints
  `1E+10M`. `numerator`/`denominator` demote to the canonical
  integer, so `(numerator 1/2)` is `1` and only a value past 64 bits stays a bigint.
- **`clj_bigint_to_double` and `clj_decimal_to_double` round through the decimal text** (`strtod`), which
  is correct and slow, rather than reimplementing correct rounding over the limbs. Trigger: a profile with
  bigint-to-double in a loop.
- **A decimal built from a double keeps `Double.toString`'s scale** (`BigDecimal.valueOf`), so
  `(bigdec 0.1)` is `0.1M`; a value at or past 1e7 takes the scientific branch, where the JVM's
  `Double.toString` switches too, but our scale can differ from the JVM's by the trailing zero it keeps.
  Equality and hashing ignore trailing zeros, so only the printed form differs.
- **Every bigint carries at least one spare limb**: results are sized by the worst case and the count is
  trimmed without a `clj_realloc`, so a one-limb value can occupy two. Four bytes per bigint; trigger is
  a heap profile with many of them.

## Builtins (Sources/CljCore/builtins.c)

- **Coverage is the minimum for the evaluator tests and core.clj**: arithmetic and comparison, type
  predicates, `get assoc dissoc disj contains? count conj nth first rest next cons list list* vector
  hash-map hash-set set empty seq lazy-seq* realized? range* list* into second last butlast reverse empty? hash
  resolve deref identical? type instance? satisfies? extends? meta with-meta alter-meta!
  reset-meta! reduce reduce-kv reduced reduced? unreduced ensure-reduced volatile! volatile?
  vreset! fused-reduce* fused-into* fused-count*` (`deref` takes vars, reduced boxes, volatiles and atoms; `alter-meta!`/`reset-meta!`
  take vars and atoms), the bit predicates `seq? seqable? sequential? coll? counted? ifn? associative?
  indexed? list? vector? map? char? integer?`, `symbol keyword name namespace gensym`, `str pr-str
  pr prn print println identity apply`, `macroexpand-1 macroexpand ex-info ex-message ex-data
  ex-cause`, the atom API below, and the numeric tower's own file (builtins_number.c: `+' -' *' inc' dec'
  == int? double? ratio? decimal? rational? NaN? infinite? long int short byte float double num bigint
  biginteger bigdec rationalize numerator denominator parse-long parse-double unchecked-*`).
  No `keys`, `vals`, `sort`, ... Most of the rest belongs in core.clj.
- **`into` is C in both arities**: `(into to from)` conj's through `clj_seq_iter`, `(into to xform
  from)` runs the `fused-into*` driver under `[xform]` (fusion.c `clj_into_xform`), because
  `destructure` calls `into` above the `fn` macro, where a core.clj `into` could not be defined.
  Both hold the accumulator alone and conj in place; the transducers see `nil` as `result`, as under a
  fused pipeline (the fusion entry of the evaluator section).
- **Atoms** (atom.c; design §4 "Атомы"): `atom` (with `:meta`/`:validator`), `deref`/`@`, `reset!`,
  `swap!` (any arity), `swap-vals!`, `reset-vals!`, `compare-and-set!` (`identical?`), `add-watch`/
  `remove-watch`, `set-validator!`/`get-validator`, `atom?`, `meta`/`alter-meta!`/`reset-meta!`; the
  type name `Atom`. One `clj_lock` per atom; `f` runs *exactly once* under it (no CAS retry loop, so an
  `f` with side effects runs them once) on the value borrowed from the atom — the atom keeps its
  reference, `f` sees it at +0 — the validator runs under it too, watches run after it with
  `(f key atom old new)` (a watch may deref and swap the atom; a throwing watch propagates after the
  store, the remaining watches are skipped), `deref` takes the lock (a load and an atomic retain, ~2 ns
  more than a volatile). **JVM semantics on a throw:** a throw out of `f`, a validator rejection or the
  nested-op trap leaves the state exactly as it was; `(swap! a (fn [s] (if ok (assoc s …) (throw …))))`
  is a rejection idiom and code relies on it. There is no hand-over of the atom's reference to `f`: the
  uniqueness trick (the atom at nil while `f` ran, `assoc` in place through a consuming native or a frame
  owning param 0, `clj_call_invoke_owning`) was tried and removed, because a throw after an in-place step
  cannot restore a version that no longer exists, and an undo journal was judged too complex for the gain.
  The price is one root-to-leaf path copy per `swap! assoc` on a shared map: 91 → 288 ns at 16 keys, 101 →
  613 at 1000, 131 → 904 at 100000 (bench/RESULTS.md, "Atoms", the dated subsection); `swap! inc`, `deref`,
  the watched and the 4-thread rows did not move. `deref` of the same atom from inside `f`, a validator or
  an `alter-meta!` fn returns the current (old) value, as on the JVM: the lock holder reads its own atom
  without taking the lock again (`held_by_me`, the owner thread id in the atom). The lock is not recursive,
  so the trap stays for every op that would take it — a nested `swap!`, `swap-vals!`, `reset!`,
  `reset-vals!`, `compare-and-set!`, `add-watch`, `set-validator!`, `alter-meta!`, `meta`, ... on the
  *same* atom from inside `f`, a validator or an `alter-meta!` fn throws "<op> on an atom this thread is
  already swapping (nested swap! trap)" (the JVM retries forever). **Publication:** everything stored into
  an atom — value, meta, validator, watches — is `clj_share`d before the store, whether or not the atom
  itself is shared: the atom is a publication point, so a value read on another thread is on the atomic
  path from its first store. Triggers: the uniqueness trick returns only for atoms without watches or a
  validator (both need `old` intact), and only with either a proven no-throw `f` (the `throws` fact of the
  design's lattice, §3) or an undo journal, whichever is cheaper — when a profile shows `swap!` on a large
  map hot (the 100000-key row is a 4-level trie, not a typical atom); `IRef`/`IAtom` as interfaces (a
  deftype implementing `deref`); `agent`/`ref` (no); `swap!` returning a `reduced`-style early exit (no).
- **Volatiles are single-thread cells by contract** (`volatile!`, `vreset!`, `vswap!`; box.c): a
  read returns the value retained, a write retains the new value, shares it when the cell is
  shared (so a volatile published through a var keeps the RC invariant) and releases the old one
  with no ordering — a concurrent reader may retain a freed value, the `def`/`deref` race of the
  evaluator section. Transducer state (`take`, `partition-all`, `dedupe`, ...) lives in volatiles,
  so one transducer application (one `(xf rf)`) belongs to one thread. Trigger: a transducer
  shared across threads; Clojure's contract is the same.
- **`seq` on a map is an eager list of `[k v]` vectors** (no O(1) view, no first/next fast path):
  `(first m)` builds the whole entry list. Trigger: `first`/`some` over big maps in a profile. Fix:
  a map-seq cursor over the CHAMP trie and a map-entry type instead of 2-vectors.
- **`range` handles fixnums in C** (`range*`, an O(1) view); doubles and step 0 go through
  `take-while`/`iterate`/`repeat` in core.clj. A bound that is an integer but not a fixnum — a boxed long
  or a bigint — reaches `range*` and throws "Argument must be an integer" rather than falling to that path,
  because `range`'s guard is `integer?` and there is no fixnum predicate to guard with
  (docs/jvm-differences.md, deferred). The same holds for the fixnum index of `nth`, `assoc` on a vector,
  `subvec` and `subs`.
- **Printing realizes lazy seqs and can throw**: `clj_pr_str` returns CLJ_THROWN, which `str`,
  `pr-str`, `print*` and the error-message callers propagate; `Value.description` on the Swift side
  substitutes the exception text.
- **Output hook is process-wide** (`clj_set_output`), not per thread or per runtime. Trigger: two
  hosts printing concurrently.
- **Error messages are Clojure-like, not Clojure-identical**: type names are the runtime's
  (`string cannot be cast to a number`, not `java.lang.String ... java.lang.Number`).

## core.clj (Sources/CljCore/boot/core.clj)

- **Embedded as a byte array** (`core_clj.inc`, regenerated by `make boot`, as are `boot/clojure/*.clj`
  into `libs_clj.inc`); `CoreCljTests` fails when the two drift. A boot error is `clj_fatal` with the form's position: core.clj is part of the
  binary, so it is a build bug, not a user error.
- **Loaded once per process into `clojure.core`**; its vars, closures and fn nodes are live for the
  process and sit under every test baseline taken after `clj_init`.
- **Contents**: `concat lazy-seq when when-not if-not cond destructure let loop fn defn defn-
  vary-meta and or -> ->> comment profile dotimes if-let when-let assert declare doc vswap!`, the seq
  library `complement comp partial constantly completing transduce cat nthrest some every?
  not-any? not-every? map filter remove keep take drop take-while drop-while iterate repeat range
  interleave interpose mapcat dorun doall vec partition partition-all map-indexed keep-indexed
  sequence dedupe distinct group-by frequencies zipmap get-in assoc-in update update-in eduction`
  (`reduce` and `into` are C; `assoc-in`/`update-in` read the map in the first operand of their `assoc`,
  so a nested update copies the path — trigger for a consuming core: a profile with nested state updates),
  the predicates `boolean true? false? some? any? ident? simple-/qualified-ident?/symbol?/keyword? int?
  nat-int? pos-int? neg-int? double? float? NaN? infinite? distinct? map-entry?`, numbers `max min abs mod
  max-key min-key rand rand-int` (`quot rem bit-* rand*` are C), seqs and maps `ffirst nfirst fnext nnext
  nthnext not-empty peek pop subvec rseq keys vals key val find select-keys merge merge-with juxt some-fn
  every-pred fnil cycle repeatedly take-last take-nth drop-last split-at split-with flatten mapv filterv
  run! sort-by partition-by tree-seq shuffle rand-nth array-map`, the control macros `when-first if-some
  when-some while doto cond-> cond->> as-> some-> some->> case condp letfn doseq for defonce locking
  with-out-str` plus `memoize trampoline print-str println-str prn-str newline flush`, `delay`/`force`/
  `delay?` (a deftype over an atom), the multimethods `defmulti defmethod methods get-method remove-method
  remove-all-methods prefer-method prefers` and the hierarchy fns `make-hierarchy derive underive isa?
  parents ancestors descendants`, the namespace functions `require use refer refer-clojure loaded-libs` and the `ns`
  macro, and the private helpers `check-bindings`, `maybe-destructured`, `sigs`, `print-doc`,
  `preserving-reduced`, `load-one`, `load-lib`, `load-libs`, `libspec?` (`destructure` is public, as in
  Clojure). `clojure.set`, `clojure.string`, `clojure.walk`, `clojure.template` are separate embedded
  namespaces loaded on the first `require`. Not yet: `defrecord` (trigger: medley's `record?` and the
  suite's skip list — a deftype with a map behind it, `assoc` returning the record until a key leaves the
  basis), `defstruct`, `proxy`, `reify`-style
  `IDeref`, `future`/`pmap`/`agent`, `ref`, `dosync`,
  `with-local-vars`, `time`, `partition-all` transducer flush order, `chunk-*`. The 1.11/1.12 tail is in:
  `partition`'s pad arity, `partitionv`, `partitionv-all`, `splitv-at`, `reductions`, `halt-when`,
  `random-sample`, `bounded-count`, `boolean?`, `parse-boolean`, `reversible?` (vectors and the sorted
  collections), `replicate`, `lazy-cat`, `update-keys`, `update-vals`, `iteration` (a `reify` over `Seqable`
  and `IReduceInit`, whose type is made on the first call and cached for the process), `printf`, the data
  reader vars and `*print-length*`/`*print-level*`.
- **Hierarchies and multimethod dispatch are core.clj, not C** (~line 1848). The global hierarchy is the
  root of `#'clojure.core/global-hierarchy`, a `{:parents :ancestors :descendants}` map that `derive` and
  `underive` replace through `alter-var-root`, as Clojure does; `underive` rebuilds from the remaining
  edges. `isa?` is `=`, then the ancestor set, then elementwise over two vectors. A `MultiFn` is a
  `deftype` over `IMultiFn` and `IFn` holding the method table, the prefer table and the cache in three
  atoms; `defmulti`'s `:hierarchy` is a var, `#'global-hierarchy` by default, and the cache is
  `[hierarchy-value {dispatch-val method}]` — keyed by the hierarchy it was built from, so a `derive`
  invalidates every entry at once, as Clojure's `cachedHierarchy` check does, and a table or preference
  change resets it outright. `invoke` has fixed arities up to three plus a variadic tail, which is what
  makes the `=` hit *cheaper* than the pre-hierarchy `=`-only dispatch (222 vs 306 ns, bench/RESULTS.md):
  the rest seq and the two `apply`s cost more than the lookup. Still 6× a protocol call, which is a
  call-site cache; trigger for one here: multimethod dispatch in a profile.
- **clojure.test** (boot/clojure/test.clj) covers `deftest deftest- set-test with-test is are testing
  thrown? thrown-with-msg? use-fixtures (:each/:once) compose-fixtures join-fixtures test-var test-vars
  test-all-vars test-ns run-tests run-all-tests run-test run-test-var successful? report do-report
  assert-expr assert-predicate assert-any function? *load-tests* *stack-trace-depth* *report-counters*
  *testing-vars* *testing-contexts* *initial-report-counters* inc-report-counter testing-vars-str
  testing-contexts-str with-test-out`. `report` and `assert-expr` are multimethods, so a library adds
  its own heads (`(defmethod t/assert-expr 'p/thrown? ...)`). A failure's `:file`/`:line` come from the
  `is` form's reader position and `*file*` at expansion (`*assertion-pos*`), the var's own position when the
  error is outside an assertion; there is no stack trace to read them from, and `*file*` is nil for
  source the host evaluates. `thrown-with-msg?` matches the message with `re-find`, as Clojure's does.
  Fixtures live in an atom keyed by namespace name (namespaces carry no meta);
  `run-all-tests` takes a pattern or a predicate on the namespace name; no `*test-out*` (output goes to
  the process hook, `with-out-str` captures it); test vars run in `:line` order. Deviation: `is` binds
  `*assertion-pos*` per assertion (a frame push and pop, ~200 ns), where Clojure's reads the stack.
- **Semantics that differ from Clojure**, each kept for a reason: `case` compiles to `cond` over `=`
  (O(clauses), no jump table); `letfn` rebinds every name from a volatile at each body's entry (closures
  copy their captures when made, so a forward reference must be read at call time) — the cell holds the fn
  and the fn's body reads the cell, so every `letfn` call leaks the cycle (2 objects for one fn) until design
  §7's trial deletion exists; `transient`,
  `persistent!`, `conj!`, `assoc!`, `dissoc!`, `disj!`, `pop!` are the persistent operations themselves
  (the in-place path is the auto-transient of design §6b, so a code path written for transients just
  works; the use-after-`persistent!` check is not made); `defonce` is a macro over `bound?`;
  `isa?` knows tags only, never host types — there is no superclass to read (trigger: `(isa? String
  CharSequence)` or an `instance?`-shaped dispatch in a corpus library; then `class_getSuperclass` and
  `swift_conformsToProtocol`, design §4);
  `MultiFn.prefers` walks the *multimethod's* hierarchy, where Clojure's walks the global one whatever
  the multimethod's `:hierarchy` says (a JVM quirk, not a documented rule);
  `rand` is SplitMix64 seeded per thread from the id counter; `upper-case`/`lower-case`/`capitalize`
  stringify any non-nil argument (`(str/lower-case :a)` is `":a"`) and map ASCII letters only — trigger for
  Unicode case tables: a corpus test on a non-ASCII case change, which needs the full SpecialCasing data,
  not a range table; `subs`/`index-of` count code points where
  Java counts UTF-16 units; `clojure.string/split` also takes a literal string or char, where Clojure's takes
  only a pattern (the deviation keeps the literal fast path of builtins_string.c: no matcher and no code point
  array for the common separator); a map entry is a two-element vector, so `(key [1 2])`
  cannot throw; a symbol is not invokable, so `(ifn? 'x)` is false; a char is a Unicode scalar, so
  `(char 65895)` is in range; map and set seq order is the HAMT's, where the JVM's small collections keep
  insertion order (Clojure does not specify it). Triggers: a corpus test failing on any of these.
- **Transducers**: `map filter remove keep take drop take-while drop-while mapcat interpose
  partition-all dedupe distinct map-indexed keep-indexed` carry Clojure's transducer arities, `cat`,
  `completing`, `transduce`, `sequence`, `eduction` and `into` drive them. `sequence` is a lazy
  seq that pulls one input per realization into the transformed rf, whose bottom parks outputs in
  a volatile vector, so an infinite source stays lazy; each realization costs a vector copy per
  output (`vswap! ... conj` on a cell that also holds the vector: rc 2). `eduction` is a
  `deftype` implementing `Seqable` (through `sequence`) and `IReduceInit` (through `transduce`),
  not a C type: the slot trampoline already existed, so the type is four lines, and it prints as
  `#object[clojure.core.Eduction]` where Clojure prints the items. `vswap!` is a macro over
  `vreset!`/`deref`, as Clojure's. A transducer's stateful step (`partition-all`'s buffer) copies
  its vector per input for the same rc-2 reason, and `distinct`'s seen-set is conj'd at rc 2 the same
  way (a path copy per new element). `halt-when` substitutes the result with its `{::halt v}` map, which the
  fused `into`/`reduce` drivers (fusion.c) could not see while their bottom fn ignored `result`: the walk's
  final result now goes through the completion arity, whose answer replaces the accumulator, and the bottom's
  completion answers the accumulator so `retf` gets it, as `transduce`'s rf does. `sequence` drops the
  substituted result, as the JVM's TransformerIterator does.
- **`defn` follows clojure.core's** `name docstring? attr-map? ([params] body)+ attr-map?` but has no
  `:inline`/`:tag` handling and no `:pre`/`:post` map in `sigs` (a map after the params is a body
  form, see `fn` below). `doc` handles vars only: no special forms, no namespaces.
- **Protocol macro helpers are private** (`group-impls`, `form-uses?`, `method-fn`, `method-map`,
  `body-as-is`); they run at expansion time inside clojure.core, so user code never resolves them.
  `defprotocol` puts its docstrings in `:doc` (the protocol's on its var, a method's on the method's)
  and takes no options (`:extend-via-metadata`, `:on-interface`). `extend` rejects a key that names
  no method where Clojure ignores it. Method fns are unnamed, so an arity error inside an impl says
  `fn`; the dispatching fn checks the declared arities first and names the method.
- **`concat` is defined first, with `fn*`/`let*`/`lazy-seq*` only**: syntax-quote expands `~@` to
  `(seq (concat ...))`, so every macro expansion runs through it. A macro's output is therefore a
  cons chain with lazy tails, which the analyzer realizes while collecting items; code-sized data,
  but each splice costs a closure and a lazy seq per element. Trigger: macro expansion in a profile.
  Fix: a C `concat` over already-realized arguments when every argument is counted. `assert` is always
  on (no `*assert*`). `dotimes` does not coerce its count to a long.
- **`destructure` follows clojure.core with these gaps.** A keyword as a binding form (`[:a 1]`) and a
  map key that is a keyword other than `:as`/`:or`/`:keys`/`:strs`/`:syms` (`{:foo x}`) are
  "Unsupported binding form/key" here; Clojure's function binds `a`/`foo` but its `let` spec rejects
  both. Kwargs: a rest seq is turned into a map when it is all pairs or a single map; Clojure 1.11 also
  merges a trailing map after pairs (`(f :a 1 {:b 2})`), here that is "No value supplied for key".
  Trigger: a library relying on the trailing-map call style.
- **Reader metadata on a collection literal is dropped**: `^:foo [1]` reads as a vector with meta, and the
  analyzer then builds a fresh vector node from the items and loses it, so `(meta ^:foo [1])` is nil where
  Clojure's is `{:foo true}` (its compiler emits the `with-meta`). A bug; the fix is a meta child on the
  vector/map/set nodes, applied after the collection is built. Trigger: `^:const`, `^{:doc}` or any
  annotation on a literal, and the corpus `group-by` test.
- **A cooperative deadline bounds what a thread runs** (`clj_deadline_set_ms`): an interpreted closure call
  (`run_body`), a `loop` turn in both backends, a lazy-seq cell's realization (`run_thunk`), `clj_reduce_iter`
  and a fusion driver's entry check it (`clj_deadline_tick`), the clock is read once per 1024 of them, and
  past it the check throws `CLJ_DEADLINE_MESSAGE`. A compiled fn's entry does not check (its prologue is
  empty, "Compiler"): an endless recursion runs into the guard page, an endless loop into the tick, and an
  endless lazy seq or reduce into the driver's check — `(count (iterate inc 0))` on the compiled core is the
  test. The fields live in the shadow stack, which those paths already load, so off it costs one
  predictable branch. A caught timeout keeps the deadline: the handler gets an unwind budget of
  calls and, after a fixed number of those budgets, every check throws, so a loop that catches the timeout
  still stops. Cooperative only: a native that loops without calling back into Clojure is not interrupted
  (`(hash (range))` is such a loop). The corpus watchdog is the one user so far; an untrusted-code host is
  the other.
- **`fn` has no `:pre`/`:post` conditions**: a map as the first body form is evaluated and discarded
  like any expression. Trigger: the first `{:pre [...]}`; the `fn` macro then wraps the body in
  `assert`s as Clojure's does (`assert` is defined below it, so the wrap must use `when-not`/`throw`).

## Corpus (corpus/, Tests/PippinTests/CorpusTests.swift, docs/corpus.md)

- **What is vendored**: `corpus/medley` (medley.core and its test, EPL) and `corpus/clojure-test-suite`
  (jank-lang's cross-dialect clojure.core suite, the whole `test/` tree, MPL 2.0), each with a `SOURCE`
  (repo, commit, license, files) and a `manifest.edn` (`:load-path`, `:features` for `#?`, the test
  namespaces or `:test-dirs` to scan). No submodules.
- **The harness** (`CorpusTests`) is opt-in: `CLJ_CORPUS=1 swift test --filter CorpusTests`, `CLJ_CORPUS_LIB=medley`
  for one library, `CLJ_CORPUS_UPDATE=1` to rewrite `corpus/<lib>/allowlist.edn` and `docs/corpus.md` from the
  run. It sets the load path and reader features from the manifest, requires every test namespace under
  lenient loading (a failing top-level form is recorded, not fatal), captures the suite's own `SKIP - x`
  lines (`when-var-exists`), runs each namespace through `clojure.test/test-ns` under a collecting
  reporter and folds the events into pass/fail/error per var; a second run over the loaded namespaces is
  the memory check (baseline after the first). Allowlist rule: a failing form, test or skip not in the
  allowlist fails; a listed one that now loads, passes or runs fails too (stale); an entry carries
  `:missing` (the symbols the runtime lacks, extracted from the message), `:design-line` (a line of design §8)
  or `:note` — a hand-written sentence saying whether the failure is an accepted deviation or a runtime bug
  still open, with the repro. A test entry with none of the three fails the check, and a regeneration carries
  `:design-line` and `:note` over, so the review is not lost. Forms are not annotated: a form's reason is its
  own classification (a reader gap or an unresolved symbol). `:second-run-live-objects` is what a second run
  of the same tests leaves alive; a different number fails.
- **On by default** (`CLJ_CORPUS=0` skips it): the whole corpus is about a second of a debug run, three under
  ASan. `make corpus` runs it alone, `make corpus-update` regenerates the allowlists and docs/corpus.md.
- **`:second-run-live-objects` is not always zero**: the suite's own `letfn` leaves a reference cycle per call
  (the volatile cell holds the fn, the fn's body derefs the cell), which RC cannot free — 2 objects per
  `letfn` call, 4 for the two namespaces that use one. The number is recorded per library and checked, so a
  runtime leak still fails; design §7's trial deletion is what would collect it.
- **The watchdog**: a deadline per deftest (`CLJ_CORPUS_TIMEOUT_MS`, 5 s by default) armed by the collecting
  reporter on `:begin-test-var` and cleared on `:end-test-var` (`clj_deadline_set_ms`, analyzer/evaluator
  section). A test past it is `:timeout` and counts as a failure, so one spinning form no longer takes the run
  with it. `CLJ_CORPUS_LOG=<file>` writes the progress lines to a file as well as stderr: the test runner
  forwards stderr through a pipe and drops what it has not flushed when a killed run dies, which is why the
  earlier hang appeared to be in a different test each time.
- **What the earlier hang was**: `((juxt (range)))` in `clojure.core-test.juxt` — calling a value that is not
  a fn built the "%s cannot be invoked" message with `clj_pr_str`, which realized the infinite lazy seq. The
  fix is `clj_pr_str_max` (printer section) in every error message that quotes a runtime value. It was never
  state-dependent: the namespace hangs in isolation too.
- **Symbols the suite and medley need from the JVM**: `clojure.lang.LazySeq` (`p/lazy-seq?`), `Throwable` in
  `catch` works, `instance?` of a JVM class works only for the names bound in core (`clojure.lang.IEditableCollection`,
  `clojure.lang.IRecord`, `clojure.lang.PersistentQueue`, `java.util.UUID`, `java.util.Date`); a static call
  such as `java.util.UUID/fromString` is interop and stays unresolved. The suite's parse-uuid test expects
  `fromString`'s lenient grouping under `:clj` and nil under `:default`; with no features set it fails
  here for answering as the JVM does (allowlist note).
- **`make api-diff`** runs the parity report: `scripts/api-diff.clj dump-jvm` on JVM Clojure, the
  `clj-api-dump` executable for ours (it evaluates `ns-publics` and prints the EDN — name from the map key,
  not the meta, so a var whose meta lost its `:name` still appears), then the diff, weighted by symbol
  occurrences in `corpus/**/*.clj*`. It writes `docs/api-parity.md`, which is committed: 470 of the JVM's 679
  public vars exist, 209 missing, 21 of those used by the corpus; `ref`, `with-precision`, `future`, the
  agents and `tap>` lead the weighted list. One macro/fn mismatch (`refer-clojure` is a fn here), one dynamic
  mismatch (`pr` is `^:dynamic` on the JVM) and 12 arity mismatches, of which `sequence`'s multi-coll arity
  and `disj!`'s 1-arity are real gaps rather than differently-written variadics.

## Host bridge (Sources/Pippin, error.c host-error, fn.c context natives)

- **A host error keeps the Swift `Error` boxed as an opaque payload** and captures
  `String(describing:)` as its message when made; `ex-data` builds `{:host/error e}` on every call
  (storing it would make the value its own child). Trigger: `(catch MyError e ...)` by host type, or
  `(:code (ex-data e))`-style access to the error's fields; both need a host type registry, and the
  second a `Codable`/reflection walk of the error (design section "Интероп").
- **Only a host error thrown as is comes back as the Swift error.** Wrapped as a cause (an
  `ex-info` from Clojure code, or the analyzer's positioned rethrow of a macro failure) it surfaces as
  `ClojureError` with `cause.hostError` set. Trigger: a host caller wanting `catch let e as MyError`
  through a macro; then unwrap the cause chain in `takePending` or stop positioning host errors.
- **A Swift fn extends a protocol only through `extend`** (`(extend T P {:m f})` with `f` a
  `Value(function:)`); there is no Swift API for protocols, types or `satisfies?`. Trigger: a host
  wanting to implement a Clojure protocol for its own type registry (design section "Интероп").
- **`Value(function:)` bounds arity with a closed range**; a variadic fn with a minimum is `nil`
  (any count) plus a check in the body. Trigger: the first host fn wanting `[a & rest]` semantics.
- **The Swift body of a host fn is not `Sendable`-checked** and runs on whichever thread invokes the
  fn; the runtime evaluates on one thread at a time (NOTES, evaluator). Trigger: multi-threaded
  evaluation.
- **`ClojureError.trace` is the frames at the throw**, innermost first, and `description` appends
  them Clojure-style (`at user/f (line:col)`); a `ClojureError` rethrown from a host fn hands the
  same frames back to the core, so a non-error value keeps them across the boundary. Compiled frames
  come from the real stack and interpreted ones from the shadow stack, merged by stack order (trace.c):
  the same frames either way, a compiled one at its fn's own position.
- **`Value.apply` is a recovery point** (`clj_host_invoke`): a stack overflow in compiled code called
  from Swift lands there as the "Stack overflow" `ClojureError` (guard.c, "Compiler"), and the call is a
  top-level bracket, so a `def` inside parks the fn roots the caller may still borrow. `Runtime.eval` has
  the same through `clj_eval`. Other host entries that run Clojure code (a lazy seq realized through
  `Value`, a deftype's `equals`) have none: an overflow there is fatal with the trace on stderr.

### Host-defined vars and primitives (Runtime.swift `define`, Differential.swift, Primitives.swift)

- **`Runtime.define(name, in:, arity:, doc:, body)`** interns the var (the namespace is created when
  missing) and binds a `Value(function:)` as its root through the calls `eval_def` makes —
  `clj_var_bind_root` (epoch bump, a replaced fn root parked until the thread is idle), then
  `clj_var_set_meta` with `{:ns :name}` plus `:doc`, then the macro and dynamic flags cleared — so a
  `def` and a `define` of the same var take turns freely and a site warmed on the boot fn (an INTRINSIC
  or FUSED node) falls back through its guard (DefineTests, `clojure.core/+`). The fn is named
  `ns/name`, so arity errors read as a `defn`'s. No `:line`/`:column`, no `:arglists`; `(doc x)` prints
  the doc. Nothing new in C: the entries existed. Returns the var, as `def` does.
- **A primitive never exists without its specification and the differential test** (design §6b item 1,
  the intrinsics rule of intrinsics.h from the other side of the bridge): the Swift implementation of
  something Clojure already says how to compute lives next to the Clojure implementation that stays its
  specification, and `Runtime.differential(primitive:spec:samples:messages:)` runs both over the samples
  and returns the divergent ones — a value must be `=`, a throw must meet a throw, the messages agree only
  with `messages:` (a spec in Clojure rarely throws the primitive's text; the sort spec meets a mixed pair
  in another argument order). It is public API, not a test helper: the escape hatch is for host libraries
  (hiccup diff, JSON, sorting) whose own tests cannot import ours, and it returns data, so it binds to no
  test framework.
- **`compare` and `sort` are the first residents** (Primitives.swift), and both bodies are now one C call
  each: `clj_compare` and `clj_sort` (compare.c) do the work, the Swift fns are the vars. They bind into
  `clojure.core` from `clj_host_boot`, a weak C hook `clj_init` calls last, defined by the Swift module with
  `@_cdecl`: a raw `clj_init()` and `Runtime()` boot the same core, tests take baselines after either. Their
  roots are ordinary (not immortal, read owned). `Runtime.sortSpecification` is the top-down merge sort in
  Clojure in the same file, evaluated by PrimitiveTests and the bench; `compare` is the leaf without a
  Clojure spec, since nothing in Clojure here orders two strings or chars (no `int` of a char, no `subs`),
  and it is checked by table. A `(sort ...)` call from Clojure therefore costs one crossing, not one per
  comparison; `sort-by` reaches `clj_sort_by` directly through a builtin and never crosses at all. A C-only
  host has `sort-by` and the sorted collections but not `compare` or `sort` as vars. Trigger for dropping
  the Swift residents entirely: char/int conversion landing, so a Clojure `compare` spec becomes writable
  and the differential can take the C builtin as its subject.
- **Deviations from Clojure's `compare`**: −1/0/1 always (the JVM returns the char or length difference
  for strings); strings order by code point, the JVM by UTF-16 unit (they differ only between an astral
  char and U+E000–U+FFFF); the mixed-type message names the runtime's types (`long cannot be cast to a
  string`) and an unordered type says `cannot be cast to Comparable`. A nil comparator is the default one
  inside the core, so the 2-arity `(sort nil coll)` refuses it by hand, the way invoking nil would.
- **Limits.** Varargs are `arity: nil` plus a check in the body, as with `Value(function:)`. core.clj
  cannot call a primitive at load time and cannot reference one without `(declare ...)`: the hook runs
  after core.clj. A C-only host has neither `compare` nor `sort` as vars, though `sort-by` and the sorted
  collections reach the same C functions. Meta is `:doc` only; `:private`,
  `:dynamic`, `:arglists`, `:tag` need a `def` afterwards. `define` on another thread against a running
  call is the concurrent-`def` race of the evaluator section.
- **Cost** (bench/RESULTS.md, "Host-defined fns"): a host fn call is ~64 ns over a C builtin at the same
  site and ~60 over a closure — the `clj_invoke` path for context natives plus the bridge's `[Value]`
  array, per-argument wrapping and the box retain; the design's "tens of ns" at the upper end. `sort` of
  1k fixnums: 83 ns per element against 4960 through its Clojure spec. Trigger for a cheaper crossing: a
  host fn in a per-element position of a profile; then an argument-buffer body signature.
- **Triggers.** Many primitives → a registration table and a generated differential suite over it, the
  design's one-table shape for intrinsics; a primitive core.clj needs at boot → a C builtin under the
  intrinsics rule, or a second hook before core.clj; a host wanting a Clojure protocol implemented in
  Swift → the `extend` entry above; `Runtime.define` of a macro → `:macro` meta and `clj_var_set_macro`,
  when a host has a reason.

## Printer (Sources/CljCore/printer.c)

- **Map entries are collected into a temporary array per map** because `clj_map_each` is callback-only.
  Trigger: printing huge maps in a profile. Fix: a resumable map iterator.
- **An error message quotes a value through `clj_pr_str_max`** (`CLJ_ERROR_PRINT_MAX` bytes, then `...` and
  the closers of what is still open), so a message about an unbounded lazy seq does not realize it. Every
  error path that prints arbitrary runtime data uses it — "cannot be invoked", the arity error, "No value
  supplied for key", "Duplicate key", the protocol and node-data messages; `pr-str` itself is unbounded, as
  Clojure's is with `*print-length*` nil.
- **Control characters print as `\uXXXX`** inside strings and as char literals; Clojure prints them raw.
  Readable by both, but `(pr-str "\u0001")` differs from the JVM byte for byte.
- **`*print-length*` and `*print-level*` are read by `clj_pr_str_dynamic`**, the entry `pr`, `prn`, `print`,
  `println` and `pr-str` use, once per print into a `limits` pair; `clj_pr_str` (`str`, `Value.description`,
  the codec) and `clj_pr_str_max` (error messages) never read them, so an error message stays the same
  under any binding. The rules are print-sequential's: after `length` items a frame writes its separator and
  `...` (`elide`), which for a seq realizes one more element as the JVM's `[x & xs]` does; a collection
  about to open at depth `stack.count >= level` prints as `#` (`is_collection`: every kind that gets a frame,
  the `#error` map excepted). A negative length prints everything and a negative level nothing, as on the
  JVM; a non-integer throws "cannot be cast to a number". The vars are looked up by name on each call until
  core.clj has defined them (vars are immortal, so the cache never goes stale).
- **`#queue [1 2 3]`** for a PersistentQueue (queue.c): the JVM prints an address. The `F_QUEUE` frame is the seq
  frame with `]` as its closer; nothing reads the form back (docs/jvm-differences.md).
- **`format` is java.util.Formatter's subset** (builtins_format.c): `%s %S %b %B %c %C %d %o %x %X %e %E %f %g
  %G %n %%`, the flags `- + space 0 ,`, width, precision and `n$` positions, with the ordinary argument index
  independent of explicit ones as Formatter's is. `%s` is `str` with `null` for nil (`clj_str_value`,
  builtins.c); `%d` takes a long or a bigint and `%x`/`%o` a long (two's complement, as `Long`); `%f`/`%e`/`%g`
  take a double or a decimal and refuse an integer, as the JVM's `f != java.lang.Long` does. Floats round the
  shortest round-trip digits HALF_UP (`shortest`, `round_to`), because Formatter does — `(format "%.2f" 1.005)` is
  `1.01`, which C's printf would print as `1.00` — and `%g` follows Formatter's rule (decimal within
  `[1e-4, 10^precision)`, trailing zeros kept). Anything else (`%h`, `%t`, `%a`, the `#` and `(` flags, a
  precision on `%d`, a bigint under `%x`) is an error naming the spec.

## Symbol / keyword (Sources/CljCore/symbol.c, keyword.c)

- **A symbol's meta survives `with-meta` copies only**: `symbol`/`name`/`namespace` and the analyzer
  work on the fields, and `def` interns a bare copy of a meta-carrying name so the mapping key never
  holds the var's meta twice.
- **Interning a keyword is permanent and shows in `clj_debug_live_objects`** (keyword, symbol,
  string, intern-table nodes); tests intern the keywords they use before taking a baseline. The runtime's own
  first-use sets (the reader's, analyzer's, node codec's, printer's, error's, trace's, profiler's and var's
  `intern_keywords`, each behind a `pthread_once` so the module also works unbooted) are interned by `clj_init`
  through their `clj_<module>_intern_keywords` entries, and every suite under `CoreTests` runs behind the
  `.booted` trait (`EvalSupport.swift`, a recursive `TestScoping` calling `clj_init`), so no baseline lands
  before the boot or before a lazily made set, whichever suite runs first. `make test-isolated` runs every suite
  alone to keep that true; it is periodic, not a gate. A baseline that still depends on order is one taken
  before test data the test itself interns (a `:k` read from its own source, a method name an error names).
- **Keyword intern table is one global map under one mutex**, and interning allocates a temporary
  symbol for the lookup even on a hit. Trigger: keyword literals resolved at runtime in a hot path
  (the reader/analyzer resolves them once, so unlikely). Fix: sharded tables or a lock-free read path.
- **A string is limited to 4 GiB** (`uint32_t len`); `clj_string_new` aborts beyond that.

## Compiler (Sources/CljCompiler, Sources/CljCore/compiled.c, boot/core.c)

- **What it is.** `compiler.c` turns the analyzer's optimized trees into C, one translation unit per source
  file; `jit.c` runs the same generator per host form through clang and `dlopen` (`CLJ_EVAL=compiled`);
  `compiled.h`/`compiled_internal.h`/`compiled.c` are the runtime side the generated code calls; `boot/core.c`
  and `boot/libs_*.c` are core.clj and the embedded libs as units, regenerated by `make boot` and linked by a
  `-DCLJ_COMPILED_CORE` build (`make test-compiled`), where `clj_init` runs `clj_compiled_core_init` in place of
  reading `core_clj.inc` and registers the libs by their `<embedded>/...` paths. Three facts are consumed: the
  escape classification (promoted slots, below), the int64 or double kind of arithmetic arguments and let or loop
  variables (unboxed arithmetic, below) and the receiver kind at a protocol call (protocol calls, below);
  everything else stays a boxed `clj_value`, and there is no tree shaking.
- **The load hook** (`clj_load_set_hook`, load.c/eval.c) is how the compiler sees the program: a loader
  (`clj_load_source`, `load_core`) arms the hook before each top-level `clj_eval`, which fires it with the
  optimized `const clj_node *` of every tree the form yields (a top-level `do` fires once per item, all with
  the form's serial), and in `toplevel` mode it also fires on host evals made while nothing runs on the thread,
  which is the compiled eval. A hook that runs the form itself sets `handled`; the file compiler leaves it to
  the interpreter, so a compile *evaluates* the file — macros must exist to expand the next form — and the
  unit runs in another process. What the hook sees is kept (the node retained, the form and the namespace
  of the moment) and emitted at `cljc_end`, in load order: by then the interpreter has run every form, so
  every fn is emitted under the caller join of every caller the load defined (the Facts entry: the compiler's
  closed-world round is the same store the interpreter fed). Under lenient loading a form that failed to read or analyze reaches
  `failed` and the unit throws the same message at that point; a form whose evaluation failed is emitted as
  is and fails at run time by itself. The analyzer is untouched: the hook is one thread-local arm plus one
  call in `clj_eval`.
- **Calling convention.** A fn node becomes one C function per arity, `<base>_a<n>(self, captured, args,
  nargs)` (`_v<n>` for the variadic one), and a dispatcher `<base>(void *ctx, args, n)` that is the fn's
  `CLJ_FN_NATIVE_CTX` callback; the closure is `clj_fn_native_env` — a `clj_fn` whose ctx is the fn object
  itself and whose captured values sit in its own `env[]`, so a closure is one allocation as in the
  interpreter and `self` is the ctx. Arguments are +0, results +1, `CLJ_THROWN` checked after every call that
  can throw. The frame is `clj_cframe` — the interpreter's frame without the exec: a C array of slots, the
  captured pointer, the owned bitmask, the static link — and every step of a body is the corresponding step
  of eval.c through the inline helpers (`clj_c_set` is `slot_set`, `clj_c_take` the last-use hand-over,
  `clj_c_var_borrow` the var case of `eval_borrowed`, `clj_c_loop_tick` the deadline check of a loop turn;
  `run_body`'s guard, deadline, shadow frame and instrumentation have no counterpart on the call path — the
  next entry). Expressions
  are emitted into temps with a static ownership (`t5` owned, borrowed, or owned-if-`o5`); the emitter keeps
  the list of live owned temps and every throw site releases those above the enclosing handler's mark and
  jumps to it, so `try` is the interpreter's `eval_try` with labels and the function's `fail` label releases
  the owned slots. `loop`/`recur` and a fn-body `recur` are a label and `goto` after the slots are rebound;
  a direct fn is `<base>_a<n>(outer, captured, slots, owned)` with the caller's filled slot array, the
  static link `clj_c_outer(&fr, depth)`, and `OUTER` reads walk it; `DEF` binds the root, evaluates the meta
  and calls `clj_c_def`; literals go through `clj_vector_from_array`/`clj_c_map_literal`/`clj_c_set_literal`
  (with the duplicate-key check). A top-level form is `top_N(void)` under `clj_eval_top_enter/leave`, the
  bracket `clj_exec_run` puts around a form so a def inside parks fn roots; the unit's `init` runs them in
  file order, each behind `clj_load_form_failed` (lenient: record and go on; else the CompilerException wrap
  of the loader). A trace names a compiled fn at its own position rather than the caller's line: the one
  visible deviation of compiled frames.
- **An empty prologue: frames, traces, the guard page, the deadline, instrumentation** (design §4 "Пролог
  скомпилированной функции — ноль на горячем пути"; trace.c, guard.c, compiled_internal.h `CLJC_FRAME`,
  `CLJC_SITE`, `CLJC_ENTER`/`CLJC_LEAVE`). A compiled arity function is an ordinary C function: no shadow frame,
  no stack check, no deadline, no instrumentation byte; its epilogue is the release of the promoted owned
  slots. Each check moved. *Traces from the real stack.* Every frame function (an arity, a direct fn body)
  carries `CLJC_FRAME`: `noinline`, `disable_tail_calls`, and the `__TEXT,__cljframe` section, so the unit's
  frame functions are contiguous and nothing else is among them; the unit's `FR[]` table pairs each with its
  stub (`S[k]`) and `unit_pools` registers it once (`clj_c_register_frames`). trace.c keeps one sorted table of
  every unit's function starts and one record per image (its `__TEXT` bounds and the `__cljframe` section's, from
  `getsectiondata`), so a fn's code runs to the next entry's start or the section's end: no sentinel, whatever
  order clang emits static functions in. A throw (`clj_throw_traced` → `clj_shadow_stack_trace` →
  `clj_trace_collect`) walks the frame-pointer chain from the throw site to the thread's stack top (bounds from
  `pthread_get_stackaddr_np`, kept in the shadow stack; return addresses stripped of pointer authentication where
  the target signs them) and maps each return address through the table; addresses in the runtime, in a
  dispatcher, in a `top_N` or in the host are skipped. *The merge rule*: an interpreted frame records the frame
  address of the function that runs its body at the push (`run_body`, `__builtin_frame_address(0)`), a compiled
  frame's key is the frame pointer of the frame that holds its return address, and the two lists, each
  innermost first with ascending keys, merge by key — lower is deeper, and a compiled fn called from an
  interpreted body is below it while the interpreter it calls is below that. The corpus reports, the fixtures
  (`seqs.clj`'s `[inner outer fixture.seqs/trace-names]` in dev and closed), `HostErrorTests` and `TraceTests`
  pin the frames as they were. *Markers for inlined bodies* (`CLJC_SITE(&S[k])`, after every call the emitter
  checks for `CLJ_THROWN` and after every direct throw): an `asm volatile` that writes `{address after the call,
  stub}` as two self-relative 32-bit offsets into `__TEXT,__cljsite` — data only, no instruction, no runtime
  relocation — and travels with the body when clang inlines it. For a return address inside frame fn F, the
  first marker at or past it within F's range names the fn whose body made the call; a marker naming another
  fn than F is an inlined callee, and the trace shows `[callee, F]`. The marker sits in the call's own basic
  block, so block reordering (a cold throw path moved to the function's end) cannot separate them, and every
  call site being marked is what makes "first marker at or past" the right one. *Inlining is one level deep by
  construction* (below), so the chain never needs a third name. *The guard page.* A thread's stack ends in the
  system's guard page; `clj_init` installs SIGSEGV and SIGBUS handlers (`clj_guard_install`, chaining to the
  previous action — the sanitizer's — for any other fault) and every thread that makes a shadow stack gets a
  256 KB alternate signal stack (`sigaltstack`, an `mmap` of its own: a sanitizer's thread teardown unmaps
  whatever stack it finds installed, and only the stack still installed is ours to unmap). A fault whose
  address lies within 1 MB below the stack's low end, or whose stack pointer is within 64 KB of it, is an
  overflow. The handler collects the merged trace from the interrupted registers (`pc`, `lr`, `fp`, `sp`; a
  fault in a prologue or a leaf has the caller's frame pointer and the return address still in `lr`) into
  the shadow stack's buffer and then, if it can, lands at the thread's innermost **recovery point**
  (`clj_recovery`, guard.h): `clj_eval` pushes one per top-level form, `clj_host_invoke` per host call,
  `run_unit` (load.c) around a compiled unit's init. The landing is not a `siglongjmp` from the handler: the
  handler rewrites the interrupted context so that its return resumes in `land()` on the alternate stack's
  memory used as a plain stack, which `siglongjmp`s to the point — a jump straight out of the handler leaves
  XNU believing the thread is still on the signal stack, and the next overflow's frame then lands on the
  overflowed stack, which the kernel answers with SIGILL. At the point (`clj_recovery_throw`) the thread's
  state is restored to what the push saw — shadow depth, `clj_exec_run` nesting, the dynamic binding frames
  (popped down to the mark) — the sanitizer is told the frames are gone (`__asan_handle_no_return`), and
  "Stack overflow" is thrown as an `ex-info` carrying the collected trace, so the host sees the same error
  the interpreter's own check produces. What the landing abandons: the C frames between the point and the
  fault, so their owned temporaries leak, a lazy seq being realized stays claimed (its next force throws
  "Recursive realization"), and a `try` between them never sees the error — the point is the host boundary,
  not the nearest handler (trigger: a compiled `try` around deep compiled recursion in real code, then a
  recovery point per `try`, one `sigsetjmp` each). *What cannot be converted* is fatal instead, with the trace
  on stderr (`clj: fatal stack overflow (…)`) and the default disposition: no recovery point on the thread (a
  thread that entered compiled code some other way), the faulting `pc` outside the runtime's own image and the
  registered units' (`libsystem`, `malloc`, dyld: a lock may be held there), or a `clj_lock` held by the thread
  (`clj_locks_held`, counted in lock.h: an atom's `swap!` runs its fn under the atom's lock). Under ASan the
  sanitizer's runtime image counts as the runtime's own, since every instrumented entry calls into it
  (`__asan_stack_malloc`) and the fault lands there as often as in ours; `make test`, `test-ubsan` and
  `test-compiled` need nothing else. The interpreter keeps its own check (`run_body`, `STACK_MARGIN`), so an
  interpreted recursion, and any mixed one whose compiled stretches stay under the margin, still unwinds
  frame by frame with no leak; the guard page is for compiled-only recursion, which
  `CompilerFixtureTests.endlessRecursionAndLoopAreStopped` exercises in dev (through the dispatcher) and closed
  (direct calls) mode, in every sanitizer mode. A tail call turned into a jump would hide such a recursion from
  the guard page for ever: `disable_tail_calls` on every frame function keeps the stack growing, as on the JVM.
  *The deadline* left the prologue for the loop tick and the drivers (evaluator section). *Instrumentation*
  is decided at build time: `clj-compile --instrument` defines `CLJC_INSTRUMENT` at the unit's top and `core.c`
  takes `-DCLJC_INSTRUMENT` from the build, and only then `CLJC_ENTER`/`CLJC_LEAVE` expand to the profiler and
  signpost hooks of `run_body`, which still read the runtime byte; a plain unit has no hook and its fns are
  absent from `profile-start!`'s report (`clj_core_instrumented`, which `ProfileTests` consults for the compiled
  core's macros). *Parked fn roots*: `in_flight()` (the guard of `clj_eval_retire_root` and the drain) can no
  longer count compiled frames, so it walks the real stack for one when the shadow depth and the exec nesting
  are both zero — paid at a fn-root rebind and at a drain, never on a call. *Leaf inlining* (design §6 "маленькие
  чистые — always_inline"): an arity whose body makes no direct call (no `CLJC_DIRECT` site, no direct fn) and
  emits under 3000 bytes of C is written as `<name>_i` (`CLJC_INLINE`, `always_inline`) with the frame function a
  wrapper around it; the closed prelude defines `CLJC_LOCAL_<target>` and `CLJC_CALL_<target>` (the twin, or the
  frame function when the target is no leaf) for every direct target defined in the unit, and such a site calls
  it by name, skipping the var read (`#ifndef CLJC_LOCAL_…` around the head's borrow, which only the fallback
  of a cross-unit call needs) and passing nil as `self` when the arity binds no self slot; a cross-unit target
  keeps the registry pointer, a top-level form keeps calling the frame function (it is no frame itself, so the
  callee's frame must be the callee's own). A direct fn under a frame fn is inlined the same way, without a
  wrapper: its definer is its only caller. One level deep by construction: a leaf calls nothing directly, so
  what a marker names inside a frame is at most the leaf inside the frame. `otool -v -s __TEXT __cljframe` of
  the bench's `bench_sq_closed.dylib`: `bench_sq_to_a1` holds the `mul`/`smulh` of `(* x x)` and no `bl` to
  `bench_sq_a1`, whose only reference is the dispatcher's `b`; the standalone `bench_sq_a1` is 22
  instructions — frame record, the tag check, the multiply with its overflow check, the fixnum range check,
  the box. Measured (bench/RESULTS.md, "An empty prologue"): the accumulating loop calling `(defn sq [x] (* x
  x))` 8.3 ns per iteration as a dev unit (the var, the dispatcher, the wrapper) and 4.0 as a closed unit,
  against 1.4 with the square written out — what the inlined call still pays is the boxed calling convention:
  the argument boxed, unboxed behind a tag check, the result boxed, unboxed again by `+`. Trigger: a primitive
  entry beside the boxed one (design §6, worker/wrapper). The call rows themselves moved little — the plain fn
  call through a var 7.6 → 7.2, the closure call in a loop 6.9 → 5.6, the protocol call 8.2 → 7.6 — because the
  prologue was about 1 ns of them; what is left is the loop's own boxed compare, the var read and the registry
  pointer of a cross-unit direct call.
- **Pools.** A unit has `K[]` constants, `V[]` vars, `OP[]`/`B[]` intrinsic entries and their boot builtins,
  `F[]` fusion vars and `S[]` fn stubs — immortal static `clj_node`s of kind FN carrying only a name and a
  position, what traces, the profiler and signposts read — and `FR[]`, the frame table (above). `unit_pools` fills them before any form
  runs: a constant is `clj_c_const` over its printed form (`pr-str` at emit time, the reader with the
  namespace hooks at init, so keywords, symbols, strings, numbers, regexes, `#uuid`, `#inst` and collection
  literals all travel one way), a var `clj_c_var` (find-or-create the namespace, intern), the entries by
  qualified name. Constants are keyed by identity within a top-level form, not by text: two literals the
  reader made separately stay two objects (`(= ##NaN ##NaN)` is false, `(compare #{1} #{1})` throws) while a
  value a macro copied into two nodes stays one, exactly the object graph the interpreter's tree has. What
  the codec refuses (`clj_node_to_data`'s "not serializable": a fn, a type, a host value in a constant) the
  generator refuses too. `(var x)` constants are `V[]` entries.
- **Dev and closed** are one generated text: `CLJC_GUARD(var, boot)` is the intrinsic guard (`root ==
  boot builtin`, else `clj_c_intrinsic_fallback` through `clj_invoke`) and `CLJC_FUSED` the fusion guard
  (`clj_fusion_guard`, else the original program), both `1` under `CLJ_CLOSED`; every INVOKE whose head is a
  var defined in the compiled set with a captureless fn init at that arity is emitted as `#ifdef
  CLJC_DIRECT_<base>_a<n>` direct call / `#else` `clj_c_invoke` `#endif`, and the unit's prelude, under
  `#ifdef CLJ_CLOSED`, defines the macro and the `extern` for each target whose var is defined exactly once
  in the set and is not `^:dynamic` (a second definition anywhere turns the sites back into generic calls at
  write time). A target defined in the same unit is a static function pointer initialized to the function
  (LLVM folds it to a direct call); one from another unit is a pointer filled at the first call from the
  symbol registry (`clj_compiled_register_symbol`, which each closed unit's init fills for its own defs), so
  units load `RTLD_LOCAL`: a global image costs dyld ~200 ms per `dlopen` and grows with the loaded count. A user unit compiled with `--closed` defines `CLJ_CLOSED` at its top; `core.c` leaves it to
  the build (`-DCLJ_CLOSED` on top of `-DCLJ_COMPILED_CORE`, the closed bench variant), since a closed core
  cannot be `with-redefs`'d and the test suite needs that. Under `--closed` a user unit refuses `eval` and
  `load-string` (design §6); core.clj may name them. Dev keeps every var, `with-redefs`, `def` at run time,
  `eval` and `load-string` working over compiled code, the interpreter stays linked, and a var rebound from
  the REPL reaches compiled call sites through the same deref the interpreter makes.
- **Promoted slots.** `emit_top` runs `clj_facts_of` over each tree and every frame (closure arity, direct
  arity, top-level form) decides per slot whether it lives in the `clj_cframe` array or in a C variable
  `clj_value l<i>` (`promote_slots`). A promoted slot has one ownership for its whole life: *owned-or-nil* for
  a let, loop, catch or rest slot (declared `= CLJ_NIL`, bound by `clj_c_rebind`, which releases the old value,
  released at every exit of the function, before the array's `clj_c_release_slots`), or *borrowed* for a
  closure's fixed param or self slot that no fn-body `recur` rebinds (`clj_value l0 = args[0];`, never released,
  a rebind is an emitter fatal). A read is `l7`, an owned read `clj_retain(l7)`, a last use `t = l7; l7 =
  CLJ_NIL;` — owned statically, so a consuming intrinsic (`conj` on a loop accumulator) takes its in-place entry
  without the `o<n>` flag branch `clj_c_take` needs. The array keeps the slots that are not promoted, up to the
  highest such index; when none is left the function has `clj_cframe fr = {NULL, captured, 0, NULL}`, no
  `s[]`, no owned bits and no teardown loop. A slot stays in the array when: a closure captures it
  (`CLJ_CAPTURE_LOCAL`, the fact `captured`); a direct fn body reads it through the static link (`OUTER`, or a
  `CLJ_CAPTURE_OUTER` capture inside such a body) — the facts pass does not charge those to the definer, so
  `pin_walk` scans the frame's subtree, entering direct-fn bodies one level deeper and matching depth against
  level; it is a param a fn-body `recur` rebinds (the owner changes from the caller to the frame mid-call);
  it is a direct fn's param (owned per the caller's mask); the frame has more than 64 slots (`clj_c_retain_params`
  makes every entry owned and the teardown releases them all); it is a fused node's argument frame (the array is
  the argument array itself). The escape fact `escapes` does *not* bar promotion: it says the value leaves the
  frame (returned, stored, passed to an unknown call), which a C variable is indifferent to — only where the
  *slot* is read matters, and the lattice joins `captured` into `escapes`, so the emitter's own scan is the
  safety and the fact table is the census. Every array read the emitter still generates (`emit_outer`, the
  capture list) goes through `check_array_slot`, which walks the `definer` chain of contexts and fatals on a
  promoted slot: a scan that misses a reader fails the compile, not the program (the first version missed the
  arguments of `recur`, and core.clj's `group-impls` read `fr.slots[8]` of a frame without an array).
  `clj-compile --stats` prints the census per unit against docs/facts-coverage.md's population: core.clj 1813
  slots, 22.9 % `local` by the fact, 83.4 % promoted (395 of the 416 `local` slots; the 21 that are not: 17
  params a `recur` rebinds or a direct fn owns, 4 read through the static link); medley 1347 slots, 25.2 %
  `local`, 91.8 % promoted (src 75.6 %, test 99.7 %). What it buys at `-O2` is smaller than the census: when
  nothing takes the frame's address, clang's SROA already kept the array in registers — the counting loop's
  machine code is byte-identical before and after — and the array is only pinned to memory where `&fr` escapes,
  which is a direct call (the static link) and nothing else; the "loop with a local helper" row moves 7.0 → 6.4
  and the rest is noise (bench/RESULTS.md). A direct-fn caller sizes `ds[]` by what the callee's promotion left in
  the array (`direct_array_slots`: the entry the callee's arity recorded when its body was emitted — its params
  are always in it —, or the whole frame when an earlier arity of the same fn calls one not emitted yet), and the
  callee's teardown releases over the same count; `--stats` prints it as "direct-call arrays N of M callee slots":
  core.clj 103 of 194. A param a fn-body `recur` rebinds could be promoted with a
  runtime owned flag — trigger: a hot self-recursive fn showing the array store in a profile. The boxed `<`/`inc`
  the counting loop paid are the next entry's.
- **Unboxed arithmetic, int64 and double slots, entry-checked frames** (`emit_unboxed`, `emit_tag_checked`,
  `typed_masks`, `select_split`, `emit_split`; bench/RESULTS.md, "Specialized arithmetic" and "Specialized
  arithmetic over doubles"). `emit_top` builds the tree's table with the dev store (`clj_facts_of_with`, the join
  included, the store read without its lock since a compile is one thread; a compiled-eval form records its own
  fn-body sites first and reads the table again, so a callee defined in the same form sees its callers). Two
  **kinds** of unboxed value: `int64_t` for a fact that is a fixnum or a boxed long, `double` for a fact that is
  exactly a double; an arithmetic node over one of each is a double, as the `Numbers` ladder says, and an integer
  `/` stays boxed (a ratio). An arithmetic INTRINSIC of the interpreter's list is emitted in one of three forms.
  *Unboxed*: every argument is an **unboxable** expression — a fixnum or double literal (hex float, `__builtin_inf`
  and `__builtin_nan` for the non-finite ones), a typed slot, or such arithmetic over those — so the operation runs
  on the C values, `__builtin_*_overflow` for int64 and IEEE for double, and only the result is boxed
  (`clj_long_new` or `clj_double_new`; a comparison yields `clj_bool`, no allocation); an overflow throws "integer
  overflow" and unwinds where the interpreter would. Only under `--closed`: an unboxable expression has *no path
  that yields a box*, which needs the intrinsic guard to be constant; in dev a rebound `+` would have to take the
  generic path, whose result is a box. *Tag-checked*: every argument's fact has a kind but some come boxed (a
  parameter the join narrowed, a `count`) — the inline operation runs behind `clj_is_fixnum` or `clj_is_double` of
  the boxed ones, the table's function otherwise, under `CLJC_GUARD` as before; an unboxable argument is computed
  as a C value and boxed only on the generic path. `=` between an int64 and a double is generic in both forms: it
  is false by type, which a C comparison of the converted values would not say. Both forms in dev and closed.
  *Generic*: the call as in v0. The **typed slots**: a promoted owned slot is an `int64_t l<i>` or a `double l<i>`
  when it is bound at least once, every binding's fact has the same kind and every binding — let init, loop init,
  recur argument — is unboxable in that kind, which may rest on other typed slots, so the candidates shrink to a
  fixpoint; a read in a boxed position is `clj_long_new(l<i>)` or `clj_double_new(l<i>)`, owned, since a value
  outside the fixnum range allocates and a double always does; the slot is never released, and a fused node's
  argument frame has none (its locals are the argument array, not the enclosing frame's variables — the first
  version read them as such and passed a long where a seq was due). A loop whose variable turns double
  (`(recur (+ i 0.5))` from `0`) has bindings of two kinds and stays boxed. **Entry-checked frames**, the
  two-program form the FUSED node has: a loop whose own slot is fed by a *boxed* value of a typed fact —
  `(loop [i (count v)] …)`, a parameter the join typed — or whose body reads such a value bound once by a `let` in
  scope (`(let [n (count v)] (loop … (< i n)))`) is emitted twice. The inits run once, boxed; then
  `clj_is_fixnum`/`clj_is_double` of every such value chooses the fast branch, where the typed C variables shadow
  the boxed ones for the extent of the loop (with whatever the fixpoint types beside them once those are typed:
  the accumulator whose recur argument adds the count, the accumulator fed by an outer loop's variable), or the
  generic branch, the loop as it would be emitted without the split. A slot typed only in the fast branch is
  bound only inside the loop or checked at its entry — a sibling's binding of the index would write the boxed
  variable while the shadow is read —, and a read after the loop is of the boxed one. A wrong fact — a bigint at
  entry, a call from the host with a string — costs the check and takes the generic branch. One split per frame,
  the outermost loop, never inside a branch: a split in a split doubles the body again; and only when an
  arithmetic node of the body reads a slot it would type, since otherwise the check buys nothing. Why the tag
  check and not the epoch: the join is a fact about the recorded callers, and a closed unit is still called from
  the host and from top-level forms the index does not record; the design's "closed removes the guard" holds for
  the var-root guard, which `CLJ_CLOSED` folds, and for the typed slots whose every binding the unit itself
  computes. A wrong fact therefore costs a failed tag check and never a result; the differential gates
  (`corpus-compiled` dev and closed, the fixtures dev and closed, `test-compiled`) are what keep that true, and
  `arith.clj` is the fixture that tries to break it: overflow at the fixnum edge and at the int64 edge in unboxed
  and tag-checked nodes, a loop variable that turns double, a double and a string through every specialized fn,
  `apply`, `map` over the fn, a direct fn with an int64 loop, a fused node under one, every operator over doubles
  and over a fixnum beside a double with `##Inf`, `##NaN` and `-0.0` through them, a NaN loop variable, a loop fed
  by a `count`, by a let-bound `count`, by a parameter the join typed, by an outer loop's variable, a `count` read
  after its loop, and the same entered with a bigint, a double and a string; `rebind.clj` (dev only, it rebinds `*`) is the root-rebind case. `core.c` is one text for
  dev and closed, emitted without `--closed`, so the closed core gets the tag-checked nodes (46 in core.clj) and no
  typed slots. `clj-compile --stats` counts the forms per unit: `arith.clj` closed, 27 int64 slots, 4 double
  slots, 66 unboxed, 58 tag-checked, 9 entry-checked frames; dev, 98 tag-checked; the closed test suite has 8
  int64 slots and 2 entry-checked frames, medley none — the corpus has few loops, and the fixture and the bench
  are where the forms are exercised. Measured (the "loops compiled `--closed -O2`" column): the counting loop
  3.6–3.7 → 2.9 ns per iteration — `i` is an `int64_t`, `(inc i)` an add with an overflow branch, and what
  remains is `(< i n)` boxing `i` for a `clj_lt` call, since `n` comes from the host and has no fact, plus the
  deadline tick —, the accumulating loop 6.9–7.2 → 3.9, and both **1.3 ns** with the bound known from a def'd
  caller in the same form: the loop is a compare against the untagged parameter behind one tag check, an add, the
  tick. A loop accumulating a double 13.8 → 3.0: the `clj_double_new` and the release per iteration are gone, `x`
  is a `double`. The accumulating loop with its bound from `(count v)` **1.2 → 0.5** with the count in a `let`
  and 2.2 → 0.5 with the count as the loop's entry value: the fast branch is a compare, an add and the tick, no
  tag check inside the loop. `swap! inc` 22–25 → 21. A dot product over two vectors through `nth` moves only by
  its counter (38 → 36): `nth` answers ⊤ — trigger: an element fact for `nth`, which the lattice does not carry.
  What remains and its trigger: a parameter used directly in the loop's arithmetic is tag-checked at every use
  (`(< i n)` above), since a parameter is bound by the caller and cannot be a C variable — trigger: a profile
  with a hot loop over a parameter, then the parameter as a checked slot of the split (it is bound once, like a
  let's); a second split inside a fast branch — trigger: a nested loop whose inner entry is boxed, in a profile;
  a `let` without a loop, whose typed reads stay tag-checked — trigger: a hot straight-line body in a profile.
- **The primitive entry: worker/wrapper** (design §6 "worker/wrapper = наши два входа, боксовый + примитивный", §6b
  "unboxed-конвенция между функциями"; `emit_worker`, `emit_result`, `prim_site_of`, `emit_prim`, `target_entry`;
  compiled_internal.h `clj_wlong`/`clj_wdouble`, `clj_c_as_int64`, `clj_c_unbox_long`, `clj_c_prim_fallback`,
  `clj_compiled_register_worker`; bench/RESULTS.md "The primitive entry"). *What qualifies.* Under `--closed`, a
  top-level def'd fn's fixed arity of 1–8 parameters and at most 64 slots whose caller join in the form's table
  (`clj_facts_join_at`) puts every parameter in a numeric domain — int64 (fixnum|long) or double — whose summary
  specialized to those domains (`clj_summary_of_var_at`, the Facts entry) answers a domain for the result, whose body's
  own fact under the join agrees, and whose parameters can be C values: not captured, not read through the static
  link, and rebound by a fn-body `recur` only with an unboxable expression of the same kind (`typed_masks` seeds them
  as bound at entry). *The worker* is a second C function, `<base>_a<n>_<sig>` with `sig` = `w_<kinds>_<ret>` — `l` for
  int64, `d` for double, `w_ld_d` a (long, double) → double — taking `(self, captured, int64_t p0, double p1)` and
  returning `clj_wlong`/`clj_wdouble`: the unboxed value beside a `thrown` flag, which is `CLJ_THROWN`'s spelling on
  the primitive path (the exception is pending as usual). A two-register struct return: no memory on either side,
  the flag folds away once the worker is inlined, and a cross-unit call through a pointer pays one register and one
  branch — the out-parameter alternative would pin a stack slot for the flag on every call. The body is emitted a
  second time with the parameters as typed slots (`int64_t l0 = p0`), so every operation over them is unboxed; the
  result is raw where the body is an unboxable expression, through `if`/`do`/`let` where the branches are
  (`emit_result`), and otherwise the boxed emission unboxed once (`clj_c_unbox_long`; a mismatch is fatal — a lattice
  bug, never a program error, and it is never reached from user data since the parameters are C-typed). A worker is a
  frame fn of its own (`CLJC_FRAME`, in `FR[]` under the fn's stub, so a throw inside it names the fn) and a leaf
  worker (no direct calls, under 3000 bytes) also an always_inline twin `_i`. *The wrapper* is the boxed arity
  function as written, and when the worker is a leaf, the boxed twin `<name>_i` tries the worker first behind
  `clj_c_as_int64`/`clj_c_as_double` of the arguments and boxes the result, so a boxed caller — the host, a dev unit,
  a site whose argument is a `count` — unboxes once and runs the same code; a non-leaf worker is not tried from the
  wrapper, because a frame calling a frame of the same fn would show the fn twice in a trace. *Sites.* A direct call
  inside a frame fn (a top-level form's sites are not in the reverse index, so the join never saw them) whose every
  argument is an unboxable expression, of kinds K, and whose result kind R the store's specialized summary at K gives,
  is a primitive site: `unboxable_kind` answers R for the INVOKE, so `(+ acc (sq i))` types `acc` and the result flows
  into an `int64_t` without a box. Its text is two programs under `#ifdef CLJC_PRIM_<target>`: the worker by name
  (`CLJC_CALL_`, the twin for a leaf) or, for another unit, through the registry by its full name
  (`clj_compiled_worker`, a `void (*)(void)` cast back to the signature's type) with `clj_c_prim_fallback` — the
  var's root over the boxed arguments — while the symbol is not registered; else the boxed call exactly as before.
  The prelude defines the macro only when the set's callee emitted a worker of that signature (`target_entry`): a
  callee without one downgrades the site to the boxed call, and a callee with *another* signature at that arity stops
  the compile (`clj_fatal`, the build-time error): both sides derive the signature from one store entry, so a
  disagreement is a compiler bug, never something to bind through. The registry name carries the signature, so a unit
  compiled against other facts finds no symbol and falls back rather than misbinding. Nothing is specialized on what
  the analyzer did not record: the join and the specialized summary are the two facts, read by the callee and by every
  site alike, and the interpreter reads the same store. `quot` and `rem` are unboxable int64 operations now (the
  intrinsics entry, `emit_int64_op`): a zero divisor throws "Divide by zero", `INT64_MIN` by −1 "integer overflow", as
  `long_arith` does; over doubles they stay generic (the builtin rounds). `clj-compile --stats` counts `workers` and
  `primitive sites … of which bound`; `arith.clj` covers a leaf worker from a typed loop and from the top level with a
  double, a string and a boxed long, a double worker, a worker over `if`/`let`, a fn-body `recur` (`gcd`), a worker
  calling a worker, a throw inside the worker (`(qr 1 0)`, `(qr INT64_MIN -1)`), a `count` through the wrapper, `map`
  and `apply` over the fn. Measured (bench/RESULTS.md, "The primitive entry"): the accumulating loop calling `(defn sq
  [x] (* x x))` as one closed unit 4.0 → 0.7 ns per iteration — under the 1.4 of the square written out in a
  compiled-eval form, because `bench-sq-to` is a worker itself and its bound `n` an `int64_t` parameter, so the loop's
  compare is untagged too; `(quot i 3)` through a helper 3.4 → 0.5, the same as written out, the zero-divisor branch
  costing nothing on the path; a double helper 32.4 → 0.7, the boxed call having paid a `clj_double_new` and its
  release per iteration. `otool` of the unit: the sq-to worker's loop is thirteen instructions — the multiply with its
  overflow check, the add with its, the tick's two loads, the increment, the compare — and no `bl`. In core.clj
  compiled closed one arity qualifies (`mod`); of the other 326 non-variadic arities of 1–8 parameters 175 have no
  recorded caller, 99 a site passing ⊤ at some position, 34 are read first-class, 15 have a non-numeric parameter by
  the join and 3 a numeric join with a non-numeric result (`range`, `rand`, `rand-int`). *A self-recursive fn* gets
  its worker since the fn's own site enters the entry's own join (the Facts entry, the caller join): `fact`, `fib`,
  `halve-n` over (double, int64) and both arities of `(defn sum-to ([n] (sum-to n 0)) ([n acc] …))` in `arith.clj`,
  the recursion a call from the worker to itself (`CLJC_CALL_` by name, an `int64_t` in, a `clj_wlong` back), a
  self-site passing ⊤ (`rec-top`, a deref) still none; a worker that calls is no leaf, so the boxed wrapper does not
  try it and a top-level `(fact 21)` overflows on the boxed path where `(defn fact-over [] (fact 21))` overflows in
  the worker, the same message. Measured (bench/RESULTS.md, "The self-recursive worker"): `(fact 20)` 6.3 → 1.5 ns
  per recursive call, `(fib 25)` 8.1 → 1.7 per call. In core.clj it adds no arity: none of the 118 arities with a
  narrowed join has a site of its own arity (core's recursive fns are over seqs, through another arity, or read
  first-class), so the count stays at `mod`. *Deferred, each with its trigger.* Two def'd fns recursing through each
  other (`(defn ev [n] … (od (dec n)))`, `(defn od [n] … (ev (dec n)))`) get no worker: each one's site in the other
  is an ordinary recorded site, first recorded with the other's parameters at ⊤, and a re-derivation only ever
  re-records under the join that site itself made "a number" — the cycle sits at its greatest fixpoint, and the self
  rule reaches one arity, not a component. Trigger: a hot mutually recursive numeric pair in a profile, then the
  self rule over a strongly connected component of the def'd call graph: the sites inside the component dropped from
  the index, its joins started from the external sites alone and closed together. A non-leaf worker is not tried from the
  boxed wrapper — trigger: a boxed caller of a large numeric fn in a profile, then a trace rule for a frame under its
  own wrapper. A variadic `+` is no intrinsic and answers "a number" — trigger: the optimizer's n-ary lowering. A boxed
  argument with an int64 fact (a `count`, a parameter of a fn that is no worker) takes the boxed twin and its tag check
  rather than a primitive site: the box of a fixnum and nothing else — trigger: a profile. A let-bound direct fn has
  no specialized summary (`clj_summary_of_arity`) and no worker — trigger: a numeric helper in a `letfn`.
- **Refused** (reported with the node kind and position, the unit throws at the form, `clj-compile` exits 2
  unless `--allow-refused`): a constant that does not print and read back; `eval`/`load-string` in a
  `--closed` user unit. Every node kind is expressible; nothing in core.clj, the embedded libs, medley or the
  suite is refused in dev mode, and in closed mode only the suite's `eval` test is.
- **Names.** `cljc_mangle`: `.` and `-` become `_`, `_` becomes `_USCORE_`, `? ! * + > < = / ' & % # : $`
  become `_QMARK_ _BANG_ _STAR_ _PLUS_ _GT_ _LT_ _EQ_ _SLASH_ _QUOTE_ _AMP_ _PCT_ _HASH_ _COLON_ _DOLLAR_`,
  anything else `_u<hex>_`; `clojure.core/map` is `clojure_core_map`, its arities `clojure_core_map_a1 ..`,
  the fns nested in a top-level form `<base>__<k>` in pre-order (`user_my_fn__3`), a form that is no def
  `<ns>_form<n>`, a second definition of a name in one unit `<base>__r2`. Demangling: the namespace is the
  longest known namespace prefix, `_` in the rest is `-` unless it starts a `_TOKEN_`, `__<k>` is the k-th
  fn inside, `__r<k>` the k-th redefinition; a dot inside a name is not told from a dash. `#line` directives
  name the source (the package-relative `Sources/CljCore/boot/core.clj` for core, `<embedded>/...` for the
  libs, the load path's file otherwise); `--no-line` drops them.
- **Gates.** `CompilerFixtureTests`: each `Tests/PippinTests/Fixtures/compiler/<kind>.clj` prints through
  the interpreter and, as one unit built and registered in-process, through the compiled backend, both
  against `<kind>.out` (`CLJ_FIXTURE_UPDATE=1` rewrites), and a second compiled run must not grow the live
  count by more than a second interpreted run does. `make test-compiled`: the suite on the compiled core in
  the pool and ASan modes. `make corpus-compiled`: `clj-compile --lenient` per library in a child process,
  clang per file, `dlopen`, `clj_compiled_register` by path so the harness's `require`s run the units, and
  the per-line report (`CLJ_CORPUS_REPORT`) diffed against the interpreter's — identical for both corpora;
  `CLJ_CORPUS_CLOSED=1` compiles them `--closed`, where only the suite's `eval` test differs, by the refusal;
  a refused form is a failure unless `corpus/<lib>/refused.edn` lists its file and line with a `:note`.
  `make test-eval-compiled`: the whole suite with every host eval compiled (`CLJ_EVAL=compiled`, the weak
  `clj_compiled_eval_boot` `clj_init` calls); each unit's pool objects are taken out of the live count
  (`clj_debug_live_objects_exclude`) because they live for the process by design.
- **Protocol calls** (`emit_proto_call`, `receiver_arms`, `arm_target`; compiled_internal.h `clj_c_arm_hit`,
  `clj_c_proto_ic_call`; bench/RESULTS.md "Compiled protocol calls"). An INVOKE whose head var holds a protocol
  method at emit time (the compile evaluated the file first, so the var and the tables are final) is emitted as a
  chain over `pt = clj_dispatch_type_inline(receiver)` and `pe = clj_epoch_load()`: the **arms** the receiver fact
  allows, then the **inline cache**. *The CHA rule*: each kind of the fact names a descriptor when exactly one
  dispatches through it — nil, boolean and char their pseudo-descriptors, fixnum and long `clj_long_type`, the
  other builtins theirs, a `record`/`host` fact its descriptor when it carries one, else the protocol's single live
  user implementor of that kind (`clj_proto_each_user`, proto.c's registry of live deftype/record/reify
  descriptors, since the caller join drops the descriptor); a deftype is guarded through its var
  (`clj_c_var_type(V[k])`, the var found from the descriptor's `ns.Name`), so a reify or a type no var holds gets no
  arm, and so does a `seq` (many descriptors). An arm is emitted only when the impl the tables hold for that
  descriptor is a closure of the compiled set with a fixed arity that reads neither `self` nor `captured` (no
  captures, no self slot, at most 64 slots), or a compiled closure whose dispatcher a closed unit registered
  (`clj_compiled_register_impl`: the fn values of every `deftype*`/`record*`/`extend` method map, and whatever
  another unit of the set names). *The switch shape*: at most four arms (the lattice's cap), each
  `if (pt == D && (clj_c_arm_hit(&PA, pe) || clj_c_arm_fill(…))) r = IMPL_aN(CLJ_NIL, NULL, args, n); else …`,
  the last `else` the cache; `PA` is one `_Atomic uint64_t` per arm holding the epoch at which the tables were last
  seen to bind D's impl to that symbol, the fill a table lookup comparing the impl's dispatcher against the symbol
  and storing the epoch only when it still stands — so a wrong fact (the host boundary), an `extend` after the
  compile or an impl replaced later in the same file (the arms name the *final* impls; before the replacing form
  runs, the fill fails every call and the cache serves) costs a fallback and never a result. The symbol is
  `CLJC_PIMPL_<id>_CODE`/`_FN`, defined in the closed prelude at write time (the impl's fn may be emitted after the
  site, `twice` calling `once` on `this`): a static of the same unit by name, another unit's through a
  `clj_cproto_impl` filled from the registry at the first fill; undefined, the arm is compiled out. The macros sit
  under `CLJ_CLOSED`, so dev units and the dev core see the cache alone, and `core.c` (one text) takes the arms in
  the closed build. *The cache*: one `static _Thread_local clj_cproto_ic {method, type, impl, epoch}` per site — per
  thread so a fill never races a hit, which is why no seqlock and no reader window: a hit is `epoch == pe && type
  == pt && method == fn`, then the impl's dispatcher directly (`clj_c_call_impl`, a compiled closure by `ctx == fn`,
  anything else through `clj_invoke`). The cell *owns* its impl, and a replacement releases the old one through
  `clj_eval_retire_root` (parked until this thread is idle, as a rebound fn root is), so a call in flight on this
  thread keeps its impl; an `extend` on another thread against a call in flight is the same accepted race as a
  `def` against a running root ("Concurrent `def`"). The epoch is the process one, so any `def` empties every cell
  and arm on its next hit — one table walk each, the interpreter's cost. *Folded predicates* (`--closed` only):
  `(satisfies? P x)` with a known receiver is a chain of the same shape whose arm answers a constant computed at
  emit time (`clj_proto_extends` on the descriptor) behind a cell `clj_c_satisfies_fill` verifies against the
  runtime answer once per epoch; `(extends? P T)` with `T` a var holding a type the same with one cell, since a
  rebind of `T` bumps the epoch. *What stays generic*: a head that is not a var, a `^:dynamic` one, a var that holds
  no method at emit time (a protocol defined in the same compiled-eval form), an impl that is a native or a
  reify trampoline or captures, a variadic impl, past four descriptors, and a receiver whose kind has many
  descriptors — all of these through the cache, whose miss is the method's own tables. `clj-compile --stats` counts
  the sites as written — each site keeps its arms' ids and `cljc_unit_slots` classifies them after `cljc_end`
  with the prelude's own `pimpl_defined`, so an arm compiled out is a cache site: core.clj 8 direct (the multimethod API on `MultiFn`, `-realized?` on `Delay`), clojure.test 16 direct,
  medley none, the test suite 2 cache (`defmethod` expansions whose impl is the interpreted core's closure, not of
  the set; under the compiled core they resolve). Measured (bench, closed compiled `-O2`): every monomorphic row
  17–19 → 8.5–9 ns, the plain fn call's 8; the bi-morphic 22 → 12; arm and cache within a nanosecond of each
  other, since what is left is the callee's entry. Triggers: a multimethod call is `MultiFn`'s `invoke` slot
  through core.clj, untouched — a static hierarchy switch is the design's next step; the caller join keeping the
  descriptor under the type's epoch would give arms to a parameter of a protocol with several deftype
  implementors, which today only a constructor-derived fact reaches; the entry protocol is gone (the empty
  prologue entry) and the row sits at 7.6–7.8: what stands between that and the design's 5 is the boxed
  argument array and the loop around the call.
- **Deviations and skips, each with its trigger.** Trace positions as above; trigger: a host wanting caller lines from compiled code, then a line
  in each `CLJC_SITE` marker (the emitter knows the call's position) read by the walk in place of the fn's own. A closed unit binds a direct call to the registry's
  latest entry at its first call and never again, so redefining a var across compiled-eval forms under
  `CLJ_EVAL_CLOSED` is wrong by design (a bench tool). Every unit exports its top-level fns' arity functions
  as globals; with `RTLD_LOCAL` loads they clash with nothing. `clj_compiled_find`
  is a linear scan of registered paths. The compiled eval costs ~0.6 s per form on macOS 15: clang ~0.14 s and
  `dlopen` ~0.3 s, the latter the system's first-load assessment of every new code signature (a second load
  of the same dylib is 1 ms, and 30 fresh trivial dylibs take 9 s from a C program too); the whole suite is
  hours, which is why the gate is opt-in. The wait is syspolicyd's YARA scan of every first-loaded image on macOS 14+, cached by hash, so no
  signing or permission avoids it; a compiling eval is not a goal (design §9), and if it ever is, the fix is loading the
  object into `MAP_JIT` memory (own Mach-O loader, tcc or ORC), not a file per form. Compiling a file evaluates it (the hook cannot skip
  evaluation without losing macros), so `clj-compile` runs the program once. Nothing checks that
  `boot/core.c` matches `boot/core.clj` the way `CoreCljTests` checks `core_clj.inc`: the generator needs an
  interpreted boot with the hook armed, which a test process past `clj_init` cannot redo; trigger: a stale
  `core.c` slipping through, then a CI step diffing `make boot`'s output. A `reify` site keyed by its
  gensym makes a new type when the protocol it names was redefined (proto.c `reify_type_current`): the
  fixture reloads showed a stale type implementing the old protocol, which the interpreter has on any
  re-evaluation of a `defprotocol` too.

## Benchmarks (bench/)

- Numbers drift between sessions (thermal, background load). Compare only within one run; use
  `CLJ_SYSTEM_ALLOC=1` on the same binary as the control.
- The "C iterator" number (3.8 ns/element) moves to 4.3 with identical machine code when the linker
  places `clj_seq_iter_next`/`clj_vector_nth` differently; `aligned(64)` on both brings it back.
  Compare that row across builds only with the alignment forced, or read it as ±0.5 ns.
- The same for the call rows of the compiled-core binary, at ±1.5 ns: a change to facts.c alone moved every
  protocol-call row 7.7 → 9.1 with byte-identical compiled-eval C (`CLJ_EVAL_KEEP=1` keeps it for the diff), and
  `-Xcc -falign-functions=64` on both sides took the gap to 0.3 (bench/RESULTS.md, "The self-recursive worker").
  A CljCore change that shows less than that on a call row it does not touch is layout until the aligned build says
  otherwise.
- Not yet measured: multi-threaded reads of a shared map, assoc from a shared base across threads,
  cross-thread free, cost of `clj_share` on a large graph, forcing one shared lazy seq from many
  threads (the CAS claim path).

## Open decisions

- **File extension and reader-conditional key.** Source stays `.clj` (`.cljc` for portable user
  code) until the project has a name; the key in `#?(:key …)` and the extension are the same word
  and permanent, and they should name the runtime (portable C core), not Apple or Swift. Reader
  conditionals are in (the reader takes any feature set), so the default set is `#{:default}` alone until
  the key exists; the corpus harness reads medley with `#{:clj}` so its JVM branches surface as
  resolution errors in the backlog rather than as silently empty bodies.
