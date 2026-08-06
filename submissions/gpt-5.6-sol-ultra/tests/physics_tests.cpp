#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

constexpr double kHorizon = 2.0;
constexpr double kPhotonSphere = 3.0;
constexpr double kISCO = 6.0;
constexpr double kCriticalImpact = 3.0 * 1.7320508075688772935;

struct State {
    double r;
    double radialVelocity;
    double phi;
};

struct Derivative {
    double r;
    double radialVelocity;
    double phi;
};

Derivative derivative(const State& state, double impactParameter) {
    const double r = state.r;
    return {
        state.radialVelocity,
        impactParameter * impactParameter / (r * r * r) * (1.0 - 3.0 / r),
        impactParameter / (r * r),
    };
}

State add(const State& state, const Derivative& derivativeValue, double scale) {
    return {
        state.r + scale * derivativeValue.r,
        state.radialVelocity + scale * derivativeValue.radialVelocity,
        state.phi + scale * derivativeValue.phi,
    };
}

State rk4(const State& state, double step, double impactParameter) {
    const Derivative k1 = derivative(state, impactParameter);
    const Derivative k2 = derivative(add(state, k1, 0.5 * step), impactParameter);
    const Derivative k3 = derivative(add(state, k2, 0.5 * step), impactParameter);
    const Derivative k4 = derivative(add(state, k3, step), impactParameter);
    return {
        state.r + step * (k1.r + 2.0 * k2.r + 2.0 * k3.r + k4.r) / 6.0,
        state.radialVelocity
            + step * (k1.radialVelocity + 2.0 * k2.radialVelocity
                    + 2.0 * k3.radialVelocity + k4.radialVelocity) / 6.0,
        state.phi + step * (k1.phi + 2.0 * k2.phi
                          + 2.0 * k3.phi + k4.phi) / 6.0,
    };
}

double invariant(const State& state, double impactParameter) {
    const double f = 1.0 - 2.0 / state.r;
    return state.radialVelocity * state.radialVelocity
         + f * impactParameter * impactParameter / (state.r * state.r);
}

struct TraceResult {
    bool captured = false;
    bool escaped = false;
    State finalState{};
    double maximumInvariantError = 0.0;
};

TraceResult trace(double impactParameter, double observerRadius) {
    const double f0 = 1.0 - 2.0 / observerRadius;
    State state{
        observerRadius,
        -std::sqrt(1.0 - f0 * impactParameter * impactParameter
                         / (observerRadius * observerRadius)),
        0.0,
    };

    TraceResult result;
    for (int i = 0; i < 500000; ++i) {
        if (state.r <= kHorizon + 1.0e-5) {
            result.captured = true;
            break;
        }
        if (i > 3 && state.r >= observerRadius && state.radialVelocity > 0.0) {
            result.escaped = true;
            break;
        }
        const double radialStep = 0.0075 * state.r;
        const double angularStep = 0.008 * state.r * state.r
                                 / std::max(impactParameter, 0.1);
        const double step = std::clamp(std::min(radialStep, angularStep),
                                       1.0e-4, 2.0);
        state = rk4(state, step, impactParameter);
        if (state.r > kHorizon + 1.0e-4) {
            result.maximumInvariantError = std::max(
                result.maximumInvariantError,
                std::abs(invariant(state, impactParameter) - 1.0));
        }
        if (!std::isfinite(state.r) || !std::isfinite(state.radialVelocity)
            || !std::isfinite(state.phi)) {
            throw std::runtime_error("non-finite geodesic state");
        }
    }
    result.finalState = state;
    return result;
}

TraceResult traceRealtimePreset(double impactParameter, double stepScale,
                                int maximumSteps) {
    constexpr double observerRadius = 32.0;
    constexpr double skyRadius = 66.0;
    const double f0 = 1.0 - 2.0 / observerRadius;
    State state{
        observerRadius,
        -std::sqrt(1.0 - f0 * impactParameter * impactParameter
                         / (observerRadius * observerRadius)),
        0.0,
    };

    TraceResult result;
    for (int i = 0; i < maximumSteps; ++i) {
        if (state.r <= kHorizon + 0.0025) {
            result.captured = true;
            break;
        }
        if (i > 2 && state.r >= skyRadius && state.radialVelocity > 0.0) {
            result.escaped = true;
            break;
        }

        const double radialStep = stepScale * state.r;
        const double angularStep = 0.045 * state.r * state.r
                                 / std::max(impactParameter, 0.20);
        const double step = std::clamp(std::min(radialStep, angularStep),
                                       0.012, 0.90);
        state = rk4(state, step, impactParameter);

        // Match the realtime shader's null-first-integral projection.
        if (state.r > kHorizon + 0.01
            && std::abs(state.radialVelocity) > 0.002) {
            const double f = 1.0 - 2.0 / state.r;
            const double expected = std::sqrt(std::max(
                0.0, 1.0 - f * impactParameter * impactParameter
                              / (state.r * state.r)));
            state.radialVelocity = std::copysign(expected,
                                                 state.radialVelocity);
        }
    }
    result.finalState = state;
    return result;
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void requireNear(double actual, double expected, double tolerance,
                 const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + " (actual=" + std::to_string(actual)
                                 + ", expected=" + std::to_string(expected) + ")");
    }
}

void testCharacteristicRadii() {
    requireNear(kHorizon, 2.0, 0.0, "horizon must be 2M");
    requireNear(kPhotonSphere, 3.0, 0.0, "photon sphere must be 3M");
    requireNear(kISCO, 6.0, 0.0, "Schwarzschild ISCO must be 6M");
    requireNear(kCriticalImpact, 5.196152422706632, 1.0e-14,
                "critical impact parameter must be 3 sqrt(3) M");
}

void testCaptureBoundary() {
    const TraceResult below = trace(kCriticalImpact - 0.04, 100.0);
    const TraceResult above = trace(kCriticalImpact + 0.04, 100.0);
    require(below.captured && !below.escaped,
            "ray below critical impact parameter should be captured");
    require(above.escaped && !above.captured,
            "ray above critical impact parameter should escape");
    require(below.maximumInvariantError < 2.0e-5,
            "captured ray null invariant drift is too large");
    require(above.maximumInvariantError < 2.0e-5,
            "escaping ray null invariant drift is too large");
}

void testStaticObserverLaunchMapping() {
    constexpr double observerRadius = 32.0;
    constexpr double localAngle = 0.27;
    const double f0 = 1.0 - 2.0 / observerRadius;
    const double impactParameter = observerRadius * std::sin(localAngle)
                                 / std::sqrt(f0);
    const State launched{
        observerRadius,
        -std::cos(localAngle),
        0.0,
    };
    requireNear(invariant(launched, impactParameter), 1.0, 1.0e-14,
                "static-tetrad camera launch must satisfy the null constraint");
}

void testFiniteObserverShadowAngle() {
    constexpr double observerRadius = 32.0;
    const double f0 = 1.0 - 2.0 / observerRadius;
    const double sineOfShadowAngle = kCriticalImpact * std::sqrt(f0)
                                   / observerRadius;
    const double reconstructedImpact = observerRadius * sineOfShadowAngle
                                     / std::sqrt(f0);
    requireNear(reconstructedImpact, kCriticalImpact, 1.0e-14,
                "finite-observer shadow angle must map to the critical impact");
    require(sineOfShadowAngle > 0.0 && sineOfShadowAngle < 1.0,
            "default observer must have a physical finite shadow angle");
}

void testRealtimePresetBudgets() {
    const double nearCritical = kCriticalImpact + 1.0e-5;
    const TraceResult balanced = traceRealtimePreset(nearCritical, 0.034, 520);
    const TraceResult high = traceRealtimePreset(nearCritical, 0.023, 820);
    const TraceResult reference = traceRealtimePreset(nearCritical, 0.015, 1400);
    require(balanced.escaped,
            "Balanced preset must reach the sky for the regression ray");
    require(high.escaped,
            "High preset must not lose range when reducing the step size");
    require(reference.escaped,
            "Reference preset must not exhaust its budget before the sky");
}

void testWeakFieldDeflection() {
    constexpr double impactParameter = 100.0;
    constexpr double observerRadius = 10000.0;
    const TraceResult ray = trace(impactParameter, observerRadius);
    require(ray.escaped, "weak-field ray must escape");

    const double f0 = 1.0 - 2.0 / observerRadius;
    const double initialRadial = -std::sqrt(
        1.0 - f0 * impactParameter * impactParameter
              / (observerRadius * observerRadius));
    double initialX = initialRadial;
    double initialY = impactParameter / observerRadius;
    const double initialLength = std::hypot(initialX, initialY);
    initialX /= initialLength;
    initialY /= initialLength;

    const double phi = ray.finalState.phi;
    const double erX = std::cos(phi);
    const double erY = std::sin(phi);
    const double ephiX = -std::sin(phi);
    const double ephiY = std::cos(phi);
    double finalX = ray.finalState.radialVelocity * erX
                  + (impactParameter / ray.finalState.r) * ephiX;
    double finalY = ray.finalState.radialVelocity * erY
                  + (impactParameter / ray.finalState.r) * ephiY;
    const double finalLength = std::hypot(finalX, finalY);
    finalX /= finalLength;
    finalY /= finalLength;

    const double measured = std::acos(std::clamp(initialX * finalX
                                                + initialY * finalY, -1.0, 1.0));
    const double firstOrder = 4.0 / impactParameter;
    require(std::abs(measured - firstOrder) / firstOrder < 0.05,
            "weak-field deflection should approach 4M/b within 5 percent");
}

void testFlammEmbeddingMetric() {
    for (double r : {2.2, 3.0, 6.0, 12.0, 20.0}) {
        // z(r)=2 sqrt(2(r-2)); dz/dr=sqrt(2/(r-2)).
        const double derivativeZ = std::sqrt(2.0 / (r - 2.0));
        const double embeddedRadialMetric = 1.0 + derivativeZ * derivativeZ;
        const double schwarzschildRadialMetric = 1.0 / (1.0 - 2.0 / r);
        requireNear(embeddedRadialMetric, schwarzschildRadialMetric, 1.0e-12,
                    "Flamm surface must reproduce the spatial radial metric");
    }
}

void testStaticGravitationalRedshift() {
    constexpr double emitterRadius = 8.0;
    constexpr double observerRadius = 18.0;
    const double computed = std::sqrt((1.0 - 2.0 / emitterRadius)
                                     / (1.0 - 2.0 / observerRadius));
    const double expected = std::sqrt(0.75 / (8.0 / 9.0));
    requireNear(computed, expected, 1.0e-14,
                "static Schwarzschild gravitational shift is incorrect");
}

void testCircularDiskDopplerOrdering() {
    constexpr double emitterRadius = 8.0;
    constexpr double observerRadius = 32.0;
    constexpr double angularMomentumProjection = 3.0;
    const double observerLapse = std::sqrt(1.0 - 2.0 / observerRadius);
    const double uTimeComponent = 1.0 / std::sqrt(1.0 - 3.0 / emitterRadius);
    const double omega = std::pow(emitterRadius, -1.5);
    const auto shift = [&](double xi) {
        return 1.0 / (observerLapse * uTimeComponent * (1.0 - omega * xi));
    };
    const double approaching = shift(angularMomentumProjection);
    const double transverse = shift(0.0);
    const double receding = shift(-angularMomentumProjection);
    require(approaching > transverse && transverse > receding,
            "circular-disk Doppler ordering must brighten the approaching side");

    // Observer on +Z. A backward ray to a +X emitter has N=+Y, so the
    // future-directed photon has xi=-b and sees +Y-rotating gas recede.
    // The mirrored -X emitter has N=-Y, xi=+b, and approaches the observer.
    constexpr double planeNormalAtPositiveX = 1.0;
    constexpr double planeNormalAtNegativeX = -1.0;
    const double xiAtPositiveX = -angularMomentumProjection
                               * planeNormalAtPositiveX;
    const double xiAtNegativeX = -angularMomentumProjection
                               * planeNormalAtNegativeX;
    require(shift(xiAtNegativeX) > shift(xiAtPositiveX),
            "past-trace plane normal must map to the correct Doppler side");
}

} // namespace

int main() {
    try {
        testCharacteristicRadii();
        testCaptureBoundary();
        testStaticObserverLaunchMapping();
        testFiniteObserverShadowAngle();
        testRealtimePresetBudgets();
        testWeakFieldDeflection();
        testFlammEmbeddingMetric();
        testStaticGravitationalRedshift();
        testCircularDiskDopplerOrdering();
        std::cout << "All Schwarzschild physics tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Physics test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
