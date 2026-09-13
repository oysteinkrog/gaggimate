// The gradient picker's two pushed pages (gm-nov3.3). CatGradientPicker.h
// carries the contract with the row that opens one; this file owns the pages.
//
// Page one lists Global (per-animation pickers only), My gradients when the
// library is not empty, and each declared category that has built-ins, with a
// count. Page two lists one group's gradients, each with its name, the ramp
// the panel would draw and a marker on the one in force. Both pages'
// SettingsCategoryDefs live in this file's anonymous namespace, the way
// CatAnimParams.cpp's and CatSchedules.cpp's pushed pages do: they are never
// in SettingsUI.h's tile registry and are reached only by pointer from the
// row that pushes them.
//
// Two rules the pages are built around:
//
// A choice is validated against the stored data before it is written. The
// list a page shows was built when the page was entered, and a web save can
// delete the library entry a row names while the finger is on the way down.
// The tap re-resolves the ref under Settings::Guard and, if it no longer
// names anything, closes the picker rather than writing a stale ref.
//
// The picker stays on the slot it was opened for. SettingsUI::service()
// reconciles only the top page, so while the picker is open the opening
// category's own reconcile never runs; the spec's onReconcileParent is what
// keeps its untouched fields current, and stillValid is what closes the
// picker when the slot itself stops being worth editing. Neither retargets
// the picker at a different animation, which is the Parameters page's rule
// too (CatAnimParams.cpp).
#include "CatGradientPicker.h"
#include "GradientSwatch.h"
#include "SettingsModel.h"
#include "SettingsRows.h"
#include "SettingsUI.h"

#include <display/core/Settings.h>
#include <display/main.h>
#include <display/ui/default/DefaultUI.h>

#include <cstdio>
#include <string>
#include <vector>

#include "esp_log.h"

namespace {

// Page titles, formatted right before each push into these file-scope
// buffers and referenced by the two defs below. One buffer each is safe for
// the reason CatAnimParams.cpp's is: the page stack holds at most one
// instance of any pushed def, and buildCategoryPage re-reads def->title on
// every rebuild.
char g_pickerTitle[40] = "Gradient";
char g_groupTitle[40] = "Gradients";

// What the stored library string is right now. Every list this file builds
// comes from one read of it, so a page cannot show two different readings of
// the same setting.
std::string storedLibrary() { return std::string(controller.getSettings().getBgAnimGradients().c_str()); }

// ---- page one: Global, My gradients, the categories --------------------------

struct PickerEntry {
    std::string label;
    std::string value;
    // What tapping it does: -1 opens the library group, >= 0 opens that
    // built-in group, and kGlobalEntry selects the ref "".
    static constexpr int kLibraryGroup = -1;
    static constexpr int kGlobalEntry = -2;
    int group = kGlobalEntry;
    // The choice whose ramp this row draws, or -1 for no swatch. Only the
    // Global row has one: a group row stands for several gradients and a
    // swatch of one of them would misreport the rest.
    int swatchChoice = -1;
    bool selected = false;
};

struct PickerCtx {
    SettingsGradientPickerSpec spec;
    std::string currentRef;
    std::string library;
    std::vector<settingsui::GradientChoice> choices;
    std::vector<settingsui::GradientGroup> groups;
    std::vector<int> libraryChoices;
    std::vector<PickerEntry> entries;
    SettingsUI *ui = nullptr;
};

// One row's identity: which page it belongs to and which entry it is. Freed
// on the row's own LV_EVENT_DELETE, the lifetime every per-row context in
// this epic uses.
struct RowCtx {
    void *page;
    int row;
};

// The choice index the current ref lands on, or -1 when it names nothing in
// the list (a deleted library entry, or an index from a longer table).
// Deliberately not gradientChoiceIndexForRef's 0: index 0 is the Default
// entry, and reporting a dangling ref as Default would put the marker on a
// row the user did not choose.
int choiceIndexOrNone(const std::vector<settingsui::GradientChoice> &choices, const std::string &ref) {
    if (ref.empty()) {
        return -1;
    }
    for (size_t i = 1; i < choices.size(); i++) {
        if (choices[i].ref == ref) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void rebuildEntries(PickerCtx *ctx) {
    const settingsui::ThemeNameProvider &themes = settingsThemeProvider();
    ctx->library = storedLibrary();
    ctx->choices = settingsui::gradientChoices(themes, ctx->library);
    ctx->groups = settingsui::gradientBuiltinGroups(themes, ctx->choices);
    ctx->libraryChoices = settingsui::gradientLibraryChoices(themes, ctx->choices);
    if (ctx->spec.currentRef != nullptr) {
        const char *ref = ctx->spec.currentRef(ctx->spec.user);
        ctx->currentRef = ref != nullptr ? ref : "";
    }
    const int current = choiceIndexOrNone(ctx->choices, ctx->currentRef);

    ctx->entries.clear();
    char buf[40];
    if (ctx->spec.allowGlobal) {
        PickerEntry e;
        e.label = "Global";
        e.value = settingsGlobalGradientLabel();
        e.group = PickerEntry::kGlobalEntry;
        // The global's own gradient, so the row shows what "follow the
        // global" will actually draw. Empty while a retained legacy custom
        // gradient is the fallback: it is not in the library and not a
        // built-in, so no ref names it and there is nothing to sample.
        e.swatchChoice = choiceIndexOrNone(ctx->choices, settingsGlobalGradientRef());
        e.selected = ctx->currentRef.empty();
        ctx->entries.push_back(std::move(e));
    }
    if (!ctx->libraryChoices.empty()) {
        PickerEntry e;
        e.label = "My gradients";
        std::snprintf(buf, sizeof(buf), "%d saved", static_cast<int>(ctx->libraryChoices.size()));
        e.value = buf;
        e.group = PickerEntry::kLibraryGroup;
        for (int idx : ctx->libraryChoices) {
            if (idx == current) {
                e.selected = true;
                break;
            }
        }
        ctx->entries.push_back(std::move(e));
    }
    for (size_t g = 0; g < ctx->groups.size(); g++) {
        PickerEntry e;
        e.label = ctx->groups[g].name;
        const int n = static_cast<int>(ctx->groups[g].choices.size());
        std::snprintf(buf, sizeof(buf), n == 1 ? "%d gradient" : "%d gradients", n);
        e.value = buf;
        e.group = static_cast<int>(g);
        for (int idx : ctx->groups[g].choices) {
            if (idx == current) {
                e.selected = true;
                break;
            }
        }
        ctx->entries.push_back(std::move(e));
    }
}

// The ramp a swatch row draws for one choice, or false when the choice does
// not resolve to a gradient (which leaves the row's swatch hidden).
bool rampForChoice(const PickerCtx *ctx, int choice, uint16_t *ramp) {
    if (choice < 0 || choice >= static_cast<int>(ctx->choices.size())) {
        return false;
    }
    settingsui::SwatchGradient gradient;
    if (!settingsui::swatchResolveRef(ctx->choices[static_cast<size_t>(choice)].ref.c_str(), ctx->library.c_str(),
                                      gradient)) {
        return false;
    }
    Settings &settings = controller.getSettings();
    settingsui::swatchApplyTone(gradient, settings.getBgAnimBrightness(), settings.getBgAnimHighlightKnee());
    settingsui::swatchBuildRamp565(gradient, ramp, kSettingsRowSwatchSamples);
    return true;
}

void pickerPushGroup(SettingsUI &ui, PickerCtx *parent, int group);

// A choice, from either page. Validates the ref against the stored data,
// hands it to the opening category and then leaves the picker. The order
// matters: onPick updates that category's draft, and popPages rebuilds its
// page from the draft on the way out, so a pick after the pop would show the
// old value until something else redrew the page.
void applyPick(PickerCtx *ctx, const std::string &ref, int depth) {
    SettingsUI *ui = ctx->ui;
    if (ui == nullptr) {
        return;
    }
    const SettingsGradientPickerSpec spec = ctx->spec; // copied: the pop below frees ctx
    bool valid = ref.empty();                          // "" is Global, which names no gradient
    if (!valid) {
        Settings::Guard guard(controller.getSettings());
        settingsui::SwatchGradient gradient;
        valid = settingsui::swatchResolveRef(ref.c_str(), storedLibrary().c_str(), gradient);
    }
    if (!valid) {
        ESP_LOGW("SettingsUI", "SettingsGradientPicker: %s no longer resolves, closing without a change", ref.c_str());
        ui->popPages(depth);
        return;
    }
    if (spec.stillValid != nullptr && !spec.stillValid(spec.user)) {
        ESP_LOGW("SettingsUI", "SettingsGradientPicker: the edited slot is gone, closing without a change");
        ui->popPages(depth);
        return;
    }
    if (spec.onPick != nullptr) {
        spec.onPick(spec.user, ref.c_str());
    }
    ui->popPages(depth);
}

void pickerRowActivate(void *user) {
    auto *rc = static_cast<RowCtx *>(user);
    auto *ctx = static_cast<PickerCtx *>(rc->page);
    if (rc->row < 0 || rc->row >= static_cast<int>(ctx->entries.size()) || ctx->ui == nullptr) {
        return;
    }
    const PickerEntry &entry = ctx->entries[static_cast<size_t>(rc->row)];
    if (entry.group == PickerEntry::kGlobalEntry) {
        applyPick(ctx, std::string(), 1);
        return;
    }
    pickerPushGroup(*ctx->ui, ctx, entry.group);
}

int pickerRowCount(void *ctx0) { return static_cast<int>(static_cast<PickerCtx *>(ctx0)->entries.size()); }

void pickerBuildRow(void *ctx0, int index, lv_obj_t *parent, SettingsUI &ui) {
    auto *ctx = static_cast<PickerCtx *>(ctx0);
    ctx->ui = &ui;
    if (index < 0 || index >= static_cast<int>(ctx->entries.size())) {
        return;
    }
    const PickerEntry &entry = ctx->entries[static_cast<size_t>(index)];
    // The row name is the entry's own label. SettingsUI::tag keeps the
    // pointer it is handed rather than copying, and these strings live in the
    // ctx for as long as the page does; nothing here reorders or resizes
    // `entries` while a row exists (rebuildEntries runs from enter and
    // reconcile, both followed by a full page rebuild).
    auto *rc = new RowCtx{ctx, index};
    lv_obj_t *row =
        settingsRowSwatchCreate(ui, parent, entry.label.c_str(), entry.label.c_str(), pickerRowActivate, rc);
    lv_obj_add_event_cb(
        row, [](lv_event_t *e) { delete static_cast<RowCtx *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, rc);
    settingsRowSetValue(row, entry.value.c_str());
    settingsRowSetSelected(row, entry.selected);
    uint16_t ramp[kSettingsRowSwatchSamples];
    if (rampForChoice(ctx, entry.swatchChoice, ramp)) {
        settingsRowSetSwatch(row, ramp);
    }
}

void pickerEnter(void *ctx0) { rebuildEntries(static_cast<PickerCtx *>(ctx0)); }

void pickerReconcile(void *ctx0) {
    auto *ctx = static_cast<PickerCtx *>(ctx0);
    if (ctx->spec.onReconcileParent != nullptr) {
        ctx->spec.onReconcileParent(ctx->spec.user);
    }
    if (ctx->spec.stillValid != nullptr && !ctx->spec.stillValid(ctx->spec.user) && ctx->ui != nullptr) {
        ctx->ui->popPages(1); // frees ctx: nothing below may touch it
        return;
    }
    rebuildEntries(ctx);
}

void pickerDestroyCtx(void *ctx) { delete static_cast<PickerCtx *>(ctx); }

const SettingsCategoryDef kCatGradientPicker = {
    g_pickerTitle,
    nullptr, // icon: never a tile, buildTile() never reads this
    pickerRowCount,
    pickerBuildRow,
    pickerEnter,
    nullptr, // refresh: nothing here changes on a timer
    nullptr, // commit: every pick is written when it is made
    pickerReconcile,
    nullptr, // createCtx: unused for a pushed page; the push below allocates it
    pickerDestroyCtx,
};

// ---- page two: the gradients in one group ------------------------------------

struct GroupCtx {
    PickerCtx *parent = nullptr;
    int group = PickerEntry::kLibraryGroup;
    std::vector<int> choices; // into parent->choices
    SettingsUI *ui = nullptr;
};

// Re-derives the group's contents from the parent's freshly-built lists. The
// group is identified by its position, not by the entries it held: a web save
// can add or delete a library entry, and this page shows what is there now.
void refreshGroup(GroupCtx *ctx) {
    const PickerCtx *parent = ctx->parent;
    if (ctx->group == PickerEntry::kLibraryGroup) {
        ctx->choices = parent->libraryChoices;
        return;
    }
    if (ctx->group >= 0 && ctx->group < static_cast<int>(parent->groups.size())) {
        ctx->choices = parent->groups[static_cast<size_t>(ctx->group)].choices;
        return;
    }
    ctx->choices.clear();
}

void groupRowActivate(void *user) {
    auto *rc = static_cast<RowCtx *>(user);
    auto *ctx = static_cast<GroupCtx *>(rc->page);
    if (rc->row < 0 || rc->row >= static_cast<int>(ctx->choices.size()) || ctx->parent == nullptr) {
        return;
    }
    const int choice = ctx->choices[static_cast<size_t>(rc->row)];
    const std::string ref = ctx->parent->choices[static_cast<size_t>(choice)].ref;
    applyPick(ctx->parent, ref, 2); // both pages: a choice returns to the category that opened the picker
}

int groupRowCount(void *ctx0) { return static_cast<int>(static_cast<GroupCtx *>(ctx0)->choices.size()); }

void groupBuildRow(void *ctx0, int index, lv_obj_t *parent, SettingsUI &ui) {
    auto *ctx = static_cast<GroupCtx *>(ctx0);
    ctx->ui = &ui;
    if (index < 0 || index >= static_cast<int>(ctx->choices.size())) {
        return;
    }
    const int choice = ctx->choices[static_cast<size_t>(index)];
    const settingsui::GradientChoice &gc = ctx->parent->choices[static_cast<size_t>(choice)];
    const bool selected = gc.ref == ctx->parent->currentRef;
    auto *rc = new RowCtx{ctx, index};
    lv_obj_t *row = settingsRowSwatchCreate(ui, parent, gc.label.c_str(), gc.label.c_str(), groupRowActivate, rc);
    lv_obj_add_event_cb(
        row, [](lv_event_t *e) { delete static_cast<RowCtx *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, rc);
    settingsRowSetValue(row, selected ? "Selected" : "");
    settingsRowSetSelected(row, selected);
    uint16_t ramp[kSettingsRowSwatchSamples];
    if (rampForChoice(ctx->parent, choice, ramp)) {
        settingsRowSetSwatch(row, ramp);
    }
}

void groupReconcile(void *ctx0) {
    auto *ctx = static_cast<GroupCtx *>(ctx0);
    PickerCtx *parent = ctx->parent;
    SettingsUI *ui = ctx->ui;
    if (parent == nullptr || ui == nullptr) {
        return;
    }
    if (parent->spec.onReconcileParent != nullptr) {
        parent->spec.onReconcileParent(parent->spec.user);
    }
    if (parent->spec.stillValid != nullptr && !parent->spec.stillValid(parent->spec.user)) {
        ui->popPages(2); // frees both ctxs: nothing below may touch either
        return;
    }
    rebuildEntries(parent);
    refreshGroup(ctx);
    if (ctx->choices.empty()) {
        // Every entry this page listed is gone (the library was emptied from
        // the web UI). Back to the group list, which still has something to
        // show, rather than a page of nothing.
        ui->popPages(1);
    }
}

void groupDestroyCtx(void *ctx) { delete static_cast<GroupCtx *>(ctx); }

const SettingsCategoryDef kCatGradientGroup = {
    g_groupTitle,
    nullptr,
    groupRowCount,
    groupBuildRow,
    nullptr, // enter: the ctx is filled in before the push, from the parent
    nullptr,
    nullptr,
    groupReconcile,
    nullptr,
    groupDestroyCtx,
};

void pickerPushGroup(SettingsUI &ui, PickerCtx *parent, int group) {
    auto *ctx = new GroupCtx();
    ctx->parent = parent;
    ctx->group = group;
    refreshGroup(ctx);
    if (ctx->choices.empty()) {
        delete ctx; // nothing to show: the row that opened it is already gone
        return;
    }
    const char *name = group == PickerEntry::kLibraryGroup
                           ? "My gradients"
                           : parent->groups[static_cast<size_t>(group)].name.c_str();
    std::snprintf(g_groupTitle, sizeof(g_groupTitle), "%s", name);
    ui.pushPage(&kCatGradientGroup, ctx);
}

} // namespace

void settingsGradientPickerPush(SettingsUI &ui, const SettingsGradientPickerSpec &spec) {
    auto *ctx = new PickerCtx();
    ctx->spec = spec;
    ctx->ui = &ui;
    std::snprintf(g_pickerTitle, sizeof(g_pickerTitle), "%s", spec.title != nullptr ? spec.title : "Gradient");
    ui.pushPage(&kCatGradientPicker, ctx);
}
