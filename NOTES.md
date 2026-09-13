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
  nested forms carry no position. Trigger: analyzer error messages. Needs the symbol/list meta slot.
- **Input is not validated as UTF-8** except inside a character literal; malformed bytes pass through
  into strings and symbols, and a column counts every non-continuation byte. Trigger: a non-Swift host
  feeding raw bytes.
- **`strtod`/`snprintf` in reader and printer follow the C locale**, which the runtime never changes;
  a host calling `setlocale` with a comma decimal point would break doubles.

## Printer (Sources/CljCore/printer.c)

- **Map entries are collected into a temporary array per map** because `clj_map_each` is callback-only.
  Trigger: printing huge maps in a profile. Fix: a resumable map iterator.
- **Control characters print as `\uXXXX`** inside strings and as char literals; Clojure prints them raw.
  Readable by both, but `(pr-str "\u0001")` differs from the JVM byte for byte.

## Symbol / keyword (Sources/CljCore/symbol.c, keyword.c)

- **Symbols carry no metadata slot.** Trigger: the reader attaching `:line`/`:column`, or `with-meta`
  on a symbol. Add a `meta` value slot (nil by default) and visit it in `each_child`.
- **Keyword intern table is one global map under one mutex**, and interning allocates a temporary
  symbol for the lookup even on a hit. Trigger: keyword literals resolved at runtime in a hot path
  (the reader/analyzer resolves them once, so unlikely). Fix: sharded tables or a lock-free read path.
- **A string is limited to 4 GiB** (`uint32_t len`); `clj_string_new` aborts beyond that.

## Benchmarks (bench/)

- Numbers drift between sessions (thermal, background load). Compare only within one run; use
  `CLJ_SYSTEM_ALLOC=1` on the same binary as the control.
- Not yet measured: multi-threaded reads of a shared map, assoc from a shared base across threads,
  cross-thread free, cost of `clj_share` on a large graph.
