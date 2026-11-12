#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace bh::physics {

// Geometric units: G = c = 1, M = 1.
// All radii and impact parameters are in units of M = GM/c^2.

inline double clampSpin(double a) {
    return std::clamp(a, 0.0, 0.998);
}

// Outer event horizon radius: r_+ = 1 + sqrt(1 - a^2)
inline double eventHorizonRadius(double a) {
    a = clampSpin(a);
    return 1.0 + std::sqrt(std::max(0.0, 1.0 - a * a));
}

// Inner Cauchy horizon radius: r_- = 1 - sqrt(1 - a^2)
inline double cauchyHorizonRadius(double a) {
    a = clampSpin(a);
    return 1.0 - std::sqrt(std::max(0.0, 1.0 - a * a));
}

// Equatorial photon orbit radii (Bardeen, Press & Teukolsky 1972)
inline double photonSpherePrograde(double a) {
    a = clampSpin(a);
    return 2.0 * (1.0 + std::cos((2.0 / 3.0) * std::acos(-a)));
}

inline double photonSphereRetrograde(double a) {
    a = clampSpin(a);
    return 2.0 * (1.0 + std::cos((2.0 / 3.0) * std::acos(a)));
}

// Innermost Stable Circular Orbit (prograde)
inline double iscoRadius(double a) {
    a = clampSpin(a);
    double oneMinusA2 = std::max(0.0, 1.0 - a * a);
    double z1 = 1.0 + std::cbrt(oneMinusA2) * (std::cbrt(1.0 + a) + std::cbrt(1.0 - a));
    double z2 = std::sqrt(3.0 * a * a + z1 * z1);
    return 3.0 + z2 - std::sqrt(std::max(0.0, (3.0 - z1) * (3.0 + z1 + 2.0 * z2)));
}

// Horizon angular velocity (frame-dragging rate at r_+)
inline double horizonAngularVelocity(double a) {
    a = clampSpin(a);
    double rp = eventHorizonRadius(a);
    return a / (2.0 * rp);
}

// Exact equatorial spatial embedding height z(r) relative to z(rOuter) = 0.
// Uses the smooth substitution s = sqrt(r - r_+) to remove the horizon 1/sqrt(r - r_+)
// coordinate singularity in dz/dr = sqrt((2r - a^2) / ((r - r_+)(r - r_-))).
// For a = 0 this reduces analytically to Flamm's paraboloid:
//   z(r) = 2*sqrt(2*(r - 2)) - 2*sqrt(2*(rOuter - 2)).
inline double kerrEquatorialEmbeddingZ(double r, double a, double rOuter) {
    a = clampSpin(a);
    double rp = eventHorizonRadius(a);
    double rm = cauchyHorizonRadius(a);
    if (r <= rp) r = rp;
    if (rOuter <= rp) return 0.0;

    auto integrand = [a, rp, rm](double s) {
        double rr = rp + s * s;
        double num = std::max(0.0, 2.0 * rr - a * a);
        double den = std::max(1e-12, (rp - rm) + s * s);
        return 2.0 * std::sqrt(num / den);
    };

    double sMax = std::sqrt(rOuter - rp);
    double sVal = std::sqrt(r - rp);
    // Integrate from sVal to sMax with composite Simpson's rule (negated so z <= 0)
    constexpr int kSteps = 64;
    double h = (sMax - sVal) / static_cast<double>(kSteps);
    if (h <= 0.0) return 0.0;

    double sum = integrand(sVal) + integrand(sMax);
    for (int i = 1; i < kSteps; ++i) {
        double s = sVal + static_cast<double>(i) * h;
        sum += (i % 2 == 0 ? 2.0 : 4.0) * integrand(s);
    }
    double integral = (h / 3.0) * sum;
    return -integral;
}

// Generates a radial lookup table of embedding heights z(r) for r in [r_+, rOuter]
inline std::vector<float> buildEmbeddingTable(double a, double rOuter, int count) {
    std::vector<float> table(static_cast<size_t>(count));
    double rp = eventHorizonRadius(a);
    for (int i = 0; i < count; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(count - 1);
        // Concentrate samples near the horizon via quadratic spacing
        double r = rp + (rOuter - rp) * (t * t);
        table[static_cast<size_t>(i)] = static_cast<float>(kerrEquatorialEmbeddingZ(r, a, rOuter));
    }
    return table;
}

// ============================================================================
// CPU Reference Kerr Null Geodesic Integrator (for scientific verification)
// ============================================================================
struct GeodesicConstants {
    double a;
    double E;
    double Lz;
    double Q;
    double K;
};

struct GeodesicState {
    double u;      // 1 / r
    double vu;     // du / dlambda_back
    double qx, qy, qz;   // unit sphere position before frame-dragging
    double vqx, vqy, vqz; // d(q)/dlambda_back
    double phiFD;  // frame-dragging azimuth accumulation
};

enum class RayOutcome {
    Escaped,
    Captured,
    MaxSteps
};

struct RayTraceResult {
    RayOutcome outcome;
    double finalU;
    double finalMu;
    double finalPhi;
    double minR;
    int diskCrossings;
    double maxConstraintErrorU;
    double maxConstraintErrorMu;
    int steps;
};

inline double radialPotentialU(double u, const GeodesicConstants& c) {
    double u2 = u * u;
    double term = c.E + c.a * (c.a * c.E - c.Lz) * u2;
    double deltaU = 1.0 - 2.0 * u + c.a * c.a * u2;
    return term * term - c.K * u2 * deltaU;
}

inline double polarPotentialM(double mu, const GeodesicConstants& c) {
    double mu2 = mu * mu;
    double a2E2 = c.a * c.a * c.E * c.E;
    return c.Q - (c.Q + c.Lz * c.Lz - a2E2) * mu2 - a2E2 * mu2 * mu2;
}

// Initialize photon constants & backward-in-time state from ZAMO observer at (r0, theta0, phi0)
// looking in orthonormal direction (nr, ntheta, nphi) (unit vector).
inline void initZamoRay(
    double r0, double theta0, double phi0,
    double nr, double ntheta, double nphi, double a,
    GeodesicConstants& outC, GeodesicState& outS)
{
    a = clampSpin(a);
    double sinT = std::sin(theta0);
    double cosT = std::cos(theta0);
    double sinP = std::sin(phi0);
    double cosP = std::cos(phi0);
    double sin2 = std::max(1e-12, sinT * sinT);

    double r2 = r0 * r0;
    double a2 = a * a;
    double sigma = r2 + a2 * cosT * cosT;
    double delta = r2 - 2.0 * r0 + a2;
    double A = (r2 + a2) * (r2 + a2) - a2 * delta * sin2;

    double varpi = std::sqrt(A * sin2 / sigma);
    double omega = (2.0 * a * r0) / A;
    double alpha = std::sqrt(sigma * delta / A);

    outC.a = a;
    outC.E = alpha - omega * varpi * nphi;
    outC.Lz = -varpi * nphi;
    outC.Q = sigma * ntheta * ntheta + cosT * cosT * ((outC.Lz * outC.Lz) / sin2 - a2 * outC.E * outC.E);
    outC.K = outC.Q + (outC.Lz - a * outC.E) * (outC.Lz - a * outC.E);

    double u0 = 1.0 / r0;
    outS.u = u0;
    outS.vu = -u0 * u0 * std::sqrt(sigma * delta) * nr;

    // Unit sphere vector q and tangent velocity vq (singularity-free at poles)
    outS.qx = sinT * cosP;
    outS.qy = sinT * sinP;
    outS.qz = cosT;

    double vTheta = std::sqrt(sigma) * ntheta;
    double vPhiSphere = (varpi / std::max(1e-8, sinT)) * nphi;
    // e_theta = (cosT*cosP, cosT*sinP, -sinT), e_phi = (-sinP, cosP, 0)
    outS.vqx = vTheta * (cosT * cosP) + vPhiSphere * (-sinP);
    outS.vqy = vTheta * (cosT * sinP) + vPhiSphere * (cosP);
    outS.vqz = vTheta * (-sinT);
    outS.phiFD = 0.0;
}

inline GeodesicState geodesicRhs(const GeodesicState& s, const GeodesicConstants& c) {
    double u = s.u;
    double u2 = u * u;
    double a = c.a;
    double a2 = a * a;

    double term = c.E + a * (a * c.E - c.Lz) * u2;
    double ddu = 2.0 * a * (a * c.E - c.Lz) * u * term
               - c.K * (u - 3.0 * u2 + 2.0 * a2 * u2 * u);

    double vq2 = s.vqx * s.vqx + s.vqy * s.vqy + s.vqz * s.vqz;
    double a2E2 = a2 * c.E * c.E;
    double qz = s.qz;

    // Exact Euler-Lagrange on unit sphere: q'' = -|q'|^2 q + a^2 E^2 q_z (z_hat - q_z q)
    double radialAccel = -(vq2 + a2E2 * qz * qz);
    double ddqx = radialAccel * s.qx;
    double ddqy = radialAccel * s.qy;
    double ddqz = radialAccel * qz + a2E2 * qz;

    double deltaU = std::max(1e-6, 1.0 - 2.0 * u + a2 * u2);
    double dphiFD = -(a * ((1.0 + a2 * u2) * c.E - a * c.Lz * u2) / deltaU - a * c.E);

    return {s.vu, ddu, s.vqx, s.vqy, s.vqz, ddqx, ddqy, ddqz, dphiFD};
}

inline RayTraceResult traceGeodesicCPU(
    double r0, double theta0, double phi0,
    double nr, double ntheta, double nphi, double a,
    int maxSteps = 2000)
{
    GeodesicConstants c{};
    GeodesicState s{};
    initZamoRay(r0, theta0, phi0, nr, ntheta, nphi, a, c, s);

    double rp = eventHorizonRadius(c.a);
    double uHorizon = 1.0 / (rp + 0.002);
    double omega0 = std::sqrt(std::max(1e-4, c.K + c.E * c.E));

    RayTraceResult res{};
    res.outcome = RayOutcome::MaxSteps;
    res.minR = r0;

    auto computePhiWorld = [](const GeodesicState& st) {
        double cfd = std::cos(st.phiFD);
        double sfd = std::sin(st.phiFD);
        double wx = st.qx * cfd - st.qy * sfd;
        double wy = st.qx * sfd + st.qy * cfd;
        return std::atan2(wy, wx);
    };

    double prevPhiUnwrapped = computePhiWorld(s);
    double totalPhiDelta = 0.0;

    for (int step = 0; step < maxSteps; ++step) {
        double errU = std::abs(s.vu * s.vu - radialPotentialU(s.u, c));
        double errMu = std::abs(s.vqz * s.vqz - polarPotentialM(s.qz, c));
        res.maxConstraintErrorU = std::max(res.maxConstraintErrorU, errU);
        res.maxConstraintErrorMu = std::max(res.maxConstraintErrorMu, errMu);

        double deltaU = std::max(1e-4, 1.0 - 2.0 * s.u + c.a * c.a * s.u * s.u);
        double baseStep = (s.u < 0.08) ? 0.08 : 0.03;
        double hNorm = baseStep / (1.0 + 2.0 * s.u + 0.08 / deltaU);
        double h = hNorm / omega0;

        GeodesicState prev = s;

        GeodesicState k1 = geodesicRhs(s, c);
        GeodesicState s2{
            s.u + 0.5*h*k1.u, s.vu + 0.5*h*k1.vu,
            s.qx + 0.5*h*k1.qx, s.qy + 0.5*h*k1.qy, s.qz + 0.5*h*k1.qz,
            s.vqx + 0.5*h*k1.vqx, s.vqy + 0.5*h*k1.vqy, s.vqz + 0.5*h*k1.vqz,
            s.phiFD + 0.5*h*k1.phiFD};
        GeodesicState k2 = geodesicRhs(s2, c);
        GeodesicState s3{
            s.u + 0.5*h*k2.u, s.vu + 0.5*h*k2.vu,
            s.qx + 0.5*h*k2.qx, s.qy + 0.5*h*k2.qy, s.qz + 0.5*h*k2.qz,
            s.vqx + 0.5*h*k2.vqx, s.vqy + 0.5*h*k2.vqy, s.vqz + 0.5*h*k2.vqz,
            s.phiFD + 0.5*h*k2.phiFD};
        GeodesicState k3 = geodesicRhs(s3, c);
        GeodesicState s4{
            s.u + h*k3.u, s.vu + h*k3.vu,
            s.qx + h*k3.qx, s.qy + h*k3.qy, s.qz + h*k3.qz,
            s.vqx + h*k3.vqx, s.vqy + h*k3.vqy, s.vqz + h*k3.vqz,
            s.phiFD + h*k3.phiFD};
        GeodesicState k4 = geodesicRhs(s4, c);

        s.u     += (h / 6.0) * (k1.u     + 2.0*k2.u     + 2.0*k3.u     + k4.u);
        s.vu    += (h / 6.0) * (k1.vu    + 2.0*k2.vu    + 2.0*k3.vu    + k4.vu);
        s.qx    += (h / 6.0) * (k1.qx    + 2.0*k2.qx    + 2.0*k3.qx    + k4.qx);
        s.qy    += (h / 6.0) * (k1.qy    + 2.0*k2.qy    + 2.0*k3.qy    + k4.qy);
        s.qz    += (h / 6.0) * (k1.qz    + 2.0*k2.qz    + 2.0*k3.qz    + k4.qz);
        s.vqx   += (h / 6.0) * (k1.vqx   + 2.0*k2.vqx   + 2.0*k3.vqx   + k4.vqx);
        s.vqy   += (h / 6.0) * (k1.vqy   + 2.0*k2.vqy   + 2.0*k3.vqy   + k4.vqy);
        s.vqz   += (h / 6.0) * (k1.vqz   + 2.0*k2.vqz   + 2.0*k3.vqz   + k4.vqz);
        s.phiFD += (h / 6.0) * (k1.phiFD + 2.0*k2.phiFD + 2.0*k3.phiFD + k4.phiFD);

        // Keep q on unit sphere and vq orthogonal to q
        double qLen = std::sqrt(s.qx * s.qx + s.qy * s.qy + s.qz * s.qz);
        if (qLen > 1e-12) {
            s.qx /= qLen; s.qy /= qLen; s.qz /= qLen;
        }
        double qdotv = s.qx * s.vqx + s.qy * s.vqy + s.qz * s.vqz;
        s.vqx -= qdotv * s.qx;
        s.vqy -= qdotv * s.qy;
        s.vqz -= qdotv * s.qz;

        constexpr double kPiLoc = 3.14159265358979323846;
        double phiNow = computePhiWorld(s);
        double dPhiStep = phiNow - prevPhiUnwrapped;
        if (dPhiStep > kPiLoc) dPhiStep -= 2.0 * kPiLoc;
        if (dPhiStep < -kPiLoc) dPhiStep += 2.0 * kPiLoc;
        totalPhiDelta += dPhiStep;
        prevPhiUnwrapped = phiNow;

        if (s.u > 1e-9) {
            res.minR = std::min(res.minR, 1.0 / s.u);
        }

        if ((prev.qz > 0.0 && s.qz <= 0.0) || (prev.qz < 0.0 && s.qz >= 0.0)) {
            res.diskCrossings++;
        }

        res.steps = step + 1;

        if (s.u >= uHorizon) {
            res.outcome = RayOutcome::Captured;
            res.finalU = s.u;
            res.finalMu = s.qz;
            res.finalPhi = phi0 + totalPhiDelta;
            return res;
        }

        if (s.u <= 0.0 && s.vu < 0.0) {
            double frac = prev.u / std::max(1e-12, prev.u - s.u);
            res.outcome = RayOutcome::Escaped;
            res.finalU = 0.0;
            res.finalMu = prev.qz + frac * (s.qz - prev.qz);
            // Interpolate final unwrapped azimuth back by (1 - frac) of last step
            res.finalPhi = phi0 + totalPhiDelta - (1.0 - frac) * dPhiStep;
            return res;
        }
    }

    res.finalU = s.u;
    res.finalMu = s.qz;
    res.finalPhi = phi0 + totalPhiDelta;
    return res;
}

} // namespace bh::physics
