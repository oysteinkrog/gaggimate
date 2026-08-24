#include "Settings.h"

#include <algorithm>
#include <display/util/ColorConversion.h>
#include <utility>

std::vector<AutoWakeupSchedule>
PreferencesCodec<std::vector<AutoWakeupSchedule>>::read(Preferences &prefs, const char *key,
                                                        const std::vector<AutoWakeupSchedule> &def) {
    String schedulesStr = prefs.getString(key, "");
    std::vector<AutoWakeupSchedule> schedules;

    if (schedulesStr.length() > 0) {
        int start = 0;
        int end = schedulesStr.indexOf(';');

        while (end != -1 || start < schedulesStr.length()) {
            String scheduleStr = (end != -1) ? schedulesStr.substring(start, end) : schedulesStr.substring(start);

            int pipePos = scheduleStr.indexOf('|');
            if (pipePos != -1) {
                String timeStr = scheduleStr.substring(0, pipePos);
                String daysStr = scheduleStr.substring(pipePos + 1);

                AutoWakeupSchedule schedule;
                schedule.time = timeStr;

                if (daysStr.length() == 7) {
                    for (int i = 0; i < 7; i++) {
                        schedule.days[i] = (daysStr.charAt(i) == '1');
                    }
                }

                schedules.push_back(schedule);
            }

            if (end == -1)
                break;
            start = end + 1;
            end = schedulesStr.indexOf(';', start);
        }
    }

    return schedules.empty() ? def : schedules;
}

bool PreferencesCodec<std::vector<AutoWakeupSchedule>>::write(Preferences &prefs, const char *key,
                                                              const std::vector<AutoWakeupSchedule> &value) {
    String serialized = "";
    for (size_t i = 0; i < value.size(); i++) {
        if (i > 0)
            serialized += ";";
        serialized += value[i].time + "|";
        for (int j = 0; j < 7; j++) {
            serialized += value[i].days[j] ? "1" : "0";
        }
    }
    return nvsPutString(prefs, key, serialized);
}

Settings::Settings() = default;

void Settings::load() {
    if (taskHandle != nullptr) {
        return; // already loaded; a second call must not spawn a second save task
    }
    // A read-only begin() fails when the namespace does not exist yet, which is
    // the normal first boot: every property then keeps its compiled-in default
    // and the first save creates the namespace. Anything else (a full or
    // corrupted NVS partition) looks identical from here, so log it rather than
    // let a silent reset to defaults pass for a fresh install.
    if (!preferences.begin(PREFERENCES_KEY, true)) {
        ESP_LOGW("Settings", "NVS namespace '%s' not readable; starting from defaults", PREFERENCES_KEY);
    }
    for (auto *property : registry) {
        property->load(preferences);
    }

    // Legacy migrations: derive defaults for keys that were never persisted
    if (!preferences.isKey("sg_m")) {
        smartGrindMode.initDefault(smartGrindToggle.get() ? 1 : 0);
    }
    sunriseR = preferences.getInt("sr_r", 0);
    sunriseG = preferences.getInt("sr_g", 250);
    sunriseB = preferences.getInt("sr_b", 150);
    sunriseW = preferences.getInt("sr_w", 255);
    if (!preferences.isKey("sr_i")) {
        sunriseIdle.initDefault(ColorConversion::toHex(sunriseR, sunriseG, sunriseB, sunriseW));
    }
    preferences.end();

    xTaskCreate(loopTask, "Settings::loop", configMINIMAL_STACK_SIZE * 6, this, 1, &taskHandle);
}

void Settings::batchUpdate(const SettingsCallback &callback) {
    // Changed properties mark themselves dirty; the next flush writes them in one NVS session
    callback(this);
}

void Settings::save(bool noDelay) {
    if (noDelay) {
        doSave();
    }
}

void Settings::setTargetSteamTemp(const int target_steam_temp) { targetSteamTemp.set(target_steam_temp); }

void Settings::setTargetWaterTemp(const int target_water_temp) { targetWaterTemp.set(target_water_temp); }

void Settings::setTemperatureOffset(const int temperature_offset) { temperatureOffset.set(temperature_offset); }

void Settings::setPressureScaling(const float pressure_scaling) { pressureScaling.set(pressure_scaling); }

void Settings::setScaleFactors(float scale_factor_1, float scale_factor_2) {
    scaleFactor1.set(scale_factor_1);
    scaleFactor2.set(scale_factor_2);
}

void Settings::setPreferredScaleSource(const String &scaleSource) {
    if (scaleSource == "hardware" || scaleSource == "bluetooth" || scaleSource == "auto") {
        preferredScaleSource.set(scaleSource);
    }
}

void Settings::setTargetGrindVolume(double target_grind_volume) { targetGrindVolume.set(target_grind_volume); }

void Settings::setTargetGrindDuration(const int target_duration) { targetGrindDuration.set(target_duration); }

void Settings::setBrewDelay(double brew_Delay) { brewDelay.set(std::clamp(brew_Delay, 0.0, 4000.0)); }

void Settings::setGrindDelay(double grind_Delay) { grindDelay.set(std::clamp(grind_Delay, 0.0, 4000.0)); }

void Settings::setDelayAdjust(bool delay_adjust) { delayAdjust.set(delay_adjust); }

void Settings::setStartupMode(const int startup_mode) { startupMode.set(startup_mode); }

void Settings::setStandbyTimeout(int standby_timeout) { standbyTimeout.set(standby_timeout); }

void Settings::setPid(const String &pid) { this->pid.set(pid); }

void Settings::setPumpModelCoeffs(const String &pumpModelCoeffs) { this->pumpModelCoeffs.set(pumpModelCoeffs); }

void Settings::setPumpSlipCoeffs(const String &pumpSlipCoeffs) { this->pumpSlipCoeffs.set(pumpSlipCoeffs); }

void Settings::setWifiSsid(const String &wifiSsid) { this->wifiSsid.set(wifiSsid); }

void Settings::setWifiPassword(const String &wifiPassword) { this->wifiPassword.set(wifiPassword); }

void Settings::setWifiApPassword(const String &wifiApPassword) { this->wifiApPassword.set(wifiApPassword); }

void Settings::setMdnsName(const String &mdnsName) { this->mdnsName.set(mdnsName); }

void Settings::setHomekit(const bool homekit) { this->homekit.set(homekit); }

void Settings::setVolumetricTarget(bool volumetric_target) { volumetricTarget.set(volumetric_target); }

void Settings::setOTAChannel(const String &otaChannel) { this->otaChannel.set(otaChannel); }

void Settings::setSavedScale(const String &savedScale) { this->savedScale.set(savedScale); }

void Settings::setBoilerFillActive(bool boiler_fill_active) { boilerFillActive.set(boiler_fill_active); }

void Settings::setStartupFillTime(int startup_fill_time) { startupFillTime.set(startup_fill_time); }

void Settings::setSteamFillTime(int steam_fill_time) { steamFillTime.set(steam_fill_time); }

void Settings::setSmartGrindActive(bool smart_grind_active) { smartGrindActive.set(smart_grind_active); }
void Settings::setScaleMenuButton(bool scale_menu_button) { scaleMenuButton.set(scale_menu_button); }
void Settings::setBgAnimId(int bg_anim_id) { bgAnimId.set(bg_anim_id); }
void Settings::setBgAnimParams(const String &bg_anim_params) { bgAnimParams.set(bg_anim_params); }
void Settings::setBgAnimAllScreens(bool bg_anim_all_screens) { bgAnimAllScreens.set(bg_anim_all_screens); }
void Settings::setBgAnimTheme(int bg_anim_theme) { bgAnimTheme.set(bg_anim_theme); }
void Settings::setBgAnimFps(int bg_anim_fps) { bgAnimFps.set(bg_anim_fps); }
void Settings::setBgAnimHalfRes(int bg_anim_half_res) { bgAnimHalfRes.set(bg_anim_half_res); }
void Settings::setBgAnimInterlace(int bg_anim_interlace) { bgAnimInterlace.set(bg_anim_interlace); }
void Settings::setBgAnimClearPlates(int bg_anim_clear_plates) { bgAnimClearPlates.set(bg_anim_clear_plates); }

void Settings::setBgAnimPlateColor(int bg_anim_plate_color) { bgAnimPlateColor.set(bg_anim_plate_color & 0xFFFFFF); }

void Settings::setBgAnimPlateOpacity(int bg_anim_plate_opacity) {
    bgAnimPlateOpacity.set(bg_anim_plate_opacity < 0 ? 0 : (bg_anim_plate_opacity > 100 ? 100 : bg_anim_plate_opacity));
}
// All three are percentages the web UI sends as slider values, so they are
// clamped rather than trusted: an out-of-range brightness would otherwise reach
// the Q8 conversion and wrap, and a negative scrim would brighten the text
// background instead of dimming it.
void Settings::setBgAnimBrightness(int bg_anim_brightness) {
    bgAnimBrightness.set(bg_anim_brightness < 0 ? 0 : (bg_anim_brightness > 100 ? 100 : bg_anim_brightness));
}
void Settings::setBgAnimHighlightKnee(int bg_anim_highlight_knee) {
    bgAnimHighlightKnee.set(bg_anim_highlight_knee < 0 ? 0 : (bg_anim_highlight_knee > 100 ? 100 : bg_anim_highlight_knee));
}
void Settings::setBgAnimScrim(int bg_anim_scrim) {
    bgAnimScrim.set(bg_anim_scrim < 0 ? 0 : (bg_anim_scrim > 100 ? 100 : bg_anim_scrim));
}
void Settings::setPanelClockDiv(int panel_clock_div) { panelClockDiv.set(panel_clock_div); }
void Settings::setBgAnimCustomTheme(const String &bg_anim_custom_theme) { bgAnimCustomTheme.set(bg_anim_custom_theme); }

void Settings::setSmartGrindIp(String smart_grind_ip) { smartGrindIp.set(smart_grind_ip); }

void Settings::setSmartGrindMode(int smart_grind_mode) { smartGrindMode.set(smart_grind_mode); }

void Settings::setHomeAssistant(const bool homeAssistant) { this->homeAssistant.set(homeAssistant); }

void Settings::setHomeAssistantIP(const String &homeAssistantIP) { this->homeAssistantIP.set(homeAssistantIP); }

void Settings::setHomeAssistantPort(const int homeAssistantPort) { this->homeAssistantPort.set(homeAssistantPort); }

void Settings::setHomeAssistantTopic(const String &homeAssistantTopic) { this->homeAssistantTopic.set(homeAssistantTopic); }

void Settings::setHomeAssistantUser(const String &homeAssistantUser) { this->homeAssistantUser.set(homeAssistantUser); }

void Settings::setHomeAssistantPassword(const String &homeAssistantPassword) {
    this->homeAssistantPassword.set(homeAssistantPassword);
}

void Settings::setMomentaryButtons(bool momentary_buttons) { momentaryButtons.set(momentary_buttons); }

void Settings::setTimezone(String timezone) { this->timezone.set(timezone); }

void Settings::setClockFormat(bool clock_24h_format) { clock24hFormat.set(clock_24h_format); }

void Settings::setSelectedProfile(String selected_profile) { selectedProfile.set(selected_profile); }

void Settings::setStartupProfile(String startup_profile) { startupProfile.set(startup_profile); }

void Settings::setFavoritedProfiles(std::vector<String> favorited_profiles) { favoritedProfiles.set(favorited_profiles); }

void Settings::addFavoritedProfile(String profile) {
    std::vector<String> profiles = favoritedProfiles.get();
    if (std::find(profiles.begin(), profiles.end(), profile) != profiles.end()) {
        return;
    }
    profiles.emplace_back(std::move(profile));
    favoritedProfiles.set(profiles);
}

void Settings::removeFavoritedProfile(String profile) {
    std::vector<String> profiles = favoritedProfiles.get();
    profiles.erase(std::remove(profiles.begin(), profiles.end(), profile), profiles.end());
    favoritedProfiles.set(profiles);
}

void Settings::setProfileOrder(std::vector<String> profile_order) {
    std::vector<String> cleaned;
    cleaned.reserve(profile_order.size());
    for (auto &id : profile_order) {
        if (id.isEmpty())
            continue;
        if (std::find(cleaned.begin(), cleaned.end(), id) == cleaned.end()) {
            cleaned.emplace_back(std::move(id));
        }
    }

    profileOrder.set(cleaned);
}

void Settings::setMainBrightness(int main_brightness) { mainBrightness.set(main_brightness); }

void Settings::setStandbyBrightness(int standby_brightness) { standbyBrightness.set(standby_brightness); }

void Settings::setStandbyBrightnessTimeout(int standby_brightness_timeout) {
    standbyBrightnessTimeout.set(standby_brightness_timeout);
}

void Settings::setWifiApTimeout(int timeout) { wifiApTimeout.set(timeout); }

void Settings::setSteamPumpPercentage(float steam_pump_percentage) { steamPumpPercentage.set(steam_pump_percentage); }

void Settings::setSteamPumpCutoff(float steam_pump_cutoff) { steamPumpCutoff.set(steam_pump_cutoff); }

void Settings::setThemeMode(int theme_mode) { themeMode.set(theme_mode); }

void Settings::setHistoryIndex(int history_index) { historyIndex.set(history_index); }

void Settings::setSunriseR(int sunrise_r) { sunriseR = sunrise_r; }

void Settings::setSunriseG(int sunrise_g) { sunriseG = sunrise_g; }

void Settings::setSunriseB(int sunrise_b) { sunriseB = sunrise_b; }

void Settings::setSunriseW(int sunrise_w) { sunriseW = sunrise_w; }

void Settings::setSunriseIdle(String hexColor) { sunriseIdle.set(hexColor); }

void Settings::setSunriseActive(String hexColor) { sunriseActive.set(hexColor); }

void Settings::setSunriseFinished(String hexColor) { sunriseFinished.set(hexColor); }

void Settings::setSunriseError(String hexColor) { sunriseError.set(hexColor); }

void Settings::setSunriseExtBrightness(int sunrise_ext_brightness) { sunriseExtBrightness.set(sunrise_ext_brightness); }

void Settings::setEmptyTankDistance(int empty_tank_distance) { emptyTankDistance.set(empty_tank_distance); }

void Settings::setFullTankDistance(int full_tank_distance) { fullTankDistance.set(full_tank_distance); }

void Settings::setAltRelayFunction(int alt_relay_function) { altRelayFunction.set(alt_relay_function); }

void Settings::setAutoWakeupEnabled(bool enabled) { autowakeupEnabled.set(enabled); }

void Settings::setAutoWakeupSchedules(const std::vector<AutoWakeupSchedule> &schedules) { autowakeupSchedules.set(schedules); }

void Settings::setButtonBehavior(int index, String behavior) {
    std::vector<String> behaviors = buttonBehavior.get();
    if (index < 0 || index >= behaviors.size()) {
        return;
    }
    behaviors[index] = std::move(behavior);
    buttonBehavior.set(behaviors);
}

void Settings::setButtonBehaviorList(const std::vector<String> &behavior_list) { buttonBehavior.set(behavior_list); }

void Settings::setCommutationGain(float commutation_gain) { commutationGain.set(commutation_gain); }

void Settings::setConvergenceGain(float convergence_gain) { convergenceGain.set(convergence_gain); }

void Settings::setIntegralGain(float integral_gain) { integralGain.set(integral_gain); }

void Settings::setMaxPumpPower(float max_pump_power) { maxPumpPower.set(max_pump_power); }

void Settings::doSave() {
    bool dirty = false;
    for (auto *property : registry) {
        if (property->isDirty()) {
            dirty = true;
            break;
        }
    }
    if (!dirty) {
        return;
    }
    ESP_LOGI("Settings", "Saving changed settings");
    // Running the loop against a namespace that failed to open would fail every
    // single put and log a line naming all of them. The flags survive either way
    // now that store() re-arms on failure, so bail out and let the next 5 s flush
    // try the same values against a hopefully recovered NVS.
    if (!preferences.begin(PREFERENCES_KEY, false)) {
        ESP_LOGE("Settings", "Could not open NVS namespace '%s' for writing; keeping changes pending", PREFERENCES_KEY);
        return;
    }
    // A per-key failure does not abort the flush: the other properties can still
    // be written, and the ones that failed stay dirty for the next pass. Counted
    // and logged once rather than per property, so a full NVS partition does not
    // bury the rest of the log at one line per setting every 5 s.
    size_t failed = 0;
    const char *firstFailed = nullptr;
    for (auto *property : registry) {
        if (!property->store(preferences)) {
            if (failed == 0) {
                firstFailed = property->name();
            }
            failed++;
        }
    }
    preferences.end();
    if (failed > 0) {
        ESP_LOGE("Settings", "%u setting(s) could not be written to NVS (first: %s); retrying in 5 s", failed,
                 firstFailed != nullptr ? firstFailed : "?");
    }
}

[[noreturn]] void Settings::loopTask(void *arg) {
    auto *settings = static_cast<Settings *>(arg);
    while (true) {
        settings->doSave();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
