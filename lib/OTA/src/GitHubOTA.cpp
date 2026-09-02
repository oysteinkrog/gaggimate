#include "GitHubOTA.h"
#include "common.h"
#include "semver_extensions.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>

GitHubOTA::GitHubOTA(const String &display_version, const String &controller_version, const String &release_url,
                     const phase_callback_t &phase_callback, const progress_callback_t &progress_callback,
                     const String &firmware_name, const String &filesystem_name, const String &controller_firmware_name) {
    ESP_LOGV("GitHubOTA", "GitHubOTA(display_version: %s, controller_version: %s, firmware_name: %s)\n", display_version.c_str(),
             controller_version.c_str(), firmware_name.c_str());

    _version = from_string(display_version.substring(1).c_str());
    _controller_version = from_string(controller_version.substring(1).c_str());
    _release_url = release_url;
    _firmware_name = firmware_name;
    _filesystem_name = filesystem_name;
    _controller_firmware_name = controller_firmware_name;
    _phase_callback = phase_callback;
    _progress_callback = progress_callback;

    Updater.rebootOnUpdate(false);
    Updater.onStart(update_started);
    Updater.onEnd(update_finished);
    Updater.onProgress([progress_callback, this](int bytesReceived, int totalBytes) {
        int percentage = 100.0 * bytesReceived / totalBytes;
        progress_callback(phase, percentage);
        ESP_LOGV("update_progress", "Data received, Progress: %d %%\r", percentage);
    });
    Updater.onError(update_error);
    Updater.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
}

void GitHubOTA::init(NimBLEClient *client) {
    _controller_ota.init(client, [this](int progress) { _progress_callback(PHASE_CONTROLLER_FW, progress); });
}

void GitHubOTA::checkForUpdates() {
    const char *TAG = "checkForUpdates";

    _latest_url = get_updated_base_url_via_redirect(_wifi_client, _release_url);
    if (_latest_url != "") {
        ESP_LOGI(TAG, "base_url %s\n", _latest_url.c_str());

        auto last_slash = _latest_url.lastIndexOf('/', _latest_url.length() - 2);
        auto semver_str = _latest_url.substring(last_slash + 1);
        semver_str.replace("/", "");
        if (semver_str.substring(0, 1) != "v") {
            ESP_LOGW(TAG, "not a valid version URL");
            return;
        }
        semver_str = semver_str.substring(1);
        ESP_LOGI(TAG, "semver_str %s\n", semver_str.c_str());
        _latest_version_string = semver_str;
        semver_free(&_latest_version);
        _latest_version = from_string(semver_str.c_str());
    } else {
        _latest_url = _release_url + "/";
        _latest_url.replace("tag", "download");
        String version = get_updated_version_via_txt_file(_wifi_client, _latest_url);

        if (version.length() == 0) {
            ESP_LOGW(TAG, "version.txt did not return a valid version string");
            return;
        }

        version = version.substring(1);
        _latest_version_string = version;
        semver_free(&_latest_version);
        _latest_version = from_string(version.c_str());
    }
}

String GitHubOTA::getCurrentVersion() const { return _latest_version_string; }

bool GitHubOTA::isUpdateAvailable(bool controller) const {
    if (controller) {
        return update_required(_latest_version, _controller_version);
    }
    return update_required(_latest_version, _version);
}

void GitHubOTA::update(bool controller, bool display) {
    const char *TAG = "update";

    bool updateExecuted = false;

    if (controller && update_required(_latest_version, _controller_version)) {
        ESP_LOGI(TAG, "Controller update is required, running firmware update.");
        this->phase = PHASE_CONTROLLER_FW;
        this->_phase_callback(PHASE_CONTROLLER_FW);
        // ControllerOTA::update() does not attach the CA bundle itself; do it
        // here like update_firmware/update_filesystem, else the HTTPS GET for the
        // controller .bin hits GitHub with no trusted roots and the TLS handshake
        // fails (the constructor no longer attaches it once for the client).
        attach_ca_bundle(_wifi_client);
        // Resolve the github.com -> release-assets.githubusercontent.com hop up
        // front: downloadFile() follows redirects with HTTPClient, which drops
        // the CA bundle across hosts (-30336). Handing it the terminal
        // single-host URL keeps the one GET it makes on a trusted leg.
        const String controller_url = resolve_redirect_chain(_wifi_client, _latest_url + _controller_firmware_name);
        if (controller_url.length() == 0) {
            ESP_LOGE(TAG, "Could not resolve controller firmware URL; aborting update.");
            this->phase = PHASE_ERROR;
            this->_phase_callback(PHASE_ERROR);
            return;
        }
        // This used to ignore the result and log "Controller update successful"
        // unconditionally, then fall through to reboot the display -- so a
        // controller that never received the image reported a clean update.
        if (!_controller_ota.update(_wifi_client, controller_url)) {
            ESP_LOGE(TAG, "Controller update failed; not touching the display image.");
            this->phase = PHASE_ERROR;
            this->_phase_callback(PHASE_ERROR);
            return;
        }
        ESP_LOGI(TAG, "Controller received the image; waiting for its install result.");
        // update() returns as soon as the controller acks receipt (0xF2), but
        // the controller flashes the image asynchronously afterwards and only
        // then notifies whether Update.end() actually succeeded. The display
        // used to reboot ~1 s later (updateExecuted -> restart below), tearing
        // down BLE before that result could arrive, so a failed install looked
        // identical to a good one. Hold a bounded window for the result to land
        // and be logged. This is diagnostic only: a timeout does not change the
        // outcome, and the display still reboots to reconnect either way.
        _controller_ota.waitForInstallResult(25000);
        ESP_LOGI(TAG, "Controller update sequence finished.");
        updateExecuted = true;
    }

    if (display && update_required(_latest_version, _version)) {
        ESP_LOGI(TAG, "Update is required, running firmware update.");
        this->phase = PHASE_DISPLAY_FW;
        this->_phase_callback(PHASE_DISPLAY_FW);
        auto result = update_firmware(_latest_url + _firmware_name);

        if (result != HTTP_UPDATE_OK) {
            ESP_LOGE(TAG, "Update failed: %s\n", Updater.getLastErrorString().c_str());
            this->phase = PHASE_ERROR;
            this->_phase_callback(PHASE_ERROR);
            return;
        }

        // The web UI now ships inside the firmware app image (GM-106), so there is no separate filesystem image to
        // flash. OTA stays a single, rollback-protected app image and the LittleFS partition (profiles + shot history)
        // is left untouched across updates. The filesystem image is only used for fresh USB installs.
        ESP_LOGI(TAG, "Update successful. Restarting...\n");
        this->phase = PHASE_FINISHED;
        this->_phase_callback(PHASE_FINISHED);
        updateExecuted = true;
    }
    this->phase = PHASE_FINISHED;
    this->_phase_callback(PHASE_FINISHED);

    if (updateExecuted) {
        delay(1000);
        ESP.restart();
    }

    ESP_LOGI(TAG, "No updates found\n");
}

void GitHubOTA::setReleaseUrl(const String &release_url) { this->_release_url = release_url; }

HTTPUpdateResult GitHubOTA::update_firmware(const String &url) {
    const char *TAG = "update_firmware";
    ESP_LOGI(TAG, "Download URL: %s\n", url.c_str());

    // Same guard the BLE DFU path already has: HTTPUpdate writes to whatever
    // esp_ota_get_next_update_partition() hands back, and IDF hands back the
    // *running* partition when the table holds only one OTA slot. Beginning an
    // update there erases the app that is executing, leaving nothing to roll
    // back to and no route in except USB. partitions/headless_8mb.csv is
    // single-slot and its build still shows the update button, so this is
    // reachable today, not hypothetical.
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr || target == running) {
        ESP_LOGE(TAG, "Refusing OTA: this build has a single app slot (running=%s); reflash over USB instead",
                 running != nullptr ? running->label : "?");
        return HTTP_UPDATE_FAILED;
    }

    attach_ca_bundle(_wifi_client);
    // Updater follows redirects with FORCE_FOLLOW, which drops the CA bundle
    // across the github.com -> release-assets.githubusercontent.com hop
    // (-30336). Resolve to the terminal single-host URL so the update runs on a
    // trusted leg with no redirect to follow.
    const String resolved = resolve_redirect_chain(_wifi_client, url);
    if (resolved.length() == 0) {
        ESP_LOGE(TAG, "Could not resolve firmware URL: %s", url.c_str());
        return HTTP_UPDATE_FAILED;
    }
    attach_ca_bundle(_wifi_client);
    auto result = Updater.update(_wifi_client, resolved);

    print_update_result(Updater, result, TAG);
    return result;
}

void GitHubOTA::setControllerVersion(const String &controller_version) {
    semver_free(&_controller_version);
    _controller_version = from_string(controller_version.substring(1).c_str());
}
