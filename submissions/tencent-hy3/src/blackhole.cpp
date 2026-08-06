#include "blackhole.h"
#include <cmath>

namespace bh {

glm::vec3 photonAccel(const glm::vec3& pos, const glm::vec3& vel, float h2) {
    // Schwarzschild geometry, Cartesian form.
    // r = |pos|, with rs = 2M.
    const float r = glm::length(pos);
    const float r2 = r * r;
    const float rs = RS;

    // Factor appearing in the metric derivatives.
    // For null geodesics the acceleration (in these coordinates) is:
    //   a = - (rs / (2 r^2 (r - rs))) * [ (r - rs) * (vel . rhat) * vel
    //                                     + (rs/2 - r) * (vel . rhat)^2 * rhat
    //                                     + h2 / r^3 * rhat ]
    // which is the exact Cartesian geodesic equation for photons.
    const glm::vec3 rhat = pos / r;
    const float vr = glm::dot(vel, rhat);
    const float vr2 = vr * vr;

    const float f = -rs / (2.0f * r2 * (r - rs));
    const float term1 = (r - rs) * vr;
    const float term2 = (rs * 0.5f - r) * vr2 + h2 / (r * r * r);

    return f * (term1 * vel + term2 * rhat);
}

// Blackbody-ish temperature gradient for the disk: hotter inside.
static glm::vec3 diskEmission(float radius, const Disk& disk) {
    // Normalized radial position (0 at inner, 1 at outer).
    const float t = (radius - disk.inner) / (disk.outer - disk.inner);
    const float tt = glm::clamp(t, 0.0f, 1.0f);
    // Temperature ~ r^{-3/4} (standard thin-disk law), brighter inside.
    const float temp = std::pow(1.0f - tt, 0.75f) + 0.05f;
    // Map temperature to a warm->white->blue palette.
    glm::vec3 hot(1.0f, 0.95f, 0.85f);
    glm::vec3 cool(1.0f, 0.45f, 0.12f);
    glm::vec3 col = glm::mix(hot, cool, tt);
    return col * temp * temp;
}

RayResult traceRay(const glm::vec3& ro, const glm::vec3& rd,
                    const Disk& disk, int maxSteps, float stepScale) {
    RayResult res;
    res.color = glm::vec3(0.0f);
    res.hitDisk = false;
    res.captured = false;
    res.diskT = -1.0f;

    glm::vec3 pos = ro;
    glm::vec3 vel = rd; // direction == d(pos)/d(lambda)

    // Conserved squared angular momentum of the photon.
    const glm::vec3 L = glm::cross(pos, vel);
    const float h2 = glm::dot(L, L);

    bool diskCrossed = false;
    float prevY = pos.y;
    glm::vec3 prevPos = pos;

    for (int i = 0; i < maxSteps; ++i) {
        const float r = glm::length(pos);

        // Adaptive step: small near the hole, large far away.
        const float step = stepScale * glm::max(r * 0.05f, RS * 0.15f);

        // RK4 integration of the affine-parameter ODE for stability.
        auto deriv = [&](const glm::vec3& p, const glm::vec3& v) {
            return photonAccel(p, v, h2);
        };

        const glm::vec3 k1v = deriv(pos, vel);
        const glm::vec3 k1x = vel;
        const glm::vec3 k2v = deriv(pos + 0.5f * step * k1x, vel + 0.5f * step * k1v);
        const glm::vec3 k2x = vel + 0.5f * step * k1v;
        const glm::vec3 k3v = deriv(pos + 0.5f * step * k2x, vel + 0.5f * step * k2v);
        const glm::vec3 k3x = vel + 0.5f * step * k2v;
        const glm::vec3 k4v = deriv(pos + step * k3x, vel + step * k3v);
        const glm::vec3 k4x = vel + step * k3v;

        prevPos = pos;
        prevY = pos.y;
        pos += (step / 6.0f) * (k1x + 2.0f * k2x + 2.0f * k3x + k4x);
        vel += (step / 6.0f) * (k1v + 2.0f * k2v + 2.0f * k3v + k4v);

        // Captured by the horizon?
        if (glm::length(pos) < RS * 1.001f) {
            res.captured = true;
            return res;
        }

        // Accretion disk lives in the equatorial plane y in [-th, +th].
        // Detect a crossing of y=0 between prevY and current y within the
        // radial band of the disk.
        if ((prevY < 0.0f && pos.y >= 0.0f) || (prevY > 0.0f && pos.y <= 0.0f)) {
            const float rad = glm::length(glm::vec3(pos.x, 0.0f, pos.z));
            if (rad >= disk.inner && rad <= disk.outer && !diskCrossed) {
                diskCrossed = true;
                res.hitDisk = true;
                res.diskT = glm::length(pos - ro);
                // Relativistic beaming: the side rotating toward the camera
                // (orbital velocity along +x for prograde disk) is brighter.
                glm::vec3 orbDir = glm::normalize(glm::vec3(-pos.z, 0.0f, pos.x));
                const float beam = glm::dot(glm::normalize(vel), orbDir);
                const float doppler = 1.0f / (1.0f - 0.45f * beam); // boost factor
                glm::vec3 emis = diskEmission(rad, disk);
                emis *= doppler * doppler; // intensity ~ doppler^4 but clamp
                res.color += emis;
                // Continue so the ray can also pick up lensed background
                // (we keep disk as emissive, additive).
            }
        }

        // Escape to infinity: sample a starfield background.
        if (glm::length(pos) > 60.0f) {
            const glm::vec3 dir = glm::normalize(vel);
            // Simple procedural starfield + faint Milky-Way band.
            const float b = 0.02f;
            float stars = 0.0f;
            const glm::vec3 sd = dir * 120.0f;
            const float n = std::fmod(std::sin(sd.x * 12.9f + sd.y * 78.2f) *
                                         43758.5453f, 1.0f);
            if (n > 0.995f) stars = 1.0f;
            // galactic band brightness near equator (y~0)
            const float band = 0.06f * std::exp(-dir.y * dir.y * 8.0f);
            res.color += glm::vec3(b + band) + glm::vec3(stars);
            return res;
        }
    }
    return res;
}

} // namespace bh
