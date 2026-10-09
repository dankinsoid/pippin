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
  - The allocation census (`--census` on both stats runs, stats.c): every object allocated in the two stats
    iterations gets a record by address (sharded table) with its birth frame — a census frame pushed by the
    interpreter's `run_body` and by every compiled body (`CLJC_ENTER`, popped by a cleanup on `cc` after the frame's
    teardown) — and is classified at its death (rc.c `bury`, or `clj_dealloc` for the collector and shape.c): died
    in the birth frame, deeper, 1/2/3+ frames up (the closest frame still running that held it all along), out of the
    outermost fn, in another execution, born outside any fn, or alive at the end. Beside that: a Perceus reuse pair
    (an allocation of the same size class in the frame of the death within the next 1 or 4 allocations), whether the
    count ever passed 1 (`CLJ_FLAG_RETAINED`, set by the stats build's retain), and whether it was born under a
    builder (`into`, the transient ops, `frequencies`, `group-by`, `zipmap`, `mapv`, `filterv`, by fn name). C
    builtins push no frame: what they allocate belongs to the calling fn. `--calibrate` also times the inline
    retain/release over 1M shuffled headers (`clj_debug_rc_op_ns_cold`), the miss the hot number leaves out.
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
  2. Thread-local access: done (bench/RESULTS.md "Cheap runtime"). The heap and the carrier are pthread keys read
     off the thread register (docs/notes/allocator.md), and dyld's `_tlv_get_addr` fell from 5–12.5 % to under 1 %.
     What is left is `clj_coro_current` itself, 2–5 % self, called once per realization by the lazy-seq
     claim/publish; passing the execution down from the caller that has it is the next step there.
  3. Lazy realization: the seqs bucket 6–18 %, `lazy forced` up to 3.28M an iteration (dependency, through `concat`
     and `mapcat` in the library code). Fusion reaches only `reduce`/`into` over a pipeline in the program; a library
     building its result with `concat` is outside it, and no work item in design §6b covers it.
  4. Runtime builtins with a single cause each: done (bench/RESULTS.md "Cheap runtime"). `clojure.string/join` and
     `escape` build one buffer in C (strings 511 → 85–88 ms, 15.7× → 2.5× the JVM); `clj_apply`'s arguments sit in
     one stack array up to `CLJ_FN_MAX_FIXED + 2`, so group-freq's system malloc left the profile (alloc/free
     28 → 14 %); its 3.15M `apply` calls an iteration, 7–8 % self in `clj_apply`, are `juxt`'s variadic arity.
  5. Generic calls and inline-cache misses: dispatch is 1–7 % self, protocol misses appear only in suite-data (60k).
     The closed build's call cuts did not show in time, so devirtualizing further ranks last among the measured.
  Not supported as a lever by these runs: hashing and equality (≤ 3.3 %, 650k hashes at most), boxing (doubles
  1.5M in pipelines, ≤ 0.2 % self), coroutine switches (the async workloads are 2–6× faster than the JVM, and their
  busy samples are idle carriers spinning in `carrier_main`). The inline RC ops are bracketed, not measured in place:
  0.85 ns on a cached header (1–9 % of the closed median) to 18.1 ns on a missed one, which would exceed the whole run
  (the census entry below). Unmeasured: how much of `bnode_copy`/`node_own` (collections, 4–31 %) reuse at rc 1 avoids — no counter counts reuse
  hits; the cost per generic call (`clj_c_invoke` is inlined into its callers); async-pipeline's closed build
  being slower than dev in all three runs; x86_64.
- **Where objects die against where they are born** (the allocation census, one `corpus-bench` run, bench/RESULTS.md
  "Allocation census"; % of allocations, the nine data workloads weighted by count, 47.2M an iteration). Every
  object of the data workloads dies inside the iteration and in the execution it was born in: nothing escapes for
  real (async-pipeline's channel traffic is the exception, 99.7 % dying in another coroutine). What each mechanism
  could take:
  - A frame region (stack allocation of what dies in its frame or below): 19.4 % (nested-update, strings and render
    43–59 %, the seq-heavy workloads 4–15 %; async-libs 65 %). By type it is `vector-seq`, `string`,
    `list` and the frame-local `vector`s.
  - An interprocedural region: the other 80.6 %, of which 33.3 % one frame up and 47.3 % two or more. Frames are fn
    bodies, core.clj's included, so "up 2" is typically the user fn above a `map`/`concat` that built the value; a
    region would have to cross the library. The 3+ bucket (up to 41 % in dependency) includes values that die near
    `run` itself, where a region is the whole iteration.
  - Drop-reuse (an allocation of the dying cell's size class in the same frame within 4 allocations): 11.4 %, 5.1 %
    as the very next allocation; concentrated in dependency's `cons` (82 % of them, the `concat` step), strings and
    render (`string`, 26–41 %) and nested-update (`map`, `vector-seq`, 18–21 %). Near zero for maps, map nodes, fns
    and lazy seqs.
  - Flat/linear representations (never retained: the count never passed 1): 21.8 % (6–48 % by workload). The
    persistent collections and the fn/lazy-seq pairs are 85–100 % retained, so a linearity-based in-place path covers
    mostly strings, doubles, `vector-seq`s and builder-local lists. Born under a builder (`into`, transients,
    `frequencies`, `group-by`, ...): 13.1 % (up to 52 % in medley); the map nodes of group-freq/suite-data are 70–86 %
    built.
  - The `fn` + `lazy-seq` pair (33–45 % of the allocations of dependency, pipelines, combinatorics) lives 2–3+ frames
    and is always retained: neither a frame region nor reuse takes it; removing the separate thunk object (the step
    already ranked first above) does.
  Method limits: frame identity is a fn body, not a `let` or a loop turn; C builtins and the fused drivers are not
  frames; reuse counts the next allocations of the execution, not of the frame, and ignores type; "retained" counts a
  borrow-then-release as sharing; one run on one runner. `drop-cheap`'s `clj_drop_dead` (not merged) bypasses `bury`:
  after it the census sees such a death only at `clj_dealloc`, with the flags already overwritten.
