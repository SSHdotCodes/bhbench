// Numerical validation of the physics core against closed-form GR results.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "blackbody.h"
#include "geodesic.h"
#include "physics.h"

using m3::Vec3;

static int g_failures = 0;
static int g_checks = 0;

static void check(const char* name, double got, double want, double tolRel,
                  const char* unit = "") {
    ++g_checks;
    double denom = std::fabs(want) > 1e-30 ? std::fabs(want) : 1.0;
    double err = std::fabs(got - want) / denom;
    bool ok = err <= tolRel;
    if (!ok) ++g_failures;
    printf("  [%s] %-46s got %14.8f  want %14.8f  rel.err %.2e %s\n",
           ok ? "PASS" : "FAIL", name, got, want, err, unit);
}

static void checkTrue(const char* name, bool cond, const char* detail = "") {
    ++g_checks;
    if (!cond) ++g_failures;
    printf("  [%s] %-46s %s\n", cond ? "PASS" : "FAIL", name, detail);
}

// ---------------------------------------------------------------------------
// 1. Weak-field light deflection. Series in M/b:
//      alpha = 4M/b + 15 pi M^2 / (4 b^2) + 128 M^3 / (3 b^3) + ...
// ---------------------------------------------------------------------------
static void testDeflection() {
    printf("\n== Gravitational light deflection (weak field) ==\n");
    const double L = 1e7;
    for (double b : {200.0, 100.0, 50.0}) {
        geo::Result r = geo::trace(Vec3(-L, b, 0), Vec3(1, 0, 0),
                                   /*maxSteps=*/400000, /*escapeR=*/L,
                                   /*stepScale=*/0.5, /*recordPath=*/false);
        double expected = 4.0 / b + 15.0 * M_PI / (4.0 * b * b) +
                          128.0 / (3.0 * b * b * b);
        char label[96];
        snprintf(label, sizeof(label), "deflection b=%.0f M (3-term series)", b);
        check(label, r.deflection, expected, 2e-3, "rad");
    }

    // Einstein's 1915 value for a grazing ray at the solar limb: 1.75 arcsec.
    // Here we simply confirm the leading 4M/b term dominates at large b.
    geo::Result r = geo::trace(Vec3(-L, 1000.0, 0), Vec3(1, 0, 0), 400000, L, 0.5, false);
    check("deflection b=1000 M vs 4M/b", r.deflection, 4.0 / 1000.0, 5e-3, "rad");
}

// ---------------------------------------------------------------------------
// 2. Critical impact parameter: bisection on capture.
// ---------------------------------------------------------------------------
static bool captured(double b) {
    geo::Result r = geo::trace(Vec3(-1e4, b, 0), Vec3(1, 0, 0),
                               /*maxSteps=*/2000000, /*escapeR=*/1e4,
                               /*stepScale=*/0.4, /*recordPath=*/false);
    return r.captured;
}

static void testShadow() {
    printf("\n== Photon capture / shadow edge ==\n");
    double lo = 4.0, hi = 7.0;  // lo captured, hi escapes
    checkTrue("b=4M is captured", captured(lo));
    checkTrue("b=7M escapes", !captured(hi));
    for (int i = 0; i < 60; ++i) {
        double mid = 0.5 * (lo + hi);
        if (captured(mid)) lo = mid; else hi = mid;
    }
    double bc = 0.5 * (lo + hi);
    check("critical impact parameter b_c", bc, bh::kBCrit, 2e-4, "M");

    // Angular radius of the shadow for a distant static observer: sin(a)=b_c/r.
    double rObs = 1e6;
    double alpha = bh::shadowAngularRadius(rObs);
    check("shadow angular radius at r=1e6 M", alpha, bh::kBCrit / rObs, 1e-6, "rad");

    // At the photon sphere the shadow fills exactly half the sky.
    check("shadow angular radius at r=3M", bh::shadowAngularRadius(3.0),
          M_PI / 2.0, 1e-9, "rad");
}

// ---------------------------------------------------------------------------
// 3. Photon sphere: near-critical rays should linger at r = 3M.
// ---------------------------------------------------------------------------
static void testPhotonSphere() {
    printf("\n== Photon sphere ==\n");
    for (double eps : {1e-2, 1e-3}) {
        double b = bh::kBCrit * (1.0 + eps);
        geo::Result r = geo::trace(Vec3(-1e4, b, 0), Vec3(1, 0, 0), 4000000, 1e4, 0.4, true);
        double rmin = 1e30;
        for (auto& p : r.path) rmin = std::min(rmin, (double)m3::length(p.pos));
        char label[96];
        snprintf(label, sizeof(label), "turning point for b=b_c(1+%.0e)", eps);
        // r_min -> 3M as eps -> 0; allow a few percent at finite eps.
        check(label, rmin, 3.0, eps < 1e-2 ? 0.06 : 0.15, "M");
    }
}

// ---------------------------------------------------------------------------
// 4. Conserved quantities along a ray.
// ---------------------------------------------------------------------------
static void testConservation() {
    printf("\n== Conservation along the geodesic ==\n");
    geo::Result r = geo::trace(Vec3(-60, 8, 0), Vec3(1, 0, 0), 200000, 60, 0.5, true);
    double emax = 0, hmax = 0;
    for (auto& p : r.path) {
        geo::Conserved q = geo::conservedQuantities(p.pos, p.vel);
        emax = std::max(emax, std::fabs(q.E - r.q.E) / r.q.E);
        hmax = std::max(hmax, std::fabs(q.h - r.q.h) / r.q.h);
    }
    check("max relative drift in E", emax, 0.0, 1e-5);
    check("max relative drift in h", hmax, 0.0, 1e-5);
}

// ---------------------------------------------------------------------------
// 5. Disk kinematics.
// ---------------------------------------------------------------------------
static void testDisk() {
    printf("\n== Accretion disk kinematics ==\n");
    check("orbital speed at ISCO (r=6M)", bh::orbitalBeta(6.0), 0.5, 1e-12, "c");
    check("u^t at ISCO", bh::circularUt(6.0), std::sqrt(2.0), 1e-12);
    check("Omega at ISCO", bh::keplerOmega(6.0), std::sqrt(1.0 / 216.0), 1e-12);

    // Face-on view (b_z = 0): pure gravitational redshift, g = sqrt(1-3M/r).
    check("g at ISCO, face-on", bh::redshiftFactor(6.0, 0.0), std::sqrt(0.5), 1e-12);
    check("g at r=20M, face-on", bh::redshiftFactor(20.0, 0.0),
          std::sqrt(1.0 - 3.0 / 20.0), 1e-12);

    // Approaching side is blueshifted, receding side redshifted.
    // Convention: b_z = L_z/E of the *physical* photon. For a disk rotating with
    // Omega > 0 about +z and an observer in the equatorial plane, the material
    // moving toward the observer carries L_z > 0.
    double gApproach = bh::redshiftFactor(6.0, +6.0);
    double gRecede = bh::redshiftFactor(6.0, -6.0);
    checkTrue("approaching limb blueshifted (g>1)", gApproach > 1.0);
    checkTrue("receding limb redshifted (g<1)", gRecede < 1.0);
    printf("         g_approach = %.4f   g_recede = %.4f   ratio = %.3f\n",
           gApproach, gRecede, gApproach / gRecede);

    // End-to-end geometry check: a ray traced *backwards* from the camera must
    // recover the same g as the forward photon. This pins the sign convention
    // used by the shader (b_z = -L_z_backward / E).
    {
        Vec3 cam(100, 0, 0);
        Vec3 target(0, -6, 0);  // approaching side of the disk
        Vec3 vb = m3::normalize(target - cam);
        Vec3 Lb = m3::cross(cam, vb);
        double bzBack = -Lb.z;  // E ~ 1 at r = 100
        check("backward-ray b_z recovers forward photon", bzBack, 5.989, 2e-3);
        check("g from backward ray at approaching limb",
              bh::redshiftFactor(6.0, bzBack), gApproach, 2e-3);
    }

    // Novikov-Thorne temperature profile.
    check("NT profile at inner edge", bh::ntTheta(1.0), 0.0, 1e-12);
    check("NT profile peak location", bh::ntTheta(bh::kNTProfilePeakX), 1.0, 1e-5);
    check("NT flux at inner edge", bh::ntFlux(1.0), 0.0, 1e-12);
    checkTrue("NT profile decreases beyond peak", bh::ntTheta(4.0) < bh::ntTheta(2.0));

    // Disk efficiency: 1 - E_ISCO = 1 - sqrt(8/9) = 5.72%.
    check("thin-disk radiative efficiency", bh::kDiskEfficiency, 0.057190958, 1e-6);
}

// ---------------------------------------------------------------------------
// 6. Physical disk temperature for a realistic system.
// ---------------------------------------------------------------------------
static void testTemperature() {
    printf("\n== Novikov-Thorne peak temperature ==\n");
    // A 1e8 Msun AGN at 10% Eddington: the disk peaks in the far-UV, which is
    // exactly the observed "big blue bump" of quasars.
    double T = bh::diskPeakTemperature(1e8, 0.1);
    printf("         M=1e8 Msun, 0.1 Eddington -> T_peak = %.0f K\n", T);
    check("T_peak for a 1e8 Msun AGN at 0.1 Edd", T, 8.64e4, 2e-2, "K");

    // Scaling: T ~ M^(-1/4) at fixed Eddington ratio.
    double T2 = bh::diskPeakTemperature(1e9, 0.1);
    check("T_peak scales as M^-1/4", T2 / T, std::pow(10.0, -0.25), 2e-3);

    // Scaling: T ~ Mdot^(1/4).
    double T3 = bh::diskPeakTemperature(1e8, 0.4);
    check("T_peak scales as Mdot^1/4", T3 / T, std::pow(4.0, 0.25), 2e-3);

    // A quiescent ultramassive black hole lands in the visible band.
    double T4 = bh::diskPeakTemperature(1e10, 1e-3);
    printf("         M=1e10 Msun, 1e-3 Eddington -> T_peak = %.0f K\n", T4);
    checkTrue("quiescent ultramassive disk peaks in the visible",
              T4 > 4000 && T4 < 20000);
}

// ---------------------------------------------------------------------------
// 7. Blackbody colour science.
// ---------------------------------------------------------------------------
static void testBlackbody() {
    printf("\n== Blackbody colour ==\n");
    bb::RGB w = bb::blackbodyLinearRGB(6500.0);
    double mx = std::max({w.r, w.g, w.b}), mn = std::min({w.r, w.g, w.b});
    checkTrue("6500 K is nearly neutral", (mx - mn) / mx < 0.25);

    bb::RGB cool = bb::blackbodyLinearRGB(2800.0);
    checkTrue("2800 K is red-dominant", cool.r > cool.g && cool.g > cool.b);

    bb::RGB hot = bb::blackbodyLinearRGB(25000.0);
    checkTrue("25000 K is blue-dominant", hot.b > hot.g && hot.g > hot.r);

    // Visible-band luminance must rise monotonically with temperature.
    double prev = -1;
    bool mono = true;
    for (double T = 1000; T <= 100000; T *= 1.5) {
        bb::RGB c = bb::blackbodyLinearRGB(T);
        double y = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
        if (y <= prev) mono = false;
        prev = y;
    }
    checkTrue("visible luminance increases with T", mono);

    // Wien: peak wavelength of B_lambda at 5778 K is ~501 nm.
    double best = 0, bestL = 0;
    for (double nm = 100; nm < 3000; nm += 0.5) {
        double v = bb::planckRadiance(nm * 1e-9, 5778.0);
        if (v > bestL) { bestL = v; best = nm; }
    }
    check("Wien peak wavelength at 5778 K", best, 501.5, 5e-3, "nm");
}

int main() {
    printf("Schwarzschild black hole physics validation\n");
    printf("units: G = c = 1, M = 1  (r_s = 2, photon sphere = 3, ISCO = 6)\n");

    testDeflection();
    testShadow();
    testPhotonSphere();
    testConservation();
    testDisk();
    testTemperature();
    testBlackbody();

    printf("\n----------------------------------------------\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
