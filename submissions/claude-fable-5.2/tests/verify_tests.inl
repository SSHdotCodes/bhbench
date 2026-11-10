// =============================================================================
//  verify_tests.inl — tests of the shader's geodesic code, compiled once per
//  scalar type.  Everything here goes through the functions of
//  shaders/kerr_geodesic.glsl exactly as the GPU executes them.
// =============================================================================

struct HitRecord { double rc, g, t, x, y, E, Lz; };
static std::vector<HitRecord> gHits;
static double gSpinReal = 0.0;     // physical spin; the chart uses -gSpinReal
static bool   gOpaque   = false;

// Records every equatorial crossing the tracer reports.
vec4 planeShade(vec3 x, vec3 p, real E, real t, real rc) {
    real Om = real(1.0) / (rc * sqrt(rc) + real(gSpinReal));      // physical +phi orbit
    real g  = diskRedshift(x, p, E, real(-gSpinReal), rc, -Om);   // reversed in the chart
    HitRecord h;
    h.rc = toDouble(rc); h.g = toDouble(g); h.t = toDouble(t);
    h.x = toDouble(x.x); h.y = toDouble(x.y); h.E = toDouble(E);
    h.Lz = toDouble(x.x * p.y - x.y * p.x);
    gHits.push_back(h);
    return vec4(0.0, 0.0, 0.0, gOpaque ? 1.0 : 0.0);
}

struct Camera {
    vec3 pos;
    vec4 tT, tR, tU, tF;
    double aChart;
};

// Static observer at Kerr-Schild position r (sin i, 0, cos i), looking at the hole.
static Camera makeCamera(double r, double inclDeg, double aReal) {
    double inc = inclDeg * kerr::kPi / 180.0;
    double X[3] = {r * std::sin(inc), 0.0, r * std::cos(inc)};
    double F[3] = {-std::sin(inc), 0.0, -std::cos(inc)};
    double U[3] = {-std::cos(inc), 0.0, std::sin(inc)};
    double R[3] = {F[1] * U[2] - F[2] * U[1], F[2] * U[0] - F[0] * U[2], F[0] * U[1] - F[1] * U[0]};
    kerr::Tetrad T;
    kerr::staticTetrad(X, -aReal, F, R, U, T);
    auto v4 = [&](int A) { return vec4(real(T.e[A][1]), real(T.e[A][2]), real(T.e[A][3]), real(T.e[A][0])); };
    Camera c;
    c.pos = vec3(real(X[0]), real(X[1]), real(X[2]));
    c.tT = v4(0); c.tR = v4(1); c.tU = v4(2); c.tF = v4(3);
    c.aChart = -aReal;
    return c;
}

static TraceParams makeParams(double aReal, double eps, int maxSteps) {
    TraceParams P;
    P.a = real(-aReal);
    P.rHorizon = real(kerr::horizon(aReal));
    P.rFar = real(60.0);
    P.eps = real(eps);
    P.maxSteps = maxSteps;
    P.rPlaneMin = P.rHorizon;
    P.rPlaneMax = P.rFar;
    return P;
}

struct Shot {
    TraceResult res;
    double E, Lz, Q;          // conserved quantities at launch (chart values)
    vec3 x0, p0;
};

static double carterQ(vec3 x, vec3 p, double E, double a) {
    double r   = toDouble(ksRadius(x, real(a)));
    double ct  = toDouble(x.z) / r;
    double st2 = toDouble(x.x * x.x + x.y * x.y) / (r * r + a * a);
    double st  = std::sqrt(st2);
    double Lz  = toDouble(x.x * p.y - x.y * p.x);
    double pth = (ct / st) * toDouble(x.x * p.x + x.y * p.y) - r * st * toDouble(p.z);
    return pth * pth + ct * ct * (Lz * Lz / st2 - a * a * E * E);
}

static Shot shoot(const Camera& c, double sx, double sy, const TraceParams& P) {
    vec4 pe = ksCameraRay(c.pos, c.tT, c.tR, c.tU, c.tF, vec2(real(sx), real(sy)), real(c.aChart));
    Shot s;
    s.x0 = c.pos;
    s.p0 = vec3(pe.x, pe.y, pe.z);
    s.E  = toDouble(pe.w);
    s.Lz = toDouble(c.pos.x * pe.y - c.pos.y * pe.x);
    s.Q  = carterQ(c.pos, s.p0, s.E, c.aChart);
    gHits.clear();
    s.res = traceRay(c.pos, s.p0, pe.w, P);
    return s;
}

// --- 1. constants of motion ---------------------------------------------------
static void testConservation(double eps, double tolH, double tolL, double tolQ) {
    double aReal = 0.9;
    Camera c = makeCamera(30.0, 60.0, aReal);
    double maxH = 0, maxL = 0, maxQ = 0;        // rays that return to infinity
    double capH = 0;                            // rays that fall in (down to 1.02 r+)
    int nEsc = 0, nCap = 0;
    for (int iy = -10; iy <= 10; ++iy) {
        for (int ix = -10; ix <= 10; ++ix) {
            double sx = ix * 0.03, sy = iy * 0.03;
            vec4 pe = ksCameraRay(c.pos, c.tT, c.tR, c.tU, c.tF, vec2(real(sx), real(sy)), real(c.aChart));
            real E = pe.w;
            real a = real(c.aChart);
            KSState s;
            s.x = c.pos; s.p = vec3(pe.x, pe.y, pe.z); s.t = 0.0;
            double L0 = toDouble(s.x.x * s.p.y - s.x.y * s.p.x);
            double Q0 = carterQ(s.x, s.p, toDouble(E), c.aChart);
            double scale = toDouble(E) * toDouble(E);
            double rH = 0, rL = 0, rQ = 0;
            bool escaped = false;
            for (int i = 0; i < 600; ++i) {
                KSDeriv k1 = ksDerivative(s.x, s.p, E, a);
                double r = toDouble(k1.r);
                if (r < kerr::horizon(aReal) * 1.02) break;
                if (r > 80.0) { escaped = true; break; }
                real h = ksStepSize(k1.r, length(k1.dx), real(eps));
                s = ksRK4(s, k1, h, E, a);
                rH = std::max(rH, std::fabs(toDouble(ksHamiltonian(s.x, s.p, E, a))) / scale);
                rL = std::max(rL, std::fabs(toDouble(s.x.x * s.p.y - s.x.y * s.p.x) - L0));
                rQ = std::max(rQ, std::fabs(carterQ(s.x, s.p, toDouble(E), c.aChart) - Q0) / (1.0 + std::fabs(Q0)));
            }
            if (escaped) {
                maxH = std::max(maxH, rH); maxL = std::max(maxL, rL); maxQ = std::max(maxQ, rQ);
                ++nEsc;
            } else {
                capH = std::max(capH, rH);
                ++nCap;
            }
        }
    }
    std::printf("    (a = 0.9, camera at r = 30 M, i = 60 deg: %d rays escape, %d are captured)\n", nEsc, nCap);
    checkBelow("escaping rays: max |H| / E^2  (null condition)", maxH, tolH);
    checkBelow("escaping rays: max |L_z drift| / M E", maxL, tolL);
    checkBelow("escaping rays: max |Carter Q drift| / (1+|Q|)", maxQ, tolQ);
    checkBelow("captured rays: max |H| / E^2 down to 1.02 r+", capH, 20.0 * tolH);
}

// --- 2. capture boundary in the equatorial plane -------------------------------
// Returns the critical L_z/E (chart sign) on the +y (side = +1) or -y side.
static double criticalImpact(double aReal, double eps, int side) {
    Camera c = makeCamera(1000.0, 90.0, aReal);
    TraceParams P = makeParams(aReal, eps, 20000);
    double lo = 0.0, hi = side * 0.02;             // lo captured, hi escaped
    for (int it = 0; it < 60; ++it) {
        double mid = 0.5 * (lo + hi);
        Shot s = shoot(c, mid, 0.0, P);
        if (s.res.status == 0) hi = mid; else lo = mid;
    }
    Shot s = shoot(c, 0.5 * (lo + hi), 0.0, P);
    return s.Lz / s.E;
}

static void testCritical(double eps, double tol) {
    double b0 = criticalImpact(0.0, eps, +1);
    check("Schwarzschild critical impact parameter (3 sqrt 3 M)", b0, std::sqrt(27.0), tol);
    const double spins[3] = {0.5, 0.9, 0.998};
    for (double a : spins) {
        char name[128];
        // Chart spin is -a: the chart's +phi side is retrograde w.r.t. the real hole.
        double bRetro = criticalImpact(a, eps, +1);
        double bPro   = criticalImpact(a, eps, -1);
        std::snprintf(name, sizeof name, "Kerr a=%.3f retrograde photon orbit  b = 3 sqrt(r_ph) + a", a);
        check(name, bRetro, kerr::criticalImpact(-a, true), tol);
        std::snprintf(name, sizeof name, "Kerr a=%.3f prograde photon orbit    b = 3 sqrt(r_ph) - a", a);
        check(name, -bPro, -kerr::criticalImpact(-a, false), tol);
    }
}

// --- 3. light deflection vs the exact Schwarzschild integral -------------------
static double exactDeflection(double b) {
    double u0 = 1.0 / b;
    for (int i = 0; i < 100; ++i) {                 // smallest positive root of 2u^3 - u^2 + 1/b^2
        double g = 2 * u0 * u0 * u0 - u0 * u0 + 1.0 / (b * b);
        double dg = 6 * u0 * u0 - 2 * u0;
        u0 -= g / dg;
    }
    const int N = 200000;
    double sum = 0.0;
    for (int i = 0; i < N; ++i) {
        double s = (i + 0.5) / N;
        double u = u0 * (1.0 - s * s);
        double Q = 2 * u * u + (2 * u0 - 1) * u + u0 * (2 * u0 - 1);
        sum += 2.0 * u0 / std::sqrt(-u0 * Q);
    }
    return 2.0 * sum / N - kerr::kPi;
}

static void testDeflection(double eps, double tolAbs) {
    Camera c = makeCamera(1000.0, 90.0, 0.0);
    TraceParams P = makeParams(0.0, eps, 4000);
    const double bs[8] = {5.5, 6.0, 8.0, 12.0, 20.0, 40.0, 100.0, 400.0};
    for (double b : bs) {
        double lo = 0.0, hi = 0.6;                  // solve L/E = b for the screen coordinate
        for (int it = 0; it < 60; ++it) {
            double mid = 0.5 * (lo + hi);
            vec4 pe = ksCameraRay(c.pos, c.tT, c.tR, c.tU, c.tF, vec2(real(mid), real(0.0)), real(0.0));
            double L = toDouble(c.pos.x * pe.y - c.pos.y * pe.x) / toDouble(pe.w);
            if (L < b) lo = mid; else hi = mid;
        }
        Shot s = shoot(c, 0.5 * (lo + hi), 0.0, P);
        // direction the photon "came from" at infinity on the camera side
        KSDeriv d0 = ksDerivative(s.x0, s.p0, real(s.E), real(0.0));
        vec3 din = -ksAsymptoticDir(s.x0, -d0.dx);
        vec3 dout = s.res.dir;
        double cr = toDouble(length(cross(din, dout)));
        double dt = toDouble(dot(din, dout));
        double defl = std::atan2(cr, dt);
        if (defl < 0) defl += 2 * kerr::kPi;
        double exact = exactDeflection(b);
        // rays that wind past pi come back modulo 2 pi; compare on the circle
        double diff = std::fabs(defl - std::fmod(exact, 2 * kerr::kPi));
        diff = std::min(diff, std::fabs(2 * kerr::kPi - defl - std::fmod(exact, 2 * kerr::kPi)));
        char name[128];
        std::snprintf(name, sizeof name, "deflection b = %6.1f M  exact %.6f rad (%d steps)", b, exact, s.res.steps);
        checkBelow(name, diff, tolAbs);
    }
}

// --- 4. shadow rim vs Bardeen's critical curve ---------------------------------
static void testShadow(double eps, double tol) {
    const double cases[3][2] = {{0.9, 60.0}, {0.998, 85.0}, {0.6, 17.0}};
    for (auto& cs : cases) {
        double aReal = cs[0], incl = cs[1];
        double a = -aReal;                          // chart spin
        Camera c = makeCamera(1000.0, incl, aReal);
        TraceParams P = makeParams(aReal, eps, 20000);
        double worst = 0.0;
        for (int k = 0; k < 16; ++k) {
            double chi = 2 * kerr::kPi * (k + 0.37) / 16;
            double lo = 0.0, hi = 0.02;
            for (int it = 0; it < 50; ++it) {
                double mid = 0.5 * (lo + hi);
                Shot s = shoot(c, mid * std::cos(chi), mid * std::sin(chi), P);
                if (s.res.status == 0) hi = mid; else lo = mid;
            }
            double rho = 0.5 * (lo + hi);
            Shot s = shoot(c, rho * std::cos(chi), rho * std::sin(chi), P);
            double xi = s.Lz / s.E, eta = s.Q / (s.E * s.E);
            // spherical photon orbit radius with this xi
            auto xiC = [&](double r) { return -(r * r * r - 3 * r * r + a * a * r + a * a) / (a * (r - 1.0)); };
            auto etaC = [&](double r) {
                return -r * r * r * (r * r * r - 6 * r * r + 9 * r - 4 * a * a) / (a * a * (r - 1.0) * (r - 1.0));
            };
            double r0 = kerr::photonOrbit(a, true), r1 = kerr::photonOrbit(a, false);
            double f0 = xiC(r0) - xi;
            for (int it = 0; it < 80; ++it) {
                double rm = 0.5 * (r0 + r1);
                double fm = xiC(rm) - xi;
                if ((fm > 0) == (f0 > 0)) { r0 = rm; f0 = fm; } else r1 = rm;
            }
            double etaExact = etaC(0.5 * (r0 + r1));
            worst = std::max(worst, std::fabs(eta - etaExact) / (1.0 + etaExact));
        }
        char name[128];
        std::snprintf(name, sizeof name, "shadow rim on Bardeen's curve, a=%.3f i=%2.0f deg (16 rays)", aReal, incl);
        checkBelow(name, worst, tol);
    }
}

// --- 5. redshift factor of orbiting disk matter ---------------------------------
static void testRedshift(double eps, double tol) {
    // (a) face-on Schwarzschild: g = sqrt(1 - 3/r) / sqrt(1 - 2/r_cam)
    {
        gSpinReal = 0.0; gOpaque = true;
        Camera c = makeCamera(1000.0, 0.001, 0.0);
        TraceParams P = makeParams(0.0, eps, 2000);
        double worst = 0.0;
        for (int i = 1; i <= 12; ++i) {
            shoot(c, 0.0024 * i, 0.0, P);
            if (gHits.empty()) continue;
            const HitRecord& h = gHits[0];
            double expect = std::sqrt(1.0 - 3.0 / h.rc) / std::sqrt(1.0 - 2.0 / 1000.0);
            if (h.rc > 6.0) worst = std::max(worst, std::fabs(h.g / expect - 1.0));
        }
        checkBelow("face-on Schwarzschild disk  g = sqrt(1 - 3M/r)", worst, tol);
    }
    // (b) general: g = 1 / [ u^t (E - Omega L_z) ] with Boyer-Lindquist u^t, all image orders
    const double spins[4] = {0.9, 0.5, -0.5, 0.998};
    for (double aReal : spins) {
        gSpinReal = aReal; gOpaque = false;
        Camera c = makeCamera(1000.0, 77.0, aReal);
        TraceParams P = makeParams(aReal, eps, 2000);
        double worst = 0.0;
        int nh = 0;
        double rin = kerr::isco(aReal);
        for (int iy = -8; iy <= 8; ++iy) {
            for (int ix = -8; ix <= 8; ++ix) {
                Shot s = shoot(c, ix * 0.0031, iy * 0.0031 + 0.0004, P);
                for (const HitRecord& h : gHits) {
                    if (h.rc < rin) continue;
                    // physical photon: L_real = -L_chart
                    double expect = 1.0 / (kerr::circUt(h.rc, aReal) * (s.E + kerr::omegaK(h.rc, aReal) * s.Lz));
                    worst = std::max(worst, std::fabs(h.g / expect - 1.0));
                    ++nh;
                }
            }
        }
        char name[128];
        std::snprintf(name, sizeof name, "Kerr a=%+.3f disk redshift vs closed form (%d hits)", aReal, nh);
        checkBelow(name, worst, tol);
    }
    // (c) sign of the Doppler shift: with spin +z the left side approaches.
    {
        gSpinReal = 0.9; gOpaque = true;
        Camera c = makeCamera(1000.0, 80.0, 0.9);
        TraceParams P = makeParams(0.9, eps, 2000);
        shoot(c, -0.010, -0.0005, P);
        double gLeft = gHits.empty() ? 0 : gHits[0].g;
        double yLeft = gHits.empty() ? 0 : gHits[0].y;
        shoot(c, +0.010, -0.0005, P);
        double gRight = gHits.empty() ? 0 : gHits[0].g;
        std::printf("    left of hole: y = %+.2f M, g = %.4f   right: g = %.4f\n", yLeft, gLeft, gRight);
        checkTrue("approaching (left) side is blueshifted, receding side redshifted",
                  gLeft > 1.0 && gRight < 1.0 && yLeft < 0.0);
    }
    gOpaque = false;
}

// --- 6. coordinate time along radial rays ---------------------------------------
static void testTime(double eps, double tol) {
    // Chart = ingoing Kerr-Schild, a = 0.  Ingoing radial photon: t + r = const.
    real a = 0.0;
    {
        KSState s; s.x = vec3(40.0, 0.0, 0.0); s.t = 0.0;
        vec4 pe = ksLower(s.x, vec4(-1.0, 0.0, 0.0, 1.0), a);
        s.p = vec3(pe.x, pe.y, pe.z);
        real E = pe.w;
        for (int i = 0; i < 400; ++i) {
            KSDeriv k1 = ksDerivative(s.x, s.p, E, a);
            if (toDouble(k1.r) < 3.0) break;
            s = ksRK4(s, k1, ksStepSize(k1.r, length(k1.dx), real(eps)), E, a);
        }
        double r = toDouble(length(s.x));
        checkBelow("ingoing radial photon: |dt - dr| / dr", std::fabs(toDouble(s.t) - (40.0 - r)) / (40.0 - r), tol);
    }
    // Outgoing radial photon: dt/dr = (1 + 2/r)/(1 - 2/r)  ->  t = r + 4 ln(r - 2)
    {
        KSState s; s.x = vec3(4.0, 0.0, 0.0); s.t = 0.0;
        // outgoing null direction: k^r/k^t = (1 - 2/r)/(1 + 2/r)
        double v = (1.0 - 2.0 / 4.0) / (1.0 + 2.0 / 4.0);
        vec4 pe = ksLower(s.x, vec4(real(v), 0.0, 0.0, 1.0), a);
        s.p = vec3(pe.x, pe.y, pe.z);
        real E = pe.w;
        for (int i = 0; i < 400; ++i) {
            KSDeriv k1 = ksDerivative(s.x, s.p, E, a);
            if (toDouble(k1.r) > 50.0) break;
            s = ksRK4(s, k1, ksStepSize(k1.r, length(k1.dx), real(eps)), E, a);
        }
        double r = toDouble(length(s.x));
        double expect = (r - 4.0) + 4.0 * std::log((r - 2.0) / 2.0);
        checkBelow("outgoing radial photon: Shapiro-delayed travel time", std::fabs(toDouble(s.t) / expect - 1.0), tol);
    }
}

static void runAll(const char* label, double eps, double k) {
    std::printf("\n== shader geodesic code, %s arithmetic, step eps = %.2f ==\n", label, eps);
    testConservation(eps, 4e-4 * k, 5e-4 * k, 5e-4 * k);
    testCritical(eps, 2e-4 * k);
    testDeflection(eps, 1.0e-3 * k);
    testShadow(eps, 2e-3 * k);
    testRedshift(eps, 3e-4 * k);
    testTime(eps, 1e-3 * k);
}
