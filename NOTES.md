# Engineering notes

Known simplifications in the runtime, each with the event that makes it worth fixing.
Delete an entry when it is done. Architecture-level decisions live in clojure-apple-design.md.

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
  `IPersistentMap/Vector/List` cannot be implemented (no assoc/nth slots — trigger: the first user
  map or vector type); `Object` methods
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
  a megamorphic cutoff: a site cycling through more than four receivers in a profile.
- **No `defrecord`, no `.-field` access, no protocol inheritance.** A deftype's fields are positional
  slots read through `field*`, visible as locals inside its own method bodies only; from outside
  there is no accessor. A deftype carries meta only by implementing `IObj` itself (a field for it).
  A protocol cannot extend another. `extend-type` on a core interface as the *type*
  (`(extend-type ISeq P ...)`) covers every type with those bits, on the concrete type missing; a
  user protocol cannot be a type designator. Trigger: the first record-shaped state (then a shape
  descriptor with map slots) or the first `(.-x o)`.
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
  `Character`, `Keyword`, `Symbol`, `PersistentVector`, `PersistentHashMap`, `PersistentHashSet`, `PersistentList`/`Cons`,
  `EmptyList`, `LazySeq`, `Range`, `Fn`, `Var`, `Namespace`, `ExceptionInfo`, `HostError`,
  `Protocol`, `Type`, `Reduced`, `Volatile`, `Object`; the core interfaces `Seqable ISeq Sequential
  IPersistentCollection Counted ILookup Associative Indexed IFn IHashEq IEquiv IMeta IObj
  IReduceInit IPersistentList IPersistentVector IPersistentMap IPersistentSet IExceptionInfo`) holding descriptors; `(type x)` reaches every
  other one and `nil` is the literal.
  A user `(def String ...)` shadows the name. `Number` does not exist: fixnum and double are two
  descriptors, extend both.
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
  vector passes the index), set (elements in trie order), `()`, and cons / lazy-seq / string / string-seq through
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
- **A cons is `list?`** (CLJ_CORE_LIST) so reader lists, which are cons chains, satisfy the
  predicate; Clojure's `Cons` is not `IPersistentList`. Goes away with the `clj_list` wrapper below.
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
  value slots (memory of big sets); `sorted-set` (a user); `clojure.set` as a namespace (the `ns`
  form, see core.clj).

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

- **A cons chain has no count slot** (`count` walks it) and no hash cache, so hashing a list walks it
  every time. Trigger: lists as map keys or `count` on long lists in a profile. Fix: a `clj_list`
  wrapper with count and hash cache, as Clojure's PersistentList; cons stays the 32-byte cell for
  `cons`/lazy seqs and loses CLJ_CORE_LIST then.
- **Hash and equality recurse on nesting depth** (`clj_hash` → element hash). Reading and printing are
  iterative, so a 200k-deep literal reads and prints but crashes when hashed. Trigger: untrusted input
  used as a map key. Fix: an explicit stack in `clj_seq_hash`/`clj_seq_equals`, or a depth cap.

## Reader (Sources/CljCore/reader.c)

- **Not supported, reported as errors**: regex literals (no engine; `clojure.string` takes literal
  patterns), namespaced maps `#:`, tagged literals, read-eval, bigint/BigDecimal/ratio numbers. Each is a
  `switch` arm in `read_dispatch`/`parse_number` to replace when the feature lands.
- **`#(...)` rewrites its body after the list is read** (`fn_literal`): `%`, `%N` (1–20), `%&` become
  `p1__N#`/`rest__N#` params of a `fn*`, with one recursive walk over the literal's own nesting (the
  reader is otherwise iterative). Nested `#(` is refused, as LispReader does.
- **Reader conditionals** `#?`/`#?@` select the first branch whose feature is in `clj_reader.features`
  (a set of keywords copied from the process-wide `clj_reader_set_features` at init; `:default` always
  matches; nil means `:default` alone). No branch → the form reads as nothing (an EOF at top level, a
  missing item inside a collection); `#?@` splices only into an enclosing collection. Which key names
  this runtime is still open (Open decisions); the corpus harness sets `#{:clj}` per library.
- **`::kw` and `::alias/kw`** resolve through `clj_reader.resolve_ns` (`clj_reader_resolve_ns`: the
  current namespace or one of its aliases); with the hook NULL they are reader errors, an unknown alias
  is "Invalid token". Hex, octal and `NrDDD` radix integers read into fixnums; out of range is the
  bigint error.
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
- **Shadow stack** (shadow.c): a per-thread ring of `{fn node, call site}` pushed and popped around
  every closure body (natives are leaves), 8192 frames, calloc'd on the thread's first push (128 KB)
  and freed when the thread exits. Deeper than that, the innermost frames are kept and
  `clj_shadow_stack_dropped` counts the outermost ones overwritten; a test shrinks the capacity with
  `clj_debug_shadow_stack_set_capacity` since Swift Testing's stacks overflow the C stack long before
  8192 (the main thread of a release build can reach it). `clj_shadow_stack_snapshot` is
  async-signal-safe: it reads through a pthread key rather than the `_Thread_local`, because a first
  touch of a `_Thread_local` on a thread that never ran Clojure allocates under dyld. The stack guard's
  limit lives in the same struct, so a call pays one TLS load for both. Cost per call: that load, a
  null check, two stores, an increment and a decrement with a compare, plus one load and branch on the
  instrumentation byte (bench/RESULTS.md: within the run-to-run noise of the closure-call scenario). The
  C stack is still what limits recursion depth; the shadow stack does not replace it.
- **Crash handler** (`clj_crash_handler_install`) is opt-in: a host with its own crash reporter
  (Crashlytics, MetricKit) must not have its handlers replaced, and calls `clj_shadow_stack_snapshot`
  from its own instead. Installed, it writes the frames with `write(2)` only (names are borrowed from
  the fn node's symbol, numbers formatted by hand) and re-raises with the default disposition. No
  alternate signal stack is set up, so a C stack overflow gets no report unless the host installs
  one. Tested on SIGUSR1 through a pipe on a plain pthread: `raise` on a dispatch worker thread
  cannot `pthread_kill` itself and delivers the signal to whichever thread has it unblocked.
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
  get a fixed 512 KB assumption measured from the first call. Trigger: a Linux port.
- **Vars are immortal.** Every `def` of a new name leaks a var, its name symbol and string for the
  life of the process, as do namespaces; tests declare their vars before taking live-object baselines.
- **No ratio, no bigint.** Fixnum overflow throws "integer overflow"; `/` of fixnums yields a fixnum
  only when exact and a double otherwise. Trigger: any arithmetic that expects promotion.
- **Analysis error messages are capped at 512 bytes** (`fail` formats into a fixed buffer): a huge
  unresolved form is truncated in the message.
- **Nodes are pool objects with a 14-arm union**, so a `const` node pays for the fn arity table.
  Trigger: memory of a large loaded program. Fix: per-kind sizes via `clj_alloc(size)`.

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
  ex-cause`, the atom API below. No `keys`, `vals`, `max`, `mod`, `sort`, ... Most of the rest belongs in core.clj.
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
  `take-while`/`iterate`/`repeat` in core.clj. No `Range` for BigInts until those exist.
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
  remove-all-methods`, the namespace functions `require use refer refer-clojure loaded-libs` and the `ns`
  macro, and the private helpers `check-bindings`, `maybe-destructured`, `sigs`, `print-doc`,
  `preserving-reduced`, `load-one`, `load-lib`, `load-libs`, `libspec?` (`destructure` is public, as in
  Clojure). `clojure.set`, `clojure.string`, `clojure.walk`, `clojure.template` are separate embedded
  namespaces loaded on the first `require`. Not yet: `defrecord`, `defstruct`, `proxy`, `reify`-style
  `IDeref`, `sorted-map`/`sorted-set`, `format`, `re-*`, `future`/`pmap`/`agent`, `ref`, `dosync`,
  `with-local-vars`, `time`, `partition-all` transducer flush order, `chunk-*`.
- **clojure.test** (boot/clojure/test.clj) covers `deftest deftest- set-test with-test is are testing
  thrown? thrown-with-msg? use-fixtures (:each/:once) compose-fixtures join-fixtures test-var test-vars
  test-all-vars test-ns run-tests run-all-tests run-test run-test-var successful? report do-report
  assert-expr assert-predicate assert-any function? *load-tests* *stack-trace-depth* *report-counters*
  *testing-vars* *testing-contexts* *initial-report-counters* inc-report-counter testing-vars-str
  testing-contexts-str with-test-out`. `report` and `assert-expr` are multimethods, so a library adds
  its own heads (`(defmethod t/assert-expr 'p/thrown? ...)`). A failure's `:file`/`:line` come from the
  `is` form's reader position and `*file*` at expansion (`*assertion-pos*`), the var's own position when the
  error is outside an assertion; there is no stack trace to read them from, and `*file*` is nil for
  source the host evaluates. `thrown-with-msg?` takes a string the message must contain (a regex literal
  does not read). Fixtures live in an atom keyed by namespace name (namespaces carry no meta);
  `run-all-tests` takes a predicate on the namespace name, not a regex; no `*test-out*` (output goes to
  the process hook, `with-out-str` captures it); test vars run in `:line` order. Deviation: `is` binds
  `*assertion-pos*` per assertion (a frame push and pop, ~200 ns), where Clojure's reads the stack.
- **Semantics that differ from Clojure**, each kept for a reason: `case` compiles to `cond` over `=`
  (O(clauses), no jump table); `letfn` rebinds every name from a volatile at each body's entry (closures
  copy their captures when made, so a forward reference must be read at call time); `transient`,
  `persistent!`, `conj!`, `assoc!`, `dissoc!`, `disj!`, `pop!` are the persistent operations themselves
  (the in-place path is the auto-transient of design §6b, so a code path written for transients just
  works; the use-after-`persistent!` check is not made); `defonce` is a macro over `bound?`; multimethods
  dispatch by `=` with a `:default` fallback and no `isa?` hierarchy or `prefer-method`; `delay` is
  not `realized?`; `rand` is SplitMix64 seeded per thread from the id counter; `upper-case`/`lower-case`
  map ASCII letters only (no Unicode case tables in the core); `subs`/`index-of` count code points where
  Java counts UTF-16 units; `clojure.string/split` and `replace` take a literal string or char pattern,
  never a regex (`split` on a string is a deviation: Clojure's takes only a regex). Triggers: a corpus
  test failing on any of these.
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
  way (a path copy per new element). No `halt-when`, `random-sample`; trigger: first use.
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
- **A cooperative deadline bounds what a thread runs** (`clj_deadline_set_ms`): a closure call (`run_body`)
  and a `loop` turn check it, the clock is read once per 1024 of them, and past it the check throws
  `CLJ_DEADLINE_MESSAGE`. The fields live in the shadow stack, which those paths already load, so off it
  costs one predictable branch. A caught timeout keeps the deadline: the handler gets an unwind budget of
  calls and, after a fixed number of those budgets, every check throws, so a loop that catches the timeout
  still stops. Cooperative only: a native that loops without calling back into Clojure is not interrupted
  (`(hash (range))` is such a loop). The corpus watchdog is the one user so far; an untrusted-code host is
  the other.
- **`fn` has no `:pre`/`:post` conditions**: a map as the first body form is evaluated and discarded
  like any expression. Trigger: the first `{:pre [...]}`; the `fn` macro then wraps the body in
  `assert`s as Clojure's does (`assert` is defined below it, so the wrap must use `when-not`/`throw`).

## Corpus (corpus/, Tests/ClojureTests/CorpusTests.swift, docs/corpus.md)

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
  `:missing` (the symbols the runtime lacks, extracted from the message) or `:design-line` (a line of
  design §8). Entries the generator cannot classify carry an empty `:missing` and the reason text: those are
  wrong-result failures, backlog items to fix or to annotate by hand.
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
- **Known reader gaps the suite hits**: a tagged literal (`#cpp`, `#inst`, `#uuid`) anywhere in a file,
  even inside an unselected `#?` branch, is a reader error that ends the file (Clojure reads unselected
  branches with tags suppressed; fix: an F_TAG frame that drops the tag inside a `#?` and errors outside);
  `0x7FFFFFFFFFFFFFFF` and friends in `number-range` exceed the 63-bit fixnum, so every `r/max-int`-style
  constant is missing (bigint); regex literals; `#:ns{}` maps. Symbols the suite needs from the JVM:
  `clojure.lang.LazySeq` (`p/lazy-seq?`), `Throwable` in `catch` works, `instance?` of JVM classes does not.
- **scripts/api-diff.clj** is the JVM side of step 5 (dump `(ns-publics 'clojure.core)`, diff against a
  dump of ours, weight by corpus uses, write docs/api-parity.md); written, not yet run: the runtime-side
  dump executable and the `make api-diff` target do not exist.

## Host bridge (Sources/Clojure, error.c host-error, fn.c context natives)

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
  same frames back to the core, so a non-error value keeps them across the boundary.

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
- **`compare` and `sort` are the first residents** (Primitives.swift). They bind into `clojure.core` from
  `clj_host_boot`, a weak C hook `clj_init` calls last, defined by the Swift module with `@_cdecl`: a raw
  `clj_init()` and `Runtime()` boot the same core, tests take baselines after either. Their roots are
  ordinary (not immortal, read owned). `sort` is a bottom-up merge sort over the retained items of any
  seqable, stable, returning a list; `Runtime.sortSpecification` is the top-down merge sort in Clojure in
  the same file, evaluated by PrimitiveTests and the bench. `compare` is the leaf without a Clojure spec:
  nothing in Clojure here orders two strings or chars (no `int` of a char, no `subs`), so a Clojure
  `compare` cannot be written; it is checked by table. Trigger: char/int conversion landing, then the
  Clojure spec and `compare` in the differential too.
- **Deviations from Clojure's `compare`**: −1/0/1 always (the JVM returns the char or length difference
  for strings); strings order by code point, the JVM by UTF-16 unit (they differ only between an astral
  char and U+E000–U+FFFF); vectors are not ordered here ("vector cannot be cast to Comparable"; Clojure
  orders them by count, then items); the mixed-type message names the runtime's types (`fixnum cannot be
  cast to a string`). `sort` has the one-argument arity only; trigger: the first `(sort cmp coll)`, then
  the comparator called through `clj_invoke` and the spec taking `cmp`.
- **Limits.** Varargs are `arity: nil` plus a check in the body, as with `Value(function:)`. core.clj
  cannot call a primitive at load time and cannot reference one without `(declare ...)`: the hook runs
  after core.clj. A C-only host has neither `compare` nor `sort`. Meta is `:doc` only; `:private`,
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

## Symbol / keyword (Sources/CljCore/symbol.c, keyword.c)

- **A symbol's meta survives `with-meta` copies only**: `symbol`/`name`/`namespace` and the analyzer
  work on the fields, and `def` interns a bare copy of a meta-carrying name so the mapping key never
  holds the var's meta twice.
- **Interning a keyword is permanent and shows in `clj_debug_live_objects`** (keyword, symbol,
  string, intern-table nodes); tests intern the keywords they use before taking a baseline.
- **Keyword intern table is one global map under one mutex**, and interning allocates a temporary
  symbol for the lookup even on a hit. Trigger: keyword literals resolved at runtime in a hot path
  (the reader/analyzer resolves them once, so unlikely). Fix: sharded tables or a lock-free read path.
- **A string is limited to 4 GiB** (`uint32_t len`); `clj_string_new` aborts beyond that.

## Benchmarks (bench/)

- Numbers drift between sessions (thermal, background load). Compare only within one run; use
  `CLJ_SYSTEM_ALLOC=1` on the same binary as the control.
- The "C iterator" number (3.8 ns/element) moves to 4.3 with identical machine code when the linker
  places `clj_seq_iter_next`/`clj_vector_nth` differently; `aligned(64)` on both brings it back.
  Compare that row across builds only with the alignment forced, or read it as ±0.5 ns.
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
