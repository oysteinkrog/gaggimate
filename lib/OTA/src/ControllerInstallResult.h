#ifndef CONTROLLER_INSTALL_RESULT_H
#define CONTROLLER_INSTALL_RESULT_H

// Reads the controller's OTA install report. Plain C++ with no Arduino or
// ESP-IDF dependency so the host test (test/test_ota_result) can include it.
//
// After the controller has received the whole image (signal 0xF2) it flashes
// it and sends one 0x0F-prefixed ASCII notification built by
// lib/ble_ota_dfu/src/ble_ota_dfu.cpp. That text is the only report of what
// its flash did. It carries a progress line and then one of these endings:
//
//   "OTA Done: Success!"                           the image is installed
//   "OTA Done: Failed!"                            Update.end() passed but the
//                                                  update is not finished
//   "Error #: N"                                   Update.end() failed
//   "Not enough space to begin BLE OTA DFU"        Update.begin() refused
//   "Refusing OTA: this build has a single app slot ..."  no spare slot
//
// Anything else is a report we do not know how to read. The display treats
// that as unconfirmed rather than as a pass (see GitHubOTA::update).

#include <cstddef>
#include <cstring>

enum class ControllerInstallResult {
    // A 0x0F message arrived but none of the known endings is in it.
    Unrecognised,
    Success,
    Failure,
};

inline bool installTextContains(const char *text, size_t len, const char *needle) {
    const size_t n = std::strlen(needle);
    if (n == 0 || len < n) {
        return false;
    }
    for (size_t i = 0; i + n <= len; i++) {
        if (std::memcmp(text + i, needle, n) == 0) {
            return true;
        }
    }
    return false;
}

// text is the notification body after the 0x0F prefix; it need not be NUL
// terminated. Failure markers are checked first, so a report that somehow
// carries both a failure and "Success!" is a failure.
inline ControllerInstallResult classifyControllerInstallResult(const char *text, size_t len) {
    if (text == nullptr) {
        return ControllerInstallResult::Unrecognised;
    }
    static const char *const kFailure[] = {
        "OTA Done: Failed!",
        "Error #:",
        "Not enough space",
        "Refusing OTA",
    };
    for (const char *marker : kFailure) {
        if (installTextContains(text, len, marker)) {
            return ControllerInstallResult::Failure;
        }
    }
    if (installTextContains(text, len, "OTA Done: Success!")) {
        return ControllerInstallResult::Success;
    }
    return ControllerInstallResult::Unrecognised;
}

#endif // CONTROLLER_INSTALL_RESULT_H
