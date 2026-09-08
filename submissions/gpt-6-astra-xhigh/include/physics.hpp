#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Geometric units G=c=M=1. Independent double-precision reference solver.
namespace physics {
constexpr double pi=3.14159265358979323846;
constexpr double horizon=2., photonSphere=3., isco=6.;
inline double criticalImpact() { return std::sqrt(27.); }
struct State { double u, q, time; };
inline State operator+(State a,State b) { return {a.u+b.u,a.q+b.q,a.time+b.time}; }
inline State operator*(State a,double s) { return {a.u*s,a.q*s,a.time*s}; }
inline State derivative(State s,double b,bool curved=true) {
    double f=std::max(1.e-8,1.-2.*s.u);
    return {s.q,-s.u+(curved?3.*s.u*s.u:0.),1./(b*std::max(1.e-20,s.u*s.u)*(curved?f:1.))};
}
inline State rk4(State s,double h,double b,bool curved=true) {
    State a=derivative(s,b,curved),c=derivative(s+a*(h*.5),b,curved);
    State d=derivative(s+c*(h*.5),b,curved),e=derivative(s+d*h,b,curved);
    return s+(a+c*2.+d*2.+e)*(h/6.);
}
inline double invariant(State s) { return s.q*s.q+s.u*s.u-2.*s.u*s.u*s.u; }
inline State initial(double r,double b) {
    double u=1./r;
    return {u,std::sqrt(std::max(0.,1./(b*b)-u*u+2.*u*u*u)),0.};
}
struct Trace { bool captured=false, escaped=false; double angle=0.,maxRelativeError=0.; State end{}; };
inline Trace trace(double b,double h=.003,double radius=2000.,bool curved=true) {
    State s=initial(radius,b); if(!curved) s.q=std::sqrt(1./(b*b)-s.u*s.u);
    Trace result; double phi=0.;
    for(int i=0;i<200000;i++) {
        double step=std::min(h,.12*std::max(s.u,1.e-8)/std::max(std::abs(s.q),1.e-8));
        State next=rk4(s,step,b,curved); phi+=step;
        if(curved) result.maxRelativeError=std::max(result.maxRelativeError,std::abs(invariant(next)*b*b-1.));
        if(next.u>=1./horizon) { result.captured=true;s=next;break; }
        if(next.u<=1./radius && next.q<0.) {
            // Extrapolate the asymptotic straight line from a finite far boundary.
            phi+=std::atan2(next.u,-next.q);result.escaped=true;s=next;break;
        }
        s=next;
    }
    result.angle=phi+std::asin(b/radius); result.end=s;return result;
}
// Page-Thorne zero-torque Schwarzschild flux, with Mdot/(4 pi) omitted.
inline double diskFlux(double r) {
    if(r<=isco) return 0.;
    const double x=std::sqrt(r),x0=std::sqrt(6.),a=std::sqrt(3.);
    const double integral=x-x0-a*.5*std::log(((x-a)/(x+a))/((x0-a)/(x0+a)));
    return std::max(0.,1.5*integral/(std::pow(r,2.5)*(r-3.)));
}
inline double omega(double r) { return std::pow(r,-1.5); }
inline double redshift(double r,double observerRadius,double futureLambda) {
    return std::sqrt(1.-3./r)/(std::sqrt(1.-2./observerRadius)*(1.-omega(r)*futureLambda));
}
inline double embedding(double r) { return 2.*std::sqrt(2.*std::max(0.,r-2.)); }
// Equatorial null rays mapped to the spatial embedding, for the diagram only.
struct PathPoint { double r,phi; };
inline std::vector<PathPoint> path(double b) {
    std::vector<PathPoint> points;State s=initial(30.,b);double phi=0.;
    for(int i=0;i<6000;i++) {
        if(s.u>=.5 || (s.u<1./30. && s.q<0.)) break;
        points.push_back({1./s.u,phi});
        double h=std::min(.009,.08*s.u/std::max(std::abs(s.q),1.e-8));
        s=rk4(s,h,b);phi+=h;
    }
    return points;
}
}
