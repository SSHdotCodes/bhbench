// post.metal -- temporal accumulation, bloom, tone mapping and compositing.

// ---------------------------------------------------------------------------------------------
// Temporal accumulation.  Static content (sky, shadow) accumulates sub-pixel jitter for anti-aliasing;
// moving content (the rotating disk) blends faster and is clamped to its neighbourhood to avoid ghosts.
// The ray tracer writes alpha = 1 for static pixels, 0 for disk pixels.
// ---------------------------------------------------------------------------------------------
kernel void taa_resolve(constant PostUniforms& P [[buffer(0)]],
                        texture2d<float, access::read> cur [[texture(0)]],
                        texture2d<float, access::read> hist [[texture(1)]],
                        texture2d<float, access::write> outT [[texture(2)]],
                        uint2 gid [[thread_position_in_grid]])
{
    const int W = int(P.b.x), H = int(P.b.y);
    if (int(gid.x) >= W || int(gid.y) >= H) return;
    float4 c4 = cur.read(gid);
    float3 c = c4.rgb;
    const float a_static = P.a.x, a_moving = P.a.y;
    if (a_static >= 0.999f) {
        outT.write(float4(c, c4.a), gid);
        return;
    }
    float3 mn = c, mx = c;
    float moving = 1.0f - c4.a;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int2 q = int2(clamp(int(gid.x) + dx, 0, W - 1), clamp(int(gid.y) + dy, 0, H - 1));
            float4 s = cur.read(uint2(q));
            mn = min(mn, s.rgb);
            mx = max(mx, s.rgb);
            moving = max(moving, 1.0f - s.a);
        }
    }
    float3 h = hist.read(gid).rgb;
    float alpha = a_static;
    if (moving > 0.5f) {
        float3 pad = (mx - mn) * 0.15f + 0.002f;
        h = clamp(h, mn - pad, mx + pad);
        alpha = a_moving;
    }
    outT.write(float4(mix(h, c, alpha), c4.a), gid);
}

// ---------------------------------------------------------------------------------------------
// Bloom: 13-tap downsample chain + tent upsample chain
// ---------------------------------------------------------------------------------------------
constant sampler linClamp(coord::normalized, address::clamp_to_edge, filter::linear);

inline float karis(float3 c) { return 1.0f / (1.0f + dot(c, float3(0.2126f, 0.7152f, 0.0722f))); }

kernel void bloom_down(texture2d<float> src [[texture(0)]],
                       texture2d<float, access::write> dst [[texture(1)]],
                       constant float4& prm [[buffer(0)]],
                       uint2 gid [[thread_position_in_grid]])
{
    const uint w = dst.get_width(), h = dst.get_height();
    if (gid.x >= w || gid.y >= h) return;
    float2 uv = (float2(gid) + 0.5f) / float2(float(w), float(h));
    float2 t = 1.0f / float2(float(src.get_width()), float(src.get_height()));
    float3 a = src.sample(linClamp, uv + float2(-2, -2) * t).rgb;
    float3 b = src.sample(linClamp, uv + float2(0, -2) * t).rgb;
    float3 c = src.sample(linClamp, uv + float2(2, -2) * t).rgb;
    float3 d = src.sample(linClamp, uv + float2(-2, 0) * t).rgb;
    float3 e = src.sample(linClamp, uv).rgb;
    float3 f = src.sample(linClamp, uv + float2(2, 0) * t).rgb;
    float3 g = src.sample(linClamp, uv + float2(-2, 2) * t).rgb;
    float3 hh = src.sample(linClamp, uv + float2(0, 2) * t).rgb;
    float3 i = src.sample(linClamp, uv + float2(2, 2) * t).rgb;
    float3 j = src.sample(linClamp, uv + float2(-1, -1) * t).rgb;
    float3 k = src.sample(linClamp, uv + float2(1, -1) * t).rgb;
    float3 l = src.sample(linClamp, uv + float2(-1, 1) * t).rgb;
    float3 m = src.sample(linClamp, uv + float2(1, 1) * t).rgb;
    float3 r;
    if (prm.x > 0.5f) {   // first level: Karis average to tame single-pixel fireflies
        float3 g0 = (a + b + d + e) * 0.25f, g1 = (b + c + e + f) * 0.25f, g2 = (d + e + g + hh) * 0.25f, g3 = (e + f + hh + i) * 0.25f, g4 = (j + k + l + m) * 0.25f;
        float w0 = karis(g0), w1 = karis(g1), w2 = karis(g2), w3 = karis(g3), w4 = karis(g4);
        r = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3) * 0.125f + g4 * w4 * 0.5f;
        r /= max((w0 + w1 + w2 + w3) * 0.125f + w4 * 0.5f, 1e-4f);
    } else {
        r = e * 0.125f + (a + c + g + i) * 0.03125f + (b + d + f + hh) * 0.0625f + (j + k + l + m) * 0.125f;
    }
    dst.write(float4(r, 1.0f), gid);
}

kernel void bloom_up(texture2d<float> small [[texture(0)]],
                     texture2d<float> base [[texture(1)]],
                     texture2d<float, access::write> dst [[texture(2)]],
                     constant float4& prm [[buffer(0)]],
                     uint2 gid [[thread_position_in_grid]])
{
    const uint w = dst.get_width(), h = dst.get_height();
    if (gid.x >= w || gid.y >= h) return;
    float2 uv = (float2(gid) + 0.5f) / float2(float(w), float(h));
    float2 t = 1.0f / float2(float(small.get_width()), float(small.get_height()));
    float3 s = small.sample(linClamp, uv + float2(-1, -1) * t).rgb * 1.0f + small.sample(linClamp, uv + float2(0, -1) * t).rgb * 2.0f +
               small.sample(linClamp, uv + float2(1, -1) * t).rgb * 1.0f + small.sample(linClamp, uv + float2(-1, 0) * t).rgb * 2.0f +
               small.sample(linClamp, uv).rgb * 4.0f + small.sample(linClamp, uv + float2(1, 0) * t).rgb * 2.0f +
               small.sample(linClamp, uv + float2(-1, 1) * t).rgb * 1.0f + small.sample(linClamp, uv + float2(0, 1) * t).rgb * 2.0f +
               small.sample(linClamp, uv + float2(1, 1) * t).rgb * 1.0f;
    s *= (1.0f / 16.0f);
    float3 b = base.sample(linClamp, uv).rgb;
    dst.write(float4(b + s * prm.x, 1.0f), gid);
}

// ---------------------------------------------------------------------------------------------
// Composite / present
// ---------------------------------------------------------------------------------------------
struct VSOut {
    float4 pos [[position]];
    float2 uv;
};

vertex VSOut present_vs(uint vid [[vertex_id]])
{
    float2 p = float2((vid << 1) & 2, vid & 2);        // fullscreen triangle
    VSOut o;
    o.pos = float4(p * 2.0f - 1.0f, 0.0f, 1.0f);
    o.uv = float2(p.x, 1.0f - p.y);
    return o;
}

inline float3 sample_catmull(texture2d<float> tex, float2 uv)
{
    float2 texSize = float2(float(tex.get_width()), float(tex.get_height()));
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;
    float2 f = samplePos - texPos1;
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / w12;
    float2 t0 = (texPos1 - 1.0f) / texSize;
    float2 t3 = (texPos1 + 2.0f) / texSize;
    float2 t12 = (texPos1 + offset12) / texSize;
    float3 r = float3(0.0f);
    r += tex.sample(linClamp, float2(t0.x, t0.y)).rgb * (w0.x * w0.y);
    r += tex.sample(linClamp, float2(t12.x, t0.y)).rgb * (w12.x * w0.y);
    r += tex.sample(linClamp, float2(t3.x, t0.y)).rgb * (w3.x * w0.y);
    r += tex.sample(linClamp, float2(t0.x, t12.y)).rgb * (w0.x * w12.y);
    r += tex.sample(linClamp, float2(t12.x, t12.y)).rgb * (w12.x * w12.y);
    r += tex.sample(linClamp, float2(t3.x, t12.y)).rgb * (w3.x * w12.y);
    r += tex.sample(linClamp, float2(t0.x, t3.y)).rgb * (w0.x * w3.y);
    r += tex.sample(linClamp, float2(t12.x, t3.y)).rgb * (w12.x * w3.y);
    r += tex.sample(linClamp, float2(t3.x, t3.y)).rgb * (w3.x * w3.y);
    return max(r, 0.0f);
}

inline float3 to_srgb(float3 c)
{
    c = clamp(c, 0.0f, 1.0f);
    float3 lo = c * 12.92f;
    float3 hi = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
    return select(hi, lo, c <= 0.0031308f);
}

fragment float4 present_fs(VSOut in [[stage_in]],
                           constant PostUniforms& P [[buffer(0)]],
                           texture2d<float> hdr [[texture(0)]],
                           texture2d<float> bloom [[texture(1)]],
                           texture2d<float> funnel [[texture(2)]],
                           texture2d<float> hud [[texture(3)]])
{
    const float2 dsz = P.b.zw;
    const float2 pix = in.uv * dsz;
    float3 col;
    float3 lin;
    if (P.d.x > 0.5f) {
        lin = float3(0.0f);
    } else {
        lin = (P.e.z > 0.5f) ? sample_catmull(hdr, in.uv) : hdr.sample(linClamp, in.uv).rgb;
        lin += bloom.sample(linClamp, in.uv).rgb * P.a.w;
    }
    lin *= P.a.z;                                                    // exposure
    float3 mapped = aces_fitted(lin);
    float luma = dot(mapped, float3(0.2126f, 0.7152f, 0.0722f));
    mapped = mix(float3(luma), mapped, P.d.w);                       // saturation
    // vignette
    float2 q = in.uv * 2.0f - 1.0f;
    mapped *= 1.0f - P.d.z * dot(q, q) * 0.25f;
    col = to_srgb(mapped);

    // ----- funnel overlay (spacetime embedding view)
    if (P.d.x > 0.5f) {
        float4 f = funnel.sample(linClamp, in.uv);
        float3 bgc = mix(float3(0.004f, 0.006f, 0.014f), float3(0.012f, 0.018f, 0.04f), 1.0f - in.uv.y);
        float3 flin = bgc + f.rgb;
        col = to_srgb(aces_fitted(flin * 1.0f));
    } else if (P.c.z > P.c.x) {
        float2 lo = P.c.xy, hi = P.c.zw;
        if (all(pix >= lo) && all(pix < hi)) {
            float2 fuv = (pix - lo) / (hi - lo);
            float4 f = funnel.sample(linClamp, fuv);
            float3 base = col * 0.22f + float3(0.004f, 0.007f, 0.016f);
            float3 fc = to_srgb(aces_fitted(f.rgb));
            col = base * (1.0f - f.a) + fc * min(f.a * 1.0f, 1.0f) + fc * (1.0f - min(f.a, 1.0f)) * 0.0f;
            // frame
            float2 e = min(pix - lo, hi - pix);
            float border = 1.0f - smoothstep(0.0f, 2.0f, min(e.x, e.y));
            col = mix(col, float3(0.25f, 0.55f, 0.85f), border * 0.85f);
        }
    }

    // ----- HUD text (premultiplied, display space)
    if (P.d.y > 0.5f) {
        float4 h = hud.sample(linClamp, in.uv);
        col = col * (1.0f - h.a) + h.rgb;
    }

    // dither
    uint2 ip = uint2(pix);
    float n = u01(hash3u(uint3(ip, uint(P.e.y)))) + u01(hash3u(uint3(ip.yx, uint(P.e.y) + 7919u))) - 1.0f;
    col += n * (1.0f / 255.0f);
    return float4(col, 1.0f);
}
