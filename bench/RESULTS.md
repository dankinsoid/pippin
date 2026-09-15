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

