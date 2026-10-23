#!/bin/sh
set -eu
: "${OPENPROSPERO_SDK:?Set OPENPROSPERO_SDK to sdk/sdk}"
exec clang-19 --target=x86_64-sie-ps5 -nostdinc \
 -isystem /usr/lib/llvm-19/lib/clang/19/include \
 -isystem "$OPENPROSPERO_SDK/target/cxx/include" \
 -isystem "$OPENPROSPERO_SDK/target/include" \
 -fPIC -femulated-tls -fno-stack-protector -B "$OPENPROSPERO_SDK/../build/bin" "$@"
