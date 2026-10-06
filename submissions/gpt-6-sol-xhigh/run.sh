#!/bin/sh
set -eu
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 4
exec ./build/black-hole "$@"
