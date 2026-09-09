// Animation category: background animation, frame rate, all-screens, the UI
// theme, the current animation's gradient, plate handling, element tint, the
// text scrim, the screen fade (out, in, curve) and interlacing. Every row is
// live (SettingsUI.h): a row writes Settings
// and calls markDirty() the moment it changes, rather than waiting for
// commit, so DefaultUI::updateState applies it on the next rerender pass
// (CLAUDE.md, UI-pipeline invariants). commit()'s only remaining job is the
// touched-field precedence rule: if a web save landed on a touched field
// while this page was open, re-assert this visit's value (gm-flw.9).
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
// without a renderer"), so it keeps a names-only mirror of both tables here
// rather than touching BgAnimRegistry.cpp/BgAnimThemes.cpp, which this bead
// does not own and which the asm-* workers are editing concurrently this
// wave. Append-only, same order as the real tables; verified against every
// AnimX.cpp's `.name` field and BgAnimThemes.cpp's THEMES array at HEAD
// 2b87cb88. A change to either real table needs the same edit made here.
#ifdef GAGGIMATE_SIM
constexpr const char *kSimAnimNames[] = {
    "Plasma", "Lava", "Silk", "Starfield", "Aurora", "Ripples", "Caustics", "Mandala",
    "Orbits", "Fireflies", "Steam", "Ember", "Nebula", "Silk 2", "Brushed Metal",
    "Quiet Horizon", "Soft Oculus", "Folded Chevron",
    "Quiet Mosaic", "Saddle", "Refraction",
    "Silent Sundial",
};
constexpr const char *kSimThemeNames[] = {
    "Espresso", "Ocean", "Violet Dusk", "Forest", "Sunset", "Fire", "Ice", "Mono", "Rose", "Gold", "Aurora", "Cyber",
    "Ember Coal", "Deep Space", "Teal Reef", "Sakura", "Lime", "Arctic Night",
};

int animCountFn() { return static_cast<int>(sizeof(kSimAnimNames) / sizeof(kSimAnimNames[0])); }
const char *animNameFn(int i) {
    const int n = animCountFn();
    return kSimAnimNames[(i >= 0 && i < n) ? i : 0];
}
int themeCountFn() { return static_cast<int>(sizeof(kSimThemeNames) / sizeof(kSimThemeNames[0])); }
const char *themeNameFn(int i) {
    const int n = themeCountFn();
    return kSimThemeNames[(i >= 0 && i < n) ? i : 0];
}
#else
int animCountFn() { return bg_animation_count(); }
const char *animNameFn(int i) { return bg_animation(i).name; }
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
    // All fifteen rows here are live, unlike Temps, so this is also how every
    // onChange callback reaches markDirty()/plugins().trigger(), not just
    // commit(). Every row on the current page runs buildRow before any of
    // these other callbacks can fire (enter -> rebuildPage -> buildRow,
    // always page 0 first), so this is set before any of them ever run.
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
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
        ctx->ui->plugins().trigger("bganim:preview-end");
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

int animRowCount(void * /*ctx*/) { return 15; }

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
    case 1: { // Frame rate
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Frame rate", "Frame rate", frameRateOnStep, ctx);
        ctx->frameRateRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->frameRateRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->fps, settingsui::kBgAnimFpsSpec).c_str());
        break;
    }
    case 2: // All screens
        settingsRowToggleCreate(ui, parent, "All screens", "All screens", ctx->allScreens, allScreensOnToggle, ctx);
        break;
    case 3: { // Theme
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Theme", "Theme", themeOnCycle, ctx);
        ctx->themeRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->themeRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::kThemeModeLabels[ctx->themeMode]);
        break;
    }
    case 4: { // Gradient
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
    case 5: { // Plates
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Plates", "Plates", platesOnCycle, ctx);
        ctx->platesRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->platesRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::kPlatesLabels[ctx->plates]);
        break;
    }
    case 6: { // Plate colour
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
    case 7: { // Plate opacity
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
    case 8: // Element tint
        settingsRowToggleCreate(ui, parent, "Element tint", "Element tint", ctx->tintEnabled, tintEnabledOnToggle, ctx);
        break;
    case 9: { // Tint colour
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
    case 10: { // Text scrim
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Text scrim", "Text scrim", scrimOnStep, ctx);
        ctx->scrimRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->scrimRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->scrim, settingsui::kBgAnimScrimSpec).c_str());
        break;
    }
    case 11: { // Fade out
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Fade out", "Fade out", fadeOutOnStep, ctx);
        ctx->fadeOutRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->fadeOutRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->fadeOut, settingsui::kBgFadeSpec).c_str());
        break;
    }
    case 12: { // Fade in
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Fade in", "Fade in", fadeInOnStep, ctx);
        ctx->fadeInRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->fadeInRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::formatNumeric(ctx->fadeIn, settingsui::kBgFadeSpec).c_str());
        break;
    }
    case 13: { // Fade curve
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Fade curve", "Fade curve", fadeCurveOnCycle, ctx);
        ctx->fadeCurveRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatAnimationCtx *>(lv_event_get_user_data(e))->fadeCurveRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, settingsui::kFadeCurveLabels[ctx->fadeCurve]);
        break;
    }
    case 14: // Interlace
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

const SettingsCategoryDef kCatAnimation = {
    "Animation", &img_settings_40x40, animRowCount, animBuildRow, animEnter, nullptr, animCommit, animReconcile,
    animCreateCtx, animDestroyCtx,
};
