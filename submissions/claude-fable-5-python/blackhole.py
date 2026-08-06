#!/usr/bin/env python3
"""
Gargantua — a real-time general-relativistic black hole renderer.
Vulkan on Metal (MoltenVK), Apple Silicon. HDR/EDR output on XDR displays.

Every pixel traces a photon backwards from the camera through **Kerr**
spacetime (rotating black hole, spin a up to 0.998) by integrating
Hamilton's equations for null geodesics in Boyer-Lindquist coordinates
with RK4 in Kerr–Schild Cartesian coordinates:

    2H = |p|² − 1 − f (1 + k·p)²

with conserved E = 1 and L. Frame dragging, the asymmetric photon ring,
the D-shaped shadow, and the spin-dependent ISCO all emerge from the
integration. On top of that:

  * Thin accretion disk with a Novikov–Thorne temperature profile
    T(r) ∝ [r^-3 (1 - sqrt(r_in/r))]^(1/4), colored by Planck blackbody.
  * Relativistic Doppler beaming + gravitational redshift of the disk
    (approaching side brighter/bluer, receding side dim/red).
  * Multiple lensed disk images (photon ring) via alpha compositing of
    every equatorial-plane crossing along the bent ray.
  * Procedural starfield / Milky Way / nebula, gravitationally lensed.
  * HDR pipeline: float16 targets -> bright pass -> two-level Gaussian
    bloom -> chromatic aberration -> ACES tonemap -> grain/vignette.

Controls
  drag          orbit camera            scroll     dolly in/out
  SPACE         pause disk time         B          toggle bloom
  D             toggle Doppler/beaming  G          toggle inflow glow
  [ / ]         spin down / up          H          toggle HDR (EDR) output
  1/2/3/4       quality presets         S          save screenshot PNG
  R             reset camera            ESC / Q    quit

CLI
  python3 blackhole.py                       interactive window (HDR if available)
  python3 blackhole.py --spin 0.998 --size 1920x1080 --quality 4
  python3 blackhole.py --sdr                 force 8-bit SDR output
  python3 blackhole.py --shot out.png        offscreen ultra-quality still
"""

import argparse
import ctypes
import math
import os
import struct
import sys
import time

import numpy as np

# ----------------------------------------------------------------------------
# CPU reference physics (mirrors the GLSL Kerr integrator; used by the tests)
# Units: G = c = M = 1. Spin axis = world +y; disk in the equatorial plane.
# ----------------------------------------------------------------------------


def kerr_horizon(a):
    """Outer event horizon radius r+ = 1 + sqrt(1 - a^2)."""
    return 1.0 + math.sqrt(max(1.0 - a * a, 0.0))


def photon_sphere_r(a):
    """Equatorial unstable photon orbit radius (M = 1 Boyer-Lindquist r)."""
    return 2.0 * (1.0 + math.cos(2.0 / 3.0 * math.acos(max(-1.0, min(1.0, -a)))))


def kerr_isco(a):
    """Prograde ISCO radius (Bardeen, Press & Teukolsky 1972)."""
    z1 = 1.0 + (1.0 - a * a) ** (1 / 3) * ((1 + a) ** (1 / 3) + (1 - a) ** (1 / 3))
    z2 = math.sqrt(3.0 * a * a + z1 * z1)
    return 3.0 + z2 - math.sqrt((3.0 - z1) * (3.0 + z1 + 2.0 * z2))


def ks_radius(q, a):
    """Boyer-Lindquist r at Kerr-Schild Cartesian position q (spin axis = q[2]):
    the positive root of r^4 - (|q|^2 - a^2) r^2 - a^2 z^2 = 0."""
    rho2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2]
    b = rho2 - a * a
    return math.sqrt(0.5 * (b + math.sqrt(b * b + 4.0 * a * a * q[2] * q[2])))


def _ks_fk(q, a):
    """Kerr-Schild scalar f = 2 r^3/(r^4 + a^2 z^2) and null vector k
    (spatial part; k_t = 1)."""
    r = ks_radius(q, a)
    d_big = r ** 4 + a * a * q[2] * q[2]
    f = 2.0 * r ** 3 / d_big
    b = r * r + a * a
    k = ((r * q[0] + a * q[1]) / b, (r * q[1] - a * q[0]) / b, q[2] / r)
    return f, k, r, d_big, b


def kerr_hamiltonian(q, p, a):
    """H = (1/2) g^{mu nu} p_mu p_nu in Kerr-Schild form for a photon with
    E = -p_t = 1:  2H = |p|^2 - 1 - f (1 + k.p)^2. Null geodesics have H = 0."""
    f, k, _, _, _ = _ks_fk(q, a)
    kp = k[0] * p[0] + k[1] * p[1] + k[2] * p[2]
    return 0.5 * (p[0] ** 2 + p[1] ** 2 + p[2] ** 2 - 1.0 - f * (1.0 + kp) ** 2)


def _kerr_deriv(q, p, a):
    """Hamilton's equations dq = dH/dp, dp = -dH/dq with analytic gradients.
    grad r = r (r^2 q + a^2 z e_z) / (r^4 + a^2 z^2)."""
    f, k, r, d_big, b = _ks_fk(q, a)
    kp = k[0] * p[0] + k[1] * p[1] + k[2] * p[2]
    qq = 1.0 + kp

    dq = tuple(p[i] - f * qq * k[i] for i in range(3))

    gr = tuple(r * (r * r * q[i] + (a * a * q[2] if i == 2 else 0.0)) / d_big
               for i in range(3))
    cf = (6.0 * r * r * d_big - 8.0 * r ** 6) / (d_big * d_big)
    gf = [cf * gr[i] for i in range(3)]
    gf[2] -= 4.0 * a * a * q[2] * r ** 3 / (d_big * d_big)

    # grad(k . p): sum_j p_j dk_j/dq_i
    two_r_b2 = 2.0 * r / (b * b)
    nx = r * q[0] + a * q[1]
    ny = r * q[1] - a * q[0]
    gkp = [0.0, 0.0, 0.0]
    for i in range(3):
        dnx = q[0] * gr[i] + (r if i == 0 else 0.0) + (a if i == 1 else 0.0)
        dny = q[1] * gr[i] + (r if i == 1 else 0.0) - (a if i == 0 else 0.0)
        dkz = (1.0 / r if i == 2 else 0.0) - q[2] * gr[i] / (r * r)
        gkp[i] = (p[0] * (dnx / b - nx * two_r_b2 * gr[i])
                  + p[1] * (dny / b - ny * two_r_b2 * gr[i])
                  + p[2] * dkz)

    dp = tuple(0.5 * qq * qq * gf[i] + f * qq * gkp[i] for i in range(3))
    return dq, dp


def kerr_init_photon(pos, direction, a):
    """Cartesian camera ray -> Kerr-Schild state (q, p) with E = -p_t = 1,
    plus the conserved azimuthal momentum L. World y = spin axis maps to
    Kerr-Schild z. Mirrors the GLSL."""
    q = (float(pos[0]), float(pos[2]), float(pos[1]))          # world -> KS
    d = np.asarray([direction[0], direction[2], direction[1]], dtype=np.float64)
    d = d / np.linalg.norm(d)

    f, k, _, _, _ = _ks_fk(q, a)
    kd = k[0] * d[0] + k[1] * d[1] + k[2] * d[2]
    # Simplified null-condition root. The discriminant reduces to
    # 4 * (1 - f + f * kd^2), and that square root is the normalization E.
    energy = math.sqrt(max(1.0 - f + f * kd * kd, 0.0))
    tdot_plus_kd = (energy + kd) / (1.0 - f)
    p = [f * k[i] * tdot_plus_kd + d[i] for i in range(3)]
    p = [pi / energy for pi in p]
    L = q[0] * p[1] - q[1] * p[0]
    return q, tuple(p), L


def trace_ray_kerr(pos, direction, a=0.0, max_steps=60000, r_escape=60.0):
    """Trace one photon through Kerr spacetime (mirrors the shader: adaptive
    steps, RK4 near the photon shell, midpoint RK2 in the weak far field).
    Returns (status, out_dir_world_cartesian, L)."""
    q, p, L = kerr_init_photon(pos, direction, a)
    q, p = list(q), list(p)
    rhor = kerr_horizon(a)
    r_photon = photon_sphere_r(a)
    r_min = ks_radius(q, a)
    for step in range(max_steps):
        r = ks_radius(q, a)
        r_min = min(r_min, r)
        if r < rhor:
            return "captured", None, L
        if r > r_escape and q[0] * p[0] + q[1] * p[1] + q[2] * p[2] > 0.0:
            out_r = math.sqrt(q[0] ** 2 + q[1] ** 2 + q[2] ** 2)
            if r_min < r_photon + 0.12 and out_r < r_escape * 0.65:
                return "captured", None, L
            n = math.sqrt(p[0] ** 2 + p[1] ** 2 + p[2] ** 2)
            return "escaped", np.array([p[0] / n, p[2] / n, p[1] / n]), L
        dl = min(max(0.045 + 0.32 * (r - rhor), 0.02), 9.0)
        if r < 9.0:
            dl = min(dl, max(0.26, abs(q[2]) * 2.0))
        if r < 5.0:
            dl = min(dl, max(0.05, 0.12 * (r - rhor)))
        k1q, k1p = _kerr_deriv(q, p, a)
        dl = min(dl, 1.2 / (math.sqrt(sum(v * v for v in k1p)) + 1e-3))
        q2 = [q[i] + 0.5 * dl * k1q[i] for i in range(3)]
        p2 = [p[i] + 0.5 * dl * k1p[i] for i in range(3)]
        k2q, k2p = _kerr_deriv(q2, p2, a)
        if r < 7.0:
            q3 = [q[i] + 0.5 * dl * k2q[i] for i in range(3)]
            p3 = [p[i] + 0.5 * dl * k2p[i] for i in range(3)]
            k3q, k3p = _kerr_deriv(q3, p3, a)
            q4 = [q[i] + dl * k3q[i] for i in range(3)]
            p4 = [p[i] + dl * k3p[i] for i in range(3)]
            k4q, k4p = _kerr_deriv(q4, p4, a)
            for i in range(3):
                q[i] += dl * (k1q[i] + 2 * k2q[i] + 2 * k3q[i] + k4q[i]) / 6.0
                p[i] += dl * (k1p[i] + 2 * k2p[i] + 2 * k3p[i] + k4p[i]) / 6.0
        else:
            for i in range(3):
                q[i] += dl * k2q[i]
                p[i] += dl * k2p[i]
    outward = q[0] * p[0] + q[1] * p[1] + q[2] * p[2] > 0.0
    if outward and r_min > r_photon * 0.82:
        return "escaped", None, L
    return "captured", None, L


def deflection_angle_kerr(b, a=0.0, r_start=500.0):
    """Equatorial bending angle for impact parameter b (photon passes on the
    +z side for b > 0). Weak field, a = 0: alpha -> 4M/b."""
    status, out, _ = trace_ray_kerr([-r_start, 0.0, b], [1.0, 0.0, 0.0], a=a,
                                    r_escape=r_start * 1.05)
    if status != "escaped":
        return math.pi
    return math.acos(max(-1.0, min(1.0, float(out[0]))))


def disk_redshift(r, l_photon, a):
    """Exact Kerr redshift factor g = E_obs/E_em for a prograde circular
    emitter at radius r and a photon with angular momentum l_photon (E = 1).
    At a = 0, l = 0 this reduces to sqrt(1 - 3M/r)."""
    sr = math.sqrt(r)
    ut = (r * sr + a) / math.sqrt(r ** 3 - 3.0 * r * r + 2.0 * a * r * sr)
    omega = 1.0 / (r * sr + a)
    return 1.0 / (ut * (1.0 - omega * l_photon))


def blackbody_rgb_cpu(t):
    """Tanner Helland-style blackbody fit, T in Kelvin. Mirrors the GLSL."""
    t = max(1000.0, min(40000.0, t)) / 100.0
    if t <= 66.0:
        r = 1.0
        g = (99.4708025861 * math.log(t) - 161.1195681661) / 255.0
        b = 0.0 if t <= 19.0 else \
            (138.5177312231 * math.log(t - 10.0) - 305.0447927307) / 255.0
    else:
        r = 329.698727446 * ((t - 60.0) ** -0.1332047592) / 255.0
        g = 288.1221695283 * ((t - 60.0) ** -0.0755148492) / 255.0
        b = 1.0
    clamp = lambda x: max(0.0, min(1.0, x))
    # the fit is in gamma space; convert to linear (matches the GLSL)
    return (clamp(r) ** 2.2, clamp(g) ** 2.2, clamp(b) ** 2.2)


def novikov_thorne_peak(r_in):
    """Radius and value of the peak of f(r) = r^-3 (1 - sqrt(r_in/r))."""
    rr = np.linspace(r_in * 1.0001, r_in * 20.0, 200000)
    f = rr ** -3.0 * (1.0 - np.sqrt(r_in / rr))
    i = int(np.argmax(f))
    return float(rr[i]), float(f[i])


# ----------------------------------------------------------------------------
# Scene parameters / camera  (lengths in M; the old rs unit was 2M)
# ----------------------------------------------------------------------------

QUALITY = {  # preset: (name, max render scale, geodesic steps)
    # presets 1-3 hold the display refresh rate via dynamic resolution
    # (the scale is the ceiling); preset 4 is fixed full-resolution beauty
    1: ("low",    0.45, 112),
    2: ("medium", 0.60, 176),
    3: ("high",   0.75, 288),
    4: ("ultra",  1.00, 720),
}
DRS_FLOOR = 0.35          # dynamic resolution never drops below this scale

DISK_ROUT = 26.0             # outer disk radius
DISK_TMAX = 4600.0           # peak disk temperature (K) — scorching orange-white
DISK_GAIN = 3.4
DISK_TIME_SCALE = 20.0       # artistic speed-up of the (physical) Kerr rotation
DEFAULT_SPIN = 0.85
HDR_PEAK = 4.0               # EDR fallback when the display can't be queried
HDR_EXPOSURE = 1.35          # exposure in EDR mode (SDR keeps 0.90); the
                             # tonemap is linear below the knee, so this is a
                             # straight brightness lift that the shoulder
                             # absorbs in the highlights
FOV_DEG = 62.0
HOME = dict(yaw=0.55, pitch=0.145, dist=27.0)

def tonemap_edr_cpu(l, peak, knee=0.85):
    """Mirrors the GLSL EDR luminance curve: identity below the knee (SDR-range
    content displays at true linear brightness), then a C1 rational shoulder
    that asymptotes to `peak` (the display's EDR headroom in SDR-white units)."""
    peak = max(peak, knee + 0.15)
    if l <= knee:
        return l
    return knee + (peak - knee) * (l - knee) / (peak - 2.0 * knee + l)


def edr_headroom(default=HDR_PEAK):
    """Current EDR headroom of the main display, in multiples of SDR white
    (NSScreen.maximumExtendedDynamicRangeColorComponentValue; 1600-nit XDR
    panels report up to ~16x at low brightness). The value tracks the
    brightness slider live, so callers re-poll. Falls back to `default` when
    the query fails or macOS hasn't ramped EDR up yet (reports 1.0)."""
    if sys.platform != "darwin":
        return default
    try:
        objc = ctypes.CDLL("/usr/lib/libobjc.A.dylib")
        ctypes.CDLL("/System/Library/Frameworks/AppKit.framework/AppKit")
        objc.objc_getClass.restype = ctypes.c_void_p
        objc.objc_getClass.argtypes = [ctypes.c_char_p]
        objc.sel_registerName.restype = ctypes.c_void_p
        objc.sel_registerName.argtypes = [ctypes.c_char_p]
        send_id = ctypes.cast(objc.objc_msgSend, ctypes.CFUNCTYPE(
            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p))
        send_f64 = ctypes.cast(objc.objc_msgSend, ctypes.CFUNCTYPE(
            ctypes.c_double, ctypes.c_void_p, ctypes.c_void_p))
        screen = send_id(objc.objc_getClass(b"NSScreen"),
                         objc.sel_registerName(b"mainScreen"))
        if not screen:
            return default
        v = send_f64(screen, objc.sel_registerName(
            b"maximumExtendedDynamicRangeColorComponentValue"))
        return min(v, 16.0) if v > 1.0 else default
    except Exception:
        return default


_disk_cache = {}


def disk_params(spin):
    """Spin-dependent disk geometry: inner edge at the prograde ISCO, and the
    Novikov-Thorne normalization for that inner edge."""
    if spin not in _disk_cache:
        rin = kerr_isco(spin)
        _disk_cache[spin] = (rin, novikov_thorne_peak(rin)[1])
    return _disk_cache[spin]


def camera_vectors(yaw, pitch):
    """Orbit camera looking at the origin. Returns unit position and the
    basis columns (right, up, -forward) used by the shader."""
    cp, sp = math.cos(pitch), math.sin(pitch)
    cy, sy = math.cos(yaw), math.sin(yaw)
    pos = np.array([cy * cp, sp, sy * cp])
    fwd = -pos
    right = np.cross(fwd, [0.0, 1.0, 0.0])
    right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    return pos, right, up, -fwd


def scene_push_constants(yaw, pitch, dist, t, res, steps,
                         spin=DEFAULT_SPIN, doppler=True, glow=True):
    pos_u, right, up, negfwd = camera_vectors(yaw, pitch)
    campos = pos_u * dist
    cam_f, cam_k, _, _, _ = _ks_fk((campos[0], campos[2], campos[1]), spin)
    rin, nt_peak = disk_params(spin)
    return struct.pack(
        "32f",
        *right, cam_f, *up, cam_k[0], *negfwd, cam_k[1],
        *campos, t,
        float(res[0]) / float(res[1]), cam_k[2],
        math.tan(math.radians(FOV_DEG) * 0.5), float(steps),
        rin, DISK_ROUT, nt_peak, DISK_TMAX,
        DISK_GAIN, 1.0 if doppler else 0.0, 1.0 if glow else 0.0,
        max(60.0, dist * 1.3),
        spin, DISK_TIME_SCALE, kerr_horizon(spin), photon_sphere_r(spin))


def composite_push_constants(t, bloom=True, exposure=0.90, hdr_peak=0.0,
                             upscale=False):
    # upscale selects Catmull-Rom resampling of the scene target (used when
    # dynamic resolution renders below the output size)
    return struct.pack("8f", 0.30 if bloom else 0.0, exposure, t, hdr_peak,
                       1.0 if upscale else 0.0, 0.0, 0.0, 0.0)


# ----------------------------------------------------------------------------
# Vulkan library bootstrap (macOS): libvulkan lives in /opt/homebrew/lib which
# is not on dyld's default search path. dyld only reads DYLD_LIBRARY_PATH at
# process start, so re-exec once with it set.
# ----------------------------------------------------------------------------

def ensure_vulkan_loadable():
    try:
        import vulkan  # noqa: F401
        return
    except OSError:
        pass
    if os.environ.get("_BH_REEXEC"):
        sys.exit("error: could not load libvulkan even with DYLD_LIBRARY_PATH set.\n"
                 "Install the runtime with:  brew install vulkan-loader molten-vk")
    libdir = next((d for d in ("/opt/homebrew/lib", "/usr/local/lib")
                   if os.path.exists(os.path.join(d, "libvulkan.dylib"))), None)
    if libdir is None:
        sys.exit("error: libvulkan.dylib not found.\n"
                 "Install it with:  brew install vulkan-loader molten-vk")
    env = dict(os.environ, _BH_REEXEC="1")
    env["DYLD_LIBRARY_PATH"] = libdir + (
        ":" + env["DYLD_LIBRARY_PATH"] if "DYLD_LIBRARY_PATH" in env else "")
    os.execve(sys.executable, [sys.executable] + sys.argv, env)


# ----------------------------------------------------------------------------
# Offscreen still
# ----------------------------------------------------------------------------

def render_still(path, size, yaw=None, pitch=None, dist=None, t=38.0,
                 steps=None, spin=DEFAULT_SPIN, doppler=True, glow=True,
                 bloom=True, quiet=False):
    import vulkan as vk
    import shaders
    import vkrenderer as vkr

    yaw = HOME["yaw"] if yaw is None else yaw
    pitch = HOME["pitch"] if pitch is None else pitch
    dist = HOME["dist"] if dist is None else dist
    steps = steps or QUALITY[4][2]

    ctx = vkr.VulkanContext(window=None)
    if not quiet:
        print(f"GPU: {ctx.device_name}")
    engine = vkr.Engine(ctx, shaders.compile_all(), size, vkr.LDR_FORMAT,
                        vk.VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)

    out = ctx.create_image(size[0], size[1], vkr.LDR_FORMAT,
                           vk.VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                           | vk.VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
    fb = vk.vkCreateFramebuffer(ctx.device, vk.VkFramebufferCreateInfo(
        renderPass=engine.rp_out, attachmentCount=1, pAttachments=[out.view],
        width=size[0], height=size[1], layers=1), None)
    nbytes = size[0] * size[1] * 4
    buf, mem = ctx.create_buffer(
        nbytes, vk.VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        vk.VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | vk.VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)

    cmd = ctx.alloc_cmd()
    vk.vkBeginCommandBuffer(cmd, vk.VkCommandBufferBeginInfo(
        flags=vk.VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
    engine.record(
        cmd, fb, size,
        scene_push_constants(yaw, pitch, dist, t, size, steps, spin,
                             doppler, glow),
        composite_push_constants(t, bloom),
        bloom=bloom)
    region = vk.VkBufferImageCopy(
        bufferOffset=0, bufferRowLength=0, bufferImageHeight=0,
        imageSubresource=vk.VkImageSubresourceLayers(
            aspectMask=vk.VK_IMAGE_ASPECT_COLOR_BIT,
            mipLevel=0, baseArrayLayer=0, layerCount=1),
        imageOffset=vk.VkOffset3D(x=0, y=0, z=0),
        imageExtent=vk.VkExtent3D(width=size[0], height=size[1], depth=1))
    vk.vkCmdCopyImageToBuffer(cmd, out.handle,
                              vk.VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, [region])
    vk.vkEndCommandBuffer(cmd)
    ctx.submit_and_wait(cmd)

    mapped = vk.vkMapMemory(ctx.device, mem, 0, nbytes, 0)  # returns ffi.buffer
    data = bytes(mapped)[:nbytes]
    vk.vkUnmapMemory(ctx.device, mem)
    ctx.wait_idle()

    from PIL import Image as PILImage
    img = PILImage.frombytes("RGBA", size, data).convert("RGB")
    img.save(path)
    if not quiet:
        print(f"saved {path}  ({size[0]}x{size[1]}, {steps} geodesic steps/px)")


# ----------------------------------------------------------------------------
# Interactive
# ----------------------------------------------------------------------------

def run_interactive(win_size, quality, spin=DEFAULT_SPIN, want_hdr=True):
    import glfw
    import vulkan as vk
    import shaders
    import vkrenderer as vkr

    if not glfw.init():
        sys.exit("glfw init failed")
    glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)
    window = glfw.create_window(win_size[0], win_size[1],
                                "Gargantua — Kerr black hole (Vulkan/Metal)",
                                None, None)
    ctx = vkr.VulkanContext(window=window)
    print(f"GPU: {ctx.device_name}  (Vulkan via MoltenVK)")

    spirv = shaders.compile_all()
    swap = vkr.Swapchain(ctx)
    if want_hdr:
        swap.recreate(hdr=True)
    print("output:",
          f"HDR — float16 extended sRGB (EDR headroom {edr_headroom():.1f}x SDR white)"
          if swap.is_hdr
          else "SDR — 8-bit" + (" (HDR not offered by surface)" if want_hdr else ""))
    _, scale, steps = QUALITY[quality]
    fbw, fbh = glfw.get_framebuffer_size(window)
    engine = vkr.Engine(ctx, spirv, (int(fbw * scale), int(fbh * scale)),
                        swap.format, vk.VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)

    # dynamic resolution: hold the display's refresh rate by trading render
    # scale, between DRS_FLOOR and the preset's ceiling (ultra opts out)
    mode = glfw.get_video_mode(glfw.get_primary_monitor())
    refresh = mode.refresh_rate or 60
    frame_budget = 1000.0 / refresh
    drs = dict(scale=scale, changed=0.0, last_uncached_ms=None,
               nf=0, t0=time.perf_counter())

    framebuffers = []

    def build_framebuffers():
        nonlocal framebuffers
        for f in framebuffers:
            vk.vkDestroyFramebuffer(ctx.device, f, None)
        framebuffers = [
            vk.vkCreateFramebuffer(ctx.device, vk.VkFramebufferCreateInfo(
                renderPass=engine.rp_out, attachmentCount=1, pAttachments=[v],
                width=swap.extent[0], height=swap.extent[1], layers=1), None)
            for v in swap.views]

    build_framebuffers()

    frames = [
        dict(
            acquire=vk.vkCreateSemaphore(ctx.device, vk.VkSemaphoreCreateInfo(), None),
            render=vk.vkCreateSemaphore(ctx.device, vk.VkSemaphoreCreateInfo(), None),
            fence=vk.vkCreateFence(ctx.device, vk.VkFenceCreateInfo(
                flags=vk.VK_FENCE_CREATE_SIGNALED_BIT), None),
            cmd=ctx.alloc_cmd(),
            submit_time=None,
        )
        for _ in range(2)
    ]

    err_out_of_date = getattr(vk, "VkErrorOutOfDateKhr", RuntimeError)
    suboptimal = getattr(vk, "VkSuboptimalKhr", RuntimeError)

    state = dict(yaw=HOME["yaw"], pitch=HOME["pitch"], dist=HOME["dist"],
                 drag=False, mouse=(0.0, 0.0), t=20.0, paused=False,
                 doppler=True, glow=True, bloom=True, spin=spin,
                 hdr=swap.is_hdr, quality=quality, edr_peak=edr_headroom(),
                 last_input=time.time(), resize=False)

    def on_mouse_button(_w, button, action, _mods):
        if button == glfw.MOUSE_BUTTON_LEFT:
            state["drag"] = action == glfw.PRESS
            state["mouse"] = glfw.get_cursor_pos(window)
            state["last_input"] = time.time()

    def on_cursor(_w, x, y):
        if state["drag"]:
            dx, dy = x - state["mouse"][0], y - state["mouse"][1]
            state["mouse"] = (x, y)
            state["yaw"] += dx * 0.005
            state["pitch"] = max(-1.45, min(1.45, state["pitch"] + dy * 0.004))
            state["last_input"] = time.time()

    def on_scroll(_w, _dx, dy):
        state["dist"] = max(6.0, min(120.0, state["dist"] * (0.92 ** dy)))
        state["last_input"] = time.time()

    def on_key(_w, key, _sc, action, _mods):
        if action != glfw.PRESS:
            return
        state["last_input"] = time.time()
        if key in (glfw.KEY_ESCAPE, glfw.KEY_Q):
            glfw.set_window_should_close(window, True)
        elif key == glfw.KEY_SPACE:
            state["paused"] = not state["paused"]
        elif key == glfw.KEY_B:
            state["bloom"] = not state["bloom"]
        elif key == glfw.KEY_D:
            state["doppler"] = not state["doppler"]
        elif key == glfw.KEY_G:
            state["glow"] = not state["glow"]
        elif key == glfw.KEY_R:
            state.update(yaw=HOME["yaw"], pitch=HOME["pitch"], dist=HOME["dist"])
        elif key == glfw.KEY_S:
            fn = time.strftime("blackhole_%Y%m%d_%H%M%S.png")
            print(f"rendering {fn} ...")
            render_still(fn, glfw.get_framebuffer_size(window),
                         yaw=state["yaw"], pitch=state["pitch"], dist=state["dist"],
                         t=state["t"], spin=state["spin"],
                         doppler=state["doppler"], glow=state["glow"],
                         bloom=state["bloom"], quiet=True)
            print(f"saved {fn}")
        elif key == glfw.KEY_H:
            state["hdr"] = not state["hdr"]
            state["resize"] = True
        elif key == glfw.KEY_LEFT_BRACKET:
            state["spin"] = max(0.0, round(state["spin"] - 0.05, 2))
        elif key == glfw.KEY_RIGHT_BRACKET:
            state["spin"] = min(0.998, round(state["spin"] + 0.05, 2))
        elif key in (glfw.KEY_1, glfw.KEY_2, glfw.KEY_3, glfw.KEY_4):
            state["quality"] = key - glfw.KEY_0
            state["resize"] = True

    def on_fb_resize(_w, _width, _height):
        state["resize"] = True

    glfw.set_mouse_button_callback(window, on_mouse_button)
    glfw.set_cursor_pos_callback(window, on_cursor)
    glfw.set_scroll_callback(window, on_scroll)
    glfw.set_key_callback(window, on_key)
    glfw.set_framebuffer_size_callback(window, on_fb_resize)

    cache_key = None

    def recreate():
        nonlocal cache_key, pending_key
        w, h = glfw.get_framebuffer_size(window)
        if w == 0 or h == 0:
            return False
        ctx.wait_idle()
        swap.recreate(hdr=state["hdr"])
        state["hdr"] = swap.is_hdr
        engine.set_output(swap.format, vk.VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        _, sc, st = QUALITY[state["quality"]]
        drs.update(scale=sc, ema=None, changed=time.perf_counter())
        engine.rebuild((int(w * sc), int(h * sc)))
        nonlocal_steps[0] = st
        build_framebuffers()
        cache_key = None
        pending_key = None
        state["resize"] = False
        return True

    def _apply_scale(new):
        nonlocal cache_key
        drs.update(scale=new, changed=time.perf_counter(),
                   nf=0, t0=time.perf_counter())
        w, h = glfw.get_framebuffer_size(window)
        ctx.wait_idle()
        engine.rebuild((int(w * new), int(h * new)))
        cache_key = None    # rebuild recreates (clears) the transport cache

    def drs_update(gpu_ms, frame_kind):
        """Hold the display refresh rate by trading render scale.

        frame_kind:
          "moving"  — camera moved this frame; full geodesic cost drives the
                      proportional controller (scale down when slow, up when
                      there is headroom).
          "rebuild" — one-time transport-cache build after the camera settles;
                      a transient investment, not a steady-state cost, so it
                      never drives a scale change.
          "cached"  — transport reused (~1 ms). The headroom is spent climbing
                      back toward full res, but only when the predicted
                      uncached cost at the target scale stays under a stutter
                      threshold so the next rebuild stays brief.
        """
        ceiling = QUALITY[state["quality"]][1]
        now = time.perf_counter()

        if frame_kind == "moving":
            drs["last_uncached_ms"] = gpu_ms
            # The fence interval includes the wait for a swapchain image, so
            # once vsync-locked it saturates at ~the frame period and says
            # nothing about GPU headroom (and rebuild hitches contaminate it).
            # Control on the achieved frame rate instead: below refresh ->
            # scale down; holding refresh -> probe upward toward the ceiling.
            drs["nf"] += 1
            window = now - drs["t0"]
            if window < 0.7:
                return
            fps = drs["nf"] / window
            drs["nf"], drs["t0"] = 0, now
            if now - drs["changed"] < 0.9:
                return
            # the loop is CPU-bound slightly below the refresh rate, so hold
            # anywhere above 0.94x refresh (ProMotion is adaptive-sync) and
            # only spend the headroom when the rate is truly sustained
            if fps < refresh * 0.90:
                new = drs["scale"] * max(0.6, math.sqrt(fps / refresh))
            elif fps > refresh * 0.94 and drs["scale"] < ceiling:
                new = drs["scale"] * 1.06
            else:
                return
            new = max(DRS_FLOOR, min(ceiling, new))
            if abs(new - drs["scale"]) / drs["scale"] < 0.03:
                return
            _apply_scale(new)
            return

        if frame_kind == "cached":
            # ultra jumps straight back to full res when the camera settles
            # (see the pending_key branch); no gradual climb needed
            if state["quality"] >= 4:
                return
            if drs["scale"] < ceiling and now - drs["changed"] > 1.0:
                target = min(ceiling, drs["scale"] * 1.10)
                # gate the climb on the predicted uncached cost (cost ~ scale^2
                # via pixel count) so the one-time rebuild stays a brief hitch
                if drs["last_uncached_ms"] is not None:
                    predicted = drs["last_uncached_ms"] * \
                        (target / drs["scale"]) ** 2
                    if predicted > frame_budget * 3.0:
                        return
                _apply_scale(target)
            return
        # "rebuild": no action — a transient investment in future cheap frames

    nonlocal_steps = [steps]
    prev = time.perf_counter()
    fps_t, fps_n = time.time(), 0
    frame_i = 0
    pending_key = None

    while not glfw.window_should_close(window):
        glfw.poll_events()
        now = time.perf_counter()
        dt = min(now - prev, 0.1)
        prev = now

        if state["resize"] and not recreate():
            time.sleep(0.05)
            continue

        if not state["paused"]:
            state["t"] += dt
        # gentle idle orbit: resumes ~0.8 s after you stop touching the camera
        # (a click no longer freezes it for seconds). Disabled at ultra: a
        # drifting camera never "settles", so it would defeat the transport
        # cache and the 2x SSAA reshade — at ultra the still camera stays crisp
        # and supersampled while the disc plasma keeps churning for life.
        if state["quality"] < 4 and not state["drag"] \
                and time.time() - state["last_input"] > 0.8:
            state["yaw"] += dt * 0.05

        frame = frames[frame_i]
        vk.vkWaitForFences(ctx.device, 1, [frame["fence"]], vk.VK_TRUE, int(1e9))

        # ultra: dynamic resolution while the camera moves; the key describes
        # the settled full-res view
        ultra_key = None
        if state["quality"] >= 4:
            ultra_key = (glfw.get_framebuffer_size(window), nonlocal_steps[0],
                         state["spin"], state["yaw"], state["pitch"],
                         state["dist"], state["glow"])

        # The fence marks completion of this frame slot's previous GPU work.
        # With multiple slots this is an upper bound, still good enough for DRS.
        if frame["submit_time"] is not None:
            kind = frame.get("frame_kind", "moving")
            # a moving-frame timing arriving after the camera settled is
            # stale; acting on it would trash the just-built transport cache
            if state["quality"] >= 4 and kind == "moving" \
                    and ultra_key in (cache_key, pending_key):
                kind = None
            if kind:
                drs_update((time.perf_counter() - frame["submit_time"]) * 1000.0,
                           kind)
            frame["submit_time"] = None

        # camera settled: snap back to full resolution right before the
        # one-time transport-cache build
        if ultra_key is not None and ultra_key == pending_key \
                and ultra_key != cache_key and drs["scale"] < 0.999:
            _apply_scale(1.0)
        try:
            idx = swap.acquire(frame["acquire"])
        except (err_out_of_date, suboptimal):
            state["resize"] = True
            continue
        vk.vkResetFences(ctx.device, 1, [frame["fence"]])

        cmd = frame["cmd"]
        vk.vkBeginCommandBuffer(cmd, vk.VkCommandBufferBeginInfo(
            flags=vk.VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
        scene_pc = scene_push_constants(
            state["yaw"], state["pitch"], state["dist"], state["t"],
            engine.render_size, nonlocal_steps[0], state["spin"],
            state["doppler"], state["glow"])
        comp_pc = composite_push_constants(
            state["t"], state["bloom"],
            exposure=HDR_EXPOSURE if state["hdr"] else 0.90,
            hdr_peak=state["edr_peak"] if state["hdr"] else 0.0,
            upscale=engine.render_size[0] < swap.extent[0])
        if state["quality"] >= 4:
            key = ultra_key
            if key == cache_key:
                # static: reuse cached geodesic transport, re-shade the disk
                engine.record_cached(cmd, framebuffers[idx], swap.extent,
                                     scene_pc, comp_pc, rebuild_cache=False,
                                     bloom=state["bloom"])
                frame["frame_kind"] = "cached"
            elif key == pending_key:
                # camera settled this frame: build the transport cache once
                engine.record_cached(cmd, framebuffers[idx], swap.extent,
                                     scene_pc, comp_pc, rebuild_cache=True,
                                     bloom=state["bloom"])
                cache_key = key
                pending_key = None
                frame["frame_kind"] = "rebuild"
            else:
                # camera moving: plain geodesic pass (no 5-target MRT writes)
                engine.record(cmd, framebuffers[idx], swap.extent,
                              scene_pc, comp_pc, bloom=state["bloom"])
                pending_key = key
                frame["frame_kind"] = "moving"
        else:
            engine.record(cmd, framebuffers[idx], swap.extent,
                          scene_pc, comp_pc, bloom=state["bloom"])
            frame["frame_kind"] = "moving"
        vk.vkEndCommandBuffer(cmd)

        vk.vkQueueSubmit(ctx.queue, 1, [vk.VkSubmitInfo(
            waitSemaphoreCount=1, pWaitSemaphores=[frame["acquire"]],
            pWaitDstStageMask=[vk.VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT],
            commandBufferCount=1, pCommandBuffers=[cmd],
            signalSemaphoreCount=1, pSignalSemaphores=[frame["render"]])],
            frame["fence"])
        frame["submit_time"] = time.perf_counter()
        try:
            swap.present(idx, frame["render"])
        except (err_out_of_date, suboptimal):
            state["resize"] = True
        frame_i = (frame_i + 1) % len(frames)

        fps_n += 1
        if time.time() - fps_t > 0.5:
            # EDR headroom tracks the brightness slider live; re-poll with
            # the title update so the tonemap follows the display
            if state["hdr"]:
                state["edr_peak"] = edr_headroom()
            name = QUALITY[state["quality"]][0]
            rw, rh = engine.render_size
            if os.environ.get("BH_FPS_LOG"):
                print(f"{fps_n / (time.time() - fps_t):6.1f} fps   "
                      f"scale {drs['scale']:.2f}   render {rw}x{rh}   "
                      f"gpu {drs['last_uncached_ms'] or 0.0:5.2f} ms", flush=True)
            glfw.set_window_title(
                window,
                f"Gargantua — Kerr a={state['spin']:.2f}   "
                f"{f'HDR {state['edr_peak']:.1f}x' if state['hdr'] else 'SDR'}   "
                f"{fps_n / (time.time() - fps_t):.0f} fps   {name} {rw}x{rh}   "
                f"r = {state['dist']:.0f} M")
            fps_t, fps_n = time.time(), 0

    ctx.wait_idle()
    glfw.terminate()


def main():
    ap = argparse.ArgumentParser(description="Kerr black hole renderer (Vulkan/MoltenVK)")
    ap.add_argument("--size", default="1280x720", help="window size WxH")
    ap.add_argument("--quality", type=int, default=3, choices=[1, 2, 3, 4])
    ap.add_argument("--spin", type=float, default=DEFAULT_SPIN,
                    help="black hole spin a/M in [0, 0.998]")
    ap.add_argument("--sdr", action="store_true",
                    help="force 8-bit SDR output (default: HDR/EDR when available)")
    ap.add_argument("--shot", metavar="PATH", help="render one offscreen still and exit")
    ap.add_argument("--yaw", type=float, default=None)
    ap.add_argument("--pitch", type=float, default=None)
    ap.add_argument("--dist", type=float, default=None)
    ap.add_argument("--time", type=float, default=38.0, help="disk time for --shot")
    a = ap.parse_args()
    w, h = (int(x) for x in a.size.lower().split("x"))
    spin = max(0.0, min(0.998, a.spin))

    ensure_vulkan_loadable()
    if a.shot:
        render_still(a.shot, (w, h), yaw=a.yaw, pitch=a.pitch, dist=a.dist,
                     t=a.time, spin=spin,
                     steps=QUALITY[a.quality][2] if a.quality != 3
                     else QUALITY[4][2])
    else:
        run_interactive((w, h), a.quality, spin=spin, want_hdr=not a.sdr)


if __name__ == "__main__":
    main()
