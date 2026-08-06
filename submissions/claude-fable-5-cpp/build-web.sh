#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
EMCC="${EMCC:-/Users/sshpro/emsdk/upstream/emscripten/emcc}"
mkdir -p "$ROOT/public"
"$EMCC" "$ROOT/web/sim.cpp" -std=c++17 -O3 \
  -s WASM=1 -s MODULARIZE=1 -s EXPORT_ES6=1 -s EXPORT_NAME=createRelativityCore \
  -s ENVIRONMENT=web -s FILESYSTEM=0 -s ALLOW_MEMORY_GROWTH=0 \
  -s INITIAL_MEMORY=16777216 -s MALLOC=none \
  -s EXPORTED_FUNCTIONS='["_sim_init","_sim_set_black_hole_mass","_sim_get_black_hole_mass","_sim_spawn","_sim_spawn_aimed","_sim_step","_sim_body_count","_sim_body_active","_sim_body_value","_sim_disk_density","_sim_disk_temperature","_sim_disk_mass","_sim_disk_normal_x","_sim_disk_normal_y","_sim_disk_normal_z","_sim_accreted_mass","_sim_merger_energy","_sim_last_event","_sim_rg_km","_sim_tg_seconds","_sim_horizon_km","_sim_circular_velocity","_sim_lapse"]' \
  -s EXPORTED_RUNTIME_METHODS='["cwrap"]' \
  -o "$ROOT/public/relativity-core.js"
cp "$ROOT/web/index.html" "$ROOT/public/index.html"
cp "$ROOT/web/styles.css" "$ROOT/public/styles.css"
cp "$ROOT/web/app.js" "$ROOT/public/app.js"
echo "Built WebAssembly experience in $ROOT/public"
