// test_kerr.cpp — validation of the shared GPU geodesic core against closed-form general relativity and an
// independent integrator.  The core (src/shaders/kerr_core.h) is compiled here in float (what the GPU runs)
// and double precision.
//
//   1. constraint consistency of the initial data (null condition, Carter constant)
//   2. Schwarzschild light deflection vs the exact elliptic-type integral
//   3. Schwarzschild shadow size vs sin α = 3√3 (1 − 2/r)^{1/2} / r
//   4. Kerr capture vs the analytic Bardeen critical curve (random cameras, spins, directions)
//   5. agreement with an independent Boyer–Lindquist Hamiltonian integrator (Dormand–Prince 5(4), double)
//   6. the r < r_ph⁺ early-capture rule never captures a ray that would escape
//   7. ISCO / photon orbits, 8. Novikov–Thorne flux (closed form + energy conservation), 9. embedding,
//  10. blackbody colorimetry
#include <cstdio>
#include <cstdlib>
#include <string>
#include <random>
#include <vector>

#include "../src/colorimetry.h"
#include "../src/kerr_cpu.h"
#include "../src/physics.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (cond) { ++g_pass; } else {                    \
            ++g_fail;                                     \
            std::printf("  FAIL: " __VA_ARGS__);          \
            std::printf("\n");                            \
        }                                                 \
    } while (0)

// ---------------------------------------------------------------------------------------------
// Precision-generic access to the two compiled copies of the core
// ---------------------------------------------------------------------------------------------
template <typename T> struct K;
#define KERR_API(NS, T)                                                                                          \
    template <> struct K<T> {                                                                                    \
        using real = T;                                                                                          \
        using vec3 = V3<T>;                                                                                      \
        using CamFrame = NS::CamFrame;                                                                           \
        using KerrRay = NS::KerrRay;                                                                             \
        using RayState = NS::RayState;                                                                           \
        using TraceCfg = NS::TraceCfg;                                                                           \
        using TraceResult = NS::TraceResult;                                                                     \
        static const char* name() { return #T; }                                                                 \
        static void frame(real a, real m, real r, real th, real ph, CamFrame& c) {                               \
            NS::kerr_frame_metric(a, m, r, std::sin(th), std::cos(th), c);                                        \
            c.sinP = std::sin(ph); c.cosP = std::cos(ph);                                                        \
            c.fwd = vec3(-1, 0, 0); c.up = vec3(0, -1, 0); c.right = vec3(0, 0, 1);                                \
            c.beta = vec3(0, 0, 0); c.gamma = 1;                                                                 \
        }                                                                                                        \
        static void init(const CamFrame& c, real a, real m, vec3 d, KerrRay& k, RayState& s) {                   \
            NS::kerr_init_ray(c, a, m, d, k, s);                                                                 \
        }                                                                                                        \
        template <class V> static TraceResult trace(const KerrRay& k, RayState s, const TraceCfg& cfg, V& v) {   \
            return NS::kerr_trace(k, s, cfg, v);                                                                 \
        }                                                                                                        \
        static real shadow(real a, real xi, real eta) { return NS::kerr_shadow_fn(a, xi, eta); }                 \
        static real photon(real a, real p) { return NS::kerr_photon_orbit(a, p); }                               \
        static real horizon(real a) { return NS::kerr_horizon(a); }                                              \
        static real isco(real a) { return NS::kerr_isco(a); }                                                    \
    };
KERR_API(kerr64, double)
KERR_API(kerr32, float)

template <typename T>
struct RecordVisitor {
    int planeHits = 0;
    T firstPlaneR = -1;
    T funnelHeight(T) const { return T(0); }
    template <class KR, class RS> bool onPlane(const KR&, const RS& c, int, T, bool) {
        if (planeHits++ == 0) firstPlaneR = T(1) / c.rho;
        return false;
    }
    template <class KR, class RS> bool onFunnel(const KR&, const RS&, const RS&, const RS&, T) { return false; }
};

static double g_stepScale = 1.0;   // 1 = production step sizes (what the GPU uses)

template <typename T>
typename K<T>::TraceCfg makeCfg(double a, bool earlyCapture = true, double rhoEscape = 0.0) {
    typename K<T>::TraceCfg c;
    double rp = phys::horizon(a);
    c.rhoHorizon = T(1.0 / (rp + 1e-4));
    c.rhoCapture = earlyCapture ? T(1.0 / phys::photonOrbit(a, 1)) : T(1e30);
    c.rhoEscape = T(rhoEscape);
    c.invStepAng = T(1.0 / (0.1 * g_stepScale));
    c.invStepRho = T(1.0 / (0.05 * g_stepScale));
    c.invStepPhi = T(1.0 / (0.1 * g_stepScale));
    c.funnelRho = T(0);
    c.maxSteps = 4000;
    c.wantTime = 0;
    return c;
}

static double wrapPi(double x) {
    while (x > M_PI) x -= 2 * M_PI;
    while (x <= -M_PI) x += 2 * M_PI;
    return x;
}

// ---------------------------------------------------------------------------------------------
// 1. constraint consistency of the initial data
// ---------------------------------------------------------------------------------------------
template <typename T>
void testInitConsistency(double tol) {
    using A = K<T>;
    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> U(0, 1);
    double worstP = 0, worstN = 0;
    const double spins[] = {0.0, 0.5, 0.9, 0.998, -0.7};
    for (int i = 0; i < 20000; ++i) {
        double a = spins[i % 5];
        double r = phys::horizon(a) + 0.05 + 100 * U(rng) * U(rng);
        double th = 0.02 + (M_PI - 0.04) * U(rng);
        typename A::CamFrame c;
        A::frame(T(a), T(1), T(r), T(th), T(2 * M_PI * U(rng)), c);
        // random boost
        double bz = 0.8 * U(rng), bth = 2 * M_PI * U(rng), bph = std::acos(2 * U(rng) - 1);
        c.beta = typename A::vec3(T(bz * std::sin(bph) * std::cos(bth)), T(bz * std::sin(bph) * std::sin(bth)), T(bz * std::cos(bph)));
        c.gamma = T(1.0 / std::sqrt(1.0 - bz * bz));
        double ct = 2 * U(rng) - 1, ph = 2 * M_PI * U(rng), st = std::sqrt(1 - ct * ct);
        typename A::KerrRay k;
        typename A::RayState s;
        A::init(c, T(a), T(1), typename A::vec3(T(st * std::cos(ph)), T(st * std::sin(ph)), T(ct)), k, s);
        double rho = s.rho;
        double P = std::pow(k.cA + k.cB * rho * rho, 2) - k.cK * rho * rho * (1 - 2 * rho + a * a * rho * rho);
        double scale = k.E * k.E + k.cK * rho * rho + 1e-30;
        worstP = std::max(worstP, std::fabs(double(s.drho) * s.drho - P) / scale);
        double n2 = dot(s.dn, s.dn), tgt = double(k.vn2) + double(k.a2E2) * s.n.z * s.n.z;
        worstN = std::max(worstN, std::fabs(n2 - tgt) / (std::fabs(tgt) + k.E * k.E * (1 + a * a) + 1e-30));
    }
    std::printf("  [%s] null-condition residual %.2e, angular-constant residual %.2e\n", A::name(), worstP, worstN);
    CHECK(worstP < tol, "[%s] radial null condition violated: %.3e", A::name(), worstP);
    CHECK(worstN < tol, "[%s] angular constant violated: %.3e", A::name(), worstN);
}

// ---------------------------------------------------------------------------------------------
// 2. Schwarzschild deflection: exact azimuth swept from the camera (r0) to infinity
// ---------------------------------------------------------------------------------------------
static double exactSweep(double b, double u0) {
    auto f = [&](double u) { return 1.0 / (b * b) - u * u + 2.0 * u * u * u; };
    double lo = 0, hi = 1.0 / 3.0;   // periapsis: smallest positive root of f
    for (int i = 0; i < 200; ++i) {
        double m = 0.5 * (lo + hi);
        (f(m) > 0 ? lo : hi) = m;
    }
    double up = 0.5 * (lo + hi);
    // ∫_{ua}^{up} du / sqrt(f) with u = up (1 − t²)
    auto seg = [&](double ua) {
        double ta = std::sqrt(1.0 - ua / up);
        static const double xg[8] = {-0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
                                     0.1834346424956498,  0.5255324099163290,  0.7966664774136267,  0.9602898564975363};
        static const double wg[8] = {0.1012285362903763, 0.2223810344533745, 0.3137066458778873, 0.3626837833783620,
                                     0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763};
        const int panels = 400;
        double s = 0;
        for (int p = 0; p < panels; ++p) {
            double t0 = ta * p / panels, t1 = ta * (p + 1) / panels;
            for (int j = 0; j < 8; ++j) {
                double t = 0.5 * (t0 + t1) + 0.5 * (t1 - t0) * xg[j];
                double u = up * (1 - t * t);
                s += 0.5 * (t1 - t0) * wg[j] * 2.0 * up * t / std::sqrt(f(u));
            }
        }
        return s;
    };
    return seg(u0) + seg(0.0);
}

template <typename T>
void testDeflection(double tol) {
    using A = K<T>;
    const double r0 = 1000.0;
    double worst = 0;
    for (double b : {5.4, 6.0, 8.0, 12.0, 20.0, 50.0, 150.0, 400.0}) {
        double sa = b * std::sqrt(1 - 2 / r0) / r0;
        double al = std::asin(sa);
        typename A::CamFrame c;
        A::frame(T(0), T(1), T(r0), T(M_PI / 2), T(0), c);
        typename A::KerrRay k;
        typename A::RayState s;
        A::init(c, T(0), T(1), typename A::vec3(T(std::sin(al)), T(0), T(std::cos(al))), k, s);
        RecordVisitor<T> v;
        auto cfg = makeCfg<T>(0.0);
        auto tr = A::trace(k, s, cfg, v);
        double phiTrace = std::atan2(double(tr.dir.y), double(tr.dir.x));
        double phiExact = exactSweep(b, 1.0 / r0);
        double err = std::fabs(wrapPi(phiTrace - phiExact));
        worst = std::max(worst, err);
        CHECK(tr.status == BH_TRACE_ESCAPED, "[%s] b=%g did not escape", A::name(), b);
        std::printf("  [%s] b = %6.1f M: swept %.9f rad (exact %.9f), error %.2e rad, %d steps\n", A::name(), b,
                    phiTrace < 0 ? phiTrace + 2 * M_PI : phiTrace, phiExact, err, tr.steps);
    }
    CHECK(worst < tol, "[%s] deflection error %.3e > %.1e", A::name(), worst, tol);
}

// ---------------------------------------------------------------------------------------------
// 3. Schwarzschild shadow
// ---------------------------------------------------------------------------------------------
template <typename T>
bool capturedAtAngle(double a, double r, double th, double alpha, double azim) {
    using A = K<T>;
    typename A::CamFrame c;
    A::frame(T(a), T(1), T(r), T(th), T(0), c);
    typename A::KerrRay k;
    typename A::RayState s;
    A::init(c, T(a), T(1),
            typename A::vec3(T(std::sin(alpha) * std::cos(azim)), T(std::sin(alpha) * std::sin(azim)), T(std::cos(alpha))),
            k, s);
    RecordVisitor<T> v;
    auto cfg = makeCfg<T>(a);
    auto tr = A::trace(k, s, cfg, v);
    return tr.status != BH_TRACE_ESCAPED;
}

template <typename T>
void testSchwarzschildShadow(double tol) {
    using A = K<T>;
    for (double ro : {4.0, 6.0, 10.0, 30.0, 100.0}) {
        double exact = std::asin(3 * std::sqrt(3.0) / ro * std::sqrt(1 - 2 / ro));
        if (ro < 3) exact = M_PI - exact;
        double lo = 0, hi = M_PI / 2;
        for (int i = 0; i < 40; ++i) {
            double m = 0.5 * (lo + hi);
            (capturedAtAngle<T>(0.0, ro, M_PI / 2, m, 0.3) ? lo : hi) = m;
        }
        double num = 0.5 * (lo + hi);
        double rel = std::fabs(num - exact) / exact;
        std::printf("  [%s] r_obs = %5.1f M: shadow radius %.8f rad (exact %.8f), rel. error %.1e\n", A::name(), ro,
                    num, exact, rel);
        CHECK(rel < tol, "[%s] shadow radius at r=%g off by %.3e", A::name(), ro, rel);
    }
}

// ---------------------------------------------------------------------------------------------
// 4 + 6. Kerr capture vs analytic critical curve; early-capture rule
// ---------------------------------------------------------------------------------------------
template <typename T>
void testKerrCapture(int n, double band) {
    using A = K<T>;
    std::mt19937 rng(99);
    std::uniform_real_distribution<double> U(0, 1);
    const double spins[] = {0.0, 0.3, 0.7, 0.9, 0.95, 0.998, -0.6};
    int tested = 0, mism = 0, maxst = 0, ruleMism = 0;
    for (int i = 0; i < n; ++i) {
        double a = spins[i % 7];
        double r = 8 + 72 * U(rng);
        double th = (10 + 160 * U(rng)) * M_PI / 180;
        double cone = std::asin(std::min(1.0, 8.0 / r)) * 1.3;
        double alpha = cone * std::sqrt(U(rng)), az = 2 * M_PI * U(rng);
        typename A::CamFrame c;
        A::frame(T(a), T(1), T(r), T(th), T(0.7), c);
        typename A::KerrRay k;
        typename A::RayState s;
        A::init(c, T(a), T(1),
                typename A::vec3(T(std::sin(alpha) * std::cos(az)), T(std::sin(alpha) * std::sin(az)), T(std::cos(alpha))),
                k, s);
        // analytic classification in double from the (float) constants
        double xi = double(k.L) / k.E, eta = double(k.Q) / (double(k.E) * k.E);
        double sfn = kerr64::kerr_shadow_fn(a, xi, eta);
        if (std::fabs(sfn) < band * (1 + std::fabs(eta))) continue;
        bool analyticCaptured = sfn < 0 && s.drho > 0;
        RecordVisitor<T> v;
        auto cfg = makeCfg<T>(a);
        auto tr = A::trace(k, s, cfg, v);
        ++tested;
        if (tr.status == BH_TRACE_MAXSTEPS) ++maxst;
        bool numCaptured = tr.status != BH_TRACE_ESCAPED;
        if (numCaptured != analyticCaptured) {
            if (mism < 5)
                std::printf("    mismatch a=%.3f r=%.2f th=%.1f xi=%.4f eta=%.4f s=%.3e num=%d\n", a, r, th * 180 / M_PI, xi,
                            eta, sfn, tr.status);
            ++mism;
        }
        // 6. the same ray with the early-capture rule disabled (only the horizon stops it)
        RecordVisitor<T> v2;
        auto cfg2 = makeCfg<T>(a, false);
        auto tr2 = A::trace(k, s, cfg2, v2);
        if ((tr2.status == BH_TRACE_ESCAPED) != (tr.status == BH_TRACE_ESCAPED)) ++ruleMism;
    }
    std::printf("  [%s] %d rays: %d disagree with the Bardeen critical curve, %d hit max steps, early-capture rule "
                "changed %d outcomes\n", A::name(), tested, mism, maxst, ruleMism);
    CHECK(mism == 0, "[%s] %d/%d capture mismatches", A::name(), mism, tested);
    CHECK(ruleMism == 0, "[%s] early-capture rule changed %d outcomes", A::name(), ruleMism);
}

// ---------------------------------------------------------------------------------------------
// 5. Independent reference: Boyer–Lindquist Hamiltonian, Dormand–Prince 5(4), double precision
// ---------------------------------------------------------------------------------------------
struct BLRef {
    double a, pt, pph;   // conserved covariant components of the (reversed, past-directed) momentum
    // y = (r, θ, φ, p_r, p_θ)
    double H(double r, double th, double pr, double pth) const {
        double s = std::sin(th), c = std::cos(th), s2 = s * s;
        double Sig = r * r + a * a * c * c, Del = r * r - 2 * r + a * a;
        double Ak = (r * r + a * a) * (r * r + a * a) - a * a * Del * s2;
        double gtt = -Ak / (Sig * Del), gtp = -2 * a * r / (Sig * Del), gpp = (Del - a * a * s2) / (Sig * Del * s2);
        return 0.5 * (gtt * pt * pt + 2 * gtp * pt * pph + gpp * pph * pph + Del / Sig * pr * pr + pth * pth / Sig);
    }
    void f(const double* y, double* dy) const {
        double r = y[0], th = y[1], pr = y[3], pth = y[4];
        double s = std::sin(th), c = std::cos(th), s2 = s * s;
        double Sig = r * r + a * a * c * c, Del = r * r - 2 * r + a * a;
        double gtp = -2 * a * r / (Sig * Del), gpp = (Del - a * a * s2) / (Sig * Del * s2);
        dy[0] = Del / Sig * pr;
        dy[1] = pth / Sig;
        dy[2] = gtp * pt + gpp * pph;
        double hr = 1e-6 * r, ht = 1e-6;
        dy[3] = -(H(r + hr, th, pr, pth) - H(r - hr, th, pr, pth)) / (2 * hr);
        dy[4] = -(H(r, th + ht, pr, pth) - H(r, th - ht, pr, pth)) / (2 * ht);
    }
    // one Dormand–Prince step; returns error estimate
    double step(const double* y, double h, double* yout) const {
        static const double c2 = 1. / 5, c3 = 3. / 10, c4 = 4. / 5, c5 = 8. / 9;
        static const double a21 = 1. / 5, a31 = 3. / 40, a32 = 9. / 40, a41 = 44. / 45, a42 = -56. / 15, a43 = 32. / 9,
                            a51 = 19372. / 6561, a52 = -25360. / 2187, a53 = 64448. / 6561, a54 = -212. / 729,
                            a61 = 9017. / 3168, a62 = -355. / 33, a63 = 46732. / 5247, a64 = 49. / 176,
                            a65 = -5103. / 18656, b1 = 35. / 384, b3 = 500. / 1113, b4 = 125. / 192,
                            b5 = -2187. / 6784, b6 = 11. / 84, e1 = 71. / 57600, e3 = -71. / 16695,
                            e4 = 71. / 1920, e5 = -17253. / 339200, e6 = 22. / 525, e7 = -1. / 40;
        (void)c2; (void)c3; (void)c4; (void)c5;
        double k1[5], k2[5], k3[5], k4[5], k5[5], k6[5], k7[5], t[5];
        f(y, k1);
        for (int i = 0; i < 5; ++i) t[i] = y[i] + h * a21 * k1[i];
        f(t, k2);
        for (int i = 0; i < 5; ++i) t[i] = y[i] + h * (a31 * k1[i] + a32 * k2[i]);
        f(t, k3);
        for (int i = 0; i < 5; ++i) t[i] = y[i] + h * (a41 * k1[i] + a42 * k2[i] + a43 * k3[i]);
        f(t, k4);
        for (int i = 0; i < 5; ++i) t[i] = y[i] + h * (a51 * k1[i] + a52 * k2[i] + a53 * k3[i] + a54 * k4[i]);
        f(t, k5);
        for (int i = 0; i < 5; ++i) t[i] = y[i] + h * (a61 * k1[i] + a62 * k2[i] + a63 * k3[i] + a64 * k4[i] + a65 * k5[i]);
        f(t, k6);
        for (int i = 0; i < 5; ++i) yout[i] = y[i] + h * (b1 * k1[i] + b3 * k3[i] + b4 * k4[i] + b5 * k5[i] + b6 * k6[i]);
        f(yout, k7);
        double err = 0;
        for (int i = 0; i < 5; ++i) {
            double e = h * (e1 * k1[i] + e3 * k3[i] + e4 * k4[i] + e5 * k5[i] + e6 * k6[i] + e7 * k7[i]);
            double sc = 1e-13 + 1e-12 * std::max(std::fabs(y[i]), std::fabs(yout[i]));
            err = std::max(err, std::fabs(e) / sc);
        }
        return err;
    }
    // integrate until r >= rEnd (returns 0) or capture (1) or near-pole (2); records first equator crossing
    int run(double* y, double rEnd, double& firstCross) const {
        double h = 0.01 * y[0];
        firstCross = -1;
        double rp = 1 + std::sqrt(1 - a * a);
        for (int it = 0; it < 2000000; ++it) {
            double yn[5];
            double err = step(y, h, yn);
            if (err > 1.0) { h *= std::max(0.2, 0.9 * std::pow(err, -0.2)); continue; }
            // equator crossing: bisect the step length
            if (firstCross < 0 && (y[1] - M_PI / 2) * (yn[1] - M_PI / 2) < 0) {
                double lo = 0, hi = h, ym[5];
                for (int b = 0; b < 60; ++b) {
                    double m = 0.5 * (lo + hi);
                    step(y, m, ym);
                    ((ym[1] - M_PI / 2) * (y[1] - M_PI / 2) > 0 ? lo : hi) = m;
                }
                step(y, 0.5 * (lo + hi), ym);
                firstCross = ym[0];
            }
            if (yn[0] >= rEnd) {   // bisect to r = rEnd
                double lo = 0, hi = h, ym[5];
                for (int b = 0; b < 60; ++b) {
                    double m = 0.5 * (lo + hi);
                    step(y, m, ym);
                    (ym[0] < rEnd ? lo : hi) = m;
                }
                step(y, 0.5 * (lo + hi), y);
                return 0;
            }
            for (int i = 0; i < 5; ++i) y[i] = yn[i];
            if (y[0] < rp + 1e-3) return 1;
            if (std::sin(y[1]) < 0.02) return 2;
            h *= std::min(5.0, 0.9 * std::pow(std::max(err, 1e-10), -0.2));
            h = std::min(h, 0.05 * y[0] * y[0]);
        }
        return 3;
    }
};

template <typename T>
void testReference(int n, double tolAng, double tolCross) {
    using A = K<T>;
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> U(0, 1);
    const double spins[] = {0.0, 0.5, 0.9, 0.998, -0.8};
    const double rEnd = 400.0;
    std::vector<double> errs, cerrs;
    std::string worstInfo;
    int done = 0;
    for (int i = 0; i < n * 3 && done < n; ++i) {
        double a = spins[i % 5];
        double r = 12 + 50 * U(rng);
        double th = (35 + 110 * U(rng)) * M_PI / 180, ph = 2 * M_PI * U(rng);
        double cone = std::asin(std::min(1.0, 14.0 / r));
        double alpha = cone * std::sqrt(U(rng)), az = 2 * M_PI * U(rng);
        typename A::CamFrame c;
        A::frame(T(a), T(1), T(r), T(th), T(ph), c);
        typename A::KerrRay k;
        typename A::RayState s;
        A::init(c, T(a), T(1),
                typename A::vec3(T(std::sin(alpha) * std::cos(az)), T(std::sin(alpha) * std::sin(az)), T(std::cos(alpha))),
                k, s);
        double xi = double(k.L) / k.E, eta = double(k.Q) / (double(k.E) * k.E);
        if (kerr64::kerr_shadow_fn(a, xi, eta) < 1.5) continue;   // stay well clear of the photon ring
        // reference initial data from the same state (double)
        kerr64::CamFrame cd;
        kerr64::kerr_frame_metric(a, 1.0, r, std::sin(th), std::cos(th), cd);
        double rho = 1.0 / r;
        double prZ = double(s.drho) / (rho * rho * std::sqrt(cd.Sigma * cd.Delta));
        V3<double> eth(std::cos(th) * std::cos(ph), std::cos(th) * std::sin(ph), -std::sin(th));
        V3<double> dnd(s.dn.x, s.dn.y, s.dn.z);
        double pthZ = -dot(dnd, eth) / std::sqrt(cd.Sigma);
        BLRef ref{a, double(k.E), -double(k.L)};   // reversed momentum: k_t = +E, k_φ = −L
        double y[5] = {r, th, ph, -std::sqrt(cd.Sigma / cd.Delta) * prZ, -std::sqrt(cd.Sigma) * pthZ};
        double firstCross;
        int st = ref.run(y, rEnd, firstCross);
        if (st != 0) continue;
        RecordVisitor<T> v;
        auto cfg = makeCfg<T>(a, true, 1.0 / rEnd);
        auto tr = A::trace(k, s, cfg, v);
        if (tr.status != BH_TRACE_ESCAPED) {
            CHECK(false, "[%s] reference escaped but core status %d", A::name(), tr.status);
            continue;
        }
        V3<double> nref(std::sin(y[1]) * std::cos(y[2]), std::sin(y[1]) * std::sin(y[2]), std::cos(y[1]));
        V3<double> ncore(tr.dir.x, tr.dir.y, tr.dir.z);
        double ang = std::atan2(length(cross(nref, ncore)), dot(nref, ncore));
        errs.push_back(ang);
        if (firstCross > 0 && v.firstPlaneR > 0) {
            double ce = std::fabs(v.firstPlaneR - firstCross) / firstCross;
            if (cerrs.empty() || ce > *std::max_element(cerrs.begin(), cerrs.end()))
                worstInfo = "r_cam=" + std::to_string(r) + " th_cam=" + std::to_string(th * 180 / M_PI) +
                            " a=" + std::to_string(a) + " cross r=" + std::to_string(firstCross) +
                            " (core " + std::to_string(double(v.firstPlaneR)) + ")";
            cerrs.push_back(ce);
        }
        ++done;
    }
    std::sort(errs.begin(), errs.end());
    std::sort(cerrs.begin(), cerrs.end());
    double med = errs.empty() ? 0 : errs[errs.size() / 2], mx = errs.empty() ? 0 : errs.back();
    double cmed = cerrs.empty() ? 0 : cerrs[cerrs.size() / 2], cmx = cerrs.empty() ? 0 : cerrs.back();
    std::printf("  [%s] %d rays vs BL/DP45 reference: direction error median %.2e max %.2e rad; "
                "disk-plane radius rel. error median %.2e max %.2e (%zu crossings)\n",
                A::name(), done, med, mx, cmed, cmx, cerrs.size());
    std::printf("       worst crossing: %s\n", worstInfo.c_str());
    CHECK(done > n / 2, "[%s] too few reference rays (%d)", A::name(), done);
    CHECK(mx < tolAng, "[%s] max direction error %.3e", A::name(), mx);
    CHECK(cmx < tolCross, "[%s] max crossing error %.3e", A::name(), cmx);
}

// ---------------------------------------------------------------------------------------------
// 7–10. orbits, disk, embedding, colour
// ---------------------------------------------------------------------------------------------
static void testOrbits() {
    for (double a : {0.0, 0.3, 0.7, 0.9, 0.99, 0.998, -0.5, -0.9}) {
        // ISCO = minimum of the circular-orbit energy (golden-section search)
        double lo = phys::photonOrbit(a, a >= 0 ? 1 : -1) + 0.02, hi = 12.0;
        const double g = (std::sqrt(5.0) - 1) / 2;
        for (int i = 0; i < 200; ++i) {
            double m1 = hi - g * (hi - lo), m2 = lo + g * (hi - lo);
            (phys::circE(a, m1) < phys::circE(a, m2) ? hi : lo) = (phys::circE(a, m1) < phys::circE(a, m2) ? m2 : m1);
        }
        double rmin = 0.5 * (lo + hi), risco = phys::isco(a);
        CHECK(std::fabs(rmin - risco) < 2e-4 * risco, "ISCO a=%g: min E at %.6f vs formula %.6f", a, rmin, risco);
        CHECK(std::fabs(kerr32::kerr_isco(float(a)) - risco) < 1e-4 * risco, "float ISCO a=%g", a);
        // photon orbit: the circular-orbit denominator r^{3/2} − 3r^{1/2} + 2a vanishes (prograde w.r.t. +φ)
        double rph = phys::photonOrbit(a, a >= 0 ? 1 : -1);
        double den = rph * std::sqrt(rph) - 3 * std::sqrt(rph) + 2 * a;
        CHECK(std::fabs(den) < 1e-12, "photon orbit a=%g residual %.2e", a, den);
        std::printf("  a = %+6.3f: r+ = %.5f  r_isco = %.5f  r_ph = %.5f / %.5f  efficiency η = %.4f\n", a,
                    phys::horizon(a), risco, phys::photonOrbit(a, 1), phys::photonOrbit(a, -1), phys::radiativeEfficiency(a));
    }
    CHECK(std::fabs(phys::isco(0) - 6) < 1e-12 && std::fabs(phys::photonOrbit(0, 1) - 3) < 1e-12, "Schwarzschild radii");
}

static void testDisk() {
    // closed form (a = 0)
    auto d0 = phys::buildDisk(0.0, 60.0, 4096);
    double worst = 0;
    for (double r : {6.1, 7.0, 9.55, 15.0, 30.0, 55.0}) {
        double u = std::sqrt((r - d0.rin) / (d0.rout - d0.rin));
        double x = u * (d0.flux.size() - 1);
        int j = int(x);
        double f = x - j;
        double tab = ((1 - f) * d0.flux[j] + f * d0.flux[j + 1]) * d0.fluxMax;
        double ex = phys::ntFluxSchwarzschild(r);
        worst = std::max(worst, std::fabs(tab - ex) / ex);
    }
    std::printf("  NT flux (a=0) table vs closed form: max rel. error %.2e; peak at r = %.3f M (F̃max = %.4e)\n", worst,
                d0.rPeak, d0.fluxMax);
    CHECK(worst < 2e-3, "NT flux a=0 table error %.3e", worst);
    for (double r : {6.5, 10.0, 40.0}) {
        double e = std::fabs(phys::ntFluxDirect(0.0, r) - phys::ntFluxSchwarzschild(r)) / phys::ntFluxSchwarzschild(r);
        CHECK(e < 1e-6, "NT direct vs closed form r=%g: %.3e", r, e);
    }
    // energy conservation: ∫ 4π r E F̃ dr = 1 − E_isco (all binding energy radiated to infinity)
    for (double a : {0.0, 0.5, 0.9, 0.998, -0.5}) {
        double L = phys::diskLuminosityIntegral(a, 1e6), eta = phys::radiativeEfficiency(a);
        double tail = 1.5 / 1e6;   // Newtonian tail beyond r_max
        std::printf("  a = %+6.3f: ∫4πrEF dr = %.6f vs η = %.6f\n", a, L + tail, eta);
        CHECK(std::fabs(L + tail - eta) < 2e-4 * std::max(eta, 0.05), "NT energy a=%g: %.6f vs %.6f", a, L + tail, eta);
    }
}

static void testEmbedding() {
    double worst = 0;
    for (double r : {2.5, 3.0, 6.0, 10.0, 30.0}) {
        double z = phys::embeddingHeight(0.0, r), ex = std::sqrt(8 * (r - 2));
        worst = std::max(worst, std::fabs(z - ex) / ex);
    }
    std::printf("  Flamm paraboloid: max rel. error %.2e\n", worst);
    CHECK(worst < 1e-4, "Flamm paraboloid error %.3e", worst);
    for (double a : {0.5, 0.9, 0.998}) {
        auto f = phys::buildFunnel(a, 30.0, 0.0, 1.0, 256);
        std::printf("  Kerr a=%.3f equatorial embedding: throat R = %.4f, depth %.3f M, non-embeddable samples %d\n", a,
                    f.R0, f.depth, f.nonEmbeddable);
    }
}

static void testColor() {
    // Planck normalisation: ∫B_λ dλ = σT⁴/π
    for (double T : {3000.0, 10000.0}) {
        double s = 0, l0 = std::log(50e-9), l1 = std::log(1e-3);
        const int n = 200000;
        for (int i = 0; i < n; ++i) {
            double lam = std::exp(l0 + (l1 - l0) * (i + 0.5) / n);
            s += color::planckLambda(lam, T) * lam * (l1 - l0) / n;
        }
        double ex = 5.670374419e-8 * std::pow(T, 4) / M_PI;
        CHECK(std::fabs(s - ex) / ex < 1e-5, "Planck normalisation T=%g: %.6e vs %.6e", T, s, ex);
    }
    auto rgbOf = [](double T) {
        double X, Y, Z;
        color::blackbodyXYZPerBolometric(T, X, Y, Z);
        simd_double3 c = color::xyzToLinearSRGB(X, Y, Z);
        double m = std::max({c.x, c.y, c.z});
        return simd_make_double3(c.x / m, c.y / m, c.z / m);
    };
    simd_double3 w = rgbOf(6504), r3 = rgbOf(3000), b20 = rgbOf(20000);
    std::printf("  blackbody sRGB (max-normalised): 3000 K (%.3f %.3f %.3f)  6504 K (%.3f %.3f %.3f)  20000 K (%.3f %.3f %.3f)\n",
                r3.x, r3.y, r3.z, w.x, w.y, w.z, b20.x, b20.y, b20.z);
    CHECK(std::min({w.x, w.y, w.z}) > 0.93, "6504 K blackbody should be near-white");
    CHECK(r3.x > r3.y && r3.y > r3.z, "3000 K should be orange-red");
    CHECK(b20.z > b20.y && b20.y > b20.x, "20000 K should be blue");
}

int main() {
    std::printf("== 1. initial-data constraints\n");
    testInitConsistency<double>(1e-12);
    testInitConsistency<float>(2e-5);
    std::printf("== 2. Schwarzschild deflection (camera at r = 1000 M)\n");
    std::printf(" -- double, step sizes x0.2 (convergence: RK4 error should drop ~625x)\n");
    g_stepScale = 0.2;
    testDeflection<double>(1e-8);
    g_stepScale = 1.0;
    std::printf(" -- float, production step sizes (GPU configuration)\n");
    testDeflection<float>(2e-5);
    std::printf("== 3. Schwarzschild shadow radius\n");
    g_stepScale = 0.2;
    testSchwarzschildShadow<double>(1e-9);
    g_stepScale = 1.0;
    testSchwarzschildShadow<float>(2e-6);
    std::printf("== 4/6. Kerr capture vs Bardeen critical curve; early-capture rule\n");
    testKerrCapture<double>(20000, 1e-4);
    testKerrCapture<float>(20000, 2e-3);
    std::printf("== 5. independent Boyer–Lindquist Hamiltonian integrator (DP45, double)\n");
    g_stepScale = 0.2;
    std::printf(" -- double, step sizes x0.2\n");
    testReference<double>(300, 2e-8, 2e-6);
    g_stepScale = 1.0;
    std::printf(" -- double / float, production step sizes\n");
    testReference<double>(300, 1e-5, 1e-4);
    testReference<float>(300, 2e-5, 1e-4);
    std::printf("== 7. circular orbits\n");
    testOrbits();
    std::printf("== 8. Novikov–Thorne disk\n");
    testDisk();
    std::printf("== 9. embedding diagram\n");
    testEmbedding();
    std::printf("== 10. blackbody colorimetry\n");
    testColor();
    std::printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
