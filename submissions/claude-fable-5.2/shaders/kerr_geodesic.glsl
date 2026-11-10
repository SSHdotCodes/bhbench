// =============================================================================
//  kerr_geodesic.glsl
//
//  Null geodesics of the Kerr spacetime in Cartesian Kerr-Schild coordinates.
//
//  This file is deliberately written in the common subset of GLSL 4.10 and
//  C++: the GPU ray tracer includes it verbatim, and tests/verify.cpp compiles
//  the *same source* on the CPU (in float32 and float64) to check it against
//  analytic results.  Keep it free of swizzles, out-parameters and uniforms.
//
//  Units: G = c = M = 1, lengths in gravitational radii GM/c^2.
//
//  Metric (ingoing Kerr-Schild form, signature -+++):
//      g_mu_nu = eta_mu_nu + f l_mu l_nu
//      f   = 2 r^3 / (r^4 + a^2 z^2)
//      l_mu = ( 1, (r x + a y)/(r^2+a^2), (r y - a x)/(r^2+a^2), z/r )
//  with r(x,y,z) defined by (x^2+y^2)/(r^2+a^2) + z^2/r^2 = 1.
//  These coordinates are regular across the future horizon, so a photon that
//  falls in is integrated straight through r = r+ with no coordinate trouble.
//
//  Hamiltonian for a photon with covariant momentum p_mu, E = -p_t conserved:
//      H = 1/2 g^{mu nu} p_mu p_nu = 1/2 [ -E^2 + |p|^2 - f (E + l.p)^2 ] = 0
//
//  Backward ray tracing: a camera pixel receives light that travelled *towards*
//  the camera.  Kerr is invariant under (t, a) -> (-t, -a), so tracing that
//  photon backwards in time through a hole of spin a is identical to tracing a
//  photon forwards in time, away from the camera, through a hole of spin -a.
//  The renderer therefore calls this code with the chart spin  a_chart = -a
//  and with all matter velocities reversed.  Captured rays then cross the
//  horizon smoothly instead of piling up on it.
// =============================================================================

struct KSState {
    vec3  x;      // position (x, y, z)
    vec3  p;      // covariant spatial momentum (p_x, p_y, p_z)
    float t;      // coordinate time elapsed along the ray
};

struct KSDeriv {
    vec3  dx;     // dx^i/dlambda
    vec3  dp;     // dp_i/dlambda
    float dt;     // dt/dlambda
    float r;      // Kerr radial coordinate at the evaluation point
};

struct TraceParams {
    float a;          // chart spin (= -a_physical for backward tracing)
    float rHorizon;   // outer horizon radius r+
    float rFar;       // beyond this radius outgoing rays finish analytically
    float eps;        // step size as a fraction of r
    int   maxSteps;   // integration step budget
    float rPlaneMin;  // equatorial crossings are reported for
    float rPlaneMax;  //   rPlaneMin < r < rPlaneMax
};

struct TraceResult {
    vec3  color;      // emission accumulated front-to-back along the ray
    float trans;      // remaining transmittance
    vec3  dir;        // direction at infinity (valid when status == 0)
    int   status;     // 0 escaped | 1 captured | 2 out of steps | 3 absorbed
    float depth;      // Euclidean distance to the first opaque equatorial hit
    int   steps;      // integration steps used
};

// The including program supplies the appearance of the equatorial plane.
// x, p: photon state at the crossing; E: conserved energy; t: coordinate time
// elapsed since leaving the camera; rc: Kerr radius of the crossing.
// Returns (premultiplied emission rgb, opacity).
vec4 planeShade(vec3 x, vec3 p, float E, float t, float rc);

// -----------------------------------------------------------------------------
// Kerr radial coordinate r of a Cartesian Kerr-Schild point.
// -----------------------------------------------------------------------------
float ksRadius(vec3 x, float a) {
    float a2 = a * a;
    float w  = dot(x, x) - a2;
    return sqrt(0.5 * (w + sqrt(w * w + 4.0 * a2 * x.z * x.z)));
}

// -----------------------------------------------------------------------------
// Right-hand side of Hamilton's equations.
//   dx^i/dl =  dH/dp_i = p_i - f w l_i                    (w = E + l.p)
//   dp_i/dl = -dH/dx^i = 1/2 w^2 d_i f + f w d_i (l.p)
//   dt/dl   =  dH/dp_t = E + f w
// All gradients are analytic.
// -----------------------------------------------------------------------------
KSDeriv ksDerivative(vec3 x, vec3 p, float E, float a) {
    float a2 = a * a;
    float z2 = x.z * x.z;
    float w  = dot(x, x) - a2;
    float r2 = 0.5 * (w + sqrt(w * w + 4.0 * a2 * z2));
    float r  = sqrt(r2);
    float r3 = r2 * r;

    float isig = 1.0 / (r2 * r2 + a2 * z2);     // 1 / (r^4 + a^2 z^2)
    float ira  = 1.0 / (r2 + a2);
    float ir   = 1.0 / r;
    float f    = 2.0 * r3 * isig;

    float lxn = r * x.x + a * x.y;              // numerators of l_x, l_y
    float lyn = r * x.y - a * x.x;
    vec3  l   = vec3(lxn * ira, lyn * ira, x.z * ir);

    float lp = dot(l, p);
    float om = E + lp;

    // grad r
    vec3 gr = vec3(r3 * x.x, r3 * x.y, r * (r2 + a2) * x.z) * isig;

    // grad f = (df/dr) grad r + (df/dz)|_r z^
    float dfdr = 2.0 * r2 * (3.0 * a2 * z2 - r2 * r2) * isig * isig;
    float dfdz = -4.0 * a2 * r3 * x.z * isig * isig;
    vec3  gf   = dfdr * gr + vec3(0.0, 0.0, dfdz);

    // grad (l.p) at fixed p = (d(l.p)/dr) grad r + explicit part
    float dlpdr = (x.x * p.x + x.y * p.y) * ira
                - 2.0 * r * (lxn * p.x + lyn * p.y) * ira * ira
                - x.z * p.z * ir * ir;
    vec3 glp = dlpdr * gr
             + vec3((r * p.x - a * p.y) * ira, (a * p.x + r * p.y) * ira, p.z * ir);

    KSDeriv d;
    d.dx = p - (f * om) * l;
    d.dp = (0.5 * om * om) * gf + (f * om) * glp;
    d.dt = E + f * om;
    d.r  = r;
    return d;
}

// H = 1/2 g^{mu nu} p_mu p_nu ; zero for a photon.  Used by the verification.
float ksHamiltonian(vec3 x, vec3 p, float E, float a) {
    float a2 = a * a;
    float r  = ksRadius(x, a);
    float r2 = r * r;
    float f  = 2.0 * r2 * r / (r2 * r2 + a2 * x.z * x.z);
    vec3  l  = vec3((r * x.x + a * x.y) / (r2 + a2), (r * x.y - a * x.x) / (r2 + a2), x.z / r);
    float om = E + dot(l, p);
    return 0.5 * (dot(p, p) - E * E - f * om * om);
}

KSState ksAdvance(KSState s, KSDeriv d, float h) {
    KSState n;
    n.x = s.x + h * d.dx;
    n.p = s.p + h * d.dp;
    n.t = s.t + h * d.dt;
    return n;
}

// Classical 4th-order Runge-Kutta step; k1 is the derivative at s.
KSState ksRK4(KSState s, KSDeriv k1, float h, float E, float a) {
    KSState s2 = ksAdvance(s, k1, 0.5 * h);
    KSDeriv k2 = ksDerivative(s2.x, s2.p, E, a);
    KSState s3 = ksAdvance(s, k2, 0.5 * h);
    KSDeriv k3 = ksDerivative(s3.x, s3.p, E, a);
    KSState s4 = ksAdvance(s, k3, h);
    KSDeriv k4 = ksDerivative(s4.x, s4.p, E, a);
    float w = h / 6.0;
    KSState n;
    n.x = s.x + w * (k1.dx + 2.0 * (k2.dx + k3.dx) + k4.dx);
    n.p = s.p + w * (k1.dp + 2.0 * (k2.dp + k3.dp) + k4.dp);
    n.t = s.t + w * (k1.dt + 2.0 * (k2.dt + k3.dt) + k4.dt);
    return n;
}

// -----------------------------------------------------------------------------
// Affine step length.  A fixed fraction of r gives constant angular resolution
// around the hole, so the cost of a ray grows only logarithmically with
// distance.  The fraction is tightened inside r < 6M, where orbits are
// unstable and errors amplify, and relaxed (up to 2x) beyond r ~ 16M, where
// the field is weak and the path nearly straight: the RK4 error per step
// scales as (h/r)^5 (M/r).
// -----------------------------------------------------------------------------
float ksStepSize(float r, float speed, float eps) {
    float f = min(1.0, 0.2 + 0.1333 * r) * (1.0 + clamp((r - 16.0) / 30.0, 0.0, 1.0));
    return eps * r * f / max(speed, 0.05);
}

// -----------------------------------------------------------------------------
// Lower the index of a contravariant null vector k^mu = (k.xyz, k.w = k^t).
// Returns (p_x, p_y, p_z, E) with E = -p_t.
// -----------------------------------------------------------------------------
vec4 ksLower(vec3 x, vec4 k, float a) {
    float a2 = a * a;
    float r  = ksRadius(x, a);
    float r2 = r * r;
    float f  = 2.0 * r2 * r / (r2 * r2 + a2 * x.z * x.z);
    vec3  l  = vec3((r * x.x + a * x.y) / (r2 + a2), (r * x.y - a * x.x) / (r2 + a2), x.z / r);
    vec3  kv = vec3(k.x, k.y, k.z);
    float lk = k.w + dot(l, kv);                 // l_mu k^mu
    vec3  p  = kv + (f * lk) * l;
    return vec4(p.x, p.y, p.z, k.w - f * lk);
}

// -----------------------------------------------------------------------------
// Photon leaving the camera through a point of the image plane.
// tetT/R/U/F are the camera's orthonormal tetrad legs (contravariant, stored
// as (x, y, z, t)); screen = (right, up) tangents of the viewing angle.
// The photon is normalised so the camera measures unit energy: -p.u_cam = 1.
// Returns (p_x, p_y, p_z, E).
// -----------------------------------------------------------------------------
vec4 ksCameraRay(vec3 camPos, vec4 tetT, vec4 tetR, vec4 tetU, vec4 tetF, vec2 screen, float a) {
    vec3 n = normalize(vec3(screen.x, screen.y, 1.0));
    vec4 k = tetT + n.x * tetR + n.y * tetU + n.z * tetF;
    return ksLower(camPos, k, a);
}

// -----------------------------------------------------------------------------
// Direction at infinity of an outgoing ray at position x with coordinate
// velocity v.  To first order in M the bending still to come is, in the areal
// radial gauge of these coordinates,
//     dpsi = (M/b) (1 - c)^2 (2 + c),   c = cos(angle between x and v),
// which follows from the orbit u(phi) = sin(phi)/b + (M/b^2)(1 + cos^2(phi)).
// Written with (1 - c) = b^2 / (r (r + s)) so it stays finite as b -> 0.
// The neglected terms are O(M^2/r^2).
// -----------------------------------------------------------------------------
vec3 ksAsymptoticDir(vec3 x, vec3 v) {
    float r = length(x);
    vec3  d = v / length(v);
    float s = dot(x, d);
    vec3  b = x - s * d;
    float q = r * (r + s);
    return normalize(d - ((2.0 + s / r) * dot(b, b) / (q * q)) * b);
}

// -----------------------------------------------------------------------------
// Redshift factor g = E_observed / E_emitted for matter on a circular
// equatorial orbit with coordinate angular velocity Omega = dphi/dt.
// The emitter four-velocity u = u^t (1, -Omega y, Omega x, 0) is normalised
// with the Kerr-Schild metric itself, so no Boyer-Lindquist formula enters.
// The photon is normalised to unit energy at the camera, hence g = 1/E_emit.
// (x must lie in the equatorial plane; rc is its Kerr radius.)
// -----------------------------------------------------------------------------
float diskRedshift(vec3 x, vec3 p, float E, float a, float rc, float Omega) {
    float ira = 1.0 / (rc * rc + a * a);
    vec3  l   = vec3((rc * x.x + a * x.y) * ira, (rc * x.y - a * x.x) * ira, 0.0);
    vec3  v   = vec3(-Omega * x.y, Omega * x.x, 0.0);
    float f   = 2.0 / rc;
    float lv  = 1.0 + dot(l, v);
    float n2  = 1.0 - dot(v, v) - f * lv * lv;       // = -g(u,u)/(u^t)^2
    float ut  = inversesqrt(max(n2, 1.0e-12));
    return 1.0 / (ut * (E - dot(p, v)));
}

// -----------------------------------------------------------------------------
// Integrate one camera ray.
// -----------------------------------------------------------------------------
TraceResult traceRay(vec3 x0, vec3 p0, float E, TraceParams P) {
    KSState s;
    s.x = x0;
    s.p = p0;
    s.t = 0.0;

    TraceResult R;
    R.color  = vec3(0.0);
    R.trans  = 1.0;
    R.dir    = vec3(0.0, 0.0, 1.0);
    R.status = 2;
    R.depth  = 1.0e30;
    R.steps  = 0;

    float a2 = P.a * P.a;
    float pmin2 = P.rPlaneMin * P.rPlaneMin;
    float pmax2 = P.rPlaneMax * P.rPlaneMax;

    for (int i = 0; i < P.maxSteps; ++i) {
        KSDeriv k1 = ksDerivative(s.x, s.p, E, P.a);
        R.steps = i;

        if (k1.r < P.rHorizon) {              // fell through the event horizon
            R.status = 1;
            break;
        }
        // Far away and receding steeply enough that the bending still to come
        // is tiny: finish with the analytic weak-field tail.
        float speed = length(k1.dx);
        if (k1.r > P.rFar && dot(s.x, k1.dx) > 0.8 * k1.r * speed) {
            R.status = 0;
            R.dir = ksAsymptoticDir(s.x, k1.dx);
            break;
        }

        float h = ksStepSize(k1.r, speed, P.eps);
        KSState n = ksRK4(s, k1, h, E, P.a);

        // Equatorial plane crossing -> locate it accurately and shade it.
        if (s.x.z * n.x.z < 0.0) {
            float fr = s.x.z / (s.x.z - n.x.z);
            KSState c  = ksRK4(s, k1, h * fr, E, P.a);
            KSDeriv dc = ksDerivative(c.x, c.p, E, P.a);
            float vz = dc.dx.z;
            if (abs(vz) > 1.0e-6) {
                // Newton polish onto z = 0
                c = ksAdvance(c, dc, clamp(-c.x.z / vz, -h, h));
            }
            float rc2 = c.x.x * c.x.x + c.x.y * c.x.y - a2;
            if (rc2 > pmin2 && rc2 < pmax2) {
                vec4 e = planeShade(c.x, c.p, E, c.t, sqrt(rc2));
                R.color = R.color + R.trans * vec3(e.x, e.y, e.z);
                if (e.w > 0.5 && R.depth > 1.0e29) {
                    R.depth = length(c.x - x0);
                }
                R.trans = R.trans * (1.0 - e.w);
                if (R.trans < 0.004) {
                    R.trans = 0.0;
                    R.status = 3;
                    break;
                }
            }
        }
        s = n;
    }
    return R;
}
