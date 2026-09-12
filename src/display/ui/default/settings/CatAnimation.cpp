// Animation category: background animation, the standby screen's own
// animation, the parameters of the chosen one (a row that pushes
// CatAnimParams.cpp's page), frame rate, all-screens, the UI theme, the
// current animation's gradient, plate handling, element tint, the text
// scrim, the screen fade (out, in, curve) and interlacing. Every value row
// is live (SettingsUI.h): a row writes Settings and calls markDirty() the
// moment it changes, rather than waiting for commit, so
// DefaultUI::updateState applies it on the next rerender pass (CLAUDE.md,
// UI-pipeline invariants). commit()'s only remaining job is the
// touched-field precedence rule: if a web save landed on a touched field
// while this page was open, re-assert this visit's value (gm-flw.9).
#include "CatAnimParams.h"
#include "SettingsModel.h"
#include "SettingsLog.h"
#include "SettingsRows.h"
#include "SettingsUI.h"

#include <display/core/PluginManager.h>
#include <display/core/Settings.h>
#include <display/main.h>
#include <display/ui/default/DefaultUI.h>
#include <display/ui/default/bganim/BgAnim.h>
#include <display/ui/default/eez/images.h>

#include <cstdio>
#include <string>
#include <vector>

#include "esp_log.h"

namespace {

// bg_animation()/bg_animation_count() (BgAnimRegistry.cpp) and
// bg_theme_name()/bg_theme_count() (BgAnimThemes.cpp) compile only outside
// GAGGIMATE_SIM: both files carry the animation render kernels and the
// simulator's stub SleepAnimation never calls them (CLAUDE.md: "SleepAnimation
// is a stub under GAGGIMATE_SIM and updateState skips animation and plate
// application"), so the whole translation unit is excluded there and neither
// symbol exists to link against on the host. This category still has to show
// real names on the sim (the bead's own acceptance criteria: "the model's
// animation names and gradient parsing are host code, so the sim shows them
// without a renderer"), so it keeps a mirror of both tables here rather than
// touching BgAnimRegistry.cpp/BgAnimThemes.cpp, which carry the kernels the
// animation workers are editing. The animation half of the mirror now
// carries each animation's parameter table too, because the Parameters page
// (gm-3vj.2, CatAnimParams.cpp) shows one row per parameter and needs the
// labels and defaults, not just the names.
#ifdef GAGGIMATE_SIM
// One entry per registry slot, in REGISTRY order (BgAnimRegistry.cpp), each
// carrying the animation's display name and its whole eight-slot parameter
// table. Generated from the real tables at HEAD 096292af (the `.name` field
// and the params brace of every Anim*.cpp BgAnimation struct, read with the
// regexes tools/animbench/check-params.py already uses), which is also how
// the eight names this mirror had wrong when it was hand-written were found
// ("Brushed Metal" for "Brushed", "Quiet Horizon" for "Horizon", and six
// more). Append-only, same order as the real table: the index is the
// persisted setting. A change to the real roster still needs the same edit
// made here and in test_animation.py's ANIM_NAMES; regenerating both from
// the source is a few lines of Python and is flagged to the epic lead.
struct SimAnim {
    const char *name;
    BgAnimParamDef params[BG_ANIM_PARAMS];
};

constexpr SimAnim kSimAnims[] = {
    {"Plasma", {{"speed", "Speed", 50}, {"scale", "Scale", 50}, {"brightness", "Brightness", 70}, {"contrast", "Contrast", 50}, {"cycle", "Colour cycle", 50}, {"stretch", "Stretch", 50}, {"grain", "Grain", 50}, {"shift", "Palette shift", 50}}},
    {"Lava", {{"speed", "Speed", 50}, {"scale", "Blob size", 50}, {"glow", "Glow", 60}, {"count", "Blob count", 67}, {"core", "Hot core", 50}, {"falloff", "Falloff", 40}, {"contrast", "Contrast", 50}, {"wander", "Wander", 50}}},
    {"Silk", {{"speed", "Speed", 50}, {"scale", "Fringe density", 45}, {"glow", "Sheen", 55}, {"spread", "Wave spread", 50}, {"twist", "Twist", 50}, {"wobble", "Breathe", 50}, {"vignette", "Edge fade", 80}, {"grain", "Grain", 50}}},
    {"Starfield", {{"speed", "Drift speed", 50}, {"density", "Stars", 45}, {"twinkle", "Twinkle", 50}, {"shooting", "Shooting stars", 30}, {"glow", "Star glow", 50}, {"skyglow", "Sky glow", 50}, {"falloff", "Sky falloff", 50}, {"tint", "Star tint", 50}}},
    {"Aurora", {{"speed", "Speed", 50}, {"intensity", "Intensity", 55}, {"waviness", "Waviness", 50}, {"height", "Height", 50}, {"spread", "Spread", 50}, {"glow", "Glow", 50}, {"drift", "Drift", 50}, {nullptr, nullptr, 0}}},
    {"Ripples", {{"speed", "Ring speed", 50}, {"rate", "Drop rate", 40}, {"decay", "Fade", 50}, {"glow", "Glow", 50}, {"spread", "Drop spread", 50}, {"width", "Ring width", 50}, {"tone", "Water tone", 50}, {"trough", "Trough dip", 50}}},
    {"Caustics", {{"speed", "Drift speed", 50}, {"scale", "Cell scale", 45}, {"contrast", "Contrast", 55}, {"glow", "Glow", 50}, {"spot", "Spot size", 50}, {"spread", "Wave spread", 50}, {"tilt", "Pattern tilt", 50}, {"turn", "Turn rate", 50}}},
    {"Mandala", {{"speed", "Speed", 50}, {"symmetry", "Symmetry", 50}, {"complexity", "Complexity", 45}, {"drift", "Ring drift", 50}, {"vignette", "Vignette", 50}, {"breathe", "Breathe", 50}, {"contrast", "Contrast", 50}, {"rings", "Ring pitch", 50}}},
    {"Orbits", {{"speed", "Speed", 50}, {"orbitCount", "Orbits", 55}, {"eccentricity", "Eccentricity", 55}, {"trail", "Trail", 50}, {"size", "Orbit size", 50}, {"path", "Path glow", 50}, {"glow", "Body glow", 50}, {"tilt", "Tilt spread", 50}}},
    {"Fireflies", {{"speed", "Speed", 50}, {"count", "Count", 60}, {"glow", "Glow", 55}, {"shimmer", "Shimmer", 40}, {"spread", "Spread", 50}, {"drift", "Drift", 50}, {"pulse", "Pulse depth", 50}, {"halo", "Halo", 50}}},
    {"Steam", {{"speed", "Rise speed", 50}, {"count", "Wisps", 55}, {"swirl", "Swirl", 45}, {"density", "Density", 50}, {"size", "Puff size", 50}, {"spread", "Base spread", 50}, {"tint", "Steam tint", 50}, {"taper", "Top fade", 50}}},
    {"Ember", {{"speed", "Speed", 50}, {"glow", "Glow size", 45}, {"flicker", "Flicker", 20}, {"pulse", "Pulse", 50}, {"height", "Height", 50}, {"falloff", "Falloff", 50}, {"core", "Core heat", 50}, {"grain", "Grain", 50}}},
    {"Nebula", {{"speed", "Drift speed", 50}, {"density", "Density", 50}, {"turbulence", "Turbulence", 40}, {"contrast", "Contrast", 50}, {"drift", "Drift angle", 50}, {"grain", "Grain", 50}, {"detail", "Fine detail", 50}, {"lspeed", "Layer speed", 50}}},
    {"Silk 2", {{"speed", "Speed", 50}, {"scale", "Fringe density", 45}, {"glow", "Contrast", 55}, {"mix", "Wave balance", 50}, {"cross", "Cross detail", 50}, {"sheenw", "Sheen width", 55}, {"rim", "Rim spread", 50}, {"drift", "Drift", 50}}},
    {"Brushed", {{"speed", "Speed", 50}, {"grain", "Grain", 35}, {"reflection", "Reflection", 45}, {"contrast", "Contrast", 30}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}},
    {"Horizon", {{"speed", "Speed", 50}, {"height", "Height", 45}, {"curvature", "Curvature", 35}, {"softness", "Softness", 60}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}},
    {"Oculus", {{"speed", "Speed", 50}, {"diameter", "Diameter", 65}, {"breath", "Breath", 20}, {"edge", "Edge softness", 55}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}},
    {"Chevrons", {{"speed", "Speed", 50}, {"spacing", "Spacing", 65}, {"angle", "Angle", 50}, {"contrast", "Contrast", 35}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}},
    {"Mosaic", {{"speed", "Speed", 50}, {"size", "Tile size", 45}, {"contrast", "Contrast", 30}, {"variation", "Variation", 55}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}},
    {"Saddle", {{"speed", "Speed", 50}, {"curvature", "Curvature", 35}, {"drift", "Drift", 25}, {"contrast", "Contrast", 30}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}},
    {"Refraction", {{"speed", "Speed", 50}, {"bend", "Bend", 35}, {"width", "Channel width", 65}, {"contrast", "Contrast", 30}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}},
    {"Sundial", {{"speed", "Speed", 50}, {"width", "Wedge width", 40}, {"contrast", "Contrast", 25}, {"shading", "Surface shading", 30}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}},
    {"Crescent", {{"speed", "Speed", 50}, {"size", "Size", 70}, {"phase", "Phase range", 40}, {"contrast", "Contrast", 40}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}},
    {"Glint", {{"speed", "Speed", 50}, {"length", "Length", 35}, {"width", "Width", 45}, {"brightness", "Brightness", 55}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}},
    {"Tunnel", {{"speed", "Speed", 50}, {"pitch", "Band pitch", 50}, {"brightness", "Brightness", 74}, {"mix", "Band share", 50}, {"contrast", "Contrast", 50}, {"curve", "Depth curve", 50}, {"spiral", "Spiral", 50}, {"turn", "Turn rate", 50}}},
    {"Kaleido", {{"speed", "Speed", 50}, {"scale", "Blotch scale", 50}, {"brightness", "Brightness", 62}, {"contrast", "Contrast", 50}, {"rays", "Rays", 50}, {"vignette", "Vignette", 50}, {"sweep", "Sweep", 50}, {nullptr, nullptr, 0}}},
    {"Shafts", {{"speed", "Speed", 50}, {"density", "Shaft count", 50}, {"brightness", "Brightness", 66}, {"contrast", "Contrast", 50}, {"falloff", "Falloff", 50}, {"reach", "Reach", 50}, {"breath", "Breath", 50}, {"sway", "Sway", 50}}},
    {"Weave", {{"speed", "Speed", 50}, {"scale", "Weave scale", 50}, {"brightness", "Brightness", 62}, {"contrast", "Contrast", 50}, {"cross", "Cross weave", 50}, {"turn", "Turn rate", 50}, {"drift", "Drift", 50}, {"breath", "Breath rate", 50}}},
    {"Lens", {{"speed", "Speed", 50}, {"size", "Lens size", 55}, {"brightness", "Brightness", 62}, {"contrast", "Contrast", 50}, {"edge", "Edge width", 50}, {"rim", "Rim darkness", 50}, {"travel", "Lens travel", 50}, {"drift", "Ground drift", 50}}},
    {"Tide", {{"speed", "Speed", 50}, {"width", "Band width", 50}, {"glow", "Glow", 55}, {"bands", "Band count", 50}, {"sway", "Sway", 50}, {"edge", "Edge shape", 50}, {"floor", "Floor", 30}, {"grain", "Grain", 50}}},
    {"Truchet", {{"speed", "Speed", 50}, {"arc", "Arc width", 50}, {"glow", "Glow", 55}, {"drift", "Drift angle", 50}, {"bias", "Tile bias", 50}, {"sharp", "Sharpness", 50}, {"contrast", "Contrast", 50}, {"grain", "Grain", 50}}},
    {"Quilt", {{"speed", "Speed", 50}, {"pitch", "Pillow size", 75}, {"relief", "Relief", 55}, {"turn", "Light turn", 50}, {"drift", "Drift", 50}, {"dome", "Puffiness", 50}, {"stretch", "Stretch", 50}, {"bright", "Brightness", 50}}},
    {"Rain", {{"speed", "Speed", 50}, {"tail", "Tail length", 50}, {"glow", "Head glow", 55}, {"width", "Drop width", 50}, {"fade", "Tail fade", 50}, {"spread", "Speed spread", 50}, {"base", "Base light", 50}, {"grain", "Grain", 50}}},
    {"Stripes", {{"speed", "Speed", 50}, {"pitch", "Stripe pitch", 50}, {"depth", "Depth", 55}, {"beat", "Beat depth", 75}, {"beats", "Beat count", 20}, {"turn", "Turn rate", 50}, {"floor", "Black level", 39}, {"grain", "Grain", 50}}},
    {"Ribbon", {{"speed", "Speed", 50}, {"width", "Ribbon width", 50}, {"twist", "Twist", 50}, {"bright", "Brightness", 62}, {"waist", "Waist", 50}, {"glow", "Edge glow", 50}, {"shade", "Face shading", 50}, {"wash", "Backdrop", 50}}},
    {"Harmonograph", {{"speed", "Speed", 50}, {"size", "Figure size", 68}, {"glow", "Thread glow", 60}, {"bright", "Brightness", 60}, {"lobes", "Lobe count", 50}, {"turn", "Turn rate", 50}, {"trail", "Trail length", 50}, {"vign", "Vignette", 50}}},
    {"Floor", {{"speed", "Speed", 50}, {"yaw", "Yaw sway", 50}, {"scale", "Plaid scale", 50}, {"bright", "Brightness", 60}, {"glide", "Glide rate", 50}, {"haze", "Haze depth", 50}, {"glow", "Horizon glow", 50}, {"tile", "Tile size", 50}}},
    {"Hills", {{"speed", "Speed", 50}, {"relief", "Ridge relief", 50}, {"depth", "Layer contrast", 55}, {"bright", "Brightness", 60}, {"spread", "Ridge spacing", 50}, {"haze", "Ridge haze", 50}, {"sky", "Sky tone", 50}, {"stars", "Star density", 50}}},
    {"Gyroid", {{"speed", "Speed", 50}, {"scale", "Passage size", 50}, {"glow", "Passage width", 55}, {"bright", "Brightness", 60}, {"aspect", "Aspect", 50}, {"morph", "Morph rate", 50}, {"floor", "Ground level", 50}, {"grain", "Grain", 50}}},
    {"Barrel", {{"speed", "Speed", 50}, {"bands", "Bands", 14}, {"shade", "Cylinder shade", 62}, {"tilt", "Band tilt", 50}, {"width", "Barrel width", 50}, {"edge", "Edge fade", 50}, {"light", "Light angle", 50}, {"depth", "Band depth", 50}}},
    {"Grid", {{"speed", "Speed", 50}, {"density", "Grid density", 50}, {"lines", "Line strength", 58}, {"width", "Line width", 50}, {"cross", "Cross lines", 50}, {"reach", "Grid reach", 50}, {"shade", "Floor shade", 50}, {"drift", "Side drift", 50}}},
    {"Cells", {{"speed", "Speed", 50}, {"width", "Channel width", 55}, {"depth", "Contrast", 60}, {"count", "Cell count", 42}, {"halo", "Halo width", 40}, {"tilt", "Drift tilt", 50}, {"grain", "Grain", 50}, {"glow", "Glow", 50}}},
    {"Dimples", {{"speed", "Speed", 50}, {"relief", "Relief", 58}, {"bright", "Brightness", 62}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}},
    {"Cube", {{"speed", "Speed", 50}, {"size", "Cube size", 50}, {"glow", "Face glow", 55}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}, {nullptr, nullptr, 0}}},
};

constexpr const char *kSimThemeNames[] = {
    "Espresso", "Ocean", "Violet Dusk", "Forest", "Sunset", "Fire", "Ice", "Mono", "Rose", "Gold", "Aurora", "Cyber",
    "Ember Coal", "Deep Space", "Teal Reef", "Sakura", "Lime", "Arctic Night",
};

int animCountFn() { return static_cast<int>(sizeof(kSimAnims) / sizeof(kSimAnims[0])); }
const SimAnim &simAnim(int i) {
    const int n = animCountFn();
    return kSimAnims[(i >= 0 && i < n) ? i : 0];
}
const char *animNameFn(int i) { return simAnim(i).name; }
const BgAnimParamDef *animParamsFn(int i) { return simAnim(i).params; }
int themeCountFn() { return static_cast<int>(sizeof(kSimThemeNames) / sizeof(kSimThemeNames[0])); }
const char *themeNameFn(int i) {
    const int n = themeCountFn();
    return kSimThemeNames[(i >= 0 && i < n) ? i : 0];
}
#else
int animCountFn() { return bg_animation_count(); }
const char *animNameFn(int i) { return bg_animation(i).name; }
const BgAnimParamDef *animParamsFn(int i) { return bg_animation(i).params; }
int themeCountFn() { return bg_theme_count(); }
const char *themeNameFn(int i) { return bg_theme_name(i); }
#endif

const settingsui::AnimationNameProvider kAnimProvider{animCountFn, animNameFn};
const settingsui::ThemeNameProvider kThemeProvider{themeCountFn, themeNameFn};

constexpr int kThemeModeCount = sizeof(settingsui::kThemeModeLabels) / sizeof(settingsui::kThemeModeLabels[0]);
constexpr int kPlatesCount = sizeof(settingsui::kPlatesLabels) / sizeof(settingsui::kPlatesLabels[0]);
constexpr int kFadeCurveCount = sizeof(settingsui::kFadeCurveLabels) / sizeof(settingsui::kFadeCurveLabels[0]);

std::vector<settingsui::GradientChoice> currentGradientChoices() {
    return settingsui::gradientChoices(kThemeProvider, std::string(controller.getSettings().getBgAnimGradients().c_str()));
}

// The stored bgAnimId is not range-checked anywhere on its way in (the web
// handler and Settings::setBgAnimId store whatever arrives; every renderer
// clamps at use), and this category indexes two per-animation vectors sized
// to the live registry with it, so an id from a longer registry (a build
// rolled back to fewer animations) would index past their end. Clamp once
// on the way into the draft; a cycle from the clamped id then wraps within
// the registry like any other.
int clampAnimId(int id) {
    const int count = animCountFn();
    if (count <= 0) {
        return 0;
    }
    if (id < 0) {
        return 0;
    }
    return id >= count ? count - 1 : id;
}

// The Standby anim row's own clamp. Its stored value is -1 for "play the
// main animation on the standby screen too", which is the default, and an id
// past the end of the live registry means the same thing: DefaultUI falls
// back to the main id there rather than indexing off the end, so the row has
// to show "Same" for that value instead of a name it cannot look up.
int clampStandbyAnimId(int id) {
    const int count = animCountFn();
    if (id < 0 || count <= 0 || id >= count) {
        return -1;
    }
    return id;
}

// The row cycles through "Same" and then every animation, so choice 0 is
// "Same" (stored -1) and choice n is animation n - 1.
int standbyChoiceIndex(int id) { return id < 0 ? 0 : id + 1; }
int standbyIdForChoice(int choice) { return choice <= 0 ? -1 : choice - 1; }
const char *standbyChoiceLabel(int id) { return id < 0 ? "Same" : animNameFn(id); }

// The choice index bgAnimThemeMap's stored ref resolves to for animId, 0
// (Default) when the map has no entry there or the entry names a library
// gradient that has since been deleted (SettingsModel.h,
// gradientChoiceIndexForRef).
int gradientIndexForAnim(int animId, const std::vector<settingsui::GradientChoice> &choices) {
    const std::string map(controller.getSettings().getBgAnimThemeMap().c_str());
    const std::string ref = settingsui::gradientMapReadRef(map, animId);
    return settingsui::gradientChoiceIndexForRef(choices, ref);
}

// Requirement: "for Default it shows 'Default (<global theme name>)'".
std::string gradientDisplayText(int index, const std::vector<settingsui::GradientChoice> &choices) {
    if (index == 0) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "Default (%s)", themeNameFn(controller.getSettings().getBgAnimTheme()));
        return buf;
    }
    return choices[static_cast<size_t>(index)].label;
}

struct CatAnimationCtx {
    int animId = 0;
    // -1 = "Same", i.e. the standby screen plays the main animation.
    int standbyAnimId = -1;
    long fps = 30; // kBgAnimFpsSpec grid (settingsui::stepValue works in long)
    bool allScreens = false;
    int themeMode = 0; // 0 Dark, 1 Light

    // Gradient lives in one ";"-separated slot of the shared bgAnimThemeMap
    // string, not its own property, so its touched-ness is tracked per
    // animation id, not against a single "last touched" id: the map is one
    // field per animation, and the touched-field precedence rule applies
    // per field. Switching from A to B and touching Gradient on both in the
    // same visit touches both slots; a web save (which replaces the whole
    // map string) must not clobber either one at commit. Sized to
    // animCountFn() in animEnter and never resized after, so no heap growth
    // per step (gm-flw.9 review).
    int gradientIndex = 0;
    std::string gradientRef;
    std::vector<bool> gradientTouched;   // per animId
    std::vector<std::string> gradientLastRef; // per animId, valid where gradientTouched[i]

    int plates = 0; // 0 Keep, 1 Hide, 2 Custom

    // Palette rows: the anchor is the colour Settings held when the row was
    // last (re)entered, fixed for the rest of the visit, so an off-palette
    // stored colour keeps its 13th cycle-able entry even after the index
    // cycles onto a named colour and back (SettingsModel.h: "An off-palette
    // stored colour stays selectable as its hex entry for the whole visit").
    // index is the current position in that fixed list; color is what index
    // resolves to right now, which is what gets written to Settings.
    int plateColorAnchor = 0;
    int plateColorIndex = 0;
    int plateColor = 0;
    long plateOpacity = 0;

    bool tintEnabled = false;
    int tintColorAnchor = 0xFFFFFF;
    int tintColorIndex = 0;
    int tintColor = 0xFFFFFF;

    long scrim = 0;

    long fadeOut = 120; // ms, kBgFadeSpec grid
    long fadeIn = 120;
    int fadeCurve = 0; // 0 Linear, 1 Smooth
    bool interlace = false;

    bool animIdTouched = false;
    bool standbyAnimIdTouched = false;
    bool fpsTouched = false;
    bool allScreensTouched = false;
    bool themeModeTouched = false;
    bool platesTouched = false;
    bool plateColorTouched = false;
    bool plateOpacityTouched = false;
    bool tintEnabledTouched = false;
    bool tintColorTouched = false;
    bool scrimTouched = false;
    bool fadeOutTouched = false;
    bool fadeInTouched = false;
    bool fadeCurveTouched = false;
    bool interlaceTouched = false;

    lv_obj_t *animRow = nullptr;
    lv_obj_t *standbyRow = nullptr;
    lv_obj_t *paramsRow = nullptr;
    lv_obj_t *frameRateRow = nullptr;
    lv_obj_t *themeRow = nullptr;
    lv_obj_t *gradientRow = nullptr;
    lv_obj_t *platesRow = nullptr;
    lv_obj_t *plateColorRow = nullptr;
    lv_obj_t *plateOpacityRow = nullptr;
    lv_obj_t *tintColorRow = nullptr;
    lv_obj_t *scrimRow = nullptr;
    lv_obj_t *fadeOutRow = nullptr;
    lv_obj_t *fadeInRow = nullptr;
    lv_obj_t *fadeCurveRow = nullptr;

    // Cached from the first buildRow call (enter/commit/reconcile receive
    // only ctx, never SettingsUI&; see CatTemps.cpp's identical comment).
    // Every value row here is live, unlike Temps, so this is also how every
    // onChange callback reaches markDirty()/plugins().trigger(), not just
    // commit(); the Parameters row needs it to push its page. Every row on
    // the current page runs buildRow before any of these other callbacks can
    // fire (enter -> rebuildPage -> buildRow, always page 0 first), so this
    // is set before any of them ever run.
    SettingsUI *ui = nullptr;
};

// ---- Animation --------------------------------------------------------------

void animIdOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    ctx->animId = settingsui::wrapIndex(ctx->animId, animCountFn(), dir);
    ctx->animIdTouched = true;
    controller.getSettings().setBgAnimId(ctx->animId);
    if (ctx->animRow != nullptr) {
        settingsRowSetValue(ctx->animRow, animNameFn(ctx->animId));
    }
    // Requirement: "Changing Animation re-reads the Gradient row's value for
    // the new animation." If the user already touched Gradient for this
    // exact animation earlier in the visit, that choice stands; otherwise
    // read the map fresh for the newly selected animation.
    const auto choices = currentGradientChoices();
    if (ctx->gradientTouched[static_cast<size_t>(ctx->animId)]) {
        ctx->gradientIndex =
            settingsui::gradientChoiceIndexForRef(choices, ctx->gradientLastRef[static_cast<size_t>(ctx->animId)]);
    } else {
        ctx->gradientIndex = gradientIndexForAnim(ctx->animId, choices);
    }
    ctx->gradientRef = choices[static_cast<size_t>(ctx->gradientIndex)].ref;
    if (ctx->gradientRow != nullptr) {
        settingsRowSetValue(ctx->gradientRow, gradientDisplayText(ctx->gradientIndex, choices).c_str());
    }
    // The Parameters row names the animation whose parameters it opens, so
    // it follows this row.
    if (ctx->paramsRow != nullptr) {
        settingsRowSetValue(ctx->paramsRow, animNameFn(ctx->animId));
    }
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
        ctx->ui->plugins().trigger("bganim:preview-end");
    }
}

// ---- Standby anim -----------------------------------------------------------

// The animation the standby screen plays. "Same" (stored -1) means it plays
// whatever the Animation row above selects; anything else names one
// animation of its own, with its own stored parameters and its own gradient,
// because both are indexed by animation id. No preview-end trigger here,
// unlike the Animation row: this value changes nothing on the screen the
// settings cover sits on, so it cannot be what a live gradient preview is
// showing.
void standbyAnimOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    const int choice = settingsui::wrapIndex(standbyChoiceIndex(ctx->standbyAnimId), animCountFn() + 1, dir);
    ctx->standbyAnimId = standbyIdForChoice(choice);
    ctx->standbyAnimIdTouched = true;
    controller.getSettings().setBgAnimStandbyId(ctx->standbyAnimId);
    if (ctx->standbyRow != nullptr) {
        settingsRowSetValue(ctx->standbyRow, standbyChoiceLabel(ctx->standbyAnimId));
    }
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
    }
}

// ---- Parameters ---------------------------------------------------------------

// Pushes the Parameters page for the animation the draft is on. The page
// writes Settings live and has no draft of its own to hand back, so nothing
// here needs to survive the push; ctx->animId is read now rather than
// captured, so cycling Animation and then tapping this opens the animation
// on screen.
void paramsOnActivate(void *user) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    if (ctx->ui != nullptr) {
        settingsAnimParamsPush(*ctx->ui, ctx->animId);
    }
}

// ---- Frame rate --------------------------------------------------------------

void frameRateOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    ctx->fps = settingsui::stepValue(ctx->fps, dir, fast, settingsui::kBgAnimFpsSpec);
    ctx->fpsTouched = true;
    controller.getSettings().setBgAnimFps(static_cast<int>(ctx->fps));
    if (ctx->frameRateRow != nullptr) {
        settingsRowSetValue(ctx->frameRateRow, settingsui::formatNumeric(ctx->fps, settingsui::kBgAnimFpsSpec).c_str());
    }
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
    }
}

// ---- All screens --------------------------------------------------------------

// The toggle widget owns its own On/Off text (SettingsRows.h), so this is a
// notification only.
void allScreensOnToggle(void *user, bool value) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    ctx->allScreens = value;
    ctx->allScreensTouched = true;
    controller.getSettings().setBgAnimAllScreens(value);
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
    }
}

// ---- Theme --------------------------------------------------------------------

void themeOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    ctx->themeMode = settingsui::wrapIndex(ctx->themeMode, kThemeModeCount, dir);
    ctx->themeModeTouched = true;
    controller.getSettings().setThemeMode(ctx->themeMode);
    if (ctx->themeRow != nullptr) {
        settingsRowSetValue(ctx->themeRow, settingsui::kThemeModeLabels[ctx->themeMode]);
    }
    if (ctx->ui != nullptr) {
        // change_color_theme runs on the next DefaultUI::updateState pass and
        // bumps eez_flow_get_selected_theme_index(); SettingsUI::service()
        // notices the mismatch and calls rebuildPage(), which recolours every
        // row on this page from the new theme_colors[...][0] (SettingsRows.cpp
        // themeFg()). No extra work needed here beyond the live write.
        ctx->ui->ui().markDirty();
    }
}

// ---- Gradient --------------------------------------------------------------

void gradientOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    Settings &settings = controller.getSettings();
    const auto choices = currentGradientChoices();
    ctx->gradientIndex = settingsui::wrapIndex(ctx->gradientIndex, static_cast<int>(choices.size()), dir);
    ctx->gradientRef = choices[static_cast<size_t>(ctx->gradientIndex)].ref;
    ctx->gradientTouched[static_cast<size_t>(ctx->animId)] = true;
    ctx->gradientLastRef[static_cast<size_t>(ctx->animId)] = ctx->gradientRef;
    {
        // Read-modify-write of the whole map string: guarded so a web save's
        // batchUpdate touching a different animation's slot in the same
        // string cannot interleave with this and lose one side's edit
        // (Settings.h: "Property::get/set stay lock-free; this only orders
        // whole transactions" -- every other row here is a single
        // Property::set, but this one reads the string before it writes it).
        Settings::Guard guard(settings);
        const std::string map(settings.getBgAnimThemeMap().c_str());
        settings.setBgAnimThemeMap(settingsui::gradientMapWriteRef(map, ctx->animId, ctx->gradientRef).c_str());
    }
    if (ctx->gradientRow != nullptr) {
        settingsRowSetValue(ctx->gradientRow, gradientDisplayText(ctx->gradientIndex, choices).c_str());
    }
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
        ctx->ui->plugins().trigger("bganim:preview-end");
    }
}

// ---- Plates --------------------------------------------------------------

void platesOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    ctx->plates = settingsui::wrapIndex(ctx->plates, kPlatesCount, dir);
    ctx->platesTouched = true;
    controller.getSettings().setBgAnimClearPlates(ctx->plates);
    if (ctx->platesRow != nullptr) {
        settingsRowSetValue(ctx->platesRow, settingsui::kPlatesLabels[ctx->plates]);
    }
    const bool custom = ctx->plates == 2;
    settingsRowSetEnabled(ctx->plateColorRow, custom);
    settingsRowSetEnabled(ctx->plateOpacityRow, custom);
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
    }
}

void plateColorOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    const int count = settingsui::paletteChoiceCount(ctx->plateColorAnchor);
    ctx->plateColorIndex = settingsui::wrapIndex(ctx->plateColorIndex, count, dir);
    ctx->plateColor = settingsui::paletteChoiceColor(ctx->plateColorIndex, ctx->plateColorAnchor);
    ctx->plateColorTouched = true;
    controller.getSettings().setBgAnimPlateColor(ctx->plateColor);
    if (ctx->plateColorRow != nullptr) {
        settingsRowSetValue(ctx->plateColorRow,
                             settingsui::paletteChoiceLabel(ctx->plateColorIndex, ctx->plateColorAnchor).c_str());
    }
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
    }
}

void plateOpacityOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    ctx->plateOpacity = settingsui::stepValue(ctx->plateOpacity, dir, fast, settingsui::kBgAnimPlateOpacitySpec);
    ctx->plateOpacityTouched = true;
    controller.getSettings().setBgAnimPlateOpacity(static_cast<int>(ctx->plateOpacity));
    if (ctx->plateOpacityRow != nullptr) {
        settingsRowSetValue(ctx->plateOpacityRow,
                             settingsui::formatNumeric(ctx->plateOpacity, settingsui::kBgAnimPlateOpacitySpec).c_str());
    }
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
    }
}

// ---- Element tint / Tint colour --------------------------------------------

void tintEnabledOnToggle(void *user, bool value) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    ctx->tintEnabled = value;
    ctx->tintEnabledTouched = true;
    controller.getSettings().setElementTintEnabled(value);
    settingsRowSetEnabled(ctx->tintColorRow, value);
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
    }
}

void tintColorOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    const int count = settingsui::paletteChoiceCount(ctx->tintColorAnchor);
    ctx->tintColorIndex = settingsui::wrapIndex(ctx->tintColorIndex, count, dir);
    ctx->tintColor = settingsui::paletteChoiceColor(ctx->tintColorIndex, ctx->tintColorAnchor);
    ctx->tintColorTouched = true;
    controller.getSettings().setElementTintColor(ctx->tintColor);
    if (ctx->tintColorRow != nullptr) {
        settingsRowSetValue(ctx->tintColorRow,
                             settingsui::paletteChoiceLabel(ctx->tintColorIndex, ctx->tintColorAnchor).c_str());
    }
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
    }
}

// ---- Text scrim --------------------------------------------------------------

void scrimOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    ctx->scrim = settingsui::stepValue(ctx->scrim, dir, fast, settingsui::kBgAnimScrimSpec);
    ctx->scrimTouched = true;
    controller.getSettings().setBgAnimScrim(static_cast<int>(ctx->scrim));
    if (ctx->scrimRow != nullptr) {
        settingsRowSetValue(ctx->scrimRow, settingsui::formatNumeric(ctx->scrim, settingsui::kBgAnimScrimSpec).c_str());
    }
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
    }
}

// ---- Fade out / Fade in / Fade curve ----------------------------------------
//
// The next screen change uses the new value: DefaultUI reads the settings
// when it starts a transition, so there is nothing to apply here beyond the
// write. The page arrows on this very page are a screen change too, so the
// fade can be tried without leaving the category.

void fadeOutOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    ctx->fadeOut = settingsui::stepValue(ctx->fadeOut, dir, fast, settingsui::kBgFadeSpec);
    ctx->fadeOutTouched = true;
    controller.getSettings().setBgFadeOutMs(static_cast<int>(ctx->fadeOut));
    if (ctx->fadeOutRow != nullptr) {
        settingsRowSetValue(ctx->fadeOutRow, settingsui::formatNumeric(ctx->fadeOut, settingsui::kBgFadeSpec).c_str());
    }
}

void fadeInOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    ctx->fadeIn = settingsui::stepValue(ctx->fadeIn, dir, fast, settingsui::kBgFadeSpec);
    ctx->fadeInTouched = true;
    controller.getSettings().setBgFadeInMs(static_cast<int>(ctx->fadeIn));
    if (ctx->fadeInRow != nullptr) {
        settingsRowSetValue(ctx->fadeInRow, settingsui::formatNumeric(ctx->fadeIn, settingsui::kBgFadeSpec).c_str());
    }
}

void fadeCurveOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    ctx->fadeCurve = settingsui::wrapIndex(ctx->fadeCurve, kFadeCurveCount, dir);
    ctx->fadeCurveTouched = true;
    controller.getSettings().setBgFadeCurve(ctx->fadeCurve);
    if (ctx->fadeCurveRow != nullptr) {
        settingsRowSetValue(ctx->fadeCurveRow, settingsui::kFadeCurveLabels[ctx->fadeCurve]);
    }
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty(); // updateState hands the curve to the render task
    }
}

// ---- Interlace ------------------------------------------------------------------

void interlaceOnToggle(void *user, bool value) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    ctx->interlace = value;
    ctx->interlaceTouched = true;
    controller.getSettings().setBgAnimInterlace(value ? 1 : 0);
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
    }
}

// ---- shell plumbing ---------------------------------------------------------

int animRowCount(void * /*ctx*/) { return 17; }

void animBuildRow(void *ctx0, int index, lv_obj_t *parent, SettingsUI &ui) {
    auto *ctx = static_cast<CatAnimationCtx *>(ctx0);
    if (ctx->ui == nullptr) {
        ctx->ui = &ui;
    }
    switch (index) {
    case 0: { // Animation
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Animation", "Animation", animIdOnCycle, ctx);
        ctx->animRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->animRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, animNameFn(ctx->animId));
        break;
    }
    case 1: { // Standby anim
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Standby anim", "Standby anim", standbyAnimOnCycle, ctx);
        ctx->standbyRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->standbyRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, standbyChoiceLabel(ctx->standbyAnimId));
        break;
    }
    case 2: { // Parameters (pushes CatAnimParams.cpp's page)
        lv_obj_t *row = settingsRowActionCreate(ui, parent, "Parameters", "Parameters", paramsOnActivate, ctx);
        ctx->paramsRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->paramsRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        // The animation's name, not a count: the page edits one animation's
        // parameters and this row sits under the one that chooses which.
        settingsRowSetValue(row, animNameFn(ctx->animId));
        break;
    }
    case 3: { // Frame rate
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Frame rate", "Frame rate", frameRateOnStep, ctx);
        ctx->frameRateRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->frameRateRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->fps, settingsui::kBgAnimFpsSpec).c_str());
        break;
    }
    case 4: // All screens
        settingsRowToggleCreate(ui, parent, "All screens", "All screens", ctx->allScreens, allScreensOnToggle, ctx);
        break;
    case 5: { // Theme
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Theme", "Theme", themeOnCycle, ctx);
        ctx->themeRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->themeRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::kThemeModeLabels[ctx->themeMode]);
        break;
    }
    case 6: { // Gradient
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Gradient", "Gradient", gradientOnCycle, ctx);
        ctx->gradientRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->gradientRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        const auto choices = currentGradientChoices();
        settingsRowSetValue(row, gradientDisplayText(ctx->gradientIndex, choices).c_str());
        break;
    }
    case 7: { // Plates
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Plates", "Plates", platesOnCycle, ctx);
        ctx->platesRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->platesRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::kPlatesLabels[ctx->plates]);
        break;
    }
    case 8: { // Plate colour
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Plate colour", "Plate colour", plateColorOnCycle, ctx);
        ctx->plateColorRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->plateColorRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::paletteChoiceLabel(ctx->plateColorIndex, ctx->plateColorAnchor).c_str());
        settingsRowSetEnabled(row, ctx->plates == 2);
        break;
    }
    case 9: { // Plate opacity
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Plate opacity", "Plate opacity", plateOpacityOnStep, ctx);
        ctx->plateOpacityRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->plateOpacityRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->plateOpacity, settingsui::kBgAnimPlateOpacitySpec).c_str());
        settingsRowSetEnabled(row, ctx->plates == 2);
        break;
    }
    case 10: // Element tint
        settingsRowToggleCreate(ui, parent, "Element tint", "Element tint", ctx->tintEnabled, tintEnabledOnToggle, ctx);
        break;
    case 11: { // Tint colour
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Tint colour", "Tint colour", tintColorOnCycle, ctx);
        ctx->tintColorRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->tintColorRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::paletteChoiceLabel(ctx->tintColorIndex, ctx->tintColorAnchor).c_str());
        settingsRowSetEnabled(row, ctx->tintEnabled);
        break;
    }
    case 12: { // Text scrim
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Text scrim", "Text scrim", scrimOnStep, ctx);
        ctx->scrimRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->scrimRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->scrim, settingsui::kBgAnimScrimSpec).c_str());
        break;
    }
    case 13: { // Fade out
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Fade out", "Fade out", fadeOutOnStep, ctx);
        ctx->fadeOutRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->fadeOutRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->fadeOut, settingsui::kBgFadeSpec).c_str());
        break;
    }
    case 14: { // Fade in
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Fade in", "Fade in", fadeInOnStep, ctx);
        ctx->fadeInRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->fadeInRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->fadeIn, settingsui::kBgFadeSpec).c_str());
        break;
    }
    case 15: { // Fade curve
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Fade curve", "Fade curve", fadeCurveOnCycle, ctx);
        ctx->fadeCurveRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->fadeCurveRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::kFadeCurveLabels[ctx->fadeCurve]);
        break;
    }
    case 16: // Interlace
        settingsRowToggleCreate(ui, parent, "Interlace", "Interlace", ctx->interlace, interlaceOnToggle, ctx);
        break;
    default:
        break;
    }
}

// Snapshots Settings into the draft, under the shell's Settings::Guard
// (SettingsUI::pushPage). Runs once, before any row exists, so it cannot
// read anything back out of ctx's row pointers.
void animEnter(void *ctx0) {
    auto *ctx = static_cast<CatAnimationCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    ctx->animId = clampAnimId(settings.getBgAnimId());
    ctx->standbyAnimId = clampStandbyAnimId(settings.getBgAnimStandbyId());
    ctx->fps = settings.getBgAnimFps();
    ctx->allScreens = settings.isBgAnimAllScreens();
    ctx->themeMode = settings.getThemeMode();
    ctx->plates = settings.getBgAnimClearPlates();
    ctx->plateColorAnchor = settings.getBgAnimPlateColor();
    ctx->plateColorIndex = settingsui::paletteCurrentIndex(ctx->plateColorAnchor);
    ctx->plateColor = ctx->plateColorAnchor;
    ctx->plateOpacity = settings.getBgAnimPlateOpacity();
    ctx->tintEnabled = settings.getElementTintEnabled();
    ctx->tintColorAnchor = settings.getElementTintColor();
    ctx->tintColorIndex = settingsui::paletteCurrentIndex(ctx->tintColorAnchor);
    ctx->tintColor = ctx->tintColorAnchor;
    ctx->scrim = settings.getBgAnimScrim();
    ctx->fadeOut = settingsui::clampOrWrap(settings.getBgFadeOutMs(), settingsui::kBgFadeSpec);
    ctx->fadeIn = settingsui::clampOrWrap(settings.getBgFadeInMs(), settingsui::kBgFadeSpec);
    ctx->fadeCurve = settings.getBgFadeCurve() != 0 ? 1 : 0;
    ctx->interlace = settings.getBgAnimInterlace() != 0;

    const auto choices = currentGradientChoices();
    ctx->gradientIndex = gradientIndexForAnim(ctx->animId, choices);
    ctx->gradientRef = choices[static_cast<size_t>(ctx->gradientIndex)].ref;

    ctx->animIdTouched = false;
    ctx->standbyAnimIdTouched = false;
    ctx->fpsTouched = false;
    ctx->allScreensTouched = false;
    ctx->themeModeTouched = false;
    ctx->gradientTouched.assign(static_cast<size_t>(animCountFn()), false);
    ctx->gradientLastRef.assign(static_cast<size_t>(animCountFn()), std::string());
    ctx->platesTouched = false;
    ctx->plateColorTouched = false;
    ctx->plateOpacityTouched = false;
    ctx->tintEnabledTouched = false;
    ctx->tintColorTouched = false;
    ctx->scrimTouched = false;
    ctx->fadeOutTouched = false;
    ctx->fadeInTouched = false;
    ctx->fadeCurveTouched = false;
    ctx->interlaceTouched = false;
}

// After a settings:changed event: refreshes only the fields this visit has
// not edited. service() rebuilds the page unconditionally right after
// calling this, so the redraw (including row enable/disable) is the shell's
// job by way of a fresh buildRow pass; this just has to leave the draft
// holding the values that pass should show.
void animReconcile(void *ctx0) {
    auto *ctx = static_cast<CatAnimationCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    if (!ctx->animIdTouched) {
        ctx->animId = clampAnimId(settings.getBgAnimId());
    }
    if (!ctx->standbyAnimIdTouched) {
        ctx->standbyAnimId = clampStandbyAnimId(settings.getBgAnimStandbyId());
    }
    if (!ctx->fpsTouched) {
        ctx->fps = settings.getBgAnimFps();
    }
    if (!ctx->allScreensTouched) {
        ctx->allScreens = settings.isBgAnimAllScreens();
    }
    if (!ctx->themeModeTouched) {
        ctx->themeMode = settings.getThemeMode();
    }
    if (!ctx->platesTouched) {
        ctx->plates = settings.getBgAnimClearPlates();
    }
    if (!ctx->plateColorTouched) {
        ctx->plateColorAnchor = settings.getBgAnimPlateColor();
        ctx->plateColorIndex = settingsui::paletteCurrentIndex(ctx->plateColorAnchor);
        ctx->plateColor = ctx->plateColorAnchor;
    }
    if (!ctx->plateOpacityTouched) {
        ctx->plateOpacity = settings.getBgAnimPlateOpacity();
    }
    if (!ctx->tintEnabledTouched) {
        ctx->tintEnabled = settings.getElementTintEnabled();
    }
    if (!ctx->tintColorTouched) {
        ctx->tintColorAnchor = settings.getElementTintColor();
        ctx->tintColorIndex = settingsui::paletteCurrentIndex(ctx->tintColorAnchor);
        ctx->tintColor = ctx->tintColorAnchor;
    }
    if (!ctx->scrimTouched) {
        ctx->scrim = settings.getBgAnimScrim();
    }
    if (!ctx->fadeOutTouched) {
        ctx->fadeOut = settingsui::clampOrWrap(settings.getBgFadeOutMs(), settingsui::kBgFadeSpec);
    }
    if (!ctx->fadeInTouched) {
        ctx->fadeIn = settingsui::clampOrWrap(settings.getBgFadeInMs(), settingsui::kBgFadeSpec);
    }
    if (!ctx->fadeCurveTouched) {
        ctx->fadeCurve = settings.getBgFadeCurve() != 0 ? 1 : 0;
    }
    if (!ctx->interlaceTouched) {
        ctx->interlace = settings.getBgAnimInterlace() != 0;
    }

    // Gradient row on screen: shows this visit's touched choice for
    // ctx->animId if there is one, otherwise re-derives fresh from the map
    // (which above may itself have just moved if Animation was untouched).
    // A web save's map replacement never overwrites a touched animation's
    // slot here or in commit(); only untouched animations re-read the map.
    const auto choices = currentGradientChoices();
    if (ctx->gradientTouched[static_cast<size_t>(ctx->animId)]) {
        ctx->gradientIndex =
            settingsui::gradientChoiceIndexForRef(choices, ctx->gradientLastRef[static_cast<size_t>(ctx->animId)]);
    } else {
        ctx->gradientIndex = gradientIndexForAnim(ctx->animId, choices);
    }
    ctx->gradientRef = choices[static_cast<size_t>(ctx->gradientIndex)].ref;
}

void animCommit(void *ctx0) {
    auto *ctx = static_cast<CatAnimationCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    char log[256];
    int used = std::snprintf(log, sizeof(log), "SettingsAnimation: committed");
    bool wrote = false;

    if (ctx->animIdTouched && settings.getBgAnimId() != ctx->animId) {
        settings.setBgAnimId(ctx->animId);
        settingsLogAppend(log, sizeof(log), used, " anim=%d", ctx->animId);
        wrote = true;
    }
    if (ctx->standbyAnimIdTouched && settings.getBgAnimStandbyId() != ctx->standbyAnimId) {
        settings.setBgAnimStandbyId(ctx->standbyAnimId);
        settingsLogAppend(log, sizeof(log), used, " standbyAnim=%d", ctx->standbyAnimId);
        wrote = true;
    }
    if (ctx->fpsTouched && settings.getBgAnimFps() != static_cast<int>(ctx->fps)) {
        settings.setBgAnimFps(static_cast<int>(ctx->fps));
        settingsLogAppend(log, sizeof(log), used, " fps=%ld", ctx->fps);
        wrote = true;
    }
    if (ctx->allScreensTouched && settings.isBgAnimAllScreens() != ctx->allScreens) {
        settings.setBgAnimAllScreens(ctx->allScreens);
        settingsLogAppend(log, sizeof(log), used, " allScreens=%d", ctx->allScreens ? 1 : 0);
        wrote = true;
    }
    if (ctx->themeModeTouched && settings.getThemeMode() != ctx->themeMode) {
        settings.setThemeMode(ctx->themeMode);
        settingsLogAppend(log, sizeof(log), used, " theme=%d", ctx->themeMode);
        wrote = true;
    }
    {
        // Re-assert every touched animation's gradient ref, not just the
        // last one touched: the map is one field per animation, and a web
        // save replaces the whole string, so a web save landing on animId A
        // while this visit's last edit was to animId B must not cost A its
        // touched value at commit (gm-flw.9 review). Already inside the
        // shell's Settings::Guard (SettingsUI::popPage/teardownAll wrap the
        // whole commit() call), so no separate guard is needed here the way
        // gradientOnCycle needs its own.
        std::string map(settings.getBgAnimThemeMap().c_str());
        bool gradientWrote = false;
        for (size_t animId = 0; animId < ctx->gradientTouched.size(); ++animId) {
            if (!ctx->gradientTouched[animId]) {
                continue;
            }
            const std::string &wanted = ctx->gradientLastRef[animId];
            if (settingsui::gradientMapReadRef(map, static_cast<int>(animId)) == wanted) {
                continue;
            }
            map = settingsui::gradientMapWriteRef(map, static_cast<int>(animId), wanted);
            settingsLogAppend(log, sizeof(log), used, " gradient[%d]=%s", static_cast<int>(animId),
                                   wanted.c_str());
            gradientWrote = true;
        }
        if (gradientWrote) {
            settings.setBgAnimThemeMap(map.c_str());
            wrote = true;
        }
    }
    if (ctx->platesTouched && settings.getBgAnimClearPlates() != ctx->plates) {
        settings.setBgAnimClearPlates(ctx->plates);
        settingsLogAppend(log, sizeof(log), used, " plates=%d", ctx->plates);
        wrote = true;
    }
    if (ctx->plateColorTouched && settings.getBgAnimPlateColor() != ctx->plateColor) {
        settings.setBgAnimPlateColor(ctx->plateColor);
        settingsLogAppend(log, sizeof(log), used, " plateColor=%06x", ctx->plateColor);
        wrote = true;
    }
    if (ctx->plateOpacityTouched && settings.getBgAnimPlateOpacity() != static_cast<int>(ctx->plateOpacity)) {
        settings.setBgAnimPlateOpacity(static_cast<int>(ctx->plateOpacity));
        settingsLogAppend(log, sizeof(log), used, " plateOpacity=%ld", ctx->plateOpacity);
        wrote = true;
    }
    if (ctx->tintEnabledTouched && settings.getElementTintEnabled() != ctx->tintEnabled) {
        settings.setElementTintEnabled(ctx->tintEnabled);
        settingsLogAppend(log, sizeof(log), used, " tintEnabled=%d", ctx->tintEnabled ? 1 : 0);
        wrote = true;
    }
    if (ctx->tintColorTouched && settings.getElementTintColor() != ctx->tintColor) {
        settings.setElementTintColor(ctx->tintColor);
        settingsLogAppend(log, sizeof(log), used, " tintColor=%06x", ctx->tintColor);
        wrote = true;
    }
    if (ctx->scrimTouched && settings.getBgAnimScrim() != static_cast<int>(ctx->scrim)) {
        settings.setBgAnimScrim(static_cast<int>(ctx->scrim));
        settingsLogAppend(log, sizeof(log), used, " scrim=%ld", ctx->scrim);
        wrote = true;
    }
    if (ctx->fadeOutTouched && settings.getBgFadeOutMs() != static_cast<int>(ctx->fadeOut)) {
        settings.setBgFadeOutMs(static_cast<int>(ctx->fadeOut));
        settingsLogAppend(log, sizeof(log), used, " fadeOut=%ld", ctx->fadeOut);
        wrote = true;
    }
    if (ctx->fadeInTouched && settings.getBgFadeInMs() != static_cast<int>(ctx->fadeIn)) {
        settings.setBgFadeInMs(static_cast<int>(ctx->fadeIn));
        settingsLogAppend(log, sizeof(log), used, " fadeIn=%ld", ctx->fadeIn);
        wrote = true;
    }
    if (ctx->fadeCurveTouched && settings.getBgFadeCurve() != ctx->fadeCurve) {
        settings.setBgFadeCurve(ctx->fadeCurve);
        settingsLogAppend(log, sizeof(log), used, " fadeCurve=%d", ctx->fadeCurve);
        wrote = true;
    }
    if (ctx->interlaceTouched && (settings.getBgAnimInterlace() != 0) != ctx->interlace) {
        settings.setBgAnimInterlace(ctx->interlace ? 1 : 0);
        settingsLogAppend(log, sizeof(log), used, " interlace=%d", ctx->interlace ? 1 : 0);
        wrote = true;
    }
    (void)used;

    if (!wrote) {
        return;
    }
    ESP_LOGI("SettingsUI", "%s", log);
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
        ctx->ui->plugins().trigger("settings:changed");
    }
}

void *animCreateCtx() { return new CatAnimationCtx(); }

void animDestroyCtx(void *ctx) { delete static_cast<CatAnimationCtx *>(ctx); }

} // namespace

// The roster, for the Parameters page (CatAnimParams.h). Exported from here
// rather than duplicated there because this file already owns the split
// between the real registry and the simulator's mirror.
int settingsAnimCount() { return animCountFn(); }
const char *settingsAnimName(int animId) { return animNameFn(clampAnimId(animId)); }
const BgAnimParamDef *settingsAnimParams(int animId) { return animParamsFn(clampAnimId(animId)); }

const SettingsCategoryDef kCatAnimation = {
    "Animation", &img_settings_40x40, animRowCount, animBuildRow, animEnter, nullptr, animCommit, animReconcile,
    animCreateCtx, animDestroyCtx,
};
