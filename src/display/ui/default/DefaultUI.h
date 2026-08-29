#ifndef DEFAULTUI_H
#define DEFAULTUI_H

#include <atomic>
#include <display/core/PluginManager.h>
#include <display/core/ProfileManager.h>
#include <display/core/constants.h>
#include <display/drivers/Driver.h>
#include <display/models/profile.h>
#include <display/ui/default/SleepAnimation.h>
#include <display/ui/default/eez/screens.h>
#include <display/ui/default/eez/structs.h>
#include <mutex>

class Controller;

constexpr int RERENDER_INTERVAL_IDLE = 2500;
// Freshness floor: force a pass at least this often during an active
// process even if no change event fired. Was 100, which against the old
// ~650 ms pipeline merely throttled; raising it alone measured marginal
// (logs 36 vs 37) because the real saturator is the event side, bounded by
// RERENDER_MIN_INTERVAL below.
constexpr int RERENDER_INTERVAL_ACTIVE = 300;
// Rate ceiling for telemetry passes: a rerender pass may not START within
// this many ms of the previous pass start unless a touch edge arrived
// within GM_TOUCH_GRACE_US. Every telemetry change event sets rerender,
// the loadtest feed changes continuously, and a full pass costs ~90-110
// ms, so unspaced passes ran back to back (~7 Hz, 60-100% UI-task duty)
// and a touch edge waited a median 122 ms just to be READ (GM_EDGEWAIT,
// log 37): the indev poll runs on the same task the passes monopolize.
// Measured alone this spacer was null on tap latency (log 38 == log 36
// medians) because the snapshot refresh keeps running off invalidations
// that never ride the rerender flag — OVERLAY_MIN_REFRESH_US below is the
// gate on the expensive unit; this one only spaces the updateState/eez
// side. 4 Hz numeric readouts are indistinguishable from 7 Hz by eye.
constexpr int RERENDER_MIN_INTERVAL = 250;
// Rate ceiling for overlay refresh STARTS (the snapshot+publish unit in
// refreshSleepOverlay), in esp_timer time. Spacing rerender passes alone
// measured null on tap latency (log 38 == log 36 medians): invalidations
// keep arriving outside the gated pass, and each ~100 ms refresh re-armed
// the next one back to back, so the UI task stayed ~96% busy and a touch
// edge still waited a median ~110 ms to be read. This gates the expensive
// unit itself; the debt lists make a held refresh lossless (coalesced, not
// dropped). A touch edge since the last refresh start bypasses the gate,
// which is what separates this from the unconditional 1000 ms throttle
// that was removed for holding tap feedback a second (see maintain call
// site). Full-buffer fills (screen change, geometry move) also bypass.
constexpr int64_t OVERLAY_MIN_REFRESH_US = 250000;
// How long after a touch edge the gates above stay open. An edge-vs-stamp
// comparison is not enough: a release's CLICK handler only sets flags, the
// flags are applied in the NEXT loop() pass, and by then an edge-triggered
// refresh has already re-stamped the gate — the click's visible result
// would wait out a full gate period. Inside this window every refresh and
// rerender pass runs ungated, so an interaction's knock-on invalidations
// (pressed visuals, applied flags, screen change) all flow immediately.
constexpr int64_t GM_TOUCH_GRACE_US = 400000;

constexpr int TEMP_HISTORY_INTERVAL = 250;
constexpr int TEMP_HISTORY_LENGTH = 20 * 1000 / TEMP_HISTORY_INTERVAL;

int16_t calculate_angle(int set_temp, int range, int offset);

enum class BrewScreenState { Brew, Settings };

class DefaultUI {
  public:
    DefaultUI(Controller *controller, Driver *driver, PluginManager *pluginManager);

    // Default work methods
    void init();
    void loop();
    void loopProfiles();

    // Interface methods
    void changeScreen(ScreensEnum screen);

    void changeBrewScreenMode(BrewScreenState state);
    void onProfileSwitch();
    void onNextProfile();
    void onPreviousProfile();
    void onProfileSelect();
    void setBrightness(int brightness) {
        if (panelDriver) {
            panelDriver->setBrightness(brightness);
        }
    };

    void onVolumetricDelete();

    // Scale mode: opened from the menu's Grind slot when the scaleMenuButton
    // setting is on. Hosted as an overlay on the grind screen (EEZ flow can't
    // grow new screens at runtime): live weight readout + tare + back.
    void openScaleScreen();

    void markDirty() { rerender = true; }
    void markProfileDirty() { profileDirty = true; }
    void markProfileClean() { profileDirty = false; }

    void applyTheme();

    bool isTaskHealthy() const {
        return is_task_healthy(eTaskGetState(taskHandle)) && is_task_healthy(eTaskGetState(profileTaskHandle));
    }

  private:
    void setupPanel();
    void setupState();

    void handleScreenChange();

    void startSleepAnimation();
    void stopSleepAnimation();
    // Move the animation's host screen without interrupting it. The only
    // per-screen state the animation holds is the host's transparent
    // background: the plate table and the status icons it also rewrites are
    // fixed global objects that span every screen, so a screen change costs
    // one style property, not a restart.
    void adoptAnimHost(lv_obj_t *host);
    void releaseAnimHost();
    void maintainSleepAnimation();
    void refreshSleepOverlay();
    // The 5 ms handler pass in loopTask calls this so widget redraws reach the
    // overlay snapshot on the input cadence instead of waiting for the next
    // 25 ms ui->loop() pass. Costs a bool check when the animation is off and
    // one comparison when it is on but nothing was drawn.
    void pumpSleepOverlay();
    // Hide, restore or repaint the opaque background plates the generated
    // screens put behind their content. mode is Settings::getBgAnimClearPlates
    // (0 keep, 1 hide, 2 custom); color is 0xRRGGBB and opaPct 0-100, both used
    // only in mode 2. See the definition for which objects and why.
    void applyAnimPlates(int mode, uint32_t color = 0, int opaPct = 0);
    // lv_snapshot_take_to_buf with a clip area. Renders only `clip` into `buf`,
    // leaving the rest of the buffer alone, so an overlay refresh costs what
    // actually changed rather than a whole screen.
    bool snapshotAreaToOverlay(lv_obj_t *obj, uint8_t *buf, uint32_t bufSize, const lv_area_t &clip, int *outW,
                               int *outH);
    void maintainScaleScreen();
    void buildScaleScreen();
    void displaceGrindWidgets(bool displaced);
    lv_obj_t *scaleScreen = nullptr;      // overlay covering the grind screen
    lv_obj_t *scaleWeightLabel = nullptr;
    lv_obj_t *scaleTareBtn = nullptr;     // opaque pill, driven by applyAnimPlates
    bool scaleScreenRequested = false;
    bool scaleMenuSwap = false; // settings.isScaleMenuButton(), cached per render
    float lastShownScaleWeight = -1000.0f;
    SleepAnimation sleepAnimation;
    unsigned long lastSleepAnimAttempt = 0;
    unsigned long lastSleepOverlayRefresh = 0;
    // Dirty regions still owed to each of the two overlay buffers, in screen
    // coordinates, and whether that buffer has ever held a full render. They
    // are written alternately, so each carries its own debt: a partial update
    // is only valid against what that specific buffer already holds.
    //
    // A list per buffer, not one rectangle: a single bounding box unioned the
    // temperature readout and the status bar into the whole screen, and every
    // refresh then re-rendered and re-scanned all 480x480 pixels. That was
    // most of the 650 ms UI pass the touch probe measured.
    static constexpr int OVERLAY_DIRTY_RECTS = 4;
    lv_area_t overlayDirty[2][OVERLAY_DIRTY_RECTS];
    int overlayDirtyN[2] = {0, 0};
    bool overlayValid[2] = {false, false};
    // The snapshot geometry each overlay buffer was built with.
    //
    // The snapshot is sized width+ext*2 by height+ext*2, where ext is the
    // screen's extended draw size, and the animation composites it centred, at
    // an offset of exactly ext. LVGL recomputes ext as widgets with shadows or
    // outlines come and go, so it is not a constant. The buffer is indexed
    // relative to coords.y1-ext, which means a change to ext moves the whole
    // buffer's coordinate system -- every pixel already in it is now read a
    // few rows off.
    //
    // The dirty rectangles track which pixels changed, not that the frame they
    // are expressed in changed, so a partial publish after an ext change
    // leaves the untouched remainder displaced. On the panel that is the
    // profile name drawn a second time about 25 rows below itself, faint and
    // clipped, while every widget that happened to be re-published looks
    // perfect. It survives with the scan-out slip counter reading zero,
    // because nothing about the scan-out is wrong.
    int overlayW[2] = {-1, -1};
    int overlayH[2] = {-1, -1};
    bool bgAnimAllScreens = false;         // settings.isBgAnimAllScreens(), cached per render
    // millis() when setupPanel() finished building the UI, or 0 before that.
    // The animation needs a live screen to host its overlay snapshot, so it
    // cannot start earlier. `initialized` cannot serve this purpose: it is set
    // only when a controller connects, which is a different thing entirely.
    unsigned long uiBuiltAt = 0;
    lv_obj_t *animHostScreen = nullptr;    // screen whose bg was made transparent for the animation
    // Number of entries in the plate table in applyAnimPlates.
    static constexpr int ANIM_PLATE_COUNT = 9;
    // Last applied (mode, color, opacity), so a no-op settings poll costs one
    // comparison. -1 means nothing has been applied yet, which forces the first
    // pass through even when the stored mode is 0.
    //
    // The cache records what this code last wrote, not what is on screen, so
    // anything else that writes bg_color on a plate silently invalidates it.
    // change_color_theme() does exactly that, which is why applyTheme() resets
    // animPlateMode after a live theme switch.
    int animPlateMode = -1;
    uint32_t animPlateColor = 0;
    int animPlateOpaPct = -1;
    // Style each plate had before mode 1 or 2 touched it, used to put it back
    // for mode 0. Captured rather than assumed: these objects do not all start
    // out fully opaque. Captured per plate, not in one pass, because the scale
    // screen's Tare pill is built long after the generated screens and would
    // otherwise be "restored" to a zero-initialised entry.
    bool animPlateHas[ANIM_PLATE_COUNT] = {};
    lv_opa_t animPlateOpa[ANIM_PLATE_COUNT] = {};
    lv_color_t animPlateBg[ANIM_PLATE_COUNT] = {};
    std::atomic<bool> panelStopRequested{false};
    std::atomic<bool> panelStopped{false};
    std::atomic<bool> otaEnded{false};

    // Animate the dial meters' tick length on screen change (short on profile/new-menu, long elsewhere).
    void animateGaugeTicks(ScreensEnum from, ScreensEnum to);
    void collectMeters(lv_obj_t *obj);
    void setGaugeTickLength(int32_t len);
    static void gaugeTickAnimCb(void *var, int32_t v);
    lv_obj_t *gaugeMeters[4] = {nullptr};
    uint8_t gaugeCount = 0;
    void positionMenuIcon(lv_obj_t *obj, int angle, int radius);

    void updateState();
    void updateSystemStatus();
    void updateProfileInfo();
    void updateBoiler();
    void updateBrewProcess();
    void updateMenuScreen();
    String getErrorMessage();

    void adjustDials(lv_obj_t *dials);
    void adjustTarget(lv_obj_t *obj, double percentage, double start, double range) const;

    int tempHistory[TEMP_HISTORY_LENGTH] = {0};
    int tempHistoryIndex = 0;
    int prevTargetTemp = 0;
    bool isTempHistoryInitialized = false;
    int isTemperatureStable = false;
    unsigned long lastTempLog = 0;

    void updateTempHistory();
    void updateTempStableFlag();
    void reloadProfiles();

    Driver *panelDriver = nullptr;
    Controller *controller;
    PluginManager *pluginManager;
    ProfileManager *profileManager;

    // Screen state
    int updateAvailable = false;
    int apActive = false;
    int wifiConnected = false;
    int waitingForController = false;
    int initialized = false;
    int grindAvailable = false;

    // Seasonal flags
    int christmasMode = false;

    bool rerender = false;
    unsigned long lastRender = 0;
    // Same stamp in esp_timer time, compared against g_touchEdgeAtUs for the
    // telemetry-pass spacer's touch bypass (millis and esp_timer drift, so
    // the comparison stays within one clock).
    int64_t lastRenderUs = 0;
    // Last overlay refresh START in esp_timer time, for OVERLAY_MIN_REFRESH_US
    // and its g_touchEdgeAtUs comparison (same clock as the edge stamp).
    int64_t lastOverlayRefreshUs = 0;

    int mode = MODE_STANDBY;
    bool pressureAvailable = false;
    int heatingFlash = 0;
    float pressure = 0.0f;
    float currentTemp = 0.0f;
    float targetTemp = 0.0f;
    double activeWeight = 0.0;
    BrewScreenState brewScreenState = BrewScreenState::Brew;

    // EEZ Structs
    SystemStatusValue systemStatus;
    ProfileInfoValue selectedProfileInfo;
    ProfileInfoValue previewProfileInfo;
    BoilerValue boiler;
    UIFlagsValue uiFlags;
    BrewProcessValue brewProcess;
    Value currentWeight = FloatValue(0.0);
    Value steamReady = BooleanValue(false);
    Value grindWeightTarget = FloatValue(18.0);
    Value grindTimeTarget = StringValue("0:15");

    int profileDirty = 0;
    int currentProfileIdx = 0;
    std::atomic<int> profileLoaded{0}; // cleared from event callbacks on arbitrary tasks
    // The profile task (core 0) rebuilds these while the UI task reads them (GM-147).
    std::mutex profilesMutex;
    std::vector<String> favoritedProfileIds;
    std::vector<Profile> favoritedProfiles;
    int currentThemeMode = -1; // Force applyTheme on first loop

    // Screen change
    ScreensEnum targetScreen = ScreensEnum::SCREEN_ID_STANDBY_SCREEN;
    ScreensEnum currentScreen = ScreensEnum::SCREEN_ID_STANDBY_SCREEN;

    // Standby brightness control
    unsigned long standbyEnterTime = 0;

    TaskHandle_t taskHandle;
    static void loopTask(void *arg);
    TaskHandle_t profileTaskHandle;
    static void profileLoopTask(void *arg);
};

#endif // DEFAULTUI_H
