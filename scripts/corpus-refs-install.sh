#!/bin/sh
# @ai-generated(solo)
# corpus-refs-install.sh <dir>: pinned bb and Hermes CLI into <dir>/bin, printed; a cached <dir> fetches nothing.
set -eu
BB_VERSION=1.13.226
# The last Hermes CLI release (RN 0.75's engine); later Hermes ships inside React Native only, without the CLI.
HERMES_VERSION=v0.13.0
HERMES_SHA=f16b0214f7b96eccbd47766f5a3914e847a4387649b2f6b60820d309879200bd
dir=$1
case "$(uname -m)" in
	arm64) bb_arch=aarch64 bb_sha=584f8679022d9f23f897fcb6ebb3f6c05508483c34b555a6730fd38bf620a5a2 ;;
	x86_64) bb_arch=amd64 bb_sha=a86e99ccffdb468a9f059dd1658063b97cd68c071e8581289fb9a0ac8fb72db8 ;;
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
echo "$dir/bin"
