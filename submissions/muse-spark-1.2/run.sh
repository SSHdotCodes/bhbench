#!/bin/zsh
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD="$DIR/build"
if [ ! -f "$BUILD/black_hole" ]; then
  echo "Building..."
  mkdir -p "$BUILD"
  cd "$BUILD"
  cmake .. -DCMAKE_BUILD_TYPE=Release
  make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
fi
echo "Launching Black Hole simulation..."
echo "Controls: G grid  D disk  H halo  SPACE pause  R reset  scroll zoom  drag orbit  ESC quit"
exec "$BUILD/black_hole"
