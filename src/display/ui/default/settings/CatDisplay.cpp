// Display category: main and standby backlight brightness, the standby dim
// timeout, the 24-hour clock toggle, and the time zone (a region choice and
// a city choice). Main brightness is the one live row (CLAUDE.md, UI-pipeline
// invariants): every step writes Settings and calls DefaultUI::setBrightness
// plus markDirty() at once, mirroring how handleScreenChange/loop already
// apply it. Standby brightness, the dim timeout, the clock format and the
// zone are deferred (draft plus per-field touched flags), same shape as
// CatTemps.cpp. The zone commits through Controller::applyTimezone() so a
// changed zone reaches the running clock without a reboot.
#include "SettingsModel.h"
#include "SettingsLog.h"
#include "SettingsRows.h"
#include "SettingsUI.h"

#include <display/core/PluginManager.h>
#include <display/core/Settings.h>
#include <display/core/zones.h>
#include <display/main.h>
#include <display/ui/default/DefaultUI.h>
#include <display/ui/default/eez/images.h>

#include <cstdio>
#include <string>

#include "esp_log.h"

namespace {

// Wired to the real 461-entry table (zones.cpp) the same way
// test_settings_model.cpp's realZoneProvider() is; cheap enough (a pair of
// std::function pointers) to build fresh wherever it is needed rather than
// caching it in the ctx.
settingsui::ZoneProvider zoneProvider() {
    settingsui::ZoneProvider zones;
    zones.count = []() -> size_t { return zones_count(); };
    zones.name = [](size_t i) -> const char * { return zones_entry(i).name; };
    return zones;
}

struct CatDisplayCtx {
    long mainBrightness = 16;            // live, kMainBrightnessSpec
    long standbyBrightness = 8;          // deferred, kStandbyBrightnessSpec
    long standbyBrightnessTimeout = 60000; // deferred, ms, kStandbyBrightnessTimeoutSpec
    bool clock24h = true;                // deferred
    int zoneRegion = 0;                  // deferred, region+city commit as one field
    int zoneCity = 0;

    bool mainBrightnessTouched = false;
    // The value main brightness's live path last wrote to Settings, so
    // commit can tell a web save landed on top of it in the same visit
    // (Settings::getMainBrightness() would then read something else) from
    // the ordinary case where nothing wrote over it.
    long mainBrightnessLastWritten = 16;
    bool standbyBrightnessTouched = false;
    bool timeoutTouched = false;
    bool clock24hTouched = false;
    bool zoneTouched = false;

    lv_obj_t *mainBrightnessRow = nullptr;
    lv_obj_t *standbyBrightnessRow = nullptr;
    lv_obj_t *timeoutRow = nullptr;
    lv_obj_t *regionRow = nullptr;
    lv_obj_t *cityRow = nullptr;

    // Cached from the first buildRow call (enter/commit/reconcile receive
    // only ctx, never the SettingsUI&); see CatTemps.cpp's plugins field for
    // the same reasoning. Main brightness's live path needs DefaultUI to
    // apply the backlight and mark a redraw; commit needs both to re-assert
    // a live field a web save clobbered and to trigger settings:changed.
    DefaultUI *ui = nullptr;
    PluginManager *plugins = nullptr;
};

void setMainBrightnessValue(CatDisplayCtx *ctx) {
    if (ctx->mainBrightnessRow != nullptr) {
        settingsRowSetValue(ctx->mainBrightnessRow,
                             settingsui::formatNumeric(ctx->mainBrightness, settingsui::kMainBrightnessSpec).c_str());
    }
}

void setStandbyBrightnessValue(CatDisplayCtx *ctx) {
    if (ctx->standbyBrightnessRow != nullptr) {
        settingsRowSetValue(
            ctx->standbyBrightnessRow,
            settingsui::formatNumeric(ctx->standbyBrightness, settingsui::kStandbyBrightnessSpec).c_str());
    }
}

void setTimeoutValue(CatDisplayCtx *ctx) {
    if (ctx->timeoutRow != nullptr) {
        settingsRowSetValue(
            ctx->timeoutRow,
            settingsui::formatNumeric(ctx->standbyBrightnessTimeout, settingsui::kStandbyBrightnessTimeoutSpec).c_str());
    }
}

void setRegionValue(CatDisplayCtx *ctx) {
    if (ctx->regionRow != nullptr) {
        settingsRowSetValue(ctx->regionRow, settingsui::regionName(zoneProvider(), ctx->zoneRegion).c_str());
    }
}

void setCityValue(CatDisplayCtx *ctx) {
    if (ctx->cityRow != nullptr) {
        settingsRowSetValue(ctx->cityRow, settingsui::cityLabel(zoneProvider(), ctx->zoneRegion, ctx->zoneCity).c_str());
    }
}

void mainBrightnessOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<CatDisplayCtx *>(user);
    ctx->mainBrightness = settingsui::stepValue(ctx->mainBrightness, dir, fast, settingsui::kMainBrightnessSpec);
    ctx->mainBrightnessTouched = true;
    ctx->mainBrightnessLastWritten = ctx->mainBrightness;
    controller.getSettings().setMainBrightness(static_cast<int>(ctx->mainBrightness));
    if (ctx->ui != nullptr) {
        ctx->ui->setBrightness(static_cast<int>(ctx->mainBrightness));
        ctx->ui->markDirty();
    }
    setMainBrightnessValue(ctx);
}

void standbyBrightnessOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<CatDisplayCtx *>(user);
    ctx->standbyBrightness = settingsui::stepValue(ctx->standbyBrightness, dir, fast, settingsui::kStandbyBrightnessSpec);
    ctx->standbyBrightnessTouched = true;
    setStandbyBrightnessValue(ctx);
}

void timeoutOnStep(void *user, int dir, bool fast) {
    auto *ctx = static_cast<CatDisplayCtx *>(user);
    ctx->standbyBrightnessTimeout =
        settingsui::stepValue(ctx->standbyBrightnessTimeout, dir, fast, settingsui::kStandbyBrightnessTimeoutSpec);
    ctx->timeoutTouched = true;
    setTimeoutValue(ctx);
}

// The toggle widget owns its own "On"/"Off" text (SettingsRows.h), so
// onToggle here is a notification only.
void clock24hOnToggle(void *user, bool value) {
    auto *ctx = static_cast<CatDisplayCtx *>(user);
    ctx->clock24h = value;
    ctx->clock24hTouched = true;
}

void regionOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatDisplayCtx *>(user);
    const settingsui::ZoneProvider zones = zoneProvider();
    ctx->zoneRegion = settingsui::wrapIndex(ctx->zoneRegion, settingsui::regionCount(zones), dir);
    ctx->zoneCity = 0; // changing the region selects that region's first city
    ctx->zoneTouched = true;
    setRegionValue(ctx);
    setCityValue(ctx); // no-op if the city row is on the other page right now
}

void cityOnCycle(void *user, int dir) {
    auto *ctx = static_cast<CatDisplayCtx *>(user);
    const settingsui::ZoneProvider zones = zoneProvider();
    ctx->zoneCity = settingsui::wrapIndex(ctx->zoneCity, settingsui::cityCount(zones, ctx->zoneRegion), dir);
    ctx->zoneTouched = true;
    setCityValue(ctx);
}

int displayRowCount(void * /*ctx*/) { return 6; }

void displayBuildRow(void *ctx0, int index, lv_obj_t *parent, SettingsUI &ui) {
    auto *ctx = static_cast<CatDisplayCtx *>(ctx0);
    if (ctx->ui == nullptr) {
        ctx->ui = &ui.ui();
    }
    if (ctx->plugins == nullptr) {
        ctx->plugins = &ui.plugins();
    }
    switch (index) {
    case 0: {
        lv_obj_t *row =
            settingsRowStepperCreate(ui, parent, "Main brightness", "Main brightness", mainBrightnessOnStep, ctx);
        ctx->mainBrightnessRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) { static_cast<CatDisplayCtx *>(lv_event_get_user_data(e))->mainBrightnessRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        setMainBrightnessValue(ctx);
        break;
    }
    case 1: {
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Standby brightness", "Standby brightness",
                                                  standbyBrightnessOnStep, ctx);
        ctx->standbyBrightnessRow = row;
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) {
                static_cast<CatDisplayCtx *>(lv_event_get_user_data(e))->standbyBrightnessRow = nullptr;
            },
            LV_EVENT_DELETE, ctx);
        setStandbyBrightnessValue(ctx);
        break;
    }
    case 2: {
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "Dim after", "Dim after", timeoutOnStep, ctx);
        ctx->timeoutRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatDisplayCtx *>(lv_event_get_user_data(e))->timeoutRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        setTimeoutValue(ctx);
        break;
    }
    case 3:
        settingsRowToggleCreate(ui, parent, "24-hour clock", "24-hour clock", ctx->clock24h, clock24hOnToggle, ctx);
        break;
    case 4: {
        lv_obj_t *row =
            settingsRowChoiceCreate(ui, parent, "Time zone region", "Time zone region", regionOnCycle, ctx);
        ctx->regionRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatDisplayCtx *>(lv_event_get_user_data(e))->regionRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        setRegionValue(ctx);
        break;
    }
    case 5: {
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "City", "City", cityOnCycle, ctx);
        ctx->cityRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatDisplayCtx *>(lv_event_get_user_data(e))->cityRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        setCityValue(ctx);
        break;
    }
    default:
        break;
    }
}

// Snapshots Settings into the draft, under the shell's Settings::Guard
// (SettingsUI::pushPage). A stored zone name the table does not have (an
// import from a build with a different zones.cpp, or hand-edited NVS) locates
// nothing; the model's fallback is region "Etc" city "UTC", found the same
// way any other zone is (a locate() call), rather than assuming its table
// position, since nothing above this promises "Etc/UTC" is the region's
// first entry.
void displayEnter(void *ctx0) {
    auto *ctx = static_cast<CatDisplayCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    ctx->mainBrightness = settings.getMainBrightness();
    ctx->standbyBrightness = settings.getStandbyBrightness();
    ctx->standbyBrightnessTimeout = settings.getStandbyBrightnessTimeout();
    ctx->clock24h = settings.isClock24hFormat();

    const settingsui::ZoneProvider zones = zoneProvider();
    int region = 0;
    int city = 0;
    if (!settingsui::locate(zones, std::string(settings.getTimezone().c_str()), region, city)) {
        settingsui::locate(zones, "Etc/UTC", region, city);
    }
    ctx->zoneRegion = region;
    ctx->zoneCity = city;

    ctx->mainBrightnessTouched = false;
    ctx->mainBrightnessLastWritten = ctx->mainBrightness;
    ctx->standbyBrightnessTouched = false;
    ctx->timeoutTouched = false;
    ctx->clock24hTouched = false;
    ctx->zoneTouched = false;
}

// After a settings:changed event: refreshes only the fields this visit has
// not edited. service() rebuilds the page unconditionally right after
// calling this, so the redraw itself is the shell's job.
void displayReconcile(void *ctx0) {
    auto *ctx = static_cast<CatDisplayCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    if (!ctx->mainBrightnessTouched) {
        ctx->mainBrightness = settings.getMainBrightness();
        ctx->mainBrightnessLastWritten = ctx->mainBrightness;
    }
    if (!ctx->standbyBrightnessTouched) {
        ctx->standbyBrightness = settings.getStandbyBrightness();
    }
    if (!ctx->timeoutTouched) {
        ctx->standbyBrightnessTimeout = settings.getStandbyBrightnessTimeout();
    }
    if (!ctx->clock24hTouched) {
        ctx->clock24h = settings.isClock24hFormat();
    }
    if (!ctx->zoneTouched) {
        const settingsui::ZoneProvider zones = zoneProvider();
        int region = 0;
        int city = 0;
        if (!settingsui::locate(zones, std::string(settings.getTimezone().c_str()), region, city)) {
            settingsui::locate(zones, "Etc/UTC", region, city);
        }
        ctx->zoneRegion = region;
        ctx->zoneCity = city;
    }
}

void displayCommit(void *ctx0) {
    auto *ctx = static_cast<CatDisplayCtx *>(ctx0);
    Settings &settings = controller.getSettings();
    char log[160];
    int used = std::snprintf(log, sizeof(log), "SettingsDisplay: committed");
    bool wrote = false;
    bool liveReasserted = false;

    // Main brightness is already applied live (every step wrote Settings
    // directly); commit only has work to do here when a web save landed on
    // top of it in the same visit, in which case the touched field wins.
    if (ctx->mainBrightnessTouched && settings.getMainBrightness() != static_cast<int>(ctx->mainBrightnessLastWritten)) {
        settings.setMainBrightness(static_cast<int>(ctx->mainBrightnessLastWritten));
        if (ctx->ui != nullptr) {
            ctx->ui->setBrightness(static_cast<int>(ctx->mainBrightnessLastWritten));
        }
        settingsLogAppend(log, sizeof(log), used, " mainBrightness=%ld", ctx->mainBrightnessLastWritten);
        wrote = true;
        liveReasserted = true;
    }
    if (ctx->standbyBrightnessTouched) {
        settings.setStandbyBrightness(static_cast<int>(ctx->standbyBrightness));
        settingsLogAppend(log, sizeof(log), used, " standbyBrightness=%ld", ctx->standbyBrightness);
        wrote = true;
    }
    if (ctx->timeoutTouched) {
        settings.setStandbyBrightnessTimeout(static_cast<int>(ctx->standbyBrightnessTimeout));
        settingsLogAppend(log, sizeof(log), used, " dimAfter=%ld", ctx->standbyBrightnessTimeout);
        wrote = true;
    }
    if (ctx->clock24hTouched) {
        settings.setClockFormat(ctx->clock24h);
        settingsLogAppend(log, sizeof(log), used, " clock24h=%d", ctx->clock24h ? 1 : 0);
        wrote = true;
    }
    if (ctx->zoneTouched) {
        const settingsui::ZoneProvider zones = zoneProvider();
        const std::string name = settingsui::zoneName(zones, ctx->zoneRegion, ctx->zoneCity);
        settings.setTimezone(name.c_str());
        controller.applyTimezone();
        settingsLogAppend(log, sizeof(log), used, " timezone=%s", name.c_str());
        wrote = true;
    }

    if (!wrote) {
        return;
    }
    ESP_LOGI("SettingsUI", "%s", log);
    if (liveReasserted && ctx->ui != nullptr) {
        ctx->ui->markDirty();
    }
    if (ctx->plugins != nullptr) {
        ctx->plugins->trigger("settings:changed");
    }
}

void *displayCreateCtx() { return new CatDisplayCtx(); }

void displayDestroyCtx(void *ctx) { delete static_cast<CatDisplayCtx *>(ctx); }

} // namespace

const SettingsCategoryDef kCatDisplay = {
    "Display", &img_clock_40x40, displayRowCount, displayBuildRow, displayEnter, nullptr, displayCommit,
    displayReconcile, displayCreateCtx, displayDestroyCtx,
};
