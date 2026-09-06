// Temperatures & timing category: boiler temperature offset and pressure
// sensor rating behind hold-to-unlock steppers, brew/grind delay steppers
// and the delay auto-adjust toggle. All five rows are deferred (draft plus
// per-field touched flags); commit writes only what the user touched, so
// Controller::loopLogic's own delay auto-adjust writes (running under the
// same Settings::Guard, possibly finishing after the UI has moved on) are
// never clobbered by an untouched delay row here (CLAUDE.md, gm-flw.7).
#include "SettingsModel.h"
#include "SettingsRows.h"
#include "SettingsUI.h"

#include <display/core/PluginManager.h>
#include <display/core/Settings.h>
#include <display/ui/default/eez/images.h>
#include <display/main.h>

#include <cmath>
#include <cstdio>

#include "esp_log.h"

namespace {

struct CatTempsCtx {
    long offset = 0;     // C, kTemperatureOffsetSpec
    long scaling = 0;    // tenths of a bar, kPressureScalingSpec
    long brewDelay = 0;  // ms, kBrewDelaySpec
    long grindDelay = 0; // ms, kGrindDelaySpec
    bool delayAdjust = true;

    bool offsetTouched = false;
    bool scalingTouched = false;
    bool brewTouched = false;
    bool grindTouched = false;
    bool delayAdjustTouched = false;

    // Independent of offsetTouched/scalingTouched: a web save touching an
    // unrelated field reconciles and rebuilds the page while this visit's
    // unlock is still live, and settingsRowLockedCreate always starts a
    // freshly built row locked, so buildRow re-asserts these with
    // settingsRowSetLocked rather than letting an in-progress unlock get
    // silently undone by a rebuild that was never a "leave" of the category.
    bool offsetLocked = true;
    bool scalingLocked = true;

    lv_obj_t *offsetRow = nullptr;
    lv_obj_t *scalingRow = nullptr;
    lv_obj_t *brewRow = nullptr;
    lv_obj_t *grindRow = nullptr;

    // Cached from the first buildRow call (enter/commit/reconcile receive
    // only ctx, never the SettingsUI&), so commit can trigger
    // "settings:changed" without reaching into Controller's private
    // pluginManager. All five rows are built once, synchronously, right
    // after enter() (rowCount fits one page), so this is always set before
    // any route that can call commit runs.
    PluginManager *plugins = nullptr;
};

void setOffsetValue(CatTempsCtx *ctx) {
    if (ctx->offsetRow != nullptr) {
        settingsRowSetValue(ctx->offsetRow, settingsui::formatNumeric(ctx->offset, settingsui::kTemperatureOffsetSpec).c_str());
    }
}

void setScalingValue(CatTempsCtx *ctx) {
    if (ctx->scalingRow != nullptr) {
        settingsRowSetValue(ctx->scalingRow, settingsui::formatNumeric(ctx->scaling, settingsui::kPressureScalingSpec).c_str());
    }
}

void setBrewValue(CatTempsCtx *ctx) {
    if (ctx->brewRow != nullptr) {
        settingsRowSetValue(ctx->brewRow, settingsui::formatNumeric(ctx->brewDelay, settingsui::kBrewDelaySpec).c_str());
    }
}

void setGrindValue(CatTempsCtx *ctx) {
    if (ctx->grindRow != nullptr) {
        settingsRowSetValue(ctx->grindRow, settingsui::formatNumeric(ctx->grindDelay, settingsui::kGrindDelaySpec).c_str());
    }
}

void offsetOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<CatTempsCtx *>(user);
    ctx->offset = settingsui::stepValue(ctx->offset, dir, fast, settingsui::kTemperatureOffsetSpec);
    ctx->offsetTouched = true;
    setOffsetValue(ctx);
}

void offsetOnUnlocked(void *user) {
    auto *ctx = static_cast<CatTempsCtx *>(user);
    ctx->offsetLocked = false;
    setOffsetValue(ctx);
}

void scalingOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<CatTempsCtx *>(user);
    ctx->scaling = settingsui::stepValue(ctx->scaling, dir, fast, settingsui::kPressureScalingSpec);
    ctx->scalingTouched = true;
    setScalingValue(ctx);
}

void scalingOnUnlocked(void *user) {
    auto *ctx = static_cast<CatTempsCtx *>(user);
    ctx->scalingLocked = false;
    setScalingValue(ctx);
}

void brewOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<CatTempsCtx *>(user);
    ctx->brewDelay = settingsui::stepValue(ctx->brewDelay, dir, fast, settingsui::kBrewDelaySpec);
    ctx->brewTouched = true;
    setBrewValue(ctx);
}

void grindOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<CatTempsCtx *>(user);
    ctx->grindDelay = settingsui::stepValue(ctx->grindDelay, dir, fast, settingsui::kGrindDelaySpec);
    ctx->grindTouched = true;
    setGrindValue(ctx);
}

// The toggle widget owns its own "On"/"Off" text (SettingsRows.h), so
// onToggle here is a notification only.
void adjustOnToggle(void *user, bool value) {
    auto *ctx = static_cast<CatTempsCtx *>(user);
    ctx->delayAdjust = value;
    ctx->delayAdjustTouched = true;
}

int tempsRowCount(void * /*ctx*/) { return 5; }

void tempsBuildRow(void *ctx0, int index, lv_obj_t *parent, SettingsUI &ui) {
    auto *ctx = static_cast<CatTempsCtx *>(ctx0);
    if (ctx->plugins == nullptr) {
        ctx->plugins = &ui.plugins();
    }
    switch (index) {
    case 0: {
        lv_obj_t *row = settingsRowLockedCreate(ui, parent, "Temperature offset", "Temperature offset", offsetOnStep,
                                                 offsetOnUnlocked, ctx);
        ctx->offsetRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatTempsCtx *>(lv_event_get_user_data(e))->offsetRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        if (!ctx->offsetLocked) {
            settingsRowSetLocked(row, false);
            setOffsetValue(ctx);
        }
        break;
    }
    case 1: {
        lv_obj_t *row = settingsRowLockedCreate(ui, parent, "Pressure sensor", "Pressure sensor", scalingOnStep,
                                                 scalingOnUnlocked, ctx);
        ctx->scalingRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatTempsCtx *>(lv_event_get_user_data(e))->scalingRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        if (!ctx->scalingLocked) {
            settingsRowSetLocked(row, false);
            setScalingValue(ctx);
        }
        break;
    }
    case 2: {
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Brew delay", "Brew delay", brewOnStep, ctx);
        ctx->brewRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatTempsCtx *>(lv_event_get_user_data(e))->brewRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        setBrewValue(ctx);
        break;
    }
    case 3: {
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Grind delay", "Grind delay", grindOnStep, ctx);
        ctx->grindRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatTempsCtx *>(lv_event_get_user_data(e))->grindRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        setGrindValue(ctx);
        break;
    }
    case 4:
        settingsRowToggleCreate(ui, parent, "Delay auto-adjust", "Delay auto-adjust", ctx->delayAdjust, adjustOnToggle,
                                 ctx);
        break;
    default:
        break;
    }
}

// Snapshots Settings into the draft, under the shell's Settings::Guard
// (SettingsUI::pushPage). Runs once, before any row exists, so it cannot
// read anything back out of ctx's row pointers.
void tempsEnter(void *ctx0) {
    auto *ctx = static_cast<CatTempsCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    ctx->offset = settings.getTemperatureOffset();
    ctx->scaling = std::lround(settings.getPressureScaling() * 10.0f);
    ctx->brewDelay = std::lround(settings.getBrewDelay());
    ctx->grindDelay = std::lround(settings.getGrindDelay());
    ctx->delayAdjust = settings.isDelayAdjust();
    ctx->offsetTouched = false;
    ctx->scalingTouched = false;
    ctx->brewTouched = false;
    ctx->grindTouched = false;
    ctx->delayAdjustTouched = false;
    ctx->offsetLocked = true;
    ctx->scalingLocked = true;
}

// After a settings:changed event: refreshes only the fields this visit
// has not edited. service() rebuilds the page unconditionally right after
// calling this, so the redraw itself is the shell's job; this just has to
// leave the draft holding the value that redraw should show.
void tempsReconcile(void *ctx0) {
    auto *ctx = static_cast<CatTempsCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    if (!ctx->offsetTouched) {
        ctx->offset = settings.getTemperatureOffset();
    }
    if (!ctx->scalingTouched) {
        ctx->scaling = std::lround(settings.getPressureScaling() * 10.0f);
    }
    if (!ctx->brewTouched) {
        ctx->brewDelay = std::lround(settings.getBrewDelay());
    }
    if (!ctx->grindTouched) {
        ctx->grindDelay = std::lround(settings.getGrindDelay());
    }
    if (!ctx->delayAdjustTouched) {
        ctx->delayAdjust = settings.isDelayAdjust();
    }
}

void tempsCommit(void *ctx0) {
    auto *ctx = static_cast<CatTempsCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    char log[128];
    int used = std::snprintf(log, sizeof(log), "SettingsTemps: committed");
    bool wrote = false;

    if (ctx->offsetTouched) {
        const int offset = static_cast<int>(ctx->offset);
        settings.setTemperatureOffset(offset);
        used += std::snprintf(log + used, sizeof(log) - used, " offset=%d", offset);
        wrote = true;
    }
    if (ctx->scalingTouched) {
        const float scaling = static_cast<float>(ctx->scaling) / 10.0f;
        settings.setPressureScaling(scaling);
        used += std::snprintf(log + used, sizeof(log) - used, " scaling=%.1f", scaling);
        wrote = true;
    }
    if (ctx->brewTouched) {
        const int brew = static_cast<int>(ctx->brewDelay);
        settings.setBrewDelay(static_cast<double>(brew));
        used += std::snprintf(log + used, sizeof(log) - used, " brew=%d", brew);
        wrote = true;
    }
    if (ctx->grindTouched) {
        const int grind = static_cast<int>(ctx->grindDelay);
        settings.setGrindDelay(static_cast<double>(grind));
        used += std::snprintf(log + used, sizeof(log) - used, " grind=%d", grind);
        wrote = true;
    }
    if (ctx->delayAdjustTouched) {
        settings.setDelayAdjust(ctx->delayAdjust);
        std::snprintf(log + used, sizeof(log) - used, " adjust=%d", ctx->delayAdjust ? 1 : 0);
        wrote = true;
    }

    if (!wrote) {
        return;
    }
    if (ctx->scalingTouched) {
        controller.setPressureScale();
    }
    ESP_LOGI("SettingsUI", "%s", log);
    if (ctx->plugins != nullptr) {
        ctx->plugins->trigger("settings:changed");
    }
}

void *tempsCreateCtx() { return new CatTempsCtx(); }

void tempsDestroyCtx(void *ctx) { delete static_cast<CatTempsCtx *>(ctx); }

} // namespace

const SettingsCategoryDef kCatTemps = {
    "Temperatures & timing", &img_thermometer_half_40x40, tempsRowCount, tempsBuildRow, tempsEnter, nullptr,
    tempsCommit, tempsReconcile, tempsCreateCtx, tempsDestroyCtx,
};
