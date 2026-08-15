#ifndef BGANIM_H
#define BGANIM_H

#include <stdint.h>

// Procedural background animation registry. Each animation renders directly
// into RGB565 horizontal bands (the SleepAnimation task owns the band buffer
// and the panel push; see SleepAnimation.cpp for the pipeline). Animations are
// pure functions of wall-clock time plus up to 4 user parameters (0-100 each,
// configured from the web UI and persisted in Settings as "p0,p1,p2,p3;..."
// indexed by animation id).
//
// Contract:
//  - init(w, h): lazy, idempotent buffer/LUT allocation; false on OOM. Called
//    on the render task before the first frame after the animation becomes
//    active. May also be used to (re)build param-dependent LUTs cheaply.
//  - frame(tMs, w, h, p): once per frame before the band loop — advance
//    particle state, rebuild per-row/column terms, rotate palettes.
//  - band(dst, y0, rows, w, tMs, p): fill rows [y0, y0+rows) into dst
//    (w * rows RGB565 pixels). Must stay within ~30 cycles/pixel overall.
//
// Params: fixed 4 slots; key == nullptr marks unused slots. The same defs are
// mirrored in the web UI (web/src/config/bgAnimations.js) — keep in sync.

struct BgAnimParamDef {
    const char *key;   // short identifier, e.g. "speed"
    const char *label; // human label for the web UI
    uint8_t def;       // default value 0-100
};

struct BgAnimation {
    const char *id;   // stable short id, e.g. "plasma"
    const char *name; // display name
    BgAnimParamDef params[4];
    bool (*init)(int w, int h);
    void (*frame)(uint32_t tMs, int w, int h, const uint8_t p[4]);
    void (*band)(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t p[4]);
};

int bg_animation_count();
// Clamps out-of-range ids to 0 (Plasma).
const BgAnimation &bg_animation(int id);

// Fills out[4] with the defaults for animId, then overrides from the packed
// settings string ("p0,p1,p2,p3;p0,p1,p2,p3;..." indexed by animation id).
void bg_parse_params(const char *packed, int animId, uint8_t out[4]);

#endif // BGANIM_H
