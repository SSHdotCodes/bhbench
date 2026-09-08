#!/bin/zsh
set -eu
cd "$(dirname "$0")"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 6
ctest --test-dir build --output-on-failure
./build/black-hole --validate-gpu
