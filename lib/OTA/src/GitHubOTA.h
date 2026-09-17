#ifndef ESP_GITHUB_OTA_H
#define ESP_GITHUB_OTA_H

#include "ControllerOTA.h"
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

#include "semver.h"

constexpr uint8_t PHASE_IDLE = 0;
constexpr uint8_t PHASE_DISPLAY_FW = 1;
constexpr uint8_t PHASE_DISPLAY_FS = 2;
constexpr uint8_t PHASE_CONTROLLER_FW = 3;
constexpr uint8_t PHASE_FINISHED = 4;
// A failed update used to return from update() without any further callback, so
// the UI kept showing the phase it had last been told about, with a spinner and
// no way back. The web UI treats any non-zero phase as "in progress", so an
// explicit terminal error phase is the only way to say the run is over and it
// did not work.
constexpr uint8_t PHASE_ERROR = 5;

using phase_callback_t = std::function<void(uint8_t phase)>;
using progress_callback_t = std::function<void(uint8_t phase, int progress)>;

extern const uint8_t x509_crt_imported_bundle_bin_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_imported_bundle_bin_end[] asm("_binary_x509_crt_bundle_end");

class GitHubOTA {
  public:
    GitHubOTA(const String &display_version, const String &controller_version, const String &release_url,
              const phase_callback_t &phase_callback, const progress_callback_t &progress_callback,
              const String &firmware_name = "firmware.bin", const String &filesystem_name = "filesystem.bin",
              const String &controller_firmware_name = "controller.bin");

    void init(NimBLEClient *client);
    void checkForUpdates();
    bool isUpdateAvailable(bool controller = false) const;
    String getCurrentVersion() const;
    void update(bool controller = true, bool display = true);
    // Flashes the display from one exact URL, with no version check and no
    // GitHub release lookup: the dev-deploy path (gm-thg), where the image is
    // whatever the developer just built and its version string is the same
    // -dirty string already running. Plain http is expected and takes a plain
    // client with no CA bundle and no redirect resolution; https falls through
    // to the same secure path a release download uses. Reports through the
    // same phase and progress callbacks, so the web UI's progress bar and the
    // display's panel stop both work unchanged, and reboots on success.
    // Returns only on failure.
    HTTPUpdateResult updateFromUrl(const String &url);
    void setReleaseUrl(const String &release_url);
    void setControllerVersion(const String &controller_version);

  private:
    HTTPUpdate Updater;

    HTTPUpdateResult update_firmware(const String &url);
    // False when the partition table has no second app slot, after logging
    // why. esp_ota_get_next_update_partition hands back the *running*
    // partition in that case, so beginning an update there erases the app that
    // is executing.
    bool haveSecondAppSlot(const char *tag) const;

    uint8_t phase = PHASE_IDLE;
    semver_t _version;
    semver_t _controller_version;
    String _latest_version_string;
    semver_t _latest_version = {0, 0, 0, nullptr, nullptr};
    String _release_url;
    String _latest_url;
    String _firmware_name;
    String _filesystem_name;
    String _controller_firmware_name;
    WiFiClientSecure _wifi_client;
    ControllerOTA _controller_ota;
    phase_callback_t _phase_callback = nullptr;
    progress_callback_t _progress_callback = nullptr;
};

#endif
