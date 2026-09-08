#!/bin/bash
# Build the black-hole simulation (macOS + Linux).
set -e
cd "$(dirname "$0")"

if ! command -v cmake >/dev/null; then
  echo "error: cmake not found (brew install cmake)" >&2
  exit 1
fi

PREFIX=""
if command -v brew >/dev/null; then
  PREFIX="$(brew --prefix)"
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release ${PREFIX:+-DCMAKE_PREFIX_PATH="$PREFIX"}
cmake --build build -j"$( (sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null) || echo 4)"

echo ""
echo "Built: ./build/blackhole"
echo "Run:   ./build/blackhole            (opens the realtime window)"
echo "Test:  ./build/blackhole --selftest (physics unit tests, no window)"
