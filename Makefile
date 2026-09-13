.PHONY: build bench test test-pool test-ubsan

build:
	swift build

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
