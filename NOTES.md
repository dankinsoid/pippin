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
  `Character`, `Keyword`, `Symbol`, `PersistentVector`, `PersistentHashMap`, `PersistentList`/`Cons`,
  `EmptyList`, `LazySeq`, `Range`, `Fn`, `Var`, `Namespace`, `ExceptionInfo`, `HostError`,
  `Protocol`, `Type`, `Reduced`, `Volatile`, `Object`; the core interfaces `Seqable ISeq Sequential
  IPersistentCollection Counted ILookup Associative Indexed IFn IHashEq IEquiv IMeta IObj
  IReduceInit IPersistentList IPersistentVector IPersistentMap IExceptionInfo`) holding descriptors; `(type x)` reaches every
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
  vector passes the index), `()`, and cons / lazy-seq / string / string-seq through
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
- **Share of retain/release on shared objects is unmeasured.** The flag is monotone, so app state in
  an atom is atomic for everyone forever (design §4, "Представление значений"); whether that is most of the RC traffic or
  a background decides whether BRC is worth its header word. Count on `clj_retain_slow`/`clj_release_slow`
  versus the inline path under `CLJ_DEBUG`, on a workload with state in an atom, after the +0/+1
  convention lands (a +1 `get` inflates the share).

## Map (Sources/CljCore/map.c)

- **`clj_debug_hash_override` is checked on every `clj_hash`** even in release (one global load +
  branch). Trigger: it shows in a profile.

- **`clj_map_of` stays in map.h**: core.c reads the hash slot through it and MapTests checks root
  identity. Trigger: a second map representation (shapes) behind the same functions; then replace both
  uses with accessors like `clj_debug_vector_root`.

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

- **Not supported, reported as errors**: sets `#{}`, `#(`, regex, namespaced maps `#:`, reader
  conditionals, tagged literals, `::kw` (needs the current ns), bigint/BigDecimal/ratio/hex/
  radix/octal numbers. Each is a `switch` arm in `read_dispatch`/`parse_number` to replace when the
  feature lands.
- **Every non-empty list read costs a `{:line :column}` map** (map wrapper plus one node) on its head
  cons, as Clojure attaches positions to lists only; `'x`, `@x`, `#'x` and the syntax-quote output
  are built by the reader without one. Syntax-quote drops the meta of the forms it rebuilds where
  LispReader keeps everything but the position keys. Trigger: `^:once`-style meta inside a
  syntax-quoted template. Fix: `sq_pop` wrapping the rebuilt collection in `with-meta` when the source
  had non-position keys.
- **Syntax-quote resolves through `clj_syntax_quote_resolve` in the thread's current namespace**, not
  the `clj_env.ns` the host later analyzes in; `resolve_ctx` is unused. The two agree while the host
  never calls `clj_ns_set_current`. Trigger: an `ns` form or a per-runtime namespace. Also no ns
  aliases, so `alias/x` is never rewritten, and no Java class heuristic (`foo.Bar` gets qualified).
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
  core var paid, borrowing user fn roots the same pair on every user call (bench/RESULTS.md).
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
  clojure.core and refuses a qualified reference from another namespace; `(var ns/x)`, `#'ns/x`,
  `resolve` of the qualified symbol and a `clj_ns_refer` still reach it (Clojure refuses the refer).
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
- **No `ns` form.** Everything the host evaluates lands in `user`; core.clj is loaded with the current
  namespace set to `clojure.core` by `clj_init`. `clj_ns_set_current` is the only way to move.
- **`defmacro` on a failing body still interns the var** (analysis creates it before the fn is
  analyzed), as `def` does: the name resolves afterwards to an unbound var. Same as Clojure.
- **No hoisting.** A file is analyzed one top-level form at a time, so a forward reference is
  "Unable to resolve symbol" (design: pre-pass registering `def` names at file load).
- **`def` is eager and vars are plain roots.** No lazy thunk state, no `binding` (`^:dynamic` only
  sets `clj_var.dynamic`, which nothing consumes yet), no `*ns*` var (the current namespace is a
  thread-local pointer, `user` by default). Trigger: the first ns whose load-time cost shows, or the
  first `binding`.
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
  in one call. `apply` and every call from a native or the host are unchanged (`clj_invoke`).
  Debug builds count per site the calls that took a fast path (`clj_debug_exec_ic_hits`) against the
  generic ones (`..._misses`); release builds count nothing. The site array is indexed by
  `clj_node.site`, the node's ordinal among the tree's INVOKE nodes, assigned with the ids (it fills
  the padding after `col`, so a node grew by nothing; `from_data` renumbers it too).
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
  `(clojure.core/+ a b)` anywhere is rewritten. Not yet: folding of pure intrinsics on constant
  arguments (the `pure` flag is set, nothing reads it), any rewrite that needs liveness.
- **Intrinsics table** (intrinsics.h/.c): `{qualified name, arity, kind INTRINSIC_1/2/3, C function, pure}`
  for `+ - * /` (2 args), `inc dec`, `< <= > >= = not= identical?` (2 args), `not nil? zero? pos? neg?
  even? odd?`, the type predicates, `empty? first rest next seq count`, `cons get(2,3) nth(2,3) conj(2)
  assoc(3) contains?`; a side table resolved at boot holds each entry's var and native fn. The rule,
  kept by structure: the builtin bound to the same var calls the same function — single-arity builtins
  forward, variadic ones fold (`b_add` is a loop over `clj_add`), `conj`/`assoc` differ only by the
  retain before their consuming core. Consequences: `(+ a b c)` boxes a double at every step where the
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
  call — no frame, no arity table, no var deref.
- **The fusion pass** (optimizer.c, fusion.c, `CLJ_NODE_FUSED`; bench/RESULTS.md, "Fusion"): `(reduce
  f [init] P)`, `(into to P)`, `(vec P)` and `(count P)`, where `P` is a nest of `map keep filter
  remove take drop take-while drop-while mapcat map-indexed keep-indexed interpose dedupe` calls — each
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
  the transducers `nil` as `result`, so the seq rules of `reduce` hold exactly: a 2-arity seeds with
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
  predicates, `get assoc dissoc contains? count conj nth first rest next cons list list* vector
  hash-map seq lazy-seq* realized? range* list* into second last butlast reverse empty? hash
  resolve deref identical? type instance? satisfies? extends? meta with-meta alter-meta!
  reset-meta! reduce reduce-kv reduced reduced? unreduced ensure-reduced volatile! volatile?
  vreset! fused-reduce* fused-into* fused-count*` (`deref` takes vars, reduced boxes and volatiles: no atoms; `alter-meta!`/`reset-meta!`
  take vars only), the bit predicates `seq? seqable? sequential? coll? counted? ifn? associative?
  indexed? list? vector? map? char? integer?`, `symbol keyword name namespace gensym`, `str pr-str
  pr prn print println identity apply`, `macroexpand-1 macroexpand ex-info ex-message ex-data
  ex-cause`. No `keys`, `vals`, `max`, `mod`, `sort`, ... Most of the rest belongs in core.clj.
- **`into` is C in both arities**: `(into to from)` conj's through `clj_seq_iter`, `(into to xform
  from)` is `transduce` with `conj` written out in C (the `conj` root kept at install), because
  `destructure` calls `into` above the `fn` macro, where a core.clj `into` could not be defined.
  Neither uses transients (there are none): the reducing `conj` receives the accumulator at +0
  while the reducer holds it at +1, so every step copies the vector's tail (bench/RESULTS.md,
  "IReduce"). Trigger: the ownership-transferring reduce of the design's auto-transient item.
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

- **Embedded as a byte array** (`core_clj.inc`, regenerated by `make boot`); `CoreCljTests` fails when
  the two drift. A boot error is `clj_fatal` with the form's position: core.clj is part of the
  binary, so it is a build bug, not a user error.
- **Loaded once per process into `clojure.core`**; its vars, closures and fn nodes are live for the
  process and sit under every test baseline taken after `clj_init`.
- **Contents**: `concat lazy-seq when when-not if-not cond destructure let loop fn defn defn-
  vary-meta and or -> ->> comment profile dotimes if-let when-let assert declare doc vswap!`, the seq
  library `complement comp partial constantly completing transduce cat nthrest some every?
  not-any? not-every? map filter remove keep take drop take-while drop-while iterate repeat range
  interleave interpose mapcat dorun doall vec partition partition-all map-indexed keep-indexed
  sequence dedupe zipmap eduction` (`reduce` and `into` are C), plus the private helpers
  `check-bindings`, `maybe-destructured`, `sigs`, `print-doc`, `preserving-reduced`
  (`destructure` is public, as in Clojure). Not yet: `doto`, `condp`, `case`, `while`, `letfn`,
  `for`, `doseq`, `fn` literals, `some->`, `as->`, `cond->`, `sort`, `group-by`, `frequencies`,
  `distinct` (needs sets; trigger: the reader's `#{}` or `hash-set` landing, then `distinct` is the
  `dedupe` shape over a set in a volatile).
- **Transducers**: `map filter remove keep take drop take-while drop-while mapcat interpose
  partition-all dedupe map-indexed keep-indexed` carry Clojure's transducer arities, `cat`,
  `completing`, `transduce`, `sequence`, `eduction` and `into` drive them. `sequence` is a lazy
  seq that pulls one input per realization into the transformed rf, whose bottom parks outputs in
  a volatile vector, so an infinite source stays lazy; each realization costs a vector copy per
  output (`vswap! ... conj` on a cell that also holds the vector: rc 2). `eduction` is a
  `deftype` implementing `Seqable` (through `sequence`) and `IReduceInit` (through `transduce`),
  not a C type: the slot trampoline already existed, so the type is four lines, and it prints as
  `#object[clojure.core.Eduction]` where Clojure prints the items. `vswap!` is a macro over
  `vreset!`/`deref`, as Clojure's. A transducer's stateful step (`partition-all`'s buffer) copies
  its vector per input for the same rc-2 reason. No `halt-when`, `random-sample`, `distinct`
  (sets), `partition-by`; trigger: first use.
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
- **`fn` has no `:pre`/`:post` conditions**: a map as the first body form is evaluated and discarded
  like any expression. Trigger: the first `{:pre [...]}`; the `fn` macro then wraps the body in
  `assert`s as Clojure's does (`assert` is defined below it, so the wrap must use `when-not`/`throw`).

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

## Printer (Sources/CljCore/printer.c)

- **Map entries are collected into a temporary array per map** because `clj_map_each` is callback-only.
  Trigger: printing huge maps in a profile. Fix: a resumable map iterator.
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
  and permanent, and they should name the runtime (portable C core), not Apple or Swift. Decide when
  reader conditionals land in the reader.
