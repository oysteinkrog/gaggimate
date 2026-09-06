// Auto wake-up schedule list and editor pages (gm-flw.11), pushed from the
// Machine category's fourth row ("Schedules": CatMachine.h/.cpp, gm-flw.10)
// through settingsMachineExtraRowCount/settingsMachineBuildExtraRow, defined
// strong here to replace CatMachine.cpp's weak default. Both pages edit the
// Machine category's own draft (MachineDraft, held by the "Schedules" row's
// onActivate and threaded through as each pushed page's ctx) directly and
// mark schedulesTouched on every edit; CatMachine.cpp's commit converts and
// persists only when that flag is set, so neither page here writes Settings
// itself or needs a commit callback (SettingsCategoryDef::commit is nullptr
// for both, same as enter/refresh: there is nothing of their own to
// snapshot or do once a second).
//
// kCatScheduleList and kCatScheduleEditor are never placed in SettingsUI.h's
// tile registry (they are reached only by SettingsUI::pushPage, from a row
// on the Machine page or the list page), so neither needs the weak/strong
// linkage trick the five real categories use; both live in this file's own
// anonymous namespace like any other internal symbol.
#include "CatMachine.h"
#include "CatSchedules.h"
#include "SettingsModel.h"
#include "SettingsRows.h"
#include "SettingsUI.h"

#include <display/core/Settings.h>
#include <display/main.h>

#include <cstdio>
#include <string>

namespace {

// "07:00 Every day": the list row's value, and the model's whole
// contribution to it beyond the raw time string.
std::string scheduleRowText(const settingsui::ScheduleDraft &s) {
    return s.time + " " + settingsui::scheduleDaysSummary(s);
}

struct ScheduleListCtx {
    MachineDraft *draft = nullptr; // never owned; the Machine category's own ctx member outlives every page pushed on top of it
    // Cached from the first buildRow call (rowCount/reconcile/destroyCtx
    // never receive a SettingsUI&); used by a row's own onActivate to push
    // the editor, and by "Add schedule"'s onActivate to rebuild this page.
    SettingsUI *ui = nullptr;
    // "Schedule n" tag storage. SettingsUI::tag stores the row-name pointer
    // handed to settingsRowActionCreate, it does not copy the string, so a
    // formatted name must live in storage that outlives the page, never a
    // stack buffer (crashed the touchmap dump in the row-widget wave). At
    // most kRowsPerPage schedule rows are ever built at once regardless of
    // how long the list is (one page of five slots is on screen at a time,
    // and a page boundary always falls on a multiple of kRowsPerPage), so
    // indexing this by the row's global index modulo kRowsPerPage gives
    // every simultaneously-visible row its own stable slot with a fixed,
    // small table, no cap on the list length and no fallback case needed.
    char rowNames[SettingsUI::kRowsPerPage][20];
};

// A list row's onActivate context: which schedule (or -1 for "Add
// schedule"). Allocated fresh per buildRow call, freed on the row's
// LV_EVENT_DELETE, same lifetime pattern every category in this epic uses
// for its own per-row state.
struct ScheduleEntryCtx {
    ScheduleListCtx *list;
    int index; // -1 for "Add schedule"
};

// Title of the editor page, formatted once (right before it is pushed) into
// this file-scope buffer and referenced by kCatScheduleEditor.title. Safe as
// a single shared buffer because only one schedule editor page can ever be
// open at a time (SettingsUI's page stack holds at most one instance of any
// pushed def), and buildCategoryPage re-reads def->title fresh on every
// rebuild, so writing it before the push is enough; nothing after that
// needs to touch it again for the life of that editor visit (the "n" in
// "Schedule n" is the position the editor was opened for, not the
// schedule's identity, and stays fixed even if reconcile later swaps in a
// different entry at the same position).
char g_scheduleEditorTitle[24] = "Schedule";

struct ScheduleEditorCtx {
    MachineDraft *draft = nullptr;
    // Position in draft->schedules, never a pointer/iterator into it: the
    // vector is replaced whole on an untouched reconcile (getAutoWakeupSchedules()
    // returns by value), which would leave an element pointer dangling.
    size_t index = 0;
    SettingsUI *ui = nullptr; // cached from the first buildRow call
    lv_obj_t *hourRow = nullptr;
    lv_obj_t *minuteRow = nullptr;
};

// A day toggle's onActivate context: which of the editor's seven identical
// rows this is. Same allocate-per-build, free-on-DELETE lifetime as
// ScheduleEntryCtx above.
struct DayToggleCtx {
    ScheduleEditorCtx *editor;
    int day; // 0=Monday..6=Sunday
};

// Both value rows go through the model rather than slicing schedule.time:
// a stored entry only looks like "HH:MM" when the display or the web UI's
// own form wrote it. The web handler stores whatever string the browser
// sent (WebUIPlugin.cpp), so a stored "|1111111" reaches here as an empty
// time, and substr on it throws out_of_range, which aborts a firmware
// built without exceptions. scheduleTimeParts reads anything malformed as
// 00:00, matching what the first stepper tap would then write.
void setHourValue(ScheduleEditorCtx *ctx) {
    if (ctx->hourRow == nullptr || ctx->index >= ctx->draft->schedules.size()) {
        return;
    }
    int hour = 0;
    int minute = 0;
    settingsui::scheduleTimeParts(ctx->draft->schedules[ctx->index], hour, minute);
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%02d", hour);
    settingsRowSetValue(ctx->hourRow, buf);
}

void setMinuteValue(ScheduleEditorCtx *ctx) {
    if (ctx->minuteRow == nullptr || ctx->index >= ctx->draft->schedules.size()) {
        return;
    }
    int hour = 0;
    int minute = 0;
    settingsui::scheduleTimeParts(ctx->draft->schedules[ctx->index], hour, minute);
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%02d", minute);
    settingsRowSetValue(ctx->minuteRow, buf);
}

void hourOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<ScheduleEditorCtx *>(user);
    if (ctx->index >= ctx->draft->schedules.size()) {
        return; // about to be torn down: scheduleEditorReconcile already popped this page
    }
    settingsui::scheduleStepHour(ctx->draft->schedules[ctx->index], dir, fast);
    ctx->draft->schedulesTouched = true;
    setHourValue(ctx);
}

void minuteOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<ScheduleEditorCtx *>(user);
    if (ctx->index >= ctx->draft->schedules.size()) {
        return;
    }
    settingsui::scheduleStepMinute(ctx->draft->schedules[ctx->index], dir, fast);
    ctx->draft->schedulesTouched = true;
    setMinuteValue(ctx);
}

// The toggle widget owns its own On/Off text (SettingsRows.h), so this is a
// notification only; the model's scheduleToggleDay flips the stored bit
// rather than this callback assigning the widget's reported value, so the
// same host-tested toggle rule (test_settings_model.cpp group G) backs the
// on-display behaviour.
void dayOnToggle(void *user, bool /*value*/) {
    auto *dc = static_cast<DayToggleCtx *>(user);
    ScheduleEditorCtx *ctx = dc->editor;
    if (ctx->index >= ctx->draft->schedules.size()) {
        return;
    }
    settingsui::scheduleToggleDay(ctx->draft->schedules[ctx->index], dc->day);
    ctx->draft->schedulesTouched = true;
}

void removeOnConfirm(void *user) {
    auto *ctx = static_cast<ScheduleEditorCtx *>(user);
    if (!settingsui::scheduleRemove(ctx->draft->schedules, ctx->index)) {
        return; // only one schedule left; the row is disabled, but stay defensive
    }
    ctx->draft->schedulesTouched = true;
    if (ctx->ui != nullptr) {
        // Pops this editor back to the list; the editor's commit is
        // nullptr (nothing more to write, the touched flag above is what
        // the Machine category's own commit acts on) and popPage()
        // rebuilds the list underneath with the row removed, same as the
        // shell rebuilds any parent page after a child edits its ctx.
        ctx->ui->popPage();
    }
}

int scheduleEditorRowCount(void * /*ctx*/) { return 10; }

void scheduleEditorBuildRow(void *ctx0, int index, lv_obj_t *parent, SettingsUI &ui) {
    auto *ctx = static_cast<ScheduleEditorCtx *>(ctx0);
    ctx->ui = &ui;
    if (ctx->index >= ctx->draft->schedules.size()) {
        // The schedule this editor was opened for is gone: an untouched
        // web-save reconcile shrank the list and scheduleEditorReconcile
        // already called popPage() for it, whose own rebuildPage() targets
        // the list page, not this one, so buildRow does not actually run
        // again for this ctx after that. Guarding here anyway rather than
        // indexing past the end, for the instant in between.
        return;
    }
    settingsui::ScheduleDraft &sched = ctx->draft->schedules[ctx->index];
    switch (index) {
    case 0: {
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Hour", "Hour", hourOnStep, ctx);
        ctx->hourRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<ScheduleEditorCtx *>(lv_event_get_user_data(e))->hourRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        setHourValue(ctx);
        break;
    }
    case 1: {
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Minute", "Minute", minuteOnStep, ctx);
        ctx->minuteRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<ScheduleEditorCtx *>(lv_event_get_user_data(e))->minuteRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        setMinuteValue(ctx);
        break;
    }
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8: {
        static constexpr const char *const kDayNames[7] = {
            "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday",
        };
        const int day = index - 2;
        auto *dc = new DayToggleCtx{ctx, day};
        lv_obj_t *row =
            settingsRowToggleCreate(ui, parent, kDayNames[day], kDayNames[day], sched.days[day], dayOnToggle, dc);
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { delete static_cast<DayToggleCtx *>(lv_event_get_user_data(e)); },
            LV_EVENT_DELETE, dc);
        break;
    }
    case 9: {
        lv_obj_t *row =
            settingsRowConfirmCreate(ui, parent, "Remove schedule", "Remove schedule", removeOnConfirm, ctx);
        settingsRowSetEnabled(row, ctx->draft->schedules.size() > 1);
        break;
    }
    default:
        break;
    }
}

// After a web save: re-reads the vector from Settings only when this visit
// never touched it (shared contract, same as every other category's
// reconcile); if the position this editor was opened for no longer exists
// in the replacement vector, pops back to the list without committing or
// re-snapshotting the Machine draft (the editor's commit is nullptr, so
// popPage() writes nothing). Otherwise the pending rebuildPage() the shell
// runs right after this call rebuilds the editor's rows from whatever is
// now at that position.
void scheduleEditorReconcile(void *ctx0) {
    auto *ctx = static_cast<ScheduleEditorCtx *>(ctx0);
    if (ctx->draft->schedulesTouched) {
        return;
    }
    Settings &settings = controller.getSettings();
    ctx->draft->schedules = fromAutoWakeupSchedules(settings.getAutoWakeupSchedules());
    if (ctx->index >= ctx->draft->schedules.size() && ctx->ui != nullptr) {
        ctx->ui->popPage();
    }
}

void scheduleEditorDestroyCtx(void *ctx) { delete static_cast<ScheduleEditorCtx *>(ctx); }

// title is this file's mutable g_scheduleEditorTitle buffer (see its own
// comment above), not a literal: SettingsCategoryDef itself is const, but
// that only fixes the pointer value, not what the pointed-to bytes hold.
const SettingsCategoryDef kCatScheduleEditor = {
    g_scheduleEditorTitle,
    nullptr, // icon: never a tile, buildTile() never reads this
    scheduleEditorRowCount,
    scheduleEditorBuildRow,
    nullptr, // enter: nothing of its own to snapshot, the draft is already current
    nullptr, // refresh
    nullptr, // commit: the Machine category's commit persists; this page writes nothing
    scheduleEditorReconcile,
    nullptr, // createCtx: unused for a pushed page; the list's onActivate allocates it
    scheduleEditorDestroyCtx,
};

int scheduleListRowCount(void *ctx0) {
    auto *ctx = static_cast<ScheduleListCtx *>(ctx0);
    return static_cast<int>(ctx->draft->schedules.size()) + 1; // +1: "Add schedule"
}

void scheduleEntryOnActivate(void *user) {
    auto *ec = static_cast<ScheduleEntryCtx *>(user);
    ScheduleListCtx *list = ec->list;
    if (ec->index < 0) { // "Add schedule"
        if (settingsui::scheduleAdd(list->draft->schedules)) {
            list->draft->schedulesTouched = true;
            if (list->ui != nullptr) {
                list->ui->rebuildPage();
            }
        }
        return;
    }
    auto *editorCtx = new ScheduleEditorCtx();
    editorCtx->draft = list->draft;
    editorCtx->index = static_cast<size_t>(ec->index);
    std::snprintf(g_scheduleEditorTitle, sizeof(g_scheduleEditorTitle), "Schedule %d", ec->index + 1);
    if (list->ui != nullptr) {
        list->ui->pushPage(&kCatScheduleEditor, editorCtx);
    }
}

void scheduleListBuildRow(void *ctx0, int index, lv_obj_t *parent, SettingsUI &ui) {
    auto *ctx = static_cast<ScheduleListCtx *>(ctx0);
    ctx->ui = &ui;
    const int count = static_cast<int>(ctx->draft->schedules.size());
    if (index < count) {
        char *nameBuf = ctx->rowNames[index % SettingsUI::kRowsPerPage];
        std::snprintf(nameBuf, sizeof(ctx->rowNames[0]), "Schedule %d", index + 1);
        auto *ec = new ScheduleEntryCtx{ctx, index};
        lv_obj_t *row = settingsRowActionCreate(ui, parent, nameBuf, nameBuf, scheduleEntryOnActivate, ec);
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { delete static_cast<ScheduleEntryCtx *>(lv_event_get_user_data(e)); },
            LV_EVENT_DELETE, ec);
        settingsRowSetValue(row, scheduleRowText(ctx->draft->schedules[index]).c_str());
    } else {
        auto *ec = new ScheduleEntryCtx{ctx, -1};
        lv_obj_t *row =
            settingsRowActionCreate(ui, parent, "Add schedule", "Add schedule", scheduleEntryOnActivate, ec);
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { delete static_cast<ScheduleEntryCtx *>(lv_event_get_user_data(e)); },
            LV_EVENT_DELETE, ec);
        settingsRowSetEnabled(row, settingsui::scheduleCanAdd(ctx->draft->schedules));
    }
}

void scheduleListReconcile(void *ctx0) {
    auto *ctx = static_cast<ScheduleListCtx *>(ctx0);
    if (!ctx->draft->schedulesTouched) {
        Settings &settings = controller.getSettings();
        ctx->draft->schedules = fromAutoWakeupSchedules(settings.getAutoWakeupSchedules());
    }
}

void scheduleListDestroyCtx(void *ctx) { delete static_cast<ScheduleListCtx *>(ctx); }

const SettingsCategoryDef kCatScheduleList = {
    "Schedules",
    nullptr, // icon: never a tile, buildTile() never reads this
    scheduleListRowCount,
    scheduleListBuildRow,
    nullptr, // enter: the draft is the Machine category's, already current
    nullptr, // refresh
    nullptr, // commit: nothing of its own to write
    scheduleListReconcile,
    nullptr, // createCtx: unused for a pushed page; the Schedules row's onActivate allocates it
    scheduleListDestroyCtx,
};

// The Machine category's fourth row: builds and pushes kCatScheduleList.
// draft outlives this row (it is the Machine category's own ctx member),
// but the row still needs a place to keep the SettingsUI& buildRow hands it
// for later (onActivate's signature is (void *user) only), so this small
// struct is allocated per build and freed on the row's own DELETE, the same
// per-row-instance lifetime every activation context in this file uses.
struct ScheduleOpenCtx {
    MachineDraft *draft;
    SettingsUI *ui;
};

void schedulesOnActivate(void *user) {
    auto *rc = static_cast<ScheduleOpenCtx *>(user);
    auto *listCtx = new ScheduleListCtx();
    listCtx->draft = rc->draft;
    rc->ui->pushPage(&kCatScheduleList, listCtx);
}

} // namespace

int settingsMachineExtraRowCount(MachineDraft * /*draft*/) { return 1; }

void settingsMachineBuildExtraRow(MachineDraft *draft, int index, lv_obj_t *parent, SettingsUI &ui) {
    if (index != 3) {
        return;
    }
    auto *rowCtx = new ScheduleOpenCtx{draft, &ui};
    lv_obj_t *row = settingsRowActionCreate(ui, parent, "Schedules", "Schedules", schedulesOnActivate, rowCtx);
    lv_obj_add_event_cb(
        row, [](lv_event_t *e) { delete static_cast<ScheduleOpenCtx *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE,
        rowCtx);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d schedules", static_cast<int>(draft->schedules.size()));
    settingsRowSetValue(row, buf);
}
