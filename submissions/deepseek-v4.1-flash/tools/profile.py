#!/usr/bin/env python3
"""Radial + scanline diagnostics for a rendered black hole frame."""
import sys
import numpy as np
from PIL import Image


def main(path):
    img = Image.open(path).convert("RGB")
    a = np.asarray(img).astype(np.float32) / 255.0
    lum = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    h, w = lum.shape
    cy, cx = h // 2, w // 2

    yy, xx = np.mgrid[0:h, 0:w]
    rr = np.sqrt((yy - cy) ** 2 + (xx - cx) ** 2)

    print(f"{path}  {w}x{h}")
    print("\nradial profile (fraction of half-height -> mean luminance, mean RGB)")
    half = h / 2
    for frac in np.arange(0.0, 1.01, 0.05):
        r0, r1 = frac * half, (frac + 0.05) * half
        m = (rr >= r0) & (rr < r1)
        if not m.any():
            continue
        print(f"  r={frac:4.2f}  lum={lum[m].mean():.4f}  "
              f"rgb=({a[...,0][m].mean():.3f},{a[...,1][m].mean():.3f},{a[...,2][m].mean():.3f})")

    print("\nhorizontal scanline through centre (every 4% of width)")
    row = lum[cy]
    for i in range(0, w, max(1, w // 25)):
        print(f"  x={i/w:4.2f}  lum={row[i]:.4f}")

    print("\nvertical scanline through centre")
    col = lum[:, cx]
    for i in range(0, h, max(1, h // 25)):
        print(f"  y={i/h:4.2f}  lum={col[i]:.4f}")


if __name__ == "__main__":
    main(sys.argv[1])
