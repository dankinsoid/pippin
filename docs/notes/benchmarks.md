## Benchmarks (bench/)

- Numbers drift between sessions (thermal, background load). Compare only within one run; use
  `CLJ_SYSTEM_ALLOC=1` on the same binary as the control.
- A cost is held here, not in a gate: the gates run under ASan on shared runners. The context-switch row
  (`clj_bench_switch_ns`, 14 ns on the M3) is the switch's number; `CoroTests.switchCost` only bounds it
  loosely (gates.md, "Timing in tests").
- The "C iterator" number (3.8 ns/element) moves to 4.3 with identical machine code when the linker
  places `clj_seq_iter_next`/`clj_vector_nth` differently; `aligned(64)` on both brings it back.
  Compare that row across builds only with the alignment forced, or read it as ±0.5 ns.
- The same for the call rows of the compiled-core binary, at ±1.5 ns: a change to facts.c alone moved every
  protocol-call row 7.7 → 9.1 with byte-identical compiled-eval C (`CLJ_EVAL_KEEP=1` keeps it for the diff), and
  `-Xcc -falign-functions=64` on both sides took the gap to 0.3 (bench/RESULTS.md, "The self-recursive worker").
  A CljCore change that shows less than that on a call row it does not touch is layout until the aligned build says
  otherwise.
- **`make bench-ab` compares two revisions in one job** (`scripts/bench-ab.sh`): release `clj-bench` of `BASE`
  (default `main`) against the working tree, alternated for `ROUNDS` rounds over the `ONLY` selectors, both sides
  built with this tree's harness; a block fenced `bench-ab: head only { … }` is cut from the base's copy. On CI it
  is a `workflow_dispatch` with `target: bench-ab` and `make_args: BASE=<full sha>` (a short sha does not fetch;
  `arches: arm64` is the M1 runner, 3 cpus, virtualized): two
  separate runs land on two runners, and the runner-to-runner spread is larger than the effects worth measuring.
  Round 1 of the side that runs first reads warm-up on that runner; compare rounds 2 and on.
- [ ] Not yet measured: multi-threaded reads of a shared map, assoc from a shared base across threads,
  cross-thread free, cost of `clj_share` on a large graph, forcing one shared lazy seq from many
  threads (the CAS claim path).

- **Corpus workloads (`make corpus-bench`; `bench/workloads`, `scripts/corpus-bench.py`, `clj-corpus-bench`).** Eleven
  programs over the corpus libraries and plain data work, one source for JVM Clojure 1.12.6 with core.async 1.6.681
  (`bench/workloads/jvm.clj`) and for this runtime. Each file's header states its input, its work and its output, and
  its `expected` is what `run` returns on the JVM, checked with `=` in every runtime (`hash` is ours by design, so no
  workload returns one): the spec a port to another language starts from. A run past 240 s is sampled into the logs
  and killed, the workload's remaining runs skipped, so a hang fails the target and names itself; the whole target
  stops starting workloads after 150 min and the CI step is capped at 180. Opt-in, in
  neither gate: a CI dispatch with `target: corpus-bench` (about 20 min on the arm64 runner; the report is the job
  summary, the logs and `sample` files the `corpus-bench-arm64` artifact), `CORPUS_BENCH_ARGS=--only=a,b` narrows it.
  What each number is:
  - Times are in-process per iteration of `run`: ours after one iteration, median of >= 3 and >= 2 s; the JVM's after
    >= 5 iterations and 5 s, median of >= 5 and >= 3 s. One runner's numbers spread ±20–30 % from the next's: read
    a backend's column against another only over several runs (bench/RESULTS.md, "Corpus workloads"). *JVM cold*
    is a whole `clojure -M` process running `run` once, the classpath resolved beforehand. Five builds: the
    interpreter, the compiled core with the workload interpreted, dev units (clj-compile, clang -O2, dlopen) over
    the compiled core, the whole program `--closed` (clj-compile `--core --closed`, built as `clj-corpus-bench` from a copy of the tree, as `make shake` does).
  - Counters come from a `-DCLJ_STATS=1` build of the dev and the closed variant (`clj/stats.h`; relaxed atomic adds,
    so the stats build is slower and its times are not the table's): retains and releases by path (the debug
    counters, kept), allocations by type and bytes, frees, `clj_invoke`, compiled generic sites, `apply`, protocol
    methods called as values, compiled inline-cache misses, lazy-seq realizations, heap hashing and equality, coroutine
    switches. The RC share is a model: plain ops × `clj_debug_rc_op_ns` (an inline op on a cached header, measured on
    the same runner) over the closed median, a floor.
  - The profile is `/usr/bin/sample` for 8 s over a closed and a dev run, self time per function, attributed to a
    source file through `nm` over the build's object files and bucketed (rc, alloc/free, dispatch, seqs, hash and
    equality, collections, compiled core, compiled program, ...); blocked threads are left out. An inline helper — the
    RC fast path, `clj_c_invoke`, a tag check — counts in its caller, which is why the RC row needs the model.
  - The facts coverage is `clj-facts` with `bench/workloads` as an extra root after the corpus (its row in the
    report), and `clj-compile --closed --stats` of each program (unboxed and tag-checked arithmetic, protocol sites,
    workers).
- [ ] **Where the compiled program's time goes, ranked** (three `corpus-bench` runs, bench/RESULTS.md "Corpus
  workloads"; the shares are self time of the closed build on the nine data workloads, the counters per iteration).
  The program's own compiled code is 0.7–4.5 % of the samples and the compiled core 3–11 %; the rest is the runtime
  under them, which is why compiling the program buys 0–30 % over the compiled core and the closed world nothing
  measurable. Dev and closed perform the same RC ops and allocations to the unit in all twelve workloads; the closed
  build cuts generic calls by 2–83 % (nested-update: `clj_invoke` 2.61M → 0.44M) without a time change past the
  runner's spread. In order of what the evidence says would move:
  1. Allocation and its free: 38–51 % of the samples, strings aside. The free cascade (rc.c: `release_child`
     through `each_child`, `release_reaches_zero`, `bury`) 12–29 %, alloc.c and system malloc 9–28 %, `pool_alloc`'s
     zeroing 3–8 %, and two thirds of the TLS row below; 1.2–14.6M objects an iteration. The inline retain/release on top is a modelled
     5–13 % (plain ops × 1.35 ns), not a sample. The compiler step with counter support: a `lazy-seq` whose thunk is
     not a separate fn object (code pointer and captures in the lazy-seq itself) — fn and lazy-seq allocations pair
     one to one (dependency 3.29M/3.29M, pipelines 2.03M/2.03M, combinatorics 983k/923k), so it removes 16–23 % of
     those workloads' allocations with their frees and RC. Then reuse of a dying cell for the next allocation of its
     size class and a free specialized by type in compiled code (Perceus' reuse and drop), aimed at the cascade.
  2. Thread-local access, a runtime fix: dyld's `_tlv_get_addr` is 5–12.5 %, called per object by `pool_alloc` and
     `clj_dealloc` (`tls_heap`) and per realization by `clj_coro_current` from the lazy-seq claim/publish. Caching
     the heap and the coroutine where the caller already has them, or a direct thread-register read, is a
     portability spot (docs/portability.md).
  3. Lazy realization: the seqs bucket 6–18 %, `lazy forced` up to 3.28M an iteration (dependency, through `concat`
     and `mapcat` in the library code). Fusion reaches only `reduce`/`into` over a pipeline in the program; a library
     building its result with `concat` is outside it, and no work item in design §6b covers it.
  4. Runtime builtins with a single cause each: `clojure.string/join` is quadratic (`(str sb sep x)` per element,
     5.4 GB copied an iteration, strings at 16× the JVM, 61 % `memmove` and 21 % `madvise`); `clj_apply` mallocs and
     frees two argument arrays per call (group-freq: 3.15M calls, ≈ 20 % of its samples in system malloc).
  5. Generic calls and inline-cache misses: dispatch is 1–7 % self, protocol misses appear only in suite-data (60k).
     The closed build's call cuts did not show in time, so devirtualizing further ranks last among the measured.
  Not supported as a lever by these runs: hashing and equality (≤ 3.3 %, 650k hashes at most), boxing (doubles
  1.5M in pipelines, ≤ 0.2 % self), coroutine switches (the async workloads are 2–6× faster than the JVM, and their
  busy samples are idle carriers spinning in `carrier_main`). Unmeasured: the cost of the inline RC ops beyond the
  model; how much of `bnode_copy`/`node_own` (collections, 4–31 %) reuse at rc 1 avoids — no counter counts reuse
  hits; the cost per generic call (`clj_c_invoke` is inlined into its callers); async-pipeline's closed build
  being slower than dev in all three runs; x86_64.
