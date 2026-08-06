#include <cassert>
#include <cmath>
#include <cstdio>

#include "../web/sim.cpp"

struct Outcome {
    bool disrupted = false;
    bool horizonStream = false;
    double maxStretch = 0.0;
    double firstDiskPhase = -1.0;
    double finalDisk = 0.0;
};

static Outcome runCase(bool direct) {
    sim_init(4.30e6);
    const int slot = direct
        ? sim_spawn_aimed(1, 1.0, 696340.0, 3, 22.0, 0.0, 0.0,
                          0.0, 0.0, 0.0, 0.90, 0.0)
        : sim_spawn(1, 1.0, 696340.0, 3, 22.0, 0.0, 0.82, 0.12);
    assert(slot >= 0);

    Outcome out;
    for (int i = 0; i < 180000 && sim_body_active(slot); ++i) {
        sim_step(0.02);
        const int state = int(sim_body_value(slot, 12));
        const double disruption = sim_body_value(slot, 14);
        const double phase = sim_body_value(slot, 15);
        out.disrupted = out.disrupted || state > 0;
        out.horizonStream = out.horizonStream || state == 3;
        out.maxStretch = std::max(out.maxStretch, disruption);
        if (out.firstDiskPhase < 0.0 && sim_disk_density() > 1.0e-6) out.firstDiskPhase = phase;
    }
    out.finalDisk = sim_disk_density();
    return out;
}

int main() {
    const Outcome direct = runCase(true);
    const Outcome grazing = runCase(false);

    assert(direct.disrupted);
    assert(direct.horizonStream);
    assert(direct.maxStretch > 0.80);
    assert(direct.finalDisk < 1.0e-6);

    assert(grazing.disrupted);
    if (grazing.firstDiskPhase >= 0.0) {
        assert(grazing.firstDiskPhase >= 1.10 * 3.14159265358979323846 - 0.03);
    }

    std::printf("direct: disrupted=%d stream=%d stretch=%.3f disk=%.6f\n",
                direct.disrupted, direct.horizonStream, direct.maxStretch, direct.finalDisk);
    std::printf("grazing: disrupted=%d stream=%d stretch=%.3f first_disk_phase=%.3f disk=%.6f\n",
                grazing.disrupted, grazing.horizonStream, grazing.maxStretch,
                grazing.firstDiskPhase, grazing.finalDisk);
}
