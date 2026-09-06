// Status category: read-only versions/network/scale/time rows plus the
// hold-to-restart row. Every value is read straight from Controller/Settings/
// WiFi on demand (buildRow's initial fill and refresh()'s once-a-second
// tick); there is no draft, so enter()/reconcile() have nothing to do and
// commit() writes nothing (CLAUDE.md, gm-flw.12). The one exception is
// Restart, which calls Settings::flushNow() itself, outside the normal
// commit path, because a restart must not proceed on an unflushed change.
#include "SettingsRows.h"
#include "SettingsUI.h"

#include <display/core/Settings.h>
#include <display/main.h>
#include <display/ui/default/eez/images.h>
#include <display/ui/default/eez/screens.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "esp_log.h"

namespace {

struct CatStatusCtx {
    lv_obj_t *displayFwRow = nullptr;
    lv_obj_t *controllerFwRow = nullptr;
    lv_obj_t *networkRow = nullptr;
    lv_obj_t *ipRow = nullptr;
    lv_obj_t *controllerRow = nullptr;
    lv_obj_t *scaleRow = nullptr;
    lv_obj_t *timeRow = nullptr;
    lv_obj_t *restartRow = nullptr;

    // Last text written to each row, so refresh() (called once a second
    // while either page of this category is visible) rewrites a label only
    // when its value actually changed, never unconditionally every tick.
    // Cleared to "" whenever the row is (re)built, so the first refresh (or
    // the initial fill in buildRow itself) always writes.
    char lastControllerFw[kSettingsRowValueCap] = {0};
    char lastNetwork[kSettingsRowValueCap] = {0};
    char lastIp[kSettingsRowValueCap] = {0};
    char lastController[kSettingsRowValueCap] = {0};
    char lastScale[kSettingsRowValueCap] = {0};
    char lastTime[kSettingsRowValueCap] = {0};
};

void setIfChanged(lv_obj_t *row, char *cache, const char *value) {
    if (row == nullptr) {
        return; // this row's page is not the one currently showing
    }
    if (strncmp(cache, value, kSettingsRowValueCap) == 0) {
        return;
    }
    snprintf(cache, kSettingsRowValueCap, "%s", value);
    settingsRowSetValue(row, value);
}

// Shared by buildRow (initial fill) and refresh (the once-a-second tick):
// every value is read straight from Controller/Settings/WiFi, never staged,
// so the two call sites cannot disagree about what "current" means.

void updateControllerFw(CatStatusCtx *ctx) {
    const bool connected = controller.getClientController()->isConnected();
    const String value = connected ? controller.getSystemInfo().version : String("Not connected");
    setIfChanged(ctx->controllerFwRow, ctx->lastControllerFw, value.c_str());
}

void updateNetwork(CatStatusCtx *ctx) {
    const bool apActive = controller.getUI()->isApActive();
    const bool wifiConnected = !apActive && WiFi.status() == WL_CONNECTED;
    const String value =
        apActive ? String("Access point") : (wifiConnected ? controller.getSettings().getWifiSsid() : String("Disconnected"));
    setIfChanged(ctx->networkRow, ctx->lastNetwork, value.c_str());
}

void updateIp(CatStatusCtx *ctx) {
    const bool apActive = controller.getUI()->isApActive();
    const String value = apActive ? String("4.4.4.1") : WiFi.localIP().toString();
    setIfChanged(ctx->ipRow, ctx->lastIp, value.c_str());
}

void updateController(CatStatusCtx *ctx) {
    const bool connected = controller.getClientController()->isConnected();
    setIfChanged(ctx->controllerRow, ctx->lastController, connected ? "Connected" : "Disconnected");
}

void updateScale(CatStatusCtx *ctx) {
    const String name = controller.getActiveScaleSourceName();
    const bool healthy = controller.isScaleSourceHealthy(controller.getEffectiveScaleSource());
    char buf[kSettingsRowValueCap];
    snprintf(buf, sizeof(buf), "%s %s", name.c_str(), healthy ? "ok" : "no data");
    setIfChanged(ctx->scaleRow, ctx->lastScale, buf);
}

void updateTime(CatStatusCtx *ctx) {
    // Same validity check as AutoWakeupPlugin::isTimeValid: year > 2020 means
    // NTP (or the sim's host clock, always valid) has set the RTC.
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    char buf[kSettingsRowValueCap] = "Not synchronised";
    if (timeinfo.tm_year > (2020 - 1900)) {
        const bool clock24h = controller.getSettings().isClock24hFormat();
        strftime(buf, sizeof(buf), clock24h ? "%H:%M:%S" : "%I:%M:%S %p", &timeinfo);
        if (!clock24h && buf[0] == '0') {
            buf[0] = ' '; // matches DefaultUI::updateSystemStatus's standby clock
        }
    }
    setIfChanged(ctx->timeRow, ctx->lastTime, buf);
}

// Leaves settings the same way a standby timeout does: changeScreen only
// queues the target, and handleScreenChange commits and tears the cover down
// on the next UI pass before the info screen loads.
void deviceInfoOnActivate(void * /*user*/) { controller.getUI()->changeScreen(SCREEN_ID_INFO_SCREEN); }

void restartOnConfirm(void *user) {
    auto *ctx = static_cast<CatStatusCtx *>(user);
    Settings &settings = controller.getSettings();
    // Held across the log line and flushNow() so a hold that lands while the
    // periodic 5 s flush is mid-doSave() (a different task, holding the same
    // recursive mutex) waits for that flush's own log lines to finish before
    // this one is emitted; the two never interleave.
    Settings::Guard guard(settings);
    ESP_LOGI("Settings", "restart from display");
    if (settings.flushNow()) {
        ESP.restart();
        return;
    }
    if (ctx->restartRow != nullptr) {
        settingsRowSetValue(ctx->restartRow, "Save failed, hold to retry");
    }
}

int statusRowCount(void * /*ctx*/) { return 9; }

void statusBuildRow(void *ctx0, int index, lv_obj_t *parent, SettingsUI &ui) {
    auto *ctx = static_cast<CatStatusCtx *>(ctx0);
    switch (index) {
    case 0: {
        lv_obj_t *row = settingsRowInfoCreate(ui, parent, "Display firmware", "Display firmware");
        ctx->displayFwRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatStatusCtx *>(lv_event_get_user_data(e))->displayFwRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        // Never changes at runtime: set once here, no entry in refresh().
        settingsRowSetValue(row, BUILD_GIT_VERSION);
        break;
    }
    case 1: {
        lv_obj_t *row = settingsRowInfoCreate(ui, parent, "Controller firmware", "Controller firmware");
        ctx->controllerFwRow = row;
        ctx->lastControllerFw[0] = '\0';
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatStatusCtx *>(lv_event_get_user_data(e))->controllerFwRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        updateControllerFw(ctx);
        break;
    }
    case 2: {
        lv_obj_t *row = settingsRowInfoCreate(ui, parent, "Network", "Network");
        ctx->networkRow = row;
        ctx->lastNetwork[0] = '\0';
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatStatusCtx *>(lv_event_get_user_data(e))->networkRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        updateNetwork(ctx);
        break;
    }
    case 3: {
        lv_obj_t *row = settingsRowInfoCreate(ui, parent, "IP address", "IP address");
        ctx->ipRow = row;
        ctx->lastIp[0] = '\0';
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatStatusCtx *>(lv_event_get_user_data(e))->ipRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        updateIp(ctx);
        break;
    }
    case 4: {
        lv_obj_t *row = settingsRowInfoCreate(ui, parent, "Controller", "Controller");
        ctx->controllerRow = row;
        ctx->lastController[0] = '\0';
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatStatusCtx *>(lv_event_get_user_data(e))->controllerRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        updateController(ctx);
        break;
    }
    case 5: {
        lv_obj_t *row = settingsRowInfoCreate(ui, parent, "Scale", "Scale");
        ctx->scaleRow = row;
        ctx->lastScale[0] = '\0';
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatStatusCtx *>(lv_event_get_user_data(e))->scaleRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        updateScale(ctx);
        break;
    }
    case 6: {
        lv_obj_t *row = settingsRowInfoCreate(ui, parent, "Time", "Time");
        ctx->timeRow = row;
        ctx->lastTime[0] = '\0';
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatStatusCtx *>(lv_event_get_user_data(e))->timeRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        updateTime(ctx);
        break;
    }
    case 7: {
        // The only place that shows the WiFi setup QR code while the config
        // access point is active, since the menu's info button went (gm-z7x).
        settingsRowActionCreate(ui, parent, "Device info", "Device info", deviceInfoOnActivate, nullptr);
        break;
    }
    case 8: {
        lv_obj_t *row = settingsRowConfirmCreate(ui, parent, "Restart", "Restart", restartOnConfirm, ctx);
        ctx->restartRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<CatStatusCtx *>(lv_event_get_user_data(e))->restartRow = nullptr; },
            LV_EVENT_DELETE, ctx);
        settingsRowSetValue(row, "Hold to restart");
        break;
    }
    default:
        break;
    }
}

// Stateless: nothing here is a draft, so there is nothing to snapshot.
void statusEnter(void * /*ctx*/) {}

// Once a second while either page of this category is open; buildRow already
// filled whichever rows exist right after this page was (re)built, so this
// only has to keep them current between rebuilds. Each update* function
// no-ops on a row whose page is not the one currently showing (its obj
// pointer was nulled by that row's own LV_EVENT_DELETE). Display firmware is
// a compile-time constant and never needs a refresh entry.
void statusRefresh(void *ctx0) {
    auto *ctx = static_cast<CatStatusCtx *>(ctx0);
    updateControllerFw(ctx);
    updateNetwork(ctx);
    updateIp(ctx);
    updateController(ctx);
    updateScale(ctx);
    updateTime(ctx);
}

// Nothing in this category writes Settings on commit; Restart writes through
// its own flushNow() call, outside the normal enter/commit cycle.
void statusCommit(void * /*ctx*/) {}

void *statusCreateCtx() { return new CatStatusCtx(); }

void statusDestroyCtx(void *ctx) { delete static_cast<CatStatusCtx *>(ctx); }

} // namespace

const SettingsCategoryDef kCatStatus = {
    "Status", &img_info_40x40, statusRowCount, statusBuildRow, statusEnter, statusRefresh, statusCommit, nullptr,
    statusCreateCtx, statusDestroyCtx,
};
