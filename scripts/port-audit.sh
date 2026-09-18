#!/bin/sh
# @ai-generated(solo)
# Fails when a source file uses a platform-specific API and docs/portability.md does not list it.
set -e
cd "$(dirname "$0")/.."
markers='__APPLE__|getsectiondata|pthread_get_stackaddr_np|arc4random|os_unfair_lock|mach_absolute_time|os_signpost|_dyld_|sigaltstack|ucontext_t|xcrun|dlopen\(|TARGET_OS_|__MACH__'
found=$(grep -rlE "$markers" Sources Makefile Package.swift 2>/dev/null | grep -v '^Sources/CljCore/boot/' | sort -u)
missing=0
for f in $found; do
	base=$(basename "$f")
	if ! grep -q "$base" docs/portability.md; then
		echo "port-audit: $f uses a platform-specific API but is not in docs/portability.md"
		missing=1
	fi
done
[ $missing -eq 0 ] && echo "port-audit: every platform-specific file is listed ($(echo "$found" | wc -l | tr -d ' ') files)"
exit $missing
