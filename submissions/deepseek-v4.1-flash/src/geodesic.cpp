#include "geodesic.h"

#include <cmath>

namespace geo {

using m3::Vec3;

Conserved conservedQuantities(Vec3 pos, Vec3 vel) {
    Conserved q;
    double r = m3::length(pos);
    Vec3 L = m3::cross(pos, vel);
    q.h = m3::length(L);
    double vr = m3::dot(vel, pos) / r;
    double lapse = 1.0 - bh::kRs / r;
    q.E = std::sqrt(std::max(0.0, vr * vr + lapse * q.h * q.h / (r * r)));
    q.bz = (q.E > 1e-12) ? L.z / q.E : 0.0;
    return q;
}

Vec3 localToCoordinateVelocity(Vec3 pos, Vec3 localDir) {
    double r = m3::length(pos);
    Vec3 rhat = pos / r;
    double nr = m3::dot(localDir, rhat);
    Vec3 nt = localDir - rhat * nr;
    double lapse = std::sqrt(std::max(1e-9, 1.0 - bh::kRs / r));
    return rhat * (nr * lapse) + nt;
}

namespace {

inline Vec3 accel(Vec3 p, double h) {
    double r2 = m3::dot(p, p);
    double r = std::sqrt(r2);
    double k = -1.5 * bh::kRs * h * h / (r2 * r2 * r);
    return p * k;
}

}  // namespace

Result trace(Vec3 pos, Vec3 vel, int maxSteps, double escapeR, double stepScale,
             bool recordPath) {
    Result res;
    res.q = conservedQuantities(pos, vel);
    const double h = res.q.h;
    const Vec3 dir0 = m3::normalize(vel);

    if (recordPath) res.path.reserve(512);

    for (int i = 0; i < maxSteps; ++i) {
        double r = m3::length(pos);
        if (recordPath) res.path.push_back({pos, vel});

        if (r < bh::kRs * 1.0005) {
            res.captured = true;
            res.steps = i;
            res.finalDir = m3::normalize(vel);
            break;
        }
        if (r > escapeR && m3::dot(pos, vel) > 0.0) {
            res.escaped = true;
            res.steps = i;
            res.finalDir = m3::normalize(vel);
            break;
        }

        double dl = stepSize(r, h, stepScale);

        Vec3 p0 = pos, v0 = vel;
        Vec3 a0 = accel(p0, h);
        Vec3 p1 = p0 + v0 * (0.5 * dl), v1 = v0 + a0 * (0.5 * dl);
        Vec3 a1 = accel(p1, h);
        Vec3 p2 = p0 + v1 * (0.5 * dl), v2 = v0 + a1 * (0.5 * dl);
        Vec3 a2 = accel(p2, h);
        Vec3 p3 = p0 + v2 * dl, v3 = v0 + a2 * dl;
        Vec3 a3 = accel(p3, h);

        pos = p0 + (v0 + v1 * 2.0 + v2 * 2.0 + v3) * (dl / 6.0);
        vel = v0 + (a0 + a1 * 2.0 + a2 * 2.0 + a3) * (dl / 6.0);
        res.steps = i + 1;
    }

    if (!res.captured && !res.escaped) {
        res.finalDir = m3::normalize(vel);
    }
    double c = m3::clampf(m3::dot(dir0, res.finalDir), -1.0f, 1.0f);
    res.deflection = std::acos(c);
    return res;
}

Result traceFromObserver(Vec3 pos, Vec3 localDir, int maxSteps, double escapeR,
                         double stepScale, bool recordPath) {
    Vec3 vel = localToCoordinateVelocity(pos, m3::normalize(localDir));
    return trace(pos, vel, maxSteps, escapeR, stepScale, recordPath);
}

namespace {

inline Vec3 accelMassive(Vec3 p, double h) {
    double r2 = m3::dot(p, p);
    double r = std::sqrt(r2);
    double k = -1.0 / (r2 * r) - 3.0 * h * h / (r2 * r2 * r);
    return p * k;
}

}  // namespace

Result traceMassive(Vec3 pos, Vec3 vel, int maxSteps, double escapeR,
                    double stepScale, bool recordPath) {
    Result res;
    res.q = conservedQuantities(pos, vel);
    const double h = res.q.h;
    const Vec3 dir0 = m3::normalize(vel);
    if (recordPath) res.path.reserve(512);

    for (int i = 0; i < maxSteps; ++i) {
        double r = m3::length(pos);
        if (recordPath) res.path.push_back({pos, vel});

        if (r < bh::kRs * 1.0005) {
            res.captured = true;
            res.steps = i;
            res.finalDir = m3::normalize(vel);
            break;
        }
        if (r > escapeR && m3::dot(pos, vel) > 0.0) {
            res.escaped = true;
            res.steps = i;
            res.finalDir = m3::normalize(vel);
            break;
        }

        double dl = stepSize(r, h, stepScale);

        Vec3 p0 = pos, v0 = vel;
        Vec3 a0 = accelMassive(p0, h);
        Vec3 p1 = p0 + v0 * (0.5 * dl), v1 = v0 + a0 * (0.5 * dl);
        Vec3 a1 = accelMassive(p1, h);
        Vec3 p2 = p0 + v1 * (0.5 * dl), v2 = v0 + a1 * (0.5 * dl);
        Vec3 a2 = accelMassive(p2, h);
        Vec3 p3 = p0 + v2 * dl, v3 = v0 + a2 * dl;
        Vec3 a3 = accelMassive(p3, h);

        pos = p0 + (v0 + v1 * 2.0 + v2 * 2.0 + v3) * (dl / 6.0);
        vel = v0 + (a0 + a1 * 2.0 + a2 * 2.0 + a3) * (dl / 6.0);
        res.steps = i + 1;
    }

    if (!res.captured && !res.escaped) res.finalDir = m3::normalize(vel);
    double c = m3::clampf(m3::dot(dir0, res.finalDir), -1.0f, 1.0f);
    res.deflection = std::acos(c);
    return res;
}

}  // namespace geo
