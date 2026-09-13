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
