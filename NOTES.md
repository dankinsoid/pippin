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

- **`clj_list_count` is O(n)** and a cons cell caches no hash, so hashing a list walks it every time.
  Trigger: lists as map keys or `count` on long lists in a profile. Fix: a `clj_list` wrapper with
  count and hash cache, as Clojure's PersistentList; cons stays the 32-byte cell for `cons`/lazy seqs.
- **Hash and equality recurse on nesting depth** (`clj_hash` → element hash). Reading and printing are
  iterative, so a 200k-deep literal reads and prints but crashes when hashed. Trigger: untrusted input
  used as a map key. Fix: an explicit stack in `clj_seq_hash`/`clj_seq_equals`, or a depth cap.

## Reader (Sources/CljCore/reader.c)

- **Not supported, reported as errors**: sets `#{}`, metadata `^`, syntax-quote and unquote, `#(`,
  regex, var quote, namespaced maps `#:`, reader conditionals, tagged literals, `::kw` (needs the
  current ns), bigint/BigDecimal/ratio/hex/radix/octal numbers. Each is a `switch` arm in
  `read_dispatch`/`parse_number` to replace when the feature lands.
- **No metadata on forms.** `form_line`/`form_col` expose the start of the last top-level form only;
  nested forms carry no position, so every analysis error reports the top-level form's `:line`/`:column`.
  Trigger: error messages inside a long `defn`. Needs the symbol/list meta slot.
- **Input is not validated as UTF-8** except inside a character literal; malformed bytes pass through
  into strings and symbols, and a column counts every non-continuation byte. Trigger: a non-Swift host
  feeding raw bytes.
- **`strtod`/`snprintf` in reader and printer follow the C locale**, which the runtime never changes;
  a host calling `setlocale` with a comma decimal point would break doubles.

## Analyzer and evaluator (Sources/CljCore/analyzer.c, eval.c, fn.c)

- **No macros.** Special forms are `def if do let fn loop recur quote` (plus `let*`/`fn*`/`loop*`);
  `defn`, `when`, `cond`, `->` and the rest of core.clj need `defmacro` and macroexpand in the pipeline.
  Trigger: the first attempt to load core.clj.
- **No destructuring.** `let`/`fn`/`loop` bind vectors of plain symbols only. Comes with macros
  (`destructure` is a macro-time function in Clojure).
- **No hoisting.** A file is analyzed one top-level form at a time, so a forward reference is
  "Unable to resolve symbol" (design: pre-pass registering `def` names at file load).
- **`def` is eager and vars are plain roots.** No lazy thunk state, no `^:dynamic`/binding, no
  `*ns*` var (the current namespace is a thread-local pointer, `user` by default). Trigger: the first
  ns whose load-time cost shows, or the first `binding`.
- **No `try`/`catch`/`throw` forms.** Exceptions propagate as `CLJ_THROWN` to the host; `ex-info`
  exists only in C (`clj_ex_info`). Trigger: error handling in Clojure code.
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

- **Coverage is the minimum for the evaluator tests**: arithmetic and comparison, type predicates,
  `get assoc dissoc contains? count conj nth first rest next cons list vector hash-map`, `str pr-str
  pr prn print println identity apply`. No `seq`, `map`, `reduce`, `keys`, `vals`, `max`, `mod`, ...
  Most of the rest belongs in core.clj once macros exist; the seq protocol (lazy seqs, `seq` on
  maps and strings) is a runtime feature.
- **`rest`/`next` on a vector copy the remainder into a list** (O(n) per step, so walking a vector
  by `rest` is O(n²)); `nth` on a list walks it. Trigger: seq-style loops over big vectors. Fix:
  chunked/indexed seqs.
- **Output hook is process-wide** (`clj_set_output`), not per thread or per runtime. Trigger: two
  hosts printing concurrently.
- **Error messages are Clojure-like, not Clojure-identical**: type names are the runtime's
  (`string cannot be cast to a number`, not `java.lang.String ... java.lang.Number`).

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
  cross-thread free, cost of `clj_share` on a large graph.
