.PHONY: bench-ab port-audit c-only-audit cmutex-audit park-audit slot-audit open-items open-items-audit load-asan build boot bench facts-report shake test test-pool test-ubsan test-tsan test-noreuse test-all test-isolated corpus corpus-update api-diff test-compiled corpus-compiled test-eval-compiled test-compiled-asan swift-reprint ios-probe ios-app gates gates-full

# A test that crashes ends with its trace and a nonzero exit; the default death waits on the crash reporter, which
# can leave the helper unkillable (NOTES.md, "Guard").
export CLJ_CRASH_EXIT ?= 1
export ASAN_OPTIONS ?= abort_on_error=0
# ld64 stamps object mtimes into the debug map, so without this a fresh build's clj-compile, whose bytes key the
# corpus cache, never matches a cached entry (docs/notes/gates.md, "Corpus compilation cache").
export ZERO_AR_DATE ?= 1
# The corpus watchdog catches a spinning deftest (NOTES "Corpus"), bounded far past any runner: under ASan beside
# three other shards on a 4-core runner, test-random-sample takes 20 s.
export CLJ_CORPUS_TIMEOUT_MS ?= 60000

BUILD_ROOT ?= .build
PLAIN = $(BUILD_ROOT)/plain
ASAN = $(BUILD_ROOT)/asan
COMPILED = $(BUILD_ROOT)/compiled
COMPILED_ASAN = $(BUILD_ROOT)/compiled-asan
RELEASE = $(BUILD_ROOT)/release
TEST_TIMEOUT ?= 500
# The suite is swift-testing only: the XCTest pass runs nothing, and its discovery helper loads an ASan-linked
# bundle without the runtime first and dies (NOTES "Guard").
TEST = timeout -k 5 $(TEST_TIMEOUT) swift test --disable-xctest
# The suite as parallel processes over disjoint suites (docs/notes/gates.md, "Shards"); TEST_SHARDS=N sets the count.
# python3 may be an xcrun shim, which sets SDKROOT for the swift calls it makes unless the caller's is passed on.
SHARDS = python3 scripts/test-shards.py run --timeout $(TEST_TIMEOUT) --sdkroot='$(SDKROOT)'
export CLJ_COMPILE = $(abspath $(PLAIN)/debug/clj-compile)
export CLJ_CORPUS_CACHE = $(abspath $(BUILD_ROOT)/corpus-cache)
CORPUS_REPORT = $(PLAIN)/corpus-report

build:
	swift build --scratch-path $(PLAIN)

# Re-embeds boot/core.clj and regenerates the compiled core (boot/core.c, boot/libs_*.c) from an interpreted
# boot; a test checks the embedded bytes against the file, so commit all of them.
boot:
	sh scripts/embed-core.sh
	swift build --scratch-path $(PLAIN) --product clj-compile
	$(PLAIN)/debug/clj-compile --core --out Sources/CljCore/boot

# ASan sees object boundaries only with the system allocator.
test:
	CLJ_SYSTEM_ALLOC=1 $(SHARDS) --gate test -- --scratch-path $(ASAN) --sanitize=address

test-pool:
	$(SHARDS) --gate test-pool -- --scratch-path $(PLAIN)

test-ubsan:
	$(SHARDS) --gate test-ubsan -- --scratch-path $(BUILD_ROOT)/ubsan --sanitize=undefined

# Data races, with the coroutine switch annotated by TSan's fiber API (docs/notes/gates.md, "TSan"). Opt-in like
# test-ubsan. TSan sees object boundaries only with the system allocator, as ASan does.
test-tsan: TEST_TIMEOUT = 3600
test-tsan:
	CLJ_SYSTEM_ALLOC=1 CLJ_TEST_HANG_S=1800 TSAN_OPTIONS="abort_on_error=0 suppressions=$(abspath scripts/tsan.supp)" \
		$(SHARDS) --gate test-tsan -- --scratch-path $(BUILD_ROOT)/tsan --sanitize=thread

# The §7 invariant: clj_is_unique always false, so every in-place path degrades to a copy (NOTES.md, RC).
test-noreuse:
	$(SHARDS) --gate test-noreuse -- --scratch-path $(BUILD_ROOT)/noreuse -Xcc -DCLJ_NO_REUSE

# Every sanitizer and allocator mode has its own incremental build.
test-all: test test-pool test-ubsan test-noreuse

# Every suite alone, one process each: a live-object baseline that only holds after another suite's one-time
# allocations fails here and not in the full run. Periodic, not a gate (NOTES.md, "Symbol / keyword").
test-isolated:
	$(SHARDS) --gate test-pool --isolated -- --scratch-path $(PLAIN)

# The acceptance corpus alone (it is part of every test run; CLJ_CORPUS=0 skips it there).
corpus:
	$(TEST) --scratch-path $(PLAIN) --filter CorpusTests

# Rewrites corpus/*/allowlist.edn and docs/corpus.md from the run; review the diff before committing.
corpus-update:
	CLJ_CORPUS_UPDATE=1 $(TEST) --scratch-path $(PLAIN) --filter CorpusTests

# ---- the compiler (Sources/CljCompiler, NOTES.md "Compiler")

# The whole suite on the compiled core with the pool allocator.
test-compiled:
	$(SHARDS) --gate test-compiled -- --scratch-path $(COMPILED) -Xcc -DCLJ_COMPILED_CORE

test-compiled-asan:
	CLJ_SYSTEM_ALLOC=1 $(SHARDS) --gate test-compiled-asan -- --scratch-path $(COMPILED_ASAN) -Xcc -DCLJ_COMPILED_CORE --sanitize=address

# Both corpora through compiled user code: clj-compile per library, clang per file, dlopen; the per-test report
# must match the interpreter's line by line.
corpus-compiled:
	swift build --scratch-path $(PLAIN) --product clj-compile
	rm -rf $(CORPUS_REPORT)
	CLJ_CORPUS_REPORT=$(CORPUS_REPORT)/interpreted $(TEST) --scratch-path $(PLAIN) --filter CorpusTests
	CLJ_CORPUS_COMPILED=1 CLJ_CORPUS_REPORT=$(CORPUS_REPORT)/compiled $(TEST) --scratch-path $(PLAIN) --filter CorpusTests
	diff -ru $(CORPUS_REPORT)/interpreted $(CORPUS_REPORT)/compiled && echo "corpus: compiled == interpreted"

# Every rt.eval of the suite through emit, clang and dlopen: slow, opt-in; CLJ_EVAL_CLOSED=1 for --closed.
# A test's evals each pay a clang run, so the hang report's 300 s would end tests that are only slow.
test-eval-compiled:
	CLJ_EVAL=compiled CLJ_EVAL_ROOT=$(PWD) CLJ_CORPUS=0 CLJ_TEST_HANG_S=3600 $(SHARDS) --gate test-eval-compiled -- --scratch-path $(PLAIN)

# The whole-program closed build of design §10 step 6: core.clj, the libs the program requires and the program as
# one shaken set (NOTES.md, "Compiler": tree shaking). SHAKE_RELEASE=1 measures it release and dead-stripped,
# which is the shape an app ships and the only one where the section numbers mean anything.
shake:
	swift build --scratch-path $(PLAIN) --product clj-compile --product clj-load
	sh scripts/shake.sh

# The type-coverage metric of design §10 step 3b: loads core.clj, the embedded libs and every corpus library,
# analyzes every form again and runs the facts pass over it. Rewrites docs/facts-coverage.md, which is committed;
# the wall-clock half goes to $(BUILD_ROOT)/facts-cost.md, which is not, so a gate run leaves the tree clean.
facts-report:
	swift build --scratch-path $(RELEASE) -c release --product clj-facts
	$(RELEASE)/release/clj-facts . docs/facts-coverage.md $(BUILD_ROOT)/facts-cost.md

# core.async is not on the default classpath, and its publics are what the async half of the parity report diffs against.
ASYNC_DEPS = {:deps {org.clojure/core.async {:mvn/version "1.6.681"}}}
# ClojureScript is the measure of admissible divergence (design §3), so its cljs.core publics are the report's third
# column; the version is pinned here and printed into the report from the jar the classpath resolved.
CLJS_DEPS = {:deps {org.clojure/clojurescript {:mvn/version "1.11.132"}}}
# The JVM Clojure the parity report diffs against. Pinned, or the report names whatever the machine resolved —
# and corpus/clojure-core-tests is vendored at this same tag, so the suite and the diff describe one Clojure.
JVM_DEPS = {:deps {org.clojure/clojure {:mvn/version "1.12.6"}}}

# clojure.core parity: the JVM's ns-publics, ours, cljs.core's, and the diff weighted by the corpus (scripts/api-diff.clj).
# Needs JVM Clojure on PATH and the two jars in ~/.m2 or Maven Central; writes docs/api-parity.md, which is committed,
# and fails on an unmarked extension or a missing name without a verdict in scripts/api-missing.edn.
api-diff:
	@mkdir -p $(PLAIN)/api
	clojure -Sdeps '$(JVM_DEPS)' -M scripts/api-diff.clj dump-jvm > $(PLAIN)/api/jvm.edn
	clojure -Sdeps '$(ASYNC_DEPS)' -M scripts/api-diff.clj dump-async > $(PLAIN)/api/jvm-async.edn
	clojure -Sdeps '$(CLJS_DEPS)' -M scripts/api-diff.clj dump-cljs > $(PLAIN)/api/cljs.edn
	swift run --scratch-path $(PLAIN) clj-api-dump > $(PLAIN)/api/ours.edn
	swift run --scratch-path $(PLAIN) clj-api-dump clojure.core.async > $(PLAIN)/api/ours-async.edn
	clojure -M scripts/api-diff.clj diff $(PLAIN)/api/jvm.edn $(PLAIN)/api/ours.edn $(PLAIN)/api/cljs.edn scripts/api-missing.edn corpus docs/api-parity.md $(PLAIN)/api/ours-async.edn $(PLAIN)/api/jvm-async.edn

# Same binary twice: pool, then system malloc as the control. Compare only within one invocation.
bench:
	swift build --scratch-path $(RELEASE) -c release --product clj-bench
	$(RELEASE)/release/clj-bench
	@echo
	CLJ_SYSTEM_ALLOC=1 $(RELEASE)/release/clj-bench

# Release bench of BASE (default main) against this tree, alternated in one job: BASE=<ref> ROUNDS=<n> ONLY="<sel> ...".
bench-ab:
	sh scripts/bench-ab.sh

# One file through the C core under ASan: FILE=x.clj make load-asan. A crash lands on the stack that caused it,
# where the swift-testing run prints "<empty stack>".
load-asan:
	CLJ_SYSTEM_ALLOC=1 swift build --scratch-path $(ASAN) --sanitize=address --product clj-load
	CLJ_SYSTEM_ALLOC=1 $(ASAN)/debug/clj-load $(FILE)

# A C-only host installs no host type resolver, so a catch clause naming one must fail loudly (design §4).
c-only-audit:
	swift build --scratch-path $(PLAIN) --product clj-load
	@out=`$(PLAIN)/debug/clj-load Tests/PippinTests/Fixtures/host-type-c-only.clj 2>&1`; \
		echo "$$out" | grep -q "No host type resolver" && echo "$$out" | grep -qx "boom"
	@echo "c-only-audit: a host type clause is refused where no resolver exists, keeping the exception it interrupted"

# Every file using a platform-specific API and every architecture-specific spot has its row in docs/portability.md.
port-audit:
	python3 scripts/port-audit.py

# Every cmutex acquisition must be accounted for: an unregistered one is a suspend! parked holding it.
cmutex-audit:
	sh scripts/cmutex-audit.sh

# Every switch out of a coroutine is a park naming its wake sources: the collector judges a parked one by them.
park-audit:
	sh scripts/park-audit.sh

# A heap object's edges are slots written only through the store primitives (design §4, «Запись в слот»).
slot-audit:
	python3 scripts/slot-audit.py

# docs/open.md is generated from the [ ]/[~] marks of docs/design/ and docs/notes/; the audit fails when it is stale.
open-items:
	python3 scripts/open-items.py

open-items-audit:
	python3 scripts/open-items.py --check

# The stub-generator measurement of design §10 step 8: how much of a Swift module's public API reprints
# into a compilable stub, and what the tail is made of. Rewrites docs/swift-reprint.md, which is committed.
# NOT a gate, and must not become one: the numbers come from the installed SDK and toolchain, so they move
# under an Xcode update and differ on another machine — the committed report names the versions it used.
# Needs network on a cold run (the SwiftPM probe package) and about ten minutes (SwiftUI's graph is 450 MB).
swift-reprint:
	python3 scripts/swift-reprint.py --work $(BUILD_ROOT)/swift-reprint --out docs/swift-reprint.md \
		Foundation SwiftUI \
		--spm https://github.com/apple/swift-argument-parser.git=1.5.0=ArgumentParser \
		--spm https://github.com/apple/swift-collections.git=1.1.0=OrderedCollections

# The iOS build of the runtime, the simulator run of the probe and the binary-size baseline of design §10
# (docs/notes/ios.md). NOT a gate: it needs an iOS SDK and a booted simulator, and the sizes move with the SDK.
ios-probe:
	sh scripts/ios-sizes.sh

# The .app of design §10 step 8: the same runtime under UIApplicationMain, with its screen in Clojure through the
# level-1 bridge (docs/notes/ios.md). NOT a gate either: it needs an iOS SDK and a booted simulator.
ios-app:
	sh scripts/ios-app.sh

# @ai-generated(solo)
gates:
	+@sh scripts/gates.sh $(MAKE) test test-compiled corpus-compiled fuzz shake facts-report port-audit c-only-audit cmutex-audit park-audit slot-audit open-items-audit api-diff

# @ai-generated(solo)
gates-full:
	+@sh scripts/gates.sh $(MAKE) test test-compiled corpus-compiled fuzz shake facts-report port-audit c-only-audit cmutex-audit park-audit slot-audit open-items-audit api-diff test-isolated test-compiled-asan

# ---- the differential fuzzer (fuzz/, docs/notes/fuzzing.md)

.PHONY: fuzz fuzz-long

# Random expressions over core functions and generated collections against JVM Clojure as the oracle, compared
# through one canonical text per value. Seeded: a finding prints its seed and the form it shrank to.
FUZZ_SEEDS ?= 1-8
FUZZ_FORMS ?= 1000
FUZZ = clojure -Sdeps '$(JVM_DEPS)' -M fuzz/differential.clj
FUZZ_INTERP = --runner interp=$(abspath $(PLAIN))/debug/clj-fuzz
FUZZ_NOREUSE = --runner noreuse=$(abspath $(BUILD_ROOT))/noreuse/debug/clj-fuzz
FUZZ_COMPILED = --runner compiled="CLJ_EVAL=compiled CLJ_EVAL_ROOT=$(PWD) $(abspath $(PLAIN))/debug/clj-fuzz"

# The gate: the committed regressions, then a bounded seeded pass of the interpreter against the oracle. The
# compiled backend pays a clang run per form (~1 form/s), so it stays in fuzz-long, as test-eval-compiled does.
fuzz:
	swift build --scratch-path $(PLAIN) --product clj-fuzz
	$(FUZZ) replay $(FUZZ_INTERP)
	$(FUZZ) run --seeds $(FUZZ_SEEDS) --forms $(FUZZ_FORMS) $(FUZZ_INTERP)

# Opt-in, by hand, in the background: all four runners of design §3 item 2, the compiled pair on few seeds.
fuzz-long:
	swift build --scratch-path $(PLAIN) --product clj-fuzz
	swift build --scratch-path $(BUILD_ROOT)/noreuse -Xcc -DCLJ_NO_REUSE --product clj-fuzz
	$(FUZZ) run --seeds 1-64 --forms 1000 $(FUZZ_INTERP) $(FUZZ_NOREUSE)
	$(FUZZ) run --seeds 1-4 --forms 300 --group 25 $(FUZZ_INTERP) $(FUZZ_COMPILED)
