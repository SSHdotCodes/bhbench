#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Geometrized units: G = c = M = 1. r is Schwarzschild areal radius.
namespace bh {
constexpr double pi = 3.14159265358979323846;
constexpr double horizon = 2.0, photonSphere = 3.0, isco = 6.0;
constexpr double criticalImpact = 5.1961524227066318806;
struct Orbit { double u, v, lookback; };
inline Orbit derivative(Orbit s, double b) {
    const double f = std::max(1e-7, 1.0 - 2.0*s.u);
    return {s.v, -s.u + 3.0*s.u*s.u, 1.0/(b*s.u*s.u*f)};
}
inline Orbit add(Orbit a, Orbit b, double h) {
    return {a.u+h*b.u, a.v+h*b.v, a.lookback+h*b.lookback};
}
inline Orbit rk4(Orbit s, double h, double b) {
    auto k1=derivative(s,b), k2=derivative(add(s,k1,h/2),b);
    auto k3=derivative(add(s,k2,h/2),b), k4=derivative(add(s,k3,h),b);
    return {s.u+h*(k1.u+2*k2.u+2*k3.u+k4.u)/6,
            s.v+h*(k1.v+2*k2.v+2*k3.v+k4.v)/6,
            s.lookback+h*(k1.lookback+2*k2.lookback+2*k3.lookback+k4.lookback)/6};
}
inline double invariant(Orbit s) { return s.v*s.v+s.u*s.u*(1-2*s.u); }
inline Orbit initial(double observerRadius, double radialCosine) {
    double f=1-2/observerRadius;
    double b=observerRadius*std::sqrt(std::max(0.0,1-radialCosine*radialCosine))/std::sqrt(f);
    return {1/observerRadius,-radialCosine/b,0};
}
inline double impact(double observerRadius, double radialCosine) {
    return observerRadius*std::sqrt(std::max(0.0,1-radialCosine*radialCosine))/std::sqrt(1-2/observerRadius);
}
// Zero-torque Novikov-Thorne flux, with the common dimensional factor removed.
inline double diskFlux(double r) {
    if(r<=isco) return 0;
    double x=std::sqrt(r), x0=std::sqrt(isco), a=std::sqrt(3.0);
    double integral=x-x0-a/2*std::log((x-a)*(x0+a)/((x+a)*(x0-a)));
    return 1.5*integral/(std::pow(r,2.5)*(r-3));
}
inline double frequencyShift(double r, double observerRadius, double photonLzOverE) {
    return std::sqrt(1-3/r)/(std::sqrt(1-2/observerRadius)*(1-std::pow(r,-1.5)*photonLzOverE));
}
inline double embeddingHeight(double r) { return 2*std::sqrt(2*(r-2)); }
struct Endpoint { Orbit orbit; double phi; int status; int steps; };
// status: 0 captured, 1 escaped, 2 unresolved. A finite loop is never classified as capture.
inline Endpoint trace(double radius, double radialCosine, double h=.018) {
    double b=impact(radius,radialCosine);
    if(b<1e-7) return {{1/radius,0,0},0,radialCosine<0?0:1,0};
    Orbit s=initial(radius,radialCosine); double phi=0;
    const int limit=int(std::ceil(24.0/h));
    for(int i=0;i<limit;++i) {
        Orbit n=rk4(s,h,b);
        if(n.u<=0) {
            auto dense=[&](double a) {
                double a2=a*a,a3=a2*a;
                return (2*a3-3*a2+1)*s.u+(a3-2*a2+a)*h*s.v
                      +(-2*a3+3*a2)*n.u+(a3-a2)*h*n.v;
            };
            double lo=0,hi=1;
            for(int k=0;k<40;++k) {
                double mid=(lo+hi)/2;
                if(dense(mid)>0) lo=mid; else hi=mid;
            }
            double a=(lo+hi)/2;
            return {add(s,{n.u-s.u,n.v-s.v,0},a),phi+a*h,1,i+1};
        }
        phi+=h; s=n;
        if(s.u>=.5) return {s,phi,0,i+1};
    }
    return {s,phi,2,limit};
}
struct DiskHit { int status; double radius, lookback; };
// Independent reference for the opaque equatorial disk, including coordinate travel time.
inline DiskHit traceDisk(double radius,double radialCosine,double erY,double etY,double h=.002) {
    const double b=impact(radius,radialCosine);
    Orbit s=initial(radius,radialCosine); double phi=0;
    for(int i=0;i<int(std::ceil(24/h));++i) {
        Orbit n=rk4(s,h,b);
        double y0=erY*std::cos(phi)+etY*std::sin(phi);
        double y1=erY*std::cos(phi+h)+etY*std::sin(phi+h);
        if(y0*y1<0) {
            double crossing=std::atan2(-erY,etY);
            crossing+=std::ceil((phi-crossing)/pi)*pi;
            Orbit hit=rk4(s,std::clamp(crossing-phi,0.0,h),b);
            double r=1/hit.u;
            if(hit.u>0 && r>=6 && r<=24) return {3,r,hit.lookback};
        }
        if(n.u>=.5) return {0,0,0};
        if(n.u<=0) return {1,0,0};
        s=n; phi+=h;
    }
    return {2,0,0};
}
}
