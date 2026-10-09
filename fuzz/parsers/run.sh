#!/bin/sh
# @ai-generated(solo)
# usage: run.sh <out dir> <target>=<seconds>...
# Fork mode, so one finding does not end a target's run.
set -u
out=$1
shift
jobs=${FUZZ_PARSERS_JOBS:-$(sysctl -n hw.ncpu)}
max_len=4096
export ASAN_OPTIONS="abort_on_error=0:handle_abort=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
export CLJ_CRASH_EXIT=1

rm -rf "$out/seeds" "$out/findings" "$out/logs"
mkdir -p "$out/logs"
python3 fuzz/parsers/seeds.py . "$out/seeds" || exit 1

summary="$out/logs/summary.txt"
: > "$summary"
found=0
for spec in "$@"; do
	t=${spec%%=*}
	secs=${spec#*=}
	bin="$out/bin/fuzz-$t"
	corpus="$out/corpus/$t"
	findings="$out/findings/$t"
	log="$out/logs/$t.log"
	mkdir -p "$corpus" "$findings"
	echo "fuzz-parsers: $t, ${secs}s on $jobs jobs, corpus $(ls "$corpus" | wc -l | tr -d ' ') units"
	# Every phase under a hard bound: a worker that outlives -max_total_time, or an OOM input whose every
	# minimization attempt fills the RSS limit, must not hold the job.
	timeout -k 30 $((secs + 300)) "$bin" -fork="$jobs" -ignore_crashes=1 -ignore_timeouts=1 -ignore_ooms=1 \
		-max_total_time="$secs" -timeout=30 -rss_limit_mb=2048 -max_len=$max_len -dict="fuzz/parsers/$t.dict" \
		-artifact_prefix="$findings/" "$corpus" "fuzz/parsers/seeds/$t" "$out/seeds/$t" > "$log" 2>&1
	echo "fuzz-parsers: $t exited $?"
	last=$(grep -E '^#[0-9]+: cov:' "$log" | tail -1)
	# The grown corpus replayed alone: the coverage it reaches, independent of the run's jobs.
	replay=$(timeout -k 10 600 "$bin" -runs=0 -timeout=30 -rss_limit_mb=2048 -max_len=$max_len "$corpus" 2>&1 \
		| grep -E 'INITED|DONE' | tail -1)
	echo "fuzz-parsers $t: last status: $last" | tee -a "$summary"
	echo "fuzz-parsers $t: corpus replay: $replay" | tee -a "$summary"
	n=0
	distinct=0
	seen="$out/logs/$t-signatures"
	: > "$seen"
	for f in "$findings"/*; do
		[ -f "$f" ] || continue
		n=$((n + 1))
		found=1
		rep="$out/logs/$t-$(basename "$f").log"
		timeout -k 10 120 "$bin" -timeout=30 -rss_limit_mb=2048 "$f" > "$rep" 2>&1
		# One bug saved many times reproduces with one message; addresses and pids differ, the words do not.
		sig=$(grep -m1 -E 'fuzz finding: |ERROR: |runtime error: |clj: fatal' "$rep" | sed -E 's/0x[0-9a-f]+|==[0-9]+==|[0-9]+Mb//g' | cut -c1-160)
		grep -qxF "$sig" "$seen" && continue
		echo "$sig" >> "$seen"
		distinct=$((distinct + 1))
		[ $distinct -le 4 ] || continue
		timeout -k 10 300 "$bin" -minimize_crash=1 -max_total_time=120 -timeout=30 -rss_limit_mb=2048 \
			-exact_artifact_path="$f.min" "$f" > "$out/logs/$t-$(basename "$f").minimize.log" 2>&1
		[ -f "$f.min" ] || cp "$f" "$f.min"
		timeout -k 10 120 "$bin" -timeout=30 -rss_limit_mb=2048 "$f.min" > "$rep" 2>&1
		echo "---- fuzz-parsers $t finding $(basename "$f"), minimized to $(wc -c < "$f.min" | tr -d ' ') bytes:"
		python3 -c 'import sys; print(repr(open(sys.argv[1], "rb").read()))' "$f.min"
		grep -E 'ERROR: |SUMMARY: |fuzz finding: |runtime error: |clj: fatal|Assertion|deadly signal|timeout after' "$rep" | head -6
		grep -E '^ +#[0-9]+ ' "$rep" | head -14
	done
	echo "fuzz-parsers $t: $distinct distinct of $n findings:" | tee -a "$summary"
	sed 's/^/    /' "$seen" | tee -a "$summary"
done

if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
	{
		echo '### fuzz-parsers'
		echo '```'
		cat "$summary"
		echo '```'
	} >> "$GITHUB_STEP_SUMMARY"
fi
exit $found
