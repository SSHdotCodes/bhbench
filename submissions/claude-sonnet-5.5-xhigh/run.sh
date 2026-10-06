#!/bin/sh
# Build (if needed) and open the interactive Kerr black-hole window.
cd "$(dirname "$0")" || exit 1
make -s || exit 1
exec ./build/blackhole "$@"
