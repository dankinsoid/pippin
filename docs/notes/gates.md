## Gates

- **The gates hold on arm64 and x86_64 Macs alike** (`docs/portability.md`). `port-audit` names every
  architecture-specific construct by file and function or macro, so a new one in a file already listed still
  needs its row; a gate passing on one architecture says nothing about the other's per-architecture rows.
- **Before every push, run `make gates`**: `test`, `test-compiled`, `corpus-compiled`, `facts-report`,
  `port-audit`, `c-only-audit`, `cmutex-audit`, `open-items-audit`, `api-diff`, in that order. `c-only-audit` runs one file
  through `clj-load`, the C-only host, to see a `catch` clause naming a host type refused out loud. The runner prints wall seconds and exit status per step,
  stops on the first failure, and prints the total on success. Even `make -j gates` keeps that order.
  Put JVM Clojure on PATH (`/opt/homebrew/bin` for Homebrew); `api-diff` also resolves the core.async jar it
  dumps, so the first run of it needs the network and later ones the Maven cache. Every Makefile `swift test` is bounded by
  `timeout -k 5 $(TEST_TIMEOUT)`, 500 seconds unless overridden; GNU coreutils supplies `timeout` on macOS. Keep long
  runs in background logs. SwiftPM passes the test helper's output on in lumps (~16 KB, 64 KB with the XCTest
  pass every run now skips, NOTES "Guard"), and a run killed by the bound loses what it held. So a test still
  running after 300 s (`CLJ_TEST_HANG_S`) prints its name, `clj_debug_sched_dump` and a one-second `sample` of
  every thread to stderr and ends the process (exit 3): the exit delivers everything.
- **`make gates-full` adds `test-isolated` and `test-compiled-asan`.** Run it weekly and after changes to
  allocation/RC, boot, compiler emission, or suite initialization/lifetimes. `test-isolated` retains one
  process per suite: an incorrect live-object baseline can pass when another suite initialized it first.
  It is periodic because that startup cost repeats for every suite. All live-object assertions also remain
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
  the run's bound. The runtime cannot see host threads: the nREPL server's connection threads hold its sessions
  until they see the client hang up, so `NReplTests` waits for the server to be freed (`overTheWire`) — a
  `NamespaceTests` baseline counted them otherwise. A test that starts host threads holding values joins them
  before it returns. A blocking-pool thread that retires frees nothing the count sees (its implicit coroutine
  and carrier are calloc'd, its heap is handed on), so the settle waits for jobs, not for threads.
- **The `test-compiled` hangs on CI were the blocking pool** (NOTES "Scheduler", "Blocking pool"). The report
  above named the test (`AsyncLibTests.pipelines`), and its coroutine dump showed the deadlock: both pool threads
  in `>!!`, the results loop parked on the channel a queued `thread` body would have fed. The first such hang
  (run 36913041719, `d3a3181`) printed nothing, but stopped inside the same range of suites.
- [ ] **Two CI failures seen once, not explained.** `AsyncLibTests.withDeadlineOverCoroutines`
  (AsyncLibTests.swift:190, a 30 ms `with-deadline` over `go-scoped`) failed on arm64 in run 37023946974;
  swift-testing did not print which element differed, and 20 local ASan runs passed. `CoroTests.switchCost`
  failed on x86_64 in run 37027460141 at 848 ns against its 200 ns bound, on a runner whose `test` gate took
  768 s against 490: a timing bound in a correctness gate fails on a slow runner. Trigger: either one again —
  then make the first print the differing element, and give the second a bound relative to the runner.
- **One build directory per configuration.** Plain tools/tests use `.build/plain`, interpreted ASan
  `.build/asan`, compiled core `.build/compiled`, compiled core ASan `.build/compiled-asan`, release tools
  `.build/release`, UBSan `.build/ubsan`, and no-reuse `.build/noreuse`. `BUILD_ROOT` can relocate them as a
  group. Release executables are inside `.build/release/release/`. `make boot` uses the plain compiler;
  `scripts/embed-core.sh` only writes embedded source bytes and has no build-directory dependency.
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
- **CI.** `.github/workflows/gates.yml` runs `make gates` on push to main, on pull requests and by hand, one job
  per architecture, both required: `macos-26` (arm64, 3 cores, 7 GB) and `macos-26-intel` (x86_64, 4 cores,
  14 GB); a matrix entry's `required: false` would turn its job back into `continue-on-error`. The x86_64 job
  became required after consecutive green dispatches on both runners (runs 36989106164, 36992908023,
  36995366088; about 12 min arm64, 24 min x86_64). Both select Xcode 26.6 explicitly; Homebrew coreutils and the pinned Clojure CLI
  are installed per run, the image's JDK 21 runs it. `TEST_TIMEOUT=1200`: the runners are several times
  slower than the M3 the table above was measured on, and `CLJ_CORPUS_TIMEOUT_MS=20000` for the same reason:
  clojure-test-suite's `test-random-sample` takes 4–6 s under ASan there, against the 5 s default ("Corpus").
  Cached: `~/.m2` (api-diff's jars), the SwiftPM
  repository cache, and `.build/corpus-cache`, restored from the newest entry of the architecture and
  pruned to one key per library before saving; its content keys make a stale entry a miss, never a wrong
  hit. Scratch paths are not cached: several GB per architecture, and whether SwiftPM reuses them after
  a fresh checkout (new mtimes and inodes on every source) is unmeasured. No benchmarks run in CI.
