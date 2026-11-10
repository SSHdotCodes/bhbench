#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
[ -x build/bin/blackhole ] || ./build.sh
exec ./build/bin/blackhole "$@"
