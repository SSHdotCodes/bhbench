#!/usr/bin/env python3
"""High-detail ASCII preview of a rendered frame, with optional hue map."""
import sys
import numpy as np
from PIL import Image

RAMP = " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$"


def lum_preview(img, w):
    h = max(1, int(w * img.height / img.width * 0.48))
    small = img.resize((w, h), Image.LANCZOS)
    a = np.asarray(small).astype(np.float32) / 255.0
    lum = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    lum = np.power(np.clip(lum, 0, 1), 0.40)
    out = []
    for row in lum:
        out.append("".join(RAMP[min(len(RAMP) - 1, int(v * len(RAMP)))] for v in row))
    return "\n".join(out)


def hue_preview(img, w):
    import colorsys
    h = max(1, int(w * img.height / img.width * 0.48))
    small = img.resize((w, h), Image.LANCZOS)
    a = np.asarray(small).astype(np.float32) / 255.0
    lum = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    out = []
    for y in range(h):
        line = []
        for x in range(w):
            r, g, b = a[y, x]
            L = lum[y, x]
            if L < 0.012:
                line.append(" ")
                continue
            mx, mn = max(r, g, b), min(r, g, b)
            sat = (mx - mn) / max(mx, 1e-6)
            if sat < 0.07:
                line.append("W" if L > 0.45 else "w")
                continue
            hh, _, _ = colorsys.rgb_to_hsv(float(r), float(g), float(b))
            deg = hh * 360.0
            if deg < 18 or deg >= 340:   c = "R"   # red
            elif deg < 45:               c = "O"   # orange
            elif deg < 70:               c = "Y"   # yellow
            elif deg < 160:              c = "G"
            elif deg < 200:              c = "C"
            elif deg < 260:              c = "B"   # blue
            else:                        c = "V"   # violet
            line.append(c if L > 0.30 else c.lower())
        out.append("".join(line))
    return "\n".join(out)


if __name__ == "__main__":
    path = sys.argv[1]
    w = int(sys.argv[2]) if len(sys.argv) > 2 else 130
    img = Image.open(path).convert("RGB")
    print(f"--- luminance ({img.width}x{img.height}) ---")
    print(lum_preview(img, w))
    print("\n--- hue (W/w white, B/b blue, R/r red, G/g green, Y yellow) ---")
    print(hue_preview(img, w))
