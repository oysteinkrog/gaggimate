// The animation Parameters page (gm-3vj.2): one stepper row per parameter
// the current background animation defines, plus a "Reset to defaults"
// confirm row. Pushed from the Animation category's Parameters row
// (CatAnimation.cpp, through settingsAnimParamsPush below); its
// SettingsCategoryDef lives in this file's anonymous namespace, like
// CatSchedules.cpp's two pushed pages, because it is never in SettingsUI.h's
// tile registry and is reached only by pointer from the row that pushes it.
//
// Every step is live: it writes the whole bgAnimParams string back through
// Settings and calls markDirty(), and DefaultUI::updateState re-parses that
// string and hands the eight bytes to the render task on the next pass
// (bg_parse_params in DefaultUI.cpp), so a parameter moves under the finger
// on the animation behind the settings cover.
//
// The page is fixed to the animation it was opened for, the way the schedule
// editor is fixed to the position it was opened for. A web save that changes
// bgAnimId while this page is open therefore leaves the page editing the
// animation named in its title, and the Animation row underneath showing the
// new one; the rows still refresh from the stored string, so what the page
// shows is never stale, only about a different animation than the one the
// renderer is drawing.
#include "CatAnimParams.h"
#include "SettingsModel.h"
#include "SettingsRows.h"
#include "SettingsUI.h"

#include <display/core/Settings.h>
#include <display/main.h>
#include <display/ui/default/DefaultUI.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "esp_log.h"

namespace {

// Title of the page, formatted right before it is pushed into this
// file-scope buffer and referenced by kCatAnimParams.title. One shared
// buffer is safe for the same reason g_scheduleEditorTitle is: the page
// stack holds at most one instance of any pushed def, and
// buildCategoryPage re-reads def->title on every rebuild.
char g_animParamsTitle[40] = "Parameters";

// The 144 px the shell gives a page title, in the font it draws it in
// (SettingsUI.cpp's header: 240 px wide, arrows either side, the title on one
// line in montserrat 24 with LV_LABEL_LONG_DOT past that).
constexpr int kTitleWidth = 144;

struct AnimParamsCtx {
    int animId = 0;
    // The whole eight-slot group, defaults behind whatever the stored string
    // carried (SettingsModel.h, bgParamsRead).
    uint8_t values[settingsui::kBgAnimParamSlots] = {};
    // Per slot, for the touched-field precedence rule the epic applies
    // everywhere: a web save that lands on a slot this visit edited must not
    // cost the visit its value at commit.
    bool touched[settingsui::kBgAnimParamSlots] = {};
    // Slot index per built row, so a row's callbacks need only the row index
    // the page was built with. Slots the animation does not define are not
    // shown, so this is not the identity map for an animation with a gap.
    int slotOfRow[settingsui::kBgAnimParamSlots] = {};
    int paramCount = 0;
    lv_obj_t *rows[settingsui::kBgAnimParamSlots] = {};
    // Cached from the first buildRow call, the way every category caches it
    // (enter/commit/reconcile receive only ctx).
    SettingsUI *ui = nullptr;
};

// One row's step context: which of the page's rows this is. Allocated per
// buildRow call and freed on the row's LV_EVENT_DELETE, the same lifetime
// every per-row context in this epic uses.
struct ParamRowCtx {
    AnimParamsCtx *page;
    int row;
};

// The animation's own defaults, in slot order, as bg_parse_params reports
// them: the descriptor's default where the slot is defined, 0 where it is
// not. Into a caller's buffer, so nothing here is shared between calls.
void defaultsFor(const AnimParamsCtx *ctx, uint8_t *out) {
    const BgAnimParamDef *params = settingsAnimParams(ctx->animId);
    for (int i = 0; i < settingsui::kBgAnimParamSlots; i++) {
        out[i] = params[i].key != nullptr ? params[i].def : 0;
    }
}

// The slots this animation defines, in slot order. An unused slot is one
// whose key is nullptr (BgAnim.h); they are trailing in every animation
// today, but the page reads the table rather than assuming that, so a gap
// would hide one row instead of misaligning every slot after it.
void collectSlots(AnimParamsCtx *ctx) {
    const BgAnimParamDef *params = settingsAnimParams(ctx->animId);
    ctx->paramCount = 0;
    for (int i = 0; i < settingsui::kBgAnimParamSlots; i++) {
        if (params[i].key != nullptr) {
            ctx->slotOfRow[ctx->paramCount++] = i;
        }
    }
}

void readStored(AnimParamsCtx *ctx) {
    uint8_t defs[settingsui::kBgAnimParamSlots];
    defaultsFor(ctx, defs);
    const std::string packed(controller.getSettings().getBgAnimParams().c_str());
    settingsui::bgParamsRead(packed, ctx->animId, defs, ctx->values);
}

void setRowValue(AnimParamsCtx *ctx, int row) {
    if (row < 0 || row >= ctx->paramCount || ctx->rows[row] == nullptr) {
        return;
    }
    const int slot = ctx->slotOfRow[row];
    settingsRowSetValue(ctx->rows[row], settingsui::formatNumeric(ctx->values[slot], settingsui::kBgAnimParamSpec).c_str());
}

// Writes the draft's group back. Read-modify-write of one string shared with
// the web save's batchUpdate, so it takes the guard the same way
// CatAnimation.cpp's gradient row does; every other live row in this epic is
// a single Property::set and needs none.
void storeGroup(AnimParamsCtx *ctx) {
    Settings &settings = controller.getSettings();
    Settings::Guard guard(settings);
    const std::string packed(settings.getBgAnimParams().c_str());
    settings.setBgAnimParams(settingsui::bgParamsWriteGroup(packed, ctx->animId, ctx->values).c_str());
}

// Every slot this visit has touched merged into `packed`, with every slot it
// has not left exactly as the string holds it. Returns true when the string
// changed.
//
// Shared by animParamsReconcile and animParamsCommit so the rule that decides
// which slots survive a web save cannot drift between them, the way
// CatAnimation.cpp's mergeTouchedGradientSlots is shared: bgAnimParams is one
// group per animation, a web save replaces the whole string in one write, and
// this visit owns only the slots it has stepped.
bool mergeTouchedSlots(const AnimParamsCtx *ctx, std::string &packed) {
    uint8_t defs[settingsui::kBgAnimParamSlots];
    defaultsFor(ctx, defs);
    uint8_t stored[settingsui::kBgAnimParamSlots];
    settingsui::bgParamsRead(packed, ctx->animId, defs, stored);
    bool differs = false;
    for (int i = 0; i < settingsui::kBgAnimParamSlots; i++) {
        if (ctx->touched[i] && stored[i] != ctx->values[i]) {
            differs = true;
            break;
        }
    }
    if (!differs) {
        return false;
    }
    uint8_t merged[settingsui::kBgAnimParamSlots];
    for (int i = 0; i < settingsui::kBgAnimParamSlots; i++) {
        merged[i] = ctx->touched[i] ? ctx->values[i] : stored[i];
    }
    packed = settingsui::bgParamsWriteGroup(packed, ctx->animId, merged);
    return true;
}

void paramOnStep(void *user, int dir, bool fast) {
    auto *rc = static_cast<ParamRowCtx *>(user);
    AnimParamsCtx *ctx = rc->page;
    if (rc->row < 0 || rc->row >= ctx->paramCount) {
        return;
    }
    const int slot = ctx->slotOfRow[rc->row];
    ctx->values[slot] = static_cast<uint8_t>(settingsui::stepValue(ctx->values[slot], dir, fast, settingsui::kBgAnimParamSpec));
    ctx->touched[slot] = true;
    storeGroup(ctx);
    setRowValue(ctx, rc->row);
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
    }
}

void resetOnConfirm(void *user) {
    auto *ctx = static_cast<AnimParamsCtx *>(user);
    {
        // Empty the group rather than writing the defaults out: the parser
        // reads a missing group as "every slot at its default", so this is
        // the same state a device that never edited a parameter stores, and
        // it stays right if a later build changes a default.
        Settings &settings = controller.getSettings();
        Settings::Guard guard(settings);
        const std::string packed(settings.getBgAnimParams().c_str());
        settings.setBgAnimParams(settingsui::bgParamsClearGroup(packed, ctx->animId).c_str());
    }
    readStored(ctx);
    // Reset is this visit's deliberate value for every slot, so it is a
    // touch: a web save landing afterwards does not take the defaults back
    // off the screen. commit() compares values, and a cleared group reads
    // back as the defaults, so in the ordinary case it still writes nothing.
    for (int i = 0; i < settingsui::kBgAnimParamSlots; i++) {
        ctx->touched[i] = true;
    }
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
        ctx->ui->rebuildPage(); // every row's value at once
    }
}

int animParamsRowCount(void *ctx0) {
    auto *ctx = static_cast<AnimParamsCtx *>(ctx0);
    if (ctx->paramCount == 0) {
        return 1; // the "no parameters" info row
    }
    return ctx->paramCount + 1; // + "Reset to defaults"
}

void animParamsBuildRow(void *ctx0, int index, lv_obj_t *parent, SettingsUI &ui) {
    auto *ctx = static_cast<AnimParamsCtx *>(ctx0);
    ctx->ui = &ui;
    if (ctx->paramCount == 0) {
        // No animation in the roster defines zero parameters today; this is
        // what the page shows if one ever does, rather than a bare Reset row
        // that would reset nothing.
        lv_obj_t *row = settingsRowInfoCreate(ui, parent, "Parameters", "Parameters");
        settingsRowSetValue(row, "None");
        return;
    }
    if (index < ctx->paramCount) {
        // The row name is the parameter's own label, straight out of the
        // descriptor table: SettingsUI::tag keeps the pointer it is handed
        // rather than copying the string, and these are string literals in
        // flash (or in the simulator's mirror), so they outlive the page.
        const BgAnimParamDef *params = settingsAnimParams(ctx->animId);
        const char *label = params[ctx->slotOfRow[index]].label;
        auto *rc = new ParamRowCtx{ctx, index};
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, label, label, paramOnStep, rc);
        ctx->rows[index] = row;
        // One callback for both jobs, and in this order: it clears the
        // page's pointer to this row and only then frees the row's own
        // context, which is what it read the row index out of.
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) {
                auto *dead = static_cast<ParamRowCtx *>(lv_event_get_user_data(e));
                if (dead->row >= 0 && dead->row < settingsui::kBgAnimParamSlots) {
                    dead->page->rows[dead->row] = nullptr;
                }
                delete dead;
            },
            LV_EVENT_DELETE, rc);
        setRowValue(ctx, index);
        return;
    }
    if (index == ctx->paramCount) {
        settingsRowConfirmCreate(ui, parent, "Reset to defaults", "Reset to defaults", resetOnConfirm, ctx);
    }
}

// Refreshes every slot this visit has not edited from the stored string, the
// shared reconcile contract, and writes every slot it has edited back over
// the web's. The shell rebuilds the page right after this call, so the row
// values come from the draft on that pass and nothing here has to touch a
// row.
//
// Every slot here is a live field: a step writes the whole string at once and
// DefaultUI::updateState re-parses it and hands the eight bytes to the render
// task on its next pass. So a web save that replaced the string left the
// panel drawing the web's value for a slot this visit had stepped, while the
// row went on showing the visit's, from the save until the pop. Putting the
// touched slots back is the same write the step itself makes, so it restores
// the live field rather than changing commit-time precedence (gm-nov3.39);
// commit still writes exactly what the rows said, and an untouched slot still
// adopts whatever the web posted. Runs under the shell's Settings::Guard
// (SettingsUI::service wraps every reconcile call), so it takes none of its
// own the way storeGroup has to.
void animParamsReconcile(void *ctx0) {
    auto *ctx = static_cast<AnimParamsCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    uint8_t defs[settingsui::kBgAnimParamSlots];
    defaultsFor(ctx, defs);
    std::string packed(settings.getBgAnimParams().c_str());
    uint8_t stored[settingsui::kBgAnimParamSlots];
    settingsui::bgParamsRead(packed, ctx->animId, defs, stored);
    for (int i = 0; i < settingsui::kBgAnimParamSlots; i++) {
        if (!ctx->touched[i]) {
            ctx->values[i] = stored[i];
        }
    }
    if (mergeTouchedSlots(ctx, packed)) {
        settings.setBgAnimParams(packed.c_str());
    }
}

// Every step already wrote the string and reconcile has usually written the
// touched slots back over a web save too, so in the ordinary case this writes
// nothing. It stays because a web save can land between the last reconcile
// and this call, and because a visit with no web save in it never reconciles
// at all. Through the same merge as reconcile, so the two cannot disagree
// about which slots this visit owns. Runs under the shell's Settings::Guard
// (popPage/teardownAll wrap the whole call), so it takes none of its own.
void animParamsCommit(void *ctx0) {
    auto *ctx = static_cast<AnimParamsCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    std::string packed(settings.getBgAnimParams().c_str());
    if (!mergeTouchedSlots(ctx, packed)) {
        return;
    }
    settings.setBgAnimParams(packed.c_str());
    ESP_LOGI("SettingsUI", "SettingsAnimParams: re-asserted anim=%d over a web save", ctx->animId);
    if (ctx->ui != nullptr) {
        ctx->ui->ui().markDirty();
        ctx->ui->plugins().trigger("settings:changed");
    }
}

void animParamsDestroyCtx(void *ctx) { delete static_cast<AnimParamsCtx *>(ctx); }

// title is this file's mutable g_animParamsTitle buffer, not a literal: the
// SettingsCategoryDef being const fixes the pointer, not the bytes.
const SettingsCategoryDef kCatAnimParams = {
    g_animParamsTitle,
    nullptr, // icon: never a tile, buildTile() never reads this
    animParamsRowCount,
    animParamsBuildRow,
    nullptr, // enter: the ctx is filled in before the push, from Settings
    nullptr, // refresh
    animParamsCommit,
    animParamsReconcile,
    nullptr, // createCtx: unused for a pushed page; the row below allocates it
    animParamsDestroyCtx,
};

} // namespace

void settingsAnimParamsPush(SettingsUI &ui, int animId) {
    auto *ctx = new AnimParamsCtx();
    ctx->animId = animId;
    collectSlots(ctx);
    readStored(ctx);

    // "<name> params" when the shell's 144 px title box can hold it, else
    // the name on its own. In montserrat 24 that is most of them: "Animation"
    // alone measures about 125 px, so the suffix only fits behind a short
    // name.
    const char *name = settingsAnimName(animId);
    std::snprintf(g_animParamsTitle, sizeof(g_animParamsTitle), "%s params", name);
    const lv_coord_t withSuffix =
        lv_txt_get_width(g_animParamsTitle, std::strlen(g_animParamsTitle), &lv_font_montserrat_24, 0, LV_TEXT_FLAG_NONE);
    if (withSuffix > kTitleWidth) {
        std::snprintf(g_animParamsTitle, sizeof(g_animParamsTitle), "%s", name);
    }

    ui.pushPage(&kCatAnimParams, ctx);
}
