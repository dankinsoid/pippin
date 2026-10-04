## Corpus (corpus/, Tests/PippinTests/CorpusTests.swift, docs/corpus.md)

- **What is vendored**: `corpus/medley` (medley.core and its test, EPL) and `corpus/clojure-test-suite`
  (jank-lang's cross-dialect clojure.core suite, the whole `test/` tree, MPL 2.0), each with a `SOURCE`
  (repo, commit, license, files) and a `manifest.edn` (`:load-path`, `:features` for `#?`, the test
  namespaces or `:test-dirs` to scan). No submodules.
- **The harness** (`CorpusTests`) runs by default: `make corpus`, `CLJ_CORPUS_LIB=medley`
  for one library, `CLJ_CORPUS_UPDATE=1` to rewrite `corpus/<lib>/allowlist.edn` and `docs/corpus.md` from the
  run. It sets the load path and reader features from the manifest, requires every test namespace under
  lenient loading (a failing top-level form is recorded, not fatal), captures the suite's own `SKIP - x`
  lines (`when-var-exists`), runs each namespace through `clojure.test/test-ns` under a collecting
  reporter and folds the events into pass/fail/error per var; a second run over the loaded namespaces is
  the memory check (baseline after the first). Allowlist rule: a failing form, test or skip not in the
  allowlist fails; a listed one that now loads, passes or runs fails too (stale); an entry carries
  `:missing` (the symbols the runtime lacks, extracted from the message), `:design-line` (a line of design §8)
  or `:note` — a hand-written sentence saying whether the failure is an accepted deviation or a runtime bug
  still open, with the repro. A test entry with none of the three fails the check, and a regeneration carries
  `:design-line` and `:note` over, so the review is not lost. `:flaky true` marks a test whose outcome depends on timing
  here (its `:note` says why; the one so far is `realized?` on a `future` whose body is a no-op because the suite's
  `sleep` has no `:default` branch): it is tolerated either way, left out of the two-runs-agree check and kept by a
  regeneration when it happened to pass. Forms are not annotated: a form's reason is its
  own classification (a reader gap or an unresolved symbol). `:second-run-live-objects` is what a second run
  of the same tests leaves alive; a different number fails.
- **On by default** (`CLJ_CORPUS=0` skips it). `make corpus` runs it alone, `make corpus-update` regenerates
  the allowlists and docs/corpus.md. Gate timings, including the corpus, are in "Gates".
- **`:second-run-live-objects` is not always zero**: the suite's own `letfn` leaves a reference cycle per call
  (the volatile cell holds the fn, the fn's body derefs the cell), which RC cannot free — 2 objects per
  `letfn` call, 4 for the two namespaces that use one. The number is recorded per library and checked, so a
  runtime leak still fails; design §7's trial deletion is what would collect it.
- **The watchdog**: a deadline per deftest (`CLJ_CORPUS_TIMEOUT_MS`, 5 s by default) armed by the collecting
  reporter on `:begin-test-var` and cleared on `:end-test-var` (`clj_deadline_set_ms`, analyzer/evaluator
  section). A test past it is `:timeout` and counts as a failure, so one spinning form no longer takes the run
  with it. The expiry is a `:cancelled`, which the `:default` catches of `is` and `test-var` let by, so `run-ns`
  wraps each test fn in a `catch :cancelled` for the run (`guard-expiry`, the `:test` meta restored after): the
  expiry becomes that test's error and the namespace goes on under a fresh deadline; one that still escapes (a
  fixture) is the open test's verdict. `CLJ_CORPUS_LOG=<file>` writes the progress lines to a file as well as stderr: the test runner
  forwards stderr through a pipe and drops what it has not flushed when a killed run dies, which is why the
  earlier hang appeared to be in a different test each time.
- **What the earlier hang was**: `((juxt (range)))` in `clojure.core-test.juxt` — calling a value that is not
  a fn built the "%s cannot be invoked" message with `clj_pr_str`, which realized the infinite lazy seq. The
  fix is `clj_pr_str_max` (printer section) in every error message that quotes a runtime value. It was never
  state-dependent: the namespace hangs in isolation too.
- **What the CI-only `random-sample` failure was**: `test-random-sample` takes 4–6 s in `make test` (ASan, system
  allocator) on the runners, against the 5 s budget, and the expiry landed inside `(= (random-sample 1 coll)
  coll)`. `=` dropped the cancellation the forced `filter` thunk threw and answered false, so the test read as a
  `:fail` of that assertion, in whichever of the two runs crossed the line. Past that, the harness lost the
  timeout itself: the `:cancelled` escaped `test-ns`, the fold kept the open test's status so far — a spinning
  test was a `:pass` — and the namespace's later tests never ran. The verdict followed the clock, not the caller
  join: of the eight corpus runs in two workflow runs every one that failed had crossed 5 s, the one that crossed and passed had
  met the expiry outside `=`, and none under 5 s failed. Fixed at all three places: `=`/`not=`/`hash` rethrow a cancellation equals dropped
  (NOTES "Type descriptor"), the harness's `guard-expiry` and fold above, and `CLJ_CORPUS_TIMEOUT_MS=60000`, which the
  Makefile exports for every target: shards share the cores, and beside three other ASan shards on the 4-core
  runner the test took 20.4 s (run 37188956209) ("Gates", "Shards"). `CorpusTests.anExpiredTestIsATimeoutAndTheNamespaceGoesOn` and
  `DeadlineTests.anExpiryInsideEqualsOrHashIsNotAnAnswer` reproduce it with a 100 ms budget.
- **Symbols the suite and medley need from the JVM**: `clojure.lang.LazySeq` (`p/lazy-seq?`), `Throwable` in
  `catch` works, `instance?` of a JVM class works only for the names bound in core (`clojure.lang.IEditableCollection`,
  `clojure.lang.IRecord`, `clojure.lang.PersistentQueue`, `java.util.UUID`, `java.util.Date`); a static call
  such as `java.util.UUID/fromString` is interop and stays unresolved. The suite's parse-uuid test expects
  `fromString`'s lenient grouping under `:clj` and nil under `:default`; with no features set it fails
  here for answering as the JVM does (allowlist note).
- **`make api-diff`** runs the parity report: `scripts/api-diff.clj dump-jvm` on JVM Clojure, the
  `clj-api-dump` executable for ours (it evaluates `ns-publics` and prints the EDN — name from the map key,
  not the meta, so a var whose meta lost its `:name` still appears), then the diff, weighted by symbol
  occurrences in `corpus/**/*.clj*`. It writes `docs/api-parity.md`, which is committed: 488 of the JVM's 679
  public vars exist, 191 missing, 16 of those used by the corpus; `ref`, `with-precision`, the agents and
  `tap>` lead the weighted list. One macro/fn mismatch (`refer-clojure` is a fn here), one dynamic
  mismatch (`pr` is `^:dynamic` on the JVM) and 12 arity mismatches, of which `sequence`'s multi-coll arity
  and `disj!`'s 1-arity are real gaps rather than differently-written variadics.
- [~] **`api-diff` is also the gate on `^:pippin/extension`** (design, "Инвариант: язык не меняется"): an
  ours-only public var without the mark fails the step and is named in the report's "Unmarked extensions".
  The mark reaches the var's meta the same way in both backends — the compiler emits the whole `def` meta map
  (`compiler.c`, `clj_c_def`) — so the two `clj-api-dump` outputs are byte-identical. Natives carry no source
  metadata, and ~80 of the 104 ours-only publics are natives: `clj_core_mark_extension` (builtins.c) sets the
  mark on an already-bound name and asserts the var exists, so a misspelling is a boot failure rather than an
  unbound public var appearing in the report. `proto.c` marks its whole designator and core-interface tables,
  which is where most of them come from. One shared meta map serves every marked var, built inside `clj_init`
  so no live-object baseline sees it appear. `defprotocol` merges a docstring into the name's metadata
  rather than replacing it, and `defprotocol`/`deftype`/`defrecord` pass `:pippin/extension` on to the vars they
  generate (`-method`, `->Name`, `map->Name`) — a protocol method or a factory belongs to the same dialect as
  its protocol or type. The `name*` convention stays outside the mark: those are internal helpers, not API.
  Earmuffs are not that convention, so `*loaded-libs*` and `clojure.core.async/*scope*` count as publics.
  The gate reaches the two namespaces the report covers. Of the other embedded libs only
  `clojure.test/*assertion-pos*` is ours (the `is` expansion binds it, so it cannot be private); it carries the
  mark with nothing checking it. Trigger for a JVM dump per embedded lib: a second such var.
- **The core.async half of the report diffs against the library, not a written-down list.** `make api-diff`
  dumps `(ns-publics 'clojure.core.async)` with core.async on the classpath (`-Sdeps`, `ASYNC_DEPS` in the
  Makefile) and the report names the version it resolved. The hand-written set it replaces held 57 of the 87
  publics and reported the missing 30 as ours: core.async's protocols and their methods (`Mult`, `muxch*`,
  `tap*`, …) and the ten names deprecated in 0.1.319 but still public in 1.6.681 (`map<`, `partition-by`, …).
  Those are core.async API we build, not extensions, so none of them carries the mark; the report lists the
  deprecated ones apart. Two consequences of using the real dump: the `name*` heuristic may only judge names
  that are ours (core.async's own protocol methods end in `*`), and `defblockingop`, `do-alts`, `fn-handler`
  and `ioc-alts!` show as missing — the JVM implementation's ioc and macro plumbing.

