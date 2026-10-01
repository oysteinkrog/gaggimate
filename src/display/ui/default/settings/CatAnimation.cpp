// Animation category: which animation plays, which one the standby screen
// plays, the gradient they all draw with, then the parameters and the
// gradient override of one animation at a time (rows that push
// CatAnimParams.cpp's page, one for the main animation and one for the
// standby animation), frame rate, all-screens, the UI theme, plate handling,
// element tint, the text scrim, the screen fade (out, in, curve) and
// interlacing.
//
// The order is the point, and it is the epic's principle: the controls that
// apply to every animation come before the ones that override for a single
// animation. The global gradient row is the simple path, the per-animation
// Gradient rows read as "Global (<name>)" until they are changed, and the
// standby rows are disabled rather than hidden while the standby animation is
// "Same", so the page does not reflow under the finger when that row cycles.
//
// Every value row
// is live (SettingsUI.h): a row writes Settings and calls markDirty() the
// moment it changes, rather than waiting for commit, so
// DefaultUI::updateState applies it on the next rerender pass (CLAUDE.md,
// UI-pipeline invariants). What is left for commit() and reconcile() is the
// touched-field precedence rule: if a web save landed on a touched field
// while this page was open, re-assert this visit's value (gm-flw.9). Both go
// through reassertTouchedLiveFields below, because a field the display reads
// during the visit cannot wait for the exit (gm-nov3.39); the standby
// animation id, which it cannot read, is written by commit() alone.
#include "CatAnimParams.h"
#include "CatGradientPicker.h"
#include "GradientSwatch.h"
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
#include <cstdlib>
#include <string>
#include <vector>

#include "esp_log.h"

namespace {

// bg_animation()/bg_animation_count() (BgAnimRegistry.cpp) compile only
// outside GAGGIMATE_SIM: that file carries the animation render kernels and
// the simulator's stub SleepAnimation never calls them (CLAUDE.md:
// "SleepAnimation is a stub under GAGGIMATE_SIM and updateState skips
// animation and plate application"), so the whole translation unit is
// excluded there and neither symbol exists to link against on the host. This
// category still has to show real names on the sim (the bead's own acceptance
// criteria: "the model's animation names and gradient parsing are host code,
// so the sim shows them without a renderer"), so it keeps a mirror of the
// roster here rather than touching BgAnimRegistry.cpp, which carries the
// kernels the animation workers are editing. The mirror carries each
// animation's parameter table too, because the Parameters page (gm-3vj.2,
// CatAnimParams.cpp) shows one row per parameter and needs the labels and
// defaults, not just the names.
//
// The gradients are not in that boat any more: BgAnimThemes.cpp has no device
// dependency and compiles on the host since gm-nov3.3, so both builds read
// the real bg_theme_* table and the real ref validators.
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
    {"Brushed", {{"speed", "Speed", 50}, {"grain", "Grain", 35}, {"reflection", "Reflection", 45}, {"contrast", "Contrast", 30}, {"shine", "Shine", 50}, {"swell", "Swell", 50}, {"tilt", "Tilt", 50}, {"tone", "Base tone", 50}}},
    {"Horizon", {{"speed", "Speed", 50}, {"height", "Height", 45}, {"curvature", "Curvature", 35}, {"softness", "Softness", 60}, {"swell", "Swell", 50}, {"drift", "Drift", 50}, {"glow", "Glow", 50}, {"reflect", "Reflection", 50}}},
    {"Oculus", {{"speed", "Speed", 50}, {"diameter", "Diameter", 65}, {"breath", "Breath", 20}, {"edge", "Edge softness", 55}, {"glow", "Ring glow", 50}, {"halo", "Halo", 50}, {"ripple", "Ripple depth", 50}, {"waves", "Ripple count", 50}}},
    {"Chevrons", {{"speed", "Speed", 50}, {"spacing", "Spacing", 65}, {"angle", "Angle", 50}, {"contrast", "Contrast", 35}, {"round", "Roundness", 50}, {"swell", "Swell depth", 50}, {"swellw", "Swell width", 50}, {"highlight", "Highlight", 50}}},
    {"Mosaic", {{"speed", "Speed", 50}, {"size", "Tile size", 45}, {"contrast", "Contrast", 30}, {"variation", "Variation", 55}, {"bevel", "Bevel", 50}, {"wash", "Wash", 50}, {"brightness", "Brightness", 50}, {"washdensity", "Wash density", 50}}},
    {"Saddle", {{"speed", "Speed", 50}, {"curvature", "Curvature", 35}, {"drift", "Drift", 25}, {"contrast", "Contrast", 30}, {"breath", "Breathing", 50}, {"shoulder", "Shoulder", 50}, {"flow", "Contour flow", 50}, {"depth", "Contour depth", 100}}},
    {"Refraction", {{"speed", "Speed", 50}, {"bend", "Bend", 35}, {"width", "Channel width", 65}, {"contrast", "Contrast", 30}, {"glow", "Glow wave", 50}, {"darkness", "Darkness", 50}, {"ripple", "Ripple", 50}, {"flow", "Flow", 50}}},
    {"Sundial", {{"speed", "Speed", 50}, {"width", "Wedge width", 40}, {"contrast", "Contrast", 25}, {"shading", "Surface shading", 30}, {"breath", "Breath", 50}, {"surface", "Surface", 50}, {"softness", "Edge softness", 50}, {"tone", "Face tone", 50}}},
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
    {"Cube", {{"speed", "Speed", 50}, {"size", "Cube size", 50}, {"glow", "Face glow", 55}, {"tilt", "Tilt", 50}, {"wobble", "Wobble", 50}, {"edge", "Edge softness", 50}, {"backs", "Back faces", 50}, {"background", "Background", 50}}},
};

int animCountFn() { return static_cast<int>(sizeof(kSimAnims) / sizeof(kSimAnims[0])); }
const SimAnim &simAnim(int i) {
    const int n = animCountFn();
    return kSimAnims[(i >= 0 && i < n) ? i : 0];
}
const char *animNameFn(int i) { return simAnim(i).name; }
const BgAnimParamDef *animParamsFn(int i) { return simAnim(i).params; }
#else
int animCountFn() { return bg_animation_count(); }
const char *animNameFn(int i) { return bg_animation(i).name; }
const BgAnimParamDef *animParamsFn(int i) { return bg_animation(i).params; }
#endif

// Both builds read all six through BgAnimThemes.cpp, which clamps an
// out-of-range index to 0 in every one of them. That file is the one bganim
// translation unit with no device dependency, so the simulator links the real
// gradient rules rather than a stub (gm-nov3.3); before that it had to build
// this provider from the generated table instead.
settingsui::ThemeNameProvider makeThemeProvider() {
    settingsui::ThemeNameProvider p;
    p.count = bg_theme_count;
    p.name = bg_theme_name;
    p.category = bg_theme_category;
    p.categoryCount = bg_theme_category_count;
    p.categoryName = bg_theme_category_name;
    p.stops = bg_theme_stops;
    return p;
}

const settingsui::AnimationNameProvider kAnimProvider{animCountFn, animNameFn};
const settingsui::ThemeNameProvider kThemeProvider = makeThemeProvider();

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

// The global gradient's ref as this page sees it: the choice this visit made
// if it made one, else the stored field. Both readings below go through it,
// so the row, the swatches and the picker all describe the value commit will
// write rather than a web save that landed mid-visit (the touched-field
// precedence rule, CLAUDE.md). animReconcile puts that same value back into
// the stored field the panel draws from, so the page never describes one
// gradient while another is on screen (gm-nov3.23). Defined under
// CatAnimationCtx, which holds the draft.
std::string globalRefInEffect();

// True while the pre-library custom gradient is still what the global
// fallback draws: the ref in effect names nothing that exists and the legacy
// pair resolves to the custom string rather than to a built-in. Reachable
// whenever DefaultUI::migrateBgAnimGradients had to defer (BgAnimThemes.cpp
// lists the reasons), and the row has to say so rather than name a built-in.
bool legacyCustomFallbackActive(const std::vector<settingsui::GradientChoice> &choices) {
    Settings &settings = controller.getSettings();
    const std::string ref = globalRefInEffect();
    if (!ref.empty() && settingsui::gradientChoiceIndexForRef(choices, ref) != 0) {
        return false;
    }
    return bg_legacy_builtin(settings.getBgAnimTheme(), bg_custom_valid(settings.getBgAnimCustomTheme().c_str())) < 0;
}

// Where the global gradient sits in the choice list. The ref in effect when
// it names something that exists, else the built-in the legacy pair resolves
// to, which is what bg_resolve_anim_theme falls back to and what a device
// that has never set the global stores. Never returns 0: choices[0] is the
// per-animation "Global" entry, which is not a value the global itself can
// take.
//
// The legacy integer goes through bg_legacy_builtin rather than being used as
// a choice index directly: its namespace is frozen, so a stored 18 means the
// custom gradient and must never select the built-in that lands at index 18
// once the table is longer than 18 entries.
int globalGradientChoiceIndex(const std::vector<settingsui::GradientChoice> &choices) {
    Settings &settings = controller.getSettings();
    const std::string ref = globalRefInEffect();
    if (!ref.empty()) {
        const int i = settingsui::gradientChoiceIndexForRef(choices, ref);
        if (i != 0) {
            return i;
        }
    }
    const int legacy =
        bg_legacy_builtin(settings.getBgAnimTheme(), bg_custom_valid(settings.getBgAnimCustomTheme().c_str()));
    if (legacy >= 0) {
        const int i = settingsui::gradientChoiceIndexForRef(choices, std::to_string(legacy));
        if (i != 0) {
            return i;
        }
    }
    return choices.size() > 1 ? 1 : 0;
}

// What the "Gradient all" row shows for its current value. A retained legacy
// custom gradient has no choice entry of its own (it is not in the library
// yet, and it is not a built-in), so it is named for what it is. Read only:
// cycling the row moves to a real choice and never writes this back.
std::string globalGradientLabel(const std::vector<settingsui::GradientChoice> &choices) {
    if (legacyCustomFallbackActive(choices)) {
        return "Custom (legacy)";
    }
    return choices[static_cast<size_t>(globalGradientChoiceIndex(choices))].label;
}

// The one rollback-mirror rule, applied by both writers here. A built-in
// selection writes bgAnimTheme so a build without bgAnimGradientRef draws the
// same thing; an appended built-in mirrors as 0, and a library selection or
// an unresolvable ref leaves the legacy field alone. bg_legacy_mirror_for_ref
// (BgAnim.h) is the same policy the web form and the POST handler apply.
void mirrorGlobalRefIntoLegacyTheme(Settings &settings, const std::string &ref) {
    const int count = kThemeProvider.count ? kThemeProvider.count() : 0;
    const int mirror = bg_legacy_mirror_for_ref(ref.c_str(), count);
    if (mirror >= 0) {
        settings.setBgAnimTheme(mirror);
    }
}

// A per-animation Gradient row's value. Index 0 is "no override", which draws
// whatever the global row is set to, so it names it rather than saying
// "Default" and leaving the reader to find out where the default lives.
std::string gradientDisplayText(int index, const std::vector<settingsui::GradientChoice> &choices) {
    if (index == 0) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "Global (%s)", globalGradientLabel(choices).c_str());
        return buf;
    }
    return choices[static_cast<size_t>(index)].label;
}

// The choice index the map holds for the standby animation, or 0 while it is
// "Same as main": there is no separate slot to override then, because the
// standby screen draws the main animation with the main animation's gradient.
int standbyGradientIndexFor(int standbyAnimId, const std::vector<settingsui::GradientChoice> &choices) {
    if (standbyAnimId < 0) {
        return 0;
    }
    const std::string map(controller.getSettings().getBgAnimThemeMap().c_str());
    return settingsui::gradientChoiceIndexForRef(choices, settingsui::gradientMapReadRef(map, standbyAnimId));
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

    // The standby animation's own slot of the same map. It shares the two
    // vectors above rather than keeping a second pair, because it writes the
    // same per-animation field and the touched-ness that matters is the
    // animation's, not which row wrote it.
    int standbyGradientIndex = 0;
    std::string standbyGradientRef;

    // The global gradient, one field of its own, so one touched flag like
    // every other scalar row here.
    int globalGradientIndex = 0;
    std::string globalGradientRef;
    bool globalGradientTouched = false;

    // Which row opened the gradient picker, and the animation slot it was
    // opened for. Captured at the push and never re-derived while the picker
    // is open, so a web save that changes the main or the standby animation
    // cannot silently point the open picker at a different slot (gm-nov3.3).
    // One set of fields is enough: the shell's page stack holds at most one
    // picker.
    enum class PickerTarget { None, GlobalAll, Main, Standby };
    PickerTarget pickerTarget = PickerTarget::None;
    int pickerAnimId = -1;
    // Storage for the ref the picker reads back through its spec, which
    // returns a pointer the picker copies at once. A temporary's c_str()
    // would dangle before it got there.
    std::string pickerRefScratch;

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
    lv_obj_t *standbyParamsRow = nullptr;
    lv_obj_t *standbyGradientRow = nullptr;
    lv_obj_t *globalGradientRow = nullptr;
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

// The Animation category's ctx while its page is on the stack, null
// otherwise. Set and cleared by animCreateCtx/animDestroyCtx, which the shell
// runs on push and on pop, so at most one is ever live.
//
// globalRefInEffect needs it because two of the places that describe the
// global gradient are reached without a ctx: settingsGlobalGradientLabel and
// settingsGlobalGradientRef (CatGradientPicker.h), which the picker's
// "Global" entry reads. Without this the row, the swatch and the picker's
// marker showed the stored field while commit wrote the draft, so a web save
// during a visit made the display name one gradient and save another
// (gm-nov3.16). The per-animation rows never had that split: their draft is
// per animation id and draftRefForAnim below is the one reading of it.
CatAnimationCtx *gOpenAnimCtx = nullptr;

std::string globalRefInEffect() {
    if (gOpenAnimCtx != nullptr && gOpenAnimCtx->globalGradientTouched) {
        return gOpenAnimCtx->globalGradientRef;
    }
    return std::string(controller.getSettings().getBgAnimGradientRef().c_str());
}

// Defined with the three gradient rows below, where the picker wiring lives.
void applyRowSwatch(lv_obj_t *row, const std::string &ref);

// Brings the two standby rows in line with the current pair of ids: live
// while the standby animation is a different one, disabled and showing the
// main animation's own values otherwise. Called from both id rows, since
// either can make the ids meet or part.
void refreshStandbyRows(CatAnimationCtx *ctx, const std::vector<settingsui::GradientChoice> &choices) {
    const bool separate = ctx->standbyAnimId >= 0 && ctx->standbyAnimId != ctx->animId;
    const bool touched = ctx->standbyAnimId >= 0 &&
                         static_cast<size_t>(ctx->standbyAnimId) < ctx->gradientTouched.size() &&
                         ctx->gradientTouched[static_cast<size_t>(ctx->standbyAnimId)];
    if (touched) {
        ctx->standbyGradientIndex = settingsui::gradientChoiceIndexForRef(
            choices, ctx->gradientLastRef[static_cast<size_t>(ctx->standbyAnimId)]);
    } else {
        ctx->standbyGradientIndex = standbyGradientIndexFor(ctx->standbyAnimId, choices);
    }
    ctx->standbyGradientRef = choices[static_cast<size_t>(ctx->standbyGradientIndex)].ref;
    if (ctx->standbyParamsRow != nullptr) {
        settingsRowSetValue(ctx->standbyParamsRow, separate ? animNameFn(ctx->standbyAnimId) : "Same as main");
        settingsRowSetEnabled(ctx->standbyParamsRow, separate);
    }
    if (ctx->standbyGradientRow != nullptr) {
        settingsRowSetValue(ctx->standbyGradientRow,
                            separate ? gradientDisplayText(ctx->standbyGradientIndex, choices).c_str()
                                     : "Same as main");
        settingsRowSetEnabled(ctx->standbyGradientRow, separate);
        if (separate) {
            applyRowSwatch(ctx->standbyGradientRow, ctx->standbyGradientRef);
        } else {
            // "Same as main" is not a gradient, so the row shows no ramp.
            // applyEnabledRecurse dims labels and images by exact class and a
            // canvas is neither, so hiding it is what a disabled row needs.
            settingsRowSetSwatch(ctx->standbyGradientRow, nullptr);
        }
    }
}

// ---- the three gradient rows --------------------------------------------------
//
// Each carries three targets (gm-nov3.32): a centre band that pushes the
// picker (CatGradientPicker.h) and takes the chosen ref back through a
// callback, and a prev and a next arrow that step to the adjacent gradient
// without opening anything.
//
// The rows were prev/next cycles over one flat list until gm-nov3.3, which
// added the picker and took the arrows off, because a row-wide target laid
// over them is two targets in one place and Rig.audit() rejects the overlap.
// That reason was the target being row wide, not the arrows existing: the
// band is the row's left 200 px now, which is the width a choice row's text
// column has, so the three rectangles are ones the audit already passes.
// Both ways in survive, and they answer different questions: the arrows are
// for trying the next gradient, the picker for finding a particular one
// among sixty, where a name on its own stops saying what it looks like.
//
// The pick callbacks run while the picker is still the page on screen, so
// every row pointer here is null (each row's DELETE callback cleared it when
// the picker's push deleted this page's objects). They write the draft and
// Settings; the shell's popPages rebuilds this page from the draft on the way
// out, which is what puts the new value on the row.

// The ref this visit holds for one animation: the choice it made if it made
// one, else what the stored map says.
std::string draftRefForAnim(const CatAnimationCtx *ctx, int animId) {
    if (animId < 0 || static_cast<size_t>(animId) >= ctx->gradientTouched.size()) {
        return std::string();
    }
    if (ctx->gradientTouched[static_cast<size_t>(animId)]) {
        return ctx->gradientLastRef[static_cast<size_t>(animId)];
    }
    const std::string map(controller.getSettings().getBgAnimThemeMap().c_str());
    return settingsui::gradientMapReadRef(map, animId);
}

// Paints an entry row's swatch with the ramp the panel would build for `ref`,
// at the stored tone. An empty ref means "whatever the global draws", which is
// what a per-animation row on "Global" shows and what the global row itself
// asks for; settingsGlobalGradientSwatch answers it, legacy fallback included.
// A ref that resolves to nothing (a deleted library entry, an index from a
// longer table) leaves the swatch hidden rather than drawing a wrong one.
void applyRowSwatch(lv_obj_t *row, const std::string &ref) {
    if (row == nullptr) {
        return;
    }
    Settings &settings = controller.getSettings();
    settingsui::SwatchGradient gradient;
    const bool resolved =
        ref.empty() ? settingsGlobalGradientSwatch(gradient)
                    : settingsui::swatchResolveRef(ref.c_str(), settings.getBgAnimGradients().c_str(), gradient);
    if (!resolved) {
        settingsRowSetSwatch(row, nullptr);
        return;
    }
    settingsui::swatchApplyTone(gradient, settings.getBgAnimBrightness(), settings.getBgAnimHighlightKnee());
    uint16_t ramp[kSettingsRowSwatchSamples];
    settingsui::swatchBuildRamp565(gradient, ramp, kSettingsRowSwatchSamples);
    settingsRowSetSwatch(row, ramp);
}

void animReconcile(void *ctx0);
void animGradientPicked(void *user, const char *ref);

// ---- stepping through the gradients from the row (gm-nov3.32) ----------------
//
// The arrows walk the same flat list the picker shows, in the picker's own
// order, so somebody who steps to a gradient and then opens the picker finds
// the marker where they left it. CatGradientPicker's first page lists Global
// (per-animation rows only), then "My gradients", then each declared category
// that has built-ins; a group page lists its entries in table order. Flattened,
// that is exactly the loop below.
std::vector<int> gradientStepOrder(const std::vector<settingsui::GradientChoice> &choices, bool allowGlobal) {
    std::vector<int> order;
    if (allowGlobal) {
        // choices[0] is the "" entry, which the picker shows as "Global" and a
        // per-animation row can land on. The global row itself cannot: it is
        // what "Global" means, so its picker offers no such entry and neither
        // do its arrows.
        order.push_back(0);
    }
    for (int i : settingsui::gradientLibraryChoices(kThemeProvider, choices)) {
        order.push_back(i);
    }
    for (const settingsui::GradientGroup &group : settingsui::gradientBuiltinGroups(kThemeProvider, choices)) {
        for (int i : group.choices) {
            order.push_back(i);
        }
    }
    return order;
}

// The ref one step away from `ref` in that order, wrapping at both ends.
//
// A ref that is not a position in the list gets the end the arrow points from:
// the next arrow enters at the head and the prev arrow at the tail. Two states
// reach that path, and neither is a gradient the list contains: a per-animation
// slot naming a library entry that has since been deleted, and the global row
// while the retained pre-library custom gradient is still what it draws
// (gm-nov3.18). gradientChoiceIndexForRef is deliberately not used for the
// lookup, because it answers 0 ("Global") for a ref that names nothing, which
// would make a dangling ref step as though it were Global.
bool gradientStepRef(const std::vector<settingsui::GradientChoice> &choices, bool allowGlobal, const std::string &ref,
                     int dir, std::string &out) {
    const std::vector<int> order = gradientStepOrder(choices, allowGlobal);
    if (order.empty()) {
        return false;
    }
    int here = -1;
    for (size_t i = 0; i < choices.size(); i++) {
        if (choices[i].ref == ref) {
            here = static_cast<int>(i);
            break;
        }
    }
    int at = -1;
    for (size_t i = 0; here >= 0 && i < order.size(); i++) {
        if (order[i] == here) {
            at = static_cast<int>(i);
            break;
        }
    }
    const int n = static_cast<int>(order.size());
    const int to = at < 0 ? (dir > 0 ? 0 : n - 1) : settingsui::wrapIndex(at, n, dir);
    out = choices[static_cast<size_t>(order[static_cast<size_t>(to)])].ref;
    return true;
}

// Shared by all three rows: the picker's view of this category's draft.
// SettingsUI::service() reconciles only the top page, so while the picker is
// open this is the only thing keeping this category's untouched fields
// current, exactly as the schedule editor does for the Machine draft.
void gradientPickerReconcileParent(void *user) { animReconcile(user); }

// ---- Gradient for all animations ---------------------------------------------

// The rows a change to the global gradient also changes. A per-animation
// gradient row with no override of its own reads as "Global (<name>)" and
// samples what the global resolves to, so it is a second view of the value the
// "Gradient all" row holds and has to follow it.
//
// Called from globalGradientPicked, which both routes into the global go
// through. On the picker route this page's rows have already been deleted and
// the null checks skip them, and the pop rebuilds the page anyway. The arrow
// step is the route that needs it: nothing rebuilds anything after a step, and
// until gm-nov3.41 the two rows below the global went on naming the gradient it
// used to be. Each refresh writes exactly what animBuildRow writes for that
// row, so a stepped row and a rebuilt one show the same thing.
void refreshRowsReadingGlobalGradient(CatAnimationCtx *ctx) {
    const auto choices = currentGradientChoices();
    if (ctx->globalGradientRow != nullptr) {
        settingsRowSetValue(ctx->globalGradientRow, globalGradientLabel(choices).c_str());
        applyRowSwatch(ctx->globalGradientRow, std::string());
    }
    // Index 0 is the "Global" entry. A row holding an override of its own names
    // a gradient the global cannot move, so it is left alone.
    if (ctx->gradientRow != nullptr && ctx->gradientIndex == 0) {
        settingsRowSetValue(ctx->gradientRow, gradientDisplayText(ctx->gradientIndex, choices).c_str());
        applyRowSwatch(ctx->gradientRow, ctx->gradientRef);
    }
    const bool standbySeparate = ctx->standbyAnimId >= 0 && ctx->standbyAnimId != ctx->animId;
    if (ctx->standbyGradientRow != nullptr && standbySeparate && ctx->standbyGradientIndex == 0) {
        settingsRowSetValue(ctx->standbyGradientRow,
                            gradientDisplayText(ctx->standbyGradientIndex, choices).c_str());
        applyRowSwatch(ctx->standbyGradientRow, ctx->standbyGradientRef);
    }
}

// The gradient every animation draws with unless it has one of its own. The
// picker offers no "Global" entry here: the global is what "Global" means, so
// this row must land on a gradient.
void globalGradientPicked(void *user, const char *ref) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    const auto choices = currentGradientChoices();
    ctx->globalGradientRef = ref != nullptr ? ref : "";
    ctx->globalGradientIndex = settingsui::gradientChoiceIndexForRef(choices, ctx->globalGradientRef);
    ctx->globalGradientTouched = true;
    Settings &settings = controller.getSettings();
    settings.setBgAnimGradientRef(ctx->globalGradientRef.c_str());
    // A built-in is mirrored into bgAnimTheme, which is the last fallback and
    // what a build without this field reads. The web form and the POST
    // handler apply the same rule, so all four writers agree.
    mirrorGlobalRefIntoLegacyTheme(settings, ctx->globalGradientRef);
    refreshRowsReadingGlobalGradient(ctx);
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
        ctx->ui->plugins().trigger("bganim:preview-end");
    }
}

// The prev/next arrows on the same row (gm-nov3.32). The step writes exactly
// what a pick writes, through the same function, so the panel follows a tap
// the way it follows a pick and the touched-field rule of gm-nov3.23 applies
// to a stepped value as well. What a step also has to do, and a pick does not,
// is redraw the rows: a pick is followed by the picker's pop rebuilding this
// page from the draft, and nothing pops here. globalGradientPicked does that
// redraw for both routes (refreshRowsReadingGlobalGradient), because the rows
// below this one read the global too (gm-nov3.41).
void globalGradientOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    const auto choices = currentGradientChoices();
    // Step from what the row says, not from the raw ref: with no ref stored
    // the row names the built-in the legacy pair resolves to, and stepping
    // from the head of the list instead would skip whatever is on screen.
    const std::string from = legacyCustomFallbackActive(choices)
                                 ? std::string()
                                 : choices[static_cast<size_t>(globalGradientChoiceIndex(choices))].ref;
    std::string next;
    if (!gradientStepRef(choices, false, from, dir, next)) {
        return;
    }
    globalGradientPicked(ctx, next.c_str());
}

void globalGradientOnActivate(void *user) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    if (ctx->ui == nullptr) {
        return;
    }
    ctx->pickerTarget = CatAnimationCtx::PickerTarget::GlobalAll;
    ctx->pickerAnimId = -1;
    SettingsGradientPickerSpec spec;
    spec.title = "Gradient all";
    spec.allowGlobal = false;
    spec.currentRef = [](void *user) -> const char * {
        auto *c = static_cast<CatAnimationCtx *>(user);
        // What the global resolves to for this visit, which is "" while a
        // retained legacy custom gradient is still the fallback: that state is
        // not one of the picker's entries, so nothing is marked.
        c->pickerRefScratch = settingsGlobalGradientRef();
        return c->pickerRefScratch.c_str();
    };
    spec.onPick = globalGradientPicked;
    spec.onReconcileParent = gradientPickerReconcileParent;
    spec.stillValid = [](void *) { return true; }; // the global is always a slot worth editing
    spec.user = ctx;
    settingsGradientPickerPush(*ctx->ui, spec);
}

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
    // The standby rows are live only while the standby animation is a
    // different one, and cycling this row can make the two ids meet or part.
    refreshStandbyRows(ctx, choices);
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
    refreshStandbyRows(ctx, currentGradientChoices());
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

// ---- Standby parameters and standby gradient ---------------------------------

// Both rows edit the standby animation's own per-animation fields, which exist
// only while it is a different animation from the main one; the same id on
// both rows is one animation with one set of parameters. Disabled rather than
// hidden so the page keeps its shape while Standby anim is cycled.
void standbyParamsOnActivate(void *user) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    if (ctx->ui != nullptr && ctx->standbyAnimId >= 0) {
        settingsAnimParamsPush(*ctx->ui, ctx->standbyAnimId);
    }
}

void standbyGradientOnActivate(void *user) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    if (ctx->ui == nullptr || ctx->standbyAnimId < 0 || ctx->standbyAnimId == ctx->animId) {
        return;
    }
    ctx->pickerTarget = CatAnimationCtx::PickerTarget::Standby;
    ctx->pickerAnimId = ctx->standbyAnimId;
    SettingsGradientPickerSpec spec;
    spec.title = "Standby grad";
    spec.allowGlobal = true;
    spec.currentRef = [](void *user) -> const char * {
        auto *c = static_cast<CatAnimationCtx *>(user);
        c->pickerRefScratch = draftRefForAnim(c, c->pickerAnimId);
        return c->pickerRefScratch.c_str();
    };
    spec.onPick = animGradientPicked;
    spec.onReconcileParent = gradientPickerReconcileParent;
    // The standby animation's own slot exists only while it is a different
    // animation from the main one. A web save that turns standby off, or onto
    // the main animation, leaves this row disabled, so the picker over it has
    // nothing left to write.
    spec.stillValid = [](void *user) {
        auto *c = static_cast<CatAnimationCtx *>(user);
        return c->pickerAnimId >= 0 && c->pickerAnimId == c->standbyAnimId && c->standbyAnimId != c->animId;
    };
    spec.user = ctx;
    settingsGradientPickerPush(*ctx->ui, spec);
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

// One animation's gradient, written for whatever named the animation: the
// picker through the id it captured at the push, or an arrow through the id
// the row is showing. The bounds check is load bearing and not defensive
// habit: the draft's two per-animation vectors are sized to the live registry,
// and a stored bgAnimId from a longer one wrote past the end of them on the
// first Gradient arrow press once already (91cb0ed5). clampAnimId at enter and
// at reconcile is what keeps ctx->animId inside them; this is the second line.
void animGradientAssign(CatAnimationCtx *ctx, int animId, const char *ref) {
    if (animId < 0 || static_cast<size_t>(animId) >= ctx->gradientTouched.size()) {
        return;
    }
    Settings &settings = controller.getSettings();
    const std::string picked(ref != nullptr ? ref : "");
    ctx->gradientTouched[static_cast<size_t>(animId)] = true;
    ctx->gradientLastRef[static_cast<size_t>(animId)] = picked;
    {
        // Read-modify-write of the whole map string: guarded so a web save's
        // batchUpdate touching a different animation's slot in the same
        // string cannot interleave with this and lose one side's edit
        // (Settings.h: "Property::get/set stay lock-free; this only orders
        // whole transactions" -- every other row here is a single
        // Property::set, but this one reads the string before it writes it).
        Settings::Guard guard(settings);
        const std::string map(settings.getBgAnimThemeMap().c_str());
        settings.setBgAnimThemeMap(settingsui::gradientMapWriteRef(map, animId, picked).c_str());
    }
    // Whichever draft fields this animation feeds. Both when the main and the
    // standby rows are on the same animation, which is also when the standby
    // row is disabled and could not have opened the picker.
    const auto choices = currentGradientChoices();
    if (animId == ctx->animId) {
        ctx->gradientRef = picked;
        ctx->gradientIndex = settingsui::gradientChoiceIndexForRef(choices, picked);
    }
    if (animId == ctx->standbyAnimId) {
        ctx->standbyGradientRef = picked;
        ctx->standbyGradientIndex = settingsui::gradientChoiceIndexForRef(choices, picked);
    }
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
        ctx->ui->plugins().trigger("bganim:preview-end");
    }
}

// The picker's side: the animation is the one captured at the push, so a web
// save moving bgAnimId under an open picker cannot retarget it.
void animGradientPicked(void *user, const char *ref) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    animGradientAssign(ctx, ctx->pickerAnimId, ref);
}

// The main Gradient row's arrows (gm-nov3.32). The animation is ctx->animId,
// which clampAnimId has already brought inside the live registry, and which
// is also the animation the row is naming.
void gradientOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    const auto choices = currentGradientChoices();
    std::string next;
    if (!gradientStepRef(choices, true, ctx->gradientRef, dir, next)) {
        return;
    }
    animGradientAssign(ctx, ctx->animId, next.c_str());
    if (ctx->gradientRow != nullptr) {
        settingsRowSetValue(ctx->gradientRow, gradientDisplayText(ctx->gradientIndex, choices).c_str());
        applyRowSwatch(ctx->gradientRow, ctx->gradientRef);
    }
    // The standby rows read the same map, and when the two ids meet they read
    // the very slot this just wrote, exactly as after an Animation cycle.
    refreshStandbyRows(ctx, choices);
}

// The Standby grad row's arrows. Disabling a row already stops its arrows
// (settingsRowSetEnabled adds LV_STATE_DISABLED, which lv_obj_hit_test
// refuses), so this guard is the second line rather than the first.
void standbyGradientOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    if (ctx->standbyAnimId < 0 || ctx->standbyAnimId == ctx->animId) {
        return;
    }
    const auto choices = currentGradientChoices();
    std::string next;
    if (!gradientStepRef(choices, true, ctx->standbyGradientRef, dir, next)) {
        return;
    }
    animGradientAssign(ctx, ctx->standbyAnimId, next.c_str());
    if (ctx->standbyGradientRow != nullptr) {
        settingsRowSetValue(ctx->standbyGradientRow, gradientDisplayText(ctx->standbyGradientIndex, choices).c_str());
        applyRowSwatch(ctx->standbyGradientRow, ctx->standbyGradientRef);
    }
}

void gradientOnActivate(void *user) {
    auto *ctx = static_cast<CatAnimationCtx *>(user);
    if (ctx->ui == nullptr) {
        return;
    }
    ctx->pickerTarget = CatAnimationCtx::PickerTarget::Main;
    ctx->pickerAnimId = ctx->animId;
    SettingsGradientPickerSpec spec;
    spec.title = "Gradient";
    spec.allowGlobal = true;
    spec.currentRef = [](void *user) -> const char * {
        auto *c = static_cast<CatAnimationCtx *>(user);
        c->pickerRefScratch = draftRefForAnim(c, c->pickerAnimId);
        return c->pickerRefScratch.c_str();
    };
    spec.onPick = animGradientPicked;
    spec.onReconcileParent = gradientPickerReconcileParent;
    // The captured animation stays the one being edited even if a web save
    // moves bgAnimId underneath, the same rule the Parameters page follows.
    // Only an id that has left the roster makes the slot unwritable.
    spec.stillValid = [](void *user) {
        auto *c = static_cast<CatAnimationCtx *>(user);
        return c->pickerAnimId >= 0 && c->pickerAnimId < animCountFn();
    };
    spec.user = ctx;
    settingsGradientPickerPush(*ctx->ui, spec);
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

int animRowCount(void * /*ctx*/) { return 20; }

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
    case 2: { // Gradient for all animations (arrows step, the band pushes the picker)
        lv_obj_t *row =
            settingsRowSwatchStepCreate(ui, parent, "Gradient all", "Gradient all", globalGradientOnActivate,
                                        globalGradientOnCycle, ctx);
        ctx->globalGradientRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->globalGradientRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        const auto choices = currentGradientChoices();
        // globalGradientLabel, not the choice at globalGradientIndex: while a
        // legacy custom gradient is still the fallback there is no choice
        // that stands for it, and naming a built-in there would be a lie.
        settingsRowSetValue(row, globalGradientLabel(choices).c_str());
        // "" rather than the ref: the global's swatch and a per-animation
        // row on Global are the same question, and a retained legacy custom
        // gradient has no ref to pass (settingsGlobalGradientSwatch).
        applyRowSwatch(row, std::string());
        break;
    }
    case 3: { // Parameters (pushes CatAnimParams.cpp's page)
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
    case 4: { // Gradient (the main animation's override; arrows step, the band pushes the picker)
        lv_obj_t *row =
            settingsRowSwatchStepCreate(ui, parent, "Gradient", "Gradient", gradientOnActivate, gradientOnCycle, ctx);
        ctx->gradientRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->gradientRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        const auto choices = currentGradientChoices();
        settingsRowSetValue(row, gradientDisplayText(ctx->gradientIndex, choices).c_str());
        applyRowSwatch(row, ctx->gradientRef);
        break;
    }
    case 5: { // Standby parameters
        const bool separate = ctx->standbyAnimId >= 0 && ctx->standbyAnimId != ctx->animId;
        lv_obj_t *row =
            settingsRowActionCreate(ui, parent, "Standby params", "Standby params", standbyParamsOnActivate, ctx);
        ctx->standbyParamsRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->standbyParamsRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, separate ? animNameFn(ctx->standbyAnimId) : "Same as main");
        settingsRowSetEnabled(row, separate);
        break;
    }
    case 6: { // Standby gradient (arrows step, the band pushes the picker)
        const bool separate = ctx->standbyAnimId >= 0 && ctx->standbyAnimId != ctx->animId;
        lv_obj_t *row =
            settingsRowSwatchStepCreate(ui, parent, "Standby grad", "Standby grad", standbyGradientOnActivate,
                                        standbyGradientOnCycle, ctx);
        ctx->standbyGradientRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) {
                static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->standbyGradientRow = nullptr;
            },
            LV_EVENT_DELETE, ctx);
        const auto choices = currentGradientChoices();
        settingsRowSetValue(row, separate ? gradientDisplayText(ctx->standbyGradientIndex, choices).c_str()
                                          : "Same as main");
        settingsRowSetEnabled(row, separate);
        if (separate) {
            applyRowSwatch(row, ctx->standbyGradientRef);
        }
        break;
    }
    case 7: // All screens
        settingsRowToggleCreate(ui, parent, "All screens", "All screens", ctx->allScreens, allScreensOnToggle, ctx);
        break;
    case 8: { // Theme
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Theme", "Theme", themeOnCycle, ctx);
        ctx->themeRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->themeRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::kThemeModeLabels[ctx->themeMode]);
        break;
    }
    case 9: { // Frame rate
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Frame rate", "Frame rate", frameRateOnStep, ctx);
        ctx->frameRateRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->frameRateRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->fps, settingsui::kBgAnimFpsSpec).c_str());
        break;
    }
    case 10: { // Plates
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Plates", "Plates", platesOnCycle, ctx);
        ctx->platesRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->platesRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::kPlatesLabels[ctx->plates]);
        break;
    }
    case 11: { // Plate colour
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
    case 12: { // Plate opacity
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
    case 13: // Element tint
        settingsRowToggleCreate(ui, parent, "Element tint", "Element tint", ctx->tintEnabled, tintEnabledOnToggle, ctx);
        break;
    case 14: { // Tint colour
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
    case 15: { // Text scrim
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Text scrim", "Text scrim", scrimOnStep, ctx);
        ctx->scrimRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->scrimRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->scrim, settingsui::kBgAnimScrimSpec).c_str());
        break;
    }
    case 16: { // Fade out
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Fade out", "Fade out", fadeOutOnStep, ctx);
        ctx->fadeOutRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->fadeOutRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->fadeOut, settingsui::kBgFadeSpec).c_str());
        break;
    }
    case 17: { // Fade in
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Fade in", "Fade in", fadeInOnStep, ctx);
        ctx->fadeInRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->fadeInRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->fadeIn, settingsui::kBgFadeSpec).c_str());
        break;
    }
    case 18: { // Fade curve
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Fade curve", "Fade curve", fadeCurveOnCycle, ctx);
        ctx->fadeCurveRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->fadeCurveRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::kFadeCurveLabels[ctx->fadeCurve]);
        break;
    }
    case 19: // Interlace
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
    ctx->standbyGradientIndex = standbyGradientIndexFor(ctx->standbyAnimId, choices);
    ctx->standbyGradientRef = choices[static_cast<size_t>(ctx->standbyGradientIndex)].ref;
    ctx->globalGradientIndex = globalGradientChoiceIndex(choices);
    ctx->globalGradientRef = choices[static_cast<size_t>(ctx->globalGradientIndex)].ref;

    ctx->animIdTouched = false;
    ctx->standbyAnimIdTouched = false;
    ctx->fpsTouched = false;
    ctx->allScreensTouched = false;
    ctx->themeModeTouched = false;
    ctx->gradientTouched.assign(static_cast<size_t>(animCountFn()), false);
    ctx->gradientLastRef.assign(static_cast<size_t>(animCountFn()), std::string());
    ctx->globalGradientTouched = false;
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

// Every touched animation's draft ref merged into `map`, with every untouched
// slot left exactly as the string holds it. Returns true when the string
// changed, and names each slot it changed in `log` when one is given.
//
// Shared by animReconcile and animCommit so the rule that decides which slots
// survive a web save cannot drift between them: bgAnimThemeMap is one field
// per animation, a web save replaces the whole string in one write, and this
// visit owns only the slots it has touched.
bool mergeTouchedGradientSlots(const CatAnimationCtx *ctx, std::string &map, char *log, size_t logCap, int *used) {
    bool wrote = false;
    for (size_t animId = 0; animId < ctx->gradientTouched.size(); ++animId) {
        if (!ctx->gradientTouched[animId]) {
            continue;
        }
        const std::string &wanted = ctx->gradientLastRef[animId];
        if (settingsui::gradientMapReadRef(map, static_cast<int>(animId)) == wanted) {
            continue;
        }
        map = settingsui::gradientMapWriteRef(map, static_cast<int>(animId), wanted);
        if (log != nullptr && used != nullptr) {
            settingsLogAppend(log, logCap, *used, " gradient[%d]=%s", static_cast<int>(animId), wanted.c_str());
        }
        wrote = true;
    }
    return wrote;
}

// Every touched field this page writes that the display reads back while the
// page is still open, written over whatever a web save left in storage.
//
// This is the live-field rule (CLAUDE.md, "On-display settings"). A row here
// writes Settings the moment it changes and DefaultUI resolves the panel from
// the stored value on its next pass, so a web save landing mid-visit put the
// web's value on the panel while the row went on naming this visit's, with
// nothing on screen to say so, until commit moved it back at the exit. Fields
// the display cannot read during the visit are not in here: the standby
// animation id is the one this page writes that is not live (animCommit writes
// it on its own), because DefaultUI takes it only on the standby screen and
// the settings cover sits on the menu screen.
//
// The inventory, read off this file and DefaultUI.cpp on 2026-09-13. Keep it
// true when a row is added: written here / read there.
//
//   bgAnimId           714 / 4559  updateState, every pass
//   bgAnimFps          833 / 4589
//   bgAnimAllScreens   850 / 4543
//   themeMode          862 / 4929  applyTheme, called at 1967 every rerender
//   bgAnimThemeMap     902 / 4642
//   bgAnimGradientRef  644 / 4644  legacy mirror written at 291
//   bgAnimClearPlates 1003 / 2248  every pass while the animation runs
//   bgAnimPlateColor  1021 / 2249
//   bgAnimPlateOpacity 1035 / 2249
//   elementTintEnabled 1051 / 4940 and 4961
//   elementTintColor  1064 / 4940 and 4962
//   bgAnimScrim       1080 / 4680
//   bgFadeOutMs       1100 / 3873  through beginOverlayTransition (2128), and
//                                  pushing or popping a child page of this
//                                  category is a transition (SettingsUI.cpp
//                                  225 and 253), so it is read during a visit
//   bgFadeInMs        1110 / 3878  through 2173 and 3902
//   bgFadeCurve       1120 / 4592
//   bgAnimInterlace   1135 / 4591
//   bgAnimParams      CatAnimParams.cpp 138 / 4584, per slot, through
//                                  mergeTouchedSlots there
//
//   bgAnimStandbyId    761 / 4561  NOT live, see above
//
// Shared by animReconcile and animCommit so the two cannot disagree about
// which fields this visit owns, the same reason mergeTouchedGradientSlots is
// shared. Reconcile passes no log; commit names each field it wrote.
bool reassertTouchedLiveFields(CatAnimationCtx *ctx, Settings &settings, char *log, size_t logCap, int *used) {
    bool wrote = false;
    auto note = [&](const char *fmt, auto value) {
        if (log != nullptr && used != nullptr) {
            settingsLogAppend(log, logCap, *used, fmt, value);
        }
        wrote = true;
    };

    if (ctx->animIdTouched && settings.getBgAnimId() != ctx->animId) {
        settings.setBgAnimId(ctx->animId);
        note(" anim=%d", ctx->animId);
    }
    if (ctx->fpsTouched && settings.getBgAnimFps() != static_cast<int>(ctx->fps)) {
        settings.setBgAnimFps(static_cast<int>(ctx->fps));
        note(" fps=%ld", ctx->fps);
    }
    if (ctx->allScreensTouched && settings.isBgAnimAllScreens() != ctx->allScreens) {
        settings.setBgAnimAllScreens(ctx->allScreens);
        note(" allScreens=%d", ctx->allScreens ? 1 : 0);
    }
    if (ctx->themeModeTouched && settings.getThemeMode() != ctx->themeMode) {
        settings.setThemeMode(ctx->themeMode);
        note(" theme=%d", ctx->themeMode);
    }
    {
        // Every touched animation's gradient ref, not just the last one
        // touched: the map is one field per animation and a web save replaces
        // the whole string, so a save landing on animation A while this
        // visit's last edit was to B must not cost A its touched value
        // (gm-flw.9 review). The read-modify-write is already inside the
        // shell's Settings::Guard, which wraps both callers, so it takes none
        // of its own the way gradientOnCycle has to.
        std::string map(settings.getBgAnimThemeMap().c_str());
        if (mergeTouchedGradientSlots(ctx, map, log, logCap, used)) {
            settings.setBgAnimThemeMap(map.c_str());
            wrote = true;
        }
    }
    if (ctx->globalGradientTouched &&
        std::string(settings.getBgAnimGradientRef().c_str()) != ctx->globalGradientRef) {
        settings.setBgAnimGradientRef(ctx->globalGradientRef.c_str());
        mirrorGlobalRefIntoLegacyTheme(settings, ctx->globalGradientRef);
        note(" gradientAll=%s", ctx->globalGradientRef.c_str());
    }
    if (ctx->platesTouched && settings.getBgAnimClearPlates() != ctx->plates) {
        settings.setBgAnimClearPlates(ctx->plates);
        note(" plates=%d", ctx->plates);
    }
    if (ctx->plateColorTouched && settings.getBgAnimPlateColor() != ctx->plateColor) {
        settings.setBgAnimPlateColor(ctx->plateColor);
        note(" plateColor=%06x", ctx->plateColor);
    }
    if (ctx->plateOpacityTouched && settings.getBgAnimPlateOpacity() != static_cast<int>(ctx->plateOpacity)) {
        settings.setBgAnimPlateOpacity(static_cast<int>(ctx->plateOpacity));
        note(" plateOpacity=%ld", ctx->plateOpacity);
    }
    if (ctx->tintEnabledTouched && settings.getElementTintEnabled() != ctx->tintEnabled) {
        settings.setElementTintEnabled(ctx->tintEnabled);
        note(" tintEnabled=%d", ctx->tintEnabled ? 1 : 0);
    }
    if (ctx->tintColorTouched && settings.getElementTintColor() != ctx->tintColor) {
        settings.setElementTintColor(ctx->tintColor);
        note(" tintColor=%06x", ctx->tintColor);
    }
    if (ctx->scrimTouched && settings.getBgAnimScrim() != static_cast<int>(ctx->scrim)) {
        settings.setBgAnimScrim(static_cast<int>(ctx->scrim));
        note(" scrim=%ld", ctx->scrim);
    }
    // The two fade lengths are read at the start of every overlay transition
    // (DefaultUI::beginOverlayTransition), and pushing or popping a child page
    // of this category is one, so they are live during the visit like the rest.
    if (ctx->fadeOutTouched && settings.getBgFadeOutMs() != static_cast<int>(ctx->fadeOut)) {
        settings.setBgFadeOutMs(static_cast<int>(ctx->fadeOut));
        note(" fadeOut=%ld", ctx->fadeOut);
    }
    if (ctx->fadeInTouched && settings.getBgFadeInMs() != static_cast<int>(ctx->fadeIn)) {
        settings.setBgFadeInMs(static_cast<int>(ctx->fadeIn));
        note(" fadeIn=%ld", ctx->fadeIn);
    }
    if (ctx->fadeCurveTouched && settings.getBgFadeCurve() != ctx->fadeCurve) {
        settings.setBgFadeCurve(ctx->fadeCurve);
        note(" fadeCurve=%d", ctx->fadeCurve);
    }
    if (ctx->interlaceTouched && (settings.getBgAnimInterlace() != 0) != ctx->interlace) {
        settings.setBgAnimInterlace(ctx->interlace ? 1 : 0);
        note(" interlace=%d", ctx->interlace ? 1 : 0);
    }
    return wrote;
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

    // Every touched field the display reads while this page is open goes back
    // into storage here, not only at commit: the row writes Settings live and
    // the panel resolves from the stored value, so a web save that landed
    // mid-visit otherwise drew its own value behind a row still naming this
    // visit's, from the save until the exit (gm-nov3.23 for the global
    // gradient, gm-nov3.36 for the map slots, gm-nov3.39 for the rest).
    //
    // These are the same writes the rows themselves make, so this restores
    // live fields rather than changing the commit-time precedence the
    // category depends on (gm-nov3.16): commit still writes exactly what the
    // rows said, and an untouched field still adopts whatever the web posted.
    reassertTouchedLiveFields(ctx, settings, nullptr, 0, nullptr);

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

    // The standby animation's slot of the same map, by the same rule; the
    // row's own refresh is the shell's rebuildPage pass right after this.
    if (ctx->standbyAnimId >= 0 && ctx->gradientTouched[static_cast<size_t>(ctx->standbyAnimId)]) {
        ctx->standbyGradientIndex = settingsui::gradientChoiceIndexForRef(
            choices, ctx->gradientLastRef[static_cast<size_t>(ctx->standbyAnimId)]);
    } else {
        ctx->standbyGradientIndex = standbyGradientIndexFor(ctx->standbyAnimId, choices);
    }
    ctx->standbyGradientRef = choices[static_cast<size_t>(ctx->standbyGradientIndex)].ref;

    // The touched case needs nothing here: the row already holds this visit's
    // choice and reassertTouchedLiveFields above has put it back into the
    // stored field the panel draws from. service() runs this before
    // updateState() in the same UI pass, and updateState re-resolves the
    // palette only when the stored ref changes under it, so in the ordinary
    // case the render task never sees the web's ref at all.
    if (!ctx->globalGradientTouched) {
        ctx->globalGradientIndex = globalGradientChoiceIndex(choices);
        ctx->globalGradientRef = choices[static_cast<size_t>(ctx->globalGradientIndex)].ref;
    }
}

void animCommit(void *ctx0) {
    auto *ctx = static_cast<CatAnimationCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    char log[256];
    int used = std::snprintf(log, sizeof(log), "SettingsAnimation: committed");

    // Since gm-nov3.39 reconcile has usually written every live field back
    // already, so in the ordinary case this finds nothing to do. It stays
    // because a web save can land between the last reconcile and this call,
    // and because a visit with no web save in it never reconciles at all.
    bool wrote = reassertTouchedLiveFields(ctx, settings, log, sizeof(log), &used);

    // The standby animation id is the one touched field of this page that the
    // display cannot read while the cover is up: DefaultUI takes it only on
    // the standby screen (updateState), and the cover sits on the menu screen.
    // So it is not a live field, it is not re-asserted at reconcile, and it is
    // written here, at the exit, like an ordinary drafted field.
    if (ctx->standbyAnimIdTouched && settings.getBgAnimStandbyId() != ctx->standbyAnimId) {
        settings.setBgAnimStandbyId(ctx->standbyAnimId);
        settingsLogAppend(log, sizeof(log), used, " standbyAnim=%d", ctx->standbyAnimId);
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

void *animCreateCtx() {
    auto *ctx = new CatAnimationCtx();
    gOpenAnimCtx = ctx;
    return ctx;
}

void animDestroyCtx(void *ctx) {
    if (gOpenAnimCtx == ctx) {
        gOpenAnimCtx = nullptr;
    }
    delete static_cast<CatAnimationCtx *>(ctx);
}

} // namespace

// The roster, for the Parameters page (CatAnimParams.h). Exported from here
// rather than duplicated there because this file already owns the split
// between the real registry and the simulator's mirror.
// The built-in gradient table and the two readings of the global gradient
// the picker shows (CatGradientPicker.h). Exported from here for the same
// reason the roster is: this file already owns how the Animation category
// reads the stored gradient fields, and the picker must not grow a second
// reading of them.
const settingsui::ThemeNameProvider &settingsThemeProvider() { return kThemeProvider; }

// Both readings follow the Animation category's draft while it is open
// (globalRefInEffect), so the picker's "Global" entry names and samples the
// same gradient the category's own row shows.
std::string settingsGlobalGradientLabel() { return globalGradientLabel(currentGradientChoices()); }

std::string settingsGlobalGradientRef() {
    const auto choices = currentGradientChoices();
    if (legacyCustomFallbackActive(choices)) {
        return std::string(); // no ref names the retained legacy gradient
    }
    return choices[static_cast<size_t>(globalGradientChoiceIndex(choices))].ref;
}

// The gradient behind that ref, which a row can draw even when no ref names
// it. The retained legacy custom gradient is exactly that case: it is not a
// built-in and not in the library, so settingsGlobalGradientRef returns "",
// and until gm-nov3.18 every surface that asked for the global by ref drew no
// swatch at all. It is read through swatchFromLegacyCustom rather than
// swatchFromWire, because the resolver's last fallback drops the stored
// positions (GradientSwatch.h says why that changes the picture).
bool settingsGlobalGradientSwatch(settingsui::SwatchGradient &out) {
    Settings &settings = controller.getSettings();
    const std::string ref = settingsGlobalGradientRef();
    if (!ref.empty()) {
        return settingsui::swatchResolveRef(ref.c_str(), settings.getBgAnimGradients().c_str(), out);
    }
    return settingsui::swatchFromLegacyCustom(settings.getBgAnimCustomTheme().c_str(), out);
}

int settingsAnimCount() { return animCountFn(); }
const char *settingsAnimName(int animId) { return animNameFn(clampAnimId(animId)); }
const BgAnimParamDef *settingsAnimParams(int animId) { return animParamsFn(clampAnimId(animId)); }

const SettingsCategoryDef kCatAnimation = {
    "Animation", &img_settings_40x40, animRowCount, animBuildRow, animEnter, nullptr, animCommit, animReconcile,
    animCreateCtx, animDestroyCtx,
};
