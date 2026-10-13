// Export the submitted native geometry and preset values without changing it.
#define main bhbench_native_argon_main
#include "../../submissions/gemini-4-argon/src/main.cpp"
#undef main
#include <filesystem>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    std::filesystem::path output(argv[1]);
    std::filesystem::create_directories(output);
    std::ofstream metadata(output / "presets.json");
    metadata << std::setprecision(9) << "[\n";
    const int presets[] = {-1,6,7,8,9,0};
    bool first = true;
    for (int id : presets) {
        gApp = AppState{};
        if (id >= 0) applyPreset(id);
        // Export photon beams for every preset so the browser can toggle them.
        const bool nativeBeams = gApp.params.showGeodesicBeams;
        gApp.params.showGeodesicBeams = true;
        bh::SpacetimeMeshData mesh;
        bh::buildSpacetimeTrapdoorMesh(mesh, gApp.params, 0.0f);
        const std::string file = "preset-" + std::to_string(id) + ".bin";
        std::ofstream binary(output / file, std::ios::binary);
        auto write = [&](const bh::GridVertex& v) {
            const float values[] = {v.pos.x,v.pos.y,v.pos.z,v.color.r,v.color.g,v.color.b,v.color.a,v.meta.x,v.meta.y};
            binary.write(reinterpret_cast<const char*>(values), sizeof(values));
        };
        for (const auto& v : mesh.surfaceTris) write(v);
        size_t gridCount = 0;
        for (const auto& v : mesh.gridLines) if (v.meta.y < 4.5f) { write(v); ++gridCount; }
        size_t beamCount = 0;
        for (const auto& v : mesh.gridLines) if (v.meta.y > 4.5f) { write(v); ++beamCount; }
        const auto& p = gApp.params; const auto& c = gApp.cam;
        if (!first) metadata << ",\n";
        first = false;
        metadata << "{\"id\":" << id << ",\"file\":\"" << file << "\",\"surfaceCount\":" << mesh.surfaceTris.size()
          << ",\"gridCount\":" << gridCount << ",\"beamCount\":" << beamCount
          << ",\"spin\":" << p.spin << ",\"temperature\":" << p.diskTempKelvin
          << ",\"intensity\":" << p.diskIntensity << ",\"halo\":" << p.haloStrength
          << ",\"depth\":" << p.trapdoorDepth << ",\"beams\":" << (nativeBeams ? "true":"false")
          << ",\"jets\":" << (p.showRelativisticJets ? "true":"false")
          << ",\"yaw\":" << c.yaw << ",\"pitch\":" << c.pitch << ",\"distance\":" << c.distance
          << ",\"targetY\":" << c.target.y << ",\"fov\":" << c.fovDeg
          << ",\"horizon\":" << bh::kerrEventHorizon(p.mass,p.spin)
          << ",\"isco\":" << bh::kerrISCO(p.mass,p.spin)
          << ",\"photon\":" << bh::kerrPhotonSpherePrograde(p.mass,p.spin) << "}";
    }
    metadata << "\n]\n";
}
