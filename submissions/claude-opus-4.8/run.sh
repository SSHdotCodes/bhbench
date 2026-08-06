#!/usr/bin/env bash
# Configure, build and launch the black-hole simulator.
set -e
cd "$(dirname "$0")"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found. Install with:  brew install cmake glfw"; exit 1
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j

echo
echo "Launching… a window should open. Press TAB to switch views, ESC to quit."
exec ./build/blackhole "$@"
