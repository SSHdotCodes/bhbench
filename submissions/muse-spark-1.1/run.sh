#!/bin/zsh
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD="$DIR/build"
if [ ! -f "$BUILD/blackhole" ]; then
  echo "Building..."
  mkdir -p "$BUILD"
  cd "$BUILD"
  cmake ..
  make -j8
  cd "$DIR"
fi
echo "Launching Black Hole simulation..."
echo "Controls: G=grid, D=disk, Mouse=orbit, Scroll=zoom, Space=auto-orbit, ESC=exit"
"$BUILD/blackhole"
