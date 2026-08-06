#!/bin/zsh
# Build the black hole geodesic ray tracer (no external dependencies —
# only system frameworks: Cocoa, Metal, QuartzCore).
set -e
cd "$(dirname "$0")"
mkdir -p build

clang++ -std=c++17 -O2 -fobjc-arc -Wall \
    -framework Cocoa -framework Metal -framework QuartzCore \
    src/main.mm src/Renderer.mm \
    -o build/blackhole

# shader source is compiled at runtime; keep a copy next to the binary
cp src/Shaders.metal build/Shaders.metal

echo "Built: build/blackhole"
