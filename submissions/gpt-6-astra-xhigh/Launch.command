#!/bin/zsh
set -eu
cd "$(dirname "$0")"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 6
exec ./build/black-hole "$@"
