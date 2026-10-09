#!/bin/bash
# Build and run the black hole simulation.
#
#   ./build.sh                      -> configure, build, run
#   ./build.sh --scene 3 --dist 25  -> pass any flags straight through
set -e
cd "$(dirname "$0")"

if [ ! -f build/CMakeCache.txt ]; then
  echo "== configuring =="
  cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew -DCMAKE_BUILD_TYPE=Release
fi
echo "== building =="
cmake --build build -j

echo "== running =="
exec ./build/black-hole "$@"
