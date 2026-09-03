#include "BleClientTransport.h"

void BleClientTransport::init(const String &deviceName) {
    NimBLEDevice::init(deviceName.c_str());
    // +9 dBm — preserves master's setPower(ESP_PWR_LVL_P9), which was +9 dBm
    // under NimBLE 1.4.x. 2.x setPower takes int8_t dBm, so pass 9 directly
    // (2.x quantizes 9 -> ESP_PWR_LVL_P9). Do NOT pass the enum: ESP_PWR_LVL_P9
    // is value 11 on ESP32-S3 and would round up to +12 dBm.
    NimBLEDevice::setPower(9);
    NimBLEDevice::setMTU(256);
    _client = NimBLEDevice::createClient();
    _scanner = NimBLEDevice::getScan();
    if (_client == nullptr) {
        ESP_LOGE(LOG_TAG, "Failed to create BLE client");
        return;
    }
    _client->setClientCallbacks(this);
    scan();
}

void BleClientTransport::scan() {
    _readyForConnection = false;
    _scanner->clearResults(); // esp-nimble-cpp 2.x has no clearDuplicateCache(); results vector is unused (setMaxResults(0))
    // 1.x setAdvertisedDeviceCallbacks(cb, wantDuplicates=true) -> 2.x
    // setScanCallbacks(cb, wantDuplicates). wantDuplicates=true keeps duplicate
    // adverts flowing (internally setDuplicateFilter(false)) -- same as the explicit
    // setDuplicateFilter(false) below, preserving the pre-2.x rediscovery behaviour.
    _scanner->setScanCallbacks(this, true);
    // Every scan window open/close forces a Wi-Fi/BLE coex switch, and on the
    // RGB-panel display each switch can stall the MSPI bus long enough to
    // starve the panel's bounce-buffer refill (~0.55 displaced bands per
    // second measured at a 1000 ms interval, 0.19/s at 5000 ms). Boost at
    // 1000 ms only while discovery is likely imminent -- scan() entry means
    // boot or a fresh disconnect -- then maintain() backs off to 5000 ms.
    // Passive scan with a 50 ms window catches typical 100-200 ms adverts
    // within a few windows, so backed-off discovery still lands in seconds.
    _scanStartedMs = millis();
    _scanBackedOff = false;
    _scanner->setInterval(SCAN_BOOST_INTERVAL_MS);
    _scanner->setWindow(SCAN_WINDOW_MS);
    _scanner->setMaxResults(0);
    _scanner->setDuplicateFilter(false);
    _scanner->setActiveScan(false);
    _scanner->start(0, false, false); // 2.x: start(duration=0 continuous, isContinue, restart)
}

void BleClientTransport::maintain() {
    if (_client == nullptr || _scanner == nullptr)
        return; // init() failed to create the client/scanner
    if (!_readyForConnection && !_client->isConnected() && !_scanner->isScanning()) {
        // Restart in place, keeping the current interval. Scans stall every
        // minute or two when coexistence aborts them; a stall says nothing
        // about whether a controller is near, and going through scan() here
        // re-entered boost each time, which held the radio at the boost duty
        // (and its display cost) nearly continuously on a bench with no
        // controller. Parameters survive in the scanner object, so start()
        // alone resumes; fall back to a full scan() only if it refuses.
        ESP_LOGI(LOG_TAG, "Scan stalled, restarting");
        if (!_scanner->start(0, false, false)) {
            scan();
        }
        return;
    }
    // Back off a long-running fruitless scan (rationale at scan()). Inline
    // stop/start rather than scan(), which would reset the boost clock; the
    // early return above keeps the stall-restart path from seeing the brief
    // not-scanning gap this creates.
    if (!_scanBackedOff && _scanner->isScanning() && millis() - _scanStartedMs >= SCAN_BOOST_MS) {
        _scanBackedOff = true;
        _scanner->stop();
        _scanner->setInterval(SCAN_BACKOFF_INTERVAL_MS);
        _scanner->start(0, false, false);
        ESP_LOGI(LOG_TAG, "No controller in %us, scan backing off to %u ms interval",
                 (unsigned)(SCAN_BOOST_MS / 1000), (unsigned)SCAN_BACKOFF_INTERVAL_MS);
    }
}

bool BleClientTransport::connectToServer() {
    if (!_haveServerAddress)
        return false;

    ESP_LOGI(LOG_TAG, "Connecting to advertised device: %s", _serverAddress.toString().c_str());
    unsigned int tries = 0;
    do {
        if (tries >= MAX_CONNECT_RETRIES) {
            ESP_LOGE(LOG_TAG, "Connection timeout, rescanning");
            scan();
            return false;
        }
        if (!_client->connect(_serverAddress)) {
            int error = _client->getLastError();
            ESP_LOGW(LOG_TAG, "Connect failed: %d, retrying", error);
            delay(500);
        }
        tries++;
    } while (!_client->isConnected());
    applyConnParams(); // baseline for the new connection (idle unless set active)

    NimBLERemoteService *service = _client->getService(NimBLEUUID(gm_proto::SERVICE_UUID));
    if (service == nullptr) {
        ESP_LOGE(LOG_TAG, "Service not found");
        _client->disconnect();
        scan();
        return false;
    }

    _writeChar = service->getCharacteristic(NimBLEUUID(gm_proto::RX_CHAR_UUID));
    _notifyChar = service->getCharacteristic(NimBLEUUID(gm_proto::TX_CHAR_UUID));
    if (_writeChar == nullptr || _notifyChar == nullptr) {
        // The controller advertises the GaggiMate service but lacks the framed
        // comms characteristics -> old/incompatible firmware. Keep the link up
        // (the OTA service lives on a separate service and stays reachable) and
        // report incompatibility so the display can offer an OTA recovery, the
        // same way it handles a protocol-version mismatch.
        ESP_LOGW(LOG_TAG, "Comms characteristics missing -- incompatible controller firmware (OTA only)");
        _writeChar = nullptr;
        _notifyChar = nullptr;
        _readyForConnection = false;
        _incompatible = true;
        // Read the legacy read-only INFO characteristic (present on old
        // controllers too) so the display can show the real hardware/version.
        String info;
        NimBLERemoteCharacteristic *infoChar = service->getCharacteristic(NimBLEUUID(gm_proto::INFO_CHAR_UUID));
        if (infoChar != nullptr && infoChar->canRead())
            info = String(infoChar->readValue().c_str());
        if (_onIncompatible)
            _onIncompatible(info);
        return true; // link intentionally kept; do not disconnect/rescan
    }

    // Without the notify subscription we would connect but never receive data;
    // treat a failed subscribe as a failed connection.
    if (!_notifyChar->canNotify() ||
        !_notifyChar->subscribe(true, std::bind(&BleClientTransport::notifyCallback, this, std::placeholders::_1,
                                                std::placeholders::_2, std::placeholders::_3, std::placeholders::_4))) {
        ESP_LOGE(LOG_TAG, "Failed to subscribe to TX characteristic");
        _client->disconnect();
        scan();
        return false;
    }

    _readyForConnection = false;
    _incompatible = false;
    ESP_LOGI(LOG_TAG, "Connected, MTU: %d", _client->getMTU());
    emitConnection(true);
    return true;
}

void BleClientTransport::disconnect() {
    _readyForConnection = false;
    _haveServerAddress = false;
    if (_client && _client->isConnected())
        _client->disconnect();
}

void BleClientTransport::setLowLatency(bool active) {
    _lowLatency = active;
    applyConnParams();
}

void BleClientTransport::setIdleInterval(uint16_t mn, uint16_t mx) {
    // mn==0 clears the override (both fields), so idleMin/MaxInterval() fall
    // back to the compiled IDLE_*_INTERVAL. applyConnParams() only re-issues
    // the update while idle (not mid-shot) and connected; harmless otherwise.
    _idleMinOverride = mn;
    _idleMaxOverride = mn ? mx : 0;
    applyConnParams();
}

void BleClientTransport::applyConnParams() {
    if (_client == nullptr || !_client->isConnected())
        return;
    if (_lowLatency)
        _client->updateConnParams(ACTIVE_MIN_INTERVAL, ACTIVE_MAX_INTERVAL, CONN_LATENCY, CONN_TIMEOUT);
    else
        _client->updateConnParams(idleMinInterval(), idleMaxInterval(), CONN_LATENCY, CONN_TIMEOUT);
}

bool BleClientTransport::send(const uint8_t *data, size_t length) {
    if (!isConnected() || _writeChar == nullptr || data == nullptr || length == 0)
        return false;
    return _writeChar->writeValue(data, length, false); // write without response
}

bool BleClientTransport::isConnected() const { return _client != nullptr && _client->isConnected(); }

void BleClientTransport::onResult(const NimBLEAdvertisedDevice *advertisedDevice) {
    if (!advertisedDevice->haveServiceUUID())
        return;
    if (advertisedDevice->isAdvertisingService(NimBLEUUID(gm_proto::SERVICE_UUID))) {
        // Copy everything we need off advertisedDevice BEFORE stopping the scan.
        // With setMaxResults(0), NimBLEScan::stop() calls clearResults(), which
        // deletes the very advertisedDevice handed to this callback -- so reading
        // it after stop() is a use-after-free that returns a garbage peer address
        // (the connect then fails with BLE_HS_EINVAL).
        _serverAddress = advertisedDevice->getAddress();
        _haveServerAddress = true;
        ESP_LOGI(LOG_TAG, "Found controller at address %s with name %s, ready to connect", _serverAddress.toString().c_str(),
                 advertisedDevice->getName().c_str());
        _scanner->stop();
        _readyForConnection = true;
    }
}

void BleClientTransport::onDisconnect(NimBLEClient *, int) {
    ESP_LOGI(LOG_TAG, "Disconnected, will rescan");
    _writeChar = nullptr;
    _notifyChar = nullptr;
    _incompatible = false;
    emitConnection(false);
    scan();
}

void BleClientTransport::notifyCallback(NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool) {
    emitData(data, length);
}
