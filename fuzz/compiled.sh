#!/bin/sh
# @ai-generated(solo)
# The fuzzer's compiled runner (docs/notes/fuzzing.md). Usage: compiled.sh <build-dir> <case.clj>.
set -e
bin="$1"
case_file="$2"
out="${case_file%.clj}.units"
rm -rf "$out"
mkdir -p "$out"
# Compiling evaluates the file, so this transcript is the interpreter's.
"$bin/clj-compile" --file "$case_file" --out "$out" > /dev/null
exec "$bin/clj-fuzz" --units "$out" "$case_file"
