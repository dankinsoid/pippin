#!/bin/sh
# @ai-generated(solo)
# The iOS .app of design §10: a bundle around the C runtime whose whole screen is Clojure (docs/notes/ios.md).

# No Xcode project: SwiftPM compiles the runtime, clang links, plutil writes Info.plist, codesign signs ad hoc.

#   sh scripts/ios-app.sh                       # both modes: built, installed, launched, measured
#   sh scripts/ios-app.sh compiled [iphoneos]   # one mode; the iphoneos slice is built and left unsigned
#   CLJ_APP_KEEP=1 sh scripts/ios-app.sh compiled   # leave the screen standing, to tap it by hand

# CLJ_IOS_SIM names the simulator (a udid or a name); the booted one, else "iPhone 17".
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${CLJ_IOS_WORK:-$ROOT/.build/ios}
CONFIG=${CLJ_IOS_CONFIG:-release}
SIM=${CLJ_IOS_SIM:-}
BUNDLE_ID=${CLJ_APP_BUNDLE_ID:-dev.pippin.app}
DEPLOY=15.0
SETTLE=${CLJ_APP_SETTLE_MS:-3000}
KEEP=${CLJ_APP_KEEP:-}
HEADLESS=${CLJ_IOS_HEADLESS:-}

plist() {
	app=$1
	platform=$2
	cat >"$app/Info.plist" <<-EOF
		<?xml version="1.0" encoding="UTF-8"?>
		<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
		<plist version="1.0">
		<dict>
			<key>CFBundleExecutable</key><string>PippinApp</string>
			<key>CFBundleIdentifier</key><string>$BUNDLE_ID</string>
			<key>CFBundleName</key><string>Pippin</string>
			<key>CFBundleDisplayName</key><string>Pippin</string>
			<key>CFBundlePackageType</key><string>APPL</string>
			<key>CFBundleShortVersionString</key><string>0.1</string>
			<key>CFBundleVersion</key><string>1</string>
			<key>CFBundleSupportedPlatforms</key><array><string>$platform</string></array>
			<key>MinimumOSVersion</key><string>$DEPLOY</string>
			<key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
			<key>UILaunchScreen</key><dict/>
			<key>UISupportedInterfaceOrientations</key>
			<array><string>UIInterfaceOrientationPortrait</string></array>
		</dict>
		</plist>
	EOF
	plutil -lint "$app/Info.plist" >/dev/null
}

# The entitlements are the profile's own, as Xcode takes them (docs/notes/ios.md, "The device run").
sign_device() {
	app=$1
	if [ -z "$CLJ_APP_PROFILE" ]; then
		echo "  unsigned: set CLJ_APP_PROFILE=<file.mobileprovision> (and CLJ_APP_IDENTITY) to sign for a device"
		return 0
	fi
	cp "$CLJ_APP_PROFILE" "$app/embedded.mobileprovision"
	security cms -D -i "$CLJ_APP_PROFILE" >"$WORK/profile.plist"
	plutil -extract Entitlements xml1 -o "$WORK/entitlements.plist" "$WORK/profile.plist"
	appid=$(plutil -extract application-identifier raw -o - "$WORK/entitlements.plist")
	case $appid in
	*".$BUNDLE_ID") ;;
	*)
		echo "  profile is for $appid, bundle is $BUNDLE_ID" >&2
		exit 2
		;;
	esac
	codesign --force --sign "${CLJ_APP_IDENTITY:-Apple Development}" --entitlements "$WORK/entitlements.plist" \
		--generate-entitlement-der --timestamp=none "$app"
	echo "  signed for $appid; install with: xcrun devicectl device install app --device <udid> $app"
	echo "  then: xcrun devicectl device process launch --console --device <udid> $BUNDLE_ID"
}

one() {
	sdk=$1
	mode=$2
	case $sdk in
	iphonesimulator) triple=arm64-apple-ios$DEPLOY-simulator platform=iPhoneSimulator ;;
	iphoneos) triple=arm64-apple-ios$DEPLOY platform=iPhoneOS ;;
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
	app=$WORK/PippinApp-$sdk-$mode.app

	echo "=== $sdk / $mode / $CONFIG ($triple)"
	set -- --scratch-path "$scratch" -c "$CONFIG" --target CljCore \
		-Xcc -target -Xcc "$triple" -Xcc -isysroot -Xcc "$sdkroot" \
		-Xswiftc -target -Xswiftc "$triple" -Xswiftc -sdk -Xswiftc "$sdkroot"
	if [ -n "$core" ]; then set -- "$@" -Xcc "$core"; fi
	(cd "$ROOT" && swift build "$@" >/dev/null)

	# SwiftPM emits no .a for a C target it is not linking into a product, so the objects are gathered by hand.
	objs=$(find "$scratch" -name '*.o' -path '*CljCore.build*' | sort)
	rm -rf "$app"
	mkdir -p "$app"
	# An app links with dead stripping on, which is the baseline of docs/notes/ios.md.
	xcrun clang -target "$triple" -isysroot "$sdkroot" -I"$ROOT/Sources/CljCore/include" $core \
		-O2 -fno-omit-frame-pointer -Wl,-dead_strip \
		-o "$app/PippinApp" "$ROOT/scripts/ios-app/main.c" $objs \
		-framework UIKit -framework CoreFoundation -framework Foundation -lobjc
	cp "$ROOT/scripts/ios-app/screen.clj" "$app/screen.clj"
	# The screen's (:require-c [UIKit ...]) is parsed here, against this SDK: the device has no clang.
	python3 "$ROOT/scripts/c-headergen.py" --scan "$ROOT/scripts/ios-app/screen.clj" \
		--out "$app" --sdk "$sdkroot" --target "$triple" >/dev/null
	plist "$app" "$platform"
	if [ "$sdk" = iphonesimulator ]; then
		codesign --force --sign - --timestamp=none "$app" >/dev/null 2>&1
	else
		sign_device "$app"
	fi
	echo "$(basename "$app"): executable $(wc -c <"$app/PippinApp" | tr -d ' ') bytes, bundle $(du -sk "$app" | cut -f1) KB"
}

run_sim() {
	mode=$1
	app=$WORK/PippinApp-iphonesimulator-$mode.app
	if [ -z "$SIM" ]; then
		SIM=$(xcrun simctl list devices booted | sed -n 's/.*(\([0-9A-F-]\{36\}\)) (Booted).*/\1/p' | head -1)
	fi
	[ -n "$SIM" ] || SIM="iPhone 17"
	xcrun simctl boot "$SIM" 2>/dev/null || true
	xcrun simctl bootstatus "$SIM" -b >/dev/null 2>&1 || true
	# simctl boots headless, so without this the screen exists only in the screenshot.
	[ -n "$HEADLESS" ] || open -a Simulator --args -CurrentDeviceUDID "$SIM" 2>/dev/null || true
	echo "--- install and launch on $SIM ($mode)"
	xcrun simctl install "$SIM" "$app"
	log=$WORK/app-$mode.log
	: >"$log"
	shot=$WORK/screen-$mode.png
	if [ -n "$KEEP" ]; then
		# No CLJ_APP_EXIT: the screen stands until it is closed by hand, which is the only way to tap it.
		SIMCTL_CHILD_CLJ_APP_SETTLE_MS=$SETTLE \
			xcrun simctl launch --terminate-running-process "$SIM" "$BUNDLE_ID" >"$log" 2>&1
		sleep 3
		xcrun simctl io "$SIM" screenshot --type png "$shot" >/dev/null 2>&1 || true
		sed 's/^/  /' "$log"
		echo "  screenshot: $shot"
		echo "  the app is left running; its stdout goes to the system log:"
		echo "    xcrun simctl spawn $SIM log stream --level debug --predicate 'process == \"PippinApp\"'"
		return 0
	fi
	# --console-pty streams the app's stdout and ends when it exits, which CLJ_APP_EXIT makes it do.
	SIMCTL_CHILD_CLJ_APP_SETTLE_MS=$SETTLE SIMCTL_CHILD_CLJ_APP_EXIT=1 \
		xcrun simctl launch --console-pty --terminate-running-process "$SIM" "$BUNDLE_ID" >"$log" 2>&1 &
	pid=$!
	sleep 2
	xcrun simctl io "$SIM" screenshot --type png "$shot" >/dev/null 2>&1 || true
	wait $pid || true
	sed 's/^/  /' "$log"
	echo "  screenshot: $shot"
}

if [ $# -ge 1 ]; then
	one "${2:-iphonesimulator}" "$1"
	if [ "${2:-iphonesimulator}" = iphonesimulator ]; then run_sim "$1"; fi
	exit 0
fi

for mode in interpreted compiled; do
	for sdk in iphonesimulator iphoneos; do
		one "$sdk" "$mode"
	done
	run_sim "$mode"
done
