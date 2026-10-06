#include "raytracer.h"

RayTracer::RayTracer(BlackHole* blackhole) : bh(blackhole) {
    criticalAngle = std::asin(bh->schwarzschildRadius / (bh->photonSphere * 1.1));
    generateStars();
}

void RayTracer::generateStars() {
    RNG rng;
    for (int i = 0; i < 3000; i++) {
        double theta = rng() * 2.0 * M_PI;
        double phi = std::acos(2.0 * rng() - 1.0);
        double r = starScale * (1.0 + rng() * rng() * 5.0);
        stars.emplace_back(
            r * std::sin(phi) * std::cos(theta),
            r * std::sin(phi) * std::sin(theta),
            r * std::cos(phi)
        );
        double temp = rng();
        if (temp < 0.1) {
            starColors.emplace_back(1.0, 0.8, 0.6);
        } else if (temp < 0.3) {
            starColors.emplace_back(1.0, 1.0, 0.9);
        } else if (temp < 0.6) {
            starColors.emplace_back(0.7, 0.8, 1.0);
        } else {
            starColors.emplace_back(1.0, 1.0, 1.0);
        }
    }
}

Vec3 RayTracer::trace(const Ray& ray, int depth) {
    if (depth > maxBounces) return Vec3(0, 0, 0);

    Vec3 color(0, 0, 0);
    Vec3 throughput(1, 1, 1);
    Ray currentRay = ray;

    for (int bounce = 0; bounce < maxBounces - depth; bounce++) {
        double minT = 1e30;
        bool hitStar = false;
        Vec3 hitNormal;
        Vec3 hitColor;
        double hitEmission = 0;
        Vec3 hitPoint;

        double bhDist = (bh->position - currentRay.origin).length();

        if (bhDist < bh->schwarzschildRadius * 2.0) {
            Vec3 rayToBH = bh->position - currentRay.origin;
            double closestApproach = rayToBH.cross(currentRay.dir).length();
            if (closestApproach < bh->photonSphere * 1.5) {
                bool captured = traceGravityLensing(currentRay, 200, bhDist);
                if (captured) {
                    double brightness = std::max(0.0, 1.0 - closestApproach / (bh->photonSphere * 1.5));
                    brightness = brightness * brightness * 50.0;
                    return color + throughput * Vec3(brightness * 2.0, brightness * 1.5, brightness * 0.5);
                }
            }
        }

        for (size_t i = 0; i < stars.size(); i++) {
            Vec3 toStar = stars[i] - currentRay.origin;
            double t = toStar.dot(currentRay.dir);
            if (t < minT) {
                Vec3 closest = currentRay.origin + currentRay.dir * t;
                double dist = (closest - stars[i]).length();
                if (dist < starScale * 0.002 && t > 0.1 && t < minT) {
                    minT = t;
                    hitStar = true;
                    hitNormal = (stars[i] - closest).normalized();
                    hitColor = starColors[i];
                    hitEmission = 1.0;
                    hitPoint = closest;
                }
            }
        }

        if (hitStar) {
            color = color + throughput * hitColor * hitEmission;
            return color;
        }

        for (int s = 1; s <= maxSteps; s++) {
            double t = (double)s * stepSize;
            Vec3 pt = currentRay.origin + currentRay.dir * t;
            Vec3 acc = bh->gravityAcceleration(pt);
            Vec3 newDir = (currentRay.dir + acc * (stepSize * stepSize * 0.5)).normalized();
            currentRay.dir = newDir;
            if (bh->isInsideEventHorizon(pt)) {
                double brightness = 1.0 - (t / (bhDist * 2.0));
                brightness = std::max(0.0, brightness);
                double tCapture = bhDist - t;
                if (tCapture > 0) {
                    Vec3 accel = bh->gravityAcceleration(pt);
                    double gravBright = accel.length() * bh->schwarzschildRadius * 10.0;
                    gravBright = std::min(1.0, gravBright);
                    double r = std::sin(t * 0.3) * 0.5 + 0.5;
                    double g = std::sin(t * 0.2 + 2.0) * 0.3 + 0.3;
                    double b = std::sin(t * 0.1 + 4.0) * 0.2 + 0.1;
                    return color + throughput * Vec3(r * gravBright * 2.0, g * gravBright, b * gravBright * 0.5);
                }
                return color;
            }
            if (t > bhDist * 3.0) break;
        }

        color = color + throughput * Vec3(0.001, 0.001, 0.003);
        return color;
    }

    return color;
}

bool RayTracer::traceGravityLensing(const Ray& ray, int steps, double bhDist) {
    Vec3 pos = ray.origin;
    Vec3 dir = ray.dir;
    double dt = bhDist / (double)steps;

    for (int i = 0; i < steps; i++) {
        pos = pos + dir * dt;
        Vec3 r = pos - bh->position;
        double dist = r.length();

        if (dist < bh->schwarzschildRadius) return true;

        Vec3 acc = bh->gravityAcceleration(pos);
        dir = dir + acc * dt * dt;
        dir = dir.normalized();

        if (dist > bhDist * 5.0) return false;
    }
    return false;
}

Vec3 RayTracer::traceWithLensing(const Ray& ray) {
    Vec3 color(0, 0, 0);
    Vec3 throughput(1, 1, 1);
    Ray currentRay = ray;
    Vec3 prevPoint = ray.origin;

    for (int step = 0; step < 600; step++) {
        Vec3 pos = currentRay.origin + currentRay.dir * stepSize;
        double dt = stepSize;

        Vec3 r = pos - bh->position;
        double dist = r.length();

        if (dist < bh->schwarzschildRadius) {
            return color + throughput * Vec3(0, 0, 0);
        }

        Vec3 acc = bh->gravityAcceleration(pos);
        Vec3 correctedDir = (currentRay.dir + acc * (dt * dt)).normalized();

        currentRay.origin = prevPoint;
        currentRay.dir = correctedDir;
        prevPoint = pos;

        double gravFactor = 1.0 / (dist * dist + 1.0);
        Vec3 accretionContrib = sampleAccretionDisk(pos, correctedDir, dist);
        color = color + throughput * accretionContrib * gravFactor * dt * 2.0;

        if (dist > 50.0) {
            Ray finalRay(pos, correctedDir);
            for (size_t i = 0; i < stars.size(); i++) {
                Vec3 toStar = stars[i] - finalRay.origin;
                double t = toStar.dot(finalRay.dir);
                if (t > 0) {
                    Vec3 closest = finalRay.origin + finalRay.dir * t;
                    double d = (closest - stars[i]).length();
                    if (d < starScale * 0.003) {
                        color = color + throughput * starColors[i] * (1.0 / (1.0 + d * d * 100.0)) * 2.0;
                    }
                }
            }
            break;
        }
    }

    return color;
}

Vec3 RayTracer::sampleAccretionDisk(const Vec3& pos, const Vec3&, double) {
    Vec3 rel = pos - bh->position;
    Vec3 diskNormal = Vec3(0, 1, 0);
    double heightAboveDisk = rel.dot(diskNormal);
    Vec3 diskPos = rel - diskNormal * heightAboveDisk;
    double radialDist = diskPos.length();

    if (radialDist < bh->accretionInnerRadius * 0.3 || radialDist > bh->accretionOuterRadius) {
        return Vec3(0, 0, 0);
    }

    if (std::abs(heightAboveDisk) > 0.5) return Vec3(0, 0, 0);

    double normalizedR = (radialDist - bh->accretionInnerRadius) /
                         (bh->accretionOuterRadius - bh->accretionInnerRadius);

    double temperature = 1.0 / std::sqrt(std::max(0.1, normalizedR));
    temperature = std::clamp(temperature, 0.0, 1.0);

    Vec3 hotColor = Vec3(
        1.0,
        temperature * 0.5 + 0.1,
        temperature * temperature * 0.1
    );

    double brightness = std::exp(-std::abs(heightAboveDisk) * 2.0) *
                       std::exp(-std::pow(normalizedR - 0.3, 2.0) * 10.0) *
                       (1.0 / (radialDist * 0.5 + 1.0));

    return hotColor * brightness * 5.0;
}