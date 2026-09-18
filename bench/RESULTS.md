# Benchmark results

ns per op, median of 5 runs, each run batched to ~1M operations. `make bench` runs the same binary
with the pool allocator and then with `CLJ_SYSTEM_ALLOC=1` as the control. Numbers drift between
sessions; compare only within one invocation.

Results recorded before this harness (commits fd4cbf4, 7f54fcf) measured a 10-op scenario as one
~1 µs interval and are not comparable; they stay in git history only.

## 8533784 — Apple M3 Pro, 36 GB, Swift 6.2.4

Pool allocator (C) vs system malloc (C, control); Swift columns are the same code in both runs.

| scenario | n | C pool | C malloc | TreeDictionary | Dictionary | tree / C pool |
|---|---:|---:|---:|---:|---:|---:|
| assoc, old version dropped | 10 | 25.3 | 56.7 | 93.6 | 48.8 | 3.7× |
| assoc, all versions kept | 10 | 67.1 | 146.0 | 189.2 | 99.7 | 2.8× |
| get, hit | 10 | 2.7 | 2.8 | 11.8 | 10.3 | 4.3× |
| get, miss | 10 | 7.4 | 7.3 | 7.8 | 10.0 | 1.1× |
| dissoc to empty | 10 | 39.2 | 79.8 | 106.7 | 61.5 | 2.7× |
| assoc, old version dropped | 1000 | 38.7 | 82.1 | 101.3 | 19.8 | 2.6× |
| assoc, all versions kept | 1000 | 183.4 | 315.6 | 518.8 | 1615.4 | 2.8× |
| get, hit | 1000 | 13.1 | 13.0 | 17.0 | 10.4 | 1.3× |
| get, miss | 1000 | 17.1 | 16.9 | 19.9 | 16.4 | 1.2× |
| dissoc to empty | 1000 | 79.2 | 147.6 | 134.1 | 35.5 | 1.7× |
| assoc, old version dropped | 100000 | 78.4 | 124.3 | 155.4 | 37.2 | 2.0× |
| assoc, all versions kept | 100000 | 683.2 | 793.4 | 1277.3 | — | 1.9× |
| get, hit | 100000 | 17.8 | 17.9 | 22.4 | 12.0 | 1.3× |
| get, miss | 100000 | 16.6 | 16.8 | 19.1 | 14.5 | 1.2× |
| dissoc to empty | 100000 | 175.3 | 231.7 | 258.2 | 63.3 | 1.5× |

Swift columns agree within 1–4 % across the two runs, so the C pool / C malloc delta is the allocator.

## Vector — Apple M3 Pro, 36 GB, Swift 6.2.4

Persistent vector (32-way trie + tail) vs Swift `Array`. `Array` is mutable and appends in place;
its "all versions kept" column copies the whole buffer per version (O(n²), so n ≤ 1000 only).
swift-collections 1.1 has no persistent vector, so there is no Swift persistent reference column.
Map numbers from the same invocation matched the 8533784 table within noise.

| scenario | n | C pool | C malloc | Array | array / C pool |
|---|---:|---:|---:|---:|---:|
| conj, old version dropped | 10 | 16.4 | 42.1 | 27.1 | 1.7× |
| conj, all versions kept | 10 | 52.7 | 125.4 | 80.1 | 1.5× |
| nth, random | 10 | 1.1 | 1.1 | 0.4 | 0.4× |
| pop to empty | 10 | 24.8 | 67.0 | 27.2 | 1.1× |
| conj, old version dropped | 1000 | 10.7 | 37.9 | 1.2 | 0.1× |
| conj, all versions kept | 1000 | 61.6 | 143.2 | 145.1 | 2.4× |
| nth, random | 1000 | 1.4 | 1.4 | 0.4 | 0.3× |
| pop to empty | 1000 | 18.0 | 53.8 | 1.4 | 0.1× |
| conj, old version dropped | 100000 | 11.1 | 37.9 | 1.5 | 0.1× |
| conj, all versions kept | 100000 | 67.8 | 179.5 | — | — |
| nth, random | 100000 | 2.6 | 2.6 | 0.6 | 0.2× |
| pop to empty | 100000 | 18.5 | 64.5 | 1.9 | 0.1× |

- `conj` with the old version dropped is one `clj_realloc` of the tail per element (a size-class
  move every few elements) plus a full-leaf push every 32; `Array` appends amortized into slack.
  The 10× gap to a mutable array is the price of a persistent structure with exact-size nodes;
  a transient or a slack-capacity tail would close most of it (NOTES.md).
- `nth` is one to three dependent loads plus the tail check; 2–4× a bounds-checked array index.
- Keeping every version costs the same as a mutable `Array` copy at n = 1000 and stays flat at 100k,
  where `Array` is quadratic.
- The pool takes 2.5–3.6× off every allocating scenario, more than for the map: a vector op is
  almost nothing but allocation.

### Vector node capacity (after 732ffff), pool only, one run

Nodes carry a capacity (powers of two up to 8, then 32) so a growing tail moves 4 times per 32 conj
instead of at every size-class boundary. C vector, ns/op, 732ffff → this commit:
conj old dropped 10.7 → 8.1 (1k), 11.1 → 8.2 (100k); pop to empty 18.0 → 12.7 (1k), 18.5 → 13.2 (100k);
conj all kept and nth unchanged within noise. The remaining gap to `Array.append` (1.2 ns) is the
persistent wrapper itself: two uniqueness checks, hash-cache reset, tail indirection, and a
non-inlined call across the Swift/C boundary.

## Seqs — a3e216a, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

Lazy seqs on the descriptor slots: `(reduce + (map inc (range n)))` is a `range` view, a `map`
lazy seq realized one element per `next`, and a `loop*`-based `reduce`, all interpreted. The seq
walk is `(loop [s (seq v) acc 0] (if s (recur (next s) (+ acc (first s))) acc))` over a 1k vector;
the C iterator column is `clj_seq_iter` over the same vector, the Swift column `for x in array`.

| scenario | n | interpreted | C iterator | Swift for | interpreted / Swift |
|---|---:|---:|---:|---:|---:|
| reduce + map inc range | 1000 | 219.1 | — | 0.3 | 786× |
| reduce + map inc range | 100000 | 219.9 | — | 0.3 | 797× |
| seq walk of a vector | 1000 | 53.1 | 3.5 | 0.1 | 529× |

- ~220 ns per element for the pipeline is about ten interpreted calls: the `map` thunk (a closure
  invoke, `seq`, `first`, `f`, `cons`, `rest`, a new lazy seq), then `reduce`'s `first`/`next`/`+`
  and the loop rebind. Flat from 1k to 100k: no per-element growth, nothing recursive.
- The interpreted walk of a vector is ~50 ns per element (`seq`/`first`/`next`/`+`/`if` through
  `clj_invoke`, one vector-seq allocation and free per step); the C iterator is 3.5 ns, mostly the
  type dispatch and the trie leaf lookup per element. Chunked seqs would cut the per-element
  allocation, not the interpreter overhead (NOTES.md).

### Exec table (after 41548a8), pool only, three alternating runs each

Dispatch through `frame->exec->nodes[id]` instead of an eval pointer in the node (NOTES.md). Medians,
ns per element, before → after: reduce + map inc range 222.3 → 224.7 (1k), 222.4 → 230.5 (100k); seq walk
of a vector 55.5 → 56.0. Run-to-run spread of the same binary is ±3 %, so the cost is at most a few
percent: one dependent load per child evaluation.

## Calls — 9136398, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

Two loops over n = 100k: `(loop [i 0] (if (< i n) (recur (inc i)) i))` (two var calls, one rebind per
iteration) and the same with `(f i)` for `(def f (fn [x] (inc x)))` (one closure call per iteration
on top). The Swift column is `while i < n { i = incBox(i) }` with a non-inlined `incBox`; a plain
counting loop folds to a closed form under -O, so it has no reference column.

| scenario | n | interpreted | Swift while | interpreted / Swift |
|---|---:|---:|---:|---:|
| counting loop | 100000 | 29.8 | — | — |
| closure call in a loop | 100000 | 45.3 | 0.8 | 57× |

### Borrowed reads and params (after 9136398), pool only, alternating runs

The evaluator reads locals, captured values and constants at +0 in argument, test and non-last `do`
positions, and a closure frame borrows its fixed params from the caller's argument array (NOTES.md,
"Ownership in the evaluator"). Medians of two alternating runs of each binary, before → after:
reduce + map inc range 228.6 → 230.1 (1k), 228.6 → 227.9 (100k); seq walk of a vector 55.2 → 56.3;
counting loop 29.7 → 30.4; closure call in a loop 45.8 → 44.7. All within the ±3 % run-to-run spread:
a retain/release pair on a non-shared object is five plain instructions, so the reads it removes were
not where the time goes. The remaining per-call cost is the var deref (an atomic pair on the shared
root, kept deliberately) and `clj_invoke` dispatch.

### Shadow stack and instrumentation (after ef146b8), pool only, three alternating runs each

Every closure call pushes and pops a shadow frame and reads one instrumentation byte (profiler,
signposts); traces, the crash handler and the profiler read the frames (NOTES.md). Medians, before →
after: reduce + map inc range 224.2 → 228.8 (1k), 225.1 → 231.0 (100k); seq walk of a vector
56.1 → 56.3; counting loop 29.0 → 29.6; closure call in a loop 44.4 → 45.8. The closure-call row moves
by ~1.4 ns per call (3 %), at the edge of the ±3 % run-to-run spread; the rows without closure calls
move within it.

## Intrinsics, immortal core roots — ef3641a, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

The optimizer rewrites a call through a listed core var into an INTRINSIC node (a guard on the var's
root, then the C function on borrowed arguments: no frame, no `clj_invoke`, no var deref), and every
root bound by boot is immortal, so a call through any other core var reads it at +0 (NOTES.md,
"Analyzer and evaluator"). Medians of three alternating runs of each binary (915405b vs ef3641a),
ns per element or iteration; the machine carried a foreground load part of the session, so only runs
whose Swift reference columns sat at their quiet values (0.3 / 0.1 / 0.7 ns) were kept.

| scenario | n | before | after | change |
|---|---:|---:|---:|---:|
| reduce + map inc range | 1000 | 225.3 | 180.1 | −20 % |
| reduce + map inc range | 100000 | 223.2 | 178.1 | −20 % |
| seq walk of a vector | 1000 | 54.9 | 38.2 | −30 % |
| counting loop | 100000 | 29.1 | 15.2 | −48 % |
| closure call in a loop | 100000 | 45.2 | 31.9 | −29 % |

- **Counting loop, 29 → 15 ns.** `(< i n)` and `(inc i)` were two builtin calls at ~10 ns each (an
  atomic retain/release pair on the var's root, `clj_invoke` through the descriptor slot, the native's
  arity check, the variadic fold); as intrinsics they are ~3 ns each: the exec-table dispatch, two
  borrowed argument reads, the guard (a relaxed load and a compare) and one indirect call into the
  arithmetic. The other ~9 ns are the `loop`/`if`/`recur` nodes (one exec-table dispatch each, the
  `recur` argument buffer and the slot rebind).
- **Closure call, 45 → 32 ns.** The loop above minus its `inc` (~12 ns) plus the call of `bench-inc`:
  the var read is now +0 and the body's `(inc x)` an intrinsic, but the closure call itself still
  costs ~17 ns — arity lookup, the stack guard, a zeroed 16-slot frame, shadow push/pop, the
  instrumentation byte, the argument buffer and the slot release — which is the next target (design
  §6b item 5: the var inline cache and a call without `clj_invoke`).
- **Seq walk, 55 → 38 ns.** `first`, `next` and `+` are intrinsics (~9 ns together); what remains is
  the vector-seq view allocated and freed per `next` (~10 ns), two trie leaf lookups, the six node
  dispatches of the loop body and the two slot rebinds.
- **Pipeline, 225 → 178 ns.** The intrinsics inside `map` and `reduce` (`seq`, `first`, `rest`,
  `cons`) and the borrowed reads of `map`/`lazy-seq*` account for the 20 %; the rest is what the
  optimizer cannot touch yet: three closure calls (`map`'s thunk, its recursion, `reduce`'s loop
  body) at ~17 ns each, two calls of a fn held in a local (`(f (first s))`, `(f acc x)`) through the
  generic `clj_invoke` at ~10 ns each, and four allocations per element (the cons, the lazy seq, the
  thunk closure with its captures, the range step).

## Call-site caches, direct closure entry — 75d023c, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

A call through a user var reads the fn root at +0 (a replaced root is parked until the thread is idle),
a closure with a fixed arity and a small frame has its arguments evaluated straight into its frame and its
body entered without `clj_invoke`, and a protocol method at the head dispatches through a per-site cache
of `{receiver type, impl}` entries keyed by the definition epoch (NOTES.md, "The call path" and
"Protocol dispatch is cached per call site"). Three new scenarios: the counting loop with `(+ i (m x))`
per iteration for a one-method protocol extended to a deftype and to `Long`, the receiver an instance in
a local, a fixnum, or alternating. Medians of three alternating runs of each binary (67febaa with this
harness vs 75d023c), ns per element or iteration. The map and vector tables matched within their noise
(±4 %, `nth` 1k at its known ±0.2 ns alignment swing).

| scenario | n | before | after | change |
|---|---:|---:|---:|---:|
| reduce + map inc range | 1000 | 181.2 | 177.5 | −2 % |
| reduce + map inc range | 100000 | 184.5 | 177.9 | −4 % |
| seq walk of a vector | 1000 | 37.1 | 36.7 | −1 % |
| counting loop | 100000 | 15.2 | 15.5 | +2 % |
| closure call in a loop | 100000 | 31.1 | 22.3 | −28 % |
| protocol call, deftype receiver | 100000 | 44.9 | 35.4 | −21 % |
| protocol call, fixnum receiver | 100000 | 45.2 | 36.3 | −20 % |
| protocol call, bi-morphic | 100000 | 59.9 | 50.2 | −16 % |

Breakdown of a closure call before, by removing one stage at a time from a scratch build (three runs
each, the closure-call row; the loop without the call is 15 ns, the `(inc x)` body ~3): the var read
with its atomic retain/release on the shared root 3.3 ns, the zeroed 16-slot frame 1.7 (a
variable-size `memset` call), the stack guard 1.5 (its own thread-local), shadow push/pop 1.1, the
instrumentation byte 0.8, the argument buffer 0.8, the slot release 0.8, and ~2.5 for the arity search,
the frame and the C calls between `eval_invoke` and the body — ~12.5 ns of call mechanics.

After: the same subtraction leaves the guard, the shadow frame and the instrumentation byte each within
the ±0.3 ns run-to-run spread (one thread-local load serves the guard and the frame, the byte is a
global), and the call mechanics are ~4 ns: the var's acquire load and the fn check, three dependent
loads to the arity (`fn`, its node, `fixed[n]`), the argument evaluated into the frame, the frame's
exec load, the indirect call into the body and the owned-mask release. The closure-call row is now
22 ns: 12 for the loop, ~3 for `inc`, ~7 for the call with its body dispatch. `closure call, local head`
(the same loop with `f` in a `let`) reads 21 ns: the var costs ~1 ns.

Protocol call: 45 → 35 ns on a deftype receiver. The row is the loop (12), `+` (~3), the receiver read
(a local) and the call; the call went from ~30 ns (the var's atomic pair, `clj_invoke` → the native's
arity check → `impl_of` under its window with the table scan → `clj_invoke` of the impl → the closure
path with the buffer copy) to ~20: the method arity check, the serial and type loads, the seqlock
snapshot, the window with the epoch check and the atomic retain/release of the shared impl, then the
impl's closure entry through `closure_run` (~6). Removing the window or the retain/release from a
scratch build saved ~1.5 ns each; the rest is spread over the dispatch. The fixnum receiver costs the
same as the deftype one (the cache does not care which table the entry came from; before, the fixnum
went through the side table's hash probe). Bi-morphic adds the `even?` intrinsic, the `if` and a
second entry in the scan: 60 → 50.

The pipeline row moves only 4 %: of its three closure calls per element only `map`'s recursion takes
the direct path (`(f (first s))` and `(f acc x)` call the natives `inc` and `+` through the generic
invoke, the `lazy-seq` thunk is forced from a native), and none is a protocol call.

## IReduce, transducers — Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

`reduce` is a native over a `reduce` slot on the descriptor (vector, vector-seq and range walk their
storage with no seq objects; cons, lazy-seq and string go through `clj_seq_iter`), calls its fn
through a `clj_call` prepared once (a closure's arity resolved, its body entered without
`clj_invoke`), and stops at `reduced`; `map`/`filter`/... carry transducer arities, `transduce`,
`into` with an xform, `sequence` and `eduction` drive them (NOTES.md, "`reduce` is a `reduce` slot"
and "Transducers"). Four new rows next to the lazy pipeline; the "before" of the two transducer rows is
the lazy pipeline computing the same thing (`(reduce + (map inc (range n)))`, `(count (into []
(map inc (range n))))`) on the base binary, since the forms did not exist there. Medians of three
alternating runs of each binary (7e66f30 with this harness vs this commit), ns per element; the
map, vector and call tables matched within their noise.

| scenario | n | before | after | change |
|---|---:|---:|---:|---:|
| reduce + map inc range | 1000 | 183.6 | 155.2 | −15 % |
| reduce + map inc range | 100000 | 180.5 | 158.7 | −12 % |
| transduce (map inc) + range | 1000 | 181.4 | 30.0 | −83 % |
| transduce (map inc) + range | 100000 | 179.8 | 28.9 | −84 % |
| reduce + range | 1000 | 35.9 | 7.0 | −81 % |
| reduce + range | 100000 | 35.9 | 6.5 | −82 % |
| reduce + vector | 1000 | 37.5 | 6.6 | −82 % |
| reduce + vector | 100000 | 38.8 | 7.2 | −81 % |
| into [] (map inc) range | 1000 | 159.5 | 96.1 | −40 % |
| into [] (map inc) range | 100000 | 163.4 | 97.7 | −40 % |
| seq walk of a vector | 1000 | 37.7 | 37.2 | −1 % |

- **`reduce +` over a range or a vector, 36 → 7 ns.** The `loop*`/`first`/`next` walk with its
  vector-seq (or range) allocation per step is gone; what remains per element is the slot's loop
  (a trie leaf lookup or an add), the native call of `+` (`b_add` → `arith_fold` → `clj_add`, ~3 ns)
  and the reducer's release of the old accumulator and `reduced` check. The C iterator over the same
  vector (`clj_seq_iter`, no call) is 4–5 ns, so the fn call is about half of the 7.
- **`transduce (map inc) + range`, 29 ns**, against 181 for the lazy pipeline computing the same sum:
  the range step and the reducer bookkeeping (~3 ns), the entry into the transducer's `[result input]`
  arity through the prepared `clj_call` (~7: `closure_run` fills a 16-slot frame, the stack guard,
  the shadow frame), then its body `(rf result (f input))` — two calls of natives held in captured
  slots (`f` = `inc`, `rf` = `+`) through the generic `eval_invoke` path at ~8–9 ns each, since the
  intrinsic rewrite only applies to a core var at the head, not a local. Those two calls are the next
  target (design §6b item 6's optimizer pass, or an intrinsic-by-value guard on captured natives).
- **`clj_call` against `clj_invoke` per element** (a scratch build whose `clj_call_prepare` resolves
  nothing, three alternating runs): transduce 30.0 → 33.2 (1k) and 28.9 → 33.4 (100k), so the
  direct closure entry saves ~3–4 ns of the ~10 a `clj_invoke` → `fn_invoke` → `clj_closure_invoke`
  → `arity_for` → `closure_run` chain costs; on the native path (`reduce + range`, 7.0 → 7.2 and
  6.5 → 6.9) the saving is the fn-kind switch and the arity check, ~0.3 ns. Kept for the closure path.
- **`into [] (map inc) range`, 160 → 97 ns.** The transduce part is ~29; the other ~65 is `conj`
  called as the reducing fn: `b_conj` retains the accumulator before `clj_vector_conj` (the +0
  argument convention: the reducer still holds it), so the vector is shared at every step and each
  conj copies the tail node (up to 32 slots) and the wrapper, then the old version is freed. A
  transient, or a reduce that transfers ownership of the accumulator to the rf, is what removes it
  (NOTES.md, "`into` is C in both arities").
- **`reduce + map inc range`, 182 → 156 ns.** Only the consumer changed: the `loop*` body (a closure
  call, `first`/`next`, the rebind) became the iterator walk with one native call of `+`. The lazy
  `map` stays: the thunk call, `seq`/`first`/`rest`, `(f (first s))` through the generic invoke, and
  the cons, lazy-seq and thunk-closure allocations per element — the reason `transduce` is 5× faster
  on the same work.


## Fusion — Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

The optimizer rewrites `(reduce f [init] P)`, `(into to P)`, `(vec P)` and `(count P)` over a nest of
lazy stages into a FUSED node: the argument expressions evaluated once, then, while every core var
involved holds its boot root, a driver native fed the stages' transducer arities (NOTES.md, "The
fusion pass"). Three rows at n = 10, 1k and 100k: `(reduce + (map inc (range n)))` (the same form as
before, now fused), `(reduce + (map inc (filter even? (range n))))` and `(count (vec (map inc (range
n))))`; n = 10 shows the per-form cost. Medians of three alternating runs of each binary (2e765b2 with
this harness vs this commit), ns per element; the map, vector and call tables matched within their
noise (±5 %).

| scenario | n | before | after | change |
|---|---:|---:|---:|---:|
| reduce + map inc range | 10 | 175.5 | 63.8 | −64 % |
| reduce + map inc range | 1000 | 152.0 | 33.3 | −78 % |
| reduce + map inc range | 100000 | 151.7 | 31.4 | −79 % |
| reduce + map inc (filter even?) range | 10 | 245.3 | 73.8 | −70 % |
| reduce + map inc (filter even?) range | 1000 | 212.6 | 34.2 | −84 % |
| reduce + map inc (filter even?) range | 100000 | 216.3 | 33.9 | −84 % |
| vec (map inc range) | 10 | 190.4 | 76.6 | −60 % |
| vec (map inc range) | 1000 | 157.2 | 38.4 | −76 % |
| vec (map inc range) | 100000 | 158.5 | 37.1 | −77 % |
| transduce (map inc) + range | 1000 | 29.5 | 28.6 | −3 % |
| transduce (map inc) + range | 100000 | 30.6 | 29.6 | −3 % |
| into [] (map inc) range | 1000 | 95.2 | 91.8 | −4 % |
| into [] (map inc) range | 100000 | 94.7 | 93.4 | −1 % |
| reduce + range | 1000 | 5.4 | 5.7 | +6 % |
| reduce + vector | 1000 | 5.2 | 5.5 | +6 % |
| seq walk of a vector | 1000 | 36.6 | 36.3 | −1 % |
| counting loop | 100000 | 15.5 | 15.7 | +1 % |
| closure call in a loop | 100000 | 22.1 | 22.2 | +0 % |

- **`reduce + map inc range`, 152 → 33 ns**: the `transduce (map inc) + range` row (29) plus ~3 ns for
  the driver's reducing fn under the transducer (a native with a context, entered through the generic
  invoke, then `+` through a prepared `clj_call`). What remains per element is what the transduce row
  already listed: the range step and the reducer bookkeeping (~3), the entry into `map`'s `[result
  input]` arity (~7), and its body `(rf result (f input))` — two calls of fns held in captured slots
  (`f` = `inc`, `rf` = the driver's fn) through the generic `eval_invoke` at ~8–9 each. Those two calls
  are the next target: an intrinsic-by-value guard on a captured native, or the direct call of local
  fns (design §6b item 7).
- **With `filter even?`, 213 → 34**: per source element the filter stage's entry and `(pred input)`
  (~16), and on the half that passes the map stage, `inc`, the driver and `+` (~30 / 2); the lazy
  version paid a thunk, a cons and a lazy seq per element per stage.
- **`vec (map inc range)`, 157 → 38**, against 92 for `(into [] (map inc) (range n))` computing the
  same vector: the driver's accumulator is held by the driver alone, so `clj_conj` appends to the
  vector in place (~5 ns per element) where the C `into` retains the accumulator before every conj
  and copies the tail node each time. The same ownership transfer is what `into` with an xform still
  needs (NOTES.md, "`into` is C in both arities").
- **n = 10: ~310 ns per form** (64 ns per element minus ten elements at 33): the driver's fn and its
  context (one pool object, one `calloc`), the vector literal, the `(map g)` call building the
  transducer (~15), the `(xf rf)` application (a closure call and a closure per stage), the reduce
  slot dispatch, the completion call and the guard (three relaxed loads). The lazy form's fixed cost
  was ~230 ns (its first thunk, cons and lazy seq), so a tiny pipeline still gains 2.7×. Nothing here
  is worth an exec-side cache: only the ~15 ns of transducer construction could be shared between
  evaluations, the application must be fresh per run (NOTES.md).
- The unfused rows (`transduce`, `into` with an xform, `reduce` over a range or a vector, the walks
  and calls) move within the ±5 % run-to-run spread; the +6 % on the two 5 ns rows is 0.3 ns of code
  placement, as the "C iterator" note above describes.

## Direct native call at invoke sites — Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

A plain native (`CLJ_FN_NATIVE`) at the head of an invoke — a builtin held in a local, a captured slot,
a param or a user var — is called from the site's borrowed argument buffer after the arity check
`fn_invoke` would make (`call_native`, eval.c): no `clj_is_protocol_method` probe first, no `clj_invoke`,
no type slot, no fn-kind switch. Medians of three alternating runs of each binary (0503945 vs this
commit), ns per element or iteration; the map and vector tables matched within their noise.

| scenario | n | before | after | change |
|---|---:|---:|---:|---:|
| reduce + map inc range | 10 | 65.4 | 60.9 | −7 % |
| reduce + map inc range | 1000 | 31.9 | 30.9 | −3 % |
| reduce + map inc range | 100000 | 31.6 | 29.9 | −5 % |
| reduce + map inc (filter even?) range | 1000 | 35.4 | 33.2 | −6 % |
| reduce + map inc (filter even?) range | 100000 | 33.7 | 32.0 | −5 % |
| vec (map inc range) | 1000 | 38.9 | 36.4 | −6 % |
| vec (map inc range) | 100000 | 38.0 | 35.9 | −6 % |
| transduce (map inc) + range | 1000 | 29.1 | 27.2 | −7 % |
| transduce (map inc) + range | 100000 | 30.0 | 27.1 | −10 % |
| into [] (map inc) range | 1000 | 96.6 | 93.5 | −3 % |
| into [] (map inc) range | 100000 | 101.3 | 96.9 | −4 % |
| reduce + range | 1000 | 5.8 | 5.5 | −5 % |
| reduce + vector | 1000 | 5.6 | 5.3 | −5 % |
| seq walk of a vector | 1000 | 39.1 | 37.9 | −3 % |
| counting loop | 100000 | 15.9 | 15.6 | −2 % |
| closure call in a loop | 100000 | 23.0 | 22.1 | −4 % |
| protocol call, deftype receiver | 100000 | 35.7 | 36.1 | +1 % |
| protocol call, fixnum receiver | 100000 | 37.0 | 36.8 | −1 % |
| protocol call, bi-morphic | 100000 | 51.6 | 51.6 | 0 % |

- **Fused pipeline and transduce, ~2–3 ns per element.** Of the two calls of fns held in captured slots
  in `map`'s `[result input]` arity, `(f input)` with `f` = `inc` is a plain native and takes the new
  path; `(rf result x)` with the driver's reducing fn is a native with a context (`clj_fn_native_ctx`)
  and still goes through `clj_invoke`. The saving per native call is the protocol probe, the
  `invoke_at` → `clj_invoke` → `fn_invoke` chain and its kind switch: ~2 ns of the ~9 the call cost.
- **Two variants measured against this one and not kept**, three alternating runs each. *Intrinsic by
  value*: a reverse index from the boot builtin fn object to its intrinsics entries (two bytes filled by
  `clj_intrinsics_install`), the site calling the fixed-arity C function without the `(args, n)`
  convention or the native's arity check — fused rows 30.9 → 31.5 (1k), 29.9 → 30.5 (100k), transduce
  27.2 → 27.9, i.e. within noise or slightly worse: the builtins forward to the same function in one
  call already, and the lookup is a branch and two loads on every native call. *Context natives from
  the site too* (a host fn, the driver's rf): fused rows 30.4 → 31.0, transduce 27.1 → 27.1, nothing
  measurable, so the path stays the narrow one the code can explain.
- The rows without a native at a local head (the walk, the loops, the protocol calls) move within the
  run-to-run spread; the two 5 ns rows are the ±0.3 ns placement swing described above.

## Direct local fns — Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

A let/loop-bound fn whose binding is only ever the head of a call is a DIRECT_FN node: the let stores
nil, each call evaluates its arguments into a fresh frame linked to the defining one and runs the body
there — no closure allocated, no capture retained, no arity lookup (NOTES.md, "Direct local fns";
design §6b item 7). Two new rows against the var-headed closure call: the same loop with `f` bound
by a let around it, and a helper bound *inside* the loop body over a loop variable, which was a
closure allocation with one capture per iteration. Medians of three alternating runs of each binary
(2f91f17 vs this commit), ns per element or iteration; the machine ran ~1 ns warmer than in the
section above, so compare within the table.

| scenario | n | before | after | change |
|---|---:|---:|---:|---:|
| loop with a local helper | 100000 | 54.8 | 38.8 | −29 % |
| let-bound fn called in a loop | 100000 | 22.8 | 20.8 | −9 % |
| closure call in a loop | 100000 | 23.8 | 22.6 | −5 % |
| counting loop | 100000 | 16.8 | 15.8 | −6 % |
| reduce + map inc range | 1000 | 32.5 | 31.2 | −4 % |
| reduce + map inc range | 100000 | 31.9 | 30.1 | −6 % |
| transduce (map inc) + range | 100000 | 29.0 | 27.4 | −6 % |
| into [] (map inc) range | 100000 | 102.6 | 96.6 | −6 % |
| seq walk of a vector | 1000 | 39.8 | 38.4 | −4 % |
| protocol call, deftype receiver | 100000 | 38.9 | 36.2 | −7 % |
| protocol call, bi-morphic | 100000 | 55.7 | 52.1 | −6 % |

- **Local helper, 55 → 39 ns.** The iteration paid a closure per `let` (a pool object, the capture of
  `acc` retained and released, the exec retained) and then the closure call; now the let stores nil and
  the call is the frame setup, the guard, the shadow frame and the body. What remains is the loop (12),
  `<`/`inc`/`+` (~9) and the call itself (~7: the same mechanics as a closure entry, the fn value load
  and the three loads to its arity gone, the static link stored instead).
- **Let-bound fn, 23 → 21 ns.** The closure was created once per call of the enclosing fn, so only the
  call changed: the arity lookup on the live fn (three dependent loads) against the arity pointer the
  node carries.
- **The free-variable read stays owned.** A borrowed OUTER read (a fifth case in `eval_borrowed`)
  turned that switch into a jump table in every inlined copy and cost ~3 ns per iteration of the
  counting loop and every other row — more than the retain/release pair it saves, which on a fixnum
  (`acc` above) is nothing and on an unshared object five plain instructions. Measured in three
  variants (inline, out of line, folded into the default branch): all +3 ns; the case is gone.
- The rows without a local fn move within the run-to-run spread, on the better side here because
  the base binary ran first in a warmer minute; nothing in their path changed.

## Host-defined fns and the sort primitive — Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

`Runtime.define` binds a Swift closure as a var root (NOTES.md, host bridge). Two rows in the call table
put the Swift↔C transition next to a C builtin called from the same site, and one row puts the `sort`
primitive next to the Clojure merge sort that is its specification. Clean `--scratch-path` release build,
three runs of the same binary, ns per iteration (calls) or per element (sort).

| scenario | n | run 1 | run 2 | run 3 | median |
|---|---:|---:|---:|---:|---:|
| counting loop | 100000 | 17.1 | 15.8 | 16.3 | 16.3 |
| C builtin call in a loop | 100000 | 17.9 | 17.9 | 17.9 | 17.9 |
| closure call in a loop | 100000 | 22.2 | 23.0 | 22.3 | 22.3 |
| host fn call in a loop | 100000 | 81.5 | 82.0 | 81.1 | 81.5 |
| sort, shuffled fixnums, Swift primitive | 1000 | 82.9 | 83.5 | 83.5 | 83.5 |
| sort, shuffled fixnums, Clojure spec | 1000 | 4898 | 4962 | 5002 | 4962 |
| sort, shuffled fixnums, `[Int].sorted()` | 1000 | 13.3 | 13.4 | 13.5 | 13.4 |

- **A host fn call costs ~64 ns over a C builtin at the same site** (81.5 − 17.9); a closure costs ~4.5.
  The builtin (`(def f inc)`, a plain native) is called from the site's argument buffer; the host fn is a
  context native through `clj_invoke`, then the bridge: an `[Value]` array per call (a heap allocation and
  its release), a `Value(borrowing:)` per argument, the `NativeBody` box through `Unmanaged`, the Swift
  closure, the result retained back. The design's "tens of ns" for the transition holds at the upper end;
  the array is the likely bulk, not measured apart. Trigger: a host fn in a per-element position of a
  profile; then a body signature over an argument buffer, or a fixed-arity variant without the array.
- **`sort` of 1k fixnums: 83 ns per element in Swift, 4960 through the Clojure spec, 13 in
  `[Int].sorted()`.** The primitive pays ~10k `compare` calls through a throwing Swift function, a retain
  per item, the cons list it returns and the `clj_share` of what crosses; the spec pays an interpreted
  `compare` var call, `nth`, `conj` and a vector copy per merge step. 59× is the escape hatch's payoff on
  coarse-grained work, against the 64 ns every crossing costs.

## Constant folding — Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

The optimizer folds a pure intrinsic on constant arguments and an `if` on a constant test into what they
yield (NOTES.md, "Constant folding"). No evaluator path changed and no bench form has a constant call in its
loop, so this is a control: medians of three alternating runs of each binary (2a89bb2 vs this commit), ns per
element or iteration — reduce + map inc range 30.4 → 31.1 (1k), 30.1 → 30.7 (100k); vec (map inc range)
36.5 → 37.3 (1k), 35.6 → 36.7 (100k); transduce (map inc) + range 27.9 → 27.7 (1k), 27.8 → 27.6 (100k);
into [] (map inc) range 94.1 → 95.7 (1k), 96.0 → 96.9 (100k); seq walk of a vector 38.2 → 36.9; counting loop
15.9 → 16.0; closure call in a loop 22.2 → 22.5; let-bound fn 21.2 → 20.9; local helper 36.5 → 36.8. All
within the ±3 % run-to-run spread.

## Last-use reuse — Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

A local read that is the last use of its slot on its path hands the frame's reference to the consumer instead
of borrowing it, and a consuming intrinsic (`conj`, `assoc`, `dissoc`, `with-meta`) called with a collection the
site owns calls the consuming core directly, so a unique collection is updated in place; `reduce`'s slots and
the fusion drivers hand their own accumulator to a consuming native reducing fn the same way, and `into` with
an xform runs through the `fused-into*` driver (NOTES.md, "Last-use reuse"). Medians of three alternating runs
of each binary (12f8771 vs this commit), ns per element or iteration; the map and vector tables matched within
their noise.

| scenario | n | before | after | change |
|---|---:|---:|---:|---:|
| into [] (map inc) range | 1000 | 93.2 | 36.4 | −61 % |
| into [] (map inc) range | 100000 | 96.5 | 36.1 | −63 % |
| reduce + map inc range | 10 | 59.1 | 63.3 | +7 % |
| reduce + map inc range | 1000 | 30.1 | 31.8 | +6 % |
| reduce + map inc range | 100000 | 29.8 | 31.6 | +6 % |
| reduce + map inc (filter even?) range | 1000 | 32.6 | 33.9 | +4 % |
| reduce + map inc (filter even?) range | 100000 | 31.9 | 33.5 | +5 % |
| vec (map inc range) | 1000 | 35.6 | 36.2 | +2 % |
| vec (map inc range) | 100000 | 35.4 | 35.9 | +1 % |
| transduce (map inc) + range | 1000 | 28.2 | 27.5 | −2 % |
| transduce (map inc) + range | 100000 | 27.2 | 27.4 | +1 % |
| reduce + range | 1000 | 5.5 | 5.8 | +5 % |
| reduce + range | 100000 | 5.4 | 5.8 | +7 % |
| reduce + vector | 1000 | 5.4 | 5.6 | +4 % |
| reduce + vector | 100000 | 5.3 | 5.6 | +6 % |
| seq walk of a vector | 1000 | 38.4 | 36.2 | −6 % |
| counting loop | 100000 | 15.7 | 15.4 | −2 % |
| closure call in a loop | 100000 | 21.9 | 21.9 | +0 % |
| let-bound fn called in a loop | 100000 | 20.9 | 21.5 | +3 % |
| loop with a local helper | 100000 | 36.5 | 36.4 | −0 % |
| protocol call, deftype receiver | 100000 | 35.6 | 35.8 | +1 % |

- **`into [] (map inc) range`, 93 → 36 ns**, now the same as `vec (map inc range)` computing the same vector:
  the ~60 ns of tail copying per element are gone, since the driver's accumulator is held by the driver alone
  and `conj` appends in place. What remains is the transduce part (~28) and the driver's step (~8).
- **The counting loop, the closure call and the walk are unchanged.** Three variants of the read were measured
  on the way (three alternating runs each, the counting loop row): a distinct node kind dispatched through the
  exec table for the last-use read, +3 ns per iteration (the `i` of `(inc i)` is a last use, and an indirect
  call replaced an inlined load); the flag inside the LOCAL case of `eval_borrowed` with the function left to
  the compiler's inlining, +5.5 ns (it stopped inlining and emitted a call per borrowed read); the flag with the
  function force-inlined and every last use marked, +2.5 ns (the hand-over itself — the slot write, the mask
  bit, the release loop — on a fixnum nobody consumes). Kept: the flag, force-inlined, marked only where the
  consumer can own the value (the collection of a consuming intrinsic, a let init, a recur argument, a body's
  value), 15.7 → 15.4. Marking call arguments as well (the callee's frame owns them, so `(f v)` lets `f` conj in
  place) cost ~2 ns per call on fixnum arguments — closure call 22.0 → 24.2, let-bound fn 21.0 → 22.7, local
  helper 36.1 → 39.0 — and is not kept; trigger: a profile with a collection built through a helper per element.
- **The reduce-driven rows pay ~0.3 ns per element** (`reduce + range` 5.5 → 5.8): the reducer's step and the
  fusion bottom check for a consuming reducing fn once per element, and every borrowed local read tests the
  flag. The fused pipeline (`map`'s transducer arity reads four locals per element) shows it as ~1.5 ns.

## Growing a collection per step — Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

Three rows for the last-use hand-over (NOTES.md, "Last-use reuse"): `(loop [v [] i 0] (if (< i n) (recur
(conj v i) (inc i)) v))`, the same with `(assoc m i i)` into a map, and `(reduce conj [] (range n))`, each
counted at the end; the Swift columns are an `Array.append` loop and a `Dictionary` insert loop. The "before"
binary is the C core of 12f8771 (before the hand-over) with this harness, "after" is this commit; medians of
three alternating runs, ns per element or iteration. The other rows are the re-measurement the section above
promised, on a session with one warmer run in the "after" set.

| scenario | n | before | after | change | Swift |
|---|---:|---:|---:|---:|---:|
| loop conj into a vector | 1000 | 89.3 | 33.8 | −62 % | 1.3 |
| loop conj into a vector | 100000 | 92.8 | 34.6 | −63 % | 1.9 |
| loop assoc into a map | 1000 | 216.9 | 83.0 | −62 % | 20.5 |
| loop assoc into a map | 100000 | 369.1 | 109.3 | −70 % | 39.2 |
| reduce conj [] range | 1000 | 67.8 | 11.8 | −83 % | 1.3 |
| reduce conj [] range | 100000 | 69.8 | 12.1 | −83 % | 1.8 |
| into [] (map inc) range | 1000 | 92.7 | 36.3 | −61 % | 2.7 |
| into [] (map inc) range | 100000 | 96.0 | 36.5 | −62 % | 2.8 |
| vec (map inc range) | 1000 | 36.1 | 37.8 | +5 % | 2.8 |
| vec (map inc range) | 100000 | 36.2 | 36.3 | +0 % | 3.0 |
| reduce + map inc range | 1000 | 30.6 | 32.3 | +6 % | 0.3 |
| reduce + map inc range | 100000 | 31.3 | 31.8 | +2 % | 0.3 |
| transduce (map inc) + range | 100000 | 27.0 | 27.8 | +3 % | 0.3 |
| reduce + range | 100000 | 5.4 | 5.7 | +6 % | 0.1 |
| seq walk of a vector | 1000 | 36.3 | 37.9 | +4 % | 0.1 |
| counting loop | 100000 | 15.5 | 16.0 | +3 % | — |
| closure call in a loop | 100000 | 21.9 | 22.8 | +4 % | 0.8 |
| let-bound fn called in a loop | 100000 | 20.8 | 21.7 | +4 % | — |
| loop with a local helper | 100000 | 35.7 | 37.1 | +4 % | — |

- **`loop conj`, 89 → 34 ns per element.** The `conj` was a copy of the tail node per step (up to 32 slots)
  plus the wrapper, then the old version freed; now `v`'s read in `(conj v i)` is its last use, the frame's
  reference goes to `clj_conj`, and at rc 1 the tail is appended in place (~5 ns, the "conj, old version
  dropped" row of the vector table is 8). What remains is the loop itself (~16: `<`, `inc`, the `if`, the
  recur rebind of two slots) and the intrinsic's guard and call. `Array.append` is 1.3.
- **`loop assoc` into a map, 217 → 83 (1k), 369 → 109 (100k).** Same mechanism on the HAMT: the path from
  the root to the leaf was copied per step (deeper at 100k), now it is edited in place; the map table's
  "assoc, old version dropped" row (25 / 78 ns) is the floor, the rest is the loop.
- **`reduce conj [] range`, 68 → 12.** The reducer's own accumulator goes to `clj_conj` per element (the driver
  hand-over); the row is the range step, the reducer bookkeeping and the in-place append. Against `(reduce +
  (range n))` at 5.7, the conj costs ~6 ns per element.
- The rows without a growing collection moved +2–6 % in this session; the previous section's cleaner run put
  the counting loop, the closure call and the walk within ±2 %, and the reduce-driven rows at +0.3 ns per
  element for the consuming-fn check, which is where the difference lives.

## Atoms — Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

The atom of design §4 ("Атомы"): one `clj_lock` (os_unfair_lock) per atom, `f` once under it, watches after
it, `deref` under it; without watches or a validator `swap!` hands the atom's own reference to `f`, so
`(swap! a assoc i i)` edits the map in place (the hand-over is removed since: the dated subsection at the end
of this section). Medians of five runs, ns per iteration;
the Swift column is an `os_unfair_lock` around a `Dictionary` insert, an `Int` increment or a `Dictionary`
read, the closest thing to what the atom does.

| scenario | n | interpreted | Swift locked | interpreted / Swift |
|---|---:|---:|---:|---:|
| swap! assoc, in place | 100000 | 140.5 | 53.1 | 2.6× |
| swap! assoc, second holder | 100000 | 765.2 | — | — |
| swap! assoc, watched | 100000 | 762.2 | — | — |
| loop assoc into a map (no atom) | 100000 | 102.6 | — | — |
| swap! inc | 100000 | 41.0 | 1.8 | 23.1× |
| get @atom :k | 100000 | 55.4 | 15.4 | 3.6× |
| get @volatile :k | 100000 | 49.6 | — | — |
| counting loop | 100000 | 15.9 | — | — |
| swap! inc, 4 threads | 100000 | 71.7 | 8.7 | 8.2× |
| swap! assoc, 4 threads | 100000 | 241.3 | — | — |

- **`swap! assoc` in place, 140 ns against 103 for the same `assoc` loop over a local map**: the ~38 ns are
  the `swap!` call (a 4-argument native through the var, ~9), `clj_call_prepare` on `assoc` (the consuming
  entry is now one load off the fn object: the table scan it replaced cost 15 ns per swap, `swap! inc` 56
  → 41), the lock pair (~2), `clj_share` of the result (stops at the shared root) and the retain of the
  new value for the caller. The Swift row is a `Dictionary` insert under the same lock kind.
- **A second holder costs 5×** (765): `(let [old @a] (swap! a assoc i i))` keeps the previous version
  alive across the swap, so `assoc` copies the path from the root to the leaf — the map table's "assoc,
  all versions kept" row at 100k is 674 — with an atomic retain per child of every copied node, since the
  whole map is shared. A watched atom takes the same path (762) because the watch must see `old` intact;
  the watch call itself is in the noise at this size.
- **`swap! inc`, 41 ns; `deref` + `get`, 55 against 50 through a volatile**: the lock pair, the owner-id
  store and the atomic retain/release of the shared value cost ~5 ns per `deref` over the unlocked cell;
  the rest is the interpreted loop (16) and the calls.
- **Four threads on one atom: 72 ns per `swap! inc` and 241 per `swap! assoc`** (total throughput, four
  workers each doing a quarter): 1.7× the single-thread cost per op for the counter, the lock handing
  off between cores at every op; the map case adds the cache-line traffic of a trie four cores edit in
  turn. Swift's locked increment goes 1.8 → 8.7 the same way.

### The share of the atomic path with state in an atom

Design §4 deferred one question to this point: with the application state in an atom, what share of the
retain/release traffic takes the atomic path? Debug builds now count every retain and release by path
(`clj_debug_rc_ops`: plain, shared, immortal; `CLJ_BENCH_ONLY=rc-share` on a debug `clj-bench` runs the
workload). One tick of the workload: `(swap! state assoc-in [:users i] {:id i :name "x"})`, `(swap! state
update :counter inc)`, then `(get-in @state [:users (- i 1) :name] "")` and `(get @state :counter)`; the
same tick over a `loop` local that is never published is the baseline.

| workload | n | plain | shared | immortal | shared share |
|---|---:|---:|---:|---:|---:|
| state in an atom | 1000 | 21001 | 78965 | 26006 | 79.0 % |
| state in an atom | 10000 | 210001 | 1033571 | 260006 | 83.1 % |
| state in a watched atom | 1000 | 21002 | 82969 | 26010 | 79.8 % |
| state in a watched atom | 10000 | 210002 | 1073575 | 260010 | 83.6 % |
| state in a loop local | 1000 | 91966 | 0 | 26006 | 0.0 % |
| state in a loop local | 10000 | 1163572 | 0 | 260006 | 0.0 % |

- **79–83 % of the pairs are atomic once the state lives in an atom**, and every one of them is on an object
  the *owner* thread made and only the owner touches: the flag is monotone, so the first `reset!` puts the
  whole domain on the atomic path forever, exactly as the design's worst case says. The 21 plain pairs per
  tick are the fresh values before they are stored (the `[:users i]` path vector, the `{:id i :name "x"}`
  literal, `assoc-in`'s intermediate maps); the immortal ones (26 per tick, keywords and core roots) are
  no-ops either way. A watch changes nothing here: the copy path retains the same children.
- **Interpretation.** An atomic pair on Apple silicon is ~5 ns against ~1 for the plain one, so at ~80
  pairs per tick the flag costs the atom workload on the order of 300 ns per tick — the same order as
  one `swap! assoc` (140 in place, 765 copied). That is the baseline Swift ARC pays everywhere; the
  design's promise was "no loss against it, a win on transients", and the win is the in-place row above.
  Whether BRC (Choi 2018: the owner keeps the plain path on published objects) earns its header word is
  now a number: at most ~4 ns × 80 pairs per tick of this shape, against 8 bytes on every object (cons 32
  → 40, a size-class step) and the merge protocol. Decision: not now — the in-place hand-over recovers
  more per swap than BRC could, and the copy path (a second holder, a watch) is dominated by the node
  copies, not by their retains. Trigger: a profile of a real app-state loop where the atomic pairs are a
  visible fraction next to the interpreter's own per-node cost (~10–15 ns per node today).

### swap! without the hand-over — 2026-09-15, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

The uniqueness trick is gone (NOTES.md, "Atoms"): `swap!` keeps the atom's reference and hands `f` the value
at +0, so a throw out of `f` leaves the state as it was (the JVM contract), and every `assoc` inside a
`swap!` copies the root-to-leaf path. The "in place" / "second holder" rows above are replaced by
`(swap! a assoc k v)` over a map of 16, 1000 and 100000 keys that the atom alone holds, `k` cycling through
the keys and `v` new on every call — an assoc of the value already there copies nothing, and the first cut
of this row measured that no-op at 100000 keys (130 ns in both binaries). "Before" is the commit before this
change with the same bench file, so its three sized rows are the in-place path; both binaries built clean
with `--scratch-path`, run one after the other on an idle machine (the `counting loop`, `swap! inc`, `deref`
and 4-thread rows reproduce the table above within 3 %). Medians of five runs, ns per iteration.

| scenario | n | before (hand-over) | after | after / before | Swift locked |
|---|---:|---:|---:|---:|---:|
| swap! assoc, map of 16 keys | 100000 | 90.7 | 288.4 | 3.2× | 29.1 |
| swap! assoc, map of 1000 keys | 100000 | 100.5 | 612.6 | 6.1× | 27.3 |
| swap! assoc, map of 100000 keys | 100000 | 130.7 | 904.3 | 6.9× | 27.7 |
| swap! assoc, watched | 100000 | 774.7 | 786.4 | 1.0× | — |
| loop assoc into a map (no atom) | 100000 | 106.5 | 104.9 | — | — |
| swap! inc | 100000 | 40.7 | 42.6 | 1.0× | 1.9 |
| get @atom :k | 100000 | 54.9 | 54.3 | 1.0× | 15.7 |
| get @volatile :k | 100000 | 50.2 | 50.6 | — | — |
| counting loop | 100000 | 16.1 | 15.5 | — | — |
| swap! inc, 4 threads | 100000 | 73.0 | 69.8 | 1.0× | 9.8 |
| swap! assoc, 4 threads | 100000 | 240.7 | 253.2 | 1.1× | — |

- **The cost of the decision is one path copy per swap, plus the atomic retain/release of every child of
  the copied nodes** (the whole map is shared): +200 ns at 16 keys (one root node), +510 at 1000 (two
  levels, the second one nearly full), +770 at 100000 (four levels). A scratch binary running only this
  loop over 16, 100, 300, 1k, 3k, 10k, 30k and 100k keys grows monotonically — ~260, 390, 435, 525, 600,
  650, 755, 907 — with trie depth and node fill, no cliff; the in-place path in the same sweep goes 90 → 134.
  A run with a load spike measured 1396 at 100000 keys; the runs reported bracket the sweep number.
- **Everything else is unchanged**: the watched atom always took the copying path (775 → 786, noise); the
  4-thread assoc (241 → 253) is a growing map contended by four cores, the lock hand-off and the
  cache-line traffic, not the copy; `swap! inc`, `deref` and the counter under contention are the lock pair
  and the interpreted loop as before.
- **Against Swift**: a locked `Dictionary` write of an existing key is 27–29 ns at every size, so the
  persistent copy path is 10–33× a mutable write here, against 3–5× with the hand-over. Trigger to bring the
  trick back is in NOTES.md ("Atoms"): a profile with `swap!` on a large map hot, and then a proven
  no-throw `f` or an undo journal, whichever is cheaper.

## Dynamic vars — 2026-09-16, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

A var read in `eval_borrowed` now tests the var's `dynamic` byte before the root load (a bound dynamic var
derefs through the thread's binding frame). The two rows that read a var per iteration, from one run of the
full bench after the change, against the last recorded ones:

| scenario | n | before, ns/op | after, ns/op |
|---|---|---|---|
| counting loop | 100000 | 15.5–16.1 | 14.3 / 15.3 (two rows of the same run) |
| closure call in a loop | 100000 | 21.9–22.8 | 20.8 |
| C builtin call in a loop | 100000 | — | 16.3 |
| let-bound fn called in a loop | 100000 | 20.9–21.2 | 19.9 |

Within the run-to-run noise: the byte sits in the var's own cache line next to the root, and the branch is
never taken for a non-dynamic var.

## Sorted collections, multimethod dispatch — 2026-09-16, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

The left-leaning red-black tree of sorted.c against the CHAMT of map.c over the same keys and probes.
Every comparison is a call of `clojure.core/compare`, which is a host primitive (Primitives.swift), so
each tree level costs one Swift↔C transition; the ratio column is that transition, not the tree.

| scenario | n | sorted map | hash map | sorted / hash |
|---|---:|---:|---:|---:|
| assoc, old version dropped | 10 | 230.4 | 22.7 | 10.1× |
| get, hit | 10 | 227.6 | 2.7 | 83.3× |
| assoc, old version dropped | 1000 | 742.6 | 38.0 | 19.5× |
| get, hit | 1000 | 699.2 | 13.0 | 53.6× |
| assoc, old version dropped | 100000 | 1318.7 | 78.1 | 16.9× |
| get, hit | 100000 | 1207.9 | 18.0 | 67.1× |

- **A `get` is one host call per level**: 227 ns at 10 keys (~4 levels), 699 at 1000 (~10), 1208 at
  100000 (~17) — ~70 ns each, flat in the key count, which is the transition and not the comparison.
  The section below is the same table after `clj_compare` landed.
- **`assoc` adds the node copies** on top of the same walk: 230 → 743 → 1319 against the trie's 23 → 38 → 78.

One multimethod call per iteration inside an interpreted loop, dispatch fn `identity`. "pre-hierarchy
shape" is the dispatch this replaced — `=` against the method table with a `:default` fallback, one
variadic `invoke` — as a deftype in the bench, so both rows come from the same run.

| scenario | n | ns/op |
|---|---:|---:|
| multimethod, = hit | 100000 | 229.1 |
| multimethod, = hit, pre-hierarchy shape | 100000 | 319.7 |
| multimethod, isa? hit | 100000 | 225.8 |
| multimethod, :default hit | 100000 | 222.0 |
| protocol call, keyword receiver | 100000 | 35.0 |
| plain fn call through a var | 100000 | 26.1 |

- **`isa?` dispatch costs what `=` dispatch costs**: both are a hit in the `[hierarchy-value {dv method}]`
  cache, so the hierarchy walk happens once per dispatch value and never again until the hierarchy or the
  table changes.
- **The new shape is 28 % faster than the one it replaces**, although it does strictly more work: fixed
  `invoke` arities up to three remove the rest seq and the two `apply`s, which cost more than the lookup.
- **Still 6× a protocol call and 9× a plain call**: what a protocol call has and a multimethod has not is
  the call-site cache of eval.c. Trigger for one here is a profile with multimethod dispatch hot.

## Numeric tower — 2026-09-16, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

The rows that would show a slower `+`, taken before the tower (fixnum and double only, `/` of fixnums
yielding a double) and after it (five kinds behind `clj_num_arith`, the fixnum and double paths still in
builtins.c ahead of the ladder). Two separate `clj-bench` invocations, so the spread between them is the
session-to-session drift the file warns about; the counting-loop row appears twice per run because two
sections measure it.

| scenario | n | before | after |
|---|---:|---:|---:|
| counting loop | 100000 | 17.1 / 17.3 | 15.4 / 15.9 |
| reduce + range | 1000 | 6.0 | 5.5 |
| reduce + range | 100000 | 6.0 | 5.3 |
| reduce + vector | 1000 | 5.7 | 5.2 |
| reduce + vector | 100000 | 5.8 | 5.2 |
| reduce + map inc range | 1000 | 33.4 | 32.3 |
| reduce + map inc range | 100000 | 33.1 | 30.9 |
| swap! inc | 100000 | 44.8 | 43.6 |
| swap! inc, 4 threads | 100000 | 70.1 | 70.8 |

- **Nothing regressed**: every row is equal or slightly faster, which is the drift, not the change.
  `clj_add` still starts with `to_num` on both arguments — a tag check each — and only a pair that is
  neither a fixnum nor a double reaches `clj_num_arith`, so the hot path gained no branch.
- **`clj_num_kind_of` is a type-pointer compare chain**, not a slot on the header, and it runs only where
  the fast paths already failed; `number?` and `integer?` pay it, which no benchmark row exercises.

## compare in C, typed arrays — 2026-09-16, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

`clj_compare` (compare.c) replaced the Swift `compare` primitive inside the core: a nil comparator on a
sorted collection means the C one, `sort`/`sort-by` merge in C, and the Swift primitives are one call each
(NOTES.md, "Sorted"). Before and after are two invocations of the same release build, so the hash-map column
is the control: 24.4 / 40.6 / 77.6 before against 24.2 / 42.0 / 82.1 after.

| scenario | n | sorted map before | sorted map after | before / hash | after / hash |
|---|---:|---:|---:|---:|---:|
| assoc, old version dropped | 10 | 246.6 | 49.8 | 10.1× | 2.1× |
| get, hit | 10 | 241.1 | 18.5 | 85.3× | 6.5× |
| assoc, old version dropped | 1000 | 797.0 | 132.3 | 19.6× | 3.2× |
| get, hit | 1000 | 765.1 | 42.1 | 56.3× | 3.1× |
| assoc, old version dropped | 100000 | 1421.9 | 305.6 | 18.3× | 3.7× |
| get, hit | 100000 | 1401.4 | 158.1 | 71.3× | 8.3× |

| scenario | n | before | after |
|---|---:|---:|---:|
| sort, shuffled fixnums, Swift primitive | 1000 | 82.7 | 59.9 |
| sort, shuffled fixnums, Clojure spec | 1000 | 4913.3 | 4766.4 |

- **The ratio column was the bridge, not the tree.** A `get` at 1000 keys walks ~10 levels: 765 ns before is
  ten crossings at ~70 ns, 42 after is ten C comparisons plus the walk, and what is left over the hash map
  (3.1×) is the tree itself. The 100000-key `get` keeps a wider ratio (8.3×) because 17 levels of pointer
  chasing miss the cache where the trie's 4 do.
- **`sort` of 1k fixnums: 82.7 → 59.9 ns per element.** The remaining cost is the interpreted `clj_invoke`
  entry, the list it builds and `clj_share` of what crosses; the ~10k comparisons themselves no longer
  cross the bridge, and the items are borrowed from the iterator's keep instead of retained one by one. The
  Clojure spec moved with the drift, as it should: it calls the `compare` var either way.

Typed arrays (array.c) against the vector, same 1000 fixnums, the loops interpreted. Medians of four runs
of the same binary; one run measured the whole section at ~2× and is dropped as thermal.

| scenario | n | ns/element |
|---|---:|---:|
| loop + aget over a long-array | 1000 | 45.2 |
| loop + nth over a vector | 1000 | 45.4 |
| reduce + over a long-array | 1000 | 6.8 |
| reduce + over a vector | 1000 | 5.5 |

- **`aget` and `nth` are indistinguishable inside an interpreted loop**: ~45 ns per element is the loop —
  two INTRINSIC nodes, a `recur` and the `+` — and both reads are intrinsics behind it (`aget` was 5 ns
  slower until it joined the table, which is the plain-native call it used to pay). The array's advantage is
  memory, not the read: 8 bytes per element against a trie leaf's 8 plus the node overhead, and a `u8` array
  is one byte per element where a vector is eight plus a box for anything that is not a fixnum.
- **`reduce` shows the box**: 6.8 against the vector's 5.5. The vector's slot hands out the stored word; the
  array's reads the element and boxes it, which is free for a fixnum and an allocation for an `f64` — a
  `double-array` would pay `clj_double_new` per element, which is the trigger for an unboxed reduce path.
## Boxed 64-bit long — 2026-09-16, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

The rows a slower `+` would show, before the boxed long kind (`9cf00fd`) and after it. `clj_add`'s fixnum
path pays the same three tests as before — one overflow check and the two range comparisons that used to
throw and now pick the representation — so no row should move. Three separate `clj-bench` invocations,
each after its own release build; "after, again" is the same binary as "after" and gives the spread.

| scenario | n | before | after | after, again |
|---|---:|---:|---:|---:|
| counting loop | 100000 | 16.1 / 16.3 | 15.5 / 15.8 | 16.3 / 16.6 |
| reduce + range | 1000 | 5.6 | 5.9 | 6.0 |
| reduce + range | 100000 | 5.4 | 5.9 | 5.8 |
| reduce + vector | 1000 | 5.4 | 5.7 | 5.7 |
| reduce + vector | 100000 | 5.4 | 5.9 | 6.1 |
| reduce + map inc range | 1000 | 31.8 | 32.9 | 34.9 |
| reduce + map inc range | 100000 | 32.1 | 32.9 | 33.6 |
| swap! inc | 100000 | 43.6 | 44.6 | 43.2 |
| swap! inc, 4 threads | 100000 | 72.6 | 74.1 | 70.5 |

- **Nothing regressed**: two runs of the identical binary differ by as much as before and after do, and in
  both directions — the counting loop is 15.5 in one and 16.3 in the other. The Swift reference column of
  `reduce + vector` (unchanged code) moved 5.4 → 5.9 → 5.9 across the same three runs, which is the drift
  this file warns about.
- **The fixnum path gained no branch**: `to_num` still decides on two tag checks, and only a pair that is
  neither a fixnum nor a double reaches `clj_num_arith`, where the new `CLJ_NUM_LONG` arm sits between the
  fixnum and the bigint. A boxed long is an allocation, but only for a value no fixnum can hold.

## Records — 2026-09-16, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

The §10 step 1b targets on the first half of shapes-by-observation: a `defrecord` of five keyword fields
against a hash map of the same five keywords and values. The record reads a field by scanning the
descriptor's interned basis keywords by pointer and loading the slot; the hash map hashes the keyword and
walks the trie. `assoc` of a basis key writes the slot, in place when the record is the only reference.
Four runs of the same release binary; the spread between them is ±0.1 ns on the read rows, ±0.3 on the
unique writes and ±4 on the shared `assoc`.

| scenario | n | record | hash map | map / record |
|---|---:|---:|---:|---:|
| (:count r), field 3 of 5 | 100000 | 2.8 | 4.2 | 1.5× |
| (:id r), field 1 of 5 | 100000 | 1.8 | 4.2 | 2.3× |
| (assoc r :count v), unique | 100000 | 4.8 | 7.9 | 1.7× |
| (assoc r :count v), shared | 100000 | 29.9 | 57.2 | 1.9× |
| (update r :count inc), unique | 100000 | 7.3 | 11.7 | 1.6× |

- **Field access hits the ≤2 ns target at the front of the basis and misses it in the middle**: 1.8 ns for
  field 1, 2.8 ns for field 3. The scan costs ~0.5 ns per skipped keyword, so a record's read cost is its
  field position. The design's answer is not a faster scan but the inline cache on the call site (§4,
  "Inline cache на call site"), which turns any position into one guard and one load; a record has the
  layout and not yet the cache.
- **The unique update is 7.3 ns, inside the ≤10 ns target**: 1.8 for the read, 4.8 for the `assoc`, the rest
  the `inc`. Nothing is allocated — the same object comes back with one word changed.
- **The shared `assoc` is six times the unique one** because it copies the header, the five values, the
  extmap and the meta and retains each: one 80-byte pool cell and eight retains. The hash map pays 57 ns for
  the same over two trie nodes.
- **The record is 1.5–2.3× the hash map everywhere**, which is the whole claim of the representation: no key
  hashing on the read, no node copy on the write. It is the floor for what shapes will do for a plain
  `{:id 1 :name "x"}`, since a shape is this layout with a shared descriptor instead of a named type.
## Regex — 66f6491, Apple M3 Pro, 36 GB, Swift 6.2.4

The backtracking matcher of regex.c through the `clojure.core` surface: the pattern is compiled once
outside the measured fn, which is interpreted, so each row is one interpreted call plus the match.
`split` and `replace` keep one matcher context across the matches of a scan, so a match past the first
allocates nothing.

| scenario | C pool | C malloc |
|---|---:|---:|
| `re-find` of `#"\d+"` over a 40-char string | 397.4 | 444.9 |
| `split` on `#","` of a 10-field line | 750.7 | 1224.3 |
| `replace` with `#"(\w+)@"` and `$1`, two matches | 2140.9 | 2143.0 |

- **`re-find` is 400 ns for one match in 40 code points**, most of it the leftmost scan: the matcher
  retries at every start position, as the program carries no first-character or prefix filter. Trigger
  for one: a `re-find` in a profile's inner loop; the fix is a first-set bitmap read before each start.
- **`split` of ten fields is 75 ns per field.** Reusing the context across the matches of one scan took
  it from 1256 to 751 ns (three malloc/free pairs per match gone), which is why the malloc control is
  still 1.6×.
- **`replace` is the slowest row**: each match re-scans from the end of the last one, so the work is
  quadratic in the gaps between matches, and the `$1` expansion walks the replacement again per match.

## Compiler v0 — 7b7b71e, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

`clj-bench` built three ways: the interpreted core (`core_clj.inc`), the compiled core (`-DCLJ_COMPILED_CORE`,
`boot/core.c` from `make boot`) in dev mode, and the compiled core closed (`-DCLJ_CLOSED` on top: no intrinsic
or fusion guards, direct calls between core fns). The fourth column runs the closed binary with every bench
form itself compiled (`CLJ_EVAL=compiled CLJ_EVAL_CLOSED=1 CLJ_EVAL_OPT=-O2`): the loops are C at `-O2` with
direct calls to `(def f ...)`, which is what the user-code rows need to move. One run per column, back to
back (the interpreted column in an earlier session of the same day), so treat ±2 ns as noise; the sorted
`get` row is the C map either way and moved 40 → 53 between sessions on its own.

### Boot and size

| | interpreted core | compiled core | compiled, closed |
|---|---:|---:|---:|
| `clj_init` (median of 3) | 14.9 ms | 1.1 ms | 1.2 ms |
| peak RSS after `clj_init` (2 MB before) | 7 MB | 4 MB | 4 MB |
| `clj-bench` release binary | 1 919 880 B | 3 894 360 B | 3 757 720 B |

The compiled binaries still embed `core_clj.inc` (94 KB of source, `clj_core_source` and the serializable
test read it) and the interpreter; the 2 MB delta is `core.c` and the libs (109 k + 41 k lines of C from
2 366 + 1 000 lines of Clojure, `#line` included). Boot no longer reads, analyzes or evaluates macros: it
interns constants and vars, binds roots and runs the top-level forms.

### The existing rows

ns per iteration or element, `n` = 100000 unless noted.

| scenario | interpreted core | compiled core, dev | compiled core, closed | loops compiled `--closed` |
|---|---:|---:|---:|---:|
| counting loop | 15.3 | 15.4 | 16.2 | 3.8 |
| closure call in a loop | 21.9 | 21.4 | 22.9 | 7.0 |
| C builtin call in a loop | 17.4 | 18.4 | 18.1 | 5.5 |
| let-bound fn called in a loop | 21.0 | 20.9 | 21.8 | 4.1 |
| loop with a local helper (direct fn) | 36.8 | 35.9 | 38.2 | 7.0 |
| protocol call, deftype receiver | 35.3 | 35.7 | 36.5 | 17.3 |
| protocol call, fixnum receiver | 36.1 | 36.7 | 37.5 | 18.1 |
| protocol call, bi-morphic | 51.7 | 52.6 | 54.6 | 22.0 |
| plain fn call through a var | 26.6 | 26.6 | 28.2 | 8.0 |
| multimethod, = hit | 238.6 | 133.8 | 131.1 | 106.0 |
| fused reduce: reduce + map inc range | 31.0 | 17.0 | 17.9 | 16.8 |
| transduce (map inc) + range | 26.9 | 11.8 | 11.7 | 12.0 |
| swap! inc | 41.0 | 42.2 | 42.3 | 23.1 |
| swap! assoc, map of 16 keys | 272.4 | 257.6 | 260.3 | 234.9 |
| sorted map get, hit, n = 1000 | 40.3 | 53.3 | 54.1 | 53.6 |
| record field `(:id r)` | 1.9 | 1.8 | 1.8 | 1.8 |
| record `(assoc r :count v)`, unique | 4.9 | 4.6 | 4.5 | 4.6 |

- **The compiled core moves what runs core.clj code per element**: the transducer stack behind a fused
  `reduce` (31 → 17), `transduce` (27 → 12) and multimethod dispatch (239 → 134, `MultiFn.invoke` and the
  cache lookup are core.clj). Rows whose per-iteration work is the interpreted bench loop plus C (counting
  loop, closure call, protocol call, `swap!`, the collections) do not move, as expected.
- **Closed adds nothing measurable over dev on these rows**: the guards it removes are one relaxed load and
  a compare per intrinsic call, and core-to-core direct calls sit behind the seq machinery the rows measure.
- **The loops compiled through `--closed`** take the counting loop from 15 to 4 (the boxed `<`, `inc` and
  the slot writes remain), a closure call from 22 to 7 (a direct C call of `user_f_a1` with the frame setup,
  guard and shadow frame of `clj_c_enter`), a let-bound or direct-fn call to 4–7, a protocol call to 17–22
  (the method fn's table lookup per call: compiled sites have no inline cache in v0) and `swap! inc` to 23
  (the atom's lock and the boxed `inc` are what is left).

### clang per namespace

| unit | Clojure lines | C lines | `-O0` | `-O2` |
|---|---:|---:|---:|---:|
| core.clj (`boot/core.c`) | 2 366 | 109 582 | 0.98 s | 8.1 s (7.2 s closed) |
| medley.core | 781 | 14 936 | 0.21 s | 1.36 s |
| medley.core-test | 626 | 107 027 | 1.02 s | — |
| one compiled-eval form (the fixture and test forms) | — | 100–700 | 0.12–0.17 s | — |

The test file is bigger than the library it tests: every `is` expands into clojure.test's reporting, and
each expansion is emitted in full. A compiled-eval form costs ~0.6 s end to end, of which `dlopen` is ~0.3 s:
macOS assesses every new code signature on its first load (NOTES.md, "Compiler").

## Facts pass — b1b9b2f, Apple M3 Pro, 36 GB, Swift 6.2.4 (release, pool only)

`make facts-report` loads every library, then reads each file back and times `clj_analyze` and
`clj_facts_of` over the same top-level forms; one run, totals per library (docs/facts-coverage.md).

| input | forms | nodes | analysis, ms | facts, ms | facts / analysis | tables, KB | largest table, KB |
|---|---:|---:|---:|---:|---:|---:|---:|
| core.clj | 264 | 12 416 | 8.2 | 2.5 | 0.31× | 337 | 13 |
| embedded libs | 108 | 3 710 | 2.0 | 0.5 | 0.27× | 102 | 6 |
| medley (src + test) | 104 | 22 661 | 14.9 | 1.5 | 0.10× | 551 | 23 |
| clojure-test-suite | 516 | 415 781 | 320.0 | 36.7 | 0.11× | 9 833 | 262 |
| all | 992 | 454 568 | 345.2 | 41.3 | 0.12× | 10 822 | 262 |

- The pass is a fraction of analysis everywhere, and the fraction falls as forms grow: analysis pays for
  macroexpansion and the facts pass does not, so core.clj's small forms are its worst case at 0.31×.
- Memory is 24 bytes per node plus one byte per frame slot; a table lives only as long as its form, so the
  "tables" column is the sum over a whole library and the peak is the "largest table" column.
- Nothing calls the pass: `clj_analyze`, `clj_exec_new` and the compiler are unchanged, so this cost is paid
  only by a caller of `clj_facts_of` (NOTES.md, "Facts").

## Compiler v0, promoted slots — aa26129, Apple M3 Pro, 36 GB, Swift 6.2.4 (pool only)

The "loops compiled `--closed`" column of the Compiler v0 table, re-measured before and after non-escaping
slots became C variables (NOTES.md "Compiler", promoted slots): the closed compiled-core binary with every
bench form compiled `CLJ_EVAL=compiled CLJ_EVAL_CLOSED=1 CLJ_EVAL_OPT=-O2`. Two runs per side, back to back
in one session; the pairs show the session's own spread. The first `counting loop` row is bimodal on both
sides (3.8–4.8 or 7.7–8.0) with byte-identical machine code in the form's dylib: it is the first hot row
after twelve fresh `dlopen`s, and the system's first-load scan of those images is still running (the
Compiler v0 notes on `syspolicyd`); the second `counting loop` row, the same source measured later, is the
number to read.

ns per iteration or element, `n` = 100000 unless noted.

| scenario | before | after |
|---|---:|---:|
| counting loop (first row) | 4.8 / 7.7 | 8.0 / 7.7 / 3.8 |
| counting loop (second row, same source) | 3.6 / 3.8 | 3.9 / 3.7 / 3.8 |
| loop accumulating into a local | 6.0 / 7.2 | 7.1 / 7.2 / 6.0 |
| closure call in a loop | 6.9 / 6.9 | 7.0 / 6.9 / 7.0 |
| C builtin call in a loop | 5.3 / 5.2 | 5.3 / 5.3 / 5.4 |
| let-bound fn called in a loop | 4.0 / 4.0 | 4.0 / 4.0 / 4.1 |
| loop with a local helper (direct fn) | 7.0 / 6.8 | 6.4 / 6.4 / 6.5 |
| protocol call, deftype receiver | 17.1 / 17.2 | 16.6 / 16.5 / 16.9 |
| protocol call, fixnum receiver | 17.9 / 17.9 | 17.6 / 17.7 / 18.0 |
| protocol call, bi-morphic | 21.5 / 21.4 | 21.6 / 21.0 / 21.9 |
| plain fn call through a var | 8.0 / 8.1 | 8.1 / 8.1 |
| multimethod, = hit | 105.7 / 109.7 | 106.9 / 106.7 |
| fused reduce: reduce + map inc range | 16.8 | 16.8 |
| transduce (map inc) + range | 11.9 / 11.9 | 11.9 / 11.9 |
| swap! inc | 22.4 / 22.7 | 22.6 / 22.3 |
| swap! assoc, map of 16 keys | 208.4 / 217.0 | 215.7 / 213.2 |

- **What moved**: the loop with a local helper, 7.0 → 6.4. Its frame is passed as the static link of the
  direct fn (`clj_c_outer(&fr, 0)`), so before, `&fr` escaping kept the whole slot array in memory and the loop
  variable `i` was a store and a load per iteration; now `i` is a C variable and only `acc`, which the helper
  reads through `OUTER`, stays in the array.
- **What did not**: the counting loop and the accumulating loop. Their generated C changed as intended
  (`clj_value l1 = CLJ_NIL; … clj_c_rebind(&l1, t7)` in place of `s[3]`, `fr.owned` and `clj_c_set`), but the
  `-O2` machine code is identical before and after: nothing took the frame's address, so clang's SROA had
  already split the array into registers and folded the owned-bit updates. What those loops pay is the boxed
  `<`, `inc` and `+` (a call each, with the fixnum tag checks), `clj_release` on the old fixnum, the deadline
  tick and `clj_c_enter`/`leave` per call — none of it slot traffic. The slot promotion is the precondition for
  unboxing those variables, not a speed-up on its own at `-O2`; at `-O0` (test builds, `-DCLJ_COMPILED_CORE`
  in debug) every promoted slot is a stack store and load fewer.
- **Census** (`clj-compile --stats`): core.clj 1813 frame slots, 22.9 % `local` by the facts pass, 83.4 %
  promoted; medley 1347 slots, 25.2 % `local`, 91.8 % promoted. The promoted share exceeds the `local` share
  because `escapes` (the value leaves the frame) does not bar a C variable; what bars it is a capture, a
  static-link read, a param a fn-body `recur` rebinds, a direct fn's param, a frame past 64 slots.
