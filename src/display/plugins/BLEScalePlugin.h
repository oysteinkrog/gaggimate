#ifndef BLESCALEPLUGIN_H
#define BLESCALEPLUGIN_H
#include "../core/Plugin.h"
#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"
// std::unique_ptr<RemoteScales> member below. The NimBLE headers reached by
// remote_scales.h pull this in transitively on device, so the missing include
// only ever surfaced as a simulator build failure.
#include <memory>

void on_ble_measurement(float value);

constexpr unsigned long UPDATE_INTERVAL_MS = 1000;
constexpr unsigned int RECONNECTION_TRIES = 15;

class BLEScalePlugin : public Plugin {
  public:
    BLEScalePlugin();
    ~BLEScalePlugin();

    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override;
    ;

    void connect(const std::string &uuid);
    void scan() const;
    void disconnect();
    void onMeasurement(float value) const;
    bool isConnected() { return scale != nullptr && scale->isConnected(); };
    std::string getName() {
        if (scale != nullptr && scale->isConnected()) {
            return scale->getDeviceName();
        }
        return "";
    };
    std::string getUUID() {
        if (scale != nullptr && scale->isConnected()) {
            return scale->getDeviceAddress();
        }
        return "";
    };
    int getRSSI() {
        if (scale != nullptr && scale->isConnected()) {
            return scale->getRSSI();
        }
        return 0;
    };

    std::vector<DiscoveredDevice> getDiscoveredScales() const;
    void tare() const;

    // Accessors for the native scale fields that drivers optionally expose
    // (see RemoteScales). Each returns a sentinel value if not supported.
    float getFlowRate() const { return scale != nullptr && scale->hasFlowRate() ? scale->getFlowRate() : 0.0f; }
    bool hasFlowRate() const { return scale != nullptr && scale->hasFlowRate(); }
    uint8_t getBatteryLevel() const {
        return scale != nullptr && scale->hasBatteryLevel() ? scale->getBatteryLevel() : REMOTE_SCALES_BATTERY_UNKNOWN;
    }
    bool hasBatteryLevel() const { return scale != nullptr && scale->hasBatteryLevel(); }
    ScaleWeightUnit getWeightUnit() const {
        return scale != nullptr && scale->hasWeightUnit() ? scale->getWeightUnit() : ScaleWeightUnit::UNKNOWN;
    }
    bool hasWeightUnit() const { return scale != nullptr && scale->hasWeightUnit(); }
    uint32_t getScaleTimerMs() const { return scale != nullptr && scale->hasScaleTimer() ? scale->getScaleTimerMs() : 0; }
    bool hasScaleTimer() const { return scale != nullptr && scale->hasScaleTimer(); }

  private:
    void update();
    void onProcessStart() const;
    void pollScaleMetadata();

    void establishConnection();

    // mutable because scan() is const and must be able to arm discovery: it
    // is the one entry point the web UI's scan button shares with the
    // saved-scale auto paths, and loop() kills any scan while this is false.
    mutable bool active = false;
    bool doConnect = false;
    std::string uuid;

    unsigned long lastUpdate = 0;
    unsigned long lastConnectAttempt = 0;
    unsigned int reconnectionTries = 0;
    static constexpr unsigned long CONNECT_RETRY_INTERVAL_MS = 2000;

    // Discovery scan phases; the policy and the measurements behind these
    // numbers live above the gm_ble_scan_* definitions in BLEScalePlugin.cpp.
    // scan() enters the boost phase; update() runs bursts after it expires.
    // Mutable because scan() is const and every phase entry goes through it.
    static constexpr unsigned long SCAN_BOOST_MS = 60000;
    static constexpr unsigned long SCAN_BURST_LEN_MS = 15000;
    static constexpr unsigned long SCAN_BURST_PERIOD_MS = 90000;
    mutable unsigned long scanBoostUntil = 0;
    mutable unsigned long scanBurstStopAt = 0;
    mutable unsigned long scanNextBurstAt = 0;

    // Cached scale-metadata values used to avoid firing an event for each
    // unchanged poll tick. Reset when the scale disconnects.
    uint8_t lastBatteryLevel = REMOTE_SCALES_BATTERY_UNKNOWN;
    ScaleWeightUnit lastWeightUnit = ScaleWeightUnit::UNKNOWN;

    // Latch so the mid-brew oz warning + volumetric abort fires once per
    // transition into ounces, not once per sample at ~10 Hz. Reset on
    // disconnect and when the unit returns to grams.
    mutable bool warnedOunceMidBrew = false;

    // Rate limiting for callbacks
    mutable unsigned long lastMeasurementTime = 0;
    static constexpr unsigned long MIN_MEASUREMENT_INTERVAL_MS = 10; // Max 100 measurements per second

    Controller *controller = nullptr;
    PluginManager *pluginManager = nullptr;
    RemoteScalesPluginRegistry *pluginRegistry = nullptr;
    RemoteScalesScanner *scanner = nullptr;
    std::unique_ptr<RemoteScales> scale = nullptr;
};

extern BLEScalePlugin BLEScales;

#endif // BLESCALEPLUGIN_H
