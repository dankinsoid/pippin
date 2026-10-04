#!/bin/sh
# @ai-generated(solo)
# The whole-program closed build of design §10 step 6 (NOTES.md, "Compiler": tree shaking).
set -eu

APP=${APP:-Tests/PippinTests/Fixtures/shake/app.clj}
# The negative run drops this def, which the program calls: proof that reaching a dropped def is fatal.
LIVE_DEF=${LIVE_DEF:-clojure.core/frequencies}
BUILD_ROOT=${BUILD_ROOT:-.build}
WORK=$BUILD_ROOT/shake
CLJ_COMPILE=${CLJ_COMPILE:-$(pwd)/$BUILD_ROOT/plain/debug/clj-compile}
INTERPRETED=${INTERPRETED:-$(pwd)/$BUILD_ROOT/plain/debug/clj-load}

# $WORK/scratch and $WORK/tree are kept: a cold build of a 1 MB core.c is 15 s and three of them run here.
rm -rf "$WORK/before" "$WORK/after" "$WORK/dropped" "$WORK/out"
mkdir -p "$WORK/before" "$WORK/after" "$WORK/dropped" "$WORK/out" "$WORK/tree"

"$CLJ_COMPILE" --core --closed --no-shake --stats --out "$WORK/before" --file "$APP" >/dev/null 2>"$WORK/out/stats-before.txt"
"$CLJ_COMPILE" --core --closed --stats --out "$WORK/after" --file "$APP" >/dev/null 2>"$WORK/out/stats-after.txt"
"$CLJ_COMPILE" --core --closed --shake-drop "$LIVE_DEF" --out "$WORK/dropped" --file "$APP" >/dev/null 2>"$WORK/out/stats-dropped.txt"

# Only the sources, with the generated boot swapped for the run's own: Package.swift names boot/*.c by path.
sync_tree() {
	rsync -a --delete --exclude '.build' --exclude '.git' Sources Tests Package.swift Package.resolved "$WORK/tree/"
	rm -f "$WORK/tree/Sources/CljCore/boot/core.c" "$WORK/tree/Sources/CljCore/boot/libs.c" "$WORK/tree/Sources/CljCore/boot/libs_"*.c
	cp "$1"/*.c "$WORK/tree/Sources/CljCore/boot/"
}

# SHAKE_RELEASE=1 measures the shipping shape, which an app dead-strips: the only honest place to read the win.
if [ "${SHAKE_RELEASE:-0}" = 1 ]; then
	CONFIG="-c release -Xlinker -dead_strip"
	BIN=release
else
	CONFIG=
	BIN=debug
fi

build_and_run() {
	sync_tree "$1"
	# shellcheck disable=SC2086
	swift build --package-path "$WORK/tree" --scratch-path "$WORK/scratch" --product clj-load $CONFIG \
		-Xcc -DCLJ_COMPILED_CORE -Xcc -DCLJ_CLOSED >"$WORK/out/build-$2.txt" 2>&1 ||
		{ cat "$WORK/out/build-$2.txt"; echo "shake: the $2 build did not compile"; exit 1; }
	cp "$WORK/scratch/$BIN/clj-load" "$WORK/out/clj-load-$2"
	set +e
	# Through another sh, so the fatal of the force-dropped run is a status and not an "Abort trap" on our stderr.
	sh -c '"$1" "$2" >"$3" 2>"$4"' sh "$WORK/out/clj-load-$2" "$APP" "$WORK/out/run-$2.txt" "$WORK/out/run-$2.err" 2>/dev/null
	echo $? >"$WORK/out/run-$2.status"
	set -e
}

# The segment is carried because __const lives in two of them, and a bare name would match both.
sections() { /usr/bin/size -m "$1" | awk '/^Segment /{s=$2} /^\tSection /{gsub(":","",$2); print s $2, $3}'; }

"$INTERPRETED" "$APP" >"$WORK/out/run-interpreted.txt" 2>&1

build_and_run "$WORK/before" before
build_and_run "$WORK/after" after
build_and_run "$WORK/dropped" dropped

fail=0
if [ "$(cat "$WORK/out/run-after.status")" != 0 ]; then
	cat "$WORK/out/run-after.err"
	echo "shake: the shaken build failed to run"
	fail=1
fi
diff -u "$WORK/out/run-interpreted.txt" "$WORK/out/run-after.txt" ||
	{ echo "shake: the shaken build does not print what the interpreter prints"; fail=1; }
diff -u "$WORK/out/run-before.txt" "$WORK/out/run-after.txt" >/dev/null ||
	{ echo "shake: the shaken build does not print what the unshaken one prints"; fail=1; }

# A dropped def the program calls must be a fatal naming it, which is what licenses shaking at all.
if [ "$(cat "$WORK/out/run-dropped.status")" = 0 ]; then
	echo "shake: $LIVE_DEF was dropped and the program still succeeded, so the tripwire did not fire"
	fail=1
elif ! grep -q "$LIVE_DEF was dropped by --closed tree shaking" "$WORK/out/run-dropped.err"; then
	echo "shake: $LIVE_DEF was dropped and the run failed without naming it:"
	tail -5 "$WORK/out/run-dropped.err"
	fail=1
fi

filesize() { wc -c <"$1" | tr -d ' '; }
total() { cat "$1"/*.c | wc -c | tr -d ' '; }
printf '\nshake: %s\n' "$APP"
sed -n 's/^shake: /  /p' "$WORK/out/stats-after.txt"
printf '  core.c          %s -> %s bytes\n' "$(filesize "$WORK/before/core.c")" "$(filesize "$WORK/after/core.c")"
printf '  every unit      %s -> %s bytes\n' "$(total "$WORK/before")" "$(total "$WORK/after")"
printf '  clj-load binary %s -> %s bytes\n' "$(filesize "$WORK/out/clj-load-before")" "$(filesize "$WORK/out/clj-load-after")"
if [ "${SHAKE_RELEASE:-0}" = 1 ]; then
	sections "$WORK/out/clj-load-before" >"$WORK/out/sections-before.txt"
	sections "$WORK/out/clj-load-after" >"$WORK/out/sections-after.txt"
	while read -r name bytes; do
		printf '  %-22s %s -> %s bytes\n' "$name" "$bytes" "$(awk -v n="$name" '$1 == n { print $2 }' "$WORK/out/sections-after.txt")"
	done <"$WORK/out/sections-before.txt"
fi
grep '^survivors: Sources/CljCore/boot/core.clj' "$WORK/out/stats-after.txt" | cut -c1-400

[ "$fail" = 0 ] || exit 1
echo "shake: the shaken whole-program build matches the interpreter, and a dropped live def is fatal"
