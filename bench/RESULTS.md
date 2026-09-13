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
