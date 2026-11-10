#!/usr/bin/env bash
# Build (if needed) and launch the real-time simulation window.
set -e
cd "$(dirname "$0")"
if [ ! -x build/blackhole ]; then
  ./build.sh
fi
exec ./build/blackhole "$@"
