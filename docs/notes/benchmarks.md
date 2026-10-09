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
  neither gate: a CI dispatch with `target: corpus-bench` (about an hour on the arm64 runner; the report is the job
  summary, the logs and `sample` files the `corpus-bench-arm64` artifact), `CORPUS_BENCH_ARGS=--only=a,b` narrows it.
  What each number is:
  - Times are in-process per iteration of `run`: ours after one iteration, median of >= 5 and >= 3 s; the JVM's after
    >= 10 iterations and 10 s, median of >= 10 and >= 5 s. *JVM cold* is a whole `clojure -M` process running `run`
    once, the classpath resolved beforehand. Five builds: the interpreter, the compiled core with the workload
    interpreted, dev units (clj-compile, clang -O2, dlopen) over the compiled core, the whole program `--closed`
    (clj-compile `--core --closed`, built as `clj-corpus-bench` from a copy of the tree, as `make shake` does).
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
