// scene.cpp — derived physics tables and the per-frame uniform block (camera frame computed in double).
#include "scene.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "colorimetry.h"
#include "kerr_cpu.h"

static constexpr double DEG = M_PI / 180.0;

bool updateScene(const AppState& s, SceneCache& c) {
    bool changed = false;
    if (c.lut.empty()) {
        c.lut = color::buildBlackbodyLUT(1024, c.lutLogT0, c.lutLogT1);
        changed = true;
    }
    if (c.spin != s.spin || c.rDiskOut != s.rDiskOut) {
        c.spin = s.spin;
        c.rDiskOut = s.rDiskOut;
        c.disk = phys::buildDisk(s.spin, s.rDiskOut, 1024);
        c.funnel = phys::buildFunnel(s.spin, c.funnelR1, c.funnelTop, c.funnelScale, 512);
        double a = s.spin;
        double radii[BH_NUM_SPECIAL] = {phys::horizon(a), 2.0, phys::photonOrbit(a, 1), phys::photonOrbit(a, -1),
                                        phys::isco(a)};
        for (int i = 0; i < BH_NUM_SPECIAL; ++i) {
            c.special[i] = float(radii[i]);
            c.specialFunnel[i] = float(phys::equatorialCircumferentialRadius(a, radii[i]));
        }
        changed = true;
    }
    if (changed) c.version++;
    return changed;
}

static simd_float4 lutAt(const SceneCache& c, double T) {
    int n = int(c.lut.size());
    double x = (std::log10(T) - c.lutLogT0) / (c.lutLogT1 - c.lutLogT0) * (n - 1);
    x = std::clamp(x, 0.0, double(n - 1) - 1e-6);
    int i = int(x);
    float f = float(x - i);
    return c.lut[i] * (1.0f - f) + c.lut[i + 1] * f;
}

double orbitVelocity(double a, double r) {
    double a2 = a * a, delta = r * r - 2 * r + a2;
    if (delta <= 0) return 2.0;
    double x = std::sqrt(r);
    if (r * x - 3 * x + 2 * a <= 0) return 2.0;   // inside the photon orbit: no circular orbit
    double aeq = (r * r + a2) * (r * r + a2) - a2 * delta;
    double om = 2 * a * r / aeq, varpi = std::sqrt(aeq) / r, alpha = r * std::sqrt(delta / aeq);
    double Om = 1.0 / (r * x + a);
    return (Om - om) * varpi / alpha;
}

static simd_float3 f3(const V3<double>& v) { return simd_make_float3(float(v.x), float(v.y), float(v.z)); }

FrameUniforms buildUniforms(const AppState& s, const SceneCache& c, int W, int H, simd_float2 jitter, CameraInfo* info) {
    FrameUniforms U;
    std::memset(&U, 0, sizeof U);
    const double a = s.spin;
    const bool gr = s.gr;
    const double aRay = gr ? a : 0.0, mRay = gr ? 1.0 : 0.0;
    const double r = s.camR, th = s.camTheta * DEG, ph = s.camPhi * DEG;

    kerr64::CamFrame cf;
    kerr64::kerr_frame_metric(aRay, mRay, r, std::sin(th), std::cos(th), cf);

    // camera axes on the ZAMO triad (e_r, e_θ, e_φ): look at the hole, north up, then free look
    V3<double> f0(-1, 0, 0), u0(0, -1, 0), r0(0, 0, 1);
    double y = s.yaw * DEG, p = s.pitch * DEG;
    V3<double> f1 = f0 * std::cos(y) + r0 * std::sin(y), r1 = r0 * std::cos(y) - f0 * std::sin(y);
    V3<double> f2 = f1 * std::cos(p) + u0 * std::sin(p), u2 = u0 * std::cos(p) - f1 * std::sin(p);

    // observer's velocity relative to the ZAMO
    V3<double> beta(0, 0, 0);
    double v = 0;
    if (gr && s.motion == MOTION_ORBIT) {
        v = std::min(orbitVelocity(a, r), 0.99);
        beta = V3<double>(0, 0, v);
    } else if (gr && s.motion == MOTION_INFALL) {
        // free fall from rest at infinity with zero angular momentum: E_ZAMO = 1/α ⇒ v = sqrt(1 − α²), inward
        v = std::sqrt(std::max(0.0, 1.0 - cf.alpha * cf.alpha));
        v = std::min(v, 0.999);
        beta = V3<double>(-v, 0, 0);
    }
    double gamma = 1.0 / std::sqrt(1.0 - v * v);

    U.camRight = f3(r1);
    U.camUp = f3(u2);
    U.camFwd = f3(f2);
    U.camBeta = f3(beta);
    U.resolution = simd_make_float2(float(W), float(H));
    U.jitter = jitter;
    double tanH = std::tan(0.5 * s.fovDeg * DEG);
    U.tanHalfFov = float(tanH);
    U.aspect = float(double(W) / H);
    U.pixelAngle = float(2.0 * tanH / H);
    U.simTime = float(s.simTime);
    U.camR = float(r);
    U.camSinT = float(std::sin(th));
    U.camCosT = float(std::cos(th));
    U.camSinP = float(std::sin(ph));
    U.camCosP = float(std::cos(ph));
    U.camGamma = float(gamma);
    U.camSigma = float(cf.Sigma);
    U.camDelta = float(cf.Delta);
    U.camA = float(cf.Akerr);
    U.camAlpha = float(cf.alpha);
    U.camOmega = float(cf.omega);
    U.camVarpi = float(cf.varpi);

    double rp = phys::horizon(a);
    U.spin = float(aRay);
    U.mass = float(mRay);
    U.rPlus = float(rp);
    U.rIsco = float(phys::isco(a));
    U.rhoHorizon = float(gr ? 1.0 / (rp + 1e-4) : 1.0 / rp);
    U.rhoCapture = gr ? float(1.0 / phys::photonOrbit(a, 1)) : 1e30f;
    U.diskSpin = float(a);
    U.rDiskOut = float(c.disk.rout);
    U.diskRin = float(c.disk.rin);
    U.diskTpeak = float(s.Tpeak);
    U.diskOpacity = s.gridMode == BH_GRID_FUNNEL ? 0.55f : 1.0f;
    U.turbAmp = float(s.turbAmp);
    double exposure = std::pow(2.0, s.exposureEV);
    U.exposure = float(exposure);
    U.diskExposure = float(exposure / std::max(double(lutAt(c, s.Tpeak).w), 1e-30));
    U.skyGain = 1.0f;
    U.gridGain = 0.55f;
    U.stepAng = float(0.1 * s.stepScale);
    U.stepRho = float(0.05 * s.stepScale);
    U.stepPhi = float(0.1 * s.stepScale);
    U.gridRmax = 30.0f;
    U.funnelR0 = float(c.funnel.R0);
    U.funnelR1 = float(c.funnel.R1);
    double zmin = c.funnel.Z.empty() ? 0.0 : double(c.funnel.Z.front());
    double reach = std::sqrt(c.funnel.R1 * c.funnel.R1 + zmin * zmin) * 1.02 + 1.0;
    U.funnelRho = s.gridMode == BH_GRID_FUNNEL ? float(1.0 / reach) : 0.0f;
    U.funnelSurface = 1.0f;
    U.lutLogT0 = float(c.lutLogT0);
    U.lutLogT1 = float(c.lutLogT1);
    for (int i = 0; i < BH_NUM_SPECIAL; ++i) {
        U.specialPlane[i] = c.special[i];
        U.specialFunnel[i] = c.specialFunnel[i];
    }
    int flags = 0;
    if (s.disk) flags |= BH_FLAG_DISK;
    if (gr) flags |= BH_FLAG_GR;
    if (s.redshift) flags |= BH_FLAG_REDSHIFT;
    if (s.turbulence) flags |= BH_FLAG_TURBULENCE;
    if (s.limb) flags |= BH_FLAG_LIMB;
    if (s.orderViz) flags |= BH_FLAG_ORDER;
    if (s.shadowOverlay) flags |= BH_FLAG_SHADOW;
    if (s.sky) flags |= BH_FLAG_SKY;
    U.flags = flags;
    U.gridMode = s.gridMode;
    U.maxSteps = s.maxSteps;
    U.fluxCount = int(c.disk.flux.size());
    U.lutCount = int(c.lut.size());
    U.funnelCount = int(c.funnel.Z.size());

    if (info) {
        kerr64::CamFrame tf;   // true metric at the camera
        kerr64::kerr_frame_metric(a, 1.0, r, std::sin(th), std::cos(th), tf);
        info->alpha = tf.alpha;
        info->omega = tf.omega;
        info->velocity = v;
        info->gamma = gamma;
        info->redshiftSky = gr ? 1.0 / tf.alpha : 1.0;
        info->insideErgosphere = r < 1.0 + std::sqrt(std::max(0.0, 1.0 - a * a * std::cos(th) * std::cos(th)));
    }
    return U;
}

static std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
static std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

std::string hudText(const AppState& s, const SceneCache& c, const CameraInfo& ci, double fps, double gpuMs, int w,
                    int h, int accum) {
    double a = s.spin;
    std::string t;
    t += fmt("KERR BLACK HOLE   a = %+.3f M%s\n", a, a < 0 ? "  (retrograde disk)" : "");
    t += fmt("  horizon r+ = %.3f M   ISCO = %.3f M   photon orbits %.3f / %.3f M\n", phys::horizon(a), phys::isco(a),
             phys::photonOrbit(a, 1), phys::photonOrbit(a, -1));
    const char* motion = s.plunging ? "free fall (plunging!)"
                       : s.motion == MOTION_ORBIT ? "circular orbit"
                       : s.motion == MOTION_INFALL ? "radial free fall"
                                                   : "hovering (ZAMO)";
    t += fmt("CAMERA  r = %.2f M   inclination %.1f°   φ %.0f°   fov %.0f°\n", s.camR, s.camTheta,
             std::fmod(std::fmod(s.camPhi, 360.0) + 360.0, 360.0), s.fovDeg);
    t += fmt("  observer: %s   v = %.3fc   lapse α = %.3f   sky blueshift ×%.3f%s\n", motion, ci.velocity, ci.alpha,
             ci.redshiftSky, ci.insideErgosphere ? "   [inside ergosphere]" : "");
    double mdot = phys::mdotForPeakTemperature(a, s.massSolar, s.Tpeak, c.disk.fluxMax);
    double tM = phys::gravitationalTimeSeconds(s.massSolar);
    t += fmt("DISK  Novikov–Thorne  T_peak = %.0f K (rest frame, at r = %.2f M)\n", s.Tpeak, c.disk.rPeak);
    t += fmt("  ⇔ ṁ = %.2e Ṁ_Edd for M = %.2g M☉ (η = %.3f);  1 M of time = %.3g s\n", mdot, s.massSolar,
             phys::radiativeEfficiency(a), tM);
    static const char* gridName[] = {"off", "embedding (trapdoor)", "equatorial"};
    static const char* tmName[] = {"asinh", "ACES", "linear"};
    t += fmt("VIEW  lensing %s  redshift %s  turbulence %s  grid %s  exposure %+.2f EV  %s\n", s.gr ? "on" : "OFF",
             s.redshift ? "on" : "OFF", s.turbulence ? "on" : "off", gridName[s.gridMode], s.exposureEV,
             tmName[s.tonemap]);
    t += fmt("  t = %.1f M  (%s, ×%.3g M/s)\n", s.simTime, s.paused ? "paused" : "running", s.timeScale);
    t += fmt("PERF  %.0f fps   GPU %.1f ms   %d×%d%s\n", fps, gpuMs, w, h,
             accum > 1 ? fmt("   accumulated %d spp", accum).c_str() : "");
    t += "[H] help";
    return t;
}

std::string helpText() {
    return "MOUSE   drag: orbit    right/⌥-drag: look around    scroll / pinch: distance\n"
           "[ ]     spin −/+ 0.05  ({ } fine)           ↑ ↓  disk temperature   ← →  time speed\n"
           "G       spacetime grid: off / embedding funnel ('trapdoor') / equatorial coordinate grid\n"
           "L       lensing (GR) on/off     Z  Doppler + gravitational redshift     D  disk\n"
           "T       disk turbulence         K  limb darkening      Y  stars         B  bloom\n"
           "O       colour disk images by order (n = 0, 1, 2+)    C  analytic shadow edge (Bardeen)\n"
           "V       observer: hovering / circular orbit / radial free fall      F  plunge into the hole\n"
           "A       auto-orbit camera       SPACE pause time      - =  exposure    M  tone map\n"
           ", .     field of view           1–6  camera presets     S  screenshot    X  reset    Q  quit";
}
