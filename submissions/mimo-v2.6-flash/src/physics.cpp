#include "physics.hpp"

#include <algorithm>
#include <cmath>

namespace bh {

TraceOut tracePhoton(Photon y, double stepMax, double rFar, int maxSteps) {
    const double uHor = 1.0 / RS;
    const double uFar = 1.0 / rFar;
    double phi = 0.0;
    Photon prev = y;
    double phiPrev = 0.0;
    int i = 0;
    for (; i < maxSteps; ++i) {
        if (y.u >= uHor) return {Term::Captured, phi, y, i};
        if (y.w < 0.0 && y.u <= uFar) {
            // Interpolate back to u = uFar so the exit point is exactly rFar:
            // symmetrical entry/exit keeps the measured deflection unbiased.
            double ph = phi;
            if (prev.u > uFar && prev.u > y.u) {
                double t = (prev.u - uFar) / (prev.u - y.u);
                if (t > 0.0 && t < 1.0) ph = phiPrev + t * (phi - phiPrev);
            }
            return {Term::Escaped, ph, y, i};
        }

        double h = stepH(y.u, stepMax);
        // Never step u through zero (r -> infinity); that is an escape, handled
        // by the check above on the next iteration.
        if (y.w < 0.0) {
            double hl = (y.u - 0.999 * uFar) / std::max(-y.w, 1e-12);
            if (hl < h) h = std::max(hl, 1e-9);
        }
        Photon yn = rk4Step(y, h);
        if (!(yn.u > 0.0)) return {Term::Escaped, phi + h, yn, i};
        prev = y;
        phiPrev = phi;
        y = yn;
        phi += h;
    }
    return {Term::MaxSteps, phi, y, i};
}

double gFactor(double r, double bLz) {
    double Om = keplerOmega(r);
    double denom = 1.0 - Om * bLz;
    if (denom < 1e-6) denom = 1e-6;
    double grav = std::sqrt(std::max(1.0 - RS / r, 1e-9));
    return grav / denom;
}

DiskParams makeDiskParams(double Msun, double mdot, bool physicalColors) {
    const double G = 6.67430e-8;      // cm^3 g^-1 s^-2
    const double c = 2.99792458e10;   // cm/s
    const double MSUN = 1.98892e33;   // g
    const double YR = 3.155815e7;     // s

    DiskParams d;
    d.massMsun = Msun;
    d.mdotMsunPerYr = mdot;

    double Mgeo = G * (Msun * MSUN) / (c * c);           // cm  ( = 1 length unit )
    double Mdot = mdot * MSUN / YR;                      // g/s
    double Mmass = Msun * MSUN;                          // g

    d.F0 = 3.0 * G * Mmass * Mdot / (8.0 * M_PI * Mgeo * Mgeo * Mgeo);
    d.FPeak = d.F0 * ntFluxShape(R_FPeak);
    d.TPhysPeak = std::pow(d.FPeak / SIGMA_SB, 0.25);
    // Map the (X-ray hot) physical spectrum onto a display response so the
    // image is visible, exactly like a camera exposure / film response curve.
    // The peak of the disk maps to ~6500 K, so the r^-3/4 temperature gradient
    // shows as white-blue in the inner disk and orange near the outer edge.
    d.tempScale = physicalColors ? 1.0 : d.TPhysPeak / 6500.0;
    if (d.tempScale <= 0.0) d.tempScale = 1.0;
    return d;
}

// ---------------------------------------------------------------------------
// Blackbody -> linear sRGB
// ---------------------------------------------------------------------------
std::vector<float> buildBlackbodyLUT(int N) {
    // CIE 1931 2-degree colour matching functions, 3-lobe Gaussian fit
    // (Wyman, Shirley & Wang 2013). Accurate to about 1% over the visible band.
    auto lobe = [](double l, double mu, double s1, double s2) {
        double s = (l < mu) ? s1 : s2;
        double d = (l - mu) / s;
        return std::exp(-0.5 * d * d);
    };
    auto xbar = [&](double l) {
        return 1.056 * lobe(l, 599.8, 37.9, 31.0) + 0.362 * lobe(l, 442.0, 16.0, 26.7) -
               0.065 * lobe(l, 501.1, 20.4, 26.2);
    };
    auto ybar = [&](double l) {
        return 0.821 * lobe(l, 568.8, 46.9, 40.5) + 0.286 * lobe(l, 530.9, 16.3, 31.1);
    };
    auto zbar = [&](double l) {
        return 1.217 * lobe(l, 437.0, 11.8, 36.0) + 0.681 * lobe(l, 459.0, 26.0, 13.8);
    };

    const double TMIN = 300.0, TMAX = 1.0e7;
    const double logTMIN = std::log(TMIN), logRange = std::log(TMAX / TMIN);
    const double c2 = 1.438776877e7;  // second radiation constant hc/k_B, in nm*K

    std::vector<float> lut(static_cast<size_t>(N) * 4, 0.f);
    std::vector<double> wl, planck;
    wl.reserve(401);
    planck.reserve(401);

    for (int i = 0; i < N; ++i) {
        double T = std::exp(logTMIN + logRange * (double(i) / double(N - 1)));

        // Planck shape in log space (overflows otherwise at low T / short lambda).
        double maxLogB = -1e300;
        wl.clear();
        planck.clear();
        for (int nm = 380; nm <= 780; ++nm) {
            double l = double(nm);          // wavelength in nm
            double x = l;
            double a = c2 / (x * T);        // hc / (lambda k T), lambda in nm, c2 in nm K
            double logExpm1 = (a > 30.0) ? a : std::log(std::expm1(a));
            double logB = -5.0 * std::log(x) - logExpm1;
            wl.push_back(l);
            planck.push_back(logB);
            maxLogB = std::max(maxLogB, logB);
        }

        double X = 0, Y = 0, Z = 0;
        for (size_t k = 0; k < wl.size(); ++k) {
            double B = std::exp(planck[k] - maxLogB);
            X += B * xbar(wl[k]);
            Y += B * ybar(wl[k]);
            Z += B * zbar(wl[k]);
        }

        // XYZ -> linear sRGB (D65)
        double r = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
        double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
        double b = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;

        r = std::max(r, 0.0);
        g = std::max(g, 0.0);
        b = std::max(b, 0.0);
        double m = std::max({r, g, b});
        if (m > 0.0) { r /= m; g /= m; b /= m; }

        lut[i * 4 + 0] = float(r);
        lut[i * 4 + 1] = float(g);
        lut[i * 4 + 2] = float(b);
        lut[i * 4 + 3] = 1.f;
    }
    return lut;
}

// ---------------------------------------------------------------------------
Polyline traceEquatorialRay(double b, double rStart, double phiMax, int maxSteps) {
    Polyline out;
    out.b = b;

    Photon y;
    y.u = 1.0 / rStart;
    double radicand = 1.0 / (b * b) - y.u * y.u * (1.0 - RS * y.u);
    if (radicand <= 0.0) return out;
    y.w = std::sqrt(radicand);  // incoming: r decreasing, u increasing

    // Start point in world coordinates: x = -sqrt(r0^2-b^2), y = b (incoming from -x).
    double x0 = -std::sqrt(std::max(rStart * rStart - b * b, 0.0));
    double phi0 = std::atan2(b, x0);

    double phi = 0.0;
    out.pts.reserve(4096);

    auto push = [&](double ph) {
        double r = 1.0 / y.u;
        out.pts.push_back({r * std::cos(ph + phi0), r * std::sin(ph + phi0)});
    };
    push(0.0);

    const double uHor = 1.0 / RS;
    for (int i = 0; i < maxSteps; ++i) {
        if (y.u >= uHor) { out.captured = true; break; }
        double r = 1.0 / y.u;
        if (r >= rStart && y.w < 0.0 && phi > 0.1) break;  // escaped back out
        if (phi > phiMax) break;

        double h = stepH(y.u, 0.03);
        if (phi + h > phiMax) h = phiMax - phi;
        if (h <= 0.0) break;

        Photon yn = rk4Step(y, h);
        if (!(yn.u > 0.0)) break;
        y = yn;
        phi += h;
        push(phi);
    }
    if (!out.pts.empty() && y.u > 0.0) push(phi);
    return out;
}

// ---------------------------------------------------------------------------
double infallProperRadius(double tau, double r0) {
    // (2/3) r0^{3/2} - sqrt(rs) tau = (2/3) r^{3/2}
    double c = (2.0 / 3.0) * std::pow(r0, 1.5) - std::sqrt(RS) * tau;
    if (c <= 0.0) return RS;                       // reached the horizon in finite proper time
    double r = std::pow(1.5 * c, 2.0 / 3.0);       // r = ((3/2) c)^{2/3}
    return std::max(r, RS);
}

void infallCoordinateTimeTable(double r0, double dt, int n, std::vector<double>& outR) {
    // dr/dt = -(1 - rs/r) sqrt(rs/r), starting at r0 at t = 0.
    outR.clear();
    outR.reserve(static_cast<size_t>(n) + 1);
    double r = r0;
    outR.push_back(r);
    auto f = [](double rr) { return -(1.0 - RS / rr) * std::sqrt(RS / rr); };
    for (int i = 0; i < n; ++i) {
        double k1 = f(r);
        double k2 = f(r + 0.5 * dt * k1);
        double k3 = f(r + 0.5 * dt * k2);
        double k4 = f(r + dt * k3);
        r += (dt / 6.0) * (k1 + 2 * k2 + 2 * k3 + k4);
        if (r < RS) r = RS;   // asymptotes to the horizon: the "frozen star"
        outR.push_back(r);
    }
}

}  // namespace bh
