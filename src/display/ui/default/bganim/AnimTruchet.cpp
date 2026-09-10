#ifndef GAGGIMATE_SIM

// "Truchet": placeholder registration. The design is entry 30 (truchetdrift) of
// tools/animbench/web/anim_bench.html, the web preview page; the port
// replaces this file. Until then the animation paints the background colour
// so the registry, the settings and the web mirror already carry the id.

#include "BgAnim.h"
#include "BgAnimCommon.h"

namespace {

bool init(int, int) { return true; }

void frame(uint32_t, int, int, const uint8_t[4]) {}

void band(uint16_t *dst, int, int rows, int w, uint32_t, const uint8_t[4]) {
    for (int i = 0; i < rows * w; i++) {
        dst[i] = 0;
    }
}

} // namespace

extern const BgAnimation bg_anim_truchet;
const BgAnimation bg_anim_truchet = {
    "truchet",
    "Truchet",
    {{"speed", "Speed", 50},
     {"arc", "Arc width", 50},
     {"glow", "Glow", 55},
     {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    nullptr,
    nullptr,
};

#endif // GAGGIMATE_SIM
