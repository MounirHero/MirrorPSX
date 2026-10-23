#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE="$ROOT/reference/ps5-homebrew-ui/tooling/native"
WORK="$ROOT/build/ps5-runtime"
RAW_SHA=8ee6e124993e1af26420cb455890fd002f5d6c7e78883c860ce45734e7d002bb
PRX_SHA=e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036
mkdir -p "$WORK"
if [ ! -x "$WORK/libc-builder" ]; then
  clang++-19 -std=c++20 -O2 -Wall -Wextra -Werror \
    "$SOURCE/libc_builder.cpp" -o "$WORK/libc-builder"
fi
if [ ! -x "$WORK/ps5-native-tool" ]; then
  clang++-19 -std=c++20 -O2 -Wall -Wextra -Werror \
    "$SOURCE/native_app_builder.cpp" "$SOURCE/self_container.cpp" \
    "$SOURCE/elf_object.cpp" "$SOURCE/sce_module_writer.cpp" \
    -lz -o "$WORK/ps5-native-tool"
fi
for COPY in a b; do
  "$WORK/libc-builder" "$SOURCE/runtime/api-surface.txt" \
    "$SOURCE/runtime/imports.txt" "$WORK/libc-$COPY.elf"
  echo "$RAW_SHA  $WORK/libc-$COPY.elf" | sha256sum -c -
  "$WORK/ps5-native-tool" self --sign --in "$WORK/libc-$COPY.elf" \
    --out "$WORK/libc-$COPY.prx"
  echo "$PRX_SHA  $WORK/libc-$COPY.prx" | sha256sum -c -
done
cmp "$WORK/libc-a.prx" "$WORK/libc-b.prx"
cp "$WORK/libc-a.prx" "$WORK/libc.prx"
"$WORK/ps5-native-tool" self --inspect --file "$WORK/libc.prx"
printf 'Verified clean-room PS5 runtime: %s\n' "$WORK/libc.prx"
