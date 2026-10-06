# BHBench integration

Added September 22, 2026 as Claude Opus 5.5, replacing Opus 5 in the current-generation list. Opus 5 retains its identifier, live renderer, and votes in previous-generation results.

The site displays the unmodified `renders/01_kerr_a0.9_default.png` as a native Metal capture. This entry does not run a browser shader. All submitted source and eight native renders are preserved here without build artifacts.

Verification on Apple Silicon: `make test` completed with 85 checks passed, zero failures, and all seven GPU/CPU comparison scenes passed. The cube-map convention test also passed. Generation usage and reasoning metadata were not supplied and are displayed as unavailable.
