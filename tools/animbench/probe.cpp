// Minimal stage probe: which stage of one animation hangs/crawls on host.
#include "../../src/display/ui/default/bganim/BgAnim.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <vector>

static double now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    const int id = argc > 1 ? atoi(argv[1]) : 0;
    const BgAnimation &a = bg_animation(id);
    setvbuf(stderr, nullptr, _IONBF, 0);
    fprintf(stderr, "anim %d (%s)\n", id, a.id);
    uint8_t p[4] = {a.params[0].def, a.params[1].def, a.params[2].def, a.params[3].def};
    std::vector<uint16_t> band(480 * 16);
    double t0 = now();
    fprintf(stderr, "init...\n");
    if (!a.init(480, 480)) {
        fprintf(stderr, "init FAILED\n");
        return 1;
    }
    fprintf(stderr, "init done %.3fs\nframe...\n", now() - t0);
    t0 = now();
    a.frame(1000, 480, 480, p);
    fprintf(stderr, "frame done %.3fs\nband y0=0...\n", now() - t0);
    t0 = now();
    a.band(band.data(), 0, 16, 480, 1000, p);
    fprintf(stderr, "band done %.3fs\nfull frame (30 bands)...\n", now() - t0);
    t0 = now();
    for (int y0 = 0; y0 < 480; y0 += 16) {
        a.band(band.data(), y0, 16, 480, 1000, p);
    }
    fprintf(stderr, "full frame done %.3fs\n", now() - t0);
    return 0;
}
