// funnel.metal -- the "trapdoor in spacetime": embedding diagram of the equatorial plane of the Kerr geometry.
//
// The surface is  (rho(r) cos phi, rho(r) sin phi, z(r))  with rho^2 = r^2 + a^2 + 2 a^2/r  and
// dz/dr = sqrt(r^2/Delta - rho'^2): distances measured ALONG the sheet are the true proper distances of the
// t = const equatorial slice.  A flat sheet would be an empty universe; the funnel is what mass does to space.
// The square lattice is laid on the sheet in Boyer-Lindquist (x, y) = (r cos phi, r sin phi) -- in flat space
// its cells would all be equal squares.

struct FOut {
    float4 pos [[position]];
    float4 attr;
    float3 world;
};

vertex FOut funnel_vs(uint vid [[vertex_id]],
                      device const FunnelVertex* v [[buffer(0)]],
                      constant FunnelUniforms& U [[buffer(1)]])
{
    FunnelVertex fv = v[vid];
    FOut o;
    o.pos = U.viewProj * float4(fv.pos.xyz, 1.0f);
    o.attr = fv.attr;
    o.world = fv.pos.xyz;
    return o;
}

inline float ring_line(float r, float rk, float wr)
{
    float d = abs(r - rk) / max(wr, 1e-4f);
    return (1.0f - clamp(d - 0.7f, 0.0f, 1.0f)) + exp(-d * 0.22f) * 0.30f;
}

fragment float4 funnel_fs(FOut in [[stage_in]],
                          constant FunnelUniforms& U [[buffer(1)]],
                          device const float* diskTab [[buffer(2)]],
                          device const float4* bb [[buffer(3)]])
{
    const float a = U.bh.x, rp = U.bh.y, risco = U.bh.z, rout = U.bh.w;
    const float r = in.attr.z;
    const bool tube = in.attr.w >= 0.0f;

    float3 dpx = dfdx(in.world), dpy = dfdy(in.world);
    float3 N = normalize(cross(dpx, dpy) + float3(1e-9f));
    float3 V = normalize(U.camPos.xyz - in.world);
    float ndv = abs(dot(N, V));
    float fres = pow(1.0f - ndv, 3.0f);

    // glass body of the sheet
    float glassA = 0.50f + 0.30f * fres;
    float3 glass = float3(0.012f, 0.026f, 0.06f) + float3(0.04f, 0.16f, 0.32f) * fres * 0.9f;
    float3 emit = float3(0.0f);
    float cover = glassA;

    if (!tube) {
        // depth colouring: shallow = cool cyan, deep = hot orange
        float depth = clamp(-in.world.z / max(U.opt.z, 1e-3f), 0.0f, 1.0f);
        float3 lc = mix(float3(0.16f, 0.62f, 1.0f), float3(1.0f, 0.34f, 0.10f), pow(depth, 1.6f));

        float2 xy = in.attr.xy;
        float2 fw = max(fwidth(xy), float2(1e-5f));
        float S = U.opt.x;
        float2 d1 = abs(fract(xy / S + 0.5f) - 0.5f) * S / fw;
        float m1 = min(d1.x, d1.y);
        float fade1 = 1.0f - smoothstep(0.30f, 0.85f, max(fw.x, fw.y) / S);
        float l1 = (1.0f - clamp(m1 - 0.25f, 0.0f, 1.0f)) + exp(-m1 * 0.45f) * 0.22f;
        float S2 = S * 0.25f;
        float2 d2 = abs(fract(xy / S2 + 0.5f) - 0.5f) * S2 / fw;
        float m2 = min(d2.x, d2.y);
        float fade2 = 1.0f - smoothstep(0.30f, 0.85f, max(fw.x, fw.y) / S2);
        float l2 = (1.0f - clamp(m2 - 0.25f, 0.0f, 1.0f));
        emit += lc * (l1 * fade1 * 1.15f + l2 * fade2 * 0.28f);

        // the accretion disk lives on this very surface
        if (U.opt.w > 0.5f && r >= risco && r <= rout) {
            float ratio = disk_temp_ratio(diskTab, r, risco, rout);
            float phi = atan2(in.attr.y, in.attr.x);
            float tex = disk_pattern(r, phi, U.time.x, a);
            float T = U.time.y * ratio * pow(max(tex, 0.05f), 0.25f);
            float3 em = bb_radiance(bb, T) * U.camRight.w * 0.30f * smoothstep(rout, rout - 2.5f, r);
            emit += em;
            cover = max(cover, 0.85f);
        }

        // landmark radii
        const float wr = fwidth(r);
        emit += float3(1.00f, 0.22f, 0.06f) * ring_line(r, rp, wr) * 1.8f;                                     // event horizon
        if (a > 0.02f) emit += float3(0.70f, 0.30f, 1.00f) * ring_line(r, 2.0f, wr) * 1.2f;                    // ergosphere (equator)
        emit += float3(1.00f, 0.92f, 0.55f) * ring_line(r, U.radii.x, wr) * 1.4f;                              // photon orbit
        emit += float3(0.25f, 1.00f, 0.50f) * ring_line(r, risco, wr) * 1.4f;                                  // ISCO
        // observer
        emit += float3(1.0f, 0.75f, 0.30f) * ring_line(r, U.radii.z, wr) * 0.6f;
    } else {
        // the throat continues down forever: the trapdoor
        float d = in.attr.w;
        float2 fwxy = fwidth(in.attr.xy);
        float fwA = max(length(fwxy), 1e-4f);
        float ang = atan2(in.attr.y, in.attr.x);
        const float N24 = 24.0f;
        float da = 2.0f * PI_F / N24;
        float rho = length(in.attr.xy);
        float ap = abs(fract(ang / da + 0.5f) - 0.5f) * da * rho / fwA;
        float fwd = max(fwidth(d), 1e-4f);
        float rd = abs(fract(d + 0.5f) - 0.5f) / fwd;
        float m = min(ap, rd);
        float fadeD = exp(-d / max(U.opt.z * 0.55f, 1e-3f));
        float l = (1.0f - clamp(m - 0.25f, 0.0f, 1.0f)) + exp(-m * 0.45f) * 0.22f;
        emit += mix(float3(1.0f, 0.34f, 0.10f), float3(0.55f, 0.06f, 0.02f), clamp(d / 12.0f, 0.0f, 1.0f)) * l * 1.15f * fadeD;
        emit += float3(1.00f, 0.22f, 0.06f) * ring_line(d, 0.0f, fwd) * 1.6f;
        cover = glassA * fadeD;
        glass *= fadeD;
    }

    float3 body = glass * cover;
    float a_out = clamp(cover + dot(emit, float3(0.3f, 0.4f, 0.3f)), 0.0f, 1.0f);
    return float4(body + emit, a_out);
}

// ---------------------------------------------------------------------------------------------
// Test particles / trails: camera-facing soft discs (additive)
// ---------------------------------------------------------------------------------------------
struct POut {
    float4 pos [[position]];
    float2 uv;
    float4 color;
};

vertex POut particle_vs(uint vid [[vertex_id]],
                        uint iid [[instance_id]],
                        device const ParticleVertex* p [[buffer(0)]],
                        constant FunnelUniforms& U [[buffer(1)]])
{
    const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
    ParticleVertex pv = p[iid];
    float2 c = corners[vid];
    float3 world = pv.pos.xyz + (U.camRight.xyz * c.x + U.camUp.xyz * c.y) * pv.pos.w;
    POut o;
    o.pos = U.viewProj * float4(world, 1.0f);
    o.uv = c;
    o.color = pv.color;
    return o;
}

fragment float4 particle_fs(POut in [[stage_in]])
{
    float d2 = dot(in.uv, in.uv);
    if (d2 > 1.0f) discard_fragment();
    float core = exp(-d2 * 7.0f);
    float halo = exp(-d2 * 2.2f) * 0.30f;
    float k = (core + halo) * in.color.a;
    return float4(in.color.rgb * k, min(k, 1.0f));
}
