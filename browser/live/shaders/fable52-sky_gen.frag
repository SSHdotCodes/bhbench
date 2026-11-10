#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp samplerCube;
// =============================================================================
//  sky_gen.frag — renders one face of the background cube map, once at start.
//
//  A procedural sky: stars with Euclidean number counts (N(>S) ~ S^-3/2) and
//  blackbody colours, plus a diffuse galactic band with dust lanes.  Baked
//  into a mip-mapped cube map so that the strongly minified, lensed images
//  near the photon ring are filtered correctly.
//  The sky is scenery: its brightness relative to the disk is a free choice.
// =============================================================================
in vec2 vUV;
out vec4 oColor;

uniform int   uFace;
uniform float uTexel;        // angular size of a texel at the face centre (rad)
uniform sampler2D uBlackbody;
uniform vec2  uLogT;
uniform float uLutN;
uniform int   uFirstLayer;  // star layers below this are left to the ray tracer


// Manual equivalent of native linear float lookup sampling, without OES_texture_float_linear.
vec4 nativeLookup(sampler2D tab, vec2 uv) {
    int n=textureSize(tab,0).x;float x=clamp(uv.x*float(n)-0.5,0.0,float(n-1));
    int a=int(floor(x)),b=min(a+1,n-1);return mix(texelFetch(tab,ivec2(a,0),0),texelFetch(tab,ivec2(b,0),0),fract(x));
}
const float PI = 3.14159265358979;

vec3 faceDir(int face, vec2 uv) {
    if (face == 0) return vec3( 1.0, -uv.y, -uv.x);
    if (face == 1) return vec3(-1.0, -uv.y,  uv.x);
    if (face == 2) return vec3( uv.x,  1.0,  uv.y);
    if (face == 3) return vec3( uv.x, -1.0, -uv.y);
    if (face == 4) return vec3( uv.x, -uv.y,  1.0);
    return vec3(-uv.x, -uv.y, -1.0);
}

vec3 bbChroma(float T) {
    float u = clamp((log(T) - uLogT.x) / (uLogT.y - uLogT.x), 0.0, 1.0);
    return nativeLookup(uBlackbody, vec2((u * (uLutN - 1.0) + 0.5) / uLutN, 0.5)).rgb;
}

// =============================================================================
//  stars.glsl — the procedural star catalogue, shared by the sky baker
//  (sky_gen.frag) and the ray tracer (raytrace.frag).
//
//  Stars live in cells of a cube-sphere grid, one candidate per cell per
//  layer; each layer is four times denser and (Euclidean number counts,
//  N(>S) ~ S^-3/2) 4^(2/3) times fainter than the one before.
//  The includer must define  vec3 bbChroma(float T).
// =============================================================================

uint pcg(uint v) {
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
float u01(uint h) { return float(h) * (1.0 / 4294967296.0); }

const float STAR_LAYER0_CELLS = 26.0;   // cells per cube-face edge in layer 0
const float STAR_SIGMA0 = 6.0e-4;       // reference blob size [rad]: a star of unit
                                        // "flux" carries 2 pi STAR_SIGMA0^2 of flux

// Galactic frame: band tilted 62 degrees to the hole's equator, passing behind
// the hole as seen from the default camera.
const vec3 GAL_POLE   = vec3(0.0, 0.8829, 0.4695);
const vec3 GAL_CENTRE = vec3(-0.8192, -0.2693, 0.5064);

float galacticDensity(vec3 d, out float lat, out float lon) {
    vec3 e2 = cross(GAL_POLE, GAL_CENTRE);
    lat = asin(clamp(dot(d, GAL_POLE), -1.0, 1.0));
    lon = atan(dot(d, e2), dot(d, GAL_CENTRE));
    float thin  = exp(-lat * lat / (2.0 * 0.085 * 0.085));
    float thick = exp(-lat * lat / (2.0 * 0.26 * 0.26));
    float bulge = exp(-(lon * lon / (2.0 * 0.30 * 0.30) + lat * lat / (2.0 * 0.17 * 0.17)));
    float arm   = 0.75 + 0.25 * cos(lon);            // brighter towards the centre
    return (0.75 * thin + 0.25 * thick) * arm + 1.1 * bulge;
}

// The star (if any) of layer L in the cell containing direction d.
bool starInCell(vec3 d, int L, float density, out vec3 sd, out float flux, out float T) {
    vec3 ad = abs(d);
    int axis;
    float sgn;
    vec2 uv;
    if (ad.x >= ad.y && ad.x >= ad.z) { axis = 0; sgn = sign(d.x); uv = d.yz / ad.x; }
    else if (ad.y >= ad.z)            { axis = 1; sgn = sign(d.y); uv = d.zx / ad.y; }
    else                              { axis = 2; sgn = sign(d.z); uv = d.xy / ad.z; }
    int face = axis * 2 + (sgn > 0.0 ? 0 : 1);

    float N = STAR_LAYER0_CELLS * exp2(float(L));
    ivec2 cell = clamp(ivec2(floor((uv * 0.5 + 0.5) * N)), ivec2(0), ivec2(int(N) - 1));
    uint h = pcg(uint(cell.x) + pcg(uint(cell.y) + pcg(uint(face * 16 + L))));
    float r0 = u01(h); h = pcg(h);
    float r1 = u01(h); h = pcg(h);
    float r2 = u01(h); h = pcg(h);
    float r3 = u01(h); h = pcg(h);
    float r4 = u01(h);

    // Cube cells near face corners subtend less solid angle: thin them out so
    // stars are uniform per steradian, then follow the galactic density.
    vec2 cuv = (vec2(cell) + 0.5) / N * 2.0 - 1.0;
    if (r0 > pow(1.0 + dot(cuv, cuv), -1.5) * 0.9 * density) return false;

    // Kept clear of the cell border so no neighbour search is needed.
    vec2 suv = (vec2(cell) + 0.2 + 0.6 * vec2(r1, r2)) / N * 2.0 - 1.0;
    sd = axis == 0 ? vec3(sgn, suv.x, suv.y)
       : axis == 1 ? vec3(suv.y, sgn, suv.x)
                   : vec3(suv.x, suv.y, sgn);
    sd = normalize(sd);

    // Euclidean source counts within the layer: S = S_min u^(-2/3).
    flux = pow(0.397, float(L)) * min(pow(max(r3, 1e-4), -0.6667), 40.0);

    // Stellar temperatures: mostly cool-to-solar, a tail of hot blue stars.
    T = r4 < 0.86 ? mix(3000.0, 7500.0, pow(r4 / 0.86, 1.4))
                  : mix(8000.0, 28000.0, pow((r4 - 0.86) / 0.14, 2.0));
    return true;
}


float hash3(ivec3 p) { return u01(pcg(uint(p.x) + pcg(uint(p.y) + pcg(uint(p.z))))); }
float valueNoise3(vec3 p) {
    vec3 i = floor(p), f = p - i;
    vec3 u = f * f * (3.0 - 2.0 * f);
    ivec3 c = ivec3(i);
    float n000 = hash3(c), n100 = hash3(c + ivec3(1, 0, 0));
    float n010 = hash3(c + ivec3(0, 1, 0)), n110 = hash3(c + ivec3(1, 1, 0));
    float n001 = hash3(c + ivec3(0, 0, 1)), n101 = hash3(c + ivec3(1, 0, 1));
    float n011 = hash3(c + ivec3(0, 1, 1)), n111 = hash3(c + ivec3(1, 1, 1));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}
float fbm3(vec3 p) {
    float a = 0.5, s = 0.0;
    for (int o = 0; o < 6; ++o) {
        s += a * valueNoise3(p);
        p = p * 2.03 + vec3(11.7, 3.1, 7.3);
        a *= 0.5;
    }
    return s / 0.984;
}

// Faint stars (layers >= uFirstLayer) baked as small blobs.  The brighter
// layers are drawn by the ray tracer as true point sources.
vec3 stars(vec3 d, float density) {
    vec3 col = vec3(0.0);
    for (int L = uFirstLayer; L < 4; ++L) {
        vec3 sd;
        float flux, T;
        if (!starInCell(d, L, density, sd, flux, T)) continue;
        float sigma = max(STAR_SIGMA0, uTexel * 0.6);
        vec3 dd = d - sd;
        // same total flux whatever the texel size
        col += bbChroma(T) * flux * (STAR_SIGMA0 * STAR_SIGMA0 / (sigma * sigma)) * exp(-dot(dd, dd) / (2.0 * sigma * sigma));
    }
    return col;
}

void main() {
    vec3 d = normalize(faceDir(uFace, vUV * 2.0 - 1.0));

    float lat, lon;
    float gal = galacticDensity(d, lat, lon);

    // Diffuse band: unresolved starlight, modulated by clouds and dust lanes.
    float clouds = fbm3(d * 5.0 + 3.7);
    float fine   = fbm3(d * 23.0 - 9.1);
    float dustN  = fbm3(d * 9.0 + vec3(31.0, 5.0, 17.0));
    float lane   = exp(-lat * lat / (2.0 * 0.05 * 0.05));
    float dust   = 1.0 - 0.85 * lane * smoothstep(0.38, 0.62, dustN);
    float glow   = gal * (0.35 + 1.1 * clouds * clouds) * (0.7 + 0.6 * fine) * dust;

    float bulge = exp(-(lon * lon / (2.0 * 0.30 * 0.30) + lat * lat / (2.0 * 0.17 * 0.17)));
    vec3 tint = mix(vec3(0.72, 0.82, 1.0), vec3(1.0, 0.80, 0.58), clamp(bulge * 1.4 + 0.15, 0.0, 1.0));
    vec3 col = tint * glow * 0.050;

    // Faint reddish emission nebulosity along the plane.
    float neb = smoothstep(0.55, 0.85, fbm3(d * 3.1 + 40.0)) * lane;
    col += vec3(0.9, 0.22, 0.25) * neb * 0.02 * dust;

    // Resolved stars, denser towards the galactic plane.
    float starDensity = clamp(0.30 + 0.70 * min(gal, 1.0), 0.0, 1.0) * (0.35 + 0.65 * dust);
    col += stars(d, starDensity);

    oColor = vec4(col, 1.0);
}
