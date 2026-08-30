#ifndef WIFISTAWATCHDOGPLUGIN_H
#define WIFISTAWATCHDOGPLUGIN_H

#include "../core/Plugin.h"
#include <Arduino.h>

struct Event;

// Force STA reassociation when arduino-esp32 auto-reconnect has stopped retrying.
// _isReconnectableReason() ignores vendor codes (UniFi 81, 168) and several
// 802.11 reasons, so the SDK can leave STA "disconnected" indefinitely.
// NetworkWatchdog only acts after WiFi is up; this covers the never-comes-back
// case.
class WifiStaWatchdogPlugin : public Plugin {
  public:
    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override;

  private:
    static constexpr unsigned long STA_DOWN_GRACE_MS = 20000;
    static constexpr unsigned long STA_REASSOC_BACKOFF_MS = 30000;
    static constexpr unsigned long AP_STA_RETRY_MS = 60000;
    // Escalation past reassoc. Observed on the bench (log 52, 2026-08-30): a
    // boot that came up wedged spent over an hour in a 4WAY_HANDSHAKE_TIMEOUT
    // loop - the driver's own auto-reconnect, its coex reconnect policy, and
    // this plugin's disconnect+begin all cycling without ever associating,
    // while an esptool reset joined the same AP in 10 s. Reassoc alone cannot
    // clear whatever RF/coex state that wedge lives in, so after
    // DRIVER_RESTART_AFTER_MS down the driver is torn down and brought back
    // (WIFI_OFF deinits it, which also returns the one-time ~6 KB cache-TX
    // pool growth), and after REBOOT_AFTER_DOWN_MS the chip reboots - the one
    // cure proven on the bench - under the same uptime+standby gate as
    // NetworkWatchdogPlugin::rebootAllowed.
    static constexpr unsigned long DRIVER_RESTART_AFTER_MS = 150000;         // ~2.5 min down, repeats
    static constexpr unsigned long REBOOT_AFTER_DOWN_MS = 10UL * 60 * 1000;  // 10 min down
    static constexpr unsigned long MIN_UPTIME_FOR_REBOOT = 15UL * 60 * 1000; // mirror NetworkWatchdog
    static constexpr unsigned long MIN_STANDBY_FOR_REBOOT = 5UL * 60 * 1000; // mirror NetworkWatchdog

    Controller *controller = nullptr;
    String ssid;
    String pass;
    unsigned long lastConnectedMs = 0;
    unsigned long lastReassocMs = 0;
    unsigned long lastApRetryMs = 0;
    unsigned long lastDriverRestartMs = 0;
    unsigned long standbySinceMs = 0;
    bool armed = false;
    bool updating = false;
    bool rebootHeld = false;

    void forceReassoc();
    void restartDriver();
    bool rebootAllowed(unsigned long now) const;
};

#endif // WIFISTAWATCHDOGPLUGIN_H
