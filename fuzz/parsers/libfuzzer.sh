#!/bin/sh
# @ai-generated(solo)
# usage: libfuzzer.sh <compiler-rt tarball url> <sha256> <out dir> <c++ compiler>
# libFuzzer itself is built without coverage instrumentation, or it would fuzz its own loop.
set -eu
url=$1 sha=$2 dir=$3 cxx=$4
mkdir -p "$dir/obj"
tarball="$dir/compiler-rt.src.tar.xz"
if ! [ -f "$tarball" ]; then
	curl -fsSL --retry 3 -o "$tarball.part" "$url"
	mv "$tarball.part" "$tarball"
fi
echo "$sha  $tarball" | shasum -a 256 -c -
rm -rf "$dir/src"
mkdir -p "$dir/src"
tar -xJf "$tarball" -C "$dir/src" '*/lib/fuzzer/*'
for f in "$dir"/src/*/lib/fuzzer/Fuzzer*.cpp; do
	# Split on purpose: the compiler is "xcrun clang++".
	$cxx -std=c++17 -O2 -g -fno-omit-frame-pointer -c "$f" -o "$dir/obj/$(basename "$f" .cpp).o"
done
rm -f "$dir/libFuzzer.a"
ar rcs "$dir/libFuzzer.a" "$dir"/obj/*.o
