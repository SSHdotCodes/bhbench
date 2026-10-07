# Live browser adaptations

These ports make the successful recent additions interactive on [BHBench](https://bhbench.ssh.codes). Every entry uses its own submitted photon integrator and shading. Original generated projects remain untouched in `submissions/`.

| Site ID | Original source |
| --- | --- |
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

`live/bh-core.js` supports a per-model composite shader and clears composite uniform locations when switching programs. `live/live.js` provides existing site camera helpers and renderer configs. `live/imports.js` registers the ten additions. The existing Fable 5.1 port is also included and enabled on the site. `sonnet.frag` is included for the model-switch regression check.

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
