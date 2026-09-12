#ifndef BGANIM_H
#define BGANIM_H

#include <stdint.h>

// Procedural background animation registry. Each animation renders directly
// into RGB565 horizontal bands (the SleepAnimation task owns the band buffer
// and the panel push; see SleepAnimation.cpp for the pipeline). Animations are
// pure functions of wall-clock time plus up to BG_ANIM_PARAMS (8) user
// parameters (0-100 each, configured from the web UI and the display's
// Parameters page, persisted in Settings as "p0,p1,...,p7;..." indexed by
// animation id; a stored group may be shorter, the rest keep their defaults).
//
// Contract:
//  - init(w, h): lazy, idempotent buffer/LUT allocation; false on OOM. Called
//    on the render task before the first frame after the animation becomes
//    active. May also be used to (re)build param-dependent LUTs cheaply.
//  - frame(tMs, w, h, p): once per frame before the band loop — advance
//    particle state, rebuild per-row/column terms, rotate palettes.
//  - band(dst, y0, rows, w, tMs, p): fill rows [y0, y0+rows) into dst
//    (w * rows RGB565 pixels). Must stay within ~30 cycles/pixel overall.
//    Alignment precondition (gm-bzu.21): dst is 4-byte aligned, and when
//    rows > 1, w is even. Kernels store pixel pairs as one 32-bit word and
//    take row r of dst to begin on a 4-byte boundary, which row r*w pixels in
//    does only when w is even. An odd w (the 466 px panel at half
//    resolution, 233) is therefore rendered one row per call into a 4-byte
//    aligned destination; SleepAnimation's half path does exactly that
//    (BAND_H = 2, one source row per band) and the host harnesses
//    (tools/animbench interlace_check, render_one --shapes) keep their
//    multi-row shapes to even widths. A row's pixels depend only on its
//    absolute y and the frame state, never on which rows share the call.
//
// Params: fixed BG_ANIM_PARAMS slots; key == nullptr marks unused slots. The
// same defs are mirrored in the web UI (web/src/config/bgAnimations.js) and
// the page (tools/animbench/web/anim_bench.html); check-params.py checks the
// mirrors. The cap was 4 until 2026-09-10 (gm-3vj.1).
#define BG_ANIM_PARAMS 8

struct BgAnimParamDef {
    const char *key;   // short identifier, e.g. "speed"
    const char *label; // human label for the web UI
    uint8_t def;       // default value 0-100
};

struct BgAnimation {
    const char *id;   // stable short id, e.g. "plasma"
    const char *name; // display name
    BgAnimParamDef params[BG_ANIM_PARAMS];
    bool (*init)(int w, int h);
    void (*frame)(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]);
    void (*band)(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t p[BG_ANIM_PARAMS]);
    // Optional. Frees everything init() allocated and resets whatever staleness
    // sentinel gates the rebuild, so the next init() reallocates from scratch.
    // Called on the render task when the animation is switched away from, or
    // when the render resolution changes under it -- the latter is why this is
    // a correctness requirement and not only a memory one, since the tables are
    // sized from w/h and init() alone will not resize them.
    // nullptr means "no teardown"; that animation simply keeps its tables.
    void (*release)();
    // Optional. The portable C++ implementation of band(), kept alongside
    // when band() dispatches to a hand-written Xtensa kernel. Same contract
    // and the same output, pixel for pixel: the on-device equivalence test
    // (SleepAnimation::runAnimTest, /api/debug/animtest) renders every band
    // of several frames through both and reports the first pixel that
    // differs, which is the only way an assembly kernel gets validated, since
    // the host bench compiles the C++ path only. nullptr when band() is
    // portable code and there is nothing to compare it against.
    void (*bandRef)(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t p[BG_ANIM_PARAMS]);
};

int bg_animation_count();
// Clamps out-of-range ids to 0 (Plasma).
const BgAnimation &bg_animation(int id);

// Fills out[BG_ANIM_PARAMS] with the defaults for animId, then overrides from
// the packed settings string ("p0,p1,...;p0,p1,...;..." indexed by animation
// id; a group shorter than BG_ANIM_PARAMS leaves the remaining defaults).
void bg_parse_params(const char *packed, int animId, uint8_t out[BG_ANIM_PARAMS]);

// ---- shared color themes -------------------------------------------------
// Every animation draws its colors from one gradient: 2-16 RGB stops ordered
// dark -> bright (stop 0 is the background/darkest tone, the last stop the
// brightest accent), each at a position 0-255 along the ramp. Built-in
// themes have 6 evenly spaced stops and are mirrored in
// web/src/config/bgAnimations.js — keep in sync, append only (the index is
// persisted). Which gradient an animation draws with comes from three
// settings, resolved by bg_resolve_anim_theme:
//   bgAnimThemeMap     "ref;ref;..." indexed by animation id, ref = built-in
//                      index, "c<id>" for a library gradient, or empty
//   bgAnimGradients    the user's library, "id|name|gradient;..." (<= 12)
//   bgAnimGradientRef  the global default, one ref in the same grammar, used
//                      by every animation with no map entry of its own. Empty
//                      means fall through to the two fields below, which is
//                      what a device that has never set it stores.
//   bgAnimTheme        the legacy integer, the fallback when the global ref is
//                      empty or does not resolve. Its namespace is frozen:
//                      0..17 are the original built-ins,
//                      BG_THEME_LEGACY_CUSTOM (18) is the pre-library custom
//                      gradient in bgAnimCustomTheme, and anything else reads
//                      as built-in 0
// A gradient string is "rrggbb[@pos],rrggbb[@pos],..."; without positions the
// stops are spaced evenly, which is also the original palette arithmetic.
// With positions, the colour holds flat before the first stop and after the
// last one, as a CSS gradient does.

constexpr int BG_THEME_MAX_STOPS = 16;
constexpr int BG_GRADIENT_LIB_MAX = 12;       // library entries
constexpr int BG_GRADIENT_LIB_MAX_LEN = 3800; // chars; NVS strings cap at 4000
constexpr int BG_GRADIENT_NAME_MAX = 24;      // characters, as the UI counts them
constexpr int BG_GRADIENT_STR_MAX = BG_THEME_MAX_STOPS * 11; // "rrggbb@255," per stop

// ---- the legacy bgAnimTheme namespace, frozen -----------------------------
//
// Before the gradient library existed, bgAnimTheme was the whole setting: an
// index into an 18-entry built-in table, with the one value past its end, 18,
// meaning "the single custom gradient stored in bgAnimCustomTheme". Devices in
// the field hold that 18. The sentinel is therefore pinned here for good and
// is deliberately NOT bg_theme_count(): deriving it from the table length
// would hand a device that stored 18 whatever built-in is appended at index 18
// the day the table grows. Legacy integers outside 0..18 read as built-in 0.
//
// This is only the meaning of a stored legacy integer. An explicit ref in
// bgAnimGradientRef or one slot of bgAnimThemeMap is a different namespace:
// there a decimal names the built-in at that index in this build's table, so
// the ref "18" can select the first appended built-in while a legacy 18 in
// bgAnimTheme still means the custom gradient.
constexpr int BG_THEME_LEGACY_CUSTOM = 18;

// What the legacy pair (bgAnimTheme, bgAnimCustomTheme) draws: the built-in
// index, or -1 when the custom string is what draws. customValid is what
// bg_custom_valid() says about the stored string. Mirrors bg_resolve_theme
// exactly, and is what a settings surface must use before naming the current
// value, so a stored 18 is never labelled as appended built-in 18.
inline int bg_legacy_builtin(int themeId, bool customValid) {
    if (themeId == BG_THEME_LEGACY_CUSTOM) {
        return customValid ? -1 : 0;
    }
    return (themeId >= 0 && themeId < BG_THEME_LEGACY_CUSTOM) ? themeId : 0;
}

// The rollback mirror. Whoever selects a built-in as the global gradient also
// writes bgAnimTheme, so a build without bgAnimGradientRef draws the same
// thing. themeCount is this build's table length.
//
// Returns the value to write, or -1 to leave bgAnimTheme alone:
//   0..17          mirror unchanged, they mean the same thing in both
//                  namespaces
//   18 and above   mirror as 0, never as the index itself and never as 17: 18
//                  would mean the custom gradient to an older build, and 17
//                  would be a gradient nobody chose
//   out of range   leave it alone; the ref does not resolve, so the legacy
//                  fallback is what still draws and must not move
inline int bg_legacy_mirror_for_builtin(int builtin, int themeCount) {
    if (builtin < 0 || builtin >= themeCount) {
        return -1;
    }
    return builtin < BG_THEME_LEGACY_CUSTOM ? builtin : 0;
}

// The same decision from a stored ref: "" and "c<id>" leave bgAnimTheme alone
// (a library selection keeps whatever legacy fallback was there), a decimal
// goes through bg_legacy_mirror_for_builtin. This is the one mirror policy;
// the web form mirrors it in web/src/config/bgAnimations.js.
inline int bg_legacy_mirror_for_ref(const char *ref, int themeCount) {
    if (ref == nullptr || *ref == '\0' || *ref == 'c') {
        return -1;
    }
    int v = 0;
    for (const char *s = ref; *s != '\0'; s++) {
        if (*s < '0' || *s > '9' || v > 99999) {
            return -1;
        }
        v = v * 10 + (*s - '0');
    }
    return bg_legacy_mirror_for_builtin(v, themeCount);
}

int bg_theme_count();                       // number of built-in themes
const char *bg_theme_name(int i);           // clamped like bg_animation
const uint8_t (*bg_theme_stops(int i))[3];  // 6 RGB stops, dark -> bright

// Resolves the legacy pair into stops/count, by the frozen rules above:
// themeId == BG_THEME_LEGACY_CUSTOM selects the custom string, an invalid or
// empty custom string (or any other out-of-range id) falls back to theme 0.
// The stops are always read as evenly spaced, which is what this setting has
// always drawn; explicit positions in the custom string are discarded.
void bg_resolve_theme(int themeId, const char *custom, uint8_t stops[BG_THEME_MAX_STOPS][3], int &nStops);

// True when the stored custom string parses as a gradient, i.e. when
// BG_THEME_LEGACY_CUSTOM would draw it rather than falling back to built-in 0.
bool bg_custom_valid(const char *custom);

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

// True when ref is one the gradient settings accept: empty, a decimal
// built-in index, or "c<id>". Says nothing about whether the thing it names
// exists; resolution falls back on its own when it does not.
bool bg_ref_valid(const char *ref);

// The category a built-in gradient belongs to, and the ordered category list
// the pickers group by. Both come from data/gradients.json through
// scripts/gen_gradients.py. An out-of-range index reads as index 0, the same
// way bg_theme_name does, so a stored id from a longer table cannot fault.
const char *bg_theme_category(int i);
int bg_theme_category_count();
const char *bg_theme_category_name(int i);

// The gradient animId draws with, in three steps: its own map entry when that
// resolves (a built-in, or a library id that exists), else globalRef by the
// same rule, else the global theme via bg_resolve_theme. A caller with no
// globalRef passes nullptr and gets the two-step behaviour this had before.
void bg_resolve_anim_theme(int animId, const char *map, const char *library, const char *globalRef, int themeId,
                           const char *custom, uint8_t stops[BG_THEME_MAX_STOPS][3], uint8_t pos[BG_THEME_MAX_STOPS],
                           int &nStops, bool &uniform);

// ---- carrying the legacy custom gradient into the library ----------------
//
// One-time migration of bgAnimCustomTheme. Split into a pure planner and a
// runner over an abstract store, because the ordering is the hard part:
// Settings::doSave() writes properties in registration order (bg_th and bg_ct
// before bg_gl and bg_gref) and continues past an individual failure, so
// marking every field dirty at once can durably clear the source before its
// replacement exists. The runner persists and checks one step at a time.
//
// What the planner will not do, and why (each of these was a way to lose a
// user's settings):
//  - It never reads "a parsable bgAnimCustomTheme" as "the user is using it".
//    A device migrated by the first version of this code still has the string
//    with bgAnimTheme at 0, so presence alone proves nothing.
//  - It never replaces a non-empty bgAnimGradientRef, fills a map slot, or
//    touches bgAnimThemeMap at all.
//  - It only retires the legacy fields when nothing still needs them to draw.
//    A global ref that does not resolve in this build (an index from a longer
//    table, a rolled-back firmware) is preserved, and while it is unresolved
//    the legacy fallback behind it is preserved too.
//  - It never evicts a library entry or writes a library that does not
//    validate. When no destination fits it defers and changes nothing.
struct BgGradientMigration {
    enum Action {
        None,     // nothing to carry over, or it is already carried over
        Deferred, // the custom gradient has to stay where it is; reason says why
        Migrate,  // perform the steps below, in this order
    };
    Action action = None;
    // Step 1: append this "id|name|gradient" entry to bgAnimGradients. False
    // when an existing entry already has the same gradient semantics.
    bool appendEntry = false;
    char entry[BG_GRADIENT_NAME_MAX * 4 + BG_GRADIENT_STR_MAX + 16] = {0};
    // The destination entry's id, existing or new.
    int entryId = 0;
    // Step 2: publish bgAnimGradientRef. Only when the legacy custom gradient
    // was the global fallback, i.e. bgAnimGradientRef was empty.
    bool setGlobalRef = false;
    char globalRef[8] = {0};
    // Step 3: retire the legacy fields, writing themeAfter into bgAnimTheme
    // and clearing bgAnimCustomTheme.
    bool retireLegacy = false;
    int themeAfter = 0;
    // Why the plan is what it is, for the log. Always a literal.
    const char *reason = "";
};

BgGradientMigration bg_plan_gradient_migration(const char *library, const char *custom, int themeId,
                                               const char *globalRef, const char *map);

// The settings the runner writes through. flush() must return true only when
// everything set since the last flush is durable; the runner stops at the
// first false and leaves the rest for the next boot.
struct BgGradientStore {
    void *user = nullptr;
    const char *(*library)(void *user) = nullptr;
    void (*setLibrary)(void *user, const char *library) = nullptr;
    void (*setGlobalRef)(void *user, const char *ref) = nullptr;
    void (*setLegacy)(void *user, int themeId, const char *custom) = nullptr;
    bool (*flush)(void *user) = nullptr;
};

enum class BgMigrateResult {
    NothingToDo,
    Deferred,
    Incomplete, // a step did not reach storage; the next boot re-runs the plan
    Done,       // every step was issued; the clear itself is not verifiable
};

BgMigrateResult bg_run_gradient_migration(const BgGradientStore &store, const BgGradientMigration &plan);

#ifdef GM_BGANIM_TEST_TABLE
// Host tests only (test/test_settings_model). Swaps the built-in table so the
// stored-index rules can be checked against a table longer than the one this
// build ships, which is the whole point of freezing the legacy sentinel.
// Never compiled into firmware; nothing but the test defines the macro.
namespace bganim_gen {
struct ThemeDef;
}
void bg_test_set_theme_table(const bganim_gen::ThemeDef *defs, int count);
#endif

#endif // BGANIM_H
