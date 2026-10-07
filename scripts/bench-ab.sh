#!/bin/sh
# @ai-generated(solo)
# One job for both sides: two jobs land on two runners, and runner noise swamps the difference.
# ONLY's "default" is the full run without CLJ_BENCH_ONLY.
set -eu

BASE=${BASE:-main}
ROUNDS=${ROUNDS:-3}
ONLY=${ONLY:-rc-main default}
OUT=${BUILD_ROOT:-.build}/bench-ab
BASE_SRC=$OUT/base-src
mkdir -p "$OUT"

# CI checks out one commit, so the base may exist only on the remote.
if ! rev=$(git rev-parse --verify --quiet "$BASE^{commit}"); then
	git fetch --no-tags --depth=1 origin "$BASE"
	rev=$(git rev-parse --verify FETCH_HEAD^{commit})
fi

git worktree remove --force "$BASE_SRC" 2>/dev/null || rm -rf "$BASE_SRC"
git worktree add --detach "$BASE_SRC" "$rev" >/dev/null
trap 'git worktree remove --force "$BASE_SRC" 2>/dev/null || true' EXIT

# This tree's harness on both sides, so a selector added here (rc-main) runs on the base too.
# The marked block reads debug counters this tree defines; the base keeps its own rc-share.
sed '/bench-ab: head only {/,/bench-ab: }/d' Sources/clj-bench/main.swift >"$BASE_SRC/Sources/clj-bench/main.swift"
swift build --scratch-path "$OUT/head" -c release --product clj-bench
if ! swift build --package-path "$BASE_SRC" --scratch-path "$OUT/base" -c release --product clj-bench; then
	echo "bench-ab: this harness does not build against $BASE; the base runs its own"
	git -C "$BASE_SRC" checkout -- Sources/clj-bench/main.swift
	swift build --package-path "$BASE_SRC" --scratch-path "$OUT/base" -c release --product clj-bench
fi

echo "bench-ab: base $BASE = $rev, head = $(git rev-parse HEAD)"
echo "bench-ab: $(uname -m), $(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo unknown cpu), $(sysctl -n hw.ncpu 2>/dev/null || echo '?') cpus"

i=1
while [ "$i" -le "$ROUNDS" ]; do
	for side in base head; do
		for sel in $ONLY; do
			echo
			echo "=== bench-ab round $i, $side, $sel"
			if [ "$sel" = default ]; then
				"$OUT/$side/release/clj-bench"
			else
				CLJ_BENCH_ONLY=$sel "$OUT/$side/release/clj-bench"
			fi
		done
	done
	i=$((i + 1))
done
