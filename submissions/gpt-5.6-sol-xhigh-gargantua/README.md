# Gargantua

Gargantua is a real-time, XDR-aware Kerr black hole renderer for Apple Silicon. Run it through Python; every expensive per-pixel operation executes in a native Metal compute shader.

```bash
python3 blackhole.py
```

To create a normal macOS app bundle:

```bash
python3 blackhole.py package
open dist/Gargantua.app
```

The default output and ray map are both 2560x1440 at a requested 120 Hz. Every pixel stores its own Kerr null-geodesic result in a persistent GPU lens map; there is no spatial upscaling. The map is built before the first frame, while camera changes refresh it in eight interleaved phases. Disk motion and HDR shading then run against the full-resolution map every frame.

## Physics

- Null rays integrate the separated Kerr geodesic equations in Boyer-Lindquist coordinates.
- The black hole defaults to `a/M = 0.998`, with the event horizon and prograde ISCO calculated from that spin.
- The thin accretion disk ends at the ISCO and uses a zero-torque temperature profile.
- Disk color and radiance include gravitational/Doppler frequency shift and the bolometric `g^4` intensity transform.
- Higher-order disk and star images arise from 3.7 million stored geodesics rather than a screen-space distortion.

The renderer follows the physics behind DNGR, but it is a real-time point-ray integrator rather than DNGR's offline ray-bundle renderer. The procedural disk texture is artistic detail on top of the relativistic transfer calculation.

## HDR

The drawable is `RGBA16Float` in extended-linear Display P3. The Metal layer opts into EDR and supplies HDR10 metadata mastered from 0.0001 to 1600 nits with a 100-nit reference white. Actual peak brightness remains under macOS and the display's thermal/ambient-light control.

## Controls

| Input | Action |
| --- | --- |
| Drag | Orbit the camera |
| Scroll | Optical zoom |
| `[` / `]` | Adjust exposure |
| `Space` | Pause disk motion |
| `H` | Toggle instrumentation |
| `R` | Reset the camera |
| `F` | Toggle full screen |

## Measurement

```bash
python3 blackhole.py doctor
python3 blackhole.py benchmark --seconds 10
```

The benchmark reports measured draw cadence, average and p95 GPU time, full lens-map resolution, XDR headroom, and whether the requested target was sustained. Camera changes preserve presentation cadence by refreshing one eighth of the full-resolution map per frame.

## Tests

```bash
python3 -m unittest discover -s tests
swift test
```
