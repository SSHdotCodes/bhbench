#include <cmath>
#include <cstdint>
#include <algorithm>

// Browser-side relativistic dynamics core. Positions are in gravitational
// radii rg = GM/c^2, coordinate time is rg/c, and velocities are fractions of c.
// The exterior orbit integrator uses the Paczynski-Wiita potential, which
// reproduces the Schwarzschild ISCO at 6rg and gives the exact local circular
// speed v/c = 1/sqrt(r-2) for a non-spinning black hole.

extern "C" {

struct Body {
    double x, y, z;
    double vx, vy, vz;
    double massSolar;
    double radiusKm;
    double renderRadius;
    double temperature;
    double compactness;
    double age;
    double initialMassSolar;
    double baseRenderRadius;
    double tidalRadius;
    double disruption;
    double disruptionAge;
    double orbitalPhase;
    double circularizedFraction;
    double postCaptureAge;
    int type;
    int composition;
    int state;
    int active;
};

static constexpr int MAX_BODIES = 24;
static constexpr double PI = 3.14159265358979323846;
static Body bodies[MAX_BODIES];
static double blackHoleMass = 4.30e6;
static double accretedMass = 0.0;
static double diskDensity = 0.0;
static double diskTemperature = 0.0;
static double diskMassSolar = 0.0;
static double diskNormalX = 0.0;
static double diskNormalY = 0.0;
static double diskNormalZ = 1.0;
static double mergerEnergyJ = 0.0;
static int lastEvent = 0;
static uint32_t rngState = 0x8a5cd789u;

static double clampd(double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
static double length3(double x, double y, double z) { return std::sqrt(x*x + y*y + z*z); }
static double random01() {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return double(rngState & 0x00ffffffu) / double(0x01000000u);
}
static double rgKm() { return 1.4766250385 * blackHoleMass; }

static double tidalRadiusFor(const Body &b) {
    if (b.type == 3) return 0.0;
    double radius = (b.radiusKm / rgKm()) * std::cbrt(blackHoleMass / std::max(b.massSolar, 1.0e-12));
    if (b.type == 2) radius *= 0.38;
    return radius;
}

static void beginDisruption(Body &b) {
    if (b.state != 0) return;
    b.state = 1;
    b.disruption = std::max(b.disruption, 0.015);
    b.disruptionAge = 0.0;
    b.orbitalPhase = 0.0;
    b.circularizedFraction = 0.0;
    lastEvent = 2;
}

static void addDiskMaterial(const Body &b, double fraction) {
    if (fraction <= 0.0) return;
    const double addedMass = b.initialMassSolar * fraction;
    diskMassSolar += addedMass;
    diskDensity = clampd(diskDensity + fraction * 1.8, 0.0, 1.0);
    diskTemperature = std::max(diskTemperature,
        (b.type == 1 ? 1.2e7 : (b.type == 2 ? 2.2e7 : 4.0e6)) *
        std::pow(std::max(3.0, blackHoleMass) / 10.0, -0.25));

    double hx = b.y*b.vz - b.z*b.vy;
    double hy = b.z*b.vx - b.x*b.vz;
    double hz = b.x*b.vy - b.y*b.vx;
    const double hl = length3(hx, hy, hz);
    if (hl > 1.0e-8) {
        hx /= hl; hy /= hl; hz /= hl;
        const double oldMass = std::max(0.0, diskMassSolar - addedMass);
        const double nx = diskNormalX*oldMass + hx*addedMass;
        const double ny = diskNormalY*oldMass + hy*addedMass;
        const double nz = diskNormalZ*oldMass + hz*addedMass;
        const double nl = length3(nx, ny, nz);
        if (nl > 1.0e-8) { diskNormalX=nx/nl; diskNormalY=ny/nl; diskNormalZ=nz/nl; }
    }
}

static void beginDebrisCapture(Body &b) {
    b.state = 3;
    b.disruption = 1.0;
    b.postCaptureAge = 0.0;
    double r = length3(b.x, b.y, b.z);
    if (r < 1.0e-8) { b.x = 2.025; b.y = 0.0; b.z = 0.0; }
    else {
        const double k = 2.025 / r;
        b.x *= k; b.y *= k; b.z *= k;
    }
    lastEvent = 8;
}

static void finishDebrisCapture(Body &b) {
    const double diskOwned = b.initialMassSolar * b.circularizedFraction;
    const double captured = std::max(0.0, b.massSolar - diskOwned);
    blackHoleMass += captured;
    accretedMass += captured;
    b.active = 0;
    lastEvent = 4;
}

void sim_init(double massSolar) {
    blackHoleMass = clampd(massSolar, 3.0, 1.0e11);
    accretedMass = 0.0;
    diskDensity = 0.0;
    diskTemperature = 0.0;
    diskMassSolar = 0.0;
    diskNormalX = 0.0;
    diskNormalY = 0.0;
    diskNormalZ = 1.0;
    mergerEnergyJ = 0.0;
    lastEvent = 0;
    for (auto &b : bodies) b.active = 0;
}

void sim_set_black_hole_mass(double massSolar) {
    blackHoleMass = clampd(massSolar, 3.0, 1.0e11);
}

double sim_get_black_hole_mass() { return blackHoleMass; }

// type: 0 planet, 1 star, 2 neutron star, 3 black hole
int sim_spawn(int type, double massSolar, double radiusKm, int composition,
              double spawnR, double phase, double speedFactor, double inwardFactor) {
    int slot = -1;
    for (int i = 0; i < MAX_BODIES; ++i) if (!bodies[i].active) { slot = i; break; }
    if (slot < 0) return -1;

    Body &b = bodies[slot];
    b.type = int(clampd(type, 0, 3));
    b.massSolar = clampd(massSolar, 1.0e-9, 1.0e10);
    b.radiusKm = clampd(radiusKm, 0.001, 1.0e10);
    b.composition = composition;
    b.state = 0;
    b.active = 1;
    b.age = 0.0;
    b.temperature = b.type == 1 ? 5800.0 : (b.type == 2 ? 8.0e5 : (b.type == 0 ? 290.0 : 0.0));
    b.compactness = b.type == 3 ? 1.0 : (b.type == 2 ? 0.7 : 0.0);
    spawnR = clampd(spawnR, 4.1, 120.0);
    phase += (random01() - 0.5) * 0.035;
    b.x = std::cos(phase) * spawnR;
    b.y = std::sin(phase) * spawnR;
    b.z = (random01() - 0.5) * spawnR * 0.05;

    // Static-observer circular speed in Schwarzschild geometry.
    const double vc = 1.0 / std::sqrt(spawnR - 2.0);
    const double tx = -std::sin(phase), ty = std::cos(phase);
    const double rx = std::cos(phase), ry = std::sin(phase);
    b.vx = tx * vc * speedFactor - rx * vc * inwardFactor;
    b.vy = ty * vc * speedFactor - ry * vc * inwardFactor;
    b.vz = (random01() - 0.5) * vc * 0.06;

    const double physicalRg = rgKm();
    const double actualRadiusRg = b.radiusKm / physicalRg;
    const double minVisual = b.type == 0 ? 0.13 : (b.type == 1 ? 0.22 : (b.type == 2 ? 0.11 : 0.08));
    double visual = b.type == 3 ? clampd(2.0 * b.massSolar / blackHoleMass, 0.08, 1.7)
                                : clampd(std::pow(b.massSolar / blackHoleMass, 1.0/3.0) * 1.8, minVisual, 0.65);
    b.renderRadius = std::max(actualRadiusRg, visual);
    b.initialMassSolar = b.massSolar;
    b.baseRenderRadius = b.renderRadius;
    b.tidalRadius = tidalRadiusFor(b);
    b.disruption = 0.0;
    b.disruptionAge = 0.0;
    b.orbitalPhase = 0.0;
    b.circularizedFraction = 0.0;
    b.postCaptureAge = 0.0;
    lastEvent = 1;
    return slot;
}

// A screen-picked injection. The object begins on the observer-facing side of
// the system and is fired toward a world-space aim point. tangentBias adds an
// optional prograde/retrograde component without changing the picked target.
int sim_spawn_aimed(int type, double massSolar, double radiusKm, int composition,
                    double sx, double sy, double sz,
                    double tx, double ty, double tz,
                    double speedFactor, double tangentBias) {
    int slot = -1;
    for (int i = 0; i < MAX_BODIES; ++i) if (!bodies[i].active) { slot = i; break; }
    if (slot < 0) return -1;

    Body &b = bodies[slot];
    b.type = int(clampd(type, 0, 3));
    b.massSolar = clampd(massSolar, 1.0e-9, 1.0e10);
    b.radiusKm = clampd(radiusKm, 0.001, 1.0e10);
    b.composition = composition;
    b.state = 0;
    b.active = 1;
    b.age = 0.0;
    b.temperature = b.type == 1 ? 5800.0 : (b.type == 2 ? 8.0e5 : (b.type == 0 ? 290.0 : 0.0));
    b.compactness = b.type == 3 ? 1.0 : (b.type == 2 ? 0.7 : 0.0);

    double sr = length3(sx, sy, sz);
    if (sr < 4.1) { sx = 22.0; sy = 0.0; sz = 0.0; sr = 22.0; }
    if (sr > 120.0) { const double k = 120.0 / sr; sx*=k; sy*=k; sz*=k; sr=120.0; }
    b.x = sx; b.y = sy; b.z = sz;

    double dx = tx - sx, dy = ty - sy, dz = tz - sz;
    double dl = length3(dx, dy, dz);
    if (dl < 1e-6) { dx = -sx; dy = -sy; dz = -sz; dl = sr; }
    dx /= dl; dy /= dl; dz /= dl;
    // Prograde tangent around the spin-independent reference z axis.
    double qx = -sy / sr, qy = sx / sr, qz = 0.0;
    dx += qx * tangentBias * 0.55;
    dy += qy * tangentBias * 0.55;
    dz += qz * tangentBias * 0.55;
    dl = length3(dx, dy, dz); dx/=dl; dy/=dl; dz/=dl;
    const double vc = 1.0 / std::sqrt(std::max(0.08, sr - 2.0));
    const double speed = clampd(vc * speedFactor, 0.001, 0.985);
    b.vx = dx * speed; b.vy = dy * speed; b.vz = dz * speed;

    const double actualRadiusRg = b.radiusKm / rgKm();
    const double minVisual = b.type == 0 ? 0.13 : (b.type == 1 ? 0.22 : (b.type == 2 ? 0.11 : 0.08));
    double visual = b.type == 3 ? clampd(2.0 * b.massSolar / blackHoleMass, 0.08, 1.7)
                                : clampd(std::pow(b.massSolar / blackHoleMass, 1.0/3.0) * 1.8, minVisual, 0.65);
    b.renderRadius = std::max(actualRadiusRg, visual);
    b.initialMassSolar = b.massSolar;
    b.baseRenderRadius = b.renderRadius;
    b.tidalRadius = tidalRadiusFor(b);
    b.disruption = 0.0;
    b.disruptionAge = 0.0;
    b.orbitalPhase = 0.0;
    b.circularizedFraction = 0.0;
    b.postCaptureAge = 0.0;
    lastEvent = 1;
    return slot;
}

static void consume(Body &b, bool merger) {
    const double captured = b.massSolar;
    if (merger) {
        const double q = std::min(captured, blackHoleMass) / std::max(captured, blackHoleMass);
        const double radiated = clampd(0.048 * (4.0*q / ((1.0+q)*(1.0+q))), 0.0, 0.095);
        mergerEnergyJ = radiated * captured * 1.98847e30 * 8.987551787e16;
        blackHoleMass += captured * (1.0 - radiated);
        lastEvent = 5;
    } else {
        blackHoleMass += captured;
        lastEvent = 4;
    }
    accretedMass += captured;
    b.active = 0;
}

void sim_step(double dt) {
    // Keep leapfrog stable even at extreme visualization time scales.
    dt = clampd(dt, 0.0, 2500.0);
    const int substeps = int(clampd(std::ceil(dt / 0.08), 1.0, 128.0));
    const double h = dt / double(substeps);
    for (int s = 0; s < substeps; ++s) {
        // Central-horizon capture uses physical, not display-amplified, size.
        // A tidally disrupted body remains briefly as a visible stream instead
        // of disappearing the instant its center of mass reaches the horizon.
        for (auto &b : bodies) if (b.active) {
            if (b.state == 3) {
                b.postCaptureAge += std::min(h, 0.012);
                if (b.postCaptureAge >= 2.5) finishDebrisCapture(b);
                continue;
            }
            const double r = length3(b.x, b.y, b.z);
            b.tidalRadius = tidalRadiusFor(b);
            if (b.state == 0 && b.tidalRadius > 2.0 && r < b.tidalRadius * 1.15) beginDisruption(b);
            const double companionHorizon = b.type == 3 ? 2.0 * b.massSolar / blackHoleMass : 0.0;
            if (r <= 2.035 + companionHorizon) {
                if (b.type != 3 && b.state > 0) beginDebrisCapture(b);
                else consume(b, b.type == 3);
            }
        }

        // Resolve compact-object encounters using physical Schwarzschild radii.
        for (int i = 0; i < MAX_BODIES; ++i) {
            if (!bodies[i].active || bodies[i].type != 3) continue;
            for (int j = i + 1; j < MAX_BODIES; ++j) {
                if (!bodies[j].active || bodies[j].type != 3) continue;
                Body &a = bodies[i], &b = bodies[j];
                const double dx=b.x-a.x, dy=b.y-a.y, dz=b.z-a.z;
                const double d=length3(dx,dy,dz);
                const double mergeR=2.0*(a.massSolar+b.massSolar)/blackHoleMass;
                if (d > std::max(0.002, mergeR*1.08)) continue;
                Body *keep=&a, *gone=&b;
                if (b.massSolar > a.massSolar) { keep=&b; gone=&a; }
                const double m1=keep->massSolar, m2=gone->massSolar, total=m1+m2;
                const double q=std::min(m1,m2)/std::max(m1,m2);
                const double radiated=clampd(0.048*(4.0*q/((1.0+q)*(1.0+q))),0.0,0.095);
                keep->x=(keep->x*m1+gone->x*m2)/total; keep->y=(keep->y*m1+gone->y*m2)/total; keep->z=(keep->z*m1+gone->z*m2)/total;
                keep->vx=(keep->vx*m1+gone->vx*m2)/total; keep->vy=(keep->vy*m1+gone->vy*m2)/total; keep->vz=(keep->vz*m1+gone->vz*m2)/total;
                keep->massSolar=total*(1.0-radiated); keep->radiusKm=2.953250077*keep->massSolar;
                keep->renderRadius=clampd(2.0*keep->massSolar/blackHoleMass,0.08,1.7);
                gone->active=0;
                mergerEnergyJ=radiated*total*1.98847e30*8.987551787e16;
                lastEvent=5;
            }
        }

        // Physical capture by an injected black hole. Display radii are never
        // used here, so visual amplification cannot create false collisions.
        for (int i = 0; i < MAX_BODIES; ++i) {
            Body &hole=bodies[i]; if (!hole.active || hole.type!=3) continue;
            for (int j = 0; j < MAX_BODIES; ++j) {
                Body &matter=bodies[j]; if (i==j || !matter.active || matter.type==3 || matter.state==3) continue;
                const double dx=matter.x-hole.x, dy=matter.y-hole.y, dz=matter.z-hole.z;
                const double d=length3(dx,dy,dz);
                const double captureR=2.0*hole.massSolar/blackHoleMass+matter.radiusKm/rgKm();
                if (d>captureR) continue;
                const double diskOwned=matter.initialMassSolar*matter.circularizedFraction;
                const double capturedMatter=std::max(0.0,matter.massSolar-diskOwned);
                const double total=hole.massSolar+capturedMatter;
                hole.vx=(hole.vx*hole.massSolar+matter.vx*matter.massSolar)/total;
                hole.vy=(hole.vy*hole.massSolar+matter.vy*matter.massSolar)/total;
                hole.vz=(hole.vz*hole.massSolar+matter.vz*matter.massSolar)/total;
                hole.massSolar=total; hole.radiusKm=2.953250077*total;
                hole.renderRadius=clampd(2.0*total/blackHoleMass,0.08,1.7);
                matter.active=0; lastEvent=7;
            }
        }

        // The origin follows the primary black hole. This indirect term keeps
        // the primary-centered frame correct when a companion is massive.
        double originAx=0.0, originAy=0.0, originAz=0.0;
        for (const auto &b : bodies) if (b.active && b.state != 3) {
            const double r2=b.x*b.x+b.y*b.y+b.z*b.z+1.0e-6;
            const double inv=1.0/(r2*std::sqrt(r2));
            const double mu=b.massSolar/blackHoleMass;
            originAx+=mu*b.x*inv; originAy+=mu*b.y*inv; originAz+=mu*b.z*inv;
        }

        double ax[MAX_BODIES]{}, ay[MAX_BODIES]{}, az[MAX_BODIES]{};
        for (int i = 0; i < MAX_BODIES; ++i) {
            Body &b=bodies[i]; if (!b.active || b.state == 3) continue;
            const double r=length3(b.x,b.y,b.z);
            const double denom=std::max(0.08,(r-2.0)*(r-2.0));
            const double amag=1.0/denom;
            ax[i]=-amag*b.x/r-originAx; ay[i]=-amag*b.y/r-originAy; az[i]=-amag*b.z/r-originAz;

            // Pairwise companion gravity. Mass ratios are relative to the
            // primary because length/time are in its rg and rg/c units.
            for (int j = 0; j < MAX_BODIES; ++j) {
                if (j==i || !bodies[j].active || bodies[j].state == 3) continue;
                const Body &o=bodies[j];
                const double dx=o.x-b.x, dy=o.y-b.y, dz=o.z-b.z;
                const double physicalI=b.type==3?2.0*b.massSolar/blackHoleMass:b.radiusKm/rgKm();
                const double physicalJ=o.type==3?2.0*o.massSolar/blackHoleMass:o.radiusKm/rgKm();
                const double soft=std::max(0.001,0.35*(physicalI+physicalJ));
                const double d2=dx*dx+dy*dy+dz*dz+soft*soft;
                const double inv=1.0/(d2*std::sqrt(d2));
                const double mu=o.massSolar/blackHoleMass;
                ax[i]+=mu*dx*inv; ay[i]+=mu*dy*inv; az[i]+=mu*dz*inv;
            }
        }

        for (int i = 0; i < MAX_BODIES; ++i) {
            Body &b=bodies[i]; if (!b.active || b.state == 3) continue;
            double r=length3(b.x,b.y,b.z);

            // Differential gravity first elongates the body into leading and
            // trailing tidal arms. Only material that completes enough of an
            // orbit to self-intersect is transferred into the radiating disk.
            if (b.state == 1 || b.state == 2) {
                const double tidalR = b.tidalRadius;
                const double evolutionStep = std::min(h, 0.08);
                const double geometric = clampd((tidalR*1.15-r)/std::max(0.1,tidalR*0.90),0.0,1.0);
                const double stress = clampd(tidalR/std::max(r,2.01)-0.78,0.0,1.8);
                b.disruptionAge += evolutionStep;
                b.disruption = clampd(std::max(b.disruption,geometric)+stress*evolutionStep*0.11,0.0,1.0);
                b.renderRadius = b.baseRenderRadius;

                const double hx=b.y*b.vz-b.z*b.vy, hy=b.z*b.vx-b.x*b.vz, hz=b.x*b.vy-b.y*b.vx;
                const double angularMomentum=length3(hx,hy,hz);
                b.orbitalPhase += angularMomentum/std::max(4.0,r*r)*h;

                const double v2=b.vx*b.vx+b.vy*b.vy+b.vz*b.vz;
                const double energy=0.5*v2-1.0/std::max(0.08,r-2.0);
                const double boundFraction=clampd(0.50-energy*1.5,0.12,0.88);
                const double collisionGate=clampd((b.orbitalPhase-1.10*PI)/(1.40*PI),0.0,1.0);
                const double targetCircularized=b.disruption*collisionGate*boundFraction;
                if (targetCircularized>b.circularizedFraction) {
                    const double delta=targetCircularized-b.circularizedFraction;
                    const bool firstIntersection=b.circularizedFraction<1.0e-6 && targetCircularized>0.012;
                    b.circularizedFraction=targetCircularized;
                    addDiskMaterial(b,delta);
                    if (firstIntersection) lastEvent=3;
                }
                if (b.disruption>0.82) b.state=2;
            }

            b.vx += ax[i] * h;
            b.vy += ay[i] * h;
            b.vz += az[i] * h;
            const double v = length3(b.vx, b.vy, b.vz);
            if (v > 0.995) { const double k = 0.995 / v; b.vx*=k; b.vy*=k; b.vz*=k; }
            b.x += b.vx * h;
            b.y += b.vy * h;
            b.z += b.vz * h;
            b.age += h;

            r = length3(b.x, b.y, b.z);
            if (r > 180.0) { b.active = 0; lastEvent = 6; }
        }
    }
    if (diskMassSolar > 0.0) {
        const double inflowFraction=1.0-std::exp(-dt*2.0e-8);
        const double inflow=diskMassSolar*inflowFraction;
        diskMassSolar-=inflow;
        blackHoleMass+=inflow;
        accretedMass+=inflow;
    }
    if (diskDensity > 0.0) {
        diskDensity = clampd(diskDensity * std::exp(-dt * 1.0e-7), 0.0, 1.0);
    }
}

int sim_body_count() {
    int n = 0; for (const auto &b : bodies) if (b.active) ++n; return n;
}
int sim_body_active(int i) { return i >= 0 && i < MAX_BODIES ? bodies[i].active : 0; }
double sim_body_value(int i, int field) {
    if (i < 0 || i >= MAX_BODIES) return 0.0;
    const Body &b = bodies[i];
    switch (field) {
        case 0: return b.x; case 1: return b.y; case 2: return b.z;
        case 3: return b.vx; case 4: return b.vy; case 5: return b.vz;
        case 6: return b.massSolar; case 7: return b.radiusKm;
        case 8: return b.renderRadius; case 9: return b.temperature;
        case 10: return b.type; case 11: return b.composition;
        case 12: return b.state; case 13: return b.age;
        case 14: return b.disruption; case 15: return b.orbitalPhase;
        case 16: return b.tidalRadius; case 17: return b.postCaptureAge;
        case 18: return b.circularizedFraction; case 19: return b.disruptionAge;
        default: return 0.0;
    }
}
double sim_disk_density() { return diskDensity; }
double sim_disk_temperature() { return diskTemperature; }
double sim_disk_mass() { return diskMassSolar; }
double sim_disk_normal_x() { return diskNormalX; }
double sim_disk_normal_y() { return diskNormalY; }
double sim_disk_normal_z() { return diskNormalZ; }
double sim_accreted_mass() { return accretedMass; }
double sim_merger_energy() { return mergerEnergyJ; }
int sim_last_event() { int e = lastEvent; lastEvent = 0; return e; }
double sim_rg_km() { return rgKm(); }
double sim_tg_seconds() { return 4.925490947e-6 * blackHoleMass; }
double sim_horizon_km() { return 2.0 * rgKm(); }
double sim_circular_velocity(double r) { return 299792.458 / std::sqrt(std::max(0.02, r - 2.0)); }
double sim_lapse(double r) { return std::sqrt(std::max(0.0, 1.0 - 2.0 / std::max(2.0001, r))); }

}
