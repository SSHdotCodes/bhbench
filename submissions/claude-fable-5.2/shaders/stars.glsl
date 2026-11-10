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
