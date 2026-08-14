// ============================================================================
//  physics_test — CPU replication of the shader's null-geodesic integrator.
//  Validates against exact Schwarzschild results:
//    1. shadow critical impact parameter ......... b = 3*sqrt(3) M = 2.598 rs
//    2. photon sphere .................... periapsis of critical ray = 1.5 rs
//    3. weak-field deflection ... 4M/b + (15Pi/4)(M/b)^2  (Einstein + 2nd order)
//    4. massive-particle ISCO ......................... r = 6M = 3 rs
//  Also runs a step-size convergence study to expose integrator error.
// ============================================================================
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <algorithm>

static const double RS = 1.0, M = 0.5;

struct Vec { double x, y, z; };
static Vec operator*(Vec a, double s){ return {a.x*s, a.y*s, a.z*s}; }
static Vec operator+(Vec a, Vec b){ return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static double dot(Vec a, Vec b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
static Vec cross(Vec a, Vec b){
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
static double len(Vec a){ return std::sqrt(dot(a, a)); }

static Vec accel(Vec p, double h2){
    double r2 = dot(p, p);
    return p*(-1.5*h2/(r2*r2*std::sqrt(r2)));   // -(3/2) rs h^2 x / r^5
}

// Photon from x0 (moving +x) at impact parameter b. method 0 = velocity
// Verlet (shader default), 1 = RK4. Returns true if it escapes to r > 250.
static bool photon(double b, double x0, int method, double dtBase,
                   double& rminOut, double& deflectOut, double& hDrift){
    Vec p{x0, b, 0.0}, v{1.0, 0.0, 0.0};
    Vec h = cross(p, v);
    double h2 = dot(h, h);
    double rmin = len(p);
    for (long i = 0; i < 8000000; i++){
        double r = len(p);
        if (r < rmin) rmin = r;
        if (r < 1.0 + 1e-9){ rminOut = r; deflectOut = 0; return false; }
        if (r > 250.0 && dot(p, v) > 0.0){
            rminOut = rmin;
            double s = len(v);
            deflectOut = std::acos(std::max(-1.0, std::min(1.0, v.x/s)));
            hDrift = std::fabs(len(cross(p, v)) - std::sqrt(h2))/std::sqrt(h2);
            return true;
        }
        double dt = dtBase*std::max(0.05, r - 0.9);
        Vec vn, pn;
        if (method == 0){                       // velocity Verlet
            Vec a1 = accel(p, h2);
            pn = p + v*dt + a1*(0.5*dt*dt);
            Vec a2 = accel(pn, h2);
            vn = v + (a1 + a2)*(0.5*dt);
        } else {                                // RK4
            Vec k1v = accel(p, h2),            k1x = v;
            Vec k2v = accel(p + k1x*(dt/2), h2), k2x = v + k1v*(dt/2);
            Vec k3v = accel(p + k2x*(dt/2), h2), k3x = v + k2v*(dt/2);
            Vec k4v = accel(p + k3x*dt, h2),     k4x = v + k3v*dt;
            pn = p + (k1x + k2x*2.0 + k3x*2.0 + k4x)*(dt/6.0);
            vn = v + (k1v + k2v*2.0 + k3v*2.0 + k4v)*(dt/6.0);
        }
        p = pn; v = vn;
    }
    rminOut = len(p);
    return false;
}

// Marginal stability of timelike circular orbits from the exact effective
// potential V(r) = (1 - rs/r)(1 + L^2/r^2), L^2 = M r^2/(r - 3M).
static bool stableAt(double r){
    double L2 = M*r*r/(r - 3.0*M);
    auto V = [&](double x){ return (1.0 - RS/x)*(1.0 + L2/(x*x)); };
    double h = 1e-4;
    return (V(r - h) + V(r + h) - 2.0*V(r)) > 0.0;   // local min = stable
}

int main(){
    int fails = 0;

    // ---- 1+2: critical impact parameter (bisection on capture/escape) ----
    double rmin, defl, drift;
    double lo = 2.0, hi = 3.2;                 // lo: captured, hi: escapes
    bool escLo = photon(lo, -60, 0, 0.004, rmin, defl, drift);
    bool escHi = photon(hi, -60, 0, 0.004, rmin, defl, drift);
    if (escLo || !escHi){
        std::printf("FAIL: bracket wrong (b=2 escapes=%d, b=3.2 escapes=%d)\n",
                    escLo, escHi);
        fails++;
    } else {
        for (int i = 0; i < 60; i++){
            double mid = 0.5*(lo + hi);
            if (photon(mid, -60, 0, 0.004, rmin, defl, drift)) hi = mid;
            else lo = mid;
        }
        double bcrit = 0.5*(lo + hi);
        double exact = 3.0*std::sqrt(3.0)*M;
        std::printf("critical impact parameter : %.5f rs   (exact 3*sqrt(3)M = %.5f)  %+.2f%%\n",
                    bcrit, exact, 100.0*(bcrit - exact)/exact);
        if (std::fabs(bcrit - exact)/exact > 0.01) fails++;

        double rminC;
        photon(bcrit + 2e-4, -60, 0, 0.002, rminC, defl, drift);
        std::printf("periapsis of critical ray : %.4f rs   (photon sphere  = 1.5 rs)\n",
                    rminC);
        if (std::fabs(rminC - 1.5) > 0.05) fails++;
    }

    // ---- 3: weak-field deflection convergence study -----------------------
    std::printf("\nweak-field deflection, b = 40 rs (exact 4M/b + 15Pi/4 (M/b)^2 = %.5f rad):\n",
                4.0*M/40.0 + (15.0*M_PI/4.0)*(M*M/(40.0*40.0)));
    double dVV = -1, dRK = -1;
    for (double dt : {0.004, 0.002, 0.001}){
        photon(40.0, -100, 0, dt, rmin, defl, drift);
        std::printf("  velocity-Verlet  dtBase %.4f : %.5f rad\n", dt, defl);
        if (dt == 0.001) dVV = defl;
    }
    photon(40.0, -100, 1, 0.004, rmin, defl, drift);
    dRK = defl;
    std::printf("  RK4              dtBase 0.0040 : %.5f rad\n", dRK);
    double exactD = 4.0*M/40.0 + (15.0*M_PI/4.0)*(M*M/(40.0*40.0));
    if (std::fabs(dVV - exactD)/exactD > 0.03){ std::printf("  FAIL: Verlet not converged\n"); fails++; }
    if (std::fabs(dRK  - exactD)/exactD > 0.03){ std::printf("  FAIL: RK4 not converged\n"); fails++; }

    // ---- 4: ISCO -----------------------------------------------------------
    double a = 1.55, b2 = 10.0;                // a: unstable, b2: stable
    if (stableAt(a) || !stableAt(b2)){
        std::printf("FAIL: ISCO bracket wrong\n"); fails++;
    } else {
        for (int i = 0; i < 80; i++){
            double mid = 0.5*(a + b2);
            if (stableAt(mid)) b2 = mid; else a = mid;
        }
        double isco = 0.5*(a + b2);
        std::printf("\nISCO (marginally stable orbit): %.4f rs   (exact 6M = 3 rs)\n", isco);
        if (std::fabs(isco - 3.0) > 0.02) fails++;
    }

    std::printf(fails ? "\n%d CHECK(S) FAILED\n" : "\nALL PHYSICS CHECKS PASSED\n", fails);
    return fails ? 1 : 0;
}
