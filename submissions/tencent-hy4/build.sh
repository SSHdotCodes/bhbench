#!/usr/bin/env bash
# Configure + build the Schwarzschild black hole simulation.
set -e
cd "$(dirname "$0")"
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
echo ""
echo "  Built: build/blackhole"
echo "  Run :  ./build/blackhole            (realtime window)"
echo "         ./build/blackhole --selftest (physics checks, no window)"
