## Vector (Sources/CljCore/vector.c)

- [ ] **`clj_vector_from_array` is a conj loop**: the leaf grows through `clj_realloc` one slot at a time,
  ~13 size-class moves per 32 elements. Trigger: reader or `vec` on large inputs showing in a profile.
  Fix: build full leaves directly and push them.
- [ ] **`conj` is 10× a mutable `Array` append** (bench/RESULTS.md): wrapper and tail ownership checks,
  a retain, and a `clj_realloc` that moves at every size-class boundary. Trigger: a conj loop in a
  profile. Fix: transients (one owner, no checks), or a tail allocated at slack capacity.
- **No identity short-circuit in `assoc`**: storing the element already there still copies the path
  when shared and resets the hash cache. Clojure does the same.
- **Index and count are `uint32_t`**; a negative index from a higher layer must be rejected there.

- [~] **Tuples are the vector's second layout** (design §4 «Tuples»). `clj_vector_type` stays the one type; a
  vector built by `clj_vector_from_array` from one to six items — a literal in both backends (`eval_vector`, the
  compiled `emit_literal`), the reader, `vector`, every map entry (`map_seq`, `map_reduce`, the sorted and record
  entries), the Swift `Value` array — is `{header, count, shift = 0, hash, meta, items[n]}`: one cell of 40 + 8n
  bytes, where the trie's pair is a 56-byte wrapper plus a 48-byte tail node. `shift` tells the layouts apart (a
  trie's is never 0) and lives in the body, since rc.c overwrites a dead object's header before `each_child`
  reads it: no header flag, no rc.c change. Every `clj_vector_*` entry dispatches on it, and `run_at` hands hash,
  `=`, `reduce`, `reduce-kv` and `clj_vector_each` a run of elements (a leaf or the whole tuple), so a tuple and a
  trie with the same elements are `=` and hash alike both ways; nothing outside vector.c sees the layout.
  At rc 1 `assoc` writes the slot, `conj` below seven `clj_realloc`s by a word (the address moves with the size
  class) and `pop` clears the slot and keeps the cell; a shared tuple copies its words. A `conj` onto a full tuple
  promotes it to a trie of seven, one way, as a shape map falls to the trie. A `conj` onto `[]` builds the trie
  as before: `into`, `vec`, `mapv` and transients start there and would only pay a promotion at the seventh
  element. Measured (bench/RESULTS.md, "Tuples", x86_64 local): a pair is 56 bytes against 104, its create + release 33 ns
  against 104–108 through the C API, `[i 1]` created and destructured 66 ns against 149 compiled, a map entry
  destructured in `reduce` 69–74 against 150–156 compiled (1.6× interpreted). Inspection: `clj_vector_is_tuple`,
  `clj_tuples_enable` (the bench's control), `clj_debug_vector_stats` (the `CorpusTests` log prints it).
  Not done: unboxed tuple slots (`[x y]` as two doubles) — the read-boxes cost of elements kinds below, the same
  trigger; and not allocating the pair at all (`(map (fn [[k v]] …) m)` passing `k v` as two arguments after
  fusion, `(let [[a b] (f x)])` as two returns) — compiler work on fusion and the worker/wrapper convention;
  trigger: a destructured pair in a compiled hot loop whose remaining cost is the allocation.
- [ ] **Elements kinds (design §4): assessed and deferred.** The design's item: leaves typed by observation, all
  int64 → `int64[]`, doubles → `double[]`, mixed → boxed, one-way transitions on write, `vector-of` inferred.
  *Gain here*: none for integers — a fixnum is already the 63-bit payload of the slot's word, so an `int64` leaf
  saves no byte and no allocation (a long past 62 bits is the exception); for doubles, the 32-byte box behind each
  8-byte slot (`clj_double_new`), 40 → 8 bytes per element. *Cost*: `clj_vector_nth` lends its element (+0, 156
  callers in 19 files — printer, compare, equality, the seq view and iterator, `nth` and so destructuring); a
  double leaf has no box to lend, so either every caller takes an owned result or each read allocates a box, and a
  read that allocates is worse than the base, which design §4's "never worse than the base" rules out. The read is
  free only for a consumer that takes the raw double, and none exists: the facts hold no element fact for a vector
  (NOTES "Facts": `[:vector …]` children "describe elements no fact holds yet"), and `reduce`/`nth` hand boxed
  values on. On top: a kind per leaf, a check on every `conj`/`assoc`, and `=`/`hash`/printing boxing per element
  to agree with boxed vectors. *Measure* with memory per element and `(reduce + v)` over 100k doubles, typed against
  boxed, the consumer unboxed. Trigger: element facts in the lattice (a `[:vector :double]` the compiled
  `reduce`/`nth` reads raw), or the first `(vector-of :f32)` handed to Metal (NOTES "Arrays"); until then a
  double-heavy loop takes a `double-array`.
