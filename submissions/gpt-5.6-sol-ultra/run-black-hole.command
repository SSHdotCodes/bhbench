#!/bin/zsh
set -euo pipefail

cd "${0:A:h}"

cmake --fresh -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build --parallel
open ./build/schwarzschild_lab.app
