// Metal shaders for the Kerr black hole app.
//
// The host compiles this file at run time. It prepends <metal_stdlib>, then the shared structs,
// kerr_core.h and shading.h, so the photon tracer in the kernel is the same code the CPU renderer
// and the tests run. Compiled without fast-math so the float results stay close to the double ones.

// ---- Ray tracing -------------------------------------------------------------------------------

Observer<float> observer_from_uniforms(const TraceUniforms& U) {
    Observer<float> o;
    o.pos = v3(U.cam_pos[0], U.cam_pos[1], U.cam_pos[2]);
    o.ut = U.cam_ut;
    for (int mu = 0; mu < 4; ++mu) {
        o.u[mu] = U.cam_u[mu];
    }
    for (int k = 0; k < 3; ++k) {
        for (int mu = 0; mu < 4; ++mu) {
            o.e[k][mu] = U.cam_e[4 * k + mu];
        }
    }
    return o;
}

kernel void trace_kernel(texture2d<float, access::write> out [[texture(0)]],
                         constant TraceUniforms& uni [[buffer(0)]],
                         device const float* lut [[buffer(1)]],
                         uint2 gid [[thread_position_in_grid]]) {
    TraceUniforms U = uni;
    if (gid.x >= U.width || gid.y >= U.height) {
        return;
    }
    float halfH = 0.5f * float(U.height);
    float sx = (float(gid.x) + 0.5f - 0.5f * float(U.width)) / halfH * U.tan_half_fov;
    float sy = (halfH - (float(gid.y) + 0.5f)) / halfH * U.tan_half_fov;

    TraceParams<float> tp;
    tp.a = U.a;
    tp.r_plus = U.r_plus;
    tp.r_capture = U.r_capture;
    tp.r_isco = U.r_isco;
    tp.r_out = U.r_out;
    tp.eta = U.eta;
    tp.r_escape = U.r_escape;
    tp.cam_ut = U.cam_ut;
    tp.max_steps = U.max_steps;

    Observer<float> obs = observer_from_uniforms(U);
    Phase<float> ray = pixel_ray(obs, sx, sy);
    Hit<float> hit = trace_photon(tp, ray);
    V3<float> c = shade_hit(U, lut, hit);
    out.write(float4(c.x, c.y, c.z, 1.0f), gid);
}

// ---- Tone mapping blit -------------------------------------------------------------------------

struct BlitOut {
    float4 position [[position]];
    float2 uv;
};

vertex BlitOut blit_vertex(uint vid [[vertex_id]]) {
    // One oversized triangle covering the viewport.
    float2 p = float2(vid == 1 ? 3.0f : -1.0f, vid == 2 ? 3.0f : -1.0f);
    BlitOut o;
    o.position = float4(p, 0.0f, 1.0f);
    o.uv = float2(0.5f * (p.x + 1.0f), 0.5f * (1.0f - p.y));
    return o;
}

fragment float4 blit_fragment(BlitOut in [[stage_in]],
                              texture2d<float> src [[texture(0)]],
                              constant float& exposure [[buffer(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    float3 hdr = src.sample(s, in.uv).rgb;
    float3 c = 1.0f - exp(-exposure * hdr);
    c = pow(saturate(c), float3(1.0f / 2.2f));
    return float4(c, 1.0f);
}

// ---- Rubber-sheet ("spacetime trapdoor") pass ---------------------------------------------------

struct FunnelOut {
    float4 position [[position]];
    float r;
    float phi;
    float y;
};

vertex FunnelOut funnel_vertex(uint vid [[vertex_id]],
                               constant FunnelVertex* verts [[buffer(0)]],
                               constant FunnelUniforms& U [[buffer(1)]]) {
    FunnelVertex v = verts[vid];
    float4x4 M = float4x4(float4(U.mvp[0], U.mvp[1], U.mvp[2], U.mvp[3]),
                          float4(U.mvp[4], U.mvp[5], U.mvp[6], U.mvp[7]),
                          float4(U.mvp[8], U.mvp[9], U.mvp[10], U.mvp[11]),
                          float4(U.mvp[12], U.mvp[13], U.mvp[14], U.mvp[15]));
    FunnelOut o;
    o.position = M * float4(v.pos[0], v.pos[1], v.pos[2], 1.0f);
    o.r = v.par[0];
    o.phi = v.par[1];
    o.y = v.pos[1];
    return o;
}

// Distance (in r or phi units) to the nearest line of a regular family, and its anti-aliased coverage.
inline float line_cover(float x, float spacing, float width) {
    float d = fabs(x / spacing - floor(x / spacing + 0.5f)) * spacing;
    return 1.0f - smoothstep(0.0f, width, d);
}

fragment float4 funnel_fragment(FunnelOut in [[stage_in]],
                                constant FunnelUniforms& U [[buffer(1)]]) {
    float wr = max(fwidth(in.r) * 1.5f, 1e-4f);
    float wp = max(fwidth(in.phi) * 1.5f, 1e-4f);
    float rings = line_cover(in.r, 1.0f, wr);
    float spokes = line_cover(in.phi, 6.2831853f / 24.0f, wp);

    // Depth tint: the rim is pale, the throat deep blue-violet.
    float depth = clamp((in.y - U.y_top) / (U.y_bottom - U.y_top), 0.0f, 1.0f);
    float3 base = mix(float3(0.10f, 0.13f, 0.22f), float3(0.02f, 0.02f, 0.05f), depth);

    // Disk region (r >= r_isco) carries a faint warm tint: the physical disk lives on this sheet.
    float inDisk = smoothstep(U.r_isco - 0.05f, U.r_isco + 0.05f, in.r);
    base = mix(base, float3(0.16f, 0.09f, 0.04f), 0.5f * inDisk);

    float3 cLine = float3(0.35f, 0.80f, 1.00f);
    float3 col = base + (0.75f * rings + 0.35f * spokes) * cLine;

    // Special rings: horizon, photon orbit, ISCO.
    float wh = max(fwidth(in.r) * 2.0f, 2e-3f);
    float horizon = 1.0f - smoothstep(0.0f, wh + 0.02f, fabs(in.r - U.r_plus));
    float photon = 1.0f - smoothstep(0.0f, wh + 0.02f, fabs(in.r - U.r_ph));
    float isco = 1.0f - smoothstep(0.0f, wh + 0.02f, fabs(in.r - U.r_isco));
    col = mix(col, float3(1.0f, 0.25f, 0.20f), horizon);
    col = mix(col, float3(1.0f, 0.95f, 0.45f), photon);
    col = mix(col, float3(1.0f, 0.60f, 0.15f), isco);
    return float4(col, 1.0f);
}
