## Corpus (corpus/, Tests/PippinTests/CorpusTests.swift, docs/corpus.md)

- **What is vendored**: `corpus/medley` (medley.core and its test, EPL), `corpus/clojure-test-suite`
  (jank-lang's cross-dialect clojure.core suite, the whole `test/` tree, MPL 2.0) and
  `corpus/clojure-core-tests` (nine files of Clojure's own `test/clojure/test_clojure/`, EPL 1.0), each with a
  `SOURCE` (repo, commit, license, files) and a `manifest.edn` (`:load-path`, `:features` for `#?`, the test
  namespaces or `:test-dirs` to scan). No submodules.
- **The parity report names a pinned Clojure, not the machine's.** `dump-jvm` runs under `JVM_DEPS`
  (1.12.6, the Makefile), carries `:version` in its dump as the async and cljs dumps already did, and the
  report prints what was dumped instead of calling `(clojure-version)` in the reporting process. Before this
  the header said whatever the local CLI resolved — 1.12.4 on one machine, 1.12.6 on another — so every gate
  run flipped the committed line. The pin is also the tag `corpus/clojure-core-tests` is vendored at, so the
  suite and the diff describe one Clojure. The name sets are the same either way: pinning moved no count.
- **Clojure's own suite: what is in and what is out.** `corpus/clojure-core-tests` holds the portable part of
  `test/clojure/test_clojure/` at tag `clojure-1.12.6`, the version `docs/api-parity.md` diffs against:
  the nine files design §10 names — `sequences`, `data_structures`, `control`, `fn`, `def`, `macros`, `logic`,
  `string`, `numbers` — and seventeen more — `transducers`, `vectors`, `other_functions`, `special`,
  `clojure_set`, `multimethods`, `vars`, `clojure_walk`, `transients`, `errors`, `evaluation`,
  `for`, `atoms`, `delays`, `predicates`, `volatiles`, `keywords` — plus `test/clojure/test_helper.clj`,
  which five of them `:use`. Unmodified, headers kept. The rule for the
  rest: a file is in when its subject is the language or a namespace this core carries. Out by subject: the
  host files (`java_interop`, `reflect`, `genclass`, `proxy/`, `param_tags`, `method_thunks`, `annotations`,
  `array_symbols`, `data_structures_interop`, `serialization`, `streams`, `generated_*`), the JVM concurrency
  files (`agents`, `refs`, `parallel`), the compiler and tooling files (`compilation`, `main`, `repl`, `server`,
  `rt`, `api`, `run_single_test`, `test`, `test_fixtures`) and the files whose subject is a namespace this core
  does not carry (`clojure_xml`, `clojure_zip`, `reducers`, `math`, `data`, `printer`, `edn`, `parse`).
  `generators` and `protocols` are in only as the shims that stand in for them.
- **Four of the files NOTES called portable are not.** `clearing` is `java.lang.reflect.Field` over a closure's
  fields from end to end — its subject is the JVM's clearing of closed-overs, so it belongs among the host
  files. `metadata` requires `clojure.pprint`, `clojure.inspector`, `clojure.xml`,
  `clojure.zip`, `clojure.java.io` and `clojure.data` in a top-level `doseq` and then checks their docstrings:
  its subject is namespaces this core does not carry. `try_catch` imports `clojure.test.ReflectorTryCatchFixture`,
  a Java class compiled from the test tree, so both of its deftests are reflection on that class.
  `reader.cljc` needs `clojure.edn` and `clojure.instant`. 11 deftests in all.
- **`(:import …)` does not cost a file.** The `ns` macro here ignores an `:import` clause rather than refusing
  it, so `vectors`, `errors`, `delays` and `clearing` load their own `(ns …)` form and lose only the body forms
  that name an imported class. A file whose `ns` form failed would lose every deftest at once, which is why
  the shims exist; `:import` needs none.
- **`numbers` is taken whole, not partially**, which design §10's "частично" allowed for: the numeric tower is
  complete (`ratio?`, `bigint`, `bigdec`, `numerator`, `rationalize`) and chars are a real type, so the line is
  not drawn inside the file — what does not run there fails per form on a JVM name (`Math/round`,
  `unchecked-byte`, `Class/forName`, `Float.`) and is allowlisted with that name. 17 of the file's 44 deftests
  are lost that way, more than in any other file: 14 forms that do not load and three tests that call one of
  them.
- **The boxed number classes are namespaces of vars**, the shape `Thread/sleep` has
  (`install_boxed_classes`, builtins_number.c; docs/jvm-differences.md). It recovered 14 deftests of Clojure's
  own suite — 13 in `numbers`, one in `sequences` — and the set is demand, one line per name, not a
  `java.lang` slice: `Long/MAX_VALUE`, `Long/MIN_VALUE`, `Long/valueOf`, `Integer/MAX_VALUE`,
  `Integer/MIN_VALUE`, `Short/MAX_VALUE`, `Byte/MAX_VALUE`, `Double/MAX_VALUE`, `Double/NaN`,
  `Double/POSITIVE_INFINITY`, `Double/isNaN`. The 21 forms whose *first* unresolved name was such a constant
  were an overcount of what the constants are worth: seven of them hit a second JVM name behind it
  (`unchecked-byte`, `Float.`, `Double.`, `cast`, `iterator-seq`, `BigDecimal`, `clojure.lang.Range/create`)
  and stay unloaded. Two of the 14 do not pass: `test-abs` is `(abs Long/MIN_VALUE)`, a §8 refusal, and
  `test-longrange-corners` calls a helper that `clojure.lang.Range/create` keeps unloaded. `Float` is refused
  whole rather than half-bound: a constant of it as a double would resolve one side of
  `(Float/isNaN Float/NaN)` and refuse the other.
- **`:tests-only true` in a manifest** tells `clj-facts` the library is test code where the name heuristic
  cannot: it counts a library apart when every load-path root is named `test`, and this one's `shim` root
  would have put 111k assertion nodes into the library-code share (docs/facts-coverage.md).
- **The shims under `corpus/clojure-core-tests/shim/` are ours, not Clojure's.** Four of the nine files require
  namespaces the JVM test tooling supplies, and a failing `(ns ...)` form takes the whole file down where a
  failing body form costs one deftest, so each is stood in for: `clojure.data.generators` and
  `clojure.test-clojure.generators` (generators over an own xorshift64, not core's `rand` — the harness runs
  every test twice and compares the verdicts), `clojure.test.generative` (its `defspec` needs a runner of its
  own, so `test-ns` runs none of them on the JVM either; here it becomes a `deftest` of 20 rounds, which is how
  `numbers`' five arithmetic-law specs run at all), `clojure.test.check.*` (a property is a fn of the size, no
  shrinking) and an empty `clojure.test-clojure.protocols`, which `def.clj` `:use`s and refers nothing from.
  `gen/symbol` and `gen/keyword` are left out of the EDN-able scalars: `data_structures` picks from that vector
  with core's `rand-nth`, and interning a fresh name is permanent, so a differing pick would move the
  second-run live count.
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
  `:design-line` and `:note` over, so the review is not lost. `:flaky true` marks a test whose outcome is not a function of
  the code alone — timing, or state the first run left — and whose `:note` says which: it is tolerated either way,
  left out of the two-runs-agree check and kept by a regeneration when it happened to pass. Two so far:
  `realized?` on a `future` whose body is a no-op because the suite's `sleep` has no `:default` branch, and
  `multimethods/methods-test`, whose `defmulti` is `defonce`, so the `remove-method` of the first run reaches
  the second — not re-runnable on the JVM either. Forms are not annotated: a form's reason is its
  own classification (a reader gap or an unresolved symbol). `:second-run-live-objects` is what a second run
  of the same tests leaves alive; a different number fails.
- **On by default** (`CLJ_CORPUS=0` skips it). `make corpus` runs it alone, `make corpus-update` regenerates
  the allowlists and docs/corpus.md. Gate timings, including the corpus, are in "Gates".
- **`:second-run-live-objects` is not always zero**: the suite's own `letfn` leaves a reference cycle per call
  (the volatile cell holds the fn, the fn's body derefs the cell), which RC cannot free — 2 objects per
  `letfn` call, 4 for the two namespaces that use one and 36 for medley, of which the last 4 arrived with
  `test-mapply` and its two `letfn`s once that test started running. The number is recorded per library and
  checked, so a runtime leak still fails; design §7's trial deletion is what would collect it.
  `clojure-core-tests` is at 11: `special`'s `letfn` cycles, and the namespace and the symbols `ns_libs` and
  `keywords` intern per run from a `gensym` ("Symbol / keyword": interning is permanent). It is a fixed number
  per run, not a growing one, which is the property the check needs. The same check caught the runaway
  `(apply f (range))` coroutine ("Analyzer and evaluator"): every library's count became the time the run took,
  medley's 36 among them, so a leak elsewhere in the process shows up here too. A generated symbol or keyword
  also has to be picked deterministically, which is why the generator shims seed themselves: a differing pick
  between the two runs moved the number from run to run.
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
- **The third column is cljs, and it is derived, not written down.** `make api-diff` dumps `cljs.core`'s
  publics with ClojureScript on the classpath (`CLJS_DEPS` in the Makefile, 1.11.132) as the cljs analyzer
  itself reports them: `:defs` from `cljs/core.cljs.cache.aot.edn`, the analysis cache the compiler ships in
  the jar and reads through `cljs.analyzer.api/read-analysis-cache`, `:macros` interned by the analyzer's own
  `intern-macros` from the loaded Clojure-side `cljs.core`, and `:private` dropped by
  `cljs.analyzer.api/ns-publics` — 928 names, 828 defs and 100 macros, out of 958 cached defs and 188 interned
  macros. The cache carries no `:macros` of its own, so the macro half has to come from the macro namespace. A source scan of `cljs/core.cljs` and
  `cljs/core.cljc` would be wrong twice: cljs aliases JVM `clojure.core` as `core`, so `..` is
  `(core/defmacro ..`, `import`, `locking` and `defmacro` hide behind `core/`, and `defmacro` is not even a
  `defmacro` but `(core/defn defmacro` with macro meta; and a scan counts private defs, which `ns-publics`
  does not. It costs 3 s, and only the jar, no Closure compiler run.
- **Every missing name carries a verdict, and that is a gate.** `scripts/api-missing.edn` holds one of
  `:keep`/`:repoint`/`:drop`/`:special-form` per name with its reason, a `:drop` citing the idea cell of its
  design §8 row; `make api-diff` fails on a missing name without a verdict, on a verdict whose name is public
  here again, and prints a dangling §8 citation. The cljs column is the evidence, not the verdict (design §3,
  "Предел расхождения — ClojureScript"), so a name cljs lacks can still be `:keep`: the agents and refs are
  absent there only because JS has one thread. Two names showed what the gate is for: `defmacro` stood among
  the missing with three corpus uses because it is a special form here (`SP_DEFMACRO`, analyzer.c), not a var,
  and `monitor-enter`/`monitor-exit` are special forms on the JVM, absent from `ns-publics`, so the report
  cannot see them at all although §10 counted them among the exceptions needing a §8 row.
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

- **What the second portion of Clojure's own suite found.** Eighteen more files brought 163 deftests and five
  runtime bugs, all fixed: `apply` realized the rest argument of a variadic fn ("Analyzer and evaluator"), and
  `clojure.walk` dropped metadata, `with-redefs`/`alter-var-root` read a binding where they needed the root,
  `require`/`use` swallowed an unknown flag and an empty argument list, and `load-lib` aliased only one of
  `:as` and `:as-alias` ("core.clj"). Two API gaps closed with them: `sequence`'s multi-collection arity and
  `Eduction`'s `Sequential`. `apply` was the one worth the whole portion: a `(future (apply sample (range)))`
  in `vars.clj` never returned, and the harness reported it as 12.5M live objects in a library whose own tests
  had not changed.
- [ ] **`ns_libs` is portable and still out, for the memory check.** `refer-error-messages` makes a namespace
  from a `gensym` and `eval`s a `def` into it, and a namespace, a var and their name symbols are permanent here
  ("Analyzer and evaluator": vars are immortal), so the file alone put 11 lasting objects into
  `:second-run-live-objects` where the rest of the library leaves 0. The two backends also disagreed on the
  number — 11 interpreted against 10 compiled, the quoted form's reader position the compiled constant does not
  carry (`docs/jvm-differences.md`) — and the allowlist holds one number, so the disagreement had nowhere to go.
  It is the one file whose own subject (namespaces, `require`, `refer`) this core carries and that is not taken.
  It earned its place first: three of the eleven bugs this portion found are its — `require`/`use` swallowing an
  unknown flag and an empty argument list, `load-lib` aliasing only one of `:as` and `:as-alias`, and
  `ns-resolve`'s env argument not shadowing a var ("core.clj"). Trigger for taking it: a namespace that
  `remove-ns` makes collectable, or a live-object baseline per test rather than per library.
- **Ten deftests of Clojure's own suite are not re-runnable**, and the harness runs everything twice. One
  survives as `:flaky` (`multimethods/methods-test`, whose `defmulti` is `defonce`, so the `remove-method` of
  the first run reaches the second); the rest went out with `ns_libs`. The property is the JVM's too: a second
  `test-ns` over those namespaces fails there in the same places.
