#pragma once
#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>
#include <display/core/Property.h>
#include <display/core/constants.h>
#include <display/core/utils.h>
#include <functional>
#include <vector>

constexpr uint16_t DEFAULT_HARDWARE_SCALE_SAMPLE_RATE_SPS = 10;
constexpr float DEFAULT_HARDWARE_SCALE_IDLE_ALPHA = 0.80f;
constexpr float DEFAULT_HARDWARE_SCALE_ACTIVE_ALPHA = 0.80f;

#define PREFERENCES_KEY "controller"

struct AutoWakeupSchedule {
    String time;    // HH:MM format
    bool days[7]{}; // [Mon, Tue, Wed, Thu, Fri, Sat, Sun]

    AutoWakeupSchedule() : time("07:00") {
        // Default to all days enabled
        for (int i = 0; i < 7; i++) {
            days[i] = true;
        }
    }

    explicit AutoWakeupSchedule(const String &timeStr) : time(timeStr) {
        // Default to all days enabled
        for (int i = 0; i < 7; i++) {
            days[i] = true;
        }
    }

    [[nodiscard]] bool isDayEnabled(const int dayOfWeek) const {
        // dayOfWeek: 1=Monday, 2=Tuesday, ..., 7=Sunday
        if (dayOfWeek < 1 || dayOfWeek > 7)
            return false;
        return days[dayOfWeek - 1];
    }

    void setDayEnabled(const int dayOfWeek, const bool enabled) {
        // dayOfWeek: 1=Monday, 2=Tuesday, ..., 7=Sunday
        if (dayOfWeek >= 1 && dayOfWeek <= 7) {
            days[dayOfWeek - 1] = enabled;
        }
    }

    bool operator==(const AutoWakeupSchedule &other) const {
        if (time != other.time)
            return false;
        for (int i = 0; i < 7; i++) {
            if (days[i] != other.days[i])
                return false;
        }
        return true;
    }
};

// Serialized as "time1|days1;time2|days2" where days is a 7-bit string (e.g. "1111100" for weekdays)
template <> struct PreferencesCodec<std::vector<AutoWakeupSchedule>> {
    static std::vector<AutoWakeupSchedule> read(Preferences &prefs, const char *key, const std::vector<AutoWakeupSchedule> &def);
    static bool write(Preferences &prefs, const char *key, const std::vector<AutoWakeupSchedule> &value);
};

class Settings;
using SettingsCallback = std::function<void(Settings *)>;

class Settings {
  public:
    Settings();

    // Read NVS into the property registry and start the async-save task.
    // Deliberately not the constructor's job: main.cpp defines `Controller
    // controller;` at file scope, so this object is built during C++ global
    // initialization, which runs before app_main and therefore before
    // nvs_flash_init(). Controller::setup() calls this once the runtime is up.
    // Idempotent -- a second call returns immediately rather than starting a
    // second save task.
    void load();

    void batchUpdate(const SettingsCallback &callback);
    void save(bool noDelay = false);

    // Getters and setters
    int getTargetSteamTemp() const { return targetSteamTemp.get(); }
    int getTargetWaterTemp() const { return targetWaterTemp.get(); }
    int getTemperatureOffset() const { return temperatureOffset.get(); }
    float getPressureScaling() const { return pressureScaling.get(); }
    float getScaleFactor1() const { return scaleFactor1.get(); }
    float getScaleFactor2() const { return scaleFactor2.get(); }
    uint16_t getHardwareScaleSampleRateSps() const { return static_cast<uint16_t>(hardwareScaleSampleRateSps.get()); }
    float getHardwareScaleIdleAlpha() const { return hardwareScaleIdleAlpha.get(); }
    float getHardwareScaleActiveAlpha() const { return hardwareScaleActiveAlpha.get(); }
    String getPreferredScaleSource() const { return preferredScaleSource.get(); }
    double getTargetGrindVolume() const { return targetGrindVolume.get(); }
    int getTargetGrindDuration() const { return targetGrindDuration.get(); }
    int getStartupMode() const { return startupMode.get(); }
    int getStandbyTimeout() const { return standbyTimeout.get(); }
    double getBrewDelay() const { return brewDelay.get(); }
    double getGrindDelay() const { return grindDelay.get(); }
    bool isDelayAdjust() const { return delayAdjust.get(); }
    String getPid() const { return pid.get(); }
    String getPumpModelCoeffs() const { return pumpModelCoeffs.get(); }
    String getPumpSlipCoeffs() const { return pumpSlipCoeffs.get(); }
    String getWifiSsid() const { return wifiSsid.get(); }
    String getWifiPassword() const { return wifiPassword.get(); }
    String getWifiApPassword() const { return wifiApPassword.get(); }
    String getMdnsName() const { return mdnsName.get(); }
    bool isHomekit() const { return homekit.get(); }
    bool isVolumetricTarget() const { return volumetricTarget.get(); }
    String getOTAChannel() const { return otaChannel.get(); }
    String getSavedScale() const { return savedScale.get(); }
    bool isBoilerFillActive() const { return boilerFillActive.get(); }
    int getStartupFillTime() const { return startupFillTime.get(); }
    int getSteamFillTime() const { return steamFillTime.get(); }
    bool isSmartGrindActive() const { return smartGrindActive.get(); }
    bool isScaleMenuButton() const { return scaleMenuButton.get(); }
    int getBgAnimId() const { return bgAnimId.get(); }
    String getBgAnimParams() const { return bgAnimParams.get(); }
    bool isBgAnimAllScreens() const { return bgAnimAllScreens.get(); }
    int getBgAnimTheme() const { return bgAnimTheme.get(); }
    String getBgAnimCustomTheme() const { return bgAnimCustomTheme.get(); }
    int getBgAnimFps() const { return bgAnimFps.get(); }
    int getBgAnimHalfRes() const { return bgAnimHalfRes.get(); }
    int getBgAnimInterlace() const { return bgAnimInterlace.get(); }
    int getBgAnimClearPlates() const { return bgAnimClearPlates.get(); }
    int getBgAnimPlateColor() const { return bgAnimPlateColor.get(); }
    int getBgAnimPlateOpacity() const { return bgAnimPlateOpacity.get(); }
    int getBgAnimBrightness() const { return bgAnimBrightness.get(); }
    int getBgAnimHighlightKnee() const { return bgAnimHighlightKnee.get(); }
    int getBgAnimScrim() const { return bgAnimScrim.get(); }
    int getPanelClockDiv() const { return panelClockDiv.get(); }
    int getSmartGrindMode() const { return smartGrindMode.get(); }
    String getSmartGrindIp() const { return smartGrindIp.get(); }
    bool isHomeAssistant() const { return homeAssistant.get(); }
    String getHomeAssistantIP() const { return homeAssistantIP.get(); }
    String getHomeAssistantUser() const { return homeAssistantUser.get(); }
    String getHomeAssistantPassword() const { return homeAssistantPassword.get(); }
    int getHomeAssistantPort() const { return homeAssistantPort.get(); }
    String getHomeAssistantTopic() const { return homeAssistantTopic.get(); }
    bool isMomentaryButtons() const { return momentaryButtons.get(); }
    String getTimezone() const { return timezone.get(); }
    bool isClock24hFormat() const { return clock24hFormat.get(); }
    String getSelectedProfile() const { return selectedProfile.get(); }
    String getStartupProfile() const { return startupProfile.get(); }
    const std::vector<String> &getFavoritedProfiles() const { return favoritedProfiles.get(); }
    std::vector<String> getProfileOrder() const { return profileOrder.get(); }
    int getMainBrightness() const { return mainBrightness.get(); }
    int getStandbyBrightness() const { return standbyBrightness.get(); }
    int getStandbyBrightnessTimeout() const { return standbyBrightnessTimeout.get(); }
    int getWifiApTimeout() const { return wifiApTimeout.get(); }
    float getSteamPumpPercentage() const { return steamPumpPercentage.get(); }
    float getSteamPumpCutoff() const { return steamPumpCutoff.get(); }
    int getThemeMode() const { return themeMode.get(); }
    int getHistoryIndex() const { return historyIndex.get(); }

    [[deprecated]] int getSunriseR() const { return sunriseR; }
    [[deprecated]] int getSunriseG() const { return sunriseG; }
    [[deprecated]] int getSunriseB() const { return sunriseB; }
    [[deprecated]] int getSunriseW() const { return sunriseW; }
    String getSunriseIdle() const { return sunriseIdle.get(); }
    String getSunriseActive() const { return sunriseActive.get(); }
    String getSunriseFinished() const { return sunriseFinished.get(); }
    String getSunriseError() const { return sunriseError.get(); }
    int getSunriseExtBrightness() const { return sunriseExtBrightness.get(); }
    int getEmptyTankDistance() const { return emptyTankDistance.get(); }
    int getFullTankDistance() const { return fullTankDistance.get(); }
    int getAltRelayFunction() const { return altRelayFunction.get(); }
    bool isAutoWakeupEnabled() const { return autowakeupEnabled.get(); }
    std::vector<AutoWakeupSchedule> getAutoWakeupSchedules() const { return autowakeupSchedules.get(); }
    String getButtonBehavior(int index) const {
        if (index >= 0 && index < buttonBehavior.get().size())
            return buttonBehavior.get()[index];
        return "";
    };
    std::vector<String> getButtonBehaviorList() const { return buttonBehavior.get(); }
    float getCommutationGain() const { return commutationGain.get(); }
    float getConvergenceGain() const { return convergenceGain.get(); }
    float getIntegralGain() const { return integralGain.get(); }
    float getMaxPumpPower() const { return maxPumpPower.get(); }

    void setTargetSteamTemp(int target_steam_temp);
    void setTargetWaterTemp(int target_water_temp);
    void setTemperatureOffset(int temperature_offset);
    void setPressureScaling(float pressure_scaling);
    void setScaleFactors(float scale_factor_1, float scale_factor_2);
    void setHardwareScaleConfiguration(uint16_t sample_rate_sps, float idle_alpha, float active_alpha);
    void setPreferredScaleSource(const String &scaleSource);
    void setTargetGrindVolume(double target_grind_volume);
    void setTargetGrindDuration(int target_duration);
    void setStartupMode(int startup_mode);
    void setStandbyTimeout(int standby_timeout);
    void setBrewDelay(double brewDelay);
    void setGrindDelay(double grindDelay);
    void setDelayAdjust(bool delay_adjust);
    void setPid(const String &pid);
    void setPumpModelCoeffs(const String &pumpModelCoeffs);
    void setPumpSlipCoeffs(const String &pumpSlipCoeffs);
    void setWifiSsid(const String &wifiSsid);
    void setWifiPassword(const String &wifiPassword);
    void setWifiApPassword(const String &wifiApPassword);
    void setMdnsName(const String &mdnsName);
    void setHomekit(bool homekit);
    void setVolumetricTarget(bool volumetric_target);
    void setOTAChannel(const String &otaChannel);
    void setSavedScale(const String &savedScale);
    void setBoilerFillActive(bool boiler_fill_active);
    void setStartupFillTime(int startup_fill_time);
    void setSteamFillTime(int steam_fill_time);
    void setSmartGrindActive(bool smart_grind_active);
    void setScaleMenuButton(bool scale_menu_button);
    void setBgAnimId(int bg_anim_id);
    void setBgAnimParams(const String &bg_anim_params);
    void setBgAnimAllScreens(bool bg_anim_all_screens);
    void setBgAnimTheme(int bg_anim_theme);
    void setBgAnimCustomTheme(const String &bg_anim_custom_theme);
    void setBgAnimFps(int bg_anim_fps);
    void setBgAnimHalfRes(int bg_anim_half_res);
    void setBgAnimInterlace(int bg_anim_interlace);
    void setBgAnimClearPlates(int bg_anim_clear_plates);
    void setBgAnimPlateColor(int bg_anim_plate_color);
    void setBgAnimPlateOpacity(int bg_anim_plate_opacity);
    void setBgAnimBrightness(int bg_anim_brightness);
    void setBgAnimHighlightKnee(int bg_anim_highlight_knee);
    void setBgAnimScrim(int bg_anim_scrim);
    void setPanelClockDiv(int panel_clock_div);
    void setSmartGrindIp(String smart_grind_ip);
    void setSmartGrindMode(int smart_grind_mode);
    void setHomeAssistant(bool homeAssistant);
    void setHomeAssistantUser(const String &homeAssistantUser);
    void setHomeAssistantPassword(const String &homeAssistantPassword);
    void setHomeAssistantIP(const String &homeAssistantIP);
    void setHomeAssistantPort(int homeAssistantPort);
    void setHomeAssistantTopic(const String &homeAssistantTopic);
    void setMomentaryButtons(bool momentary_buttons);
    void setTimezone(String timezone);
    void setClockFormat(bool format_24h);
    void setSelectedProfile(String selected_profile);
    void setStartupProfile(String startup_profile);
    void setFavoritedProfiles(std::vector<String> favorited_profiles);
    void addFavoritedProfile(String profile);
    void removeFavoritedProfile(String profile);
    void setProfileOrder(std::vector<String> profile_order);
    void setMainBrightness(int main_brightness);
    void setStandbyBrightness(int standby_brightness);
    void setStandbyBrightnessTimeout(int standby_brightness_timeout);
    void setWifiApTimeout(int timeout);
    void setSteamPumpPercentage(float steam_pump_percentage);
    void setSteamPumpCutoff(float steam_pump_cutoff);
    void setThemeMode(int theme_mode);
    void setHistoryIndex(int history_index);
    [[deprecated]] void setSunriseR(int sunrise_r);
    [[deprecated]] void setSunriseG(int sunrise_g);
    [[deprecated]] void setSunriseB(int sunrise_b);
    [[deprecated]] void setSunriseW(int sunrise_w);
    void setSunriseIdle(String hexColor);
    void setSunriseActive(String hexColor);
    void setSunriseFinished(String hexColor);
    void setSunriseError(String hexColor);
    void setSunriseExtBrightness(int sunrise_ext_brightness);
    void setEmptyTankDistance(int empty_tank_distance);
    void setFullTankDistance(int full_tank_distance);
    void setAltRelayFunction(int alt_relay_function);
    void setAutoWakeupEnabled(bool enabled);
    void setAutoWakeupSchedules(const std::vector<AutoWakeupSchedule> &schedules);
    void setButtonBehavior(int index, String behavior);
    void setButtonBehaviorList(const std::vector<String> &behavior_list);

    void setCommutationGain(float commutationGain);
    void setConvergenceGain(float convergenceGain);
    void setIntegralGain(float integralGain);
    void setMaxPumpPower(float maxPumpPower);

  private:
    Preferences preferences;
    PropertyRegistry registry; // must precede the properties, they register themselves here

    Property<String> selectedProfile{registry, "sp", ""};
    Property<String> startupProfile{registry, "sup", ""}; // Empty = last used profile, otherwise profile ID
    Property<int> targetSteamTemp{registry, "ts", 145};
    Property<int> targetWaterTemp{registry, "tw", 80};
    Property<int> temperatureOffset{registry, "to", DEFAULT_TEMPERATURE_OFFSET};
    Property<float> pressureScaling{registry, "ps", DEFAULT_PRESSURE_SCALING};
    Property<float> scaleFactor1{registry, "sf1", 0.0f};
    Property<float> scaleFactor2{registry, "sf2", 0.0f};
    Property<int> hardwareScaleSampleRateSps{registry, "hs_rate", DEFAULT_HARDWARE_SCALE_SAMPLE_RATE_SPS};
    Property<float> hardwareScaleIdleAlpha{registry, "hs_ia", DEFAULT_HARDWARE_SCALE_IDLE_ALPHA};
    Property<float> hardwareScaleActiveAlpha{registry, "hs_aa", DEFAULT_HARDWARE_SCALE_ACTIVE_ALPHA};
    Property<String> preferredScaleSource{registry, "pss", "hardware"};
    Property<double> targetGrindVolume{registry, "tgv", 18.0};
    Property<int> targetGrindDuration{registry, "tgd", 25000};
    Property<double> brewDelay{registry, "del_br", 800.0};
    Property<double> grindDelay{registry, "del_gd", 1000.0};
    Property<bool> delayAdjust{registry, "del_ad", true};
    Property<int> startupMode{registry, "sm", MODE_STANDBY};
    Property<bool> autowakeupEnabled{registry, "ab_en", false};
    Property<std::vector<AutoWakeupSchedule>> autowakeupSchedules{registry, "ab_schedules", {AutoWakeupSchedule("07:00")}};
    Property<int> standbyTimeout{registry, "sbt", DEFAULT_STANDBY_TIMEOUT_MS};
    Property<String> pid{registry, "pid", DEFAULT_PID};
    Property<String> wifiSsid{registry, "ws", ""};
    Property<String> wifiPassword{registry, "wp", ""};
    Property<String> wifiApPassword{registry, "wap", ""}; // empty until generated on first start
    Property<String> mdnsName{registry, "mn", DEFAULT_MDNS_NAME};
    Property<String> savedScale{registry, "ssc", ""};
    Property<bool> homekit{registry, "hk", false};
    Property<bool> volumetricTarget{registry, "vt", false};
    Property<bool> boilerFillActive{registry, "bf_a", false};
    Property<int> startupFillTime{registry, "bf_su", 5000};
    Property<int> steamFillTime{registry, "bf_st", 5000};
    Property<bool> smartGrindActive{registry, "sg_a", false};
    // Menu shows a Scale button (live weight + tare) in place of Grind.
    Property<bool> scaleMenuButton{registry, "scl_mb", false};
    // Background animation: which procedural animation plays behind the UI
    // (index into BG_ANIMATIONS), its per-animation parameters, and whether it
    // runs behind every screen or only during standby sleep.
    Property<int> bgAnimId{registry, "bg_an", 0};
    // Per-animation params, "p0,p1,p2,p3;p0,p1,p2,p3;..." indexed by anim id,
    // each 0-100; missing/short entries fall back to the animation's defaults.
    Property<String> bgAnimParams{registry, "bg_anp", ""};
    Property<bool> bgAnimAllScreens{registry, "bg_all", false};
    Property<int> bgAnimTheme{registry, "bg_th", 0};
    Property<String> bgAnimCustomTheme{registry, "bg_ct", ""};
    // Animation task frame-rate cap. Lower values cut the animation's PSRAM
    // write bandwidth (~460 KB/frame), which is the lever against RGB scan-out
    // underruns at high panel refresh rates.
    Property<int> bgAnimFps{registry, "bg_fps", 30};
    // 1 = render at half resolution and double on the way out. Defaults on:
    // it is the only way every animation clears 40 fps on this panel.
    Property<int> bgAnimHalfRes{registry, "bg_half", 1};
    // 1 = push every other row pair, alternating each frame.
    Property<int> bgAnimInterlace{registry, "bg_ilace", 1};
    // What to do with the opaque background plates on the screens that carry
    // one (brew, status, profile, info, and the pill holding the scale weight)
    // while the animation is running: 0 = leave them as the theme drew them,
    // 1 = hide them so the animation looks the same on every screen, 2 = repaint
    // them in bgAnimPlateColor at bgAnimPlateOpacity. Values 0 and 1 predate
    // the third mode and keep their original meaning.
    Property<int> bgAnimClearPlates{registry, "bg_plate", 1};
    // Plate fill for mode 2. Colour is 0xRRGGBB; opacity is a percentage, where
    // 0 is fully transparent (same result as mode 1) and 100 fully opaque (same
    // as mode 0 but in the chosen colour).
    Property<int> bgAnimPlateColor{registry, "bg_pcol", 0x000000};
    Property<int> bgAnimPlateOpacity{registry, "bg_popa", 35};
    // Animation content brightness, 0-100 percent of the theme's own levels.
    // Distinct from mainBrightness/standbyBrightness, which are the LCD
    // backlight and so dim the text along with the animation, buying no
    // contrast at all. Default 100 leaves every existing theme as it looks
    // today; this is a styling control, not the legibility fix.
    Property<int> bgAnimBrightness{registry, "bg_bri", 100};
    // Highlight shoulder, 0-100 as a percentage of full scale. Channels above
    // this level are compressed into a quarter of their remaining range, so
    // mid-tones keep their colour and only the highlights bend. 100 is off.
    // Measured: 30 takes the worst animation/theme pair in the fleet from
    // contrast 1.00 to 4.29.
    Property<int> bgAnimHighlightKnee{registry, "bg_knee", 100};
    // Text scrim: how far to dim the animation immediately behind overlaid
    // widget pixels, 0-100 percent, where 0 is off and 100 is black. Unlike the
    // two controls above this does not change the animation anywhere text is
    // not, which is why it is the one that defaults on.
    //
    // 55 is the weakest setting that clears the WCAG comfortable bar of 4.5 on
    // every animation and theme in the fleet: measured (tools/animbench/
    // lumaprofile.cpp --scrim) it takes the worst pair, lava on Mono, from
    // contrast 1.00 to 4.81, where 50 reaches only 4.13. Weakest matters because
    // the scrim is visible as a soft dark halo behind the readouts, so anything
    // past the bar is a plate nobody asked for -- 75 measures 11.06, far more
    // dimming than legibility needs.
    //
    // Note the percentage is a scale on the gamma-encoded RGB565 value, not on
    // linear light, so its effect on measured luminance is much stronger than
    // the number suggests: 55 percent of the encoded value is roughly 25 percent
    // of the luminance.
    Property<int> bgAnimScrim{registry, "bg_scrim", 55};
    // RGB pixel-clock divider off the 80 MHz LCD group clock; 0 = keep the
    // build-flag boot value. The IDF 4.4 driver only does integer division,
    // so real choices are 80/n: 5=16 MHz (~61 Hz), 6=13.3 MHz (~51 Hz),
    // 7=11.4 MHz (~43 Hz). Applied live from DefaultUI::updateState.
    Property<int> panelClockDiv{registry, "pclk_div", 0};
    Property<bool> smartGrindToggle{registry, "sg_t", false}; // legacy, seeds the smartGrindMode default
    Property<int> smartGrindMode{registry, "sg_m", 0};
    Property<String> smartGrindIp{registry, "sg_i", ""};
    Property<bool> homeAssistant{registry, "ha_a", false};
    Property<String> homeAssistantUser{registry, "ha_u", ""};
    Property<String> homeAssistantPassword{registry, "ha_pw", ""};
    Property<String> homeAssistantIP{registry, "ha_i", ""};
    Property<int> homeAssistantPort{registry, "ha_p", 1883};
    Property<String> homeAssistantTopic{registry, "ha_t", DEFAULT_HOME_ASSISTANT_TOPIC};
    Property<bool> momentaryButtons{registry, "mb", false};
    Property<String> timezone{registry, "tz", DEFAULT_TIMEZONE};
    Property<bool> clock24hFormat{registry, "clk_24h", true};
    Property<String> otaChannel{registry, "oc", DEFAULT_OTA_CHANNEL};
    Property<std::vector<String>> favoritedProfiles{registry, "fp", {}};
    Property<std::vector<String>> profileOrder{registry, "po", {}}; // persisted profile ordering
    Property<float> steamPumpPercentage{registry, "spp", DEFAULT_STEAM_PUMP_PERCENTAGE};
    Property<float> steamPumpCutoff{registry, "spc", DEFAULT_STEAM_PUMP_CUTOFF};
    Property<int> historyIndex{registry, "hi", 0};

    // Display settings
    Property<int> mainBrightness{registry, "main_b", 16};
    Property<int> standbyBrightness{registry, "standby_b", 8};
    Property<int> standbyBrightnessTimeout{registry, "standby_bt", 60000}; // 60 seconds default
    Property<int> wifiApTimeout{registry, "wifi_apt", DEFAULT_WIFI_AP_TIMEOUT_MS};
    Property<int> themeMode{registry, "theme", 0};

    // Sunrise settings (r/g/b/w are legacy load-only values that seed the idle color default)
    int sunriseR = 0;
    int sunriseG = 250;
    int sunriseB = 150;
    int sunriseW = 255;
    Property<String> sunriseIdle{registry, "sr_i", "#00FFFF"};
    Property<String> sunriseActive{registry, "sr_a", "#0000FF"};
    Property<String> sunriseFinished{registry, "sr_f", "#00FF00"};
    Property<String> sunriseError{registry, "sr_e", "#FF0000"};
    Property<int> sunriseExtBrightness{registry, "sr_exb", 75};
    Property<int> emptyTankDistance{registry, "sr_ed", 210};
    Property<int> fullTankDistance{registry, "sr_fd", 30};

    Property<int> altRelayFunction{registry, "alt_relay", ALT_RELAY_GRIND}; // Default to grind
    Property<std::vector<String>> buttonBehavior{registry, "btnb", {"brew", "steam", "water"}};

    // Pump settings
    Property<String> pumpModelCoeffs{registry, "pmc", DEFAULT_PUMP_MODEL_COEFFS};
    Property<String> pumpSlipCoeffs{registry, "psc", DEFAULT_PUMP_SLIP_COEFFS};
    Property<float> commutationGain{registry, "p_cm", DEFAULT_COMMUTATION_GAIN};
    Property<float> convergenceGain{registry, "p_cv", DEFAULT_CONVERGENCE_GAIN};
    Property<float> integralGain{registry, "p_ig", DEFAULT_INTEGRAL_GAIN};
    Property<float> maxPumpPower{registry, "p_mp", 1.0f};

    void doSave();
    TaskHandle_t taskHandle = nullptr;
    [[noreturn]] static void loopTask(void *arg);
};

#endif // SETTINGS_H
