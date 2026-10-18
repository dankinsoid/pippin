#!/bin/sh
# @ai-generated(solo)
# Level 2's production path (design §5 «Объявленная граница», NOTES "Host bridge"): make swift-closed.
set -eu

BUILD_ROOT=${BUILD_ROOT:-.build}
WORK=$BUILD_ROOT/swift-closed
FIX=Tests/PippinTests/Fixtures/swift
APP=$FIX/app.clj
CLJ_COMPILE=${CLJ_COMPILE:-$(pwd)/$BUILD_ROOT/plain/debug/clj-compile}
PLAIN_BIN=$(dirname "$CLJ_COMPILE")

# The stub cache starts cold, so the times below are a first build's.
rm -rf "$WORK/boot" "$WORK/out" "$WORK/cache" "$WORK/fixture"
mkdir -p "$WORK/boot" "$WORK/out" "$WORK/fixture" "$WORK/tree"

now() { python3 -c 'import time; print(time.time())'; }
since() { python3 -c 'import sys, time; print("%.1f s" % (time.time() - float(sys.argv[1])))' "$1"; }
die() {
	echo "swift-closed: $1"
	exit 1
}
step() { # log, command...
	log=$1
	shift
	"$@" >"$WORK/out/$log" 2>&1 || { cat "$WORK/out/$log"; die "$* failed"; }
}

# One static build of the module: the compile's dev stubs and the app's link take the same archive.
step fixture.txt xcrun swiftc -emit-library -static -emit-module -module-name PippinFixture "$FIX/PippinFixture.swift" \
	-o "$WORK/fixture/libPippinFixture.a" -emit-module-path "$WORK/fixture/PippinFixture.swiftmodule"

# The compile evaluates the file, so its require-swift takes the dev path; run alone first, to time it.
t=$(now)
step stubgen-dev.txt python3 scripts/swift-stubgen.py --module PippinFixture --cache "$WORK/cache" \
	--runtime-modules "$PLAIN_BIN/Modules" --module-map "$PLAIN_BIN/CljCore.build/module.modulemap" \
	-I "$WORK/fixture" -L "$WORK/fixture" -l PippinFixture
dev_time=$(since "$t")

t=$(now)
"$CLJ_COMPILE" --core --closed --out "$WORK/boot" --file "$APP" --swift-stubgen scripts/swift-stubgen.py \
	--swift-cache "$WORK/cache" --swift-module "$WORK/fixture" --swift-lib PippinFixture \
	>"$WORK/out/compile.out" 2>"$WORK/out/compile.txt" || { cat "$WORK/out/compile.txt"; die "clj-compile failed"; }
compile_time=$(since "$t")

python3 -c 'import json, sys; print("\n".join(json.load(open(sys.argv[1]))["PippinFixture"]))' \
	"$WORK/boot/swift-stubs.json" >"$WORK/out/stubs.txt"
diff -u "$FIX/app.stubs" "$WORK/out/stubs.txt" || die "the closed set's Swift symbols are not app.stubs"
grep -q 'clj_host_module_link("PippinFixture", pippin_stubs_register_PippinFixture);' "$WORK/boot/libs.c" ||
	die "libs.c does not link PippinFixture's register entry"

# Only the sources, with the generated boot swapped for this build's: Package.swift names boot/*.c by path.
rsync -a --delete --exclude '.build' --exclude '.git' Sources Tests Package.swift Package.resolved "$WORK/tree/"
rm -f "$WORK/tree/Sources/CljCore/boot/core.c" "$WORK/tree/Sources/CljCore/boot/libs.c" "$WORK/tree/Sources/CljCore/boot/libs_"*.c
cp "$WORK/boot/"*.c "$WORK/tree/Sources/CljCore/boot/"
set -- --package-path "$WORK/tree" --scratch-path "$WORK/scratch" -Xcc -DCLJ_COMPILED_CORE -Xcc -DCLJ_CLOSED
step build.txt swift build "$@" --target Pippin
BIN=$(swift build "$@" --show-bin-path)

# The stubs are built against this build's Pippin.swiftmodule: its Swift ABI is not resilient (design §5).
t=$(now)
step stubgen-static.txt python3 scripts/swift-stubgen.py --module PippinFixture --cache "$WORK/cache" \
	--runtime-modules "$BIN/Modules" --module-map "$BIN/CljCore.build/module.modulemap" -I "$WORK/fixture" \
	--static --needs "$WORK/boot/swift-stubs.json"
static_time=$(since "$t")
STUBS=$(tail -1 "$WORK/out/stubgen-static.txt")

# SwiftPM has no product for a hand-made host, so the link gathers its objects, as scripts/ios-app.sh does.
# shellcheck disable=SC2046
step link.txt xcrun swiftc -swift-version 5 scripts/swift-app/main.swift -o "$WORK/out/app" \
	-I "$BIN/Modules" -Xcc -fmodule-map-file="$BIN/CljCore.build/module.modulemap" \
	$(find "$BIN/CljCore.build" "$BIN/Pippin.build" -name '*.o' | sort) "$STUBS" "$WORK/fixture/libPippinFixture.a" \
	-framework CoreFoundation -framework Foundation -lobjc

nm "$WORK/out/app" | grep -q ' T _pippin_stubs_register_PippinFixture$' || die "the app does not hold the stubs' register entry"
if otool -L "$WORK/out/app" | grep -q PippinFixture; then die "the app links PippinFixture dynamically"; fi

# From a directory where the entry file does not exist: only the compiled unit, found by its path, can run it.
set +e
(cd "$WORK/out" && DYLD_PRINT_LIBRARIES=1 ./app "$APP" fixture.swift-app/-main >run.txt 2>run.err)
status=$?
set -e
[ "$status" = 0 ] || { grep -v '^dyld\[' "$WORK/out/run.err"; die "the closed app exited $status"; }
diff -u "$FIX/app.out" "$WORK/out/run.txt" || die "the closed app does not print app.out"
grep -q '^dyld\[[0-9]*\]: .*libSystem' "$WORK/out/run.err" || die "DYLD_PRINT_LIBRARIES listed nothing, so no load is checked"
if grep -E 'PippinStubs|PippinFixture' "$WORK/out/run.err"; then die "the closed app loaded a stub or module image"; fi

generated=$(python3 -c 'import glob, json, sys; print(max(len(json.load(open(p))["generated"]) for p in glob.glob(sys.argv[1] + "/*/report.json")))' "$WORK/cache/PippinFixture")
printf '\nswift-closed: %s\n' "$APP"
printf '  stubs linked     %s of the %s PippinFixture generates\n' "$(wc -l <"$WORK/out/stubs.txt" | tr -d ' ')" "$generated"
printf '  dev stubs        %s (symbol graphs, swiftc -emit-library of the whole module, for the compile)\n' "$dev_time"
printf '  clj-compile      %s (--core --closed, the stubs from the cache)\n' "$compile_time"
printf '  static stubs     %s (symbol graphs, swiftc -static -O of the named symbols)\n' "$static_time"
printf '  app              %s bytes, archive %s bytes\n' "$(wc -c <"$WORK/out/app" | tr -d ' ')" "$(wc -c <"$STUBS" | tr -d ' ')"
echo "swift-closed: the closed build calls PippinFixture through linked stubs, with no generator and no dlopen"
