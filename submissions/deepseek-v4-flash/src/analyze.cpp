// Numerical analysis of a rendered PPM frame: finds the shadow, measures its
// apparent radius, the photon ring, and the disk Doppler asymmetry.
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>

struct Image {
    int w = 0, h = 0;
    std::vector<unsigned char> rgb;
    double at(int x, int y, int c) const { return rgb[((size_t)y * w + x) * 3 + c] / 255.0; }
    double lum(int x, int y) const {
        const double l = at(x,y,0)*0.2126 + at(x,y,1)*0.7152 + at(x,y,2)*0.0722;
        return l > 1.0 ? 1.0 : l;
    }
};

static Image loadPPM(const char* path) {
    Image im;
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return im; }
    char magic[3];
    if (std::fscanf(f, "%2s", magic) != 1 || magic[0] != 'P' || magic[1] != '6') {
        std::fprintf(stderr, "not a P6 file\n");
        return im;
    }
    int maxv;
    if (std::fscanf(f, "%d %d %d\n", &im.w, &im.h, &maxv) != 3) return im;
    im.rgb.resize((size_t)im.w * im.h * 3);
    if (std::fread(im.rgb.data(), 1, im.rgb.size(), f) != im.rgb.size())
        std::fprintf(stderr, "short read\n");
    std::fclose(f);
    return im;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: analyze <frame.ppm> <shadowRadiusPxTheory> [cx cy]\n");
        return 1;
    }
    Image im = loadPPM(argv[1]);
    if (!im.w) return 1;
    double theory = std::atof(argv[2]);
    int cx = argc > 3 ? std::atoi(argv[3]) : im.w / 2;
    int cy = argc > 4 ? std::atoi(argv[4]) : im.h / 2;

    // radial profile around the hole (hole at screen center when looking at it)
    const int NB = 256;
    std::vector<double> prof(NB, 0.0), profN(NB, 0.0), profMax(NB, 0.0);
    double minLum = 1e9; int minR = 0;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            double dx = x - cx, dy = y - cy;
            double r = std::sqrt(dx * dx + dy * dy);
            if (r >= NB) continue;
            double l = im.lum(x, y);
            int b = (int)r;
            prof[b] += l; profN[b] += 1.0;
            if (l > profMax[b]) profMax[b] = l;
            if (l < minLum) { minLum = l; minR = b; }
        }
    for (int b = 0; b < NB; ++b) if (profN[b] > 0) prof[b] /= profN[b];

    // shadow: find the largest run of dim pixels starting from the darkest ring
    double darkThresh = 0.02;
    int shadowR = 0;
    for (int b = 0; b < NB; ++b) {
        if (profN[b] < 4) break;
        if (prof[b] < darkThresh) shadowR = b; else break;
    }
    std::printf("shadow radius (dim run)     : %d px (theory %.1f px)\n", shadowR, theory);

    // photon ring: first bright spike just outside the shadow
    int ringR = 0; double ringLum = 0;
    for (int b = shadowR + 2; b < NB; ++b) {
        if (profN[b] < 4) break;
        if (prof[b] > ringLum) { ringLum = prof[b]; ringR = b; }
    }
    std::printf("brightest ring (photon ring): r = %d px, mean lum %.3f, max %.3f\n",
                ringR, ringLum, profMax[ringR]);

    // disk Doppler asymmetry: mean brightness on left vs right of the hole
    // within the disk annulus [shadowR, 2.5*shadowR]
    double lsum = 0, rsum = 0; int ln = 0, rn = 0;
    int rOut = (int)(2.5 * shadowR);
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            double dx = x - cx, dy = y - cy;
            double rr = std::sqrt(dx * dx + dy * dy);
            if (rr < shadowR || rr > rOut) continue;
            double l = im.lum(x, y);
            if (dx < 0) { lsum += l; ++ln; } else { rsum += l; ++rn; }
        }
    if (ln && rn) {
        double la = lsum / ln, ra = rsum / rn;
        std::printf("disk mean brightness: left %.3f, right %.3f, ratio %.2f\n", la, ra, la / ra);
    }

    // background stars: fraction of pixels above a modest threshold far from hole
    double ssum = 0; int sn = 0;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            double dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy < (2.0 * rOut) * (2.0 * rOut)) continue;
            ssum += im.lum(x, y); ++sn;
        }
    if (sn) std::printf("sky region mean lum: %.4f (stars + sky)\n", ssum / sn);
    return 0;
}
