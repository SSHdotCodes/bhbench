"""Tests for the Kerr black hole simulation.

Run with pytest, or directly:  .venv/bin/python test_blackhole.py
"""

import math
import struct

import numpy as np

import blackhole as bh
import shaders

SPIRV_MAGIC = b"\x03\x02\x23\x07"   # 0x07230203 little-endian


# ---------------------------------------------------------------------------
# Kerr geometry
# ---------------------------------------------------------------------------

def test_horizon_radius():
    assert bh.kerr_horizon(0.0) == 2.0                      # Schwarzschild rs
    assert abs(bh.kerr_horizon(0.998) - 1.0632) < 1e-3


def test_isco_matches_bardeen_formula():
    assert abs(bh.kerr_isco(0.0) - 6.0) < 1e-9
    assert abs(bh.kerr_isco(0.998) - 1.2370) < 1e-3
    spins = [0.0, 0.3, 0.6, 0.9, 0.998]
    iscos = [bh.kerr_isco(a) for a in spins]
    assert all(x > y for x, y in zip(iscos, iscos[1:])), iscos


def test_ks_radius_reduces_to_spherical_at_zero_spin():
    assert abs(bh.ks_radius((3.0, 4.0, 12.0), 0.0) - 13.0) < 1e-12


def test_photon_sphere_radius():
    assert abs(bh.photon_sphere_r(0.0) - 3.0) < 1e-9
    assert abs(bh.photon_sphere_r(0.998) - 1.0739) < 1e-3


# ---------------------------------------------------------------------------
# null geodesics (Kerr-Schild Hamiltonian, mirrors the GLSL)
# ---------------------------------------------------------------------------

def test_init_photon_satisfies_null_condition():
    for a in (0.0, 0.5, 0.85, 0.998):
        q, p, _ = bh.kerr_init_photon([-27.0, 4.0, 8.0], [1.0, -0.1, -0.2], a)
        assert abs(bh.kerr_hamiltonian(q, p, a)) < 1e-12


def test_null_condition_conserved_along_trajectory():
    a = 0.85
    q, p, _ = bh.kerr_init_photon([-30.0, 2.0, 6.0], [1.0, -0.05, -0.15], a)
    q, p = list(q), list(p)
    for _ in range(4000):
        dq, dp = bh._kerr_deriv(q, p, a)
        # RK4 step, dl = 0.02
        dl = 0.02
        q2 = [q[i] + 0.5 * dl * dq[i] for i in range(3)]
        p2 = [p[i] + 0.5 * dl * dp[i] for i in range(3)]
        dq2, dp2 = bh._kerr_deriv(q2, p2, a)
        q3 = [q[i] + 0.5 * dl * dq2[i] for i in range(3)]
        p3 = [p[i] + 0.5 * dl * dp2[i] for i in range(3)]
        dq3, dp3 = bh._kerr_deriv(q3, p3, a)
        q4 = [q[i] + dl * dq3[i] for i in range(3)]
        p4 = [p[i] + dl * dp3[i] for i in range(3)]
        dq4, dp4 = bh._kerr_deriv(q4, p4, a)
        for i in range(3):
            q[i] += dl * (dq[i] + 2 * dq2[i] + 2 * dq3[i] + dq4[i]) / 6.0
            p[i] += dl * (dp[i] + 2 * dp2[i] + 2 * dp3[i] + dp4[i]) / 6.0
        if bh.ks_radius(q, a) < bh.kerr_horizon(a) + 0.1:
            break
    assert abs(bh.kerr_hamiltonian(q, p, a)) < 1e-4


def test_angular_momentum_is_conserved():
    a = 0.85
    q, p, L0 = bh.kerr_init_photon([-30.0, 2.0, 6.0], [1.0, -0.05, -0.15], a)
    status, _, L1 = bh.trace_ray_kerr([-30.0, 2.0, 6.0], [1.0, -0.05, -0.15], a=a)
    assert L1 == L0     # L is carried, not re-derived; init must be stable
    # and the trajectory itself conserves q x p about the spin axis:
    q, p = list(q), list(p)
    for _ in range(2000):
        dq, dp = bh._kerr_deriv(q, p, a)
        for i in range(3):
            q[i] += 0.02 * dq[i]
            p[i] += 0.02 * dp[i]
        if bh.ks_radius(q, a) < bh.kerr_horizon(a) + 0.1:
            break
    l_now = q[0] * p[1] - q[1] * p[0]
    assert abs(l_now - L0) < 5e-3 * max(abs(L0), 1.0)


def test_photon_capture_below_critical_impact_parameter():
    """Schwarzschild critical impact parameter is 3*sqrt(3) M ~ 5.196 M."""
    s, _, _ = bh.trace_ray_kerr([-100.0, 0.0, 4.5], [1, 0, 0], a=0.0,
                                r_escape=110.0)
    assert s == "captured"
    s, _, _ = bh.trace_ray_kerr([-100.0, 0.0, 6.0], [1, 0, 0], a=0.0,
                                r_escape=110.0)
    assert s == "escaped"


def test_weak_field_deflection_matches_general_relativity():
    """alpha = 4M/b + (15 pi / 4)(M/b)^2 + O(b^-3)."""
    b = 60.0
    alpha = bh.deflection_angle_kerr(b, a=0.0, r_start=500.0)
    expected = 4.0 / b + 15.0 * math.pi / 4.0 / (b * b)
    assert abs(alpha - expected) / expected < 0.02, (alpha, expected)


def test_frame_dragging_capture_asymmetry():
    """At a = 0.9 the same |b| is captured retrograde but escapes prograde."""
    s_pro, _, l_pro = bh.trace_ray_kerr([-100.0, 0.0, -4.5], [1, 0, 0], a=0.9,
                                        r_escape=110.0)
    s_ret, _, l_ret = bh.trace_ray_kerr([-100.0, 0.0, 4.5], [1, 0, 0], a=0.9,
                                        r_escape=110.0)
    assert l_pro > 0 and s_pro == "escaped"
    assert l_ret < 0 and s_ret == "captured"


def test_near_polar_ray_is_smooth():
    """Regression: rays crossing near the spin axis must not stall or be
    ejected (the Boyer-Lindquist implementation had a polar seam)."""
    s, out, _ = bh.trace_ray_kerr([-27.0, 2.0, 0.0], [1.0, 0.25, 0.0], a=0.85)
    assert s == "escaped" and out is not None
    assert abs(float(np.linalg.norm(out)) - 1.0) < 1e-9


def test_shadow_guard_rejects_near_horizon_leakage():
    """Rays that graze the photon sphere must not be reported as escaped."""
    s, out, _ = bh.trace_ray_kerr([-100.0, 0.0, 4.8], [1.0, 0.0, 0.0], a=0.0,
                                  r_escape=110.0)
    assert s == "captured" and out is None


# ---------------------------------------------------------------------------
# radiation / disk model
# ---------------------------------------------------------------------------

def test_disk_redshift_reduces_to_schwarzschild():
    assert abs(bh.disk_redshift(8.0, 0.0, 0.0) - math.sqrt(1 - 3 / 8)) < 1e-12


def test_disk_redshift_doppler_asymmetry():
    g_approach = bh.disk_redshift(6.0, 5.0, 0.85)
    g_recede = bh.disk_redshift(6.0, -5.0, 0.85)
    assert g_approach > 1.0 > g_recede > 0.0


def test_blackbody_is_warm_at_low_temperature():
    r, g, b = bh.blackbody_rgb_cpu(3000.0)
    assert r > g > b


def test_blackbody_is_cool_at_high_temperature():
    r, g, b = bh.blackbody_rgb_cpu(20000.0)
    assert b > r


def test_blackbody_blue_red_ratio_increases_with_temperature():
    ratios = [bh.blackbody_rgb_cpu(t)[2] / bh.blackbody_rgb_cpu(t)[0]
              for t in (2000, 4000, 8000, 16000, 32000)]
    assert all(x < y for x, y in zip(ratios, ratios[1:])), ratios


def test_novikov_thorne_peak_matches_analytic_radius():
    """d/dr [r^-3 (1 - sqrt(rin/r))] = 0  =>  r_peak = (49/36) r_in."""
    r_in = bh.kerr_isco(0.85)
    r_peak, f_peak = bh.novikov_thorne_peak(r_in)
    assert abs(r_peak - 49.0 / 36.0 * r_in) < 0.01
    assert f_peak > 0


# ---------------------------------------------------------------------------
# HDR / EDR output
# ---------------------------------------------------------------------------

def test_tonemap_edr_is_identity_below_knee():
    """SDR-range luminance must display at true linear brightness — the old
    curve halved midtones (l=1 -> 0.53), which made HDR output muddy."""
    for peak in (2.0, 4.0, 8.0, 16.0):
        for l in (0.0, 0.2, 0.5, 0.85):
            assert bh.tonemap_edr_cpu(l, peak) == l


def test_tonemap_edr_shoulder_is_monotonic_and_bounded_by_peak():
    for peak in (1.0, 2.0, 4.0, 8.0, 16.0):
        ls = np.linspace(0.0, 400.0, 4000)
        out = [bh.tonemap_edr_cpu(l, peak) for l in ls]
        assert all(x < y for x, y in zip(out, out[1:]))
        assert out[-1] < max(peak, 1.0), (peak, out[-1])
        # the shoulder must actually use the headroom, not hover near SDR
        if peak >= 4.0:
            assert out[-1] > 0.9 * peak


def test_tonemap_edr_is_c1_at_the_knee():
    """The linear section and the shoulder must join with slope 1 or the
    transition bands visibly."""
    knee, eps = 0.85, 1e-6
    for peak in (2.0, 8.0):
        lo = (bh.tonemap_edr_cpu(knee, peak)
              - bh.tonemap_edr_cpu(knee - eps, peak)) / eps
        hi = (bh.tonemap_edr_cpu(knee + eps, peak)
              - bh.tonemap_edr_cpu(knee, peak)) / eps
        assert abs(lo - 1.0) < 1e-4 and abs(hi - 1.0) < 1e-4


def test_tonemap_knee_agrees_between_cpu_and_glsl():
    import inspect
    import re
    m = re.search(r"const float knee = ([0-9.]+);", shaders.FRAG_COMPOSITE)
    assert m, "knee constant not found in composite GLSL"
    sig = inspect.signature(bh.tonemap_edr_cpu)
    assert float(m.group(1)) == sig.parameters["knee"].default


def test_edr_headroom_is_sane_and_has_fallback():
    v = bh.edr_headroom()
    assert 1.0 < v <= 16.0
    assert bh.edr_headroom(default=7.5) >= 1.0   # never below SDR white


def test_edr_peak_is_polled_and_threaded_to_composite():
    import inspect
    src = inspect.getsource(bh.run_interactive)
    assert 'state["edr_peak"] = edr_headroom()' in src
    assert 'hdr_peak=state["edr_peak"] if state["hdr"] else 0.0' in src
    # EDR mode gets an exposure lift; SDR keeps the reference 0.90
    assert 'exposure=HDR_EXPOSURE if state["hdr"] else 0.90' in src
    assert bh.HDR_EXPOSURE > 0.90


# ---------------------------------------------------------------------------
# GPU pipeline plumbing
# ---------------------------------------------------------------------------

def test_push_constant_sizes():
    scene = bh.scene_push_constants(0.5, 0.1, 27.0, 20.0, (1280, 720), 256)
    comp = bh.composite_push_constants(20.0, hdr_peak=4.0)
    assert len(scene) == 128
    assert len(comp) == 32


def test_composite_upscale_flag_is_packed_and_gated():
    """The 5th composite float selects Catmull-Rom resampling; it must be set
    exactly when dynamic resolution renders below the output size."""
    off = struct.unpack("8f", bh.composite_push_constants(20.0))
    on = struct.unpack("8f", bh.composite_push_constants(20.0, upscale=True))
    assert off[4] == 0.0 and on[4] == 1.0
    assert "sampleSceneCR" in shaders.FRAG_COMPOSITE
    assert "pc.q.x > 0.5" in shaders.FRAG_COMPOSITE
    # 9-tap Catmull-Rom: 9 scene fetches inside the resample helper
    cr_body = shaders.FRAG_COMPOSITE.split("vec3 sampleSceneCR")[1] \
                                    .split("vec3 aces")[0]
    assert cr_body.count("texture(u_scene") == 9
    import inspect
    src = inspect.getsource(bh.run_interactive)
    assert "upscale=engine.render_size[0] < swap.extent[0]" in src


def test_scene_push_constants_pack_invariant_camera_terms():
    vals = struct.unpack("32f", bh.scene_push_constants(
        0.5, 0.1, 27.0, 20.0, (1280, 720), 256))
    campos = vals[12:15]
    f, k, *_ = bh._ks_fk((campos[0], campos[2], campos[1]), bh.DEFAULT_SPIN)
    assert abs(vals[3] - f) < 1e-6
    assert np.allclose([vals[7], vals[11], vals[17]], k, atol=1e-6)
    assert abs(vals[16] - 1280 / 720) < 1e-6


def test_quality_presets_increase_resolution_and_steps():
    scales = [bh.QUALITY[i][1] for i in sorted(bh.QUALITY)]
    steps = [bh.QUALITY[i][2] for i in sorted(bh.QUALITY)]
    assert scales == sorted(scales)
    assert steps == sorted(steps)
    assert scales[-1] == 1.0
    assert steps[-1] >= 700


def test_bloom_toggle_is_threaded_to_render_graph():
    import inspect

    still_src = inspect.getsource(bh.render_still)
    interactive_src = inspect.getsource(bh.run_interactive)
    assert "bloom=bloom" in still_src
    assert 'bloom=state["bloom"]' in interactive_src


def test_shaders_compile_to_spirv():
    spirv = shaders.compile_all()
    assert set(spirv) == set(shaders.SHADERS)
    for name, code in spirv.items():
        assert code[:4] == SPIRV_MAGIC, f"{name} is not SPIR-V"
        assert len(code) % 4 == 0


def test_disk_noise_texture_domain_agrees_between_bake_and_lookup():
    """The bake shader maps v -> r = exp(v * ln 30); the scene shader maps
    r -> v = ln(r) * NOISE_V. The two hard-coded GLSL constants must stay
    exact reciprocals or the disk pattern silently stretches."""
    import re
    bake = re.search(r"exp\(v_uv\.y \* ([0-9.]+)\)", shaders.FRAG_BAKEDISK)
    look = re.search(r"#define NOISE_V ([0-9.]+)", shaders.FRAG_SCENE)
    assert bake and look, "texture domain constants not found in GLSL"
    k_bake, k_look = float(bake.group(1)), float(look.group(1))
    assert abs(k_bake - math.log(30.0)) < 1e-5
    assert abs(k_bake * k_look - 1.0) < 1e-5


def test_disk_noise_bake_fills_four_channels():
    """The volumetric disk needs all four baked fields (streaks, filaments,
    clumps, alt layer), not just .x like the old thin disk."""
    assert "vec4 diskNoise" in shaders.GLSL_COMMON
    assert "fragColor = diskNoise(r, phi);" in shaders.FRAG_BAKEDISK
    for ch in (".x", ".y", ".z", ".w"):
        assert ("nA" + ch in shaders.FRAG_SCENE
                or "nB" + ch in shaders.FRAG_SCENE), f"channel {ch} unused"


def test_volumetric_disk_is_threaded_through_both_paths():
    """The two primary disk images must use the volumetric slab (with chord
    direction) in the live path, the transport cache and the cache shading."""
    assert "diskEmissionVol" in shaders.FRAG_SCENE
    assert "diskEmissionThin" in shaders.FRAG_SCENE
    assert "float diskH(" in shaders.FRAG_SCENE
    # live path: first two crossings rich, later ones thin
    assert "slot < 2" in shaders.FRAG_SCENE
    # transport stores the chord (dz, dr) for the two rich hits
    assert "vec4(hp, cd.z, drr)" in shaders.FRAG_TRANSPORT
    # cache shading unpacks and shades the same emission model
    assert shaders.FRAG_SHADE_CACHE.count("diskEmissionVol(") >= 2
    assert shaders.FRAG_SHADE_CACHE.count("diskEmissionThin(") >= 3
    # explicit-LOD sampling: implicit derivatives are undefined in the
    # divergent crossing branches once the noise texture has mip levels
    assert "textureLod(u_noise" in shaders.FRAG_SCENE
    assert "texture(u_noise" not in shaders.FRAG_SCENE


def test_noise_texture_has_mip_chain():
    """Minification of the baked noise without mips is what caused the moire
    ('pixely') ringing on the disk."""
    import inspect
    try:
        import vkrenderer
    except (OSError, ImportError):
        _skip("Vulkan runtime not loadable (run via blackhole.py which re-execs)")
        return
    assert vkrenderer.Engine.NOISE_MIPS >= 8
    src = inspect.getsource(vkrenderer.Engine)
    assert "mips=self.NOISE_MIPS" in src
    assert "downsample.frag" in shaders.SHADERS
    # the disk sampler must interpolate between mip levels
    init_src = inspect.getsource(vkrenderer.Engine.__init__)
    wrap = init_src.split("sampler_wrap")[1].split("), None)")[0]
    assert "VK_SAMPLER_MIPMAP_MODE_LINEAR" in wrap
    assert "maxLod" in wrap


def test_ultra_uses_dynamic_resolution_while_moving():
    """Ultra must drop render scale while the camera moves and snap back to
    1.0x for the one-time transport-cache rebuild when it settles."""
    import inspect
    src = inspect.getsource(bh.run_interactive)
    assert "ultra_key" in src
    assert "_apply_scale(1.0)" in src
    # the cached-frame scale climb is for presets 1-3 only
    assert 'if state["quality"] >= 4:\n                return' in src


def test_blur_shader_uses_linear_sampled_five_tap_kernel():
    import re

    blur = shaders.FRAG_BLUR
    calls = re.findall(r"texture\(u_a", blur)
    assert len(calls) == 5
    weights = [
        float(re.search(r"w0 = ([0-9.]+)", blur).group(1)),
        float(re.search(r"w12 = ([0-9.]+)", blur).group(1)),
        float(re.search(r"w34 = ([0-9.]+)", blur).group(1)),
    ]
    assert abs(weights[0] + 2.0 * weights[1] + 2.0 * weights[2] - 1.0) < 1e-5
    assert "1.3846154" in blur and "3.2307692" in blur


def test_photon_ring_later_crossings_are_stabilized():
    assert "int diskHits = 0;" in shaders.FRAG_SCENE
    assert "if (diskHits >= 2)" in shaders.FRAG_SCENE
    assert "ringFade" in shaders.FRAG_SCENE


def test_shadow_guard_exists_in_shader():
    assert "rMin" in shaders.FRAG_SCENE
    assert "pc.kerr.w" in shaders.FRAG_SCENE
    assert "reject numerically leaked sky" in shaders.FRAG_SCENE


def test_transport_cache_path_is_threaded():
    import inspect
    try:
        import vkrenderer
    except (OSError, ImportError):
        _skip("Vulkan runtime not loadable (run via blackhole.py which re-execs)")
        return

    assert "transport.frag" in shaders.SHADERS
    assert "shadecache.frag" in shaders.SHADERS
    assert "record_cached" in inspect.getsource(vkrenderer.Engine)
    interactive_src = inspect.getsource(bh.run_interactive)
    assert 'state["quality"] >= 4' in interactive_src
    assert 'engine.record_cached' in interactive_src
    # The idle orbit resumes shortly after input (no multi-second click freeze)
    # but is disabled at ultra: a drifting camera never settles, so it would
    # defeat the transport cache (ultra holds still -> crisp cached reshade;
    # the disc plasma churn provides the motion instead).
    assert 'state["quality"] < 4 and not state["drag"]' in interactive_src
    assert 'state["yaw"] += dt * 0.05' in interactive_src


def test_render_still_end_to_end(tmp_path=None):
    """Full pipeline smoke test through the startup bake (sky cubemap + disk
    noise texture), geodesic pass, bloom and composite: the image must show
    a bright disk, a dark shadow, and no NaN/garbage tiles."""
    try:
        import vulkan  # noqa: F401
    except (OSError, ImportError):
        _skip("Vulkan runtime not loadable (run via blackhole.py which re-execs)")
        return
    import pathlib
    import tempfile
    from PIL import Image

    def load(path, **kw):
        bh.render_still(str(path), (320, 180), quiet=True, **kw)
        return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)

    tmp = pathlib.Path(tempfile.mkdtemp())
    # bloom off: crisp image for the structural assertions
    img = load(tmp / "smoke.png", bloom=False)
    assert img.shape == (180, 320, 3)
    assert img.max() > 180, "no bright disk rendered"
    assert 2.0 < img.mean() < 80.0, f"implausible exposure: mean={img.mean():.1f}"
    shadow = img[70:86, 164:180].mean()           # inside the shadow
    assert shadow < 20.0, f"shadow is not dark: {shadow:.1f}"
    # NaN tile garbage renders as saturated uniform blocks; the scene must
    # not contain fully saturated green anywhere
    green = (img[..., 1] > 250) & (img[..., 0] < 30) & (img[..., 2] < 30)
    assert not green.any(), "saturated green pixels (NaN tile signature)"
    # bloom on (the default path): must brighten, not corrupt
    img_b = load(tmp / "smoke_bloom.png")
    assert img_b.mean() > img.mean(), "bloom did not add light"
    assert 2.0 < img_b.mean() < 90.0


def test_cached_transport_matches_direct_render():
    """The repacked transport cache (rich hits with chord direction + thin
    hits) must reproduce the direct geodesic pass almost exactly."""
    try:
        import vulkan as vk
        import vkrenderer as vkr
    except (OSError, ImportError):
        _skip("Vulkan runtime not loadable (run via blackhole.py which re-execs)")
        return

    size = (480, 270)
    ctx = vkr.VulkanContext(window=None)
    engine = vkr.Engine(ctx, shaders.compile_all(), size, vkr.LDR_FORMAT,
                        vk.VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    scene_pc = bh.scene_push_constants(0.55, 0.145, 33.0, 38.0, size, 720)
    comp_pc = bh.composite_push_constants(38.0)

    def render(record_fn):
        out = ctx.create_image(size[0], size[1], vkr.LDR_FORMAT,
                               vk.VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                               | vk.VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        fb = vk.vkCreateFramebuffer(ctx.device, vk.VkFramebufferCreateInfo(
            renderPass=engine.rp_out, attachmentCount=1,
            pAttachments=[out.view],
            width=size[0], height=size[1], layers=1), None)
        nbytes = size[0] * size[1] * 4
        buf, mem = ctx.create_buffer(
            nbytes, vk.VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            vk.VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | vk.VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        cmd = ctx.alloc_cmd()
        vk.vkBeginCommandBuffer(cmd, vk.VkCommandBufferBeginInfo(
            flags=vk.VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
        record_fn(cmd, fb)
        region = vk.VkBufferImageCopy(
            bufferOffset=0, bufferRowLength=0, bufferImageHeight=0,
            imageSubresource=vk.VkImageSubresourceLayers(
                aspectMask=vk.VK_IMAGE_ASPECT_COLOR_BIT,
                mipLevel=0, baseArrayLayer=0, layerCount=1),
            imageOffset=vk.VkOffset3D(x=0, y=0, z=0),
            imageExtent=vk.VkExtent3D(width=size[0], height=size[1], depth=1))
        vk.vkCmdCopyImageToBuffer(cmd, out.handle,
                                  vk.VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  buf, 1, [region])
        vk.vkEndCommandBuffer(cmd)
        ctx.submit_and_wait(cmd)
        mapped = vk.vkMapMemory(ctx.device, mem, 0, nbytes, 0)
        data = bytes(mapped)[:nbytes]
        vk.vkUnmapMemory(ctx.device, mem)
        a = np.frombuffer(data, np.uint8).reshape(size[1], size[0], 4)[..., :3]
        return a.astype(np.float64)

    direct = render(lambda cmd, fb: engine.record(
        cmd, fb, size, scene_pc, comp_pc))
    cached = render(lambda cmd, fb: engine.record_cached(
        cmd, fb, size, scene_pc, comp_pc, rebuild_cache=True))
    ctx.wait_idle()
    diff = np.abs(direct - cached)
    assert diff.mean() < 2.0, f"cached path diverges: mean {diff.mean():.2f}/255"


def test_vulkan_device_available():
    """Smoke test: create a headless MoltenVK context. Skipped when the
    Vulkan runtime is not loadable in this process."""
    try:
        import vulkan  # noqa: F401
        import vkrenderer
    except (OSError, ImportError):
        _skip("Vulkan runtime not loadable (run via blackhole.py which re-execs)")
        return
    ctx = vkrenderer.VulkanContext(window=None)
    assert ctx.device_name
    # the pipeline layouts must agree with what the app packs
    assert vkrenderer.SCENE_PC_SIZE == len(
        bh.scene_push_constants(0.5, 0.1, 27.0, 20.0, (1280, 720), 256))
    assert vkrenderer.POST_PC_SIZE == len(bh.composite_push_constants(20.0))
    ctx.wait_idle()


def _skip(reason):
    try:
        import pytest
        pytest.skip(reason)
    except ImportError:
        print(f"    skipped: {reason}")


if __name__ == "__main__":
    import sys
    import traceback

    bh.ensure_vulkan_loadable()
    failures = 0
    tests = [(n, f) for n, f in sorted(globals().items())
             if n.startswith("test_") and callable(f)]
    for name, fn in tests:
        try:
            fn()
            print(f"PASS  {name}")
        except Exception:
            failures += 1
            print(f"FAIL  {name}")
            traceback.print_exc()
    print(f"\n{len(tests) - failures}/{len(tests)} passed")
    sys.exit(1 if failures else 0)
