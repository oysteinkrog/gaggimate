#include "ControllerOTA.h"
#include "common.h"
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFiClient.h>

namespace {
constexpr char UPDATE_FILE[] = "/board-firmware.bin";
// First byte of an ESP32 application image. The controller refuses anything
// else, and checking it locally is what stops a GitHub error page or a
// truncated body from ever reaching the BLE link.
constexpr uint8_t ESP_IMAGE_MAGIC = 0xE9;
} // namespace

void ControllerOTA::init(NimBLEClient *client, const ctr_progress_callback_t &progress_callback) {
    this->client = client;
    progressCallback = progress_callback;
    // The caller hands us whatever the transport currently has, and that is
    // null until a client object exists. Dereferencing it here panics the
    // whole board, so refuse rather than trust: controller OTA needs a live
    // link anyway and a later call re-runs this with a real client.
    if (client == nullptr) {
        ESP_LOGW("ControllerOTA", "init with no BLE client; controller OTA unavailable until the link is up");
        return;
    }
    NimBLERemoteService *pRemoteService = client->getService(NimBLEUUID(SERVICE_OTA_BLE_UUID));
    if (pRemoteService == nullptr) {
        ESP_LOGE("ControllerOTA", "OTA BLE service not found");
        return;
    }
    rxChar = pRemoteService->getCharacteristic(NimBLEUUID(CHARACTERISTIC_OTA_BL_UUID_RX));
    txChar = pRemoteService->getCharacteristic(NimBLEUUID(CHARACTERISTIC_OTA_BL_UUID_TX));
    if (txChar != nullptr && txChar->canNotify()) {
        txChar->subscribe(true, std::bind(&ControllerOTA::onReceive, this, std::placeholders::_1, std::placeholders::_2,
                                          std::placeholders::_3, std::placeholders::_4));
    }
}

bool ControllerOTA::update(WiFiClientSecure &wifi_client, const String &release_url) {
    // init() bails out without touching rxChar/client when there is no link,
    // and runUpdate() dereferences client on its very first loop condition, so
    // starting an update without one panicked the board. Refuse up front.
    if (client == nullptr || rxChar == nullptr || !client->isConnected()) {
        ESP_LOGE("ControllerOTA", "No controller BLE link; refusing to start an update");
        return false;
    }

    // Clear any result left over from a previous attempt so waitForInstallResult
    // can only observe this run's notification.
    installResultReceived = false;

    if (LittleFS.exists(UPDATE_FILE)) {
        ESP_LOGI("ControllerOTA", "Removing previous update file");
        LittleFS.remove(UPDATE_FILE);
    }
    // This used to log the failure and fall through. With the file just
    // removed above, LittleFS.open() then returned an invalid File whose
    // size() is 0, and runUpdate(file, 0) announced a zero-length image to the
    // controller and walked it into DFU with nothing to install.
    if (!downloadFile(wifi_client, release_url)) {
        ESP_LOGE("ControllerOTA", "Download of firmware file failed; not starting the transfer");
        LittleFS.remove(UPDATE_FILE);
        return false;
    }

    File file = LittleFS.open(UPDATE_FILE, FILE_READ);
    if (!file) {
        ESP_LOGE("ControllerOTA", "Downloaded firmware file could not be reopened");
        return false;
    }
    const uint32_t size = file.size();
    // Re-check the image magic from the file, not the socket. downloadFile
    // peeks at the stream before the first write; this proves the bytes that
    // actually landed on flash still start like a firmware image.
    const int firstByte = file.peek();
    if (size == 0 || firstByte != ESP_IMAGE_MAGIC) {
        ESP_LOGE("ControllerOTA", "Refusing to send: size=%u firstByte=0x%02x", size, firstByte & 0xFF);
        file.close();
        LittleFS.remove(UPDATE_FILE);
        return false;
    }

    const bool ok = runUpdate(file, size);
    file.close();
    return ok;
}

bool ControllerOTA::downloadFile(WiFiClientSecure &wifi_client, const String &release_url) {
    // The CA bundle attachment does not survive a prior HTTPClient begin/end
    // cycle on this same client (the caller resolves the redirect first, which
    // runs its own requests), so re-attach immediately before this begin() or
    // the handshake fails with -30336 "No CA Chain is set". release_url is
    // already the terminal single-host asset URL, so redirect-following is off:
    // if it ever redirects we want a loud failure, not a silently untrusted leg.
    attach_ca_bundle(wifi_client);
    HTTPClient http;
    if (!http.begin(wifi_client, release_url)) {
        ESP_LOGE("ControllerOTA", "Failed to start http client");
        return false;
    }

    http.useHTTP10(true);
    http.setTimeout(60000);
    http.setConnectTimeout(10000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setUserAgent("ESP32-http-Update");
    http.addHeader("Cache-Control", "no-cache");
    int code = http.GET();
    int len = http.getSize();

    if (code != HTTP_CODE_OK) {
        ESP_LOGE("ControllerOTA", "HTTP error: %d", code);
        http.end();
        return false;
    }

    // getSize() returns -1 when the response carries no Content-Length, which
    // the old `len == 0` test let through: the copy loop below never runs for a
    // negative length, so it produced an empty file and reported success.
    if (len <= 0) {
        ESP_LOGE("ControllerOTA", "Could not fetch firmware (content length %d)", len);
        http.end();
        return false;
    }

    WiFiClient *tcp = http.getStreamPtr();
    delay(100);

    if (tcp->peek() != ESP_IMAGE_MAGIC) {
        ESP_LOGE("ControllerOTA", "Magic header does not start with 0xE9");
        http.end();
        return false;
    }

    File file = LittleFS.open(UPDATE_FILE, FILE_WRITE, true);
    if (!file) {
        ESP_LOGE("ControllerOTA", "Could not open %s for writing", UPDATE_FILE);
        http.end();
        return false;
    }

    int written = 0;
    while (written < len) {
        int bufferSize = min(1024, len - written);
        uint8_t buffer[bufferSize];
        // fillBuffer used to give up after 30 s of silence and return void, so
        // a connection that dropped mid-download left the tail of this buffer
        // as uninitialised stack, wrote it anyway, and still reported success.
        // Nothing downstream checksums the file, so that garbage went straight
        // to the controller.
        if (!fillBuffer(*tcp, buffer, bufferSize)) {
            ESP_LOGE("ControllerOTA", "Download stalled after %d/%d bytes", written, len);
            file.close();
            LittleFS.remove(UPDATE_FILE);
            http.end();
            return false;
        }
        if (file.write(buffer, bufferSize) != static_cast<size_t>(bufferSize)) {
            ESP_LOGE("ControllerOTA", "Short write to %s at %d/%d bytes (filesystem full?)", UPDATE_FILE, written, len);
            file.close();
            LittleFS.remove(UPDATE_FILE);
            http.end();
            return false;
        }
        written += bufferSize;
        double progress = (static_cast<double>(written) / static_cast<double>(len)) * 50.0;
        progressCallback(static_cast<int>(progress));
    }
    ESP_LOGI("ControllerOTA", "Downloaded firmware file with %d bytes to %s", len, UPDATE_FILE);
    file.close();
    http.end();
    return true;
}

bool ControllerOTA::runUpdate(Stream &in, uint32_t size) {
    ESP_LOGI("ControllerOTA", "Sending update instructions over BLE. File Size: %u", size);
    fileParts = (size + PART_SIZE - 1) / PART_SIZE;
    currentPart = 0;
    if (fileParts == 0) {
        ESP_LOGE("ControllerOTA", "Nothing to send; refusing to put the controller into DFU");
        return false;
    }

    uint8_t fileLengthBytes[] = {
        0xFE,
        static_cast<uint8_t>((size >> 24) & 0xFF),
        static_cast<uint8_t>((size >> 16) & 0xFF),
        static_cast<uint8_t>((size >> 8) & 0xFF),
        static_cast<uint8_t>(size & 0xFF),
    };
    uint8_t partsAndMTU[] = {
        0xFF,
        static_cast<uint8_t>(fileParts / 256),
        static_cast<uint8_t>(fileParts % 256),
        static_cast<uint8_t>(MTU / 256),
        static_cast<uint8_t>(MTU % 256),
    };
    uint8_t updateStart[] = {0xFD};
    // 0xFD is what puts the controller into DFU. If either descriptor before it
    // failed to land, the board would enter DFU with the wrong idea of how big
    // the image is or how many parts are coming, so bail before sending it.
    if (!sendData(fileLengthBytes, 5) || !sendData(partsAndMTU, 5) || !sendData(updateStart, 1)) {
        ESP_LOGE("ControllerOTA", "Controller did not accept the update descriptors; not entering DFU");
        return false;
    }
    ESP_LOGI("ControllerOTA", "Waiting for signal from controller");

    bool acknowledged = false;
    // Whether the controller ended the transfer itself rather than the link
    // dropping under us. The two cases have to be told apart at the bottom.
    bool answered = false;
    // Every part of this loop is driven by notifications from the controller,
    // and nothing here bounded the wait: a board that accepted 0xFD and then
    // stopped answering -- crashed mid-erase, wedged in its bootloader --
    // leaves the link up, so this spins forever at 50 ms a turn. The display
    // stays stuck in PHASE_CONTROLLER_FW with the web UI spinner going and no
    // way out short of a power cycle. The controller requests the next part as
    // soon as it has written the previous one, so 60 s of total silence is far
    // outside normal pacing.
    uint32_t lastActivity = millis();
    while (client->isConnected()) {
        if (millis() - lastActivity > SIGNAL_TIMEOUT_MS) {
            ESP_LOGE("ControllerOTA", "Controller went silent after %u / %u parts; aborting", currentPart, fileParts);
            return false;
        }
        uint8_t signal = lastSignal;
        lastSignal = 0x00;
        if (signal != 0x00) {
            lastActivity = millis();
        }
        if (signal == 0xAA || signal == 0xF1) {
            // Start update or send next part. The part index is driven by
            // notifications from the controller, so a stuck or confused board
            // could ask for more parts than the image has -- and sendPart's
            // `totalSize - currentPart * PART_SIZE` underflows uint32_t past
            // the end, producing a multi-gigabyte read. Stop at the last part.
            if (currentPart >= fileParts) {
                ESP_LOGW("ControllerOTA", "Controller asked for part %u of %u; transfer already complete", currentPart + 1,
                         fileParts);
                break;
            }
            ESP_LOGV("ControllerOTA", "Sending part %u / %u", currentPart + 1, fileParts);
            if (!sendPart(in, size)) {
                ESP_LOGE("ControllerOTA", "Aborting transfer at part %u / %u: could not read the image", currentPart + 1,
                         fileParts);
                return false;
            }
            currentPart++;
            notifyUpdate();
        } else if (signal == 0xF2 || signal == 0xFF) {
            acknowledged = signal == 0xF2;
            answered = true;
            break;
        }
        delay(50);
    }
    // Success is 0xF2, the controller saying it is flashing what it received.
    // Delivering every part counts too, but only when the loop ended by the
    // link going away: the board can reboot into the new image the instant it
    // has the final footer, so requiring the ack alone would report a good
    // update as failed. An explicit terminal signal that is not 0xF2 is the
    // controller telling us it did not take the image, and that has to fail
    // even with every part across -- as do a dead link, a zero-length image,
    // and an abort partway through.
    if (!acknowledged && (answered || currentPart < fileParts)) {
        ESP_LOGE("ControllerOTA", "Transfer ended after %u / %u parts without an install acknowledgement", currentPart,
                 fileParts);
        return false;
    }
    ESP_LOGI("ControllerOTA", "Controller update finished");
    return true;
}

bool ControllerOTA::sendData(uint8_t *data, uint16_t len) const {
    if (rxChar == nullptr) {
        ESP_LOGE("ControllerOTA", "RX Char uninitialized");
        return false;
    }
    // These are writes-with-response, so a false return means the controller
    // did not acknowledge this packet. The old code discarded that: a header,
    // chunk or footer could fail to land while the part counter advanced
    // anyway, and the transfer still finished "successfully" with a hole in
    // the image the controller is about to flash.
    if (!rxChar->writeValue(data, len, true)) {
        ESP_LOGE("ControllerOTA", "BLE write of %u bytes was not acknowledged", len);
        return false;
    }
    delay(50);
    return true;
}

bool ControllerOTA::fillBuffer(Stream &in, uint8_t *buffer, uint16_t len) const {
    size_t bufferLen = 0;
    size_t bytesToRead = len;
    size_t toRead = 0;
    size_t timeout_failures = 0;
    while (bufferLen < len) {
        while (!toRead) {
            toRead = in.readBytes(buffer + bufferLen, bytesToRead);
            if (toRead == 0) {
                timeout_failures++;
                if (timeout_failures >= 300) {
                    ESP_LOGE("ControllerOTA", "Failed to read data from stream");
                    return false;
                }
                ESP_LOGW("ControllerOTA", "Failed to read data from stream. Request %d bytes", bytesToRead);
                delay(100);
            }
        }
        bufferLen += toRead;
        bytesToRead = len - bufferLen;
        toRead = 0;
    }
    ESP_LOGV("ControllerOTA", "Read %d bytes", bufferLen);
    return true;
}

void ControllerOTA::notifyUpdate() const {
    if (fileParts == 0) {
        return;
    }
    double progress = (static_cast<double>(currentPart) / static_cast<double>(fileParts)) * 50.0 + 50.0;
    progressCallback(static_cast<int>(progress));
}

bool ControllerOTA::sendPart(Stream &in, uint32_t totalSize) const {
    uint8_t partData[MTU + 2];
    uint8_t buffer[MTU];
    partData[0] = 0xFB;
    uint32_t partLength = PART_SIZE;
    if ((currentPart + 1) * PART_SIZE > totalSize) {
        partLength = totalSize - (currentPart * PART_SIZE);
    }
    uint8_t parts = partLength / MTU;
    for (uint8_t part = 0; part < parts; part++) {
        partData[1] = part;
        if (!fillBuffer(in, buffer, MTU)) {
            return false;
        }
        for (uint32_t i = 0; i < MTU; i++) {
            partData[i + 2] = buffer[i];
        }
        ESP_LOGV("ControllerOTA", "Sending part %u / %u - package %d / %d", currentPart + 1, fileParts, part + 1, parts);
        if (!sendData(partData, MTU + 2)) {
            return false;
        }
    }
    if (partLength % MTU > 0) {
        uint32_t remaining = partLength % MTU;
        uint8_t remainingData[remaining + 2];
        remainingData[0] = 0xFB;
        remainingData[1] = parts;
        if (!fillBuffer(in, buffer, remaining)) {
            return false;
        }
        for (uint32_t i = 0; i < remaining; i++) {
            remainingData[i + 2] = buffer[i];
        }
        if (!sendData(remainingData, remaining + 2)) {
            return false;
        }
    }
    uint8_t footer[5];
    footer[0] = 0xFC;
    footer[1] = partLength / 256;
    footer[2] = partLength % 256;
    footer[3] = currentPart / 256;
    footer[4] = currentPart % 256;
    return sendData(footer, sizeof(footer));
}

void ControllerOTA::onReceive(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify) {
    // A zero-length notification is legal on the wire and this read the first
    // byte unconditionally.
    if (pData == nullptr || length == 0) {
        ESP_LOGW("ControllerOTA", "Empty notification from controller");
        return;
    }
    lastSignal = pData[0];
    ESP_LOGI("ControllerOTA", "Received signal 0x%x", lastSignal);
    switch (lastSignal) {
    case 0xAA:
        ESP_LOGI("ControllerOTA", "Starting transfer, only slow mode supported as of yet");
        break;
    case 0xF1:
        ESP_LOGI("ControllerOTA", "Next part requested");
        break;
    case 0xF2:
        ESP_LOGI("ControllerOTA", "Controller installing firmware");
        break;
    case 0x0F:
        // The controller's async installer reports its Arduino Update result as
        // a 0x0F-prefixed ASCII string: "Written: x/y [z %]", "OTA Done:
        // Success!/Failed!", "Error #: N", or "Not enough space...". This used
        // to fall into the unlogged default case, hiding a failed install
        // behind the display's "update successful". Log it verbatim; it is the
        // only window we get into what the controller's flash actually did.
        ESP_LOGI("ControllerOTA", "Controller install result: %.*s", static_cast<int>(length - 1),
                 reinterpret_cast<const char *>(pData + 1));
        installResultReceived = true;
        break;
    default:
        ESP_LOGI("ControllerOTA", "Unhandled message (0x%02x, %u bytes)", lastSignal, static_cast<unsigned>(length));
        break;
    }
}

bool ControllerOTA::waitForInstallResult(uint32_t timeoutMs) {
    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (installResultReceived) {
            return true;
        }
        // The controller reboots itself a few seconds after flashing, which
        // drops the link. If that happens before a result notification lands we
        // are not going to get one, so stop waiting.
        if (client == nullptr || !client->isConnected()) {
            ESP_LOGW("ControllerOTA", "Controller link dropped before an install result arrived");
            return false;
        }
        delay(100);
    }
    ESP_LOGW("ControllerOTA", "No install result from controller within %u ms", timeoutMs);
    return false;
}
