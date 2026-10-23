#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export OPENPROSPERO_SDK=${OPENPROSPERO_SDK:-"$ROOT/../sdk/sdk"}
export OPENPROSPERO_LINK_FLAGS="--no-companion --title-heap"
bash "$ROOT/tools/build-ps5-deps.sh"
bash "$ROOT/tools/build-ps5-platform.sh"
cmake -S "$ROOT" -B "$ROOT/build/ps5" -G Ninja -U CMAKE_CXX_FLAGS \
  -DMIRROR_PS5=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/ps5-toolchain.cmake"
cmake --build "$ROOT/build/ps5" -j6
