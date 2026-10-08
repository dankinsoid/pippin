## Corpus (corpus/, Tests/PippinTests/CorpusTests.swift, docs/corpus.md)

- **What is vendored**: `corpus/medley` (medley.core and its test, EPL), `corpus/clojure-test-suite`
  (jank-lang's cross-dialect clojure.core suite, the whole `test/` tree, MPL 2.0),
  `corpus/clojure-core-tests` (26 files of Clojure's own `test/clojure/test_clojure/`, EPL 1.0) and
  `corpus/math-combinatorics` (org.clojure's math.combinatorics v0.3.2 and its test, EPL 1.0) and
  `corpus/dependency` (Stuart Sierra's dependency 1.0.0 and its test, EPL 1.0), each with a
  `SOURCE` (repo, commit, license, files) and a `manifest.edn` (`:load-path`, `:features` for `#?`, the test
  namespaces or `:test-dirs` to scan). No submodules.
- **Of design §10's eight named libraries, only core.async is this core's to begin with.** Each of the other
  seven was vendored into the harness and measured, and six cannot load at all: their `:clj` branches are
  `clojure.lang.*` interfaces in a `deftype` body, JVM exception constructors and `java.lang` statics, and
  the `:cljs` branches are the same code against `cljs.core`'s protocols and `js/`. **instaparse** (45
  failing forms) carries `auto-flatten-seq` as a `deftype` over `clojure.lang.IHashEq`, `ISeq`, `Counted`,
  `ILookup`, `IObj`, `Seqable`, `IFn` and `java.util.Collection`, and needs `clojure.core.protocols`,
  `dosync`, `unchecked-multiply-int`, `print-method`, `CharSequence`, `Character/codePointAt` and four
  exception constructors. **core.match** (61) reads `clojure.lang.Compiler/LOOP_LOCALS` and
  `extend-type clojure.lang.ILookup`, and its nodes are `deftype`s over `clojure.lang.IObj`/`ILookup`.
  **meander** (80) needs `clojure.zip`, `clojure.pprint`, `print-method`, `*warn-on-reflection*`,
  `Integer/parseInt`, `iterator-seq` and `java.lang.Iterable`, and its cascade leaves `r.match/match`
  undefined, which is the library. **tools.reader** (103) is `StringBuilder.`, `Character/digit`,
  `RT/map`, `PersistentHashSet/createWithCheck` and `clojure.lang.PersistentList/create` from end to end —
  so **edamame**, whose `:clj` *and* `:default` branches both require `clojure.tools.reader.reader-types`,
  goes with it, and **malli** (41), which requires edamame, `borkdude/dynaload` and `test.check`, and whose
  own `malli.impl.regex` is a `deftype` with `^:unsynchronized-mutable` fields. **datascript** imports
  `me.tonsky.persistent_sorted_set.PersistentSortedSet`, a Java class, so there is nothing to measure.
  Two libraries outside §10's list were measured for the same reason and are also out: **tools.cli** wants
  `*out*`, `*err*`, `Exception.` and `Integer/parseInt`, and **camel-snake-kebab** wants `Pattern/compile`
  and `(.end matcher)`. The demand this reports, in order of how many libraries it blocks: a `deftype` body
  over host interfaces, JVM exception constructors, the `java.lang` statics §8 refuses, `print-method`,
  `*out*`/`*err*`, `clojure.zip`/`clojure.pprint`, and `^:unsynchronized-mutable` fields.
- **What a library of §10's class actually looks like**: one namespace over `clojure.core` (plus
  `clojure.string`/`set`/`walk`), no `deftype` over a host interface, no JVM static. `math-combinatorics` is
  the one taken — it loads whole, no top-level form lost, and passes 17 of its 18 deftests. It wanted
  `:features #{:clj}`: four of its `loop` vectors take an initial value from a `#?` pair, so with no feature
  the vector reads with an odd number of forms and the whole `defn` is lost. The one failure left is
  `partitions` of an input with duplicates, whose parts come out of a `{index count}` map and so carry that
  map's seq order (allowlist note, design §8). Four gaps closed on the way, two of them from this library
  and two from the libraries that did not land: a regex literal in a discarded `#?` branch read by
  string-escape rules (NOTES "Reader"), a lazy seq whose thunk answers `()` caching `()` where `RT.seq`
  answers nil — so every seq walk written in Clojure saw one element too many (NOTES "Type descriptor"),
  `fn`'s missing `:pre`/`:post` conditions and `*assert*` (NOTES "core.clj"), and `(Name. args)` for a
  `deftype`/`defrecord` of one's own (NOTES "Analyzer and evaluator").
- **Stuart Sierra's `dependency` 1.0.0 is in, and what kept it out was a stack overflow under a lock.** The
  library loads whole, passes all 9 of its deftests and leaves 0 live objects after a second run, so its
  allowlist is empty; one namespace over `clojure.core` and `clojure.set`, `:features #{:clj}` for the
  topological comparator's `#?(:clj Long/MAX_VALUE …)`. Its test builds `g3` as a `->` chain of 104 interpreted
  protocol calls, which is a node tree 104 deep, and under `--sanitize=address` the facts pass's recording walk
  spent ~9 KB a level on it and ran the 512 KB coroutine stack out between the 55th and 60th — inside the lock
  `clj_exec_derive` holds across the pass, where the guard cannot land, so the ASan shard died with
  `fatal stack overflow (a runtime lock is held)` rather than failing. The fix is one margin for every
  recursion whose depth is the program's (NOTES "Guard"): the pass now answers TOP for the subtree it has no
  stack for and the analyzer refuses a form it has no stack to walk. The depth ASan allows on that shape is
  140 levels against the library's 104, so the headroom is a third; a compiler that grows the pass's frames
  would need `clj_coro_set_stack_size` raised or the pass's per-level cost cut.
- **core.async's own suite is `corpus/core-async`: of `async_test.clj`'s 18 deftests, 16 run and 15 pass**,
  against 18 of 18 on the JVM (measured per deftest under Clojure 1.12.6 and core.async 1.6.681, none of them
  hanging; `expanding-transducer-delivers-to-multiple-pending` takes 4.37 s there and 4.6–4.9 s here, its own
  `(Thread/sleep 50)` poll 81 times over). It is at tag v1.6.681, the version `make api-diff` diffs the async
  half against, and it is the one file taken: the library itself is ours, so only the test tree is vendored and
  the manifest is `:tests-only`. Two forms do not load — `take!-on-caller?`
  and `put!-on-caller?`, whose subject is which thread a `put!`/`take!` callback runs on — because
  `Thread/currentThread` is refused rather than shimmed the way `Thread/sleep` is (design §8,
  `docs/jvm-differences.md`). One test fails, and the code is core.async's own: the ASYNC-127 block of `ops-tests`,
  `(mult (to-chan! [1 2 3]))` plus three `tap`s, where the source is a filled buffer and the mult's `go-loop` drains
  it before the taps register — which `mult`'s own docstring allows ("Items received when there are no taps get
  dropped"). Our `mult` is 1.6.681's text, the ASYNC-127 fix of upstream `2df8e1d` in it, and the same block with the
  source filled *after* the taps answers 1/1/2 every time, so what differs is only the order of a spawned
  coroutine's first step (design §8, the spawn-locality row). Measured over 300 runs of the block: 285 answer a later
  item, 11 pass whole and 4 leave `t-1` an orphan — the mult read the source's nil while `@cs` was still empty, so it
  closed no tap and `(<!! t-1)` never returns, which the watchdog ends as `:timeout`. The entry is `:flaky`: all three
  verdicts are its.
- [~] **What the file's leftover coroutines are, and what reclaims them.** They are abandoned parked by the tests
  themselves: fifteen `onto-chan!` fillers of a `(chan n xf)` whose takers have all reported and nobody drains again
  (`check-expanding-transducer`, run 81 times), the `mult` and `mix` loops of `ops-tests`, and the two `future`s that
  `unfulfilled-readers-block` and `expanding-transducer-puts-can-ignore-buffer-fullness` leave waiting on purpose. Each
  is a reference cycle through the channel it waits on (NOTES "Coroutines", the abandoned park), and every deftest has
  ended when a run returns, so a coroutine still parked is the library's leftover. `reclaimAbandoned` lets the cycle
  collector judge them first — `clj_cc_collect` until neither the count it cancelled nor the live count moves — which
  cancels the fillers, the futures and the `mult` loops: their cycles run through heap edges and their watchdog
  deadline counts as their own. What is left is a park whose cycle a frame's own reference holds: the two `mix` loops,
  whose `alts!` port vector the loop owns, and the ASYNC-127 `mult` when its race leaves a tap's `put!` pending with
  the tap in the loop's own `chs`. Those it cancels (`clj_debug_cancel_live_coros`) before the memory check, and
  `:abandoned-coroutines` in the allowlist bounds that residue: 0 for every library but this one, so the old "nothing
  may be left" check holds everywhere else, and 3 here. A run past the bound fails the step. Left alone, the only end
  of such a park is the per-deftest watchdog deadline its spawn conveyed, `CLJ_CORPUS_TIMEOUT_MS` from the last spawn
  (60 s as the Makefile exports it): the 20–55 s drain this harness once waited out. A coroutine that never parked is
  not on the live list, so a runaway loop is not reclaimed there and still fails the settle, which is how the
  `(apply f (range))` one was caught. Trigger for dropping the residue's cancel: design §7's frame enumeration
  (NOTES "Coroutines").
- **The other seven files of core.async's suite are out by subject, and `pipeline_test.clj` by cost.**
  `buffers_test` and `timers_test` name `clojure.core.async.impl.protocols` (`full?`/`add!`/`remove!`/`close-buf!`)
  and `impl.timers`: the buffers here are the spec objects `chan` reads, not containers with those methods, and the
  timer wheel is C. `ioc_macros_test` names `impl.ioc-macros`, the state-machine transform design §8 refuses;
  `lab_test` names `clojure.core.async.lab`, which we do not carry; `concurrent_test` is `java.util.concurrent`'s
  `ThreadFactory` and `exceptions_test` is the JVM's default uncaught-exception handler plus `clojure.stacktrace`.
  `pipeline_test` is portable and its subject is ours, but `test-compute` is `slow-fib` of 15–37 over 50 inputs:
  **30.4 s** interpreted on this machine unloaded, against the 60 s watchdog — a 2× margin where `test-random-sample`
  has 3× under four ASan shards. The file's other seven deftests run in 2.2 s and pass. Trigger for taking it: a
  corpus run whose user code is compiled in every mode, or a per-test budget in the allowlist.
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
  which seven of them require. Unmodified, headers kept. The rule for the
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
  it, so `vectors`, `errors` and `delays` load their own `(ns …)` form and lose only the body forms
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
  the memory check (baseline after the first), with `reclaimAbandoned` between them: every deftest has ended,
  so a coroutine still parked was abandoned by the library's own tests, and it is collected, or cancelled where the
  collector cannot judge it, rather than waited out. Allowlist rule: a failing form, test or skip not in the
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
- **`:second-run-live-objects` is zero for every library**, counted after the cycle collector ran
  (`runtimeSettled` collects). The suites' own `letfn` makes a reference cycle per call (the volatile cell holds
  the fn, the fn's body derefs the cell): 4 objects for clojure-test-suite and 36 for medley while nothing
  collected them, 0 since trial deletion does (NOTES "RC"). The number is recorded per library and checked, so a
  runtime leak, or a cycle the collector does not see, still fails. The same check caught the runaway
  `(apply f (range))` coroutine ("Analyzer and evaluator"): every library's count became the time the run took,
  so a leak elsewhere in the process shows up here too. A generated symbol or keyword interns for good ("Symbol /
  keyword"), so it has to be picked deterministically, which is why the generator shims seed themselves: a
  differing pick between the two runs moved the number from run to run.
- **A run the watchdog cut short does not warm the next, so its count is taken again.** A path's first run
  allocates for the process: a `reify` site makes its type on its first call and keeps it (`reify_type`, proto.c),
  and a name interns for good. The second run's count is a leak only when the first took every path the second
  does. core-async's `ops-tests` orphans `t-1` at its ASYNC-127 block now and then (the `:flaky` entry above) and the
  watchdog ends it at `CLJ_CORPUS_TIMEOUT_MS`; on x86_64 that fell in the first run twice (runs 37447701922 and
  37700500770), so `mix` and `pub` ran for the first time in the measured run and the count read 22 and 18 against
  0. Forced by a hang at that block in the first run only, both architectures read 18 — fn +10, vector +4, type +2,
  string +2: the two `reify` types with their ten methods (run 37754231933). The same cut in the measured run reads
  0 (37614772790), since that run then takes fewer paths. So a count that is off after a run with a `:timeout`, or
  in one, is measured again with the measured run as one more warm-up, at most three times; the hang probe's second
  measurement read 0. A count that is off prints the live objects by type (`clj_debug_live_by_type`), the tests cut
  short in each run, what the scheduler and the cycle collector still hold, and the live coroutines.
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
