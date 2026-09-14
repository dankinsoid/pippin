.PHONY: build boot bench test test-pool test-ubsan

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

# Same binary twice: pool, then system malloc as the control. Compare only within one invocation.
bench:
	swift build -c release --product clj-bench
	./.build/release/clj-bench
	@echo
	CLJ_SYSTEM_ALLOC=1 ./.build/release/clj-bench
