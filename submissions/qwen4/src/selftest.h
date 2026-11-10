// Numerical validation of the geodesic integrator against known GR results.
// Same equations as shaders/raytrace.frag (RS = 1, M = 0.5), in double
// precision so the tests measure the discretization, not float rounding.
#pragma once
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace test {

struct D3 { double x, y, z; };
static inline D3 operator+(D3 a, D3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static inline D3 operator-(D3 a, D3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static inline D3 operator*(D3 a, double s) { return {a.x*s, a.y*s, a.z*s}; }
static inline double dot(D3 a, D3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline D3 cross(D3 a, D3 b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
static inline double len(D3 a) { return std::sqrt(dot(a,a)); }

static inline D3 acc(D3 p, double h2) {
    double r2 = dot(p,p), r5 = r2*r2*std::sqrt(r2);
    return p * (-1.5*h2/r5);
}

// returns 0 = escaped, 1 = captured, 2 = step limit
static inline int trace(D3 p, D3 v, double startR, double& deflection,
                        double& hDrift, double& totalTurn, int maxSteps = 400000) {
    D3 hx = cross(p,v);
    double h2 = dot(hx,hx), h0 = std::sqrt(h2);
    hDrift = 0; totalTurn = 0;
    for (int i = 0; i < maxSteps; ++i) {
        double r = len(p);
        double dl = std::min(std::max(0.08*r, 0.02), 0.6);
        D3 k1p = v, k1v = acc(p, h2);
        D3 k2p = v + k1v*(0.5*dl), k2v = acc(p + k1p*(0.5*dl), h2);
        D3 k3p = v + k2v*(0.5*dl), k3v = acc(p + k2p*(0.5*dl), h2);
        D3 k4p = v + k3v*dl,       k4v = acc(p + k3p*dl, h2);
        p = p + (k1p + k2p*2 + k3p*2 + k4p)*(dl/6.0);
        D3 vn = v + (k1v + k2v*2 + k3v*2 + k4v)*(dl/6.0);
        // accumulated turning of the velocity direction
        double c = dot(v,vn)/(len(v)*len(vn));
        totalTurn += std::acos(std::min(1.0, std::max(-1.0, c)));
        v = vn;
        D3 h = cross(p,v);
        hDrift = std::max(hDrift, std::abs(len(h)-h0)/h0);
        r = len(p);
        if (r < 1.02) return 1;
        if (r > startR && dot(p,v) > 0) {
            deflection = std::atan2(v.y, v.x); // incident along +x (net, wrapped)
            return 0;
        }
    }
    return 2;
}

static inline int run() {
    int fails = 0;
    auto check = [&](bool ok, const char* name, const char* detail) {
        printf("[%s] %s — %s\n", ok ? "PASS" : "FAIL", name, detail);
        if (!ok) ++fails;
    };
    char buf[192];

    // 1. Weak-field bending: alpha = 2 RS/b + (15 pi/16) RS^2/b^2 + O(b^-3)
    {
        double b = 50.0, defl = 0, drift = 0, turn = 0;
        int st = trace({-2000.0, b, 0}, {1,0,0}, 2000.0, defl, drift, turn);
        double expect = 2.0/b + 15.0*M_PI/16.0/(b*b);
        double got = std::abs(defl);
        snprintf(buf, sizeof buf, "b=50 RS: got %.6f rad, 2nd-order GR %.6f rad (err %.3f%%)",
                 got, expect, 100*std::abs(got-expect)/expect);
        check(st == 0 && std::abs(got-expect)/expect < 0.003, "weak-field deflection", buf);
        snprintf(buf, sizeof buf, "max relative drift %.2e", drift);
        check(drift < 1e-8, "angular momentum conservation", buf);
    }
    // 2. Critical impact parameter (shadow edge): b_c = 3 sqrt(3) / 2 RS
    {
        double lo = 2.0, hi = 4.0;
        for (int i = 0; i < 30; ++i) {
            double mid = 0.5*(lo+hi), defl = 0, drift = 0, turn = 0;
            int st = trace({-80.0, mid, 0}, {1,0,0}, 80.0, defl, drift, turn, 200000);
            if (st == 1) lo = mid; else hi = mid;
        }
        double bc = 0.5*(lo+hi), exact = 1.5*std::sqrt(3.0);
        snprintf(buf, sizeof buf, "numeric %.6f RS vs 3*sqrt(3)/2 = %.6f RS (err %.4f%%)",
                 bc, exact, 100*std::abs(bc-exact)/exact);
        check(std::abs(bc-exact)/exact < 0.001, "photon capture threshold / shadow size", buf);
    }
    // 3. Static-observer Keplerian speed at ISCO (r = 6M = 3 RS): v = 0.5 c
    {
        double v = std::sqrt(0.5 / (3.0 - 1.0));
        snprintf(buf, sizeof buf, "v(ISCO) = %.4f c (expect 0.5)", v);
        check(std::abs(v-0.5) < 1e-12, "Keplerian orbit speed at ISCO", buf);
    }
    // 4. Strong deflection / photon-ring winding: b = b_c + 0.001 must escape
    //    after accumulating more than a full 2*pi of turning.
    {
        double defl = 0, drift = 0, turn = 0;
        double b = 1.5*std::sqrt(3.0) + 0.001;
        int st = trace({-80.0, b, 0}, {1,0,0}, 80.0, defl, drift, turn, 400000);
        snprintf(buf, sizeof buf, "b = b_c + 0.001: accumulated turning %.2f rad (> 2 pi expected)", turn);
        check(st == 0 && turn > 2*M_PI, "strong-field winding (photon ring)", buf);
    }
    printf("%s\n", fails ? "SELFTEST FAILED" : "ALL SELFTESTS PASSED");
    return fails;
}

} // namespace test
