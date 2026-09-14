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
  seq_cst store, bumps `clj_proto_epoch()` and frees the old one after every reader's dispatch window
  (a per-thread flag, Dekker-ordered with the publish) has closed. Per-thread reader slots are never
  freed; a retired snapshot's impls are released, so a redefinition leaks nothing. The
  core-interface slots of every type stay write-once: `(extend-type String ISeq ...)` is refused.
- **Protocol dispatch has no inline cache.** Every call walks the type's snapshot (a linear scan of
  its protocols) and on a miss the core-interface entries, then `Object`. The epoch is exposed for
  the cache the design describes; nothing consumes it yet. Trigger: protocol calls in a profile.
- **No `defrecord`, no `.-field` access, no protocol inheritance, no deftype metadata.** A deftype's
  fields are positional slots read through `field*`, visible as locals inside its own method bodies
  only; from outside there is no accessor. A protocol cannot extend another. `extend-type` on a
  core interface as the *type* (`(extend-type ISeq P ...)`) covers every type with those bits, on
  the concrete type missing; a user protocol cannot be a type designator. Trigger: the first
  record-shaped state (then a shape descriptor with map slots) or the first `(.-x o)`.
- **`reify` creates and extends its type at macro expansion**, so `macroexpand` of a reify form
  makes a throwaway type (freed with the expansion) and bumps the epoch. Its closures live in the
  instance's fields; the type's slots hold trampolines into them. `deftype` methods may shadow a
  field with a param, as in Clojure; fields a body names are bound at the top of that body (one
  `field*` call each), whether or not the reference is under a `quote`.
- **Builtin type names are vars in clojure.core** (`String`, `Long`/`Integer`, `Double`, `Boolean`,
  `Character`, `Keyword`, `Symbol`, `PersistentVector`, `PersistentHashMap`, `PersistentList`/`Cons`,
  `EmptyList`, `LazySeq`, `Range`, `Fn`, `Var`, `Namespace`, `ExceptionInfo`, `HostError`,
  `Protocol`, `Type`, `Object`; the core interfaces `Seqable ISeq Sequential IPersistentCollection
  Counted ILookup Associative Indexed IFn IPersistentList IPersistentVector IPersistentMap
  IExceptionInfo`) holding descriptors; `(type x)` reaches every other one and `nil` is the literal.
  A user `(def String ...)` shadows the name. `Number` does not exist: fixnum and double are two
  descriptors, extend both.
- **`clj_seq_iter` walks builtin seq types only** (cons, (), vector, string, the seq.h types) and
  aborts on anything else. Trigger: a `deftype` implementing ISeq — which today cannot exist, as
  core interfaces are not extendable; when they are, the iterator falls back to the first/next slots
  with an owned intermediate, and callers that keep borrowed items past the walk (analyzer
  `seq_items`, `clj_seq_items`) retain them.
- **No chunked seqs.** `seq` on a vector is a view that allocates one 32-byte object per `next`
  (bench/RESULTS.md: 50 ns per element interpreted, 3.5 ns through the iterator). Trigger: seq
  walks of big vectors in a profile; Clojure's chunked seqs batch 32 elements per allocation and
  need `chunk-first`/`chunk-rest` in `map`/`filter`/`reduce`.
- **`clj_equals`/`clj_hash` cannot throw**, so a lazy seq whose thunk throws compares unequal /
  hashes what it yielded and the exception is dropped (`drop_thrown` in coll.c); Clojure throws
  out of `=`. Trigger: user code relying on that exception. Fix: fallible equals/hash slots.
- **`apply` spreads its whole last argument** (`clj_seq_items`), so `(apply f infinite-seq)` never
  returns even for a variadic f; Clojure hands the rest seq to a variadic fn lazily. core.clj avoids
  `(apply concat ...)` for that reason (`mapcat`). Trigger: a library doing `(apply concat (map ...))`
  on a lazy source. Fix: `clj_apply` passing a seq as the rest argument of a variadic closure.
- **Forcing a shared lazy seq spins** (`sched_yield`) while another thread runs the thunk; a thunk
  reaching its own object throws "Recursive realization" (thread-local forcing stack). Trigger: a
  thunk that blocks for long with other threads waiting; then park on a condition variable.
- **A cons is `list?`** (CLJ_CORE_LIST) so reader lists, which are cons chains, satisfy the
  predicate; Clojure's `Cons` is not `IPersistentList`. Goes away with the `clj_list` wrapper below.
- **`nth` special-cases strings by type** rather than a slot: a string has `lookup`/`count` slots
  but no ILookup/Indexed bits, as `RT.get`/`RT.nth` special-case `String`.

## RC (Sources/CljCore/rc.c, object.h)

- **Live-object counter is one process-wide atomic** (debug only). Trigger: debug builds visibly slow
  under many threads. Fix: per-thread counters summed on read.
- **Copy path retains every child and then replaces one slot**: one spare retain/release pair per
  level. Trigger: profiling the "all versions kept" benchmark scenario.

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

- **Not supported, reported as errors**: sets `#{}`, metadata `^`, `#(`, regex, namespaced maps `#:`,
  reader conditionals, tagged literals, `::kw` (needs the current ns), bigint/BigDecimal/ratio/hex/
  radix/octal numbers. Each is a `switch` arm in `read_dispatch`/`parse_number` to replace when the
  feature lands.
- **Syntax-quote resolves through `clj_syntax_quote_resolve` in the thread's current namespace**, not
  the `clj_env.ns` the host later analyzes in; `resolve_ctx` is unused. The two agree while the host
  never calls `clj_ns_set_current`. Trigger: an `ns` form or a per-runtime namespace. Also no ns
  aliases, so `alias/x` is never rewritten, and no Java class heuristic (`foo.Bar` gets qualified).
- **`~`/`~@` outside syntax-quote are reader errors**, where Clojure reads `(clojure.core/unquote x)`
  and fails later. A literal `(clojure.core/unquote x)` inside a syntax-quote is still an unquote.
- **No metadata on forms.** `form_line`/`form_col` expose the start of the last top-level form only;
  nested forms carry no position, so every analysis error reports the top-level form's `:line`/`:column`.
  Trigger: error messages inside a long `defn`. Needs the symbol/list meta slot.
- **Input is not validated as UTF-8** except inside a character literal; malformed bytes pass through
  into strings and symbols, and a column counts every non-continuation byte. Trigger: a non-Swift host
  feeding raw bytes.
- **`strtod`/`snprintf` in reader and printer follow the C locale**, which the runtime never changes;
  a host calling `setlocale` with a comma decimal point would break doubles.

## Analyzer and evaluator (Sources/CljCore/analyzer.c, eval.c, fn.c)

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
- **No metadata, so no docstrings.** `defn`/`defmacro` accept a docstring and drop it; `^:private`,
  `^:dynamic`, attr-maps and `(doc x)` need a meta slot on symbols, vars and collections.
- **Exceptions unwind by return code, not by `longjmp`**: `try` sees `CLJ_THROWN` from its body and
  takes the pending value; every C frame in between releases its own temporaries on the way out. No
  stack trace is captured, so an uncaught exception reports only the top-level form's position.
  Trigger: debugging a deep failure; then the shadow stack of frames from the design (also the crash
  trace) records the Clojure frames at throw time.
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
- **`def` is eager and vars are plain roots.** No lazy thunk state, no `^:dynamic`/binding, no
  `*ns*` var (the current namespace is a thread-local pointer, `user` by default). Trigger: the first
  ns whose load-time cost shows, or the first `binding`.
- **Concurrent `def` against `deref` is unsafe.** `clj_var_root` returns a borrowed pointer and a
  racing `clj_var_bind_root` releases the old root, so a reader may retain a freed value.
  Redefinition is a dev-time operation until the epoch/inline-cache design (var inline cache, §6)
  lands; until then evaluate on one thread at a time. Same for `clj_ns_current` vs `clj_init`
  ordering: call `clj_init` before any evaluation.
- **Var lookup is a root load on every evaluation** of a var node, no inline cache or epoch check.
  Trigger: profiling a hot loop over core fns.
- **C stack per Clojure call is large.** Each call is ~5 C frames with slot and argument buffers on the
  stack: ~0.6 KB in a debug build, ~2.3 KB under ASan, ~3.7 KB under UBSan. On Swift Testing's 512 KB
  threads that is ~600 / ~170 / ~100 nested non-tail calls before the guard throws "Stack overflow"
  (the guard reads the thread's real bounds on Apple platforms; elsewhere it assumes 512 KB). Fix: a
  heap-allocated shadow stack of frames (also the crash-report trace from the design) and fewer C
  frames per call. Tests keep non-tail recursion depth ≤ 50.
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
  resolve deref identical? type instance? satisfies? extends?` (`deref` takes vars only: no atoms),
  the bit predicates `seq? seqable? sequential? coll? counted? ifn? associative? indexed? list?
  vector? map? char? integer?`, `symbol keyword name namespace gensym`, `str pr-str pr prn print
  println identity apply`, `macroexpand-1 macroexpand ex-info ex-message ex-data ex-cause`. No `keys`, `vals`, `max`,
  `mod`, `sort`, `reduced`, ... Most of the rest belongs in core.clj.
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
- **Contents**: `concat lazy-seq when when-not if-not cond destructure let loop fn defn and or -> ->>
  comment dotimes if-let when-let assert declare`, the seq library `complement nthrest some every?
  not-any? not-every? reduce map filter remove keep take drop take-while drop-while iterate repeat
  range interleave interpose mapcat dorun doall vec partition zipmap`, plus the helpers
  `check-bindings`, `maybe-destructured` (public vars; Clojure keeps the second private). Not yet:
  `defn-`, `doto`, `condp`, `case`, `while`, `letfn`, `for`, `doseq`, `fn` literals, `some->`, `as->`,
  `cond->`, `reduced` (so `reduce` cannot stop early), `sort`, `group-by`, `frequencies`, transducers.
- **Protocol macros keep their helpers public** (`group-impls`, `form-uses?`, `method-fn`,
  `method-map`, `body-as-is`), as `destructure` does. `defprotocol` drops docstrings and takes no
  options (`:extend-via-metadata`, `:on-interface`). `extend` rejects a key that names no method
  where Clojure ignores it. Method fns are unnamed, so an arity error inside an impl says `fn`; the
  dispatching fn checks the declared arities first and names the method.
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
- **`ClojureError` has no Clojure stack trace** (`clojureTrace` in the design): only the top-level
  form's `:line`/`:column` in `data`. Same trigger as the shadow stack above.

## Printer (Sources/CljCore/printer.c)

- **Map entries are collected into a temporary array per map** because `clj_map_each` is callback-only.
  Trigger: printing huge maps in a profile. Fix: a resumable map iterator.
- **Control characters print as `\uXXXX`** inside strings and as char literals; Clojure prints them raw.
  Readable by both, but `(pr-str "\u0001")` differs from the JVM byte for byte.

## Symbol / keyword (Sources/CljCore/symbol.c, keyword.c)

- **Symbols carry no metadata slot.** Trigger: the reader attaching `:line`/`:column`, or `with-meta`
  on a symbol. Add a `meta` value slot (nil by default) and visit it in `each_child`.
- **Interning a keyword is permanent and shows in `clj_debug_live_objects`** (keyword, symbol,
  string, intern-table nodes); tests intern the keywords they use before taking a baseline.
- **Keyword intern table is one global map under one mutex**, and interning allocates a temporary
  symbol for the lookup even on a hit. Trigger: keyword literals resolved at runtime in a hot path
  (the reader/analyzer resolves them once, so unlikely). Fix: sharded tables or a lock-free read path.
- **A string is limited to 4 GiB** (`uint32_t len`); `clj_string_new` aborts beyond that.

## Benchmarks (bench/)

- Numbers drift between sessions (thermal, background load). Compare only within one run; use
  `CLJ_SYSTEM_ALLOC=1` on the same binary as the control.
- Not yet measured: multi-threaded reads of a shared map, assoc from a shared base across threads,
  cross-thread free, cost of `clj_share` on a large graph, forcing one shared lazy seq from many
  threads (the CAS claim path).

## Open decisions

- **File extension and reader-conditional key.** Source stays `.clj` (`.cljc` for portable user
  code) until the project has a name; the key in `#?(:key …)` and the extension are the same word
  and permanent, and they should name the runtime (portable C core), not Apple or Swift. Decide when
  reader conditionals land in the reader.
