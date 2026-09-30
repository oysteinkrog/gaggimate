#ifndef BLE_OTA_DFU_OTA_IMAGE_SIZE_H
#define BLE_OTA_DFU_OTA_IMAGE_SIZE_H

// The size check the controller runs when the last part of an OTA image has
// arrived, before it tells the display anything (gm-bzu.55). Plain C++ with no
// Arduino or ESP-IDF dependency so test/test_ota_result can include it and
// check the report text against the display's reader
// (lib/OTA/src/ControllerInstallResult.h).
//
// Order on the wire when the check fails:
//   1. 0x0F + the report built here. The display classifies it as a failure.
//   2. 0xFF, the transfer protocol's "not taken" signal. The display's
//      transfer loop (ControllerOTA::update) ends on it with no ack, reports
//      the controller update as failed, and leaves its own image alone.
// 0xF2 is never sent for an image that fails this check. The report goes
// first because the display keeps only the latest signal byte: a 0x0F that
// landed after the 0xFF could overwrite it before the loop read it.

#include <cstddef>
#include <cstdint>
#include <cstdio>

// The transfer protocol's failure signal. The display treats any terminal
// signal other than 0xF2 as "the controller did not take the image".
constexpr uint8_t OTA_SIGNAL_REFUSED = 0xFF;
// Prefix of the ASCII install report the display reads.
constexpr uint8_t OTA_SIGNAL_REPORT = 0x0F;

enum class OtaImageSize {
    Match,
    Short,
    Oversized,
    // No 0xFE size packet arrived, so there is nothing to check against.
    Undeclared,
};

inline OtaImageSize checkOtaImageSize(uint32_t received, uint32_t expected) {
    if (expected == 0) {
        return OtaImageSize::Undeclared;
    }
    if (received < expected) {
        return OtaImageSize::Short;
    }
    if (received > expected) {
        return OtaImageSize::Oversized;
    }
    return OtaImageSize::Match;
}

// Writes the report text (without the 0x0F prefix) into buf and returns its
// length, or 0 when the size matches. The text starts with "Refusing OTA",
// which ControllerInstallResult classifies as a failure.
inline size_t formatOtaImageSizeReport(char *buf, size_t cap, uint32_t received, uint32_t expected) {
    const char *what = nullptr;
    switch (checkOtaImageSize(received, expected)) {
    case OtaImageSize::Match:
        return 0;
    case OtaImageSize::Short:
        what = "image short";
        break;
    case OtaImageSize::Oversized:
        what = "image oversized";
        break;
    case OtaImageSize::Undeclared:
        what = "image size never declared";
        break;
    }
    if (buf == nullptr || cap == 0) {
        return 0;
    }
    const int n = std::snprintf(buf, cap, "Refusing OTA: %s, received %lu of %lu bytes", what,
                                static_cast<unsigned long>(received), static_cast<unsigned long>(expected));
    if (n < 0) {
        buf[0] = '\0';
        return 0;
    }
    return static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : cap - 1;
}

#endif // BLE_OTA_DFU_OTA_IMAGE_SIZE_H
