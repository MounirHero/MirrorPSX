#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REVISION=95c08f27386fc698f6bbe21dde3030140a41d10b
SOURCE="$ROOT/../Hyperion/build/ps5-platform-source"
OUTPUT="$ROOT/build/ps5-platform-source"
if [ ! -f "$OUTPUT/platform/Makefile" ]; then
  mkdir -p "$OUTPUT"
  # Consume Hyperion's immutable, clean upstream pin without changing its checkout.
  git -C "$SOURCE" archive "$REVISION" | tar -x -C "$OUTPUT"
  printf '%s\n' "$REVISION" > "$OUTPUT/.mirror-platform-revision"
fi
test "$(cat "$OUTPUT/.mirror-platform-revision")" = "$REVISION"
make -C "$OUTPUT/platform" -j6 LLVM_CONFIG=llvm-config-19
cp /usr/lib/llvm-19/lib/clang/19/lib/linux/libclang_rt.builtins-x86_64.a \
  "$OUTPUT/platform/build/ps5/libclang_rt.builtins-x86_64.a"
