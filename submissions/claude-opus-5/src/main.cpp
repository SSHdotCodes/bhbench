#include "app.hpp"
#include <cstring>
#include <cstdio>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verify") == 0) return runVerify();
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf(
                "Kerr black hole -- general-relativistic ray tracer\n\n"
                "  blackhole [options]\n\n"
                "  --verify              run the physics test suite and exit\n"
                "  --spin <a/M>          dimensionless spin, 0 .. 0.998   (default 0.9)\n"
                "  --mass <Msun>         black hole mass                  (default 1e7)\n"
                "  --edd  <L/L_Edd>      accretion rate                   (default 0.3)\n"
                "  --incl <deg>          camera inclination               (default 80)\n"
                "  --dist <M>            camera radius in r_g             (default 48)\n"
                "  --disk-outer <M>      outer disk radius                (default 24)\n"
                "  --steps <n>           integrator step budget per ray   (default 900)\n"
                "  --scale <f>           internal render scale            (default 0.75)\n"
                "  --width/--height <px> window size\n"
                "  --view <0|1|2>        ray trace | embedding funnel | light cones\n"
                "  --disk-mode <0..3>    off | opaque | disk+halo | halo only\n"
                "  --tonemap <0|1|2>     asinh | ACES | linear\n"
                "  --debug <0..3>        beauty | steps | |H| | redshift\n"
                "  --exposure --bloom --grid --nebula --stars --corona --turb\n"
                "  --bench <n>           run n frames with vsync off, print fps\n"
                "  --frames <n> --shot <file>   render n frames, save a PNG, exit\n"
                "  --no-hud --no-help    hide the overlays\n\n"
                "Press h in the window for the key bindings.\n");
            return 0;
        }
    }
    return runApp(argc, argv);
}
