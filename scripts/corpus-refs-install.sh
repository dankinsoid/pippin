#!/bin/sh
# @ai-generated(solo)
# corpus-refs-install.sh <dir>: pinned bb and Hermes CLI into <dir>/bin and the Dart SDK into <dir>/dart-sdk-<v>, their
# bin directories printed for PATH; a cached <dir> fetches nothing.
set -eu
BB_VERSION=1.13.226
# The last Hermes CLI release (RN 0.75's engine); later Hermes ships inside React Native only, without the CLI.
HERMES_VERSION=v0.13.0
HERMES_SHA=f16b0214f7b96eccbd47766f5a3914e847a4387649b2f6b60820d309879200bd
DART_VERSION=3.13.5
dir=$1
case "$(uname -m)" in
	arm64) bb_arch=aarch64 bb_sha=584f8679022d9f23f897fcb6ebb3f6c05508483c34b555a6730fd38bf620a5a2
		dart_arch=arm64 dart_sha=9dfe7d6f2558816c2a978aff6c80e8a4509c6cb726c0702a616c64d286f60e88 ;;
	x86_64) bb_arch=amd64 bb_sha=a86e99ccffdb468a9f059dd1658063b97cd68c071e8581289fb9a0ac8fb72db8
		dart_arch=x64 dart_sha=15aaffcc5c6aebcf5e907f721671a9f03cdc5e0b95affa59a9d8291fb2972421 ;;
	*) echo "corpus-refs-install: no babashka build for $(uname -m)" >&2; exit 1 ;;
esac
mkdir -p "$dir/bin"

fetch() {
	if ! [ -f "$dir/$3" ] || ! echo "$2  $dir/$3" | shasum -a 256 -c - >/dev/null 2>&1; then
		curl -fsSL --retry 3 -o "$dir/$3" "$1"
		echo "$2  $dir/$3" | shasum -a 256 -c - >&2
	fi
}

fetch "https://github.com/babashka/babashka/releases/download/v$BB_VERSION/babashka-$BB_VERSION-macos-$bb_arch.tar.gz" \
	"$bb_sha" "babashka-$BB_VERSION-$bb_arch.tar.gz"
tar -xzf "$dir/babashka-$BB_VERSION-$bb_arch.tar.gz" -C "$dir/bin" bb
# A universal binary: one download for both architectures.
fetch "https://github.com/facebook/hermes/releases/download/$HERMES_VERSION/hermes-cli-darwin.tar.gz" \
	"$HERMES_SHA" "hermes-cli-darwin-$HERMES_VERSION.tar.gz"
tar -xzf "$dir/hermes-cli-darwin-$HERMES_VERSION.tar.gz" -C "$dir/bin" hermes hermesc
fetch "https://storage.googleapis.com/dart-archive/channels/stable/release/$DART_VERSION/sdk/dartsdk-macos-$dart_arch-release.zip" \
	"$dart_sha" "dartsdk-$DART_VERSION-$dart_arch.zip"
if ! [ -x "$dir/dart-sdk-$DART_VERSION/bin/dart" ]; then
	rm -rf "$dir/dart-sdk-$DART_VERSION" "$dir/dart-unzip"
	unzip -q "$dir/dartsdk-$DART_VERSION-$dart_arch.zip" -d "$dir/dart-unzip"
	mv "$dir/dart-unzip/dart-sdk" "$dir/dart-sdk-$DART_VERSION"
	rmdir "$dir/dart-unzip"
fi
echo "$dir/bin"
echo "$dir/dart-sdk-$DART_VERSION/bin"
