.PHONY: port-audit cmutex-audit load-asan build boot bench facts-report test test-pool test-ubsan test-noreuse test-all test-isolated corpus corpus-update api-diff test-compiled corpus-compiled test-eval-compiled test-compiled-asan gates gates-full

# A test that crashes ends with its trace and a nonzero exit; the default death waits on the crash reporter, which
# can leave the helper unkillable (NOTES.md, "Guard").
export CLJ_CRASH_EXIT ?= 1
export ASAN_OPTIONS ?= abort_on_error=0

BUILD_ROOT ?= .build
PLAIN = $(BUILD_ROOT)/plain
ASAN = $(BUILD_ROOT)/asan
COMPILED = $(BUILD_ROOT)/compiled
COMPILED_ASAN = $(BUILD_ROOT)/compiled-asan
RELEASE = $(BUILD_ROOT)/release
TEST = timeout -k 5 500 swift test
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

# ASan sees object boundaries only with the system allocator. The suite is swift-testing only; the XCTest discovery
# helper loads the ASan-linked bundle without the runtime first and dies, so it is skipped.
test:
	CLJ_SYSTEM_ALLOC=1 $(TEST) --scratch-path $(ASAN) --sanitize=address --disable-xctest

test-pool:
	$(TEST) --scratch-path $(PLAIN)

test-ubsan:
	$(TEST) --scratch-path $(BUILD_ROOT)/ubsan --sanitize=undefined

# The §7 invariant: clj_is_unique always false, so every in-place path degrades to a copy (NOTES.md, RC).
test-noreuse:
	$(TEST) --scratch-path $(BUILD_ROOT)/noreuse -Xcc -DCLJ_NO_REUSE

# Every sanitizer and allocator mode has its own incremental build.
test-all: test test-pool test-ubsan test-noreuse

# Every suite alone, one process each: a live-object baseline that only holds after another suite's one-time
# allocations fails here and not in the full run. Periodic, not a gate (NOTES.md, "Symbol / keyword").
test-isolated:
	swift build --scratch-path $(PLAIN) --build-tests
	@fail=0; for s in $$(grep -ho '@Suite[^ ]* struct [A-Za-z]*' Tests/PippinTests/*.swift | awk '{print $$3}' | grep -v '^CoreTests$$'); do \
		if $(TEST) --scratch-path $(PLAIN) --skip-build --filter "$$s" > /dev/null 2>&1; then echo "ok   $$s"; else echo "FAIL $$s"; fail=1; fi; \
	done; exit $$fail

# The acceptance corpus alone (it is part of every test run; CLJ_CORPUS=0 skips it there).
corpus:
	$(TEST) --scratch-path $(PLAIN) --filter CorpusTests

# Rewrites corpus/*/allowlist.edn and docs/corpus.md from the run; review the diff before committing.
corpus-update:
	CLJ_CORPUS_UPDATE=1 $(TEST) --scratch-path $(PLAIN) --filter CorpusTests

# ---- the compiler (Sources/CljCompiler, NOTES.md "Compiler")

# The whole suite on the compiled core with the pool allocator.
test-compiled:
	$(TEST) --scratch-path $(COMPILED) -Xcc -DCLJ_COMPILED_CORE

test-compiled-asan:
	CLJ_SYSTEM_ALLOC=1 $(TEST) --scratch-path $(COMPILED_ASAN) -Xcc -DCLJ_COMPILED_CORE --sanitize=address --disable-xctest

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
	CLJ_EVAL=compiled CLJ_EVAL_ROOT=$(PWD) CLJ_CORPUS=0 $(TEST) --scratch-path $(PLAIN)

# The type-coverage metric of design §10 step 3b: loads core.clj, the embedded libs and every corpus library,
# analyzes every form again and runs the facts pass over it. Rewrites docs/facts-coverage.md, which is committed.
facts-report:
	swift build --scratch-path $(RELEASE) -c release --product clj-facts
	$(RELEASE)/release/clj-facts . docs/facts-coverage.md

# clojure.core parity: the JVM's ns-publics, ours, and the diff weighted by the corpus (scripts/api-diff.clj).
# Needs JVM Clojure on PATH; writes docs/api-parity.md, which is committed.
api-diff:
	@mkdir -p $(PLAIN)/api
	clojure -M scripts/api-diff.clj dump-jvm > $(PLAIN)/api/jvm.edn
	swift run --scratch-path $(PLAIN) clj-api-dump > $(PLAIN)/api/ours.edn
	swift run --scratch-path $(PLAIN) clj-api-dump clojure.core.async > $(PLAIN)/api/ours-async.edn
	clojure -M scripts/api-diff.clj diff $(PLAIN)/api/jvm.edn $(PLAIN)/api/ours.edn corpus docs/api-parity.md $(PLAIN)/api/ours-async.edn

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

# Every file using a platform-specific API must have a row in docs/portability.md (other platforms are the last goal).
port-audit:
	sh scripts/port-audit.sh

# Every cmutex acquisition must be accounted for: an unregistered one is a suspend! parked holding it.
cmutex-audit:
	sh scripts/cmutex-audit.sh

# @ai-generated(solo)
gates:
	+@sh scripts/gates.sh $(MAKE) test test-compiled corpus-compiled facts-report port-audit cmutex-audit api-diff

# @ai-generated(solo)
gates-full:
	+@sh scripts/gates.sh $(MAKE) test test-compiled corpus-compiled facts-report port-audit cmutex-audit api-diff test-isolated test-compiled-asan
