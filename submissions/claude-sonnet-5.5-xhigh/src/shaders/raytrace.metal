// raytrace.metal -- real-time Kerr ray tracer.  One thread = one camera ray, traced backward through the
// Kerr geometry with bh::trace_ray (the very same code the CPU test-suite validates in double precision).

// ---------------------------------------------------------------------------------------------
// Background sky: procedural stars (blackbody coloured, gravitationally blue-shifted) + Milky Way
// ---------------------------------------------------------------------------------------------
inline float3 star_layer(float3 d, float cells, float density, float sigma, uint seed, float gshift,
                         device const float4* bb, float gain)
{
    float3 ad = abs(d);
    int face;
    float2 uv;
    if (ad.x >= ad.y && ad.x >= ad.z) { face = d.x > 0.0f ? 0 : 1; uv = float2(d.y, d.z) / ad.x; }
    else if (ad.y >= ad.z)            { face = d.y > 0.0f ? 2 : 3; uv = float2(d.x, d.z) / ad.y; }
    else                               { face = d.z > 0.0f ? 4 : 5; uv = float2(d.x, d.y) / ad.z; }
    uv = atan(uv) * (4.0f / PI_F);                       // equal-angle cell grid, uv in [-1, 1]
    float2 q = (uv * 0.5f + 0.5f) * cells;
    int2 c0 = int2(floor(q));
    const float cell_rad = (PI_F * 0.5f) / cells;        // angular size of a cell
    float3 acc = float3(0.0f);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int2 c = c0 + int2(dx, dy);
            uint h = hash3u(uint3(uint(c.x + 4096), uint(c.y + 4096), uint(face) * 131u + seed));
            if (u01(h) > density) continue;
            float2 sp = float2(u01(pcg(h ^ 0x1234567u)), u01(pcg(h ^ 0x9876543u)));
            float2 dq = q - (float2(c) + sp);
            float dist = length(dq) * cell_rad;
            if (dist > 4.0f * sigma) continue;
            float m = u01(pcg(h ^ 0xabcdef1u));
            float amp = gain * (0.03f + 5.0f * pow(m, 10.0f));
            float temp = mix(2600.0f, 26000.0f, pow(u01(pcg(h ^ 0x5555555u)), 1.7f)) * gshift;
            float prof = exp(-0.5f * dist * dist / (sigma * sigma));
            acc += bb_chroma(bb, temp) * amp * prof;
        }
    }
    return acc;
}

inline float3 sky_color(float3 d, float gshift, constant RTUniforms& U, device const float4* bb)
{
    const float sigma = U.sky.y;
    float3 col = float3(0.0f);
    // Milky Way: a tilted band with a bright bulge, dust lanes and colour variation
    if (U.sky.z > 0.0f) {
        const float3 nG = normalize(float3(0.34f, -0.46f, 0.82f));
        const float3 e1 = normalize(cross(nG, float3(0.0f, 0.0f, 1.0f)));
        const float3 e2 = cross(nG, e1);
        float sb = dot(d, nG);
        float lon = atan2(dot(d, e2), dot(d, e1));
        float band = exp(-sb * sb / (2.0f * 0.20f * 0.20f));
        float core = exp(-lon * lon / 0.9f) * exp(-sb * sb / (2.0f * 0.35f * 0.35f));
        float n1 = fbm3(d * 3.1f + 5.0f, 5);
        float n2 = fbm3(d * 9.0f + 1.0f, 4);
        float dust = smoothstep(0.42f, 0.70f, fbm3(d * 5.3f + float3(2.0f, 8.0f, 4.0f), 5));
        float lum = band * (0.35f + 0.9f * n1) * (1.0f - 0.75f * dust * band) + 1.6f * core * (0.6f + 0.6f * n2);
        float3 tint = mix(float3(0.50f, 0.62f, 1.0f), float3(1.0f, 0.74f, 0.48f), clamp(core * 2.0f + n1 - 0.3f, 0.0f, 1.0f));
        col += tint * lum * 0.016f * U.sky.z;
        // unresolved faint stars riding the band
        col += float3(0.9f, 0.85f, 0.8f) * band * 0.0025f * U.sky.z * smoothstep(0.3f, 0.9f, n2);
    }
    if (U.disk.w > 0.5f) {
        col += star_layer(d, 26.0f, 0.55f, sigma * 1.35f, 11u, gshift, bb, 1.0f);
        col += star_layer(d, 64.0f, 0.50f, sigma * 1.05f, 23u, gshift, bb, 0.55f);
        col += star_layer(d, 150.0f, 0.42f, sigma, 37u, gshift, bb, 0.30f);
        col += star_layer(d, 360.0f, 0.30f, sigma, 41u, gshift, bb, 0.22f);
    }
    // gravitational blue shift of the whole sky as seen from deep in the well (brightness ~ g^4)
    float g4 = gshift * gshift * gshift * gshift;
    col *= g4;
    // lensed celestial coordinate grid (lines every 15 degrees) -- shows the mapping of the sky
    if (U.opt.x > 0.5f) {
        float th = acos(clamp(d.z, -1.0f, 1.0f));
        float ph = atan2(d.y, d.x);
        const float gstep = PI_F / 12.0f;
        float lt = abs(fract(th / gstep + 0.5f) - 0.5f) * gstep;
        float lp = abs(fract(ph / gstep + 0.5f) - 0.5f) * gstep * max(sin(th), 0.05f);
        float w = 0.0022f;
        float line = max(exp(-lt * lt / (w * w)), exp(-lp * lp / (w * w)));
        col += line * float3(0.05f, 0.30f, 0.60f) * 0.9f;
        // celestial equator accent
        float eq = exp(-(d.z * d.z) / (w * w * 1.5f));
        col += eq * float3(0.9f, 0.5f, 0.15f) * 0.9f;
    }
    return col;
}

// ---------------------------------------------------------------------------------------------
// The accretion disk surface
// ---------------------------------------------------------------------------------------------
inline float3 disk_emission(const thread bh::TraceOut<float>& tr, float nu_cam, constant RTUniforms& U,
                            device const float* diskTab, device const float4* bb, thread float& g_out)
{
    const float a = U.bh.x;
    const float r = tr.r_hit;
    const float ut = bh::ut_kepler(r, a);
    const float Om = bh::omega_kepler(r, a);
    const float nu_em = ut * (1.0f - Om * tr.L);                 // photon energy in the gas frame (E_inf = 1)
    const float g = nu_cam / nu_em;                               // total frequency ratio (Doppler x gravitational)
    g_out = g;
    const float cosE = clamp(abs(tr.vmu_hit) / (r * nu_em), 0.0f, 1.0f);   // emission angle to the disk normal
    float limb = (1.0f + 2.06f * cosE) / (1.0f + 2.06f * 0.55f);            // electron-scattering atmosphere
    if (tr.surf >= 2) limb = 1.0f;                                          // rim / inner wall: face-on to the emitter's radial direction
    const float t_em = U.view.z + tr.t_hit;                       // retarded time of emission
    float tex = 1.0f;
    if (U.misc.x > 0.0f) tex = mix(1.0f, disk_pattern(r, tr.phi_hit, t_em, a), U.misc.x);
    const float Tloc = U.disk.x * disk_temp_ratio(diskTab, r, U.bh.z, U.bh.w) * pow(max(tex, 0.02f), 0.25f);
    // outer edge fade (the disk simply thins out)
    const float fade = mix(0.12f, 1.0f, smoothstep(U.bh.w, U.bh.w - 3.0f, r));
    const float Tobs = g * Tloc;
    float3 rad = bb_radiance(bb, Tobs) * (limb * fade * U.misc.y);
    return rad;
}

// ---------------------------------------------------------------------------------------------
// Ray-trace kernel
// ---------------------------------------------------------------------------------------------
kernel void raytrace(constant RTUniforms& U [[buffer(0)]],
                     device const float* diskTab [[buffer(1)]],
                     device const float4* bb [[buffer(2)]],
                     texture2d<float, access::write> outTex [[texture(0)]],
                     uint2 gid [[thread_position_in_grid]])
{
    const uint W = uint(U.view.x), H = uint(U.view.y);
    if (gid.x >= W || gid.y >= H) return;

    const float a = U.bh.x;
    float2 ndc = ((float2(gid) + 0.5f + U.jit.xy) / float2(float(W), float(H))) * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    const float th = U.cam.w;
    const float aspect = float(W) / float(H);
    float3 n = normalize(float3(ndc.x * aspect * th, ndc.y * th, 1.0f));   // (right, up, forward)

    // camera basis: forward = -e_r (toward the hole), up = -e_theta, right = +e_phi
    bh::Ray<float> ray;
    float nu_cam = 1.0f;
    float3 col = float3(0.0f);
    float moving = 1.0f;   // alpha channel: 1 = static content, 0 = animated (disk) -> temporal filter treats them differently
    bool ok = bh::init_ray_zamo<float>(a, U.cam.x, U.cam.y, U.cam.z, -n.z, -n.y, n.x, ray, nu_cam);

    if (ok) {
        bh::TraceCfg<float> cfg;
        cfg.a = a;
        cfg.r_plus = U.bh.y;
        cfg.r_isco = U.bh.z;
        cfg.r_out = U.bh.w;
        cfg.r_far = U.sky.w;
        cfg.c = U.opt.z;
        cfg.max_steps = int(U.opt.w);
        cfg.disk = (U.disk.y > 0.5f) ? 1 : 0;
        cfg.disk_h = U.misc.w;
        cfg.halo = U.disk.z;
        cfg.nu_cam = nu_cam;
        bh::TraceOut<float> tr;
        bh::trace_ray<float>(cfg, ray, tr);

        const int dbg = int(U.opt.y + 0.5f);
        if (dbg == 3) {
            float s = float(tr.steps) / 96.0f;
            col = float3(clamp(s * 2.0f, 0.0f, 1.0f), clamp(s * 2.0f - 0.5f, 0.0f, 1.0f), clamp(s - 0.5f, 0.0f, 1.0f));
        } else if (tr.status == 2) {
            float g;
            moving = 0.0f;
            col = disk_emission(tr, nu_cam, U, diskTab, bb, g);
            if (dbg == 1) {   // image order: which pass of the equatorial plane
                const float3 pal[4] = {float3(1.0f, 0.9f, 0.7f), float3(0.2f, 0.8f, 1.0f), float3(1.0f, 0.3f, 0.8f), float3(0.4f, 1.0f, 0.4f)};
                col = pal[min(tr.crossings, 3)] * (0.25f + 0.75f * clamp(dot(col, float3(0.3f, 0.5f, 0.2f)), 0.0f, 1.0f));
            } else if (dbg == 2) {   // redshift map: blue = blueshift (g > 1), red = redshift (g < 1), grey = no shift
                float lg = clamp(log2(g) * 0.9f, -1.0f, 1.0f);
                float t = pow(abs(lg), 0.6f);
                float3 target = lg > 0.0f ? float3(0.05f, 0.45f, 1.0f) : float3(1.0f, 0.10f, 0.02f);
                col = mix(float3(0.35f), target, t) * (0.55f + 0.45f * clamp(g, 0.0f, 1.5f) / 1.5f);
            }
        } else if (tr.status == 1) {
            float3 d = float3(tr.dx, tr.dy, tr.dz);
            col = sky_color(d, nu_cam, U, bb) * U.sky.x;
            if (dbg == 1) col = float3(0.03f);
        } else if (tr.status == 0) {
            col = float3(0.0f);
        } else {
            col = float3(0.0f);   // step limit: rays trapped near the photon sphere -> dark (they converge to the critical curve)
        }
        // optically thin hot corona ("halo"): added in front of whatever the ray finally reached
        if (dbg == 0 && cfg.halo > 0.0f && tr.halo_e > 0.0f)
            col += bb_radiance(bb, tr.halo_g * U.disk.x * 1.35f) * (tr.halo_e * U.misc.y);
    }
    outTex.write(float4(col, moving), gid);
}

// ---------------------------------------------------------------------------------------------
// Validation probe: trace explicit image-plane coordinates and dump the raw geodesic result.
// Output per ray: status, steps, crossings, r_hit, phi_hit, t_hit, L, nu_cam, dx, dy, dz, g
// ---------------------------------------------------------------------------------------------
kernel void probe(constant RTUniforms& U [[buffer(0)]],
                  device const float2* ndcIn [[buffer(1)]],
                  device float* outv [[buffer(2)]],
                  uint gid [[thread_position_in_grid]])
{
    const float a = U.bh.x;
    const float2 ndc = ndcIn[gid];
    const float th = U.cam.w;
    const float aspect = U.view.x / U.view.y;
    float3 n = normalize(float3(ndc.x * aspect * th, ndc.y * th, 1.0f));
    bh::Ray<float> ray;
    float nu_cam = 1.0f;
    device float* o = outv + gid * 12;
    for (int i = 0; i < 12; ++i) o[i] = 0.0f;
    if (!bh::init_ray_zamo<float>(a, U.cam.x, U.cam.y, U.cam.z, -n.z, -n.y, n.x, ray, nu_cam)) {
        o[0] = -1.0f;
        return;
    }
    bh::TraceCfg<float> cfg;
    cfg.a = a;
    cfg.r_plus = U.bh.y;
    cfg.r_isco = U.bh.z;
    cfg.r_out = U.bh.w;
    cfg.r_far = U.sky.w;
    cfg.c = U.opt.z;
    cfg.max_steps = int(U.opt.w);
    cfg.disk = (U.disk.y > 0.5f) ? 1 : 0;
    cfg.disk_h = U.misc.w;
    cfg.halo = U.disk.z;
    cfg.nu_cam = nu_cam;
    bh::TraceOut<float> tr;
    bh::trace_ray<float>(cfg, ray, tr);
    o[0] = float(tr.status);
    o[1] = float(tr.steps);
    o[2] = float(tr.crossings);
    o[3] = tr.r_hit;
    o[4] = tr.phi_hit;
    o[5] = tr.t_hit;
    o[6] = tr.L;
    o[7] = nu_cam;
    o[8] = tr.dx;
    o[9] = tr.dy;
    o[10] = tr.dz;
    o[11] = (tr.status == 2) ? bh::disk_redshift<float>(tr.r_hit, a, tr.L, nu_cam) : 0.0f;
}
