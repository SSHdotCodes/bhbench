#!/usr/bin/env python3
"""Quantitative checks on a rendered frame (PPM P6).

Verifies, without eyeballing:
  * the frame is a real image (not blank / not NaN-black)
  * the black-hole shadow has the analytic angular radius
      sin(theta) = b_c * sqrt(1 - rs/r0) / r0,  b_c = 3 sqrt(3) M
  * a bright photon-ring / disk structure sits just outside the shadow
  * relativistic beaming makes the approaching side of the disk brighter
  * the background has stars (structure outside the disk)
"""
import math
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # header: P6 <w> <h> <maxval>
    parts = []
    i = 0
    while len(parts) < 4:
        # skip whitespace and comments
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
    i += 1  # single whitespace after maxval
    w, h, mx = int(parts[1]), int(parts[2]), int(parts[3])
    px = data[i:]
    return w, h, mx, px


def lum(px, idx):
    o = idx * 3
    return 0.2126 * px[o] + 0.7152 * px[o + 1] + 0.0722 * px[o + 2]


def main():
    path = sys.argv[1]
    # expected geometry (must match the command line used for the render)
    fov_deg = 50.0
    dist = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0
    rs = 2.0
    bc = 3.0 * math.sqrt(3.0)

    w, h, mx, px = read_ppm(path)
    cx, cy = (w - 1) / 2.0, (h - 1) / 2.0

    # ---- global stats -----------------------------------------------------
    vals = [lum(px, i) for i in range(w * h)]
    mean = sum(vals) / len(vals)
    vmin, vmax = min(vals), max(vals)
    var = sum((v - mean) ** 2 for v in vals) / len(vals)
    zeros = sum(1 for v in vals if v <= 0.0)
    print(f"image        : {w}x{h}  lum mean={mean:.2f} min={vmin:.0f} max={vmax:.0f} "
          f"std={math.sqrt(var):.2f} black={100.0*zeros/len(vals):.2f}%")

    ok = True

    # ---- analytic shadow radius ------------------------------------------
    sin_theta = bc * math.sqrt(1.0 - rs / dist) / dist
    theta = math.asin(sin_theta)
    px_per_rad = (h / 2.0) / math.radians(fov_deg / 2.0)
    r_pred = theta * px_per_rad
    print(f"shadow       : predicted theta={math.degrees(theta):.3f} deg -> {r_pred:.1f} px")

    # radial profile (mean and max luminance per annulus)
    nb = 60
    binw = min(cx, cy) / nb
    prof_mean = [0.0] * nb
    prof_max = [0.0] * nb
    prof_cnt = [0] * nb
    for y in range(h):
        dy = y - cy
        for x in range(w):
            dx = x - cx
            r = math.hypot(dx, dy)
            b = int(r / binw)
            if b >= nb:
                continue
            v = lum(px, y * w + x)
            prof_mean[b] += v
            prof_cnt[b] += 1
            if v > prof_max[b]:
                prof_max[b] = v
    for b in range(nb):
        if prof_cnt[b]:
            prof_mean[b] /= prof_cnt[b]

    # shadow edge: innermost radius whose mean exceeds 12% of the brightest annulus
    peak = max(prof_mean)
    thr = 0.12 * peak
    r_meas = None
    for b in range(nb):
        if prof_cnt[b] > 0 and prof_mean[b] > thr:
            r_meas = b * binw
            break
    print(f"shadow edge  : measured {r_meas:.1f} px  (peak annulus lum {peak:.1f})")
    if r_meas is None:
        print("  [FAIL] no bright structure found")
        ok = False
    else:
        rel = abs(r_meas - r_pred) / r_pred
        print(f"  shadow radius error = {100.0*rel:.1f}% (predicted {r_pred:.1f} px)")
        if rel > 0.35:
            print("  [FAIL] shadow radius far from the analytic value")
            ok = False
        else:
            print("  [PASS] shadow radius consistent with 3sqrt(3) M lensing")

    # ---- darkness of the shadow interior ---------------------------------
    inner = 0.0
    n = 0
    r_in = 0.55 * r_pred
    for y in range(h):
        dy = y - cy
        for x in range(w):
            dx = x - cx
            if math.hypot(dx, dy) < r_in:
                inner += lum(px, y * w + x)
                n += 1
    inner /= max(n, 1)
    print(f"shadow core  : mean lum {inner:.2f} (image mean {mean:.2f})")
    if inner > 0.10 * peak:
        print("  [FAIL] shadow interior should be near-black")
        ok = False
    else:
        print("  [PASS] shadow interior is dark")

    # ---- photon ring: a local maximum just outside the shadow -------------
    lo = int((r_pred + 0.02 * r_pred) / binw)
    hi = int((r_pred + 0.45 * r_pred) / binw)
    ring = max(prof_mean[lo:hi + 1]) if hi > lo else 0.0
    print(f"photon ring  : max annulus lum {ring:.2f} just outside the shadow "
          f"({100.0*ring/max(peak,1e-6):.0f}% of peak)")
    if ring < 0.25 * peak:
        print("  [FAIL] expected bright lensed disk emission near the shadow")
        ok = False
    else:
        print("  [PASS] bright lensed emission hugs the shadow")

    # ---- Doppler beaming: left/right asymmetry in the disk annulus --------
    r0i, r1i = 0.75 * r_pred, 2.6 * r_pred
    left = ln = right = 0.0
    nl = nr = 0
    for y in range(h):
        dy = y - cy
        for x in range(w):
            dx = x - cx
            r = math.hypot(dx, dy)
            if r0i <= r <= r1i:
                v = lum(px, y * w + x)
                if dx < 0:
                    left += v
                    nl += 1
                else:
                    right += v
                    nr += 1
    left /= max(nl, 1)
    right /= max(nr, 1)
    ratio = left / max(right, 1e-6)
    print(f"beaming      : left(approaching)={left:.2f} right(receding)={right:.2f} "
          f"ratio={ratio:.2f}")
    if ratio < 1.15:
        print("  [FAIL] expected the approaching side to be brighter (g^4 beaming)")
        ok = False
    else:
        print("  [PASS] relativistic beaming asymmetry present")

    # ---- background structure --------------------------------------------
    corner = []
    for y in list(range(0, h, 7))[:60]:
        for x in list(range(0, w, 7))[:60]:
            if math.hypot(x - cx, y - cy) > 3.2 * r_pred:
                corner.append(lum(px, y * w + x))
    if corner:
        cm = sum(corner) / len(corner)
        cvar = sum((v - cm) ** 2 for v in corner) / len(corner)
        nb_bright = sum(1 for v in corner if v > 1.25 * cm)
        print(f"background   : mean={cm:.2f} std={math.sqrt(cvar):.2f} "
              f"star-like pixels={100.0*nb_bright/max(len(corner),1):.1f}%")
        if math.sqrt(cvar) < 0.5:
            print("  [FAIL] background looks flat (no lensed star field)")
            ok = False
        else:
            print("  [PASS] background has structure")

    # ---- radial profile summary ------------------------------------------
    print("radius profile (px: mean/max):")
    step = max(1, nb // 20)
    for b in range(0, nb, step):
        if prof_cnt[b]:
            bar = "#" * int(40 * prof_mean[b] / max(peak, 1e-6))
            print(f"   {b*binw:6.1f} {prof_mean[b]:7.2f} {prof_max[b]:7.1f} {bar}")

    print("\nRESULT:", "ALL CHECKS PASSED" if ok else "FAILURES PRESENT")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
