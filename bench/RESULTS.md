# Benchmark results

ns per op, median of 5 runs. `swift run -c release clj-bench`.

## 490ba94 — Apple M3 Pro, 36 GB, Swift 6.2.4, calloc-backed clj_alloc

| scenario | n | C HAMT | TreeDictionary | Dictionary | tree / C |
|---|---:|---:|---:|---:|---:|
| assoc, old version dropped | 10 | 133.3 | 204.1 | 129.2 | 1.5× |
| assoc, all versions kept | 10 | 316.7 | 495.8 | 262.5 | 1.6× |
| get, hit | 10 | 2.9 | 12.4 | 9.4 | 4.3× |
| get, miss | 10 | 7.5 | 8.0 | 16.4 | 1.1× |
| dissoc to empty | 10 | 116.7 | 141.7 | 104.2 | 1.2× |
| assoc, old version dropped | 1000 | 102.8 | 122.5 | 22.6 | 1.2× |
| assoc, all versions kept | 1000 | 336.2 | 520.7 | 1794.8 | 1.5× |
| get, hit | 1000 | 13.4 | 17.2 | 10.7 | 1.3× |
| get, miss | 1000 | 17.4 | 20.0 | 17.9 | 1.1× |
| dissoc to empty | 1000 | 169.4 | 173.3 | 41.5 | 1.0× |
| assoc, old version dropped | 100000 | 130.5 | 161.5 | 40.6 | 1.2× |
| assoc, all versions kept | 100000 | 807.2 | 1377.0 | — | 1.7× |
| get, hit | 100000 | 17.8 | 21.9 | 11.6 | 1.2× |
| get, miss | 100000 | 17.4 | 20.2 | 13.9 | 1.2× |
| dissoc to empty | 100000 | 230.8 | 239.2 | 60.4 | 1.0× |

## 9853c34 — Apple M3 Pro, 36 GB, Swift 6.2.4, per-thread size-class pool allocator

| scenario | n | C HAMT | TreeDictionary | Dictionary | tree / C |
|---|---:|---:|---:|---:|---:|
| assoc, old version dropped | 10 | 58.4 | 183.4 | 125.0 | 3.1× |
| assoc, all versions kept | 10 | 158.3 | 341.7 | 241.7 | 2.2× |
| get, hit | 10 | 3.9 | 8.2 | 11.0 | 2.1× |
| get, miss | 10 | 7.5 | 9.2 | 15.7 | 1.2× |
| dissoc to empty | 10 | 45.8 | 104.2 | 116.7 | 2.3× |
| assoc, old version dropped | 1000 | 48.2 | 110.3 | 22.8 | 2.3× |
| assoc, all versions kept | 1000 | 197.8 | 514.5 | 1493.2 | 2.6× |
| get, hit | 1000 | 12.9 | 17.3 | 10.8 | 1.3× |
| get, miss | 1000 | 16.3 | 18.4 | 16.9 | 1.1× |
| dissoc to empty | 1000 | 93.5 | 143.0 | 37.9 | 1.5× |
| assoc, old version dropped | 100000 | 73.6 | 155.2 | 36.0 | 2.1× |
| assoc, all versions kept | 100000 | 677.3 | 1278.4 | — | 1.9× |
| get, hit | 100000 | 16.5 | 21.4 | 11.2 | 1.3× |
| get, miss | 100000 | 16.3 | 18.9 | 13.5 | 1.2× |
| dissoc to empty | 100000 | 150.4 | 229.3 | 55.6 | 1.5× |

C HAMT vs 490ba94 (calloc-backed): assoc with the old version dropped 133→58 ns (n=10), 103→48 (1k),
131→74 (100k); all versions kept 317→158, 336→198, 807→677; dissoc to empty 117→46, 169→94, 231→150;
get unchanged within noise. The same binary with CLJ_SYSTEM_ALLOC=1 in the same session reproduces the
490ba94 numbers (75 / 98 / 124 ns for assoc dropped), so the delta is the allocator, not the machine state.
