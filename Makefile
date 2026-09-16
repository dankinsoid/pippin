.PHONY: build boot bench test test-pool test-ubsan test-all corpus corpus-update api-diff

build:
	swift build

# Re-embeds boot/core.clj; a test checks the embedded bytes against the file, so commit both.
boot:
	sh scripts/embed-core.sh

# ASan sees object boundaries only with the system allocator.
test:
	CLJ_SYSTEM_ALLOC=1 swift test --sanitize=address

test-pool:
	swift test

test-ubsan:
	swift test --sanitize=undefined

# The §7 invariant: clj_is_unique always false, so every in-place path degrades to a copy (NOTES.md, RC).
test-noreuse:
	swift test -Xcc -DCLJ_NO_REUSE

# Every mode; each rebuilds, so this is the slow one.
test-all: test test-pool test-ubsan test-noreuse

# The acceptance corpus alone (it is part of every test run; CLJ_CORPUS=0 skips it there).
corpus:
	swift test --filter CorpusTests

# Rewrites corpus/*/allowlist.edn and docs/corpus.md from the run; review the diff before committing.
corpus-update:
	CLJ_CORPUS_UPDATE=1 swift test --filter CorpusTests

# clojure.core parity: the JVM's ns-publics, ours, and the diff weighted by the corpus (scripts/api-diff.clj).
# Needs JVM Clojure on PATH; writes docs/api-parity.md, which is committed.
api-diff:
	@mkdir -p .build/api
	clojure -M scripts/api-diff.clj dump-jvm > .build/api/jvm.edn
	swift run clj-api-dump > .build/api/ours.edn
	clojure -M scripts/api-diff.clj diff .build/api/jvm.edn .build/api/ours.edn corpus docs/api-parity.md

# Same binary twice: pool, then system malloc as the control. Compare only within one invocation.
bench:
	swift build -c release --product clj-bench
	./.build/release/clj-bench
	@echo
	CLJ_SYSTEM_ALLOC=1 ./.build/release/clj-bench
