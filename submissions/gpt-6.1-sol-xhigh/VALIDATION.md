# Verification on September 29, 2026

## Build and runtime

- Apple M4 Max, 40-core GPU, arm64.
- AppleClang 21.0.0; Release build, C++17.
- GLFW 3.4.0.
- OpenGL reported `4.1 Metal - 91.7`, renderer `Apple M4 Max`.
- CMake configured and built both executables and the native app bundle successfully.
- `plutil` validated the app's Info.plist.
- Both CTest tests passed: `relativistic_physics` and `gpu_geodesic_reference`.

## Independent CPU physics checks

All 15 checks passed, including six impact parameters on either side of the critical curve.

```text
PASS  Unstable photon orbit remains at r = 3M for exact initial data
PASS  Capture / escape across b_crit: b = 4.8, 5.19, 5.20, 5.5, 8, 20
PASS  Null first integral conserved to < 1e-9
PASS  Refining the integrator improves asymptotic ray direction
PASS  Weak-field bending approaches 4M/b
PASS  Zero torque at ISCO, positive flux outside
PASS  Novikov-Thorne flux matches independent conservation-law quadrature
PASS  Face-on shift includes gravitational and transverse Doppler terms
PASS  Approaching emitter is brighter via g^4
PASS  Flamm embedding reproduces the exact spatial radial metric
```

Maximum absolute null-invariant drift in the checked double-precision scattering orbit was `1.57519830513e-12`. Weak-field bending at `b = 1000M` was `0.00401182378129` radians, consistent with `4M/b` and the small higher-order correction.

## Actual GPU shader against double-precision reference

The validation renders an 80×60 floating-point diagnostic image, reads it back, and compares the shader output with finer double-precision orbit integration. The disk reference finds the same equatorial-plane crossings, integrates coordinate travel time with RK4, and independently evaluates the frequency shift.

```text
GPU/CPU rays compared:                         4792
Capture/escape mismatches:                        0
Maximum escaped-ray angle difference:  0.000428539 rad
Disk intersections compared:                    1490
Disk visibility mismatches:                         0
Maximum relative disk radius error:      0.000587384
Maximum relative frequency shift error:     0.00018403
Maximum relative light travel time error:  0.0000607227
```

The maxima correspond to about 0.0246 degrees of sky direction error, 0.0588% disk radius error, 0.0185% frequency-shift error, and 0.0061% travel-time error in this sample. The diagnostic uses high-quality mode, a 0.01-radian GPU angular step, and a 0.002-radian CPU step.

Eight rays within `|b - 3√3| < 0.015M` were excluded because very close to the unstable photon orbit, small floating-point differences produce large winding-angle changes. These results apply to the tested camera and sample, not every possible ray or every quality mode. Pixels and integration steps still limit the resolution of extremely narrow higher-order images.

## Live performance and visual inspection

10–12 second windowed runs used a 1440×900 logical window with a 2880×1800 Retina framebuffer. Balanced mode automatically increased its internal resolution to 2052×1366 in the main optical view.

| View | Steady display rate | Smoothed GPU scene time |
|---|---:|---:|
| Lensed disk with live curvature inset | about 60 FPS | about 5.46 ms |
| Main curvature grid with live optical inset | about 60 FPS | about 1.59 ms |
| Split optical / curvature view | about 71.5 FPS | about 3.91 ms |

The displayed frame rate depends on swap synchronization, display configuration, resolution, and quality. These are measured results on this computer, not a performance guarantee for another GPU.

The framebuffer captures `captures/lensing.png`, `captures/curvature.png`, and `captures/split.png` were inspected. They verify the lensed primary and secondary disk images, Doppler brightness asymmetry, the live inset, the exact funnel mesh, radial infall dots, the horizon / photon sphere / ISCO markers, and the causal radial-light diagram. The latter shows the outgoing branch becoming stationary at the horizon and both radial future branches pointing inward inside it.

## Reproduce

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/black_hole --validate-gpu
./build/black_hole --seconds 12 --capture captures/lensing.ppm
./build/black_hole --grid --seconds 8 --capture captures/curvature.ppm
./build/black_hole --split --seconds 10 --capture captures/split.ppm
```

PPM screenshots contain the complete rendered window. On macOS, convert one with `sips -s format png captures/lensing.ppm --out captures/lensing.png`.
