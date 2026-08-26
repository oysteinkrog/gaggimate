#include "NetworkWatchdogPlugin.h"
#include "../core/Controller.h"
#include "../core/Event.h"
#include "../core/constants.h" // MODE_STANDBY
#include <WiFi.h>
#include <cerrno>
#include <esp_heap_caps.h>
#include <esp_log.h>

static constexpr char LOG_TAG[] = "NetWatchdog";

void NetworkWatchdogPlugin::setup(Controller *c, PluginManager *pluginManager) {
    this->controller = c;
    pluginManager->on("controller:wifi:connect", [this](Event const &) {
        _probe.begin(0);
        _socketReady = true;
        const unsigned long now = millis();
        _lastAlive = now;
        _lastProbe = now;
        _lastStats = now;
        _stage = 0;
        _busyGrace = 0;
        ESP_LOGI(LOG_TAG, "Watchdog started (wifi connected)");
    });
    pluginManager->on("controller:wifi:disconnect", [this](Event const &) {
        ESP_LOGI(LOG_TAG, "Watchdog stopped (wifi disconnected)");
        // _probe.stop();
        _socketReady = false;
    });
}

bool NetworkWatchdogPlugin::networkShouldBeUp() const {
    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
        return true;
    return WiFi.status() == WL_CONNECTED;
}

NetworkWatchdogPlugin::Probe NetworkWatchdogPlugin::probeEgress() {
    const wifi_mode_t mode = WiFi.getMode();
    const bool ap = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    IPAddress target = ap ? WiFi.softAPIP() : WiFi.gatewayIP();
    if (static_cast<uint32_t>(target) == 0)
        return Probe::Alive;
    if (_probe.beginPacket(target, PROBE_PORT) != 1)
        return Probe::Dead;
    _probe.write(static_cast<uint8_t>(0));
    errno = 0;
    if (_probe.endPacket() == 1)
        return Probe::Alive;
    // A failed send is not evidence of a wedged stack when the reason is that
    // there was no buffer to send from. WiFi's TX path draws 1630-byte cache
    // buffers out of the same DMA-capable internal DRAM the LCD bounce buffers
    // live in, and a browser pulling the whole UI in two tabs empties it for
    // seconds at a time; lwIP surfaces that as ERR_MEM, which arrives here as
    // ENOMEM. Treating it as a dead link is how a slow page load used to
    // become a WiFi reconnect that dropped every websocket and every request
    // in flight, turning a stall into an outage. Sends that fail this way mean
    // the stack is alive and busy, which is the opposite of the fault this
    // watchdog exists to catch.
    switch (errno) {
    case ENOMEM:
    case ENOBUFS:
    case EAGAIN:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
        return Probe::Busy;
    default:
        return Probe::Dead;
    }
}

void NetworkWatchdogPlugin::logStats(const char *reason) {
    const unsigned freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const unsigned minInt = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    const unsigned largestInt = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    // DMA-capable internal is reported separately because it is the pool that
    // actually fails first, and the plain internal figure hides that. WiFi's
    // frame buffers ask for MALLOC_CAP_INTERNAL|DMA|8BIT (caps 0x80c); measured
    // on this board, 178-to-333 byte requests were failing while the internal
    // total still read 8 KB free, because none of that 8 KB was DMA-capable.
    // Watching the wrong number is how that went undiagnosed.
    const unsigned freeDma = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    const unsigned minDma = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    const unsigned long sinceOk = millis() - _lastAlive;
    ESP_LOGI(LOG_TAG,
             "[%s] internal heap: free=%u min=%u largest=%u | dma: free=%u min=%u | egress ok %lus ago (stage %u, busy "
             "grace %lus)",
             reason, freeInt, minInt, largestInt, freeDma, minDma, sinceOk / 1000, _stage, _busyGrace / 1000);
}

bool NetworkWatchdogPlugin::rebootAllowed(unsigned long now) const {
    if (now < MIN_UPTIME_FOR_REBOOT)
        return false;
    if (_standbySince == 0)
        return false;
    if (now - _standbySince < MIN_STANDBY_FOR_REBOOT)
        return false;
    return true;
}

void NetworkWatchdogPlugin::recover(unsigned long now, unsigned long deadForMs) {
    if (_stage < 1) {
        _stage = 1;
        logStats("egress-dead");
        ESP_LOGE(LOG_TAG, "No network egress for %lus while WiFi up -> WiFi reconnect", deadForMs / 1000);
        const wifi_mode_t mode = WiFi.getMode();
        if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)
            WiFi.reconnect();
        return;
    }

    if (deadForMs < REBOOT_AFTER)
        return;

    if (!rebootAllowed(now)) {
        if (!_rebootHeld) {
            _rebootHeld = true;
            ESP_LOGW(LOG_TAG, "Network dead but deferring reboot (needs uptime > 15min and standby > 5min)");
        }
        return;
    }

    ESP_LOGE(LOG_TAG, "Network still dead after %lus and machine idle -> rebooting to reclaim the stack", deadForMs / 1000);
    logStats("egress-dead-reboot");
    delay(50);
    ESP.restart();
}

void NetworkWatchdogPlugin::loop() {
    const unsigned long now = millis();

    if (controller != nullptr && controller->getMode() == MODE_STANDBY) {
        if (_standbySince == 0)
            _standbySince = now;
    } else {
        _standbySince = 0;
    }

    if (!_socketReady || !networkShouldBeUp()) {
        _lastAlive = now;
        _stage = 0;
        _busyGrace = 0;
        _rebootHeld = false;
        return;
    }

    if (now - _lastStats >= STATS_PERIOD) {
        _lastStats = now;
        logStats("periodic");
    }

    if (now - _lastProbe >= PROBE_PERIOD) {
        _lastProbe = now;
        switch (probeEgress()) {
        case Probe::Alive:
            if (_stage != 0)
                ESP_LOGW(LOG_TAG, "Network egress recovered");
            _lastAlive = now;
            _stage = 0;
            _busyGrace = 0;
            _rebootHeld = false;
            break;
        case Probe::Busy:
            // Out of TX buffers, so this probe proved nothing either way. Hold
            // the recovery timer off for as long as the probe took, up to a
            // ceiling, rather than counting the interval as evidence of death.
            if (_busyGrace < BUSY_GRACE_MAX) {
                _busyGrace += PROBE_PERIOD;
                if (_busyGrace > BUSY_GRACE_MAX)
                    _busyGrace = BUSY_GRACE_MAX;
            }
            break;
        case Probe::Dead:
            break;
        }
    }

    const unsigned long since = now - _lastAlive;
    const unsigned long deadFor = since > _busyGrace ? since - _busyGrace : 0;
    if (deadFor >= DEAD_AFTER)
        recover(now, deadFor);
}
