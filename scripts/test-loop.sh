#!/bin/sh
# @ai-generated(solo)
# A flake of one process in hundreds shows only in hundreds of processes (docs/notes/gates.md, "Loops").
# LOOP_SAN: thread, address or empty; LOOP_ENV: NAME=value words for every process.
set -u
cd "$(dirname "$0")/.."

FILTER=${LOOP_FILTER:?LOOP_FILTER names the tests, as swift test --filter does}
RUNS=${LOOP_RUNS:-100}
JOBS=${LOOP_JOBS:-3}
SAN=${LOOP_SAN:-}
TIMEOUT=${TEST_TIMEOUT:-1200}
SCRATCH=.build/loop${SAN:+-$SAN}
SAN_ARGS=""
if [ -n "$SAN" ]; then
	SAN_ARGS="--sanitize=$SAN"
	export CLJ_SYSTEM_ALLOC=1
fi
[ "$SAN" = thread ] && export TSAN_OPTIONS="abort_on_error=0 suppressions=$(pwd)/scripts/tsan.supp"
for kv in ${LOOP_ENV:-}; do export "$kv"; done
LOGS=$SCRATCH/loop-logs
rm -rf "$LOGS"
mkdir -p "$LOGS"
export FILTER TIMEOUT SCRATCH SAN_ARGS LOGS

swift build --build-tests --scratch-path "$SCRATCH" $SAN_ARGS || exit 1

start=$(date +%s)
seq 1 "$RUNS" | xargs -P "$JOBS" -I{} sh -c '
	timeout -k 5 "$TIMEOUT" swift test --skip-build --disable-xctest --scratch-path "$SCRATCH" $SAN_ARGS \
		--filter "$FILTER" > "$LOGS/run-{}.log" 2>&1
	echo $? > "$LOGS/run-{}.rc"'

failed=0
for rc in "$LOGS"/run-*.rc; do
	[ "$(cat "$rc")" = 0 ] && continue
	failed=$((failed + 1))
	log=${rc%.rc}.log
	echo "loop: $(basename "$log"): exit $(cat "$rc")"
	grep -E '✘|cc trace|^  [0-9]+ [FPWKR] |fatal|ICE|ThreadSanitizer|AddressSanitizer|hang:' "$log" | head -80
done
echo "loop: $failed of $RUNS processes failed, $(($(date +%s) - start)) s, filter $FILTER${SAN:+, $SAN}"
[ "$failed" = 0 ]
