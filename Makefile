.PHONY: port-audit build boot bench facts-report test test-pool test-ubsan test-noreuse test-all test-isolated corpus corpus-update api-diff test-compiled corpus-compiled test-eval-compiled

# A test that crashes ends with its trace and a nonzero exit; the default death waits on the crash reporter, which
# can leave the helper unkillable (NOTES.md, "Guard").
export CLJ_CRASH_EXIT ?= 1
export ASAN_OPTIONS ?= abort_on_error=0

build:
	swift build

# Re-embeds boot/core.clj and regenerates the compiled core (boot/core.c, boot/libs_*.c) from an interpreted
# boot; a test checks the embedded bytes against the file, so commit all of them.
boot:
	sh scripts/embed-core.sh
	swift build --product clj-compile
	./.build/debug/clj-compile --core --out Sources/CljCore/boot

# ASan sees object boundaries only with the system allocator. The suite is swift-testing only; the XCTest discovery
# helper loads the ASan-linked bundle without the runtime first and dies, so it is skipped.
test:
	CLJ_SYSTEM_ALLOC=1 swift test --sanitize=address --disable-xctest

test-pool:
	swift test

test-ubsan:
	swift test --sanitize=undefined

# The §7 invariant: clj_is_unique always false, so every in-place path degrades to a copy (NOTES.md, RC).
test-noreuse:
	swift test -Xcc -DCLJ_NO_REUSE

# Every mode; each rebuilds, so this is the slow one.
test-all: test test-pool test-ubsan test-noreuse

# Every suite alone, one process each: a live-object baseline that only holds after another suite's one-time
# allocations fails here and not in the full run. Periodic, not a gate (NOTES.md, "Symbol / keyword").
test-isolated:
	swift build --build-tests
	@fail=0; for s in $$(grep -ho '@Suite[^ ]* struct [A-Za-z]*' Tests/PippinTests/*.swift | awk '{print $$3}' | grep -v '^CoreTests$$'); do \
		if swift test --skip-build --filter "$$s" > /dev/null 2>&1; then echo "ok   $$s"; else echo "FAIL $$s"; fail=1; fi; \
	done; exit $$fail

# The acceptance corpus alone (it is part of every test run; CLJ_CORPUS=0 skips it there).
corpus:
	swift test --filter CorpusTests

# Rewrites corpus/*/allowlist.edn and docs/corpus.md from the run; review the diff before committing.
corpus-update:
	CLJ_CORPUS_UPDATE=1 swift test --filter CorpusTests

# ---- the compiler (Sources/CljCompiler, NOTES.md "Compiler")

# The whole suite on the compiled core.clj (boot/core.c, boot/libs_*.c): pool and ASan modes.
test-compiled:
	swift test -Xcc -DCLJ_COMPILED_CORE
	CLJ_SYSTEM_ALLOC=1 swift test -Xcc -DCLJ_COMPILED_CORE --sanitize=address --disable-xctest

# Both corpora through compiled user code: clj-compile per library, clang per file, dlopen; the per-test report
# must match the interpreter's line by line.
corpus-compiled:
	swift build --product clj-compile
	rm -rf .build/corpus-report
	CLJ_CORPUS_REPORT=.build/corpus-report/interpreted swift test --filter CorpusTests
	CLJ_CORPUS_COMPILED=1 CLJ_CORPUS_REPORT=.build/corpus-report/compiled swift test --filter CorpusTests
	diff -ru .build/corpus-report/interpreted .build/corpus-report/compiled && echo "corpus: compiled == interpreted"

# Every rt.eval of the suite through emit, clang and dlopen: slow, opt-in; CLJ_EVAL_CLOSED=1 for --closed.
test-eval-compiled:
	CLJ_EVAL=compiled CLJ_EVAL_ROOT=$(PWD) CLJ_CORPUS=0 swift test

# The type-coverage metric of design §10 step 3b: loads core.clj, the embedded libs and every corpus library,
# analyzes every form again and runs the facts pass over it. Rewrites docs/facts-coverage.md, which is committed.
facts-report:
	swift build -c release --product clj-facts
	./.build/release/clj-facts . docs/facts-coverage.md

# clojure.core parity: the JVM's ns-publics, ours, and the diff weighted by the corpus (scripts/api-diff.clj).
# Needs JVM Clojure on PATH; writes docs/api-parity.md, which is committed.
api-diff:
	@mkdir -p .build/api
	clojure -M scripts/api-diff.clj dump-jvm > .build/api/jvm.edn
	swift run clj-api-dump > .build/api/ours.edn
	swift run clj-api-dump clojure.core.async > .build/api/ours-async.edn
	clojure -M scripts/api-diff.clj diff .build/api/jvm.edn .build/api/ours.edn corpus docs/api-parity.md .build/api/ours-async.edn

# Same binary twice: pool, then system malloc as the control. Compare only within one invocation.
bench:
	swift build -c release --product clj-bench
	./.build/release/clj-bench
	@echo
	CLJ_SYSTEM_ALLOC=1 ./.build/release/clj-bench

# Every file using a platform-specific API must have a row in docs/portability.md (other platforms are the last goal).
port-audit:
	sh scripts/port-audit.sh
