#!/usr/bin/env bash
# Build (if needed) and launch the black-hole simulator.
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
exec ./build/black-hole "$@"
