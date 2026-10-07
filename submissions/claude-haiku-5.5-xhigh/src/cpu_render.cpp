// CPU reference renderer: the same photon tracer and shading as the Metal kernel, in double precision.
// Used to check the GPU image and to render frames without a GPU.
//
//   bin/bh_cpu --out frame.png [--width 800] [--height 500] [--spin 0.9] [--yaw 0] [--pitch 0.12]
//              [--dist 40] [--fov 50] [--bg stars|grid|both] [--nodisk] [--exposure 1] [--gain 0.6]
//              [--tpeak 12000] [--eta 0.02] [--steps 4000]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "shared_types.h"
#include "kerr_core.h"
#include "shading.h"
#include "disk_model.h"
#include "scene.h"
#include "png_out.h"

namespace {

// Pixel -> static-observer direction, matching the Metal kernel.
inline void pixel_coords(const TraceUniforms& U, unsigned px, unsigned py, double& sx, double& sy) {
    double halfH = 0.5 * double(U.height);
    sx = (double(px) + 0.5 - 0.5 * double(U.width)) / halfH * double(U.tan_half_fov);
    sy = (halfH - (double(py) + 0.5)) / halfH * double(U.tan_half_fov);
}

}  // namespace

int main(int argc, char** argv) {
    Scene scene;
    std::string out = "frame.png";
    unsigned width = 800, height = 500;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : "0"; };
        if (k == "--out") out = next();
        else if (k == "--width") width = unsigned(std::atoi(next()));
        else if (k == "--height") height = unsigned(std::atoi(next()));
        else if (k == "--spin") scene.a = std::atof(next());
        else if (k == "--yaw") scene.yaw = std::atof(next());
        else if (k == "--pitch") scene.pitch = std::atof(next());
        else if (k == "--dist") scene.dist = std::atof(next());
        else if (k == "--fov") scene.fov_deg = std::atof(next());
        else if (k == "--exposure") scene.exposure = std::atof(next());
        else if (k == "--gain") scene.disk_gain = std::atof(next());
        else if (k == "--tpeak") scene.T_peak = std::atof(next());
        else if (k == "--eta") scene.eta = std::atof(next());
        else if (k == "--steps") scene.max_steps = std::atoi(next());
        else if (k == "--nodisk") scene.disk_on = false;
        else if (k == "--bg") {
            std::string m = next();
            scene.bg_mode = m == "stars" ? 0 : (m == "grid" ? 1 : 2);
        } else {
            std::fprintf(stderr, "unknown option %s\n", k.c_str());
            return 2;
        }
    }

    DiskModel disk = build_disk(scene.a, 16.0, 4096);
    TraceUniforms U = build_uniforms(scene, disk, width, height);

    TraceParams<double> tp;
    tp.a = scene.a;
    tp.r_plus = 1.0 + std::sqrt(1.0 - scene.a * scene.a);
    tp.r_capture = capture_radius(scene.a);
    tp.r_isco = disk.r_isco;
    tp.r_out = disk.r_out;
    tp.eta = scene.eta;
    tp.r_escape = double(U.r_escape);
    tp.max_steps = scene.max_steps;

    // Observer in double precision for the trace (the uniform block stores floats).
    CameraBasis cb = camera_basis(scene);
    Observer<double> obs = make_observer(scene.a, cb.pos, cb.right, cb.up, cb.fwd);
    tp.cam_ut = obs.ut;

    std::vector<uint8_t> rgb(size_t(width) * height * 3);
    const std::vector<float>& lut = disk.lut;
    const float* lutp = lut.data();

    auto t0 = std::chrono::steady_clock::now();
    unsigned n_threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < n_threads; ++t) {
        pool.emplace_back([&, t]() {
            for (unsigned py = t; py < height; py += n_threads) {
                for (unsigned px = 0; px < width; ++px) {
                    double sx, sy;
                    pixel_coords(U, px, py, sx, sy);
                    Phase<double> ray = pixel_ray(obs, sx, sy);
                    Hit<double> hit = trace_photon(tp, ray);
                    V3<float> c = shade_hit(U, lutp, hit);
                    // Exposure and a simple filmic tone curve, then gamma encoding.
                    float rgbf[3] = {c.x, c.y, c.z};
                    for (int ch = 0; ch < 3; ++ch) {
                        float v = 1.0f - std::exp(-scene.exposure * rgbf[ch]);
                        v = std::pow(clampf(v, 0.0f, 1.0f), 1.0f / 2.2f);
                        rgb[3 * (size_t(py) * width + px) + ch] = uint8_t(v * 255.0f + 0.5f);
                    }
                }
            }
        });
    }
    for (auto& th : pool) th.join();
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (!write_png_rgb8(out.c_str(), rgb.data(), int(width), int(height))) {
        std::fprintf(stderr, "could not write %s\n", out.c_str());
        return 1;
    }
    std::printf("wrote %s (%ux%u, a=%.3f) in %.2f s on %u threads\n", out.c_str(), width, height, scene.a, secs, n_threads);
    std::printf("disk: r_isco = %.5f M, radiative efficiency 1 - E_isco = %.5f, T_max = %.3g K (10 M_sun, 1e-8 M_sun/yr)\n",
                disk.r_isco, disk.efficiency, disk.T_max_K);
    return 0;
}
