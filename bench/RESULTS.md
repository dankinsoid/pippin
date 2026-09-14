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
