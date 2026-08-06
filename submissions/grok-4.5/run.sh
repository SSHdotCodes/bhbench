#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ ! -x build/black_hole_sim ]]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j
fi
exec ./build/black_hole_sim
