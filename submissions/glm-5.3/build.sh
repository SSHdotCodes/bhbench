#!/bin/zsh
# Fallback build without CMake (Homebrew GLFW via pkg-config)
set -e
cd "$(dirname "$0")"
PKGS="$(pkg-config --cflags --libs glfw3)"
clang++ -std=c++17 -O2 -Wall src/main.cpp -o blackhole $PKGS -framework OpenGL
echo "built ./blackhole"
