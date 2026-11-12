# Live browser adaptations

These ports make the successful recent additions interactive on [BHBench](https://bhbench.ssh.codes). Every entry uses its own submitted photon integrator and shading. Original generated projects remain untouched in `submissions/`.

| Site ID | Original source |
| --- | --- |
| hydrogen4 | gemini-4-hydrogen/src/shaders/BlackHole.metal (Metal compute); grid from src/platform/MetalApp.mm and src/core/BlackHolePhysics.hpp |
| fable52 | claude-fable-5.2/shaders/*.frag, *.glsl, grid.vert/frag; native tables from browser/tools/fable52-export-tables.cpp |
| kimi31 | kimi-k3.1-pro/shaders/blackhole.frag, embedding.vert/frag and present.frag |
| terra61 | gpt-6.1-terra-xhigh/shaders/scene.frag |
| qwen4 | qwen4/shaders/raytrace.frag, grid.vert/frag and bloom passes |
| hy4 | tencent-hy4/src/shaders.hpp, FS_BLACKHOLE |
| deepseek41flash | deepseek-v4.1-flash/src/shaders/*.frag; blackbody table from its src/blackbody.cpp |
| inkling2small | inkling-2-small/src/Shaders.h |
| museember | muse-ember/src/main.cpp embedded shaders and Flamm grid |
| argon4 | gemini-4-argon/src/shaders.hpp, physics.hpp and spacetime_grid.hpp |
| musewonder | muse-wonder-1.0/shaders/blackhole.frag and grid.vert/frag |
| step5 | stepfun-5-preview-high/shaders/blackhole.frag |
| sol61 | gpt-6.1-sol-xhigh/shaders/raytrace.frag |
| grok47 | grok-4.7/shaders/trace.frag |
| mistral4 | mistral-large-4/src/shaders.h, kRtFs |
| mimo26flash | mimo-v2.6-flash/shaders/raytrace.frag |
| sol6 | gpt-6-sol-xhigh/shaders/blackhole.frag |
| qwen37flash | qwen-3.7-flash/shaders/blackhole.frag |
| deepseek4 | deepseek-v4-flash/src/shader_sources.hpp, LENSING_FRAG |
| qwen3827b | qwen-3.8-27b/src/shaders.hpp, MSL raytrace |
| haiku55 | claude-haiku-5.5-xhigh/src/kerr_core.h and shading.h |
| sonnet55 | claude-sonnet-5.5-xhigh/src/kerr_shared.h and shaders/*.metal |

GLSL versions and varyings are adapted to WebGL2. Metal/C++ vector types, template floats, references, and phase-space operator overloads are translated to equivalent GLSL operations. Haiku retains its Kerr–Schild RK4 flow, static observer Gram–Schmidt frame and plane-crossing bisection. Sonnet retains its Mino-time equations, constraint projection, ZAMO observer, disk wedge intersections and corona integration. Qwen retains its submitted Schwarzschild equations, including their original limitations.

Flux and blackbody tables are exported by the original native C++ functions. Float lookup interpolation uses `texelFetch`, so it does not require a float-linear texture extension. Kerr ports currently retain each native default spin of 0.9. Browser controls cover orbit, zoom, resolution and the supported shader switches. Native HUDs, secondary mesh views and window layouts are retained in the archived applications, rather than recreated in these main-view ports.

The native tone curves are preserved. Browser bloom uses a single-pass Gaussian sampling adaptation of the native HDR output; it does not reproduce every native downsample/blur pyramid or temporal filter. Gallery images and unavailable-browser fallbacks remain original native captures. These adaptations are separately identified as live WebGL2 on the site.

`live/bh-core.js` supports a per-model composite shader and clears composite uniform locations when switching programs. `live/live.js` provides existing site camera helpers and renderer configs. `live/imports.js` registers ten additions; `live/step5.js` registers StepFun 5 Preview. The existing Fable 5.1 port is also included and enabled on the site. `sonnet.frag` is included for the model-switch regression check.

## Validate in a browser

From `browser/`, run `python3 -m http.server 8247 --bind 127.0.0.1`, then open `http://127.0.0.1:8247/live/import-test.html`. Click **Run render and camera checks**. It compiles each shader, reads actual rendered pixels, checks orbit and zoom changes, reloads its initial camera, and checks disk controls when present. A passing result requires visible pixels, changed camera views, functional disk switches, and no WebGL error. It also checks the existing Fable 5.1 and Sonnet renderers after the imports in the same persistent context.

## Rebuild derived shaders and tables

From the repository root:

```sh
python3 browser/tools/port-shaders.py
clang++ -std=c++17 -O2 browser/tools/export-browser-tables.cpp \
  submissions/mimo-v2.6-flash/src/physics.cpp \
  submissions/claude-sonnet-5.5-xhigh/src/physics.cpp \
  -o /tmp/bhbench-export-tables
/tmp/bhbench-export-tables > browser/live/lookups/native-tables.json
```

The submission checksum manifest covers the preserved native projects; browser adaptations have a separate manifest in this directory.

StepFun preserves the native camera basis, all four shader scenes, 320 integration steps, disk transfer and ACES/vignette/gamma sequence. Its native bright-pass knee is retained in a single-pass Gaussian bloom adaptation. Temporal accumulation is omitted and the subpixel jitter is zero; native captures preserve the original accumulated output. The tiny final-composite dither is stationary in the browser. The validation page supports `?models=step5,sonnet,step5` to check all four StepFun scenes, orbit, zoom, disk switches, reset and switching away and back in one persistent WebGL context.

The StepFun native host clears the HDR target every frame before blending; after warmup, this scales the submitted shader output by 0.05. The browser retains that steady-state attenuation by default. **Raw samples** corresponds to the original `--single` flag and removes it. Native camera/scene warmup transitions and jitter are not reproduced. Disk speed retains both native time multipliers.

Muse Wonder 1.0 uses `live/musewonder.js` for the native camera, submitted polar grid geometry and all three views. Its original ACES/gamma shader is preserved; no extra HDR bloom is applied. WebGL line rasterization differs from native GL line smoothing. Pause freezes simulation time, and auto-orbit follows the native 0.045 rad/s rate. The original 400-iteration loop cap is retained for the 420-step preset. Validate with `?models=musewonder,sonnet,musewonder` to exercise camera, scene, disk, grid, lensing, pause and context-switch checks.

Gemini 4 Argon uses its submitted Kerr-Schild RK4 shader and grid shaders, with the original HDR bright-pass, two separable blur iterations and ACES/vignette/gamma composite. `live/argon4-meshes/` contains interleaved float geometry exported by the original C++ mesh generator, including its photon integrator, for the default view and five native presets. Animation retains the original fragment-shader pulses. Meshes use time-zero geometry; continuous native spin/depth editing and the telemetry HUD are retained in the archived app. Native line widths depend on the GL implementation. Browser toggles cover the disk, funnel, photon beams, jets and lensed grid, alongside temperature, Doppler, exposure, pause and auto-orbit. Validate with `?models=argon4,sonnet,argon4`.

Re-export Argon geometry from the repository root:

```sh
clang++ -std=c++17 -O2 -I/opt/homebrew/include browser/tools/export-argon-meshes.cpp \
  -L/opt/homebrew/lib -lglfw -framework OpenGL -framework Cocoa \
  -framework IOKit -framework CoreVideo -o /tmp/bhbench-argon-export
/tmp/bhbench-argon-export browser/live/argon4-meshes
```

## November 9, 2026 additions

Claude Fable 5.2, Kimi K3.1 Pro, GPT 6.1 Terra, Qwen4, Tencent HY4, DeepSeek V4.1 Flash, Inkling 2 Small and Muse Ember each register a small module in `live/<id>.js` with shaders in `live/shaders/`. Each keeps the model's submitted photon integrator and shading; only GLSL headers, precision and host API calls are adapted for WebGL2, and the site's orbit and zoom controls are added. Fable 5.2's disk, blackbody and embedding tables (`live/lookups/fable52.json`) and DeepSeek V4.1 Flash's blackbody table (`live/lookups/deepseek41flash-bb.json`) are exported by the models' own C++ functions; the exporters are in `tools/`.

Known browser differences: Fable 5.2 uses a 1024 sky cubemap instead of the native 2048 and keeps its native spin of 0.9; where a port draws grid lines, WebGL line rasterization differs from native GL; native HUDs and split-screen layouts stay in the archived applications. Tencent HY4's two-sheet Flamm view and Kimi K3.1 Pro's native HUD are shown only in the native captures.

These modules were validated in local copies of the site rather than `live/import-test.html`. Each port was checked in headless Chromium (Playwright, Metal ANGLE) for visible pixels, orbit, zoom, supported toggles, reset and switching renderers in one WebGL context, next to its native capture. The merged site was then loaded with all 52 entries; every live view's rendered pixels were read back, with no console or WebGL errors.

## November 12, 2026 addition

Gemini 4 Hydrogen's Metal compute ray tracer is translated mechanically to GLSL ES 3.0 by `tools/hydrogen4/port-shaders.py`. The shader bodies stay the submission's: ZAMO photon initialization, the second-order Mino-time Kerr RK4 integrator in u = 1/r with its unit-sphere angular representation, adaptive steps and the 560-step cap at every browser quality setting, plus the disk, beaming, corona, sky and ACES tone curve. The native color and hit-distance outputs become two WebGL2 render targets, so the spacetime grid keeps the native depth occlusion. The grid mesh (`live/hydrogen4-mesh/`) is exported by a verbatim copy of the app's `buildSpacetimeGridMesh` calling the submitted C++ embedding (`tools/hydrogen4/export-grid.py`). MPS Gaussian bloom is approximated with separable Gaussian passes at the native threshold, knee and weights. The native HUD and camera easing are not recreated. Spin defaults to the native 0.9, with the native 0–0.998 range.

Rebuild from the repository root:

```sh
python3 browser/tools/hydrogen4/port-shaders.py
python3 browser/tools/hydrogen4/export-grid.py
clang++ -std=c++20 -O2 browser/tools/hydrogen4/export-grid.cpp -o /tmp/hydrogen4-export-grid
/tmp/hydrogen4-export-grid browser/live/hydrogen4-mesh
```

Both commands reproduce the published shader and mesh files byte for byte.
