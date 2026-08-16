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
constexpr int RERENDER_INTERVAL_ACTIVE = 100;

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
    void maintainSleepAnimation();
    void refreshSleepOverlay();
    // Hide or restore the opaque background plates the generated screens put
    // behind their content. See the definition for which objects and why.
    void applyAnimPlates(bool clear);
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
    bool scaleScreenRequested = false;
    bool scaleMenuSwap = false; // settings.isScaleMenuButton(), cached per render
    float lastShownScaleWeight = -1000.0f;
    SleepAnimation sleepAnimation;
    unsigned long lastSleepAnimAttempt = 0;
    unsigned long lastSleepOverlayRefresh = 0;
    // Dirty region still owed to each of the two overlay buffers, in screen
    // coordinates, and whether that buffer has ever held a full render. They
    // are written alternately, so each carries its own debt: a partial update
    // is only valid against what that specific buffer already holds.
    lv_area_t overlayDirty[2] = {{1, 1, 0, 0}, {1, 1, 0, 0}};
    bool overlayValid[2] = {false, false};
    bool bgAnimAllScreens = false;         // settings.isBgAnimAllScreens(), cached per render
    lv_obj_t *animHostScreen = nullptr;    // screen whose bg was made transparent for the animation
    bool animPlatesCleared = false;
    lv_opa_t animPlateOpa[4] = {LV_OPA_COVER, LV_OPA_COVER, LV_OPA_COVER, LV_OPA_COVER};
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

    xTaskHandle taskHandle;
    static void loopTask(void *arg);
    xTaskHandle profileTaskHandle;
    static void profileLoopTask(void *arg);
};

#endif // DEFAULTUI_H
