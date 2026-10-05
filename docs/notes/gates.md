## Gates

- **The gates hold on arm64 and x86_64 Macs alike** (`docs/portability.md`). `port-audit` names every
  architecture-specific construct by file and function or macro, so a new one in a file already listed still
  needs its row; a gate passing on one architecture says nothing about the other's per-architecture rows.
- **Before every push, run `make gates`**: `test`, `test-compiled`, `corpus-compiled`, `facts-report`,
  `port-audit`, `c-only-audit`, `cmutex-audit`, `open-items-audit`, `api-diff`, in that order. `c-only-audit` runs one file
  through `clj-load`, the C-only host, to see a `catch` clause naming a host type refused out loud. The runner prints wall seconds and exit status per step,
  stops on the first failure, and prints the total on success. Even `make -j gates` keeps that order.
  Put JVM Clojure on PATH (`/opt/homebrew/bin` for Homebrew); `api-diff` also resolves the core.async and
  ClojureScript jars it dumps, so the first run of it needs the network and later ones the Maven cache (CI
  caches `~/.m2`, so only a cold cache fetches them). `TEST_TIMEOUT`, 500 seconds unless
  overridden, bounds every Makefile `swift test`: the shard runner bounds its build, its listing and each shard by it
  ("Shards"), the corpus targets go through `timeout -k 5`, which GNU coreutils supplies on macOS. Keep long
  runs in background logs. SwiftPM passes the test helper's output on in lumps (~16 KB, 64 KB with the XCTest
  pass every run now skips, NOTES "Guard"), and a run killed by the bound loses what it held. So a test still
  running after 300 s (`CLJ_TEST_HANG_S`) prints its name, `clj_debug_sched_dump` and a one-second `sample` of
  every thread to stderr and ends the process (exit 3): the exit delivers everything. Under the shard runner
  that is per shard, and the runner prints the report from the shard's log. CI sets 900 s: on one x86_64 runner
  every suite ran 3.4× slower than on another with the same deal (`MapTests.randomOpsMatchReference`, all
  in-process CPU, 82 s against 281 s; runs 37193827945 and 37193832212), and the one test of `SwiftStubTests`, 115 s
  on the fast runner, passed 300 s on the slow one. Slow, not stuck: its report showed the test thread in
  `SwiftStubs.generate` reading the generator's pipe, nothing on a lock or in `dlopen`; the suite was alone in
  its shard and is the only user of `.build/swift-stubs`. The stubs cache ("CI") makes that test 5 s on a hit,
  but a change to Pippin's sources misses it, and `MapTests.randomOpsMatchReference` itself took 281 s on the
  slow runner, so the bound stays. `test-eval-compiled` sets 3600 s, a clang run per eval.
  A failed shard's report also lists the shard's processes with their CPU time at the last sample, which tells a
  child still working from one that waits.
- **`make gates-full` adds `test-isolated` and `test-compiled-asan`.** Run it weekly and after changes to
  allocation/RC, boot, compiler emission, or suite initialization/lifetimes. `test-isolated` retains one
  process per suite: an incorrect live-object baseline can pass when another suite initialized it first.
  It is periodic because that startup cost repeats for every suite; the shard runner (`--isolated`) runs as
  many of those processes side by side as it would run shards. All live-object assertions also remain
  active in the ordinary full-suite and corpus runs.
- **A live-object baseline is taken with the runtime settled.** `clj_debug_runtime_settle` (sched.c) waits
  until no coroutine lives beyond the target, no timer with a context is pending or firing (a timeout's
  channel, a sleeper's waiter, a deadline's coroutine; the evacuation sweep holds none), no blocking-pool job is
  queued or running, the output writer is drained, and the live-object count holds still across 1 ms (a
  coroutine's count drops at its finalize, before its children are freed). `BootedTrait` settles before every
  test under `CoreTests`, `CoroBaseline` at both ends, the corpus around its second run. Without it a baseline
  counted whatever finished on another thread a moment later — a warm-up's joined go block still in its
  epilogue on a carrier, a `timeout` that lost an `alts!` firing later, a `thread` body releasing after it
  delivered — and the check found those objects gone (`CoroTests.everyCancellationIsCancelledType`,
  `ChanTests.altsWithDefaultTimeoutAndPriority`, `AsyncLibTests.aScopeCancellationCarriesItsCause` on CI). The
  wait is for work in flight, not for the count, so a leak still fails the check; it is bounded at 10 s and
  fails loudly, once — later settles only look, so one leaked coroutine does not stall every test after it into
  the run's bound. The failure prints `clj_debug_coro_dump` beside the scheduler's state: every coroutine that
  ever parked with the trace of where it is parked, which is what names the leftover (NOTES "Corpus": the
  core.async suite's abandoned `onto-chan!` fillers were found no other way). The runtime cannot see host
  threads: the nREPL server's connection threads hold its sessions
  until they see the client hang up, so `NReplTests` waits for the server to be freed (`overTheWire`) — a
  `NamespaceTests` baseline counted them otherwise. A test that starts host threads holding values joins them
  before it returns. A blocking-pool thread that retires frees nothing the count sees (its implicit coroutine
  and carrier are calloc'd, its heap is handed on), so the settle waits for jobs, not for threads.
- **The `test-compiled` hangs on CI were the blocking pool** (NOTES "Scheduler", "Blocking pool"). The report
  above named the test (`AsyncLibTests.pipelines`), and its coroutine dump showed the deadlock: both pool threads
  in `>!!`, the results loop parked on the channel a queued `thread` body would have fed. The first such hang
  (run 36913041719, `d3a3181`) printed nothing, but stopped inside the same range of suites.
- **Timing in tests.** Rule: a test waits for the condition it needs, bounded far past any runner, and keeps
  a fixed window only after that condition, where it checks that nothing happens — a slow runner then only
  lengthens the window. The waits are `test-support` (`TimingSupport.swift`): `await-true` and `join` poll
  with 1 ms timers each waited out (a longer timer would sit pending under the next baseline), `gated?` is a
  body on its suspension gate (`clj_debug_chan_gated`), `pending-takes`/`pending-puts` a parked channel
  operation, and `eventually` the Swift side; the bound is 10 s. A cost is the bench's to hold:
  `CoroTests.switchCost` bounds the fastest of 20 short batches at 2 µs, which tells the hand-written switch
  from a kernel round trip, and the 14 ns of record stays in bench/RESULTS.md. What CI found: `switchCost` at
  848, 360 and 231 ns against 200 (runs 37027460141, 37027935840, 37041175728);
  `suspendParksTheBodyAndResumeLetsItOn` (run 37121027548), whose 10 ms windows assumed the suspension landed
  and the body moved within each; `withDeadlineOverCoroutines` (run 37023946974), whose child of a 30 ms
  `with-deadline` needed a 2 ms timer and a turn inside the deadline. That one is the test, not the runtime:
  looped on a loaded machine with the deadline cut to 1–5 ms, the only outcome besides a pass is `[:timeout
  false true]`, a child that never ran; the join and the translation to `:timeout` held in every run. Its
  child records the kind of the cancellation it met (`:deadline`, its own timer's). Under 64 busy
  processes on 16 hardware threads the same test's shielded-exit scenario timed out its 10 s poll as well:
  `spend` takes over 20 s there, so that join is bounded at 60 s.
  A wait that ends a test's window is part of the test too: `FutureTests.futureCancel` cancelled seventy
  `thread` bodies on a gate and closed it at once, and two bodies answered `:slept` (run 37125363627, x86_64).
  Traced, each had read its flag clear, its cancel landed, and the close followed before its take, which then
  completed on the closed gate without a wait — the one point a cancel is not met (NOTES "Coroutines"). In a
  loop of the scenario, 1–3 iterations in every 3000–8000 lost bodies so; with the gate left open until every body
  answered, none in 10 000.
- **One build directory per configuration.** Plain tools/tests use `.build/plain`, interpreted ASan
  `.build/asan`, compiled core `.build/compiled`, compiled core ASan `.build/compiled-asan`, release tools
  `.build/release`, UBSan `.build/ubsan`, and no-reuse `.build/noreuse`. `BUILD_ROOT` can relocate them as a
  group. Release executables are inside `.build/release/release/`. `make boot` uses the plain compiler;
  `scripts/embed-core.sh` only writes embedded source bytes and has no build-directory dependency.
- **Shards.** `test`, `test-compiled`, `test-eval-compiled` and the other whole-suite targets run through
  `scripts/test-shards.py`: one `swift build --build-tests`, `swift test list`, then the suites dealt out longest
  first by `scripts/test-times.json` onto N shards, each a `swift test --skip-build` over its suites' tests, side
  by side. The live-object counters are per process, so the suite stays serialized inside a shard and no
  baseline sees another shard's objects. A suite is the unit: its tests may share state in order, and
  `test-isolated` proves every suite alone in a process. A shard names its tests by one anchored `--filter` each
  (a filter matching a suite's ID selects the whole suite). The run fails unless the shards' event streams
  (`--event-stream-output-path`) end every listed test exactly once, so a new suite cannot drop out: it is dealt
  like any other, at the median time until its time is recorded. A failing shard prints its issues, the tests
  that never ended (all a `TEST_TIMEOUT` kill leaves, since SwiftPM drops the log it held) and its log from the
  hang or sanitizer report on; every log stays at `<scratch>/shards/shard-N.log`. `TEST_SHARDS=N` sets the count,
  `TEST_SHARDS=1` is the serial run, and `swift test --filter` by hand is unchanged.
- **What bounds the shard count.** N = min(cores, 70% of memory / the gate's recorded peak resident memory per
  shard, ⌈sum of suite times / longest suite⌉). Past the last bound the longest suite alone sets the wall time
  and a shard only adds a boot and its memory. On the runners: `test` (ASan, ~2 GB a shard) gets 2 shards on
  arm64 (7 GB) and 4 on x86_64 (4 cores); `test-compiled` (≤0.4 GB) gets one per core, 3 and 4. A runner's cores
  are the tighter bound than the suites: shards slow each other down (x86_64 shards planned at 92 s took
  129–209 s), carriers and clang included. The peak is sampled once a second over the shard's process tree;
  `record` keeps the largest one of a run. Times are keyed by machine (`x86_64-4cpu`): suites run at different
  relative speeds on a runner and on a laptop.
- **Parallel `swift test` and SwiftPM.** `swift test` holds `<scratch>/.lock` for its whole run, so a second one
  on the same scratch path waits for the first. The shards pass `--ignore-lock`; `swift test --skip-build`
  still writes `build.db` while it plans, so a shard starts only once the one before it has handed over to its
  test process (its first event, ~1.3 s) and no two plan at once. swift-test starts the test process in a
  process group of its own, so a timed-out shard is killed as a process tree. `/usr/bin/python3` is an xcrun
  shim that sets `SDKROOT` when it is unset, and a build under another `SDKROOT` rebuilds everything, so the
  Makefile hands the runner its own `SDKROOT` (or none).
- **What shards share outside the process.** A suite runs in one shard, so only paths two suites write can
  collide. `CLJ_EVAL=compiled` names its units `form1`, `form2`, … per process in one directory, so shards would
  load each other's dylibs: each shard gets its own `CLJ_EVAL_DIR` (`<scratch>/shards/shard-N.eval`, or
  `shard-N` under a given one). Safe as they are: temp files (pid, random or UUID names in `FutureTests`,
  `EvacTests`, `NamespaceTests`, `ChanTests`, `CorpusCompilationCacheTests`); `.build/compiled-fixtures`
  (`fixture_<name>` distinct between `CompilerFixtureTests` and `SwiftStubTests`, `form<n>` only from
  `CompilerFixtureTests`' own compiled eval); `.build/swift-stubs` (the fixture module and every generator entry
  are built in a `.tmp-<pid>` directory and renamed into place, the loser discarding its copy); the corpus cache
  (a lock per library); the nREPL server (port 0). The corpus watchdog's budget is the one timing collision:
  `test-random-sample` under ASan beside other shards passes 5 s, hence the Makefile's 60 s ("CI").
- **A deal can fail a test the serial run passes.** Each shard is another history before each of its suites, so a
  live-object count that holds only after some other suite's allocations, or a layout they decide, fails in one
  deal and passes in the next; a deal is fixed by `scripts/test-times.json`, so it fails the same way every run.
  `RuntimeTests.defAndRedefinition` counted the meta map of a var it defined as one object, a shape map; after
  `QueueTests`' maps a shape on that path had become a dictionary (`CLJ_SHAPE_MAX_CHILDREN`, shape.c) and the meta
  map was a hash map, two objects (runs 37190622763–37191593504, x86_64). The test now declares the var before its
  baseline, as it does its other defs. A failed shard's report ends with its rerun in one process (`TEST_SUITES=…
  TEST_SHARDS=1 make test`; on CI, a dispatch with `-f target=test -f shards=1 -f suites=…`); halves of its suite
  list, dispatched side by side, find the pair, and `clj_debug_live_report` before and after the test names the
  type.
- **The push gate's ASan pass is `test`**, with interpreted core and `CLJ_SYSTEM_ALLOC=1`. It exercises
  the evaluator/analyzer and runtime allocation boundaries; the pool would hide individual object bounds
  from ASan. `test-compiled` runs the same suite with compiled core and the pool, checking emitted boot
  code, pool behavior and live counts. `corpus-compiled` still executes both corpora twice per mode and
  diffs fresh per-test reports. Compiled-core ASan catches memory errors specific to generated boot code;
  it is retained as `test-compiled-asan` in the periodic full gate, not treated as redundant coverage.
  Both ASan passes retain `--disable-xctest` for the discovery-helper issue in "Guard".
- **Corpus compilation cache.** Each library lives at `.build/corpus-cache/<library>/<SHA-256>/`.
  The length-framed key hashes the library tree (including manifest/refusal rules), the actual `clj-compile`
  executable, ordered compiler arguments, runtime headers, `jit.c` (the clang flags), `Package.swift`,
  clang identity and SDK path. Absolute input paths are included because generated descriptors contain
  them. A lock per library serializes writers; a completion record is published atomically only after
  every unit builds and loads. Interrupted entries or missing dylibs rebuild. Corrupt metadata or a
  failing `dlopen` fails the gate. Compiler stderr and exit status are cached and refusal checks run on
  hits too. A hit opens the existing dylibs at the same paths without invoking `clj-compile` or clang;
  it still registers every unit, runs both test passes, checks live objects and compares reports.
  `CLJ_COMPILE` and `CLJ_CORPUS_CACHE` override the tool and cache locations for direct harness runs.
  The Makefile exports `ZERO_AR_DATE=1`: ld64 otherwise writes every object's mtime into the executable's debug
  map, so each fresh build of `clj-compile` has other bytes and another key: without it every CI run misses for
  both libraries, the restored cache notwithstanding (runs 37134880311 to 37142137285). On a miss the
  units go through clang in parallel (`cljc_build_dylib`, one per core) and load in manifest order after; the
  first `dlopen` of a fresh dylib still costs ~0.5 s locally, so a miss is dominated by loading, not clang.
  Delete the cache to force recompilation. This cache is local executable build output, not a shared
  artifact trust boundary.
- **Measured, one machine, `2d73cd4` (before) vs this branch (after).** Cold clears every scratch path
  and the corpus cache first; warm reruns `make gates` right after. Seconds per gate, wall clock:

  | gate | before cold | before warm | after cold | after warm |
  |---|---:|---:|---:|---:|
  | test | 106 | 89 | 114 | 73 |
  | test-compiled | 158 | 149 | 75 | 44 |
  | corpus-compiled | 160 | 152 | 202 | 13 |
  | facts-report | 10 | 4 | 11 | 4 |
  | port-audit | 1 | 2 | 1 | 1 |
  | api-diff | 6 | 4 | 6 | 4 |
  | **total** | **441** | **400** | **409** | **139** |

  `test-compiled` before ran the pool pass and the compiled-core ASan pass in one shared scratch
  directory; after, it is the pool pass alone (the ASan pass moved to `test-compiled-asan`), so the two
  numbers cover different work, not the same work faster. `corpus-compiled` cold is slower than before:
  the cache pays for fingerprinting every library tree on a guaranteed miss. Warm is where the cache
  earns it back — 152s to 13s, all cache hits, no clang or dlopen-from-a-fresh-file cost. The other four
  gates are within run-to-run noise; separate scratch paths mean nothing in `gates` rebuilds anything
  another gate already built. Total: cold about the same (independent scratch paths cost a bit up front),
  warm about 3× faster, which is what a same-day second push pays.
- **Measured on CI, serial vs shards.** Seconds, median over the runs named; a gate's wall time, then its build
  and test phases (`Build complete`; the serial run's swift-testing total, the shards' listing and run). Before:
  six serial runs, 37134880311–37142137285, every corpus run a miss. After: the parallel series
  37196078768, 37196080394, 37196081831 (`da5b9da`, both architectures, caches cold for the commit), and on
  arm64 run 37198691370 with the corpus and stubs caches hit; x86_64 alone, run 37199233108, corpus cache hit.

  | gate | arm64 before | arm64 after | arm64 after, warm | x86_64 before | x86_64 after | x86_64 after, alone |
  |---|---:|---:|---:|---:|---:|---:|
  | test | 334 | 263 | 199 | 557 | 537 | 425 |
  | — build / tests | 105 / 214 | 108 / 155 | 81 / 109 | 193 / 328 | 228 / 234 | 250 / 152 |
  | test-compiled | 184 | 132 | 139 | 246 | 298 | 200 |
  | — build / tests | 75 / 105 | 90 / 50 | 86 / 51 | 115 / 120 | 172 / 74 | 138 / 49 |
  | corpus-compiled | 154 | 132 | 88 | 263 | 347 | 177 |
  | **gates total** | **694** | **572** | **459** | **1139** | **1267** | **873** |

  The test phases are what sharding buys: 214→155 s and 105→50 s on arm64 (2 and 3 shards), 328→234 s and
  120→74 s on x86_64 (4 shards). The x86_64 builds of the series ran 1.5× slower than serial runs' (`test-compiled`
  115→172 s, the same 340 build steps): three x86_64 jobs at once, and a runner in that series ran every suite
  3.4× slower than another; the x86_64 totals of a parallel series measure the runners as much as the gates.
  The stubs cache's hit on x86_64 (run 37200095254) cut `SwiftStubTests` to 10.6 s, against 115–227 s cold, on a
  runner whose builds were again 1.8× slow (gates total 1340 s). The last series (`03ec54d`: 37201631342 with
  both architectures, 37201633260 and 37201635398 arm64 alone; both caches hit): arm64 totals 470, 445 and 652 s
  (`test` 212, `test-compiled` 140, `corpus-compiled` 88 at the median), x86_64 918 s (`test` 380), its
  `SwiftStubTests` 6 s off the stub entry another clean x86_64 build saved (run 37199233108).
  What is left: builds, about half of every job; `corpus-compiled` on a miss, its first `dlopen` of each fresh
  unit (the clang over the units now runs in parallel); on arm64 the memory bound (2 ASan shards of ~2 GB).
- **CI.** `.github/workflows/gates.yml` runs `make gates` on push to main, on pull requests, nightly and by
  hand; a push or pull request that touches only `docs/` and Markdown files does not start it. One job per
  architecture: `macos-26` (arm64, 3 cores, 7 GB) on every trigger, `macos-26-intel` (x86_64, 4 cores, 14 GB)
  only nightly (`schedule`, 23:00 UTC, on main) and on a dispatch with `arches: both`
  (`gh workflow run gates.yml --ref <branch> -f arches=both`): its job takes about twice arm64's. **A change to
  a per-architecture spot of docs/portability.md (asm, `CLJC_SITE`, the struct ABI) dispatches `both` before
  it merges.** Both jobs are required where they run; a matrix entry's `required: false` would turn its job
  back into `continue-on-error`. The x86_64 job became required after consecutive green dispatches on both
  runners (runs 36989106164, 36992908023, 36995366088; about 12 min arm64, 24 min x86_64). Both select Xcode 26.6 explicitly; Homebrew coreutils and the pinned Clojure CLI
  are installed per run, the image's JDK 21 runs it. `TEST_TIMEOUT=1200`: the runners are several times
  slower than the M3 the table above was measured on. The Makefile exports `CLJ_CORPUS_TIMEOUT_MS=60000` for the
  same reason, everywhere: clojure-test-suite's `test-random-sample` takes 4–6 s under ASan alone on a runner,
  and 20.4 s beside three other ASan shards on the x86_64 one (run 37188956209), against the 5 s default
  ("Corpus").
  A push or pull request cancels its ref's run still in progress; a manual dispatch or a nightly run is a
  concurrency group of its own, so **a CI series is dispatched all at once** (`for i in 1 2 3; do gh workflow
  run gates.yml --ref <branch>; done`) and its runs go side by side. The plan runs five macOS jobs at once
  across the repository, so a series runs fully in parallel only when CI is otherwise idle: in runs
  37188954183–37188958054 every run started at once, but two x86_64 jobs waited 13 and 15 min for a runner
  behind other branches' runs. Every run uploads `.build/*/shards/` (each shard's log and event
  stream, and `times.json`) as the artifact `shards-<arch>`.
  Cached: `~/.m2` (api-diff's jars), the SwiftPM
  repository cache, and `.build/corpus-cache`, restored from the newest entry of the architecture and
  pruned to one key per library before saving; its content keys make a stale entry a miss, never a wrong
  hit. `.build/swift-stubs` the same way: the fixture module keyed by its source and `swiftc --version`, each
  stub module by the generator's fingerprint (the module's and Pippin's `.swiftmodule` bytes and paths, the
  module maps, both generator scripts, compiler, target and SDK), so a stub built against another Pippin never
  loads. Two clean CI builds of one commit gave the same keys (runs 37197333475, 37197805426); test and
  test-compiled have one each (Pippin's path differs), so four per module survive the prune. Cold,
  `SwiftStubTests.theFixtureRunsAlikeInterpretedAndCompiled` spent, alone on arm64: 3.1 s on the fixture's
  `swiftc`, 36 s on the generator, of it 34 s in `swift-symbolgraph-extract` building the SDK's module cache
  (0.4 s once warm, as in the same job's test-compiled), 0.7 s on the stubs' `swiftc`, and 2.5 s on the
  Clojure side (the interpreter, two compiled units); 115–227 s on x86_64 beside three ASan shards. `CDeclTests`
  (level 0) needs nothing a cold machine does not already have — python3 and the toolchain's own clang, through
  `-Xclang -ast-dump=json`, with no libclang and no Python bindings — and its parse of AppKit is about 3 s cold,
  0.13 s on a cache hit; its store is `.build/c-decls`, not cached in CI, so every run pays the cold parse once.
  Scratch paths are not cached: several GB per architecture, and whether SwiftPM reuses them after
  a fresh checkout (new mtimes and inodes on every source) is unmeasured. No benchmarks run in CI.
