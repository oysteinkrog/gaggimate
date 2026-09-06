// Machine category: startup mode, standby timeout and the auto wake-up
// master toggle. All three rows are deferred (draft plus per-field touched
// flags); commit writes only what the user touched and triggers
// "settings:changed" once if anything was written, the same pattern as
// CatTemps.cpp (gm-flw.7). CatMachine.h publishes MachineDraft and the
// row-provider hook the schedule editor (gm-flw.11, next wave) codes
// against: this file calls the hook only for row index 3, and this wave's
// weak default (below) leaves that row absent (rowCount 3).
#include "CatMachine.h"
#include "SettingsModel.h"
#include "SettingsLog.h"
#include "SettingsRows.h"
#include "SettingsUI.h"

#include <display/core/PluginManager.h>
#include <display/core/Settings.h>
#include <display/core/constants.h>
#include <display/main.h>
#include <display/ui/default/eez/images.h>

#include <cstdio>

#include "esp_log.h"

// Weak default: no schedules file linked yet, so the Machine page is 3 rows
// and row index 3 is never reached. The schedule editor's own file
// (gm-flw.11) defines the strong versions, returning 1 and building the
// "Schedules" action row, wiring its onActivate to push the editor's own
// page(s) with `draft`.
int __attribute__((weak)) settingsMachineExtraRowCount(MachineDraft * /*draft*/) { return 0; }
void __attribute__((weak))
settingsMachineBuildExtraRow(MachineDraft * /*draft*/, int /*index*/, lv_obj_t * /*parent*/, SettingsUI & /*ui*/) {}

namespace {

struct MachineCtx {
    MachineDraft draft;

    lv_obj_t *startupRow = nullptr;
    lv_obj_t *standbyRow = nullptr;

    // Cached from the first buildRow call (enter/commit/reconcile receive
    // only ctx, never the SettingsUI&), same as CatTemps.cpp: commit needs
    // it to trigger "settings:changed" without reaching into Controller's
    // private pluginManager.
    PluginManager *plugins = nullptr;
};

std::vector<AutoWakeupSchedule> toAutoWakeupSchedules(const std::vector<settingsui::ScheduleDraft> &drafts) {
    std::vector<AutoWakeupSchedule> out;
    out.reserve(drafts.size());
    for (const auto &d : drafts) {
        AutoWakeupSchedule s{String(d.time.c_str())};
        for (int i = 0; i < 7; i++) {
            s.days[i] = d.days[i];
        }
        out.push_back(s);
    }
    return out;
}

} // namespace

// Moved out of the anonymous namespace above (CatMachine.h) so the schedule
// editor's own file (gm-flw.11) can call it too, to re-read the stored
// vector into the shared MachineDraft on reconcile.
std::vector<settingsui::ScheduleDraft> fromAutoWakeupSchedules(const std::vector<AutoWakeupSchedule> &schedules) {
    std::vector<settingsui::ScheduleDraft> out;
    out.reserve(schedules.size());
    for (const auto &s : schedules) {
        settingsui::ScheduleDraft d;
        d.time = s.time.c_str();
        for (int i = 0; i < 7; i++) {
            d.days[i] = s.days[i];
        }
        out.push_back(d);
    }
    return out;
}

// At file scope, and declared in CatMachine.h, for the same reason as
// fromAutoWakeupSchedules above: the schedule pages call it too, because
// only the top page's reconcile runs (CatMachine.h).
void machineDraftReconcile(MachineDraft *draft, Settings &settings) {
    if (!draft->startupModeTouched) {
        draft->startupMode = settings.getStartupMode();
    }
    if (!draft->standbyTimeoutTouched) {
        draft->standbyTimeoutMs = settings.getStandbyTimeout();
    }
    if (!draft->autowakeupEnabledTouched) {
        draft->autowakeupEnabled = settings.isAutoWakeupEnabled();
    }
    if (!draft->schedulesTouched) {
        draft->schedules = fromAutoWakeupSchedules(settings.getAutoWakeupSchedules());
    }
}

namespace {

void setStartupValue(MachineCtx *ctx) {
    if (ctx->startupRow != nullptr) {
        settingsRowSetValue(ctx->startupRow,
                             settingsui::kStartupModeLabels[settingsui::startupModeIndexForValue(ctx->draft.startupMode)]);
    }
}

void setStandbyValue(MachineCtx *ctx) {
    if (ctx->standbyRow != nullptr) {
        settingsRowSetValue(ctx->standbyRow,
                             settingsui::formatNumeric(ctx->draft.standbyTimeoutMs, settingsui::kStandbyTimeoutSpec).c_str());
    }
}

void startupOnCycle(void *user, int dir) {
    auto *ctx = static_cast<MachineCtx *>(user);
    const int idx = settingsui::wrapIndex(settingsui::startupModeIndexForValue(ctx->draft.startupMode), 2, dir);
    ctx->draft.startupMode = settingsui::startupModeValueForIndex(idx);
    ctx->draft.startupModeTouched = true;
    setStartupValue(ctx);
}

void standbyOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<MachineCtx *>(user);
    ctx->draft.standbyTimeoutMs = settingsui::stepValue(ctx->draft.standbyTimeoutMs, dir, fast, settingsui::kStandbyTimeoutSpec);
    ctx->draft.standbyTimeoutTouched = true;
    setStandbyValue(ctx);
}

// The toggle widget owns its own On/Off text (SettingsRows.h), so onToggle
// here is a notification only, same as CatTemps.cpp's delay-adjust row.
void autowakeupOnToggle(void *user, bool value) {
    auto *ctx = static_cast<MachineCtx *>(user);
    ctx->draft.autowakeupEnabled = value;
    ctx->draft.autowakeupEnabledTouched = true;
}

int machineRowCount(void *ctx0) {
    auto *ctx = static_cast<MachineCtx *>(ctx0);
    return 3 + settingsMachineExtraRowCount(&ctx->draft);
}

void machineBuildRow(void *ctx0, int index, lv_obj_t *parent, SettingsUI &ui) {
    auto *ctx = static_cast<MachineCtx *>(ctx0);
    if (ctx->plugins == nullptr) {
        ctx->plugins = &ui.plugins();
    }
    switch (index) {
    case 0: {
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "Startup mode", "Startup mode", startupOnCycle, ctx);
        ctx->startupRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<MachineCtx *>(lv_event_get_user_data(e))->startupRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        setStartupValue(ctx);
        break;
    }
    case 1: {
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Standby timeout", "Standby timeout", standbyOnStep, ctx);
        ctx->standbyRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<MachineCtx *>(lv_event_get_user_data(e))->standbyRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        setStandbyValue(ctx);
        break;
    }
    case 2:
        settingsRowToggleCreate(ui, parent, "Auto wake-up", "Auto wake-up", ctx->draft.autowakeupEnabled, autowakeupOnToggle,
                                 ctx);
        break;
    default: // row 3, once gm-flw.11 lands (see settingsMachineExtraRowCount above)
        settingsMachineBuildExtraRow(&ctx->draft, index, parent, ui);
        break;
    }
}

// Snapshots Settings into the draft, under the shell's Settings::Guard
// (SettingsUI::pushPage). Runs once, before any row exists, so it cannot
// read anything back out of ctx's row pointers.
void machineEnter(void *ctx0) {
    auto *ctx = static_cast<MachineCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    ctx->draft.startupMode = settings.getStartupMode();
    ctx->draft.standbyTimeoutMs = settings.getStandbyTimeout();
    ctx->draft.autowakeupEnabled = settings.isAutoWakeupEnabled();
    ctx->draft.schedules = fromAutoWakeupSchedules(settings.getAutoWakeupSchedules());
    ctx->draft.startupModeTouched = false;
    ctx->draft.standbyTimeoutTouched = false;
    ctx->draft.autowakeupEnabledTouched = false;
    ctx->draft.schedulesTouched = false;
}

// After a settings:changed event: refreshes only the fields this visit has
// not edited. service() rebuilds the page unconditionally right after
// calling this, so the redraw itself is the shell's job; this just has to
// leave the draft holding the value that redraw should show.
void machineReconcile(void *ctx0) {
    auto *ctx = static_cast<MachineCtx *>(ctx0);
    machineDraftReconcile(&ctx->draft, controller.getSettings());
}

void machineCommit(void *ctx0) {
    auto *ctx = static_cast<MachineCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    char log[128];
    int used = std::snprintf(log, sizeof(log), "SettingsMachine: committed");
    bool wrote = false;

    if (ctx->draft.startupModeTouched) {
        settings.setStartupMode(ctx->draft.startupMode);
        settingsLogAppend(log, sizeof(log), used, " startup=%s",
                               ctx->draft.startupMode == MODE_BREW ? "brew" : "standby");
        wrote = true;
    }
    if (ctx->draft.standbyTimeoutTouched) {
        settings.setStandbyTimeout(static_cast<int>(ctx->draft.standbyTimeoutMs));
        settingsLogAppend(log, sizeof(log), used, " standby=%ld", ctx->draft.standbyTimeoutMs);
        wrote = true;
    }
    if (ctx->draft.autowakeupEnabledTouched) {
        settings.setAutoWakeupEnabled(ctx->draft.autowakeupEnabled);
        settingsLogAppend(log, sizeof(log), used, " autowakeup=%d", ctx->draft.autowakeupEnabled ? 1 : 0);
        wrote = true;
    }
    if (ctx->draft.schedulesTouched) {
        settings.setAutoWakeupSchedules(toAutoWakeupSchedules(ctx->draft.schedules));
        settingsLogAppend(log, sizeof(log), used, " schedules=%d", static_cast<int>(ctx->draft.schedules.size()));
        wrote = true;
    }

    if (!wrote) {
        return;
    }
    ESP_LOGI("SettingsUI", "%s", log);
    if (ctx->plugins != nullptr) {
        ctx->plugins->trigger("settings:changed");
    }
}

void *machineCreateCtx() { return new MachineCtx(); }

void machineDestroyCtx(void *ctx) { delete static_cast<MachineCtx *>(ctx); }

} // namespace

const SettingsCategoryDef kCatMachine = {
    "Machine", &img_power_40x40, machineRowCount, machineBuildRow, machineEnter, nullptr, machineCommit, machineReconcile,
    machineCreateCtx, machineDestroyCtx,
};
