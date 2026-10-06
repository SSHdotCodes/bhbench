#!/bin/sh
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-/opt/homebrew}"
cmake --build build -j
echo "built build/blackhole"
