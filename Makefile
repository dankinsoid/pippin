.PHONY: build test test-ubsan

build:
	swift build

test:
	swift test --sanitize=address

test-ubsan:
	swift test --sanitize=undefined
