#!/bin/sh
# @ai-generated(solo)
# The iOS binary-size numbers of design §10, and the probe run that backs them (docs/notes/ios.md).

# SwiftPM owns the compile flags; the executable link is by hand, as SwiftPM has no iOS executable product.

#   sh scripts/ios-sizes.sh                     every slice and mode, with a simulator run
#   sh scripts/ios-sizes.sh iphonesimulator compiled

# CLJ_IOS_SIM names the simulator (a udid or a name); the booted one, else "iPhone 17".
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${CLJ_IOS_WORK:-$ROOT/.build/ios}
CONFIG=${CLJ_IOS_CONFIG:-release}
SIM=${CLJ_IOS_SIM:-}
DEPLOY=15.0

one() {
	sdk=$1
	mode=$2
	case $sdk in
	iphonesimulator) triple=arm64-apple-ios$DEPLOY-simulator ;;
	iphoneos) triple=arm64-apple-ios$DEPLOY ;;
	*)
		echo "unknown sdk $sdk" >&2
		exit 2
		;;
	esac
	case $mode in
	interpreted) core="" ;;
	compiled) core="-DCLJ_COMPILED_CORE" ;;
	*)
		echo "unknown mode $mode" >&2
		exit 2
		;;
	esac
	sdkroot=$(xcrun --sdk "$sdk" --show-sdk-path)
	scratch=$WORK/$sdk-$mode
	out=$WORK/ios-probe-$sdk-$mode

	echo "=== $sdk / $mode / $CONFIG ($triple)"
	# Pippin pulls CljCore in, so one build serves both probes.
	set -- --scratch-path "$scratch" -c "$CONFIG" --target Pippin \
		-Xcc -target -Xcc "$triple" -Xcc -isysroot -Xcc "$sdkroot" \
		-Xswiftc -target -Xswiftc "$triple" -Xswiftc -sdk -Xswiftc "$sdkroot"
	if [ -n "$core" ]; then set -- "$@" -Xcc "$core"; fi
	(cd "$ROOT" && swift build "$@" >/dev/null)

	# SwiftPM emits no .a for a C target it is not linking into a product, so the objects are gathered by hand.
	objs=$(find "$scratch" -name '*.o' -path '*CljCore.build*' | sort)
	swiftobjs=$(find "$scratch" -name '*.o' -path '*Pippin.build*' | sort)
	mods=$(dirname "$(find "$scratch" -name 'Pippin.swiftmodule' | head -1)")
	mkdir -p "$WORK"
	link "$out" "$triple" "$sdkroot" "$core" "$objs"
	# An app links with dead stripping on, so that is the baseline tree shaking is measured against (design §10).
	link "$out-dead-strip" "$triple" "$sdkroot" "$core" "$objs" -Wl,-dead_strip
	swift_link "$out-swift" "$triple" "$sdkroot" "$mods" "$objs $swiftobjs"
}

link() {
	image=$1
	shift
	t=$1 sr=$2 c=$3 o=$4
	shift 4
	xcrun clang -target "$t" -isysroot "$sr" -I"$ROOT/Sources/CljCore/include" $c \
		-O2 -fno-omit-frame-pointer "$@" \
		-o "$image" "$ROOT/scripts/ios-probe.c" $o \
		-framework CoreFoundation -framework Foundation -lobjc
	echo "$(basename "$image"): $(wc -c <"$image" | tr -d ' ') bytes"
	xcrun size -m "$image" | sed 's/^/  /'
}

# Swift's own runtime ships with iOS 12.2 and later, so none of it lands in the binary.
swift_link() {
	image=$1 t=$2 sr=$3 m=$4 o=$5
	xcrun swiftc -target "$t" -sdk "$sr" -O -I "$m" \
		-Xcc -fmodule-map-file="$(dirname "$m")/CljCore.build/module.modulemap" \
		-o "$image" "$ROOT/scripts/ios-probe.swift" $o \
		-Xlinker -dead_strip \
		-framework CoreFoundation -framework Foundation -lobjc
	echo "$(basename "$image"): $(wc -c <"$image" | tr -d ' ') bytes"
	xcrun size -m "$image" | sed 's/^/  /'
}

run_sim() {
	out=$1
	if [ -z "$SIM" ]; then
		SIM=$(xcrun simctl list devices booted | sed -n 's/.*(\([0-9A-F-]\{36\}\)) (Booted).*/\1/p' | head -1)
	fi
	[ -n "$SIM" ] || SIM="iPhone 17"
	xcrun simctl boot "$SIM" 2>/dev/null || true
	xcrun simctl bootstatus "$SIM" -b >/dev/null 2>&1 || true
	echo "--- run on $SIM"
	xcrun simctl spawn "$SIM" "$out"
}

if [ $# -ge 2 ]; then
	one "$1" "$2"
	if [ "$1" = iphonesimulator ]; then
		run_sim "$WORK/ios-probe-$1-$2"
		run_sim "$WORK/ios-probe-$1-$2-swift"
	fi
	exit 0
fi

for mode in interpreted compiled; do
	for sdk in iphonesimulator iphoneos; do
		one "$sdk" "$mode"
	done
done
for mode in interpreted compiled; do
	run_sim "$WORK/ios-probe-iphonesimulator-$mode"
	run_sim "$WORK/ios-probe-iphonesimulator-$mode-swift"
done
