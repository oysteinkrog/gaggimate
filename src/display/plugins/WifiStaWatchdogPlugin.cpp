#include "WifiStaWatchdogPlugin.h"
#include "../core/Controller.h"
#include "../core/Settings.h"
#include <WiFi.h>
#include <esp_log.h>
#include <esp_wifi.h>

static constexpr char LOG_TAG[] = "WifiStaWd";

void WifiStaWatchdogPlugin::setup(Controller *c, PluginManager *pluginManager) {
    controller = c;
    ssid = controller->getSettings().getWifiSsid();
    pass = controller->getSettings().getWifiPassword();
    armed = ssid.length() > 0 && pass.length() > 0;
    lastConnectedMs = millis();
    lastReassocMs = 0;

    // Suspend during OTA: a forced WiFi.begin() mid-download would brick the
    // pull.  NetworkWatchdog already gates its WiFi.reconnect() the same way.
    pluginManager->on("ota:update:start", [this](Event const &) { updating = true; });
    pluginManager->on("ota:update:end", [this](Event const &) { updating = false; });

    ESP_LOGI(LOG_TAG, "armed=%d grace=%lums backoff=%lums", armed, STA_DOWN_GRACE_MS, STA_REASSOC_BACKOFF_MS);
}

void WifiStaWatchdogPlugin::loop() {
    if (!armed || updating)
        return;

    // AP-fallback: setupWifi() lands here when the boot-time connect misses
    // its 10 s window (router still booting, slow AP handshake). Previously a
    // dead end until a manual reboot — now retry the stored credentials every
    // AP_STA_RETRY_MS with the config AP kept alive (AP_STA), so the device
    // self-heals the moment the network is back. Controller::loop() drops the
    // AP and clears isApConnection when STA_GOT_IP fires.
    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        if (mode == WIFI_MODE_APSTA && WiFi.status() == WL_CONNECTED)
            return; // recovered; Controller::loop() finishes the switch
        const unsigned long apNow = millis();
        if (apNow - lastApRetryMs < AP_STA_RETRY_MS)
            return;
        lastApRetryMs = apNow;
        // Never touch WiFi mode or STA state while someone is actually using
        // the config portal. Mode transitions and WiFi.begin()/disconnect()
        // with a station associated to our softAP are a documented crash class
        // in this Arduino core (dhcps and mode-switch faults), and every call
        // below runs on core 0 — the same core as the WiFi/LWIP tasks,
        // AsyncTCP and NimBLE, and the only core whose idle task is subscribed
        // to the panic-enabled task watchdog. Deferring costs at most one retry
        // interval of self-healing, and only while someone is connected to the
        // portal, which is exactly when self-healing matters least.
        if (WiFi.softAPgetStationNum() > 0) {
            ESP_LOGI(LOG_TAG, "AP fallback: %u station(s) on the config AP, deferring STA retry", WiFi.softAPgetStationNum());
            return;
        }
        ESP_LOGW(LOG_TAG, "AP fallback active; retrying STA connect to %s", ssid.c_str());
        if (mode == WIFI_MODE_AP) {
            WiFi.mode(WIFI_AP_STA);
        } else {
            WiFi.disconnect(false); // reset a stuck attempt, keep the AP up
            delay(50);
        }
        WiFi.begin(ssid.c_str(), pass.c_str());
        return;
    }
    if (mode == WIFI_MODE_NULL)
        return;

    const unsigned long now = millis();
    if (WiFi.status() == WL_CONNECTED) {
        lastConnectedMs = now;
        return;
    }

    if (now - lastConnectedMs < STA_DOWN_GRACE_MS)
        return;
    if (lastReassocMs != 0 && now - lastReassocMs < STA_REASSOC_BACKOFF_MS)
        return;

    forceReassoc();
    lastReassocMs = now;
}

void WifiStaWatchdogPlugin::forceReassoc() {
    // The SDK's auto-reconnect path consults a fixed reason-code whitelist
    // (WiFiGeneric::_isReconnectableReason); on unlisted codes (UniFi vendor
    // 168, ASSOC_LEAVE, etc) it stops retrying entirely.  An explicit
    // disconnect+begin re-enters the connect path regardless of reason.
    wifi_ap_record_t ap{};
    const bool haveAp = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    ESP_LOGW(LOG_TAG, "STA down %lums; forcing reconnect (status=%d)", millis() - lastConnectedMs, (int)WiFi.status());
    if (haveAp) {
        ESP_LOGW(LOG_TAG, "  last AP: bssid=%02x:%02x:%02x:%02x:%02x:%02x rssi=%d ch=%u", ap.bssid[0], ap.bssid[1], ap.bssid[2],
                 ap.bssid[3], ap.bssid[4], ap.bssid[5], ap.rssi, ap.primary);
    }

    WiFi.disconnect(false); // reset state machine, keep stored wifi_config_t
    delay(50);
    WiFi.begin(ssid.c_str(), pass.c_str());
}
