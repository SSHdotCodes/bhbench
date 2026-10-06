#!/usr/bin/env python3
"""Render a PPM frame as ASCII art so structure can be checked at a glance."""
import sys

RAMP = " .:-=+*#%@"


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts, i = [], 0
    while len(parts) < 4:
        while i < len(data) and data[i : i + 1].isspace():
            i += 1
        if data[i : i + 1] == b"#":
            while i < len(data) and data[i : i + 1] != b"\n":
                i += 1
            continue
        j = i
        while j < len(data) and not data[j : j + 1].isspace():
            j += 1
        parts.append(data[i:j])
        i = j
    i += 1
    return int(parts[1]), int(parts[2]), data[i:]


def main():
    path = sys.argv[1]
    cols = int(sys.argv[2]) if len(sys.argv) > 2 else 78
    auto = (len(sys.argv) > 3 and sys.argv[3] == "auto")
    w, h, px = read_ppm(path)
    rows = max(1, int(cols * h / w * 0.47))  # character cells are ~2:1 tall

    # box-average luminance
    out = []
    for ry in range(rows):
        y0, y1 = int(ry * h / rows), max(int((ry + 1) * h / rows), int(ry * h / rows) + 1)
        line = []
        for cx_ in range(cols):
            x0, x1 = int(cx_ * w / cols), max(int((cx_ + 1) * w / cols), int(cx_ * w / cols) + 1)
            s = n = 0
            for y in range(y0, y1, max(1, (y1 - y0) // 3)):
                for x in range(x0, x1, max(1, (x1 - x0) // 3)):
                    o = (y * w + x) * 3
                    s += 0.2126 * px[o] + 0.7152 * px[o + 1] + 0.0722 * px[o + 2]
                    n += 1
            line.append(s / max(n, 1))
        out.append(line)

    vmax = max(max(r) for r in out)
    lo = 0.0 if auto else vmax * 0.02
    hi = vmax * (0.35 if auto else 1.0)
    print(f"{path}  {w}x{h}  max_lum={vmax:.0f}  ramp maps {lo:.0f}..{hi:.0f}")
    for row in out:
        print("".join(RAMP[min(9, max(0, int(9 * (v - lo) / max(hi - lo, 1e-9))))] for v in row))


if __name__ == "__main__":
    main()
