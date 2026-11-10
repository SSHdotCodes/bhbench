#!/bin/sh
# Build, verify the physics, and open the simulation window.
# Extra arguments are passed to the simulation, e.g.  ./run.sh --preset 5
set -e
cd "$(dirname "$0")"

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake not found - install it with:  brew install cmake glfw" >&2
    exit 1
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build

echo
./build/verify | tail -n 1
echo
exec ./build/blackhole "$@"
