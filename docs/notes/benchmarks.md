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
  Round 1 of the side that runs first reads warm-up on that runner; compare rounds 2 and on. The base worktree
  comes from `scripts/base-tree.sh`, which `corpus-bench --base` shares; an `origin/` prefix is stripped for the fetch.
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
  neither gate: a CI dispatch with `target: corpus-bench` (on the arm64 runner about 20 min for one tree plus 10 for the reference runtimes, 79 with the default base; the report is the job
  summary, the logs and `sample` files the `corpus-bench-arm64` artifact), `CORPUS_BENCH_ARGS=--only=a,b` narrows it.
  `--interp-ab=VAR` adds a stats build of the interpreter and runs the interpreter and its counters twice, the second
  time with `VAR=1` in the environment (a switch the runtime or `clj-corpus-bench` reads, `CLJ_BENCH_NO_LAZY_INLINE`
  the first), into an "Interpreter A/B" table: the stats runs below are compiled code, so this is the only place an
  interpreter-side change shows in the counters.
  What each number is:
  - **A change is timed with `--base=<ref>`, never by two runs.** Two dispatches land on two runners and the
    runner moves every time column by more than a change does (JVM warm medley 31.9 against 63.7 ms in one pair;
    the dev column followed the runner in all three `dev-inline` pairs). `--base` (`CORPUS_BENCH_BASE=<ref>`; the
    workflow's `base` input, default `origin/main`, empty for one tree) builds the ref in a worktree
    (`.build/corpus-bench/base-src`, removed at the end) with this tree's `clj-corpus-bench` harness, or its own when
    this one does not build there, and gives it its own builds (`base-*` beside each scratch directory, its own
    clj-compile, dev units and closed tree; `CLJ_EVAL_ROOT` is the base tree, whose headers the units compile
    against). Both sides run this tree's workload sources. Per workload, after both sides' builds, `--reps` repetitions
    (8 by default with a base) each run interp, core+interp, dev and closed on both sides back to back, the base
    first in even repetitions and the head first in odd ones; then both sides' dev and closed stats runs; the
    profile, census, calibration and facts stay the head's, and the JVM runs once. The report's "Same runner"
    section has, per workload and mode, the median over the pairs of head/base of each run's fastest iteration, the
    sign test's interval for that median in brackets (the 2nd and 7th of 8 ratios, 93 % coverage) and `*` where the
    interval excludes 1, a mark the A/A runs put on 4 and 2 of 48 cells; a geomean row over the workloads; both
    sides' ms (median of the runs' fastest and of their medians); and the dev and closed counters of the head with
    their change against the base. *same result* covers
    the base's runs. The fastest iteration and not the median: over two A/A runs the pairs of run medians gave an
    interval 14 and 13 % wide (median over the 36 data cells), the fastest iterations 8 and 11 %, and 3 and 1 of the
    36 excluded 1, about the 7 % the interval promises. What that leaves resolvable (bench/RESULTS.md "Same-runner
    A/A"): a data workload's cell moves up to ±4–7 % with nothing changed, a mode's geomean ±2 %; a smaller claim
    needs more `--reps`, with `--only` to keep them inside the 150 min. Cost on the arm64 runner: 67–70 min for the step against about 20 for
    one tree (7–9 min of builds before the workloads, the workloads 59–61; a dev run recompiles its units with clang,
    about 3 s of each). The async workloads' counters move between two runs of one tree (scheduling); the others'
    are exact, so any counter change there is the head's.
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
    builtins push no frame: what they allocate belongs to the calling fn. Per (birth fn, type) the frame region —
    died in the frame or in a callee — is counted too (`census_site`, a lock-free table keyed by the census frame's fn
    node), and the report lists each workload's top 8 as `name@line:col`: what to replace is chosen by where it is born. `--calibrate` also times the inline
    retain/release over 1M shuffled headers (`clj_debug_rc_op_ns_cold`), the miss the hot number leaves out.
  - The facts coverage is `clj-facts` with `bench/workloads` as an extra root after the corpus (its row in the
    report), and `clj-compile --closed --stats` of each program (unboxed and tag-checked arithmetic, protocol sites,
    workers).
- **Reference runtimes (`--refs`; `scripts/corpus_refs.py`, `bench/workloads/cljs`).** Beside the JVM, each workload
  runs once per job (not per `--base` pair) on babashka, the reference for *interp*, and as ClojureScript on Node and
  on Hermes, React Native's engine, which is what this runtime competes with on an iPhone, and the hand-ported Swift
  below. `--refs=jvm,bb,cljs-node,cljs-hermes,swift` is the default; a comma list narrows it (`CORPUS_BENCH_ARGS=--refs=jvm`), and `--skip-jvm` drops `jvm`. The report's
  "References" table has each one's warm, 1st and cold, `closed/bb`, `interp/bb`, `closed/node`, `closed/hermes`
  (ours over theirs), its `=` verdict against `expected`, and below it every printed result that differs from the JVM's.
  - **bb** runs `bench/workloads/jvm.clj` and the sources unchanged (`bb -cp <the load path>`), core.async included:
    all twelve return `expected`.
  - **ClojureScript** is `cljs.build.api` 1.12.145 (`bench/workloads/cljs/build.clj`), `:advanced`, `:language-out
    :es5`, one bundle per workload as the closed build is one binary per workload, built before the workloads and
    outside every time (about a minute for all). The sources are copied to `.cljs` unchanged, except what
    `bench/workloads/cljs/src` holds: `workloads.util`, whose generator multiplies with `Math/imul` (the double
    product passes 2^53 and loses the low bits the mask keeps; the states are the JVM's), and async-pipeline and
    async-broadcast, whose `run` returns the channel of the result where the JVM blocks with `<!!`, which one thread
    has not; the harness (`bench/harness.cljs`, jvm.clj's lines) times such a run to the delivery. async-libs is n/a:
    turbine runs its routes on threads. The bundle wraps itself in `.call(globalThis)` (Closure's `goog.global` is
    `this`, module.exports under Node) and reads Node's argv through `globalThis.process`: the browser target defines
    its own `process` for `NODE_ENV`.
  - **Hermes** is the last CLI release, v0.13.0 (engine 0.12.0, bytecode 96, React Native 0.75's), running bytecode
    from `hermesc -O -emit-binary` as a release app does. The CLI takes no script arguments, so the mode is a global
    prepended to the bundle before `hermesc`; it has `print`, `setTimeout` and `setImmediate` but no `console` and no
    `performance.now`, so its clock is `Date.now` (1 ms) and the report names the cells timed by it.
  - Iterations: bb (jvm.clj itself) and Node (V8 tiers like C2) take the JVM's policy, >= 5 iterations and 5 s of
    warm-up then >= 5 and >= 3 s; Hermes has no JIT and takes ours, one iteration then >= 3 and >= 2 s (`nojit`).
  - Numerics: `=` cannot see a JS number's long/double ambiguity, so the printed results are compared too. pipelines'
    two double sums print `22499925000` and `14999950000` where the JVM prints `2.2499925E10` and `1.499995E10`; no
    workload has a string outside the BMP or a product past 2^53 other than the generator's.
  - Cost: about 10 min of the step on the arm64 runner (bb 2.6, Node 1.7, Hermes 4.3, the build 0.9; bench/RESULTS.md
    "References"), the step 78.6 min with the default base against 67–70 without them: inside the 150 min, so the
    default runs all four.
  - CI: `scripts/corpus-refs-install.sh` fetches babashka 1.13.226 and the Hermes CLI, checked against pinned
    sha256s, into `~/.cache/corpus-bench-refs`, cached with the ClojureScript jars (`CORPUS_BENCH_M2`); Node
    24.21.0 is `actions/setup-node`.
  - [ ] Hermes newer than React Native 0.75's: 0.76 and on ship Hermes inside React Native without a CLI, and Hermes
    V1 (static Hermes) is a source build of `facebook/hermes` at the tag React Native pins (CMake and Ninja, the
    `hermes` target, cacheable by the tag). Trigger: a decision that rests on `closed/hermes`.
- **Hand-ported references (`swift`, `dart`): the fairness contract.** Beside the references that run the workloads'
  own sources, the nine data workloads are hand-ported to the languages iPhone apps are written in, to answer how
  much a program loses against what a native developer of that platform writes. Every port keeps to:
  - the same algorithm, inputs and output: the same deterministic generator with its integer arithmetic matched
    exactly (wrapping where the JVM wraps), and the printed result compared with the workload's `expected` literal,
    the JVM's `pr-str`, so *same result* is a string equality (doubles printed as Java prints them);
  - the platform's idiomatic data structures, expected rather than tolerated: value types, structs and enums, the
    standard collections and string APIs as a developer of the platform uses them. No algorithmic shortcut the source
    lacks: nothing cached across iterations or precomputed, no work skipped, each iteration building its data from
    scratch as `run` does;
  - written as a competent app developer writes it, not micro-optimized: no unsafe pointers, no manual memory
    tricks. Where the platform has two common APIs for one operation, the faster, as a developer who profiled once
    would pick. Every judgment call is named per workload below;
  - the async workloads are n/a: structured concurrency, actors or isolates would be a different program from
    core.async's channels and go blocks;
  - timing is our AOT policy (one iteration, then >= 3 and >= 2 s, at most 30), each iteration's printed result
    compared with the first outside the timed span, which also keeps the work live; *1st* and *cold* as for the
    other references.
  - **swift** (`scripts/corpus_swift.py`, `bench/workloads/swift`): one binary per workload from `swiftc -O -wmo`
    (a release build: optimized, whole-module), the harness in `harness/` and the port in `workloads/<name>.swift`,
    built before the workloads and outside every time; its own report table, "Idiomatic Swift", with `closed/swift`
    and `interp/swift`. Cost: 0.3 min of builds and 0.2 min of runs on the arm64 runner, on by default (bench/RESULTS.md
    "Idiomatic Swift"). Judgment calls:
    - all: Clojure's `count` and `subs` of a string are Java's UTF-16 length and indexes, so the ports count
      `utf16.count` (O(1) on an ASCII native string) and slice by UTF-16 offsets; `count` would walk graphemes.
    - medley: a `Record` struct; `#{:a :b}` and `#{:c}` are one shared `Set` each, as the literal is one value;
      deep-merge is a struct merge (`:n` overwritten, `:tags` merged), update-existing-in an optional-chained `+=`.
      The source calls greatest-by with the seq as its only argument, which returns it unrealized: `(:score best)`
      is nil, and neither side does work for it.
    - combinatorics: the library's algorithms for the cases the workload calls (lex permutations, index-combinations,
      algorithm H with `:min`/`:max`, the odometer cartesian-product, nth-permutation by factorial digits,
      nth-combination-distinct) as iterators, one array per element. The library's type dispatch (`sorted-numbers?`,
      `all-different?`) is not ported; partitions builds each partition's groups, which the library leaves as lazy
      seqs that `count` never realizes.
    - dependency: a value-type graph with mutating methods; `depend` throws, its circularity check computing the full
      transitive closure as the source does. The closure's worklist is a stack where the source appends to a lazy
      `concat` (the same set, the same pushes); topo-sort pops its todo set in place where the source rebuilds it with
      `(set more)`; remove-all rebuilds both maps as the source does.
    - nested-update: the state as structs; a key the source reads through `fnil`, a `get-in` default or `keep` then
      `+` is a field defaulting to 0 or `[]`; the 100-entry log drops its head with `removeFirst` (a shift of 99)
      where the source takes a `subvec`.
    - group-freq: a `Person` struct, a `Hashable` struct for the (city, decade) key, `Dictionary(grouping:by:)` for
      group-by; merge-with over one-entry maps is `+=` into one dictionary; the sorted maps of the result are
      dictionaries sorted when printed.
    - pipelines: the lazy seqs and the transducers are `.lazy` chains (no intermediate array, as neither
      materializes one), partition-all's partitions `ArraySlice`s, the `{:k :v}` maps a struct; dedupe and
      reductions are loops, which the standard library lacks.
    - strings: `split(separator:)`, `firstRange(of:)` for index-of, Foundation's `trimmingCharacters(in:)`, and
      Foundation's `replacingOccurrences` for both the string and the regex replace: the standard library's
      `replacing` and Swift Regex took 5× and 10× as long in a one-off run on the Intel dev Mac (37 and 152 ms of a
      309 ms iteration). The growing string appends in place where Clojure's `str` copies.
    - render: hiccup as an indirect enum, attributes a `[String: String]` sorted at render; each node renders to its
      own string, which its parent concatenates, as the source does (rendering into one `inout String` would be
      another algorithm); escape walks `Character`s.
    - suite-data: a protocol over three structs held as `[any Shape]`; the field-by-name reads a type switch,
      `record?`/`satisfies?` a dynamic `is any Shape` on the boxed value, the multimethod an enum switch; the sorted
      map a `Dictionary` whose keys are sorted once at the end (the standard library has no sorted map); flatten a
      recursive enum walk.
- [ ] **Where the compiled program's time goes, ranked** (three `corpus-bench` runs, bench/RESULTS.md "Corpus
  workloads"; the shares are self time of the closed build on the nine data workloads, the counters per iteration).
  The program's own compiled code is 0.7–4.5 % of the samples and the compiled core 3–11 %; the rest is the runtime
  under them, which is why compiling the program buys 0–30 % over the compiled core and the closed world nothing
  measurable. Dev and closed perform the same RC ops and allocations to the unit in all twelve workloads; the closed
  build cuts generic calls by 2–83 % (nested-update: `clj_invoke` 2.61M → 0.44M) without a time change past the
  runner's spread. In order of what the evidence says would move:
  1. Allocation and its free: 38–51 % of the samples, strings aside; 1.2–14.6M objects an iteration. Done
     (bench/RESULTS.md "Drop by type, unzeroed cells, inline lazy-seq thunks"): the teardown is specialized by type
     (docs/notes/rc.md), freeing 13–39 % → 9–28 %; the hot constructors take unzeroed cells, `memset` 1.6–4.8 % →
     0.4–1.1 %; a compiled `lazy-seq` site needs no fn object, dependency 14.55M → 11.26M allocations and pipelines
     12.45M → 10.42M. What is left of it is the per-child decrement inside the drops (`bnode_drop` 7–14 % on the
     map workloads), `pool_alloc`/`clj_dealloc` per object, and the copies reuse at rc 1 does not take (below).
     Open: the interpreter's `lazy-seq` thunk (docs/notes/compiler.md) and the size-class hand-off, which the census
     bounds at 5.1 % of allocations (docs/notes/allocator.md); the inline retain/release on top is a modelled 5–13 %.
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
  (the census entry below). Reuse at rc 1 is counted (`reuse_type`, the RESULTS section above): nested-update
  and dependency reuse most of what they ask about, medley, group-freq, pipelines and suite-data copy 65–100 % of
  their map and sorted nodes, the source of `bnode_copy`/`node_own`. Unmeasured: the cost per generic call (`clj_c_invoke` is inlined into its callers); async-pipeline's closed build
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
