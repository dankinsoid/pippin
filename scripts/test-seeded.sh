#!/bin/sh
# @ai-generated(solo)
# The concurrency suites under the seeded scheduler, a process per seed (NOTES "Scheduler"); SEEDS, SUITES override.
set -u
cd "$(dirname "$0")/.."

SCRATCH=${SCRATCH:-.build/plain}
# Commas too: make_args on CI splits on spaces.
SEEDS=$(echo "${SEEDS:-1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16}" | tr ',' ' ')
# Virtual time makes a seeded suite fast; a hang is reported (and sampled) early, not after CI's 900 s.
export CLJ_TEST_HANG_S=${SEEDED_HANG_S:-180}
SUITES=${SUITES:-ChanTests ChanStressTests AsyncLibTests CoroTests CycleTests AgentTests RefTests FutureTests DeadlineTests CmutexTests SeededTests}
TIMEOUT=${TEST_TIMEOUT:-1200}
filter="CoreTests/($(echo $SUITES | tr ' ' '|'))/"
# Under shards/, which CI uploads with the shard logs.
logs=$SCRATCH/shards/seeded
mkdir -p "$logs"

swift build --build-tests --scratch-path "$SCRATCH" || exit 1

failed=""
first=""
for seed in $SEEDS; do
	log=$logs/seed-$seed.log
	start=$(date +%s)
	CLJ_SCHED_SEED=$seed timeout -k 5 "$TIMEOUT" swift test --skip-build --disable-xctest --scratch-path "$SCRATCH" --filter "$filter" > "$log" 2>&1
	rc=$?
	echo "seeded: seed $seed: exit $rc, $(($(date +%s) - start)) s"
	[ -z "$first" ] && first=$seed
	[ "$rc" = 0 ] && continue
	failed="$failed $seed"
	grep -E '✘|fatal|seeded scheduler|hang:|^sched:|^  carrier|^coro' "$log" | head -60
	# A test is reseeded from the process seed and its own name, so it replays alone (EvalSupport.swift).
	tests=$(sed -n 's/.*✘ Test \([A-Za-z0-9_]*\)() recorded an issue at \([A-Za-z0-9_]*\)\.swift.*/\2\/\1/p' "$log" | sort -u)
	for t in $tests; do
		echo "replay: CLJ_SCHED_SEED=$seed swift test --disable-xctest --scratch-path $SCRATCH --filter '$t'"
	done
	[ -n "$tests" ] || echo "replay: CLJ_SCHED_SEED=$seed swift test --disable-xctest --scratch-path $SCRATCH --filter '$filter'"
done

# SeededTests reseeds every run itself, so its lines must read the same under every process seed.
mismatch=""
for seed in $SEEDS; do
	[ "$seed" = "$first" ] && continue
	grep '^seeded: ' "$logs/seed-$first.log" > "$logs/lines-first"
	grep '^seeded: ' "$logs/seed-$seed.log" > "$logs/lines-other"
	cmp -s "$logs/lines-first" "$logs/lines-other" || mismatch="$mismatch $seed"
done
grep '^seeded: ' "$logs/seed-$first.log" | grep -v 'order-log'

[ -z "$mismatch" ] || echo "seeded: SeededTests printed otherwise under seeds$mismatch than under $first"
[ -z "$failed" ] || echo "seeded: failed under seeds$failed"
[ -z "$failed$mismatch" ]
