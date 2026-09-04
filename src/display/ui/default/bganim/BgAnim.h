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
    // Optional. Frees everything init() allocated and resets whatever staleness
    // sentinel gates the rebuild, so the next init() reallocates from scratch.
    // Called on the render task when the animation is switched away from, or
    // when the render resolution changes under it -- the latter is why this is
    // a correctness requirement and not only a memory one, since the tables are
    // sized from w/h and init() alone will not resize them.
    // nullptr means "no teardown"; that animation simply keeps its tables.
    void (*release)();
};

int bg_animation_count();
// Clamps out-of-range ids to 0 (Plasma).
const BgAnimation &bg_animation(int id);

// Fills out[4] with the defaults for animId, then overrides from the packed
// settings string ("p0,p1,p2,p3;p0,p1,p2,p3;..." indexed by animation id).
void bg_parse_params(const char *packed, int animId, uint8_t out[4]);

// ---- shared color themes -------------------------------------------------
// Every animation draws its colors from one gradient: 2-16 RGB stops ordered
// dark -> bright (stop 0 is the background/darkest tone, the last stop the
// brightest accent), each at a position 0-255 along the ramp. Built-in
// themes have 6 evenly spaced stops and are mirrored in
// web/src/config/bgAnimations.js — keep in sync, append only (the index is
// persisted). Which gradient an animation draws with comes from three
// settings, resolved by bg_resolve_anim_theme:
//   bgAnimThemeMap   "ref;ref;..." indexed by animation id, ref = built-in
//                    index, "c<id>" for a library gradient, or empty
//   bgAnimGradients  the user's library, "id|name|gradient;..." (<= 12)
//   bgAnimTheme      built-in index used by animations with no map entry
//                    (bg_theme_count() with bgAnimCustomTheme is the
//                    pre-library custom gradient; still honoured)
// A gradient string is "rrggbb[@pos],rrggbb[@pos],..."; without positions the
// stops are spaced evenly, which is also the original palette arithmetic.
// With positions, the colour holds flat before the first stop and after the
// last one, as a CSS gradient does.

constexpr int BG_THEME_MAX_STOPS = 16;
constexpr int BG_GRADIENT_LIB_MAX = 12;       // library entries
constexpr int BG_GRADIENT_LIB_MAX_LEN = 3800; // chars; NVS strings cap at 4000
constexpr int BG_GRADIENT_NAME_MAX = 24;      // characters, as the UI counts them
constexpr int BG_GRADIENT_STR_MAX = BG_THEME_MAX_STOPS * 11; // "rrggbb@255," per stop

int bg_theme_count();                       // number of built-in themes
const char *bg_theme_name(int i);           // clamped like bg_animation
const uint8_t (*bg_theme_stops(int i))[3];  // 6 RGB stops, dark -> bright

// Resolves themeId + custom string into stops/count. themeId ==
// bg_theme_count() selects the custom string; invalid/empty custom (or any
// out-of-range id) falls back to theme 0.
void bg_resolve_theme(int themeId, const char *custom, uint8_t stops[BG_THEME_MAX_STOPS][3], int &nStops);

// Parses one gradient string ('#', spaces tolerated). Returns the stop count,
// 0 when malformed or fewer than 2 stops. uniform is true when no stop
// carried a position; pos is then filled with the even spacing anyway.
// Positions are forced ascending; the ends are left where the string put them.
int bg_parse_gradient(const char *s, uint8_t stops[BG_THEME_MAX_STOPS][3], uint8_t pos[BG_THEME_MAX_STOPS],
                      bool &uniform);
// Writes the canonical form ("rrggbb,..." or "rrggbb@pos,..."); returns the
// length, 0 if out is too small (BG_GRADIENT_STR_MAX always fits).
int bg_format_gradient(const uint8_t stops[][3], const uint8_t *pos, int nStops, bool uniform, char *out, int outLen);

// Library: every entry has a positive numeric id, a name, a parsable gradient.
bool bg_library_valid(const char *lib);
bool bg_library_lookup(const char *lib, int id, uint8_t stops[BG_THEME_MAX_STOPS][3], uint8_t pos[BG_THEME_MAX_STOPS],
                       int &nStops, bool &uniform);

// Map: each ref is empty, a built-in index, or "c<id>".
bool bg_map_valid(const char *map);

// The gradient animId draws with: its map entry when it resolves (a built-in
// or a library id that exists), else the global theme via bg_resolve_theme.
void bg_resolve_anim_theme(int animId, const char *map, const char *library, int themeId, const char *custom,
                           uint8_t stops[BG_THEME_MAX_STOPS][3], uint8_t pos[BG_THEME_MAX_STOPS], int &nStops,
                           bool &uniform);

#endif // BGANIM_H
