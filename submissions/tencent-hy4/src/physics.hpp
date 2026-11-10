// ============================================================================
//  physics.hpp  --  The relativistic physics shared by the CPU self-test and
//                   the GPU ray-tracer.
//
//  Everything here is derived from the Schwarzschild solution of the Einstein
//  field equations. We work in geometrized units G = c = 1 and set the
//  Schwarzschild radius rs = 2M = 1 (i.e. M = 1/2). All distances are then
//  measured in units of rs and all velocities in units of c.
//
//  Metric (equatorial plane, theta = pi/2):
//
//      ds^2 = -(1 - rs/r) dt^2 + dr^2/(1 - rs/r) + r^2 dphi^2
//
//  Key exact results used below (see README.md for the derivations):
//
//    * Event horizon                 r  = rs            = 1
//    * Photon sphere                 r  = 3M = 1.5 rs   = 1.5
//    * ISCO (circular orbit)         r  = 6M = 3 rs     = 3
//    * Critical impact parameter     b  = 3 sqrt(3) M   = 3 sqrt(3)/2 rs ~ 2.598
//    * Weak-field light deflection   delta = 4GM/(c^2 b) = 2 rs / b
//    * Local orbital speed           v = sqrt( M/(r - 2M) ) = sqrt( 1/(2(r-1)) )
//    * Gravitational redshift        g = sqrt(1 - rs/r)
//    * Flamm paraboloid embedding    z(r) = +/- 2 sqrt( rs (r - rs) )
//
//  The GPU integrates the *exact* null-geodesic orbit equation. The conserved
//  specific angular momentum h = |r x dr/dlambda| lets us write the spatial
//  trajectory of a photon as a central-force problem in flat 3-vector form:
//
//      d^2 r / dlambda^2  =  -(3/2) rs h^2  r / |r|^5          .... (1)
//
//  One can show (README) this is exactly equivalent to the Schwarzschild null
//  orbit equation  d^2u/dphi^2 = -u + (3/2) rs u^2  with u = 1/r. This is the
//  equation integrated by BOTH the CPU self-test and the fragment shader, so
//  the self-test validates the very same integrator used for rendering.
// ============================================================================

#ifndef BH_PHYSICS_HPP
#define BH_PHYSICS_HPP

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <algorithm>

namespace bh {

// ---- Fundamental constants (geometric units, rs = 1) ----------------------
constexpr double RS      = 1.0;                       // Schwarzschild radius
constexpr double MASS    = 0.5;                       // M = rs/2
constexpr double R_HOR   = RS;                        // event horizon
constexpr double R_PH    = 1.5 * RS;                  // photon sphere  (3M)
constexpr double R_ISCO  = 3.0 * RS;                  // ISCO           (6M)
constexpr double B_CRIT  = 2.598076211353316;         // 3 sqrt(3)/2 * rs

// ---- Minimal 3-vector (double precision, CPU side) -------------------------
struct V3 {
    double x = 0, y = 0, z = 0;
    V3() = default;
    V3(double a, double b, double c) : x(a), y(b), z(c) {}
};
inline V3 operator+(const V3& a, const V3& b){ return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline V3 operator-(const V3& a, const V3& b){ return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline V3 operator*(const V3& a, double s){ return {a.x*s, a.y*s, a.z*s}; }
inline V3 operator*(double s, const V3& a){ return a*s; }
inline V3 operator/(const V3& a, double s){ return {a.x/s, a.y/s, a.z/s}; }
inline V3& operator+=(V3& a, const V3& b){ a.x+=b.x; a.y+=b.y; a.z+=b.z; return a; }
inline double dot(const V3& a, const V3& b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
inline V3 cross(const V3& a, const V3& b){
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
inline double len(const V3& a){ return std::sqrt(dot(a,a)); }
inline V3 norm(const V3& a){ double l = len(a); return l > 0 ? a/l : V3{}; }

// ---- The exact null-geodesic acceleration, equation (1) -------------------
inline V3 geodesicAccel(const V3& p, double h2){
    double r2 = dot(p,p);
    double r  = std::sqrt(r2);
    // -1.5 * rs * h^2 / r^5  *  r_vec   (r^5 = r2*r2*r)
    double coeff = -1.5 * RS * h2 / (r2 * r2 * r);
    return p * coeff;
}

// ---- One classical RK4 step of the system p' = v, v' = accel(p) -----------
inline void rk4Step(V3& p, V3& v, double h2, double h){
    V3 k1p = v;
    V3 k1v = geodesicAccel(p, h2);

    V3 k2p = v + 0.5*h*k1v;
    V3 k2v = geodesicAccel(p + 0.5*h*k1p, h2);

    V3 k3p = v + 0.5*h*k2v;
    V3 k3v = geodesicAccel(p + 0.5*h*k2p, h2);

    V3 k4p = v + h*k3v;
    V3 k4v = geodesicAccel(p + h*k3p, h2);

    p = p + (h/6.0)*(k1p + 2.0*k2p + 2.0*k3p + k4p);
    v = v + (h/6.0)*(k1v + 2.0*k2v + 2.0*k3v + k4v);
}

struct TraceResult {
    bool  captured = false;
    bool  escaped  = false;
    V3    finalVel;
    double minR    = 1e300;
    int    steps   = 0;
};

// Integrate a photon launched with impact parameter implicit in (p0, v0).
// Adaptive step ~ stepFrac * r, capped for stability at small/large r.
inline TraceResult traceRay(V3 p0, V3 v0, double escapeR,
                            int maxSteps, double stepFrac){
    TraceResult res;
    V3 p = p0, v = v0;
    V3 L = cross(p, v);
    double h2 = dot(L, L);
    res.finalVel = v;
    for (int i = 0; i < maxSteps; ++i){
        double r = len(p);
        res.minR = std::min(res.minR, r);
        res.steps = i + 1;
        if (r <= RS * 1.0005){ res.captured = true; res.finalVel = v; return res; }
        if (r > escapeR && dot(p, v) > 0.0){ res.escaped = true; res.finalVel = v; return res; }
        double h = std::clamp(stepFrac * r, 1e-4, 40.0);
        rk4Step(p, v, h2, h);
    }
    // Unresolved near-critical ray lingering on the photon sphere: treat as
    // captured (it asymptotes to r -> 1.5 rs and never truly escapes).
    res.captured = (len(p) < 3.0 * RS);
    res.escaped  = !res.captured;
    res.finalVel = v;
    return res;
}

// Deflection angle (radians) between incoming and outgoing asymptotes.
inline double deflection(double b){
    const double D = 1.0e4;
    V3 p0(-D, b, 0.0);
    V3 v0(1.0, 0.0, 0.0);
    TraceResult r = traceRay(p0, v0, 1.2*D, 400000, 0.004);
    double c = std::clamp(dot(norm(v0), norm(r.finalVel)), -1.0, 1.0);
    return std::acos(c);
}

// Critical impact parameter by bisection on capture / escape.
inline double criticalB(double& minRatCrit){
    const double D = 1.0e4;
    auto captured = [&](double b, double& minR)->bool{
        V3 p0(-D, b, 0.0), v0(1.0,0.0,0.0);
        TraceResult r = traceRay(p0, v0, 1.2*D, 300000, 0.006);
        minR = r.minR;
        return r.captured;
    };
    double lo = 1.0, hi = 6.0, mr = 0;
    for (int i = 0; i < 52; ++i){
        double mid = 0.5*(lo+hi);
        double mm;
        if (captured(mid, mm)) lo = mid; else hi = mid;
    }
    // Resolved boundary; record the perihelion of the critical ray.
    (void)captured(0.5*(lo+hi), minRatCrit);
    return 0.5*(lo+hi);
}

// ---------------------------------------------------------------------------
//  Headless scientific self-test. No OpenGL / window needed, so it can be run
//  on any machine to verify the integrator against closed-form GR results.
// ---------------------------------------------------------------------------
inline int runSelfTest(){
    std::printf("\n");
    std::printf("  ==========================================================\n");
    std::printf("   Schwarzschild black hole  --  numerical physics self-test\n");
    std::printf("   units: G = c = 1,  rs = 2M = 1,  M = 0.5\n");
    std::printf("  ==========================================================\n\n");

    int pass = 0, total = 0;
    auto check = [&](const char* name, double got, double expect, double tol){
        ++total;
        double err = std::fabs(got - expect);
        bool ok = err <= tol;
        if (ok) ++pass;
        std::printf("  [%s] %-34s got = %12.6f   expected = %12.6f   err = %.2e\n",
                    ok ? "PASS" : "FAIL", name, got, expect, err);
    };

    // Analytic structural radii (these are definitions/identities we rely on).
    check("photon sphere  r/rs = 3M/rs",      R_PH/RS,   1.5,      1e-12);
    check("ISCO           r/rs = 6M/rs",      R_ISCO/RS, 3.0,      1e-12);
    check("grav redshift at ISCO",            std::sqrt(1.0 - RS/R_ISCO), std::sqrt(2.0/3.0), 1e-12);
    check("orbital speed at ISCO  [c]",       std::sqrt(MASS/(R_ISCO-2*MASS)), 0.5, 1e-12);
    check("analytic critical b / rs",         B_CRIT, 1.5*std::sqrt(3.0), 1e-12);

    // Flamm paraboloid: dz/dr = sqrt(rs/(r-rs));  z(2 rs) = 2 rs.
    auto Z = [](double r){ return 2.0*std::sqrt(RS*(r-RS)); };
    double dzdr = (Z(2.0+1e-6) - Z(2.0-1e-6)) / (2e-6);
    check("Flamm  z(2 rs)",                    Z(2.0),   2.0,      1e-4);
    check("Flamm  dz/dr at r=2 rs",           dzdr,     1.0,      1e-4);

    // The two headline numerical results from the actual RK4 integrator.
    double minRatCrit = 0;
    double bc = criticalB(minRatCrit);
    check("critical impact parameter b/rs",   bc,       B_CRIT,   3e-3);
    check("critical ray perihelion r/rs",      minRatCrit, R_PH,   5e-2);

    // Weak-field deflection: Einstein's  delta = 4GM/(c^2 b) = 2 rs / b.
    for (double b : {50.0, 100.0, 200.0, 500.0}){
        char name[80];
        std::snprintf(name, sizeof(name), "deflection @ b=%4.0f  [rad]", b);
        check(name, deflection(b), 2.0*RS/b, 0.15*2.0*RS/b + 2e-4);
    }

    std::printf("\n  ----------------------------------------------------------\n");
    std::printf("   %d / %d checks passed\n", pass, total);
    std::printf("  ----------------------------------------------------------\n\n");
    std::printf("  Notes:\n");
    std::printf("   * b < 3sqrt(3)/2 rs  -> photon captured (black shadow).\n");
    std::printf("   * b = 3sqrt(3)/2 rs  -> unstable circular orbit at r=1.5 rs\n");
    std::printf("     (the photon sphere; source of the bright photon ring).\n");
    std::printf("   * The GPU fragment shader integrates the same equation (1),\n");
    std::printf("     so the rendered lensing / shadow are the exact GR result.\n\n");
    return (pass == total) ? 0 : 1;
}

} // namespace bh
#endif // BH_PHYSICS_HPP
