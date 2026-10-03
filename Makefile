.PHONY: port-audit c-only-audit cmutex-audit open-items open-items-audit load-asan build boot bench facts-report test test-pool test-ubsan test-noreuse test-all test-isolated corpus corpus-update api-diff test-compiled corpus-compiled test-eval-compiled test-compiled-asan swift-reprint gates gates-full

# A test that crashes ends with its trace and a nonzero exit; the default death waits on the crash reporter, which
# can leave the helper unkillable (NOTES.md, "Guard").
export CLJ_CRASH_EXIT ?= 1
export ASAN_OPTIONS ?= abort_on_error=0
# The corpus watchdog catches a spinning deftest (NOTES "Corpus"); under ASan beside other shards one takes 4-6 s.
export CLJ_CORPUS_TIMEOUT_MS ?= 20000

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
test-eval-compiled:
	CLJ_EVAL=compiled CLJ_EVAL_ROOT=$(PWD) CLJ_CORPUS=0 $(SHARDS) --gate test-eval-compiled -- --scratch-path $(PLAIN)

# The type-coverage metric of design §10 step 3b: loads core.clj, the embedded libs and every corpus library,
# analyzes every form again and runs the facts pass over it. Rewrites docs/facts-coverage.md, which is committed.
facts-report:
	swift build --scratch-path $(RELEASE) -c release --product clj-facts
	$(RELEASE)/release/clj-facts . docs/facts-coverage.md

# core.async is not on the default classpath, and its publics are what the async half of the parity report diffs against.
ASYNC_DEPS = {:deps {org.clojure/core.async {:mvn/version "1.6.681"}}}

# clojure.core parity: the JVM's ns-publics, ours, and the diff weighted by the corpus (scripts/api-diff.clj).
# Needs JVM Clojure on PATH; writes docs/api-parity.md, which is committed, and fails on an unmarked extension.
api-diff:
	@mkdir -p $(PLAIN)/api
	clojure -M scripts/api-diff.clj dump-jvm > $(PLAIN)/api/jvm.edn
	clojure -Sdeps '$(ASYNC_DEPS)' -M scripts/api-diff.clj dump-async > $(PLAIN)/api/jvm-async.edn
	swift run --scratch-path $(PLAIN) clj-api-dump > $(PLAIN)/api/ours.edn
	swift run --scratch-path $(PLAIN) clj-api-dump clojure.core.async > $(PLAIN)/api/ours-async.edn
	clojure -M scripts/api-diff.clj diff $(PLAIN)/api/jvm.edn $(PLAIN)/api/ours.edn corpus docs/api-parity.md $(PLAIN)/api/ours-async.edn $(PLAIN)/api/jvm-async.edn

# Same binary twice: pool, then system malloc as the control. Compare only within one invocation.
bench:
	swift build --scratch-path $(RELEASE) -c release --product clj-bench
	$(RELEASE)/release/clj-bench
	@echo
	CLJ_SYSTEM_ALLOC=1 $(RELEASE)/release/clj-bench

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

# @ai-generated(solo)
gates:
	+@sh scripts/gates.sh $(MAKE) test test-compiled corpus-compiled facts-report port-audit c-only-audit cmutex-audit open-items-audit api-diff

# @ai-generated(solo)
gates-full:
	+@sh scripts/gates.sh $(MAKE) test test-compiled corpus-compiled facts-report port-audit c-only-audit cmutex-audit open-items-audit api-diff test-isolated test-compiled-asan
