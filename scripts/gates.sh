#!/bin/sh
# @ai-generated(solo)
set -eu
make_command=$1
shift
started=$(date +%s)
for gate do
	step=$(date +%s)
	printf '\n=== %s ===\n' "$gate"
	status=0
	"$make_command" "$gate" || status=$?
	printf 'gate %s: %s s (exit %s)\n' "$gate" "$(($(date +%s) - step))" "$status"
	if [ "$status" -ne 0 ]; then exit "$status"; fi
done
printf 'gates total: %s s\n' "$(($(date +%s) - started))"
