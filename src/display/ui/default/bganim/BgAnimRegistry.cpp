#ifndef GAGGIMATE_SIM

#include "BgAnim.h"
#include <stdlib.h>
#include <string.h>

// Registration order is the persisted animation id — append only, never
// reorder (settings store the index). Mirrored in
// web/src/config/bgAnimations.js — keep in sync.
extern const BgAnimation bg_anim_plasma;
extern const BgAnimation bg_anim_lava;
extern const BgAnimation bg_anim_silk;
extern const BgAnimation bg_anim_starfield;
extern const BgAnimation bg_anim_aurora;
extern const BgAnimation bg_anim_ripples;
extern const BgAnimation bg_anim_caustics;
extern const BgAnimation bg_anim_mandala;
extern const BgAnimation bg_anim_orbits;
extern const BgAnimation bg_anim_fireflies;
extern const BgAnimation bg_anim_steam;
extern const BgAnimation bg_anim_ember;
extern const BgAnimation bg_anim_nebula;
extern const BgAnimation bg_anim_silk2;

namespace {
const BgAnimation *const REGISTRY[] = {
    &bg_anim_plasma,  &bg_anim_lava,    &bg_anim_silk,    &bg_anim_starfield, &bg_anim_aurora, &bg_anim_ripples,
    &bg_anim_caustics, &bg_anim_mandala, &bg_anim_orbits, &bg_anim_fireflies, &bg_anim_steam, &bg_anim_ember,
    &bg_anim_nebula,  &bg_anim_silk2,
};
} // namespace

int bg_animation_count() { return sizeof(REGISTRY) / sizeof(REGISTRY[0]); }

const BgAnimation &bg_animation(int id) {
    const int n = bg_animation_count();
    return *REGISTRY[(id >= 0 && id < n) ? id : 0];
}

void bg_parse_params(const char *packed, int animId, uint8_t out[4]) {
    const BgAnimation &anim = bg_animation(animId);
    for (int i = 0; i < 4; i++) {
        out[i] = anim.params[i].key != nullptr ? anim.params[i].def : 0;
    }
    if (packed == nullptr) {
        return;
    }
    // Seek to the animId-th ';'-separated group.
    const char *s = packed;
    for (int skip = 0; skip < animId && s != nullptr; skip++) {
        s = strchr(s, ';');
        if (s != nullptr) {
            s++;
        }
    }
    if (s == nullptr || *s == '\0' || *s == ';') {
        return;
    }
    for (int i = 0; i < 4 && *s != '\0' && *s != ';'; i++) {
        char *end = nullptr;
        const long v = strtol(s, &end, 10);
        if (end == s) {
            break;
        }
        out[i] = static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v));
        s = end;
        if (*s == ',') {
            s++;
        }
    }
}

#endif // GAGGIMATE_SIM
