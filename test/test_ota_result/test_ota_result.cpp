// Unit tests: how the display reads the controller's OTA install report
// (lib/OTA/src/ControllerInstallResult.h). Host-side, no Arduino or ESP-IDF:
// pio test -e native_otaresult.
//
// The inputs are the exact strings lib/ble_ota_dfu/src/ble_ota_dfu.cpp builds,
// so a change to either side that breaks the other fails here first.

#include <unity.h>

#include <cstring>
#include <string>

#include "ControllerInstallResult.h"
// The controller's size check (gm-bzu.55). Header-only and Arduino-free.
#include "../../lib/ble_ota_dfu/src/ota_image_size.h"

namespace {

ControllerInstallResult classify(const std::string &s) { return classifyControllerInstallResult(s.data(), s.size()); }

// What the controller sends on a good flash: the progress line then the verdict.
const std::string kGood = "Written : 1234567/1234567 [100 %] \nOTA Done: Success!\n";

} // namespace

void setUp() {}
void tearDown() {}

void test_success_report() { TEST_ASSERT_EQUAL(ControllerInstallResult::Success, classify(kGood)); }

void test_update_end_passed_but_not_finished() {
    TEST_ASSERT_EQUAL(ControllerInstallResult::Failure, classify("Written : 100/200 [50 %] \nOTA Done: Failed!\n"));
}

void test_update_end_error_number() {
    TEST_ASSERT_EQUAL(ControllerInstallResult::Failure, classify("Written : 100/200 [50 %] \nError #: 7"));
}

void test_not_enough_space() {
    TEST_ASSERT_EQUAL(ControllerInstallResult::Failure, classify("Not enough space to begin BLE OTA DFU"));
}

void test_single_slot_refusal() {
    TEST_ASSERT_EQUAL(ControllerInstallResult::Failure,
                      classify("Refusing OTA: this build has a single app slot, reflash over USB instead"));
}

void test_progress_only_is_unrecognised() {
    // A report with no verdict is not a pass.
    TEST_ASSERT_EQUAL(ControllerInstallResult::Unrecognised, classify("Written : 100/200 [50 %] \n"));
}

void test_empty_and_null_are_unrecognised() {
    TEST_ASSERT_EQUAL(ControllerInstallResult::Unrecognised, classify(""));
    TEST_ASSERT_EQUAL(ControllerInstallResult::Unrecognised, classifyControllerInstallResult(nullptr, 5));
}

void test_unrelated_text_is_unrecognised() {
    TEST_ASSERT_EQUAL(ControllerInstallResult::Unrecognised, classify("hello from a future controller"));
}

void test_failure_marker_beats_success_marker() {
    TEST_ASSERT_EQUAL(ControllerInstallResult::Failure, classify("OTA Done: Success!\nError #: 3"));
}

void test_length_is_honoured_not_nul() {
    // The BLE payload is not NUL terminated; only len bytes may be read.
    const char buf[] = "OTA Done: Success!XXXXXXXX";
    TEST_ASSERT_EQUAL(ControllerInstallResult::Success, classifyControllerInstallResult(buf, 18));
    // Cut short of the marker: the truncated text must not match.
    TEST_ASSERT_EQUAL(ControllerInstallResult::Unrecognised, classifyControllerInstallResult(buf, 10));
}

void test_partial_marker_does_not_match() {
    TEST_ASSERT_EQUAL(ControllerInstallResult::Unrecognised, classify("OTA Done: Succ"));
    TEST_ASSERT_EQUAL(ControllerInstallResult::Unrecognised, classify("Error"));
}

// --- The controller's size check on the last part (gm-bzu.55) ---

static std::string sizeReport(uint32_t received, uint32_t expected) {
    char buf[96];
    const size_t n = formatOtaImageSizeReport(buf, sizeof(buf), received, expected);
    return std::string(buf, n);
}

void test_matching_size_sends_no_report() {
    TEST_ASSERT_EQUAL(OtaImageSize::Match, checkOtaImageSize(1234567, 1234567));
    TEST_ASSERT_EQUAL_size_t(0, sizeReport(1234567, 1234567).size());
}

void test_short_image_is_refused_and_reads_as_failure() {
    // A truncated image: the controller must refuse it before any 0xF2, and
    // the report it sends must read as a failure on the display.
    TEST_ASSERT_EQUAL(OtaImageSize::Short, checkOtaImageSize(1200000, 1234567));
    const std::string r = sizeReport(1200000, 1234567);
    TEST_ASSERT_EQUAL_STRING("Refusing OTA: image short, received 1200000 of 1234567 bytes", r.c_str());
    TEST_ASSERT_EQUAL(ControllerInstallResult::Failure, classify(r));
}

void test_one_byte_short_is_refused() {
    TEST_ASSERT_EQUAL(OtaImageSize::Short, checkOtaImageSize(1234566, 1234567));
    TEST_ASSERT_EQUAL(ControllerInstallResult::Failure, classify(sizeReport(1234566, 1234567)));
}

void test_oversized_image_is_refused_and_reads_as_failure() {
    TEST_ASSERT_EQUAL(OtaImageSize::Oversized, checkOtaImageSize(1234568, 1234567));
    TEST_ASSERT_EQUAL(ControllerInstallResult::Failure, classify(sizeReport(1234568, 1234567)));
}

void test_undeclared_size_is_refused() {
    // No 0xFE packet: nothing to check against, so nothing is installed.
    TEST_ASSERT_EQUAL(OtaImageSize::Undeclared, checkOtaImageSize(0, 0));
    TEST_ASSERT_EQUAL(OtaImageSize::Undeclared, checkOtaImageSize(5000, 0));
    TEST_ASSERT_EQUAL(ControllerInstallResult::Failure, classify(sizeReport(5000, 0)));
}

void test_size_report_fits_small_buffer() {
    // Truncated to the buffer and NUL terminated, never past it.
    char buf[16];
    std::memset(buf, 'X', sizeof(buf));
    const size_t n = formatOtaImageSizeReport(buf, sizeof(buf), 1, 2);
    TEST_ASSERT_EQUAL_size_t(sizeof(buf) - 1, n);
    TEST_ASSERT_EQUAL_CHAR('\0', buf[sizeof(buf) - 1]);
    TEST_ASSERT_EQUAL(ControllerInstallResult::Failure, classifyControllerInstallResult(buf, n));
}

void test_refusal_signal_is_not_the_ack() {
    // The display ends its transfer loop on 0xFF without an ack.
    TEST_ASSERT_EQUAL_HEX8(0xFF, OTA_SIGNAL_REFUSED);
    TEST_ASSERT_EQUAL_HEX8(0x0F, OTA_SIGNAL_REPORT);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_success_report);
    RUN_TEST(test_update_end_passed_but_not_finished);
    RUN_TEST(test_update_end_error_number);
    RUN_TEST(test_not_enough_space);
    RUN_TEST(test_single_slot_refusal);
    RUN_TEST(test_progress_only_is_unrecognised);
    RUN_TEST(test_empty_and_null_are_unrecognised);
    RUN_TEST(test_unrelated_text_is_unrecognised);
    RUN_TEST(test_failure_marker_beats_success_marker);
    RUN_TEST(test_length_is_honoured_not_nul);
    RUN_TEST(test_partial_marker_does_not_match);
    RUN_TEST(test_matching_size_sends_no_report);
    RUN_TEST(test_short_image_is_refused_and_reads_as_failure);
    RUN_TEST(test_one_byte_short_is_refused);
    RUN_TEST(test_oversized_image_is_refused_and_reads_as_failure);
    RUN_TEST(test_undeclared_size_is_refused);
    RUN_TEST(test_size_report_fits_small_buffer);
    RUN_TEST(test_refusal_signal_is_not_the_ack);
    return UNITY_END();
}
