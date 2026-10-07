## Type descriptor (object.h, coll.c, seq.c)

- **Builtin descriptors stay `const`; their protocol tables live in a side table** (proto.c) keyed by
  descriptor pointer, while a `deftype`/`reify` descriptor owns its table in `user_protos`. Both are
  immutable snapshots: `extend` builds the next one under the protocol mutex, publishes it with a
  seq_cst store, bumps the definition epoch (`clj_epoch()`) and frees the old one after every reader's
  dispatch window (a per-thread flag, Dekker-ordered with the publish) has closed. Per-thread reader
  slots are never freed, so the writer's scan needs no lock; an exiting thread's slot goes to the next thread
  (`reader_retire`, a pthread key's destructor), so the scan is as long as the most threads alive at once, not
  every thread the blocking pools ever retired (`ChanStressTests.idlePoolThreadsRetire`). A retired snapshot's
  impls are released, so a redefinition leaks nothing. The core-interface
  slots of every type are write-once: a builtin's are static, a `deftype`/`reify` fills its own at
  creation from the interfaces its form names (next item); `(extend-type String ISeq ...)` and
  `(extend-type MyType ISeq ...)` are refused alike.
- [~] **`deftype`/`reify` implement core interfaces through slot trampolines.** `deftype*` takes every
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
- [~] **Protocol dispatch is cached per call site** (eval.c, `proto_ic`; bench/RESULTS.md, "Call-site
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
- [ ] **No `.-field` access, no protocol inheritance.** A deftype's fields are positional
  slots read through `field*`, visible as locals inside its own method bodies only; from outside
  there is no accessor. A deftype carries meta only by implementing `IObj` itself (a field for it);
  a record has the slot (see "Records").
  A protocol cannot extend another. `extend-type` on a core interface as the *type*
  (`(extend-type ISeq P ...)`) covers every type with those bits, on the concrete type missing; a
  user protocol cannot be a type designator. Trigger: the first `(.-x o)`.
- [~] **`reify` expands to data and var references only**: `(new* (reify-type* 'reify__N '[m ...] P {:m 0}
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
- [~] **`clj_seq_iter` walks builtin seq types inline** (cons, (), vector, string, the seq.h types) and
  everything else through its slots: a seqable that is no seq is `seq`'d, a seq's `first`/`next`
  hand out owned values the iterator holds (`held`, `item`) until the next step or
  `clj_seq_iter_close`, which a walk that stops early must call (`nth`, `clj_seq_equals`, the
  printer's frames, `is_do_form`, `macro_var` do). Items of the inline path stay borrowed from the
  walked value; once a slot yielded one (`it.slots`) `clj_seq_items` retains every item and hands
  back a vector as `keep`, and the analyzer holds such vectors (`analyzer.keeps`) until the
  analysis ends. Trigger for a faster path: a user seq in a profile (chunking, or a slot walk that
  batches).
- [~] **`reduce` is a `reduce` slot on the descriptor** (`IReduceInit`; reduce.h): `(*reduce)(self, f, init)`
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
- [ ] **No chunked seqs.** `seq` on a vector is a view that allocates one 32-byte object per `next`
  (bench/RESULTS.md: 37 ns per element interpreted, 3.5 ns through the iterator); `reduce` and
  `transduce` over a vector or range take the reduce slot and allocate nothing per element, so
  chunking only matters for the lazy `map`/`filter`/`first`/`next` walks. Trigger: seq
  walks of big vectors in a profile; Clojure's chunked seqs batch 32 elements per allocation and
  need `chunk-first`/`chunk-rest` in `map`/`filter`. The API a library calls exists (core.clj: `chunk-buffer`,
  `chunk`, `chunk-cons`, `->ArrayChunk` …, deftypes), so such a library loads and takes its chunked branch
  only over what `chunk-cons` built; the producing side is what this entry waits for.
- [~] **`clj_equals`/`clj_hash` cannot throw**, so a lazy seq whose thunk throws compares unequal /
  hashes what it yielded and the exception is dropped (`drop_thrown` in coll.c); a deftype `equiv`
  that throws compares unequal and a `hasheq` that throws or yields a non-integer hashes 0, the
  same way. Clojure throws out of `=`. A cancellation is the exception no answer may survive — a deadline
  that expired in a thunk `=` forced read as unequal (NOTES "Corpus", the `random-sample` failure) — so every
  drop goes through `clj_equals_drop_pending` (error.c), which records a cancellation's kind on the execution,
  and `=`, `not=` and `hash` clear the record before comparing and rethrow it after (`clj_equals_rethrow`;
  `clj_eq`/`clj_neq` may return `CLJ_THROWN`, which both backends' intrinsic paths check). A refusal takes the
  same road and more of it: a boxed Swift struct without `Hashable`/`Equatable` records one (`clj_refuse`), and the
  HAMT's assoc throws it besides `=`, `not=` and `hash` (NOTES "Host bridge", "Swift stubs"). Left: any other
  exception is still dropped, and so is a cancellation met inside a lookup, `contains?`, `distinct` or a
  sorted collection's compare, where equals answers a question nobody rethrows for — the cancellation is
  sticky, so the next call or loop turn throws it, after the wrong answer. Trigger: user code relying on that
  exception. Fix: fallible equals/hash slots.
- [ ] **`apply` spreads its whole last argument** (`clj_seq_items`), so `(apply f infinite-seq)` never
  returns even for a variadic f; Clojure hands the rest seq to a variadic fn lazily. core.clj avoids
  `(apply concat ...)` for that reason (`mapcat`). Trigger: a library doing `(apply concat (map ...))`
  on a lazy source. Fix: `clj_apply` passing a seq as the rest argument of a variadic closure.
- **Forcing a shared lazy seq parks** on the lot while another execution runs the thunk (a one-shot wait,
  "Coroutine mutex"; a bare thread blocks); a thunk reaching its own object throws "Recursive realization"
  (the forcing stack is per execution). A thunk may itself park (`(lazy-seq [(<! c)])`).
- [~] **Metadata is any IPersistentMap**, a sorted map included, and so is `ex-info`'s data map. The three
  places that read a flag out of metadata with `clj_map_get` — a form's reader position, `def`'s
  `:dynamic`, a var's `:private` — first test the hash-map representation and treat any other as absent;
  nothing but the reader and `def` ever writes those keys. Trigger for reading them generically: a library
  that puts a position or `:private` in a sorted map.
- **Meta lives in per-type fields, not the header** (design, "Дескриптор типа"): symbol, vector, map
  and fn have a `meta` field; a cons, `()` or a seq view grows a trailing word under `CLJ_FLAG_META` (the flag
  survives the dead-link in rc.c so the free path still visits it), so only with-meta'd and
  reader-produced lists pay 8 bytes; a var has an atomic `meta`. `with-meta` on a unique root sets
  the field in place, on a shared one copies the root (a fn copy shares code and env; a copy of a
  native-with-context fn keeps the original alive through `code` and borrows its context, since a
  context has one release callback). `conj`/`assoc`/`dissoc`/`pop` keep a vector's or map's meta;
  `conj` on a cons drops it (Clojure's `PersistentList` keeps it, `Cons` does not — the `clj_list`
  wrapper below fixes that too). Equality, hash and the printer ignore meta (no `*print-meta*`).
- **The seq views keep meta in the trailing word too**: vector-seq, string-seq, array-seq, range and
  lazy-seq carry `CLJ_CORE_META | CLJ_CORE_OBJ` and reach `clj_view_meta`/`clj_view_with_meta` (seq.c),
  which are the cons mechanism over any plain view: the with-meta'd copy is `clj_alloc` of the type's own
  size plus the word, the bytes past the header memcpy'd and the children retained through `each_child`,
  which cannot see the new slot because the flag goes on after it. A view without metadata keeps its size
  (32 bytes for the three seqs, 40 for a range and a lazy-seq), and one size class is 8 bytes up to 64, so
  metadata costs exactly the word. `next` drops it as Clojure's `LongRange` and `ChunkedSeq` do, and hands
  it on as `StringSeq` and `ArraySeq` do; `conj` drops it, being a cons. `with-meta` on a lazy-seq realizes
  the head first, as `LazySeq.withMeta` does: a thunk runs once per object, so a copy may share the
  realized value but never the thunk. `sort` and `sort-by` carry the source's metadata over
  (`(with-meta (seq a) (meta coll))`, compare.c), an empty collection answering the bare `()`.
- **`nth` special-cases strings by type** rather than a slot: a string has `lookup`/`count` slots
  but no ILookup/Indexed bits, as `RT.get`/`RT.nth` special-case `String`.
- **`hash-ordered-coll`, `hash-unordered-coll`, `mix-collection-hash` and `hash-combine` agree with this core's
  `hash`** (builtins.c): the collections hash by Murmur3's `hashOrdered`/`hashUnordered` over the element hashes
  and `clj_mix_coll_hash`, which is the JVM's mixing bit for bit, so a user collection built on them hashes as a
  vector, a seq or a set of the same items does. Whether the element hashes are the JVM's is design §10's open
  question; these leave it alone.
