#!/usr/bin/env python3
"""Analyze a screenshot BMP: ASCII preview + physics sanity checks."""
import struct, sys, math

def read_bmp(path):
    with open(path, 'rb') as f:
        data = f.read()
    off = struct.unpack_from('<I', data, 10)[0]
    w = struct.unpack_from('<i', data, 18)[0]
    h = struct.unpack_from('<i', data, 22)[0]
    bpp = struct.unpack_from('<H', data, 28)[0]
    assert bpp in (24, 32), f"bpp={bpp}"
    step = bpp // 8
    row = ((w * step) + 3) & ~3
    px = bytearray(w * abs(h) * 3)
    H = abs(h)
    for y in range(H):
        src_y = (H - 1 - y) if h > 0 else y
        base = off + src_y * row
        for x in range(w):
            b, g, r = data[base + x*step], data[base + x*step + 1], data[base + x*step + 2]
            o = (y * w + x) * 3
            px[o], px[o+1], px[o+2] = r, g, b
    return w, H, px

def lum(px, i):
    return 0.2126*px[i] + 0.7152*px[i+1] + 0.0722*px[i+2]

def main(path):
    w, h, px = read_bmp(path)
    print(f"image: {w}x{h}")

    # ASCII preview (downsample)
    cw, ch = 110, 36
    chars = " .:-=+*#%@"
    print("+" + "-"*cw + "+")
    for cy in range(ch):
        line = "|"
        for cx in range(cw):
            x0, y0 = cx*w//cw, cy*h//ch
            s = n = mx = 0
            for yy in range(y0, min(y0+h//ch, h), 2):
                for xx in range(x0, min(x0+w//cw, w), 2):
                    L = lum(px, (yy*w+xx)*3)
                    s += L; n += 1; mx = max(mx, L)
            v = (0.35*s/max(n,1) + 0.65*mx)/255.0   # max-pool keeps thin lines
            line += chars[min(int(v*len(chars)*1.6), len(chars)-1)]
        print(line + "|")
    print("+" + "-"*cw + "+")

    cx0, cy0 = w//2, h//2
    # radial brightness profile from center (shadow detection)
    print("\nradial profile (avg luminance vs radius px):")
    maxr = min(w, h)//2 - 2
    prof = []
    for r in range(0, maxr, 8):
        s = n = 0
        for a in range(0, 360, 4):
            x = int(cx0 + r*math.cos(math.radians(a)))
            y = int(cy0 + r*math.sin(math.radians(a)))
            if 0 <= x < w and 0 <= y < h:
                s += lum(px, (y*w+x)*3); n += 1
        prof.append((r, s/max(n,1)))
        if r % 40 == 0:
            print(f"  r={r:4d}  L={s/max(n,1):6.1f}")

    # shadow radius: first radius where luminance exceeds 25% of max
    lmax = max(l for _, l in prof)
    shadow_r = next((r for r, l in prof if l > 0.25*lmax), None)
    print(f"\nmax annulus luminance={lmax:.1f}, shadow edge ~ r={shadow_r}px")

    # expected shadow radius: b_crit = 3*sqrt(3)/2 rs, camera dist 13 rs,
    # vertical half-FOV 19 deg, gnomonic projection
    dist = 13.0
    bcrit = 3*math.sqrt(3)/2
    sin_a = bcrit*math.sqrt(1-1/dist)/dist
    alpha = math.asin(sin_a)
    expected = math.tan(alpha)/math.tan(math.radians(19)) * (h/2)
    print(f"expected shadow radius ~ {expected:.0f}px (b_crit={bcrit:.3f} rs, D={dist} rs)")

    # left/right asymmetry in the disk annulus (Doppler beaming)
    if shadow_r:
        r1, r2 = int(shadow_r*1.15), int(shadow_r*2.2)
        ls = rs_ = nl = nr = 0
        for y in range(cy0-r2, cy0+r2):
            for x in range(cx0-r2, cx0+r2):
                if not (0 <= x < w and 0 <= y < h): continue
                d = math.hypot(x-cx0, y-cy0)
                if r1 <= d <= r2:
                    L = lum(px, (y*w+x)*3)
                    if x < cx0: ls += L; nl += 1
                    else: rs_ += L; nr += 1
        print(f"disk annulus: left avg={ls/max(nl,1):.1f}  right avg={rs_/max(nr,1):.1f}  "
              f"(beaming -> one side should be clearly brighter)")

    # grid presence: count strongly blue-ish line pixels
    nb = 0
    for i in range(0, w*h*3, 3*997):
        r, g, b = px[i], px[i+1], px[i+2]
        if b > 60 and b > r + 25: nb += 1
    print(f"bluish grid-line pixel samples: {nb}")

main(sys.argv[1])
