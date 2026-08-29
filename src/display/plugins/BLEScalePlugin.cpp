#include "BLEScalePlugin.h"
#include "remote_scales.h"
#include "remote_scales_plugin_registry.h"
#include <cmath> // For isfinite()
#include <display/core/Controller.h>
#include <scales/acaia.h>
#include <scales/bookoo.h>
#include <scales/decent.h>
#include <scales/difluid.h>
#include <scales/dot.h>
#include <scales/eclair.h>
#include <scales/eureka.h>
#include <scales/felicitaScale.h>
#include <scales/myscale.h>
#include <scales/timemore.h>
#include <scales/varia.h>
#include <scales/weighmybru.h>

// Scan cadence, read by the vendored scanner each time an async scan starts
// (scripts/patch_ble_scan_duty.py wires that up). Two cadences exist because
// every scan window TRANSITION stalls the display: the BT controller runs from
// flash, flash and PSRAM share the MSPI controller, and opening or closing the
// receiver costs the RGB panel's PSRAM bounce refill its deadline. The cost is
// per transition, not per millisecond the receiver is open; that was measured,
// not reasoned (the table is in scripts/patch_ble_scan_duty.py).
//
// There is only one cadence: 80 ms window every 2000 ms. A transition every
// 2 s, discovery within a few windows, tolerable to the panel and to WiFi
// coexistence. What varies is WHEN a scan runs at all. scan() holds one
// continuously for SCAN_BOOST_MS after power-on, mode change, an explicit
// scan from the web UI, or a lost connection: every moment a user is actually
// waiting for a scale to appear. After a boosted minute finds nothing, the
// scale is off or absent, and update() drops to SCAN_BURST_LEN_MS of this
// same cadence every SCAN_BURST_PERIOD_MS: ~7 window transitions per period
// instead of 45, a 6x cut in how often the receiver disturbs the panel, for
// a worst-case discovery latency of period plus burst. Scanning stops
// entirely once a scale connects, and in standby; a paired, present scale
// costs nothing at all.
//
// A high-duty burst (window == interval, receiver held open) was tried first
// and measured WORSE than scanning all day: 0.79/s panel resyncs against
// 0.5/s. With the receiver held open, WiFi coexistence preempts BLE in tens-
// of-milliseconds timeslices, and every preemption is a radio transition
// with the same MSPI cost as a window boundary -- one per ~50 ms for the
// whole burst. Short windows at a long interval are the shape coexistence
// can schedule around; that is not a tunable, it is the mechanism.
extern "C" {
uint16_t gm_ble_scan_interval_ms = 2000;
uint16_t gm_ble_scan_window_ms = 80;
}

void on_ble_measurement(float value) {
    if (&BLEScales != nullptr) {
        BLEScales.onMeasurement(value);
    }
}

BLEScalePlugin BLEScales;

BLEScalePlugin::BLEScalePlugin() = default;

BLEScalePlugin::~BLEScalePlugin() noexcept {
    try {
        // Disable active flag first to stop processing
        active = false;

        // Give any running callbacks time to complete
        delay(100);

        // Ensure proper cleanup
        disconnect();

        if (scanner != nullptr) {
            // Stop scanning first
            scanner->stopAsyncScan();
            // Give it time to actually stop
            delay(50);
            delete scanner;
            scanner = nullptr;
        }
    } catch (...) {
        // Swallow: destructors must not propagate exceptions.
        // NimBLE + Arduino delay() calls don't throw in practice; belt-and-braces.
    }
}

void BLEScalePlugin::setup(Controller *controller, PluginManager *manager) {
    if (controller == nullptr || manager == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Invalid controller or manager passed to setup");
        return;
    }

    this->controller = controller;
    this->pluginManager = manager;
    this->pluginRegistry = RemoteScalesPluginRegistry::getInstance();

    // Apply scale plugins with error checking
    AcaiaScalesPlugin::apply();
    BookooScalesPlugin::apply();
    DecentScalesPlugin::apply();
    DifluidScalesPlugin::apply();
    EclairScalesPlugin::apply();
    EurekaScalesPlugin::apply();
    FelicitaScalePlugin::apply();
    TimemoreScalesPlugin::apply();
    VariaScalesPlugin::apply();
    WeighMyBrewScalePlugin::apply();
    myscalePlugin::apply();
    TimemoreDotScalesPlugin::apply();

    // Initialize scanner with error handling
    this->scanner = new (std::nothrow) RemoteScalesScanner();
    if (this->scanner == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Failed to create RemoteScalesScanner - out of memory");
        return;
    }

    // Both auto-scan paths below only fire when a scale is actually saved.
    // Auto-scan exists to RECONNECT a known scale; with none saved it was
    // pure cost with no possible payoff, and the cost is not small: every
    // wake-from-standby and every mode change re-armed a 60 s scan boost,
    // whose RX buffers eat ~3 KB of internal heap and enough DMA that the
    // web server times out serving the frontend and mDNS UDP sends fail
    // with ENOMEM for minutes at a time ("web UI stops working whenever I
    // touch the machine"). Discovery for pairing still works: the web UI's
    // scan endpoint calls scan() directly, unguarded.
    manager->on("controller:bluetooth:connect", [this](Event const &) {
        if (this->controller != nullptr && this->controller->getMode() != MODE_STANDBY &&
            this->controller->getSettings().getSavedScale() != "") {
            ESP_LOGI("BLEScalePlugin", "Resuming scanning");
            scan();
            active = true;
        }
    });
    manager->on("controller:bluetooth:disconnect", [this](Event const &) {
        ESP_LOGW("BLEScalePlugin", "Controller disconnected, stopping BLE scan");
        active = false;
    });
    manager->on("controller:brew:prestart", [this](Event const &) { onProcessStart(); });
    manager->on("controller:brew:end", [this](Event const &) {
        if (scale != nullptr && scale->isConnected() && scale->hasTimerControl()) {
            scale->stopTimer();
        }
    });
    manager->on("controller:grind:start", [this](Event const &) { onProcessStart(); });
    manager->on("controller:mode:change", [this](Event const &event) {
        if (event.getInt("value") != MODE_STANDBY) {
            if (this->controller != nullptr && this->controller->getSettings().getSavedScale() != "") {
                ESP_LOGI("BLEScalePlugin", "Resuming scanning");
                scan();
                active = true;
            }
        } else {
            active = false;
        }
    });
}

void BLEScalePlugin::loop() {
    if (doConnect && scale == nullptr) {
        const unsigned long now = millis();
        if (lastConnectAttempt == 0 || now - lastConnectAttempt >= CONNECT_RETRY_INTERVAL_MS) {
            lastConnectAttempt = now;
            establishConnection();
        }
    }
    if (!active) {
        if (scale != nullptr) {
            disconnect();
        }
        if (scanner->isScanRunning()) {
            scanner->stopAsyncScan();
        }
    }
    const unsigned long now = millis();
    if (now - lastUpdate > UPDATE_INTERVAL_MS) {
        lastUpdate = now;
        update();
    }
}

void BLEScalePlugin::update() {
    // Graceful failure - if controller is null, just disable ourselves
    if (controller == nullptr) {
        ESP_LOGW("BLEScalePlugin", "Controller is null, disabling BLE scale");
        active = false;
        return;
    }

    bool hasConnectedScale = false;
    if (scale != nullptr) {
        // Check if scale pointer is valid before accessing
        hasConnectedScale = scale->isConnected();
    }

    if (!active)
        return;

    if (scale != nullptr) {
        // Call scale update with error checking
        scale->update();
        if (!hasConnectedScale) {
            reconnectionTries++;
            if (reconnectionTries > RECONNECTION_TRIES) {
                ESP_LOGW("BLEScalePlugin", "Max reconnection attempts reached, disconnecting");
                disconnect();
                scan();
            }
        } else {
            // Poll slow-changing metadata (battery, unit). Flow rate is
            // emitted inline with each weight measurement, not polled here.
            pollScaleMetadata();
        }
    } else if (controller->getSettings().getSavedScale() != "" && scanner != nullptr) {
        // Protected scanner access with null checks
        auto discoveredScales = scanner->getDiscoveredScales();
        for (const auto &d : discoveredScales) {
            if (d.getAddress().toString() == controller->getSettings().getSavedScale().c_str()) {
                ESP_LOGI("BLEScalePlugin", "Connecting to last known scale");
                connect(d.getAddress().toString());
                break;
            }
        }
    }

    // Scan phase scheduler: while nothing is connected or connecting, run the
    // boost phase out and then keep discovery alive as short high-duty bursts
    // (the cadence rationale sits above the gm_ble_scan_* definitions). All
    // comparisons are wrap-safe deltas; this runs on update()'s 1 s tick, so
    // every deadline lands within a second of its nominal time.
    if (scale == nullptr && !doConnect && scanner != nullptr && NimBLEDevice::isInitialized()) {
        const unsigned long now = millis();
        if (static_cast<long>(now - scanBoostUntil) < 0) {
            // Boost phase: scan() already started the pairing-cadence scan.
        } else if (scanner->isScanRunning()) {
            if (scanBurstStopAt == 0) {
                // The boost expired with its scan still running: stop it and
                // schedule the first burst a period out. The boost minute
                // itself was continuous discovery, so there is nothing to
                // gain from a burst right away.
                scanner->stopAsyncScan();
                scanNextBurstAt = now + SCAN_BURST_PERIOD_MS;
            } else if (static_cast<long>(now - scanBurstStopAt) >= 0) {
                scanner->stopAsyncScan();
                scanBurstStopAt = 0;
            }
        } else if (static_cast<long>(now - scanNextBurstAt) >= 0) {
            scanner->initializeAsyncScan();
            scanBurstStopAt = now + SCAN_BURST_LEN_MS;
            scanNextBurstAt = now + SCAN_BURST_PERIOD_MS;
        }
    }
}

void BLEScalePlugin::connect(const std::string &uuid) {
    if (uuid.empty()) {
        ESP_LOGE("BLEScalePlugin", "Cannot connect with empty UUID");
        return;
    }
    if (controller == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Controller is null, cannot save scale setting");
        return;
    }

    doConnect = true;
    this->uuid = uuid;
    controller->getSettings().setSavedScale(uuid.data());
}

void BLEScalePlugin::scan() const {
    if (scale != nullptr && scale->isConnected()) {
        return;
    }
    if (scanner == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Scanner not initialized, cannot start scan");
        return;
    }
    // The host stack may never have been brought up. Controller::connect()
    // skips comms.init(), and therefore NimBLEDevice::init(), on the builds that
    // synthesize the controller handshake instead of talking to a real board.
    // The scan path does not survive that: initializeAsyncScan() goes straight
    // to NimBLEDevice::getScan(), whose constructor initialises a callout
    // against host structures that do not exist yet, and the load faults on a
    // null pointer. It reached users as "touching the screen reboots it",
    // because leaving standby raises controller:mode:change, which lands here.
    //
    // Guarding at this end rather than at that one event covers every caller,
    // including the web UI's scan button.
    if (!NimBLEDevice::isInitialized()) {
        ESP_LOGW("BLEScalePlugin", "BLE host not initialized, skipping scale scan");
        return;
    }
    // Every caller of scan() is a moment someone may be waiting for a scale:
    // leaving standby, the controller link coming up, the web UI's scan
    // button, a lost connection. Enter the boost phase; update() drops to
    // bursts if a minute of this finds nothing. A burst already in flight
    // just keeps running (initializeAsyncScan() is a no-op then, and it is
    // the same cadence); the boost extension alone is what matters.
    scanBoostUntil = millis() + SCAN_BOOST_MS;
    scanBurstStopAt = 0;
    // Arm discovery here rather than relying on the callers: the auto paths
    // only reach this with a saved scale (see begin()), but the web UI's
    // scan button must also survive loop()'s !active scan-kill when no scale
    // was ever saved — that is the first-pairing case. Standby keeps its old
    // semantics (the scan is stopped on the next loop pass).
    if (controller != nullptr && controller->getMode() != MODE_STANDBY) {
        active = true;
    }
    scanner->initializeAsyncScan();
}

void BLEScalePlugin::disconnect() {
    if (scale != nullptr) {
        // Add small delay to let any pending callbacks complete
        delay(50);

        // Check if scale is still valid before calling disconnect
        if (scale) {
            scale->disconnect();
        }

        scale = nullptr;
        uuid = "";
        doConnect = false;
        reconnectionTries = 0;
        // Reset metadata caches so we re-emit change events when a new scale
        // connects (possibly a different model with different capabilities).
        lastBatteryLevel = REMOTE_SCALES_BATTERY_UNKNOWN;
        lastWeightUnit = ScaleWeightUnit::UNKNOWN;
        warnedOunceMidBrew = false;
    }
}

void BLEScalePlugin::onProcessStart() const {
    if (scale != nullptr && scale->isConnected()) {
        // Double tare with validation
        scale->tare();
        delay(50);

        // Check if scale is still connected before second tare
        if (scale != nullptr && scale->isConnected()) {
            scale->tare();
        }
    }
}

void BLEScalePlugin::pollScaleMetadata() {
    if (scale == nullptr || !scale->isConnected() || pluginManager == nullptr) {
        return;
    }
    auto *pm = pluginManager;

    // Battery % -- fire event only on change so consumers can subscribe without
    // being hammered at 1 Hz with duplicate values.
    if (scale->hasBatteryLevel()) {
        const uint8_t pct = scale->getBatteryLevel();
        if (pct != lastBatteryLevel && pct != REMOTE_SCALES_BATTERY_UNKNOWN) {
            lastBatteryLevel = pct;
            pm->trigger("scale:battery:change", "value", static_cast<int>(pct));
        }
    }
}

void BLEScalePlugin::tare() const { onProcessStart(); }

void BLEScalePlugin::establishConnection() {
    if (uuid.empty()) {
        ESP_LOGE("BLEScalePlugin", "Cannot establish connection with empty UUID");
        return;
    }

    ESP_LOGI("BLEScalePlugin", "Connecting to %s", uuid.c_str());
    if (scanner == nullptr) {
        ESP_LOGE("BLEScalePlugin", "Scanner not initialized, cannot establish connection");
        return;
    }

    scanner->stopAsyncScan();

    auto discoveredScales = scanner->getDiscoveredScales();
    bool deviceFound = false;

    for (const auto &d : discoveredScales) {
        if (d.getAddress().toString() == uuid) {
            deviceFound = true;
            reconnectionTries = 0;

            auto factory = RemoteScalesFactory::getInstance();
            if (factory == nullptr) {
                ESP_LOGE("BLEScalePlugin", "RemoteScalesFactory instance is null");
                return;
            }

            scale = factory->create(d);
            if (!scale) {
                ESP_LOGE("BLEScalePlugin", "Connection to device %s failed", d.getName().c_str());
                return;
            }

            scale->setLogCallback([](std::string message) {
                if (!message.empty()) {
                    Serial.print(message.c_str());
                }
            });

            scale->setWeightUpdatedCallback([](float weight) {
                // Check if we're in an ISR context
                if (xPortInIsrContext()) {
                    // Skip measurement to avoid FreeRTOS deadlocks from interrupt context
                    return;
                }
                // Safe to call directly from task context with null check
                if (&BLEScales != nullptr) {
                    BLEScales.onMeasurement(weight);
                }
            });

            bool connectResult = scale->connect();
            if (!connectResult) {
                ESP_LOGW("BLEScalePlugin", "Failed to connect to scale, retrying scan");
                disconnect();
                scan();
            }
            break;
        }
    }

    if (!deviceFound) {
        ESP_LOGW("BLEScalePlugin", "Device %s not found in discovered scales", uuid.c_str());
        scan();
    }
}

void BLEScalePlugin::onMeasurement(float value) const {
    // Rate limiting to prevent callback flooding
    unsigned long now = millis();
    if (now - lastMeasurementTime < MIN_MEASUREMENT_INTERVAL_MS) {
        return; // Drop measurement to prevent flooding
    }
    lastMeasurementTime = now;

    // Multiple safety checks to prevent crashes
    if (controller == nullptr) {
        return; // Silently ignore if controller is null
    }

    // Check if we're being destroyed or in an unsafe state
    if (!active) {
        return; // Don't process measurements when not active
    }

    // Validate the measurement value
    if (!isfinite(value) || value < -1000.0f || value > 10000.0f) {
        ESP_LOGW("BLEScalePlugin", "Invalid measurement value: %f, ignoring", value);
        return;
    }

    // Safe to call controller method
    controller->onVolumetricMeasurement(value, VolumetricMeasurementSource::BLUETOOTH);

    // If the scale driver also provides native flow rate (e.g. Bookoo), emit
    // it on the same tick so consumers get it at the scale's native cadence
    // (~10 Hz) without having to poll. Controller.onVolumetricMeasurement
    // updates lastBluetoothMeasurement timestamps as a side effect; we reuse
    // a lighter path here since flow is not gating shot state.
    if (scale != nullptr && scale->hasFlowRate() && pluginManager != nullptr) {
        pluginManager->trigger("controller:volumetric-measurement:scale-flow:change", "value", scale->getFlowRate());
    }
}

std::vector<DiscoveredDevice> BLEScalePlugin::getDiscoveredScales() const {
    if (scanner == nullptr) {
        ESP_LOGW("BLEScalePlugin", "Scanner not initialized, returning empty device list");
        return std::vector<DiscoveredDevice>();
    }
    return scanner->getDiscoveredScales();
}
