#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cmake -S "$project_dir" -B "$project_dir/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_dir/build" --parallel
ctest --test-dir "$project_dir/build" --output-on-failure
if [ -x "$project_dir/build/black-hole-sim.app/Contents/MacOS/black-hole-sim" ]; then
    exec "$project_dir/build/black-hole-sim.app/Contents/MacOS/black-hole-sim"
fi
exec "$project_dir/build/black-hole-sim"
