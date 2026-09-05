#include "DefaultUI.h"

#include <WiFi.h>
#ifdef GM_ANIM_BENCH
#include <display/ui/default/SleepAnimation.h>
const BenchGateState &bench_gate_state() {
    static BenchGateState state;
    return state;
}
#endif
#include <display/core/Controller.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/Process.h>
#include <display/core/zones.h>
#ifndef GAGGIMATE_SIM // hardware panel drivers are device-only
#include <display/drivers/AmoledDisplayDriver.h>
#include <display/drivers/LilyGoDriver.h>
#include <display/drivers/WaveshareDriver.h>
#include <display/drivers/common/LV_Helper.h>
#include <display/drivers/common/PanelClock.h>
#include <display/gm_lv_mem.h>

#include <climits>
#ifdef GM_TOUCH_PROBE
#include "esp_log.h"
#include "esp_timer.h"
// Where does a UI pass spend its time? The touch probe showed the LVGL task
// pass effectively running every ~650 ms instead of every 5, which starves
// input and widget updates alike. All on the UI task, so no locking.
namespace {
uint32_t g_uiPassN = 0;
int64_t g_uiPassSum = 0, g_uiPassMax = 0;
uint32_t g_ovlN = 0;
int64_t g_ovlSnapSum = 0, g_ovlSnapMax = 0;
int64_t g_ovlPubSum = 0, g_ovlPubMax = 0;
int64_t g_ovlAreaSum = 0, g_ovlAreaMax = 0;
// Snapshot sub-stages: alpha-clear memset vs the lv_obj_redraw itself.
int64_t g_snapClearSum = 0, g_snapDrawSum = 0;
int64_t g_uiStatLastLog = 0;
} // namespace
// Defined in eez/actions.cpp: the meter tick-draw handler's share of the
// draw= bucket, plus its clip-precheck hit rate. Outside the anonymous
// namespace, or the extern picks up internal linkage and never resolves.
extern int64_t g_meterDrawUs;
extern uint32_t g_meterDrawCalls, g_meterTicksDrawn, g_meterTicksClipped;
// Defined in the patched LVGL libdep (scripts/patch_lvgl_walkstat.py): the
// walk's event-dispatch / style-lookup / image-blit shares. gm_ws_active is
// raised around the snapshot lv_obj_redraw below so these partition exactly
// the draw= bucket. C symbols, hence the linkage block.
extern "C" {
extern bool gm_ws_active;
extern uint32_t gm_ws_ev_calls;
extern int64_t gm_ws_ev_us;
extern uint32_t gm_ws_style_calls;
extern int64_t gm_ws_style_us;
extern uint32_t gm_ws_img_calls;
extern int64_t gm_ws_img_us;
extern uint32_t gm_ws_rect_calls;
extern int64_t gm_ws_rect_us;
extern uint32_t gm_ws_rectr_calls;
extern int64_t gm_ws_rectr_us;
extern int64_t gm_ws_rect_max_us;
extern uint32_t gm_ws_label_calls;
extern int64_t gm_ws_label_us;
extern uint32_t gm_ws_line_calls;
extern int64_t gm_ws_line_us;
extern uint32_t gm_ws_arc_calls;
extern int64_t gm_ws_arc_us;
}
#endif
#endif
#include <display/main.h>
#include <display/ui/utils/effects.h>
#include <utility>

#include "esp_sntp.h"

#include <display/ui/default/bganim/BgAnim.h>
#include <display/ui/default/bganim/BgAnimCommon.h>
#include <display/ui/default/eez/actions.h>
#include <display/ui/default/eez/images.h>
#include <display/ui/default/eez/ui.h>

// Kitchen-scale glyph for the menu's Scale button (img_scale_80x80.c).
extern const lv_img_dsc_t img_scale_80x80;

static EffectManager effect_mgr;

static constexpr uint32_t STARTUP_FADE_MS = 1000; // standby fade-in duration on power-up
// How long one gradient-preview message holds the panel. The editor re-sends
// on every edit and every few seconds while open, so this only needs to
// outlast the gap between two of those.
static constexpr uint32_t BGANIM_PREVIEW_HOLD_MS = 15000;

namespace {
inline bool areaEmpty(const lv_area_t &a) { return a.x1 > a.x2 || a.y1 > a.y2; }
} // namespace

static constexpr int32_t GAUGE_TICK_LONG = 25;      // meter tick length on most screens
static constexpr int32_t GAUGE_TICK_SHORT = 10;     // shortened tick length on profile / new-menu screens
static constexpr uint32_t GAUGE_TICK_ANIM_MS = 300; // tick length transition duration

// Profile and the new menu screen show shortened meter ticks.
static bool isShortTickScreen(ScreensEnum s) {
    return s == SCREEN_ID_PROFILE_SCREEN || s == SCREEN_ID_MENU_SCREEN_NEW || s == SCREEN_ID_INFO_SCREEN;
}

// Format a millisecond duration as "m:ss" for the brew/profile time labels.
static void formatDuration(unsigned long ms, char *buf, size_t len) {
    const double seconds = ms / 1000.0;
    const int minutes = static_cast<int>(seconds / 60.0);
    const int secs = static_cast<int>(seconds) % 60;
    snprintf(buf, len, "%d:%02d", minutes, secs);
}

static float clampPercentage(float pct) { return pct < 0.0f ? 0.0f : (pct > 100.0f ? 100.0f : pct); }

// EEZ string setters allocate a fresh StringRef on the LVGL heap each call; skip unchanged text to cut churn.
static bool stringChanged(const char *current, const char *next) {
    return current == nullptr || next == nullptr || strcmp(current, next) != 0;
}

int16_t calculate_angle(int set_temp, int range, int offset) {
    const double percentage = static_cast<double>(set_temp) / static_cast<double>(MAX_TEMP);
    return (percentage * ((double)range)) - range / 2 - offset;
}

void DefaultUI::updateTempHistory() {
    if (currentTemp > 0) {
        if (tempHistoryIndex >= TEMP_HISTORY_LENGTH) {
            tempHistoryIndex = 0;
            isTempHistoryInitialized = true;
        }
        tempHistory[tempHistoryIndex] = currentTemp;
        tempHistoryIndex += 1;
    }

    if (tempHistoryIndex % 4 == 0) {
        heatingFlash = !heatingFlash;
        rerender = true;
    }
}

void DefaultUI::updateTempStableFlag() {
    if (isTempHistoryInitialized) {
        float totalError = 0.0f;
        float maxError = 0.0f;
        for (uint16_t i = 0; i < TEMP_HISTORY_LENGTH; i++) {
            float error = abs(tempHistory[i] - targetTemp);
            totalError += error;
            maxError = error > maxError ? error : maxError;
        }

        const float avgError = totalError / TEMP_HISTORY_LENGTH;
        const float errorMargin = max(2.0f, static_cast<float>(targetTemp) * 0.02f);

        isTemperatureStable = avgError < errorMargin && maxError <= errorMargin;
    }

    // instantly reset stability if setpoint has changed
    if (prevTargetTemp != targetTemp) {
        isTemperatureStable = false;
    }

    prevTargetTemp = targetTemp;
}

void DefaultUI::reloadProfiles() { profileLoaded = 0; }

#ifndef GAGGIMATE_SIM
// One-time carry-over from the single custom gradient (bgAnimCustomTheme,
// selected by bgAnimTheme == bg_theme_count()) to the library: the string
// becomes library entry 1 "Custom", and if it was the active theme every
// animation is pointed at it so nothing changes on screen. Runs only while
// the library is empty, so a user who has since built their own is left
// alone.
void DefaultUI::migrateBgAnimGradients() {
    ::Settings &settings = controller->getSettings();
    if (!settings.getBgAnimGradients().isEmpty()) {
        return;
    }
    uint8_t stops[BG_THEME_MAX_STOPS][3];
    uint8_t pos[BG_THEME_MAX_STOPS];
    bool uniform = true;
    const int n = bg_parse_gradient(settings.getBgAnimCustomTheme().c_str(), stops, pos, uniform);
    if (n == 0) {
        return;
    }
    char gradient[BG_GRADIENT_STR_MAX];
    bg_format_gradient(stops, pos, n, uniform, gradient, sizeof(gradient));
    settings.setBgAnimGradients(String("1|Custom|") + gradient);
    if (settings.getBgAnimTheme() == bg_theme_count() && settings.getBgAnimThemeMap().isEmpty()) {
        String map;
        for (int i = 0; i < bg_animation_count(); i++) {
            if (i > 0) {
                map += ';';
            }
            map += "c1";
        }
        settings.setBgAnimThemeMap(map);
        settings.setBgAnimTheme(0);
    }
    ESP_LOGI("DefaultUI", "custom gradient moved to the library (%d stops)", n);
}
#endif

DefaultUI::DefaultUI(Controller *controller, Driver *driver, PluginManager *pluginManager)
    : controller(controller), panelDriver(driver), pluginManager(pluginManager) {
    setupPanel();
    xTaskCreatePinnedToCore(loopTask, "DefaultUI::loop", configMINIMAL_STACK_SIZE * 6, this, 1, &taskHandle, 1);
}

void DefaultUI::init() {
    profileManager = controller->getProfileManager();
    g_overlayMinRefreshUs = OVERLAY_MIN_REFRESH_US;
    auto triggerRender = [this](Event const &) { rerender = true; };
    pluginManager->on("boiler:currentTemperature:change", [this](Event const &event) {
        int newTemp = static_cast<int>(event.getFloat("value"));
        if (newTemp != currentTemp) {
            currentTemp = newTemp;
            rerender = true;
        }
    });
    pluginManager->on("boiler:pressure:change", [this](Event const &event) {
        float newPressure = event.getFloat("value");
        if (round(newPressure * 10.0f) != round(pressure * 10.0f)) {
            pressure = newPressure;
            rerender = true;
        }
    });
    pluginManager->on("boiler:targetTemperature:change", [this](Event const &event) {
        int newTemp = static_cast<int>(event.getFloat("value"));
        if (newTemp != targetTemp) {
            targetTemp = newTemp;
            rerender = true;
        }
    });
    pluginManager->on("controller:targetVolume:change", [this](Event const &event) { rerender = true; });
    pluginManager->on("controller:targetDuration:change", [this](Event const &event) { rerender = true; });
    pluginManager->on("controller:grindDuration:change", [this](Event const &event) { rerender = true; });
    pluginManager->on("controller:grindVolume:change", [this](Event const &event) { rerender = true; });
    pluginManager->on("controller:process:end", triggerRender);
    pluginManager->on("controller:process:start", triggerRender);
    pluginManager->on("controller:mode:change", [this](Event const &event) {
        mode = event.getInt("value");
        switch (mode) {
        case MODE_STANDBY:
            changeScreen(SCREEN_ID_STANDBY_SCREEN);
            break;
        case MODE_BREW:
            changeScreen(SCREEN_ID_BREW_SCREEN);
            break;
        case MODE_GRIND:
            changeScreen(SCREEN_ID_GRIND_SCREEN);
            break;
        case MODE_STEAM:
            changeScreen(SCREEN_ID_STEAM_SCREEN);
            break;
        case MODE_WATER:
            changeScreen(SCREEN_ID_WATER_SCREEN);
            break;
        default:
            break;
        };
    });
    pluginManager->on("controller:brew:start", [this](Event const &event) { changeScreen(SCREEN_ID_STATUS_SCREEN); });
    pluginManager->on("controller:brew:clear", [this](Event const &event) {
        if (eez_flow_get_current_screen() == SCREEN_ID_STATUS_SCREEN) {
            changeScreen(SCREEN_ID_BREW_SCREEN);
        }
    });
    pluginManager->on("controller:bluetooth:waiting", [this](Event const &) {
        waitingForController = true;
        rerender = true;
    });
    pluginManager->on("controller:bluetooth:connect", [this](Event const &) {
        waitingForController = false;
        rerender = true;
        initialized = true;
        // Stay on the standby screen when the controller is incompatible so the
        // mismatch message remains visible instead of jumping into brew.
        if (eez_flow_get_current_screen() == SCREEN_ID_STANDBY_SCREEN && !controller->getSystemInfo().protocolMismatch) {
            ::Settings &settings = controller->getSettings();
            if (settings.getStartupMode() == MODE_BREW) {
                changeScreen(SCREEN_ID_BREW_SCREEN);
            } else {
                standbyEnterTime = ::millis();
            }
        }
        pressureAvailable = controller->getSystemInfo().capabilities.pressure;
    });
    pluginManager->on("controller:bluetooth:disconnect", [this](Event const &) {
        waitingForController = true;
        rerender = true;
    });
    pluginManager->on("controller:wifi:connect", [this](Event const &event) {
        rerender = true;
        apActive = event.getInt("AP");
    });
    pluginManager->on("ota:update:start", [this](Event const &event) {
        rerender = true;
        changeScreen(SCREEN_ID_STANDBY_SCREEN);
        // A display update flashes ~4 MB while the RGB peripheral streams the
        // framebuffer from PSRAM; both contend on the S3's shared memory bus
        // and the download starves and aborts. The device reboots right after
        // a display OTA anyway, so stop scan-out for the duration. Handled on
        // the UI task (via flag) so it can't race an in-flight flush.
        if (event.getString("component") != "controller") {
            panelStopRequested = true;
        }
    });
    pluginManager->on("ota:update:end", [this](Event const &) {
        rerender = true;
        changeScreen(SCREEN_ID_STANDBY_SCREEN);
        otaEnded = true;
    });
    pluginManager->on("ota:update:status", [this](Event const &event) {
        rerender = true;
        updateAvailable = event.getInt("value");
    });
    pluginManager->on("bganim:preview", [this](Event const &event) {
        std::lock_guard<std::mutex> guard(previewMutex);
        previewAnim = event.getInt("anim");
        previewStops = event.getString("stops");
        previewDirty = true;
        previewUntil = ::millis() + BGANIM_PREVIEW_HOLD_MS;
    });
    pluginManager->on("bganim:preview-end", [this](Event const &) {
        std::lock_guard<std::mutex> guard(previewMutex);
        previewUntil = 0;
    });
#ifndef GAGGIMATE_SIM
    migrateBgAnimGradients();
#endif
    pluginManager->on("controller:error", [this](Event const &) {
        rerender = true;
        changeScreen(SCREEN_ID_STANDBY_SCREEN);
    });
    pluginManager->on("controller:protocol:mismatch", [this](Event const &) {
        // Incompatible firmware on the other end: control is inhibited (OTA only),
        // so surface it on the standby screen like a runaway error.
        rerender = true;
        changeScreen(SCREEN_ID_STANDBY_SCREEN);
    });
    pluginManager->on("controller:autotune:start", [this](Event const &) { changeScreen(SCREEN_ID_STANDBY_SCREEN); });
    pluginManager->on("controller:autotune:result", [this](Event const &) { changeScreen(SCREEN_ID_STANDBY_SCREEN); });

    pluginManager->on("profiles:profile:select", [this](Event const &event) {
        reloadProfiles();
        rerender = true;
    });
    pluginManager->on("profiles:profile:favorite", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("profiles:profile:unfavorite", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("profiles:profile:save", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("controller:volumetric-measurement:active:change", [this](Event const &event) {
        double newWeight = event.getFloat("value");
        if (round(newWeight * 10.0) != round(activeWeight * 10.0)) {
            activeWeight = newWeight;
            rerender = true;
        }
    });
    // Scale screen feed. The hardware cells report through this event on every
    // valid measurement regardless of controller mode, so the readout stays live
    // even while the overlay forces MODE_GRIND (which routes active:change to the
    // Bluetooth-only grind source and would otherwise freeze it).
    pluginManager->on("controller:volumetric-measurement:hardware:change",
                      [this](Event const &event) { scaleHardwareWeight = event.getFloat("value"); });
    xTaskCreatePinnedToCore(profileLoopTask, "DefaultUI::loopProfiles", configMINIMAL_STACK_SIZE * 4, this, 1, &profileTaskHandle,
                            0);
}

void DefaultUI::loop() {
#ifndef GAGGIMATE_SIM
    // Here as well as in pumpSleepOverlay: a UI pass on a busy screen runs
    // longer than UI_PERIOD_MS, so the task loop takes this branch every
    // time and the pump branch never.
    serviceUiAnimTest();
    if (panelStopRequested && !panelStopped) {
        panelStopped = true;
        stopSleepAnimation();
        if (panelDriver != nullptr) {
            panelDriver->stopPanel();
        }
    }
    if (panelStopped && otaEnded) {
        // Success never reaches here (GitHubOTA restarts the device); a failed
        // display OTA must reboot to bring the panel back.
        delay(250);
        ESP.restart();
    }
#endif

    const unsigned long now = ::millis();
    const unsigned long diff = now - lastRender;

    if (now - lastTempLog > TEMP_HISTORY_INTERVAL) {
        updateTempHistory();
        lastTempLog = now;
    }

    if ((controller->isActive() && diff > RERENDER_INTERVAL_ACTIVE) || diff > RERENDER_INTERVAL_IDLE) {
        rerender = true;
    }

    // Telemetry-pass spacing (see RERENDER_MIN_INTERVAL): rerender stays
    // pending while held, so no change is ever dropped, only coalesced into
    // the next spaced pass. Within GM_TOUCH_GRACE_US of a touch edge the
    // spacer is bypassed so interaction effects — including CLICK flags that
    // only get applied on the pass AFTER the edge — run immediately.
    // ui_tick() and the maintain calls below still run on a held pass.
    const bool spacerHold =
        rerender && diff < RERENDER_MIN_INTERVAL && esp_timer_get_time() - g_touchEdgeAtUs >= GM_TOUCH_GRACE_US;

    if (rerender && !spacerHold) {
        rerender = false;
        lastRender = now;
        lastRenderUs = esp_timer_get_time();
        applyTheme();
        if (controller->isErrorState()) {
            changeScreen(SCREEN_ID_STANDBY_SCREEN);
        }
        updateTempStableFlag();

        updateState();
        // Fill the EEZ data models before handleScreenChange() creates/ticks a screen (undefined fields abort the flow).
        updateSystemStatus();
        updateProfileInfo();
        updateBoiler();
        updateBrewProcess();
        currentWeight = FloatValue(activeWeight);
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SCALE_WEIGHT_CURRENT, currentWeight);

        char timeBuf[12];
        formatDuration(controller->getSettings().getTargetGrindDuration(), timeBuf, sizeof(timeBuf));
        if (stringChanged(grindTimeTarget.getString(), timeBuf)) {
            grindTimeTarget = StringValue(timeBuf);
            eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_GRIND_TIME_TARGET, grindTimeTarget);
        }
        grindWeightTarget = FloatValue(controller->getSettings().getTargetGrindVolume());
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_GRIND_WEIGHT_TARGET, grindWeightTarget);

        handleScreenChange();
        currentScreen = static_cast<ScreensEnum>(eez_flow_get_current_screen());
        effect_mgr.evaluate_all();

        if (currentScreen == SCREEN_ID_STANDBY_SCREEN) {
            if (standbyEnterTime > 0) {
                const Settings &settings = controller->getSettings();
                const unsigned long now = millis();
                if (now - standbyEnterTime >= settings.getStandbyBrightnessTimeout()) {
                    setBrightness(settings.getStandbyBrightness());
                }
            }
        }
    }

    // ui_tick() first: it runs the generated tick_screen_*, which is what derives
    // widget visibility from flow state. Running it after the maintain calls
    // meant that on the pass where a screen had just been created, the overlay
    // snapshot captured HIDDEN flags still at their creation defaults, and the
    // real values only reached the panel on the following refresh — a widget
    // visibly settling a frame late, but only the first time a screen was
    // entered, since after that the flags were already correct.
    ui_tick();
    // After ui_tick so a screen eez_flow_set_screen created this pass exists
    // before the walk runs.
    applyPressedFeedback();
    // Scale overlay before the animation maintenance: maintainSleepAnimation
    // snapshots the LVGL tree into the panel overlay, so the overlay's
    // show/hide state must be final by then, or the pass that enters the
    // grind screen publishes it bare - one refresh of naked grind widgets -
    // before the scale cover is up.
    maintainScaleScreen();
    maintainSleepAnimation();

    lv_task_handler();
}

// Runs every UI-task pass. Starts/stops the background animation and keeps
// the composited widget snapshot fresh. Two operating modes:
//  - default: animation only during genuine sleep — standby screen, standby
//    mode, controller BLE-connected, and no status overlay (update/error/
//    autotune/protocol mismatch, which render on the plain standby screen).
//  - all-screens (settings.bgAnimAllScreens): animation behind every screen
//    whenever the UI is up and nothing critical is running. OTA/error states
//    still stop it (the OTA download needs every byte of PSRAM bandwidth).
void DefaultUI::maintainSleepAnimation() {
#ifndef GAGGIMATE_SIM
    const bool blocked = controller->isUpdating() || controller->isErrorState() || controller->isAutotuning() ||
                         controller->getSystemInfo().protocolMismatch;
    const bool connected = controller->isLinkUp();
    // getMode() is controller-sourced, so while the link is down its value
    // carries no information -- only consult it once there is a link that could
    // have supplied it. Requiring MODE_STANDBY unconditionally is what left the
    // standby screen blank behind "Waiting for controller...".
    const bool standbyMode = !connected || controller->getMode() == MODE_STANDBY;
    // Not `initialized`, which despite the name only becomes true once a
    // controller has connected -- testing it here is what kept the standby
    // screen blank behind "Waiting for controller...". What the animation
    // actually needs is a built UI and a finished power-up fade, since it
    // suppresses the LVGL flushes the fade is made of.
    const bool uiReady = uiBuiltAt != 0 && ::millis() - uiBuiltAt >= STARTUP_FADE_MS;
    const bool sleepWant = uiReady && currentScreen == SCREEN_ID_STANDBY_SCREEN && standbyMode && !blocked;
    const bool wantAnimation = bgAnimAllScreens ? (uiReady && !blocked) : sleepWant;

#ifdef GM_ANIM_BENCH
    {
        BenchGateState &g = const_cast<BenchGateState &>(bench_gate_state());
        g.uiInitialized = initialized;
        g.blocked = blocked;
        g.wantAnimation = wantAnimation;
        g.animActive = sleepAnimation.isActive();
        g.mode = controller->getMode();
        g.screen = static_cast<int>(currentScreen);
    }
#endif

    if (wantAnimation) {
        if (!sleepAnimation.isActive()) {
            const unsigned long now = ::millis();
            // lastSleepAnimAttempt is only armed after a FAILED start, so a
            // stop/start across a screen change restarts on the next pass.
            if (now - lastSleepAnimAttempt > 2000) {
                startSleepAnimation();
                if (!sleepAnimation.isActive()) {
                    lastSleepAnimAttempt = now;
#ifdef GM_ANIM_BENCH
                    BenchGateState &g = const_cast<BenchGateState &>(bench_gate_state());
                    g.startFailed = true;
                    g.lastStartAttempt = now;
#endif
                }
            }
        } else {
            // A screen change released the old host without stopping the
            // animation; give it the new one. Idempotent on every other pass.
            adoptAnimHost(lv_scr_act());
            // Standby content changes once a minute (clock); active screens
            // update continuously — refresh the snapshot faster there so
            // gauges and numbers stay reasonably live behind the animation.
            const Settings &plateSettings = controller->getSettings();
            applyAnimPlates(plateSettings.getBgAnimClearPlates(), static_cast<uint32_t>(plateSettings.getBgAnimPlateColor()),
                            plateSettings.getBgAnimPlateOpacity());
            // No refresh throttle. refreshSleepOverlay's own early-out makes a
            // pass with no fresh LVGL output cost one comparison, so the only
            // thing an interval here rate-limited was the response to actual
            // changes -- and the standby screen's old 1000 ms interval held a
            // tap's visual feedback for up to a second, which is most of what
            // "the UI feels slow under the animation" was. The snapshot rate
            // stays bounded by the rate at which LVGL actually redraws.
            refreshSleepOverlay();
        }
    } else if (sleepAnimation.isActive()) {
        stopSleepAnimation();
    }
#endif
}

void DefaultUI::pumpSleepOverlay() {
#ifndef GAGGIMATE_SIM
    serviceUiAnimTest();
    if (sleepAnimation.isActive()) {
        refreshSleepOverlay();
    }
#endif
}

static void uiAnimTestSlideCb(void *var, int32_t v) { lv_obj_set_x(static_cast<lv_obj_t *>(var), static_cast<lv_coord_t>(v)); }

void DefaultUI::serviceUiAnimTest() {
    const int req = g_uiAnimTestReq;
    if (req == uiAnimTestMode) {
        // Mode 3 ping-pongs the layer: when one leg lands, start the other.
        if (req == 3 && uiAnimTestLayer >= 0 && !sleepAnimation.layerAnimating(uiAnimTestLayer)) {
            uiAnimTestFwd = !uiAnimTestFwd;
            sleepAnimation.layerAnimate(uiAnimTestLayer, uiAnimTestFwd ? uiAnimTestX1 : uiAnimTestX0, uiAnimTestY, 1200,
                                        SleepAnimation::LayerEase::EaseInOut);
        }
        return;
    }
    if (uiAnimTestObj != nullptr) {
        lv_anim_del(uiAnimTestObj, nullptr);
        lv_obj_del(uiAnimTestObj);
        uiAnimTestObj = nullptr;
    }
    if (uiAnimTestLayer >= 0) {
        sleepAnimation.layerRelease(uiAnimTestLayer);
        uiAnimTestLayer = -1;
    }
    uiAnimTestMode = req;
    if (req == 0) {
        return;
    }
    lv_obj_t *scr = lv_scr_act();
    if (scr == nullptr) {
        return;
    }
    const lv_coord_t size = req == 2 ? 60 : 120;
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_set_pos(o, 40, (lv_obj_get_height(scr) - size) / 2);
    lv_obj_set_style_radius(o, size / 6, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0xF4A261), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = lv_label_create(o);
    lv_label_set_text(l, req == 2 ? "1" : "9.2");
    lv_obj_set_style_text_color(l, lv_color_hex(0x1B1B1B), 0);
    lv_obj_center(l);
    uiAnimTestObj = o;
    if (req == 3 || req == 4) {
        // The layer path: render the plate once into a layer, take it out of
        // LVGL's picture, and let the render task move the layer. Same
        // travel and duration as mode 1, so the two are directly comparable
        // on the probe (ui_fpsprobe.py: mode 1 shows up as overlay
        // refreshes, mode 3 as animation frames with layer_us). Mode 4 parks
        // the layer at the far end instead of moving it, for a framebuffer
        // grab that a moving sprite would tear.
        lv_obj_update_layout(scr);
        const lv_coord_t ext = _lv_obj_get_ext_draw_size(o);
        const int lw = size + ext * 2, lh = size + ext * 2;
        const int id = sleepAnimation.layerAcquire(lw, lh);
        lv_area_t area;
        if (id >= 0 && snapshotObjectToBuffer(o, sleepAnimation.layerBuffer(id), static_cast<uint32_t>(lw) * lh * 3, &area)) {
            sleepAnimation.layerPublish(id, area.x1, area.y1);
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
            uiAnimTestLayer = id;
            uiAnimTestX0 = area.x1;
            uiAnimTestX1 = area.x1 + (lv_obj_get_width(scr) - size - 80);
            uiAnimTestY = area.y1;
            uiAnimTestFwd = true;
            if (req == 4) {
                sleepAnimation.layerSetPos(id, uiAnimTestX1, uiAnimTestY);
            } else {
                sleepAnimation.layerAnimate(id, uiAnimTestX1, uiAnimTestY, 1200, SleepAnimation::LayerEase::EaseInOut);
            }
        } else {
            log_w("uianim: layer snapshot failed (id %d)", id);
            if (id >= 0) {
                sleepAnimation.layerRelease(id);
            }
        }
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, uiAnimTestSlideCb);
    lv_anim_set_values(&a, 40, lv_obj_get_width(scr) - size - 40);
    lv_anim_set_time(&a, 1200);
    lv_anim_set_playback_time(&a, 1200);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

bool DefaultUI::snapshotObjectToBuffer(lv_obj_t *obj, uint8_t *buf, uint32_t bufSize, lv_area_t *outArea) {
#ifndef GAGGIMATE_SIM
    if (obj == nullptr || buf == nullptr) {
        return false;
    }
    const lv_coord_t ext = _lv_obj_get_ext_draw_size(obj);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    lv_area_increase(&area, ext, ext);
    const int w = lv_area_get_width(&area);
    const int h = lv_area_get_height(&area);
    if (w <= 0 || h <= 0 || static_cast<uint32_t>(w) * h * 3 > bufSize) {
        return false;
    }
    memset(buf, 0, static_cast<size_t>(w) * h * 3);

    lv_disp_t *objDisp = lv_obj_get_disp(obj);
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = lv_disp_get_hor_res(objDisp);
    driver.ver_res = lv_disp_get_ver_res(objDisp);
    lv_disp_drv_use_generic_set_px_cb(&driver, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_disp_t fakeDisp;
    lv_memset_00(&fakeDisp, sizeof(lv_disp_t));
    fakeDisp.driver = &driver;
    lv_draw_ctx_t *drawCtx = static_cast<lv_draw_ctx_t *>(lv_mem_alloc(objDisp->driver->draw_ctx_size));
    if (drawCtx == nullptr) {
        return false;
    }
    objDisp->driver->draw_ctx_init(fakeDisp.driver, drawCtx);
    fakeDisp.driver->draw_ctx = drawCtx;
    drawCtx->clip_area = &area;
    drawCtx->buf_area = &area;
    drawCtx->buf = static_cast<void *>(buf);
    driver.draw_ctx = drawCtx;
    lv_disp_t *refrOri = _lv_refr_get_disp_refreshing();
    _lv_refr_set_disp_refreshing(&fakeDisp);
    lv_obj_redraw(drawCtx, obj);
    _lv_refr_set_disp_refreshing(refrOri);
    objDisp->driver->draw_ctx_deinit(fakeDisp.driver, drawCtx);
    lv_mem_free(drawCtx);
    if (outArea != nullptr) {
        *outArea = area;
    }
    return true;
#else
    return false;
#endif
}

void DefaultUI::loopProfiles() {
    if (!profileLoaded) {
        // Build into locals and swap under the lock — the UI task reads these concurrently (GM-147).
        const auto favoritedIds = profileManager->getFavoritedProfiles();
        std::vector<String> ids;
        ids.reserve(favoritedIds.size() + 1);
        ids.emplace_back(controller->getSettings().getSelectedProfile());
        for (const auto &id : favoritedIds) {
            if (std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.emplace_back(id);
        }
        std::vector<Profile> profiles;
        profiles.reserve(ids.size());
        for (const auto &profileId : ids) {
            Profile profile{};
            profileManager->loadProfile(profileId, profile);
            profiles.emplace_back(std::move(profile));
        }
        {
            std::lock_guard<std::mutex> guard(profilesMutex);
            favoritedProfileIds = std::move(ids);
            favoritedProfiles = std::move(profiles);
        }
        profileLoaded = 1;
    }
}

void DefaultUI::changeScreen(ScreensEnum screen) {
    targetScreen = screen;
    brewScreenState = BrewScreenState::Brew;
    rerender = true;
    // Reset some submenus
}

void DefaultUI::changeBrewScreenMode(BrewScreenState state) {
    brewScreenState = state;
    rerender = true;
}

void DefaultUI::onProfileSwitch() {
    currentProfileIdx = 0;
    changeScreen(SCREEN_ID_PROFILE_SCREEN);
}

void DefaultUI::onNextProfile() {
    std::lock_guard<std::mutex> guard(profilesMutex);
    if (currentProfileIdx + 1 < static_cast<int>(favoritedProfileIds.size())) {
        currentProfileIdx++;
    }
    rerender = true;
}

void DefaultUI::onPreviousProfile() {
    if (currentProfileIdx > 0) {
        currentProfileIdx--;
    }
    rerender = true;
}

void DefaultUI::onProfileSelect() {
    String id;
    {
        std::lock_guard<std::mutex> guard(profilesMutex);
        if (currentProfileIdx >= 0 && currentProfileIdx < static_cast<int>(favoritedProfileIds.size())) {
            id = favoritedProfileIds[currentProfileIdx];
        }
    }
    if (!id.isEmpty()) {
        profileManager->selectProfile(id);
    }
    profileDirty = false;
    changeScreen(SCREEN_ID_BREW_SCREEN);
}

void DefaultUI::onVolumetricDelete() {
    controller->onVolumetricDelete();
    profileDirty = true;
}

void DefaultUI::setupPanel() {
    ui_init();
    setupState();
    applyTheme();
    ui_tick();

    // Polished power-up: ui_init() makes standby active instantly, so stage a black screen and
    // fade standby in over it (lv_scr_load_anim no-ops when the target is already the active screen).
    lv_obj_t *standby = lv_scr_act();
    lv_obj_t *black = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(black, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(black, LV_OPA_COVER, LV_PART_MAIN);
    lv_scr_load(black);
    lv_scr_load_anim(standby, LV_SCR_LOAD_ANIM_FADE_ON, STARTUP_FADE_MS, 0, true);

    lv_task_handler();

    delay(100);
    // Set initial brightness based on settings
    const ::Settings &settings = controller->getSettings();
    setBrightness(settings.getMainBrightness());
    uiBuiltAt = ::millis();
}

void DefaultUI::setupState() {
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SCALE_WEIGHT_CURRENT, currentWeight);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_GRIND_WEIGHT_TARGET, grindWeightTarget);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_GRIND_TIME_TARGET, grindTimeTarget);

    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SYSTEM, systemStatus);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PREVIEW_PROFILE, previewProfileInfo);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SELECTED_PROFILE, selectedProfileInfo);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BOILER, boiler);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_UI_FLAGS, uiFlags);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BREW_PROCESS_INFO, brewProcess);

    updateState();
    updateSystemStatus();
    updateProfileInfo();
    updateBoiler();
    updateBrewProcess();

    effect_mgr.use_effect([this]() { return currentScreen == SCREEN_ID_INFO_SCREEN; },
                          [this]() {
                              String content = "";
                              if (apActive) {
                                  // WIFI: QR syntax — escape \ ; , : " in the password per the spec.
                                  const String pw = controller->getSettings().getWifiApPassword();
                                  String escaped;
                                  escaped.reserve(pw.length() + 4);
                                  for (size_t i = 0; i < pw.length(); i++) {
                                      const char c = pw.charAt(i);
                                      if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
                                          escaped += '\\';
                                      }
                                      escaped += c;
                                  }
                                  if (escaped.isEmpty()) {
                                      content = "WIFI:S:GaggiMate;;;;";
                                  } else {
                                      content = "WIFI:S:GaggiMate;T:WPA;P:" + escaped + ";;";
                                  }
                              } else if (wifiConnected) {
                                  content = "http://" + WiFi.localIP().toString() + "/";
                              }
                              if (content == "") {
                                  return;
                              }
                              const char *data = content.c_str();
                              lv_qrcode_update(objects.qrcode, data, strlen(data));
                          },
                          &wifiConnected, &apActive);
    effect_mgr.use_effect([this]() { return currentScreen == SCREEN_ID_MENU_SCREEN_NEW; },
                          [this]() {
                              const bool fourthButton = grindAvailable || scaleMenuSwap;
                              int radius = 135;
                              int count = fourthButton ? 4 : 3;
                              int step = 360 / (fourthButton ? 4 : 3);
                              int iconOffset = fourthButton ? 1 : 0;
                              int rotationOffset = count == 4 ? 45 : 0;
                              positionMenuIcon(objects.btn_brew_1, step * 0 - rotationOffset, radius);
                              positionMenuIcon(objects.btn_steam_1, step * 1 - rotationOffset, radius);
                              positionMenuIcon(objects.btn_water_1, step * 2 - rotationOffset, radius);
                              positionMenuIcon(objects.btn_grind_1, step * 3 - rotationOffset, radius);
                              // positionMenuIcon(objects.btn_settings_1, step * (3 + iconOffset) - rotationOffset, radius);
                              // Grind slot doubles as the Scale button.
                              if (objects.btn_grind_1 != nullptr) {
                                  lv_obj_set_style_bg_img_src(objects.btn_grind_1,
                                                              scaleMenuSwap ? &img_scale_80x80 : &img_coffee_bean_80x80,
                                                              LV_PART_MAIN | LV_STATE_DEFAULT);
                              }
                          },
                          &grindAvailable, &scaleMenuSwap);
}

void DefaultUI::handleScreenChange() {
    if (currentScreen != targetScreen) {
        if (targetScreen == SCREEN_ID_STANDBY_SCREEN) {
            standbyEnterTime = ::millis();
        } else if (currentScreen == SCREEN_ID_STANDBY_SCREEN) {
            const ::Settings &settings = controller->getSettings();
            setBrightness(settings.getMainBrightness());
        }
        // Any screen change while animating has to release the old host screen,
        // or its transparent-bg override leaks onto a screen that is no longer
        // being drawn over plasma.
        //
        // Releasing is all it takes though. This used to stop the animation
        // outright and let the next maintain pass start it again, which meant
        // every navigation tore the background down and built it back up: the
        // render task exited, the framebuffers changed hands twice, and the
        // plasma visibly restarted. In all-screens mode that is the common
        // case, not the rare one, and it read as jank. Nothing about the
        // animation is per-screen except this one style property; the plate
        // table and the status icons it also rewrites are fixed global objects
        // that span every screen. So when the animation is going to keep
        // running anyway, hand it the new host instead of restarting it.
        // maintainSleepAnimation does the adopting, which runs later in this
        // same pass, after ui_tick has actually swapped the screen.
        if (bgAnimAllScreens && sleepAnimation.isActive()) {
            releaseAnimHost();
        } else {
            stopSleepAnimation();
        }
        eez_flow_set_screen(targetScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0);
        animateGaugeTicks(currentScreen, targetScreen);
        // The flow engine may delete and later recreate the screen this
        // leaves; a recreated screen can land on the old lv_obj address, so
        // an address compare alone would skip restyling it.
        pressedStyledRoot = nullptr;
        rerender = true;
    }
}

// See the declaration for why this exists and why it darkens colors instead
// of changing background opacity. Local per-object props (not one shared
// style) because the pressed look derives from each widget's OWN rest
// colors — a flat black-60 recolor read as "almost too much" dimming on
// theme-tinted icons and lost their hue entirely. Setting a local prop
// twice just overwrites it, so the walk is idempotent, and the rest colors
// are re-read on every walk, which is what makes the theme-change rewalk
// (applyTheme clears the root) pick up new hues.
static void applyPressedRecurse(lv_obj_t *obj, lv_color_t dim) {
    const bool isImgBtn = lv_obj_check_type(obj, &lv_imgbtn_class);
    if (isImgBtn || (lv_obj_check_type(obj, &lv_img_class) && lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE))) {
        // Resolved for the unpressed state this walk runs in. Icons the theme
        // fully recolors (opa 255, most of them) shift 40% toward the
        // configured dim color from their own rest color; raw bitmaps get
        // the dim color at 40% directly.
        const lv_color_t rest = lv_obj_get_style_img_recolor(obj, LV_PART_MAIN);
        const lv_opa_t restOpa = lv_obj_get_style_img_recolor_opa(obj, LV_PART_MAIN);
        lv_color_t pressed = dim;
        lv_opa_t pressedOpa = LV_OPA_40;
        if (restOpa > LV_OPA_50) {
            pressed = lv_color_mix(dim, rest, LV_OPA_40);
            pressedOpa = restOpa;
        }
        lv_obj_set_style_img_recolor(obj, pressed, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_img_recolor_opa(obj, pressedOpa, LV_PART_MAIN | LV_STATE_PRESSED);
    } else if (lv_obj_check_type(obj, &lv_btn_class)) {
        // The generated buttons set their bg locally in the DEFAULT state,
        // which outranks the theme's pressed styles — so give them a local
        // pressed bg too. Buttons without a visible bg are left alone rather
        // than given one: adding opacity on press would change the snapshot's
        // alpha coverage and re-trigger the scrim rebuild.
        if (lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) >= LV_OPA_20) {
            const lv_color_t bg = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
            lv_obj_set_style_bg_color(obj, lv_color_mix(dim, bg, LV_OPA_40), LV_PART_MAIN | LV_STATE_PRESSED);
        }
    }
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        applyPressedRecurse(lv_obj_get_child(obj, i), dim);
    }
}

void DefaultUI::applyPressedFeedback() {
    lv_obj_t *scr = lv_scr_act();
    const int dim = controller->getSettings().getTouchDimColor();
    if (scr == nullptr || (scr == pressedStyledRoot && dim == appliedDimColor)) {
        return;
    }
    appliedDimColor = dim;
    applyPressedRecurse(scr, lv_color_hex(static_cast<uint32_t>(dim)));
    pressedStyledRoot = scr;
}

void DefaultUI::startSleepAnimation() {
#ifndef GAGGIMATE_SIM
    Display *display = panelDriver != nullptr ? panelDriver->getDisplay() : nullptr;
    lv_obj_t *host = lv_scr_act();
    if (display == nullptr || host == nullptr) {
        return;
    }
    // Before start(), not after: LVGL renders into the panel's framebuffers, and
    // start() spawns a task that begins writing them. Suppressing first is what
    // moves LVGL onto its scratch buffer, so the two never hold the same memory
    // at once. Put back if the animation declines to run.
    lvgl_helper_suppress_flush(true);
    sleepAnimation.start(display);
    if (!sleepAnimation.isActive()) {
        lvgl_helper_suppress_flush(false);
        return;
    }
    // The status icons carry a 10 px border in the theme background color (an
    // EEZ spacing trick, invisible on black) — over the plasma it snapshots as
    // an opaque plate around each icon. Hide the borders while animating.
    for (lv_obj_t *icon : {objects.wifi_icon, objects.bluetooth_icon, objects.update_icon}) {
        if (icon != nullptr) {
            lv_obj_set_style_border_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
        }
    }
    const Settings &plateSettings = controller->getSettings();
    applyAnimPlates(plateSettings.getBgAnimClearPlates(), static_cast<uint32_t>(plateSettings.getBgAnimPlateColor()),
                    plateSettings.getBgAnimPlateOpacity());
    // Last, because it ends by snapshotting the widgets, and the two rewrites
    // above are part of what that snapshot has to capture.
    adoptAnimHost(host);
#endif
}

// The generated screens put a full-bleed opaque object behind their content:
// brew, status and profile each a 360x360 circle (radius 180), info a 400x400
// rounded square. Menu, steam, water and grind have none. startSleepAnimation
// makes the SCREEN transparent, but not these children, so the animation shows
// whole on the screens without a plate and with a black disc punched through it
// on the ones with. Hiding them is what makes every screen look alike.
//
// The original opacity is saved and put back rather than dropping the local
// style property, because the generated code sets that property explicitly and
// removing it would fall through to the theme default instead of the value the
// screen was designed with. Colours are restored differently — see the note on
// change_color_theme at the end of the function.
void DefaultUI::applyAnimPlates(int mode, uint32_t color, int opaPct) {
#ifndef GAGGIMATE_SIM
    if (opaPct < 0) {
        opaPct = 0;
    } else if (opaPct > 100) {
        opaPct = 100;
    }
    // Colour and opacity only mean anything in mode 2; ignoring them otherwise
    // keeps mode 0/1 from repainting every time the (unused) colour changes.
    const uint32_t wantColor = mode == 2 ? color : 0;
    const int wantOpa = mode == 2 ? opaPct : 0;
    if (mode == animPlateMode && wantColor == animPlateColor && wantOpa == animPlateOpaPct) {
        return;
    }

    // obj2/obj9/obj15/obj26 are the full-bleed panels behind the dials.
    // profile_name/profile_name_1 are the profile-name labels on the brew and
    // profile screens: not panels, but opaque, and because the text scrolls
    // (LONG_SCROLL_CIRCULAR) the bar behind it is in constant motion against
    // the animation, which makes it the most obvious of the lot.
    // mode_switch/mode_switch1 are the pills carrying the scale weight readout
    // on the brew and grind screens. They are buttons as well as backdrops, so
    // mode 2 is the useful setting for them: it keeps a visible affordance
    // instead of dissolving the control into the animation.
    // The last entry is the scale overlay's Tare pill, built at runtime by
    // buildScaleScreen rather than generated, so it is null whenever that screen
    // is down. It is the one plate change_color_theme() knows nothing about.
    lv_obj_t *const plates[ANIM_PLATE_COUNT] = {objects.obj2,        objects.obj9,         objects.obj15,
                                                objects.obj26,       objects.profile_name, objects.profile_name_1,
                                                objects.mode_switch, objects.mode_switch1, scaleTareBtn};

    for (int i = 0; i < ANIM_PLATE_COUNT; i++) {
        if (plates[i] == nullptr) {
            continue;
        }
        if (!animPlateHas[i]) {
            animPlateOpa[i] = lv_obj_get_style_bg_opa(plates[i], LV_PART_MAIN);
            animPlateBg[i] = lv_obj_get_style_bg_color(plates[i], LV_PART_MAIN);
            animPlateHas[i] = true;
        }
        switch (mode) {
        case 0:
            lv_obj_set_style_bg_opa(plates[i], animPlateOpa[i], LV_PART_MAIN);
            lv_obj_set_style_bg_color(plates[i], animPlateBg[i], LV_PART_MAIN);
            // Mode 2 adds a checked-state colour where most of these objects
            // never had one; drop it so the state falls back to the default
            // again. The ones that legitimately do have one (mode_switch1)
            // get it reinstated by change_color_theme below.
            lv_obj_remove_local_style_prop(plates[i], LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_CHECKED);
            break;
        case 2:
            lv_obj_set_style_bg_color(plates[i], lv_color_hex(color), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(plates[i], static_cast<lv_opa_t>((opaPct * 255 + 50) / 100), LV_PART_MAIN);
            // mode_switch1 carries a checked-state bg_color of its own, applied
            // whenever grind_volumetric is set, and a default-state colour does
            // not win against it. Without this the grind screen's weight pill
            // ignores the chosen colour (at the chosen opacity) exactly half the
            // time. Writing both states is also what makes the custom colour
            // hold on any plate that gains a checked style later.
            lv_obj_set_style_bg_color(plates[i], lv_color_hex(color), LV_PART_MAIN | LV_STATE_CHECKED);
            break;
        default:
            lv_obj_set_style_bg_opa(plates[i], LV_OPA_TRANSP, LV_PART_MAIN);
            break;
        }
    }

    // Restoring from a capture is not enough for the generated plates. The
    // capture reads whichever state the object was in at the time (LVGL resolves
    // a style query against the live state, it takes no state argument), and
    // change_color_theme() may have overwritten the colour since. Re-running the
    // theme is the authoritative restore: it reassigns every generated object's
    // colours in both states from theme_colors. The per-plate capture above
    // still carries the Tare pill, which the theme function does not reach.
    //
    // That leaves the Tare pill relying on its capture having been taken in the
    // default state, and it is: buildScaleScreen creates it with lv_btn_create
    // and never sets LV_OBJ_FLAG_CHECKABLE, so it has no checked state to be in,
    // and LVGL's default theme renders PRESSED through a colour filter rather
    // than a bg_color override, so a capture taken mid-press still reads the
    // default colour. If the pill ever becomes checkable, or gains a state-
    // specific bg_color, capture it explicitly instead of querying the live
    // state -- the query resolves against whatever state the object is in.
    if (mode == 0 && currentThemeMode >= 0) {
        change_color_theme(static_cast<uint32_t>(currentThemeMode));
    }

    animPlateMode = mode;
    animPlateColor = wantColor;
    animPlateOpaPct = wantOpa;
#endif
}

void DefaultUI::releaseAnimHost() {
#ifndef GAGGIMATE_SIM
    if (animHostScreen != nullptr) {
        // Drop the transparent-background override, back to the EEZ style.
        lv_obj_remove_local_style_prop(animHostScreen, LV_STYLE_BG_OPA, LV_PART_MAIN);
        animHostScreen = nullptr;
    }
#endif
}

void DefaultUI::adoptAnimHost(lv_obj_t *host) {
#ifndef GAGGIMATE_SIM
    if (host == nullptr || host == animHostScreen) {
        return;
    }
    releaseAnimHost();
    animHostScreen = host;
    // The render task owns the panel while the animation runs; LVGL keeps the
    // host screen active only for input and for the offscreen widget
    // snapshots. Making the screen background transparent keeps those
    // snapshots per-pixel alpha (widgets only, no opaque color plate).
    lv_obj_set_style_bg_opa(animHostScreen, LV_OPA_TRANSP, LV_PART_MAIN);
    // Both overlay buffers describe the screen that just went away.
    overlayValid[0] = overlayValid[1] = false;
    overlayDirtyN[0] = overlayDirtyN[1] = 0;
    refreshSleepOverlay();
#endif
}

void DefaultUI::stopSleepAnimation() {
#ifndef GAGGIMATE_SIM
    sleepAnimation.stop();
    lvgl_helper_suppress_flush(false);
    applyAnimPlates(0);
    for (lv_obj_t *icon : {objects.wifi_icon, objects.bluetooth_icon, objects.update_icon}) {
        if (icon != nullptr) {
            lv_obj_remove_local_style_prop(icon, LV_STYLE_BORDER_OPA, LV_PART_MAIN);
        }
    }
    lv_obj_t *const host = animHostScreen;
    releaseAnimHost();
    if (host != nullptr) {
        // Repaint the whole screen over the last animation frame.
        lv_obj_invalidate(host);
    }
#endif
}

// Renders the host screen's widgets into the animation's back overlay buffer
// via an offscreen LVGL snapshot (RGB565+A8), then publishes it for the
// render task to alpha-blend into every animation frame.
// Modelled on lv_snapshot_take_to_buf (lvgl/src/extra/others/snapshot), with
// one difference that is the entire point: buf_area stays the full snapshot
// rectangle, so the buffer keeps its geometry and stride, while clip_area is
// narrowed to the region that changed. LVGL then draws only that region, into
// its correct place in the existing buffer.
//
// The upstream function also memsets the whole buffer first. Here only the clip
// rectangle is cleared, because everything outside it is still valid from an
// earlier pass. Clearing it at all matters: alpha has to go back to zero where
// a widget shrank or moved away, or it would leave a trail.
bool DefaultUI::snapshotAreaToOverlay(lv_obj_t *obj, uint8_t *buf, uint32_t bufSize, const lv_area_t &clip, int *outW,
                                      int *outH) {
#ifndef GAGGIMATE_SIM
    const uint32_t needed = lv_snapshot_buf_size_needed(obj, LV_IMG_CF_TRUE_COLOR_ALPHA);
    if (needed == 0 || needed > bufSize) {
        return false;
    }
    const lv_coord_t ext = _lv_obj_get_ext_draw_size(obj);
    lv_area_t snapshotArea;
    lv_obj_get_coords(obj, &snapshotArea);
    lv_area_increase(&snapshotArea, ext, ext);

    const int w = lv_obj_get_width(obj) + ext * 2;
    const int h = lv_obj_get_height(obj) + ext * 2;

    lv_area_t clipped = clip;
    if (clipped.x1 < snapshotArea.x1)
        clipped.x1 = snapshotArea.x1;
    if (clipped.y1 < snapshotArea.y1)
        clipped.y1 = snapshotArea.y1;
    if (clipped.x2 > snapshotArea.x2)
        clipped.x2 = snapshotArea.x2;
    if (clipped.y2 > snapshotArea.y2)
        clipped.y2 = snapshotArea.y2;
    if (areaEmpty(clipped)) {
        return false;
    }

    // Reset alpha (and colour) across the region about to be redrawn.
#ifdef GM_TOUCH_PROBE
    const int64_t clear0 = esp_timer_get_time();
#endif
    const int rowBytes = (clipped.x2 - clipped.x1 + 1) * 3;
    for (int y = clipped.y1; y <= clipped.y2; y++) {
        uint8_t *row = buf + (static_cast<size_t>(y - snapshotArea.y1) * w + (clipped.x1 - snapshotArea.x1)) * 3;
        memset(row, 0, rowBytes);
    }
#ifdef GM_TOUCH_PROBE
    g_snapClearSum += esp_timer_get_time() - clear0;
#endif

    lv_disp_t *objDisp = lv_obj_get_disp(obj);
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = lv_disp_get_hor_res(objDisp);
    driver.ver_res = lv_disp_get_hor_res(objDisp);
    lv_disp_drv_use_generic_set_px_cb(&driver, LV_IMG_CF_TRUE_COLOR_ALPHA);

    lv_disp_t fakeDisp;
    lv_memset_00(&fakeDisp, sizeof(lv_disp_t));
    fakeDisp.driver = &driver;

    lv_draw_ctx_t *drawCtx = static_cast<lv_draw_ctx_t *>(lv_mem_alloc(objDisp->driver->draw_ctx_size));
    if (drawCtx == nullptr) {
        return false;
    }
    objDisp->driver->draw_ctx_init(fakeDisp.driver, drawCtx);
    fakeDisp.driver->draw_ctx = drawCtx;
    drawCtx->clip_area = &clipped;     // only this is redrawn
    drawCtx->buf_area = &snapshotArea; // buffer keeps full geometry and stride
    drawCtx->buf = static_cast<void *>(buf);
    driver.draw_ctx = drawCtx;

    lv_disp_t *refrOri = _lv_refr_get_disp_refreshing();
    _lv_refr_set_disp_refreshing(&fakeDisp);
#ifdef GM_TOUCH_PROBE
    const int64_t draw0 = esp_timer_get_time();
    gm_ws_active = true;
#endif
    lv_obj_redraw(drawCtx, obj);
#ifdef GM_TOUCH_PROBE
    gm_ws_active = false;
    g_snapDrawSum += esp_timer_get_time() - draw0;
#endif
    _lv_refr_set_disp_refreshing(refrOri);

    objDisp->driver->draw_ctx_deinit(fakeDisp.driver, drawCtx);
    lv_mem_free(drawCtx);

    if (outW != nullptr)
        *outW = w;
    if (outH != nullptr)
        *outH = h;
    return true;
#else
    return false;
#endif
}

void DefaultUI::refreshSleepOverlay() {
#ifndef GAGGIMATE_SIM
    // The debt lists, LVGL's accumulator and publishOverlayRanges' local range
    // buffer all size to the same cap; a mismatch silently drops debt.
    static_assert(OVERLAY_DIRTY_RECTS == GM_DIRTY_RECT_CAP, "rect caps must match");
    lv_obj_t *scr = animHostScreen;
    if (scr == nullptr) {
        return;
    }
    // lv_obj_redraw() below draws objects where the layout says they are, and
    // it does not run the layout itself — lv_task_handler() does, and that runs
    // after this. On a freshly created screen the flex containers have not been
    // laid out yet, so their children still sit at the container origin and the
    // snapshot catches them stacked. This is a no-op once the layout is valid.
    lv_obj_update_layout(scr);
    // Collect what LVGL redrew since the last pass FIRST, and owe it to both
    // buffers. Doing this before any early return is what makes the retry paths
    // below safe: a refresh that cannot proceed loses nothing.
    lv_area_t fresh[OVERLAY_DIRTY_RECTS];
    const int freshN = lvgl_helper_take_dirty_rects(fresh, OVERLAY_DIRTY_RECTS);
    for (int i = 0; i < freshN; i++) {
        lvgl_helper_rect_add(overlayDirty[0], &overlayDirtyN[0], OVERLAY_DIRTY_RECTS, fresh[i]);
        lvgl_helper_rect_add(overlayDirty[1], &overlayDirtyN[1], OVERLAY_DIRTY_RECTS, fresh[i]);
    }

    // nullptr means the render task is still reading that buffer; retry next
    // pass without stamping the refresh time.
    uint8_t *buf = sleepAnimation.overlayBackBuffer();
    if (buf == nullptr) {
        return;
    }
    const int back = sleepAnimation.overlayBackIndex();

    // Nothing moved and this buffer is already complete: the whole refresh
    // costs one comparison. This is the case that gives touch its time back on
    // a screen that is merely sitting there.
    if (overlayValid[back] && overlayDirtyN[back] == 0) {
        return;
    }

    // Telemetry refresh spacing (see OVERLAY_MIN_REFRESH_US). Only partial
    // refreshes of an already-filled buffer are gated: a buffer that needs a
    // whole fill (screen change, geometry move) paints immediately, and for
    // GM_TOUCH_GRACE_US after any touch edge everything goes straight
    // through so a tap's visual effects never wait behind the gate. The debt
    // merged above survives the return, so a held refresh coalesces instead
    // of dropping.
    if (overlayValid[back]) {
        const int64_t nowUs = esp_timer_get_time();
        if (nowUs - g_touchEdgeAtUs >= GM_TOUCH_GRACE_US && nowUs - lastOverlayRefreshUs < g_overlayMinRefreshUs) {
            return;
        }
    }

    // If the snapshot geometry has moved since this buffer was last written,
    // its contents are no longer where they claim to be: the buffer is indexed
    // from coords.y1-ext and composited at an offset of ext, so a change to
    // the screen's extended draw size displaces everything already in it. A
    // dirty rectangle cannot express that, so redraw the buffer whole.
    {
        lv_area_t geom;
        lv_obj_get_coords(scr, &geom);
        const lv_coord_t extNow = _lv_obj_get_ext_draw_size(scr);
        const int wNow = lv_area_get_width(&geom) + extNow * 2;
        const int hNow = lv_area_get_height(&geom) + extNow * 2;
        if (overlayValid[back] && (overlayW[back] != wNow || overlayH[back] != hNow)) {
            log_i("sleep overlay: snapshot geometry %dx%d -> %dx%d, redrawing buffer %d whole", overlayW[back],
                  overlayH[back], wNow, hNow, back);
            overlayValid[back] = false;
        }
    }

    // One snapshot per owed rectangle rather than one of their bounding box:
    // the snapshot render is the expensive stage, and its cost has to scale
    // with what actually changed, not with how far apart the changes sit.
    lv_area_t clips[OVERLAY_DIRTY_RECTS];
    int clipN = 0;
    if (overlayValid[back]) {
        clipN = overlayDirtyN[back];
        for (int i = 0; i < clipN; i++) {
            clips[i] = overlayDirty[back][i];
        }
    } else {
        // First use of this buffer: it holds nothing, so a partial draw would
        // composite against garbage. Take the whole screen once.
        lv_obj_get_coords(scr, &clips[0]);
        const lv_coord_t ext = _lv_obj_get_ext_draw_size(scr);
        lv_area_increase(&clips[0], ext, ext);
        clipN = 1;
    }

    lastSleepOverlayRefresh = ::millis();
    lastOverlayRefreshUs = esp_timer_get_time();
    int w = 0, h = 0;
    const int64_t probeSnap0 = esp_timer_get_time();
    int64_t probeArea = 0;
    for (int i = 0; i < clipN; i++) {
        if (!snapshotAreaToOverlay(scr, buf, sleepAnimation.overlayCapacity(), clips[i], &w, &h)) {
            // Leave the debt list intact; the next pass retries every rect.
            // Rects already snapshotted this pass just render identically then.
            log_w("Sleep overlay snapshot failed");
            return;
        }
        probeArea += static_cast<int64_t>(lv_area_get_width(&clips[i])) * lv_area_get_height(&clips[i]);
    }
    const int64_t probeSnap1 = esp_timer_get_time();
    // Marked here rather than at the call site, and after the snapshot rather
    // than before it, so the slip log measures the thing that actually costs
    // something. Every early return above is a pass that touched no memory --
    // most of them, since on standby the widgets only change when the clock
    // does -- and marking those would spread the timestamp over passes that
    // cannot have caused anything.
    panelclock::scanoutMark(panelclock::SCANOUT_ACT_OVERLAY);

    // Only the rows that changed need their alpha spans recomputed. The clips
    // are in screen coordinates and the host object is the screen, so screen
    // row and panel row are the same number.
    int ranges[OVERLAY_DIRTY_RECTS][2];
    for (int i = 0; i < clipN; i++) {
        ranges[i][0] = clips[i].y1;
        ranges[i][1] = clips[i].y2 + 1;
    }
    sleepAnimation.publishOverlayRanges(w, h, ranges, clipN);
    g_overlayStats.lastSnapUs = static_cast<uint32_t>(probeSnap1 - probeSnap0);
    g_overlayStats.lastPubUs = static_cast<uint32_t>(esp_timer_get_time() - probeSnap1);
    g_overlayStats.lastAreaPx = static_cast<uint32_t>(probeArea);
    g_overlayStats.lastClips = static_cast<uint32_t>(clipN);
    g_overlayStats.refreshes = g_overlayStats.refreshes + 1;
#ifdef GM_TOUCH_PROBE
    {
        const int64_t snapUs = probeSnap1 - probeSnap0;
        const int64_t pubUs = esp_timer_get_time() - probeSnap1;
        g_ovlN++;
        g_ovlSnapSum += snapUs;
        g_ovlPubSum += pubUs;
        if (snapUs > g_ovlSnapMax)
            g_ovlSnapMax = snapUs;
        if (pubUs > g_ovlPubMax)
            g_ovlPubMax = pubUs;
        g_ovlAreaSum += probeArea;
        if (probeArea > g_ovlAreaMax)
            g_ovlAreaMax = probeArea;
    }
    if (g_probeEdgeUs != 0) {
        const int64_t edge = g_probeEdgeUs;
        g_probeEdgeUs = 0;
        ESP_LOGI("TouchProbe", "GM_TOUCHLAT: %s->overlay_publish %lld us", g_probeEdgeIsPress ? "press" : "release",
                 (long long)(esp_timer_get_time() - edge));
        // Hand the interval to the render task: the pixels reach the panel at
        // the present of the first frame whose composite samples this publish.
        g_probePublishIsPress = g_probeEdgeIsPress;
        g_probePublishUs = edge;
    }
#endif
    overlayValid[back] = true;
    overlayW[back] = w;
    overlayH[back] = h;
    overlayDirtyN[back] = 0;
    // A widget just changed. Do not let interlacing split that change across
    // two frames; on hard-edged UI content the half-updated frame is plainly
    // visible, where on the animation it is not. Regional, not global: only
    // the bands `ranges` actually covers need this, and ranges is the exact
    // list publishOverlayRanges() just used a few lines up, in the same
    // panel-row space -- not a fresh read of anything, so there is nothing
    // to race against that publish. A first-fill or geometry-change pass
    // above set clips[0] (and so ranges[0]) to the whole screen, which
    // covers every band the same way the old global call did; only a
    // partial telemetry update actually narrows this to a handful of bands.
    sleepAnimation.requestBandWarmup(ranges, clipN);
#endif
}

// Entry point for the menu's Scale button (via action_on_grind_screen when the
// scaleMenuButton setting is on). Mirrors the other menu actions: switch to the
// host screen, set a non-heating mode, ensure nothing is active.
void DefaultUI::openScaleScreen() {
    scaleScreenRequested = true;
    changeScreen(SCREEN_ID_GRIND_SCREEN);
    controller->setMode(MODE_GRIND);
    controller->deactivate();
}

// Runs every UI pass. The overlay can only be built once the EEZ grind screen
// object exists (it is created lazily on first load), so creation is deferred
// here; the same pass also keeps the weight readout current and tears the
// overlay down when the user leaves the screen by any path.
void DefaultUI::maintainScaleScreen() {
    if (scaleScreenRequested && currentScreen == SCREEN_ID_GRIND_SCREEN && objects.grind_screen != nullptr) {
        if (scaleScreen == nullptr) {
            buildScaleScreen();
        }
        if (scaleScreen != nullptr) {
            // Re-asserted every pass: the EEZ tick fights HIDDEN flags on these
            // widgets, but never touches translate, so displacement sticks.
            displaceGrindWidgets(true);
            lv_obj_clear_flag(scaleScreen, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(scaleScreen);
            const float w = static_cast<float>(scaleHardwareWeight);
            if (scaleWeightLabel != nullptr && fabsf(w - lastShownScaleWeight) >= 0.05f) {
                lastShownScaleWeight = w;
                lv_label_set_text_fmt(scaleWeightLabel, "%.1f", static_cast<double>(w));
            }
        }
    } else if (scaleScreen != nullptr && !lv_obj_has_flag(scaleScreen, LV_OBJ_FLAG_HIDDEN) &&
               (!scaleScreenRequested || currentScreen != SCREEN_ID_GRIND_SCREEN)) {
        displaceGrindWidgets(false);
        lv_obj_add_flag(scaleScreen, LV_OBJ_FLAG_HIDDEN);
        if (currentScreen != SCREEN_ID_GRIND_SCREEN) {
            scaleScreenRequested = false;
        }
    }
}

// The grind screen's own widgets can't be hidden while the scale overlay is up:
// tick_screen_grind_screen re-derives their HIDDEN flags from flow state every
// tick and would undo it. Translating them off-panel instead is tick-proof and
// fully reversible (the local style prop is simply removed on exit).
void DefaultUI::displaceGrindWidgets(bool displaced) {
    // grind_dials__menu_icon goes with them: the scale overlay draws its own exit
    // arrow in that slot, and the dials' icon is flow-driven so it cannot simply
    // be hidden.
    lv_obj_t *const widgets[] = {objects.main_label4,   objects.grind_start_button, objects.mode_switch1,
                                 objects.target_weight, objects.target_time,        objects.grind_dials__menu_icon};
    for (lv_obj_t *obj : widgets) {
        if (obj == nullptr) {
            continue;
        }
        if (displaced) {
            if (lv_obj_get_style_translate_y(obj, LV_PART_MAIN) != 600) {
                lv_obj_set_style_translate_y(obj, 600, LV_PART_MAIN);
            }
        } else {
            lv_obj_remove_local_style_prop(obj, LV_STYLE_TRANSLATE_Y, LV_PART_MAIN);
        }
    }
}

void DefaultUI::buildScaleScreen() {
    lv_obj_t *scr = objects.grind_screen;
    if (scr == nullptr) {
        return;
    }
    const uint32_t themeIdx = eez_flow_get_selected_theme_index();
    const lv_color_t fg = lv_color_hex(theme_colors[themeIdx][0]);   // text/accent
    const lv_color_t fill = lv_color_hex(theme_colors[themeIdx][1]); // bg/pill fill

    // Transparent, click-through cover: the dial gauges (and their standby/menu
    // icons) stay visible and operable underneath, so the screen shares the
    // visual signature of every other process screen. The grind-only widgets
    // are translated off-panel (displaceGrindWidgets) and the scale widgets
    // occupy the same layout slots the grind widgets vacated.
    lv_obj_t *cover = lv_obj_create(scr);
    scaleScreen = cover;
    lv_obj_set_size(cover, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(cover, 0, 0);
    lv_obj_set_style_radius(cover, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cover, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cover, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cover, 0, LV_PART_MAIN);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    // If a flow action ever deletes the grind screen, the overlay dies with it —
    // null the cached pointers so the next maintain pass rebuilds cleanly.
    lv_obj_add_event_cb(
        cover,
        [](lv_event_t *e) {
            auto *ui = static_cast<DefaultUI *>(lv_event_get_user_data(e));
            ui->scaleScreen = nullptr;
            ui->scaleWeightLabel = nullptr;
            // Child of the cover, so it dies with it. applyAnimPlates walks this
            // pointer every pass and would otherwise reach a freed object.
            ui->scaleTareBtn = nullptr;
        },
        LV_EVENT_DELETE, this);

    // Title in the standard slot (grind's "Grind" label position/font).
    lv_obj_t *title = lv_label_create(cover);
    lv_label_set_text(title, "Scale");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, fg, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -140);

    // Readout as a flex row (number, unit) sized to its content and centred as
    // a group. A one-shot lv_obj_align_to() of the unit against the number only
    // holds for the width the number had at build time: "302.2" in a 48 pt face
    // is ~60 px wider than "0.0", and a centre-aligned number grows both ways,
    // so its last digits landed on top of the "g". Flex re-lays the pair on
    // every width change, so the unit follows the number.
    lv_obj_t *readout = lv_obj_create(cover);
    lv_obj_remove_style_all(readout);
    lv_obj_set_size(readout, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(readout, LV_FLEX_FLOW_ROW);
    // Cross axis END puts both baselines on the row's bottom edge; the unit's
    // bottom padding then lifts its glyph the 6 px the old alignment offset did.
    lv_obj_set_flex_align(readout, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(readout, 8, LV_PART_MAIN);
    lv_obj_clear_flag(readout, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(readout, LV_ALIGN_CENTER, 0, -15);

    scaleWeightLabel = lv_label_create(readout);
    lv_label_set_text(scaleWeightLabel, "0.0");
    lv_obj_set_style_text_font(scaleWeightLabel, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(scaleWeightLabel, fg, LV_PART_MAIN);
    lastShownScaleWeight = -1000.0f;

    lv_obj_t *unit = lv_label_create(readout);
    lv_label_set_text(unit, "g");
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(unit, fg, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(unit, 6, LV_PART_MAIN);

    // Tare as a standard pill (mode_switch1 geometry: 160x50, r10, 2px border).
    // Opaque, and on screen over the animation, so applyAnimPlates drives its
    // background like the generated plates. It is registered below, after its
    // styles are set, so the first capture sees the designed values.
    lv_obj_t *tareBtn = lv_btn_create(cover);
    lv_obj_set_size(tareBtn, 160, 50);
    lv_obj_align(tareBtn, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_style_radius(tareBtn, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tareBtn, fill, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tareBtn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(tareBtn, fg, LV_PART_MAIN);
    lv_obj_set_style_border_opa(tareBtn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tareBtn, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(tareBtn, 0, LV_PART_MAIN);
    // A rebuilt pill is a fresh object with the designed background, so drop the
    // capture and force the next pass to re-apply the current mode to it.
    scaleTareBtn = tareBtn;
    animPlateHas[ANIM_PLATE_COUNT - 1] = false;
    animPlateMode = -1;
    lv_obj_add_event_cb(
        tareBtn, [](lv_event_t *e) { action_on_volumetric_hold(e); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *tareLabel = lv_label_create(tareBtn);
    lv_label_set_text(tareLabel, "Tare");
    lv_obj_set_style_text_font(tareLabel, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(tareLabel, fg, LV_PART_MAIN);
    lv_obj_center(tareLabel);

    // Exit to the menu, in the dials widget's menu-icon slot (CENTER + 210) so
    // it lands where it does on every other screen. The dials' own menu icon is
    // translated off-panel by displaceGrindWidgets while this screen is up:
    // both are img_angle_up_40x40 and both leave to the menu, so leaving both
    // visible drew the arrow twice. This one is kept rather than the dials' one
    // because it also clears scaleScreenRequested and deactivates the
    // controller, which the generated handler does not.
    lv_obj_t *exitBtn = lv_imgbtn_create(cover);
    lv_obj_set_size(exitBtn, 40, 40);
    lv_obj_align(exitBtn, LV_ALIGN_CENTER, 0, 210);
    lv_imgbtn_set_src(exitBtn, LV_IMGBTN_STATE_RELEASED, nullptr, &img_angle_up_40x40, nullptr);
    lv_obj_set_style_img_recolor(exitBtn, fg, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(exitBtn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_event_cb(
        exitBtn,
        [](lv_event_t *e) {
            auto *ui = static_cast<DefaultUI *>(lv_event_get_user_data(e));
            ui->scaleScreenRequested = false;
            ui->controller->deactivate();
            ui->changeScreen(SCREEN_ID_MENU_SCREEN_NEW);
        },
        LV_EVENT_CLICKED, this);

    // The pill above was just built with its designed opaque fill and its plate
    // capture dropped, so the configured mode has to be re-applied to it. The
    // maintenance loop does that, but only on its next tick: entering this screen
    // while the animation is already running would show one frame of the opaque
    // fill first. Apply it here so the pill is never briefly wrong. Same call the
    // loop makes, and a no-op when the animation is not running (mode -1 is
    // re-applied by startSleepAnimation in that case).
    if (sleepAnimation.isActive()) {
        const Settings &plateSettings = controller->getSettings();
        applyAnimPlates(plateSettings.getBgAnimClearPlates(), static_cast<uint32_t>(plateSettings.getBgAnimPlateColor()),
                        plateSettings.getBgAnimPlateOpacity());
    }
}

// Collect every lv_meter under obj (the dial gauges) so their tick length can be animated together.
void DefaultUI::collectMeters(lv_obj_t *obj) {
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        if (gaugeCount < 4 && lv_obj_check_type(child, &lv_meter_class)) {
            gaugeMeters[gaugeCount++] = child;
        }
        collectMeters(child);
    }
}

void DefaultUI::setGaugeTickLength(int32_t len) {
    for (uint8_t i = 0; i < gaugeCount; i++) {
        auto *meter = reinterpret_cast<lv_meter_t *>(gaugeMeters[i]);
        auto *scale = static_cast<lv_meter_scale_t *>(_lv_ll_get_head(&meter->scale_ll));
        if (scale != nullptr) {
            scale->tick_length = static_cast<uint16_t>(len);
        }
        lv_obj_invalidate(gaugeMeters[i]);
    }
}

void DefaultUI::gaugeTickAnimCb(void *var, int32_t v) { static_cast<DefaultUI *>(var)->setGaugeTickLength(v); }

void DefaultUI::animateGaugeTicks(ScreensEnum from, ScreensEnum to) {
    const int32_t fromLen = isShortTickScreen(from) ? GAUGE_TICK_SHORT : GAUGE_TICK_LONG;
    const int32_t toLen = isShortTickScreen(to) ? GAUGE_TICK_SHORT : GAUGE_TICK_LONG;

    lv_anim_del(this, gaugeTickAnimCb); // cancel any in-flight tick animation
    gaugeCount = 0;
    collectMeters(lv_scr_act());
    if (gaugeCount == 0) {
        return;
    }
    // Start at the previous screen's length so the ticks morph continuously in both directions.
    setGaugeTickLength(fromLen);
    if (fromLen == toLen) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_exec_cb(&a, gaugeTickAnimCb);
    lv_anim_set_values(&a, fromLen, toLen);
    lv_anim_set_time(&a, GAUGE_TICK_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void DefaultUI::positionMenuIcon(lv_obj_t *obj, int angle, int radius) {
    int x = sin(angle * M_PI / 180) * radius;
    int y = -1 * cos(angle * M_PI / 180) * radius;
    lv_obj_set_pos(obj, x, y);
}

void DefaultUI::updateState() {
    const auto &settings = controller->getSettings();
    mode = controller->getMode();
    currentTemp = static_cast<int>(controller->getCurrentTemp());
    targetTemp = static_cast<int>(controller->getTargetTemp());
    pressureAvailable = controller->getSystemInfo().capabilities.pressure ? 1 : 0;
    wifiConnected = WiFi.status() == WL_CONNECTED;
    grindAvailable = settings.isSmartGrindActive() || settings.getAltRelayFunction() == ALT_RELAY_GRIND;
    scaleMenuSwap = settings.isScaleMenuButton();

#ifndef GAGGIMATE_SIM
    // Keep the background animation's selection and params current — cheap
    // (two atomic stores) and makes web-UI tweaks apply live on the next frame.
#ifdef GM_ANIM_BENCH
    // The bench exists to render animations, so do not make that conditional
    // on being parked on the standby screen in standby mode.
    bgAnimAllScreens = true;
#else
    bgAnimAllScreens = settings.isBgAnimAllScreens();
#endif
    // A live gradient preview overrides both the animation shown and its
    // gradient until it lapses; the saved selection is re-resolved after.
    int animId = settings.getBgAnimId();
    bool previewActive = false;
    bool previewApply = false;
    String previewGradient;
    {
        std::lock_guard<std::mutex> guard(previewMutex);
        previewActive = previewUntil != 0 && static_cast<long>(::millis() - previewUntil) < 0;
        if (previewActive) {
            animId = previewAnim;
            previewApply = previewDirty;
            previewDirty = false;
            if (previewApply) {
                previewGradient = previewStops;
            }
        }
    }
    uint8_t animP[4];
    bg_parse_params(settings.getBgAnimParams().c_str(), animId, animP);
    sleepAnimation.configure(static_cast<uint8_t>(animId), animP);
    sleepAnimation.setMaxFps(static_cast<uint8_t>(settings.getBgAnimFps()));
    sleepAnimation.setHalfRes(settings.getBgAnimHalfRes() != 0);
    sleepAnimation.setInterlace(settings.getBgAnimInterlace() != 0);
    // Panel refresh rate: live pclk divider (0 = build default). One register
    // poke, but only touch the peripheral on an actual change. Floored at
    // MIN_USER_DIV: see PanelClock.h for the measurement behind it.
    static int lastPclkDiv = INT_MIN;
    const int pclkDiv = panelclock::clampUserDiv(settings.getPanelClockDiv());
    if (pclkDiv != lastPclkDiv) {
        lastPclkDiv = pclkDiv;
        panelclock::setDiv(pclkDiv);
    }
    // Panel VCOM, same shape: a register write over the panel's SPI control
    // interface, which is separate from the RGB data path, so it is safe to do
    // while scan-out is running. Panels that have no such register ignore it.
    static int lastVcom = INT_MIN;
    const int vcom = settings.getPanelVcom();
    if (vcom != lastVcom) {
        lastVcom = vcom;
        if (panelDriver != nullptr) {
            panelDriver->setPanelVcom(vcom);
        }
    }
    // Publish the color theme only on change — setThemeStops bumps a
    // generation counter that makes every animation rebuild its palettes.
    // The key covers everything the resolution depends on; the map and
    // library strings are a few KB at most, read by reference, and equal on
    // every ordinary tick, so the comparison is a length check plus memcmp.
    static int lastThemeAnim = -1;
    static int lastThemeId = -1;
    static String lastCustom;
    static String lastMap;
    static String lastLibrary;
    if (previewActive) {
        lastThemeAnim = -1; // force a re-resolve once the preview lapses
        if (previewApply) {
            uint8_t stops[BG_THEME_MAX_STOPS][3];
            uint8_t pos[BG_THEME_MAX_STOPS];
            bool uniform = true;
            const int nStops = bg_parse_gradient(previewGradient.c_str(), stops, pos, uniform);
            if (nStops > 0) {
                if (uniform) {
                    bganim::setThemeStops(stops, nStops);
                } else {
                    bganim::setThemeStopsPos(stops, pos, nStops);
                }
            }
        }
    } else {
        const int themeId = settings.getBgAnimTheme();
        const String custom = settings.getBgAnimCustomTheme();
        const String &map = settings.getBgAnimThemeMap();
        const String &library = settings.getBgAnimGradients();
        if (animId != lastThemeAnim || themeId != lastThemeId || custom != lastCustom || map != lastMap ||
            library != lastLibrary) {
            lastThemeAnim = animId;
            lastThemeId = themeId;
            lastCustom = custom;
            lastMap = map;
            lastLibrary = library;
            uint8_t stops[BG_THEME_MAX_STOPS][3];
            uint8_t pos[BG_THEME_MAX_STOPS];
            int nStops = 0;
            bool uniform = true;
            bg_resolve_anim_theme(animId, map.c_str(), library.c_str(), themeId, custom.c_str(), stops, pos, nStops,
                                  uniform);
            if (uniform) {
                bganim::setThemeStops(stops, nStops);
            } else {
                bganim::setThemeStopsPos(stops, pos, nStops);
            }
        }
    }
    // Tone is published separately from the stops, and after them: it survives
    // a theme change (setThemeStops re-applies the stored tone), so a user who
    // has dimmed the animation does not get full brightness back the moment
    // they try a different theme. setThemeTone is a no-op when neither value
    // moved, which keeps this off the generation counter on ordinary ticks.
    bganim::setThemeTone(settings.getBgAnimBrightness() * 256 / 100, settings.getBgAnimHighlightKnee() * 255 / 100);
    sleepAnimation.setScrim(settings.getBgAnimScrim());
#endif

    uiFlags.brew_adjustments(brewScreenState == BrewScreenState::Settings);
    uiFlags.active(controller->isActive());
    uiFlags.grind_active(controller->isGrindActive());
    uiFlags.grind_volumetric(controller->isVolumetricAvailable() && settings.isVolumetricTarget());
    uiFlags.heating_flash(heatingFlash);
    uiFlags.temperature_stable(isTemperatureStable);
    uiFlags.has_prev_profile(currentProfileIdx > 0);
    {
        std::lock_guard<std::mutex> guard(profilesMutex);
        uiFlags.has_next_profile(currentProfileIdx + 1 < static_cast<int>(favoritedProfileIds.size()));
    }
}

void DefaultUI::updateSystemStatus() {
    const auto &settings = controller->getSettings();
    systemStatus.bluetooth(controller->getClientController()->isConnected());
    systemStatus.wifi(!apActive && WiFi.status() == WL_CONNECTED);
    bool error = !initialized || waitingForController || controller->isErrorState() || controller->isUpdating() ||
                 controller->isAutotuning() || controller->getSystemInfo().protocolMismatch || !controller->isReady();
    systemStatus.error(error);
    const String errorLabel = error ? getErrorMessage() : "";
    if (stringChanged(systemStatus.error_label(), errorLabel.c_str()))
        systemStatus.error_label(errorLabel.c_str());
    systemStatus.volumetric_available(controller->isVolumetricAvailable());
    systemStatus.bluetooth_scales(controller->isScaleSourceHealthy(controller->getEffectiveScaleSource()));
    systemStatus.controller_version(controller->getSystemInfo().version.c_str());
    systemStatus.display_version(BUILD_GIT_VERSION);
    systemStatus.update_available(updateAvailable);
    // targetScreen, not currentScreen: this runs before handleScreenChange()
    // in the same pass, so on the pass that switches screens currentScreen
    // still names the screen being left. The dials widget's standby and menu
    // icons share one slot with hidden flags that are exact complements of
    // in_menu, so a stale value showed the power icon on the grind screen for
    // one refresh, under the exit arrow buildScaleScreen draws in that slot.
    // targetScreen is set synchronously by changeScreen() and already names
    // the screen this pass is switching to.
    systemStatus.in_menu(targetScreen == SCREEN_ID_MENU_SCREEN_NEW);
    systemStatus.pressure_available(pressureAvailable);
    // The Scale menu button reuses the grind slot, so the flow variable that
    // shows/hides that button must account for both.
    systemStatus.grind_available(grindAvailable || scaleMenuSwap);
    systemStatus.mode(mode);
    const String ip = apActive ? String("4.4.4.1") : WiFi.localIP().toString();
    if (stringChanged(systemStatus.ip(), ip.c_str()))
        systemStatus.ip(ip.c_str());
    const String network = apActive ? String("GaggiMate") : systemStatus.wifi() ? settings.getWifiSsid() : String("Disconnected");
    if (stringChanged(systemStatus.network(), network.c_str()))
        systemStatus.network(network.c_str());
    systemStatus.ap_active(apActive);

    char timeBuf[12] = "";
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5)) {
        strftime(timeBuf, sizeof(timeBuf), settings.isClock24hFormat() ? "%H:%M" : "%I:%M %p", &timeinfo);
        if (!settings.isClock24hFormat() && timeBuf[0] == '0')
            timeBuf[0] = ' ';
    }
    if (stringChanged(systemStatus.time(), timeBuf))
        systemStatus.time(timeBuf);
}

static void populateProfileInfo(ProfileInfoValue &info, const Profile &profile, bool isCurrent) {
    char timeBuf[12];
    formatDuration(static_cast<unsigned long>(profile.getTotalDuration() * 1000.0f), timeBuf, sizeof(timeBuf));
    if (stringChanged(info.name(), profile.label.c_str()))
        info.name(profile.label.c_str());
    info.temperature(profile.temperature);
    if (stringChanged(info.time(), timeBuf))
        info.time(timeBuf);
    info.phases(static_cast<int>(profile.getPhaseCount()));
    info.steps(static_cast<int>(profile.phases.size()));
    info.is_volumetric(profile.isVolumetric());
    info.is_current(isCurrent);
    info.target_weight(profile.getTotalVolume());
}

void DefaultUI::updateProfileInfo() {
    if (!initialized) {
        return;
    }
    populateProfileInfo(selectedProfileInfo, profileManager->getSelectedProfile(), true);
    selectedProfileInfo.dirty(profileDirty);

    // Preview backs the ProfileScreen carousel (index 0 = selected); hold the lock while
    // reading the vector — the profile task rebuilds it concurrently (GM-147).
    bool populated = false;
    {
        std::lock_guard<std::mutex> guard(profilesMutex);
        if (!favoritedProfiles.empty() && currentProfileIdx >= 0 &&
            currentProfileIdx < static_cast<int>(favoritedProfiles.size())) {
            populateProfileInfo(previewProfileInfo, favoritedProfiles[currentProfileIdx], currentProfileIdx == 0);
            populated = true;
        }
    }
    if (!populated) {
        populateProfileInfo(previewProfileInfo, profileManager->getSelectedProfile(), true);
    }
}

void DefaultUI::updateBoiler() {
    const ::Settings &settings = controller->getSettings();
    boiler.current_temperature(controller->getCurrentTemp());
    boiler.target_temperature(controller->getTargetTemp());
    boiler.current_pressure(pressure);
    boiler.target_pressure(controller->getTargetPressure());
    boiler.max_temperature(160.0f);
    boiler.max_pressure(settings.getPressureScaling());
}

// Mirror the live BrewProcess into brew_process_info; every field must stay valid/typed or the StatusScreen flow aborts.
void DefaultUI::updateBrewProcess() {
    if (!initialized) {
        return;
    }

    const Profile &selected = profileManager->getSelectedProfile();
    char buf[12];

    // Profile-derived defaults so the struct is valid even before a process runs.
    formatDuration(static_cast<unsigned long>(selected.getTotalDuration() * 1000.0f), buf, sizeof(buf));
    brewProcess.profile_temperature(selected.temperature);
    if (stringChanged(brewProcess.profile_time(), buf))
        brewProcess.profile_time(buf);
    brewProcess.profile_phases(static_cast<int>(selected.getPhaseCount()));
    brewProcess.profile_steps(static_cast<int>(selected.phases.size()));
    brewProcess.profile_is_volumetric(selected.isVolumetric());
    brewProcess.profile_is_current(true);
    brewProcess.profile_target_weight(selected.getTotalVolume());
    brewProcess.boiler_target_temperature(controller->getTargetTemp());

    // Hold the process lock across every deref below — the logic/AsyncTCP/BLE tasks delete
    // the process at any time (GM-147).
    std::lock_guard<std::recursive_mutex> guard(controller->getProcessLock());
    Process *process = controller->getProcess();
    if (process == nullptr) {
        process = controller->getLastProcess();
    }
    const bool validBrew = process != nullptr && process->getType() == MODE_BREW;
    if (!validBrew) {
        if (stringChanged(brewProcess.phase_type(), ""))
            brewProcess.phase_type("");
        if (stringChanged(brewProcess.phase_name(), ""))
            brewProcess.phase_name("");
        brewProcess.phase_value_current(0.0f);
        brewProcess.phase_value_target(0.0f);
        brewProcess.phase_value_is_weight(false);
        if (stringChanged(brewProcess.elapsed_time(), "0:00"))
            brewProcess.elapsed_time("0:00");
        brewProcess.elapsed_percentage(0.0f);
        brewProcess.is_complete(false);
        return;
    }

    auto *bp = static_cast<BrewProcess *>(process);
    if (bp->profile.phases.empty() || bp->phaseIndex >= bp->profile.phases.size()) {
        // Object is mid-mutation/invalid: keep the last valid values.
        return;
    }

    const Phase phase = bp->currentPhase;
    const bool active = process->isActive();

    // Live profile fields from the running process.
    formatDuration(bp->getTotalDuration(), buf, sizeof(buf));
    brewProcess.profile_temperature(bp->profile.temperature);
    if (stringChanged(brewProcess.profile_time(), buf))
        brewProcess.profile_time(buf);
    brewProcess.profile_phases(static_cast<int>(bp->profile.getPhaseCount()));
    brewProcess.profile_steps(static_cast<int>(bp->profile.phases.size()));
    brewProcess.profile_is_volumetric(bp->target == ProcessTarget::VOLUMETRIC);
    brewProcess.profile_target_weight(bp->getBrewVolume());
    brewProcess.boiler_target_temperature(bp->getTemperature());
    brewProcess.current_volume(bp->currentVolume);

    const char *phaseType = phase.phase == PhaseType::PHASE_TYPE_BREW ? "BREW" : "INFUSION";
    if (stringChanged(brewProcess.phase_type(), phaseType))
        brewProcess.phase_type(phaseType);

    String phaseName = "Finished";
    if (active) {
        phaseName = phase.name;
    } else if (controller->getSettings().isDelayAdjust() && !process->isComplete()) {
        phaseName = "Calibrating...";
    }
    if (stringChanged(brewProcess.phase_name(), phaseName.c_str()))
        brewProcess.phase_name(phaseName.c_str());

    unsigned long now = ::millis();
    if (!active && bp->finished > 0) {
        now = bp->finished;
    }
    const unsigned long elapsedMs = (bp->processStarted > 0 && now >= bp->processStarted) ? now - bp->processStarted : 0;
    formatDuration(elapsedMs, buf, sizeof(buf));
    if (stringChanged(brewProcess.elapsed_time(), buf))
        brewProcess.elapsed_time(buf);

    const bool weightTarget = bp->target == ProcessTarget::VOLUMETRIC && phase.hasVolumetricTarget();
    brewProcess.phase_value_is_weight(weightTarget);
    if (weightTarget) {
        const float target = phase.getVolumetricTarget().value;
        const float current = static_cast<float>(bp->currentVolume);
        brewProcess.phase_value_current(current);
        brewProcess.phase_value_target(target);
        brewProcess.elapsed_percentage(target > 0.0f ? clampPercentage(current / target * 100.0f) : 0.0f);
    } else {
        const unsigned long phaseElapsed =
            (bp->currentPhaseStarted > 0 && now >= bp->currentPhaseStarted) ? now - bp->currentPhaseStarted : 0;
        const float current = phaseElapsed / 1000.0f;
        const float target = bp->getPhaseDuration() / 1000.0f;
        brewProcess.phase_value_current(current);
        brewProcess.phase_value_target(target);
        brewProcess.elapsed_percentage(target > 0.0f ? clampPercentage(current / target * 100.0f) : 0.0f);
    }

    brewProcess.is_complete(process->isComplete());
}

void DefaultUI::updateMenuScreen() {}

String DefaultUI::getErrorMessage() {
    if (controller->isUpdating()) {
        return "Updating...";
    }
    if (controller->isAutotuning()) {
        return "Autotuning...";
    }
    if (controller->getSystemInfo().protocolMismatch) {
        return controller->getSystemInfo().protocolVersion > gm_proto::PROTOCOL_VERSION ? "Version mismatch, update display"
                                                                                        : "Version mismatch, update controller";
    }
    if (controller->isErrorState()) {
        switch (controller->getError()) {
        case ERROR_CODE_RUNAWAY:
            return "Temperature error, restart...";
        default:
            return "Unknown error";
        }
    }
    if (waitingForController) {
        return "Waiting for controller...";
    }
    return initialized ? "" : "Starting...";
}

void DefaultUI::applyTheme() {
    const ::Settings &settings = controller->getSettings();
    int newThemeMode = settings.getThemeMode();
#ifndef GAGGIMATE_SIM // Amoled-specific black theme override is device-only
    if (newThemeMode == 0 && panelDriver == AmoledDisplayDriver::getInstance()) {
        newThemeMode = THEME_ID_AMOLED_DARK;
    }
#endif

    // The element-tint override participates in the change key: enabling,
    // disabling, or recoloring it must re-run change_color_theme just like a
    // mode change. 0x1000000 is out of the 24-bit color range, so "disabled"
    // can never collide with a chosen color.
    const int tintKey = settings.getElementTintEnabled() ? settings.getElementTintColor() : 0x1000000;
    if (newThemeMode != currentThemeMode || tintKey != appliedTintKey) {
        currentThemeMode = newThemeMode;
        appliedTintKey = tintKey;
        // The generated screens read their accent (icons, accent text) from
        // theme_colors slot 0, both at create time and inside
        // change_color_theme's live re-apply — so patching that one slot IS
        // the custom-tint mechanism, and it survives EEZ regen because only
        // runtime memory is written. Pristine values are captured before the
        // first override so disabling the tint restores the theme's own
        // accent.
        static bool accentCaptured = false;
        static uint32_t themeAccent[sizeof(theme_colors) / sizeof(theme_colors[0])];
        constexpr int themeCount = sizeof(theme_colors) / sizeof(theme_colors[0]);
        if (!accentCaptured) {
            accentCaptured = true;
            for (int i = 0; i < themeCount; i++) {
                themeAccent[i] = theme_colors[i][0];
            }
        }
        if (currentThemeMode >= 0 && currentThemeMode < themeCount) {
            theme_colors[currentThemeMode][0] = settings.getElementTintEnabled()
                                                    ? static_cast<uint32_t>(settings.getElementTintColor())
                                                    : themeAccent[currentThemeMode];
        }
        change_color_theme(currentThemeMode);
        // Rest colors just changed under the pressed-feedback props; rewalk.
        pressedStyledRoot = nullptr;
        // change_color_theme just reassigned bg_color on every plate, so a
        // custom-coloured plate (mode 2) has silently reverted to the theme
        // colour while applyAnimPlates still believes it wrote the custom one.
        // Clearing the cache makes the next pass re-apply. The captures are
        // deliberately kept: mode 0 restores the generated plates by re-running
        // the theme, so a stale captured colour cannot outlive a restore.
        animPlateMode = -1;
    }
}

void DefaultUI::loopTask(void *arg) {
    auto *ui = static_cast<DefaultUI *>(arg);
    // The UI work and LVGL do not want the same cadence, and running them at
    // one rate charged the responsive half the price of the expensive half.
    // lv_task_handler() is where the touch controller is polled, so how often
    // it is called IS the input sampling rate -- LVGL cannot read the panel
    // more often than it is asked to run. At one call per 25 ms pass a tap
    // could sit unnoticed for most of a frame, on top of the indev timer's own
    // period, and the result was a screen that answered late. ui_tick() and the
    // widget updates, meanwhile, are worth doing only a few times a second.
    //
    // A refresh still only happens when something was invalidated, so the
    // faster handler costs nothing on a screen that is merely sitting there.
    constexpr unsigned long HANDLER_PERIOD_MS = 5;
    constexpr unsigned long UI_PERIOD_MS = 25;
    unsigned long lastUi = 0;
    while (true) {
        const unsigned long now = ::millis();
#ifdef GM_TOUCH_PROBE
        const int64_t probePass0 = esp_timer_get_time();
        g_statPassStartUs = probePass0;
#endif
        if (now - lastUi >= UI_PERIOD_MS) {
            lastUi = now;
            ui->loop();
        } else {
            lv_task_handler();
            // While the animation owns the panel, LVGL output only reaches the
            // screen through the overlay snapshot, and waiting for the next
            // ui->loop() pass added up to UI_PERIOD_MS to every touch response.
            // Publishing from here puts the snapshot on the same 5 ms cadence
            // as input; the refresh early-outs to one comparison when the
            // handler above drew nothing.
            ui->pumpSleepOverlay();
        }
#ifdef GM_TOUCH_PROBE
        {
            const int64_t passEnd = esp_timer_get_time();
            g_statPassEndUs = passEnd;
            g_statPassStartUs = 0;
            const int64_t passUs = passEnd - probePass0;
            g_uiPassN++;
            g_uiPassSum += passUs;
            if (passUs > g_uiPassMax)
                g_uiPassMax = passUs;
            if (esp_timer_get_time() - g_uiStatLastLog >= 5000000 && g_uiPassN > 0) {
                g_uiStatLastLog = esp_timer_get_time();
                ESP_LOGI("TouchProbe",
                         "GM_UISTAT: passes=%lu avg=%lld max=%lld us | refreshes=%lu snap avg=%lld max=%lld pub "
                         "avg=%lld max=%lld area avg=%lld max=%lld px | clear=%lld draw=%lld scan=%lld scrim=%lld"
                         " | meter=%lld mcalls=%lu ticks=%lu clip=%lu | ev=%lld/%lu sty=%lld/%lu img=%lld/%lu"
                         " rect=%lld/%lu rectr=%lld/%lu rmax=%lld lbl=%lld/%lu ln=%lld/%lu arc=%lld/%lu",
                         (unsigned long)g_uiPassN, (long long)(g_uiPassSum / g_uiPassN), (long long)g_uiPassMax,
                         (unsigned long)g_ovlN, (long long)(g_ovlN ? g_ovlSnapSum / g_ovlN : 0), (long long)g_ovlSnapMax,
                         (long long)(g_ovlN ? g_ovlPubSum / g_ovlN : 0), (long long)g_ovlPubMax,
                         (long long)(g_ovlN ? g_ovlAreaSum / g_ovlN : 0), (long long)g_ovlAreaMax,
                         (long long)(g_ovlN ? g_snapClearSum / g_ovlN : 0),
                         (long long)(g_ovlN ? g_snapDrawSum / g_ovlN : 0),
                         (long long)(g_ovlN ? g_statPubScanUs / g_ovlN : 0),
                         (long long)(g_ovlN ? g_statPubScrimUs / g_ovlN : 0),
                         (long long)(g_ovlN ? g_meterDrawUs / g_ovlN : 0), (unsigned long)g_meterDrawCalls,
                         (unsigned long)g_meterTicksDrawn, (unsigned long)g_meterTicksClipped,
                         (long long)(g_ovlN ? gm_ws_ev_us / g_ovlN : 0), (unsigned long)gm_ws_ev_calls,
                         (long long)(g_ovlN ? gm_ws_style_us / g_ovlN : 0), (unsigned long)gm_ws_style_calls,
                         (long long)(g_ovlN ? gm_ws_img_us / g_ovlN : 0), (unsigned long)gm_ws_img_calls,
                         (long long)(g_ovlN ? gm_ws_rect_us / g_ovlN : 0), (unsigned long)gm_ws_rect_calls,
                         (long long)(g_ovlN ? gm_ws_rectr_us / g_ovlN : 0), (unsigned long)gm_ws_rectr_calls,
                         (long long)gm_ws_rect_max_us,
                         (long long)(g_ovlN ? gm_ws_label_us / g_ovlN : 0), (unsigned long)gm_ws_label_calls,
                         (long long)(g_ovlN ? gm_ws_line_us / g_ovlN : 0), (unsigned long)gm_ws_line_calls,
                         (long long)(g_ovlN ? gm_ws_arc_us / g_ovlN : 0), (unsigned long)gm_ws_arc_calls);
                g_uiPassN = 0;
                g_uiPassSum = g_uiPassMax = 0;
                g_ovlN = 0;
                g_ovlSnapSum = g_ovlSnapMax = g_ovlPubSum = g_ovlPubMax = 0;
                g_ovlAreaSum = g_ovlAreaMax = 0;
                g_snapClearSum = g_snapDrawSum = 0;
                g_statPubScanUs = g_statPubScrimUs = 0;
                g_meterDrawUs = 0;
                g_meterDrawCalls = g_meterTicksDrawn = g_meterTicksClipped = 0;
                gm_ws_ev_us = gm_ws_style_us = gm_ws_img_us = 0;
                gm_ws_ev_calls = gm_ws_style_calls = gm_ws_img_calls = 0;
                gm_ws_rect_us = gm_ws_label_us = gm_ws_line_us = gm_ws_arc_us = 0;
                gm_ws_rect_calls = gm_ws_label_calls = gm_ws_line_calls = gm_ws_arc_calls = 0;
                gm_ws_rectr_us = gm_ws_rect_max_us = 0;
                gm_ws_rectr_calls = 0;
                {
                    // Sizing data for a possible internal-RAM LVGL arena:
                    // the live set the tree walk chases vs. the internal
                    // heap headroom that would have to absorb it.
                    GmLvMemStats m = gm_lv_mem_stats();
                    ESP_LOGI("TouchProbe",
                             "GM_LVMEM: live=%lu hwm=%lu n=%lu allocs=%lu | int_free=%u int_lgst=%u",
                             (unsigned long)m.liveBytes, (unsigned long)m.hwmBytes, (unsigned long)m.liveCount,
                             (unsigned long)m.allocCalls,
                             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                }
            }
        }
#endif
        vTaskDelay(HANDLER_PERIOD_MS / portTICK_PERIOD_MS);
    }
}

void DefaultUI::profileLoopTask(void *arg) {
    auto *ui = static_cast<DefaultUI *>(arg);
    while (true) {
        ui->loopProfiles();
        vTaskDelay(25 / portTICK_PERIOD_MS);
    }
}
