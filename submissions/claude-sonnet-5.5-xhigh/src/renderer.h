// renderer.h -- Metal renderer for the Kerr black hole (ray-traced lensing, accretion disk, spacetime funnel).
#pragma once
#import <Metal/Metal.h>
#include <memory>
#include <string>
#include <vector>

// Everything the user can change (the UI mutates this; the renderer reads it every frame).
struct SimState {
    double spin = 0.90;          // a/M
    double incl_deg = 76.0;      // camera polar angle theta_0 measured from the spin axis
    double azim_deg = 0.0;       // camera azimuth phi_0
    double dist = 64.0;          // camera radius r_0 [M]
    double fov_deg = 32.0;       // vertical field of view
    double sim_time = 0.0;       // simulation clock [M]
    double time_scale = 3.0;     // simulated M per real second
    bool paused = false;

    bool disk = true;
    bool stars = true;
    bool milky_way = true;
    bool celestial_grid = false; // lensed latitude/longitude lines on the sky
    bool bloom = true;
    bool hud = true;
    bool taa = true;             // temporal anti-aliasing / progressive accumulation
    bool turbulence = true;
    bool auto_orbit = false;
    bool halo = true;            // optically thin hot corona around the hole
    double halo_amp = 0.45;
    bool critical_curve = false; // overlay Bardeen's analytic critical curve (validation)
    int funnel_mode = 1;         // 0 off, 1 inset, 2 fullscreen
    int debug_view = 0;          // 0 normal, 1 image order, 2 redshift, 3 integrator cost

    double t_peak = 8500.0;      // peak disk temperature [K] (sets the colour palette)
    double exposure = 1.0;
    double disk_gain = 2.6;      // disk brightness relative to the sky (arbitrary photometric scale)
    double disk_r_out = 20.0;
    double disk_h = 0.02;        // photosphere half-thickness H/r of the disk
    double render_scale = 1.0;   // internal resolution / drawable resolution
    bool auto_scale = true;      // dynamic resolution to hold the frame-rate target
    double target_fps = 60.0;
    double step_coeff = 0.10;    // integrator phase increment per step (smaller = more accurate)

    // funnel camera
    double funnel_yaw = 0.35, funnel_pitch = 0.42, funnel_dist = 78.0;
    bool funnel_auto = true;
    double ui_scale = 2.0;       // backing scale factor (HUD font size)
    std::string status_msg;      // transient message shown on the HUD
    double status_until = 0.0;
};

struct FrameInfo {
    double gpu_ms = 0.0;         // smoothed GPU time per frame
    int internal_w = 0, internal_h = 0;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    bool init(id<MTLDevice> device, std::string* error);

    // Encode a whole frame into `cb`, ending with the composite drawn into `target` (BGRA8Unorm).
    void encode_frame(id<MTLCommandBuffer> cb, id<MTLTexture> target, SimState& s, double dt_real, double wall_time);

    // Is the drawable pixel (x, y) [origin top-left] inside the funnel inset?
    bool in_inset(double x, double y) const;
    FrameInfo info() const;
    double instant_fps() const;
    void note_frame_time(double dt_real);

    // Force the temporal history to restart (e.g. after a screenshot resize).
    void reset_history();

    // ---- validation helpers ----
    // Trace `n` camera rays (image-plane coordinates in [-1,1]^2) with the GPU probe kernel. Output per ray:
    // {status, steps, crossings, r_hit, phi_hit, t_hit, L, nu_cam, dx, dy, dz, g}
    bool gpu_probe(const SimState& s, const std::vector<float>& xy, std::vector<float>* out, int width, int height);

private:
    struct Impl;
    std::unique_ptr<Impl> p;
};

std::string state_summary(const SimState& s);

// Exact image-plane position (NDC, x right / y up, both in [-1,1] over the view) of the point of Bardeen's critical curve
// with photon-orbit radius r_p, as seen by THIS camera (finite distance, ZAMO frame).  branch 0/1 = upper/lower half.
// Returns false if that photon direction is not visible (outside the field of view is still returned true).
bool bardeen_ndc(const SimState& s, double aspect, int branch, double r_p, double* x, double* y);
