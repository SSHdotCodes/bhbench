// Scene description shared by the CPU reference renderer and the Metal app: the camera orbit,
// the static observer's frame and the per-frame uniform block.
#ifndef BH_SCENE_H
#define BH_SCENE_H

#include <cmath>
#include "shared_types.h"
#include "kerr_core.h"
#include "disk_model.h"

struct Scene {
    double a = 0.9;            // spin
    double yaw = 0.0;          // camera azimuth about the spin axis (radians)
    double pitch = 0.12;       // camera elevation above the equator (radians)
    double dist = 40.0;        // camera distance (units of M)
    double fov_deg = 50.0;     // vertical field of view
    int bg_mode = 2;           // 0 stars, 1 grid, 2 both
    bool disk_on = true;
    double exposure = 0.85;
    double disk_gain = 1.4;    // artistic brightness scale of the disk
    double T_peak = 7500.0;    // display temperature of the hottest annulus (K); the true T_max is shown in the HUD
    double eta = 0.03;         // step fraction: photons move ~eta*|x| per RK4 step (0.02 for the strictest runs)
    int max_steps = 4000;
};

// Camera position and its right/up/forward unit vectors (flat Cartesian, z = spin axis).
struct CameraBasis {
    V3<double> pos;
    V3<double> right;
    V3<double> up;
    V3<double> fwd;
};

inline CameraBasis camera_basis(const Scene& s) {
    double p = s.pitch;
    if (p > 1.45) p = 1.45;
    if (p < -1.45) p = -1.45;
    CameraBasis c;
    c.pos = v3(s.dist * std::cos(p) * std::cos(s.yaw), s.dist * std::cos(p) * std::sin(s.yaw), s.dist * std::sin(p));
    c.fwd = norm3(v3(-c.pos.x, -c.pos.y, -c.pos.z));
    V3<double> world_up = v3(0.0, 0.0, 1.0);
    // right = forward x world_up, up = right x forward: a right-handed screen frame.
    c.right = norm3(v3(c.fwd.y * world_up.z - c.fwd.z * world_up.y,
                           c.fwd.z * world_up.x - c.fwd.x * world_up.z,
                           c.fwd.x * world_up.y - c.fwd.y * world_up.x));
    c.up = v3(c.right.y * c.fwd.z - c.right.z * c.fwd.y,
                  c.right.z * c.fwd.x - c.right.x * c.fwd.z,
                  c.right.x * c.fwd.y - c.right.y * c.fwd.x);
    return c;
}

// Fills the GPU/CPU uniform block for a frame of the given pixel size.
inline TraceUniforms build_uniforms(const Scene& s, const DiskModel& disk, unsigned w, unsigned h) {
    CameraBasis cb = camera_basis(s);
    Observer<double> o = make_observer(s.a, cb.pos, cb.right, cb.up, cb.fwd);
    TraceUniforms u;
    u.a = float(s.a);
    u.r_plus = float(1.0 + std::sqrt(1.0 - s.a * s.a));
    u.r_capture = float(capture_radius(s.a));
    u.r_isco = float(disk.r_isco);
    u.r_out = float(disk.r_out);
    u.eta = float(s.eta);
    u.r_escape = float(std::fmax(2.0 * s.dist + 40.0, 100.0));
    u.cam_ut = float(o.ut);
    u.disk_gain = float(s.disk_gain);
    u.T_peak = float(s.T_peak);
    u.exposure = float(s.exposure);
    for (int mu = 0; mu < 4; ++mu) {
        u.cam_u[mu] = float(o.u[mu]);
    }
    for (int k = 0; k < 3; ++k) {
        for (int mu = 0; mu < 4; ++mu) {
            u.cam_e[4 * k + mu] = float(o.e[k][mu]);
        }
    }
    u.cam_pos[0] = float(cb.pos.x);
    u.cam_pos[1] = float(cb.pos.y);
    u.cam_pos[2] = float(cb.pos.z);
    u.tan_half_fov = float(std::tan(0.5 * s.fov_deg * M_PI / 180.0));
    u.aspect = float(w) / float(h);
    u.width = w;
    u.height = h;
    u.lut_n = int(disk.lut.size());
    u.max_steps = s.max_steps;
    u.bg_mode = s.bg_mode;
    u.disk_on = s.disk_on ? 1 : 0;
    u.pad0 = 0;
    u.pad1 = 0;
    return u;
}

#endif
