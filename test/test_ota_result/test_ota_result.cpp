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
    return UNITY_END();
}
