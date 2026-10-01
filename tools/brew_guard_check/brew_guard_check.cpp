// Host check for the controller's brew bound (gm-warz,
// lib/GaggiMateController/src/BrewGuard.h). `make -C tools/brew_guard_check
// check` builds and runs it; a non-zero exit names the failed case.
//
// The guard is pure logic with the clock passed in, so every case here drives
// it the way GaggiMateController does: valve and pump commands as the display
// sends them, poll() every 250 ms as loop() does, onLinkDown() from the BLE
// host task's disconnect or the ping watchdog.

#include "BrewGuard.h"

#include <cstdio>
#include <cstdlib>

using gm_safety::BREW_MAX_DURATION_MS;
using gm_safety::BrewGuard;
using gm_safety::BrewStop;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                                                         \
    do {                                                                                                                    \
        ++checks;                                                                                                           \
        if (!(cond)) {                                                                                                      \
            ++failures;                                                                                                     \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #cond);                                           \
        }                                                                                                                   \
    } while (0)

// The 2026-10-01 incident: a brew the display never stops. The controller
// polls every 250 ms for 65 minutes; the brew must end within one poll of the
// maximum and stay ended.
static void incident_brew_ends_at_the_maximum() {
    BrewGuard g;
    CHECK(g.allowPumpDemand(true));
    CHECK(g.onValveCommand(true, 0));
    uint32_t stoppedAt = 0;
    int stops = 0;
    for (uint32_t t = 250; t <= 65u * 60u * 1000u; t += 250) {
        if (g.poll(t)) {
            ++stops;
            if (stoppedAt == 0) {
                stoppedAt = t;
            }
        }
    }
    CHECK(stops == 1);
    CHECK(stoppedAt >= BREW_MAX_DURATION_MS);
    CHECK(stoppedAt < BREW_MAX_DURATION_MS + 250);
    CHECK(g.latched() == BrewStop::TimeLimit);
    CHECK(!g.brewing());
}

static void stops_exactly_at_the_maximum() {
    BrewGuard g(10000);
    g.onValveCommand(true, 1000);
    CHECK(!g.poll(1000 + 9999));
    CHECK(g.brewing());
    CHECK(g.poll(1000 + 10000));
    CHECK(!g.poll(1000 + 10001)); // once, not on every poll after
}

static void default_is_300_seconds() {
    // The owner has not set the default; this pins the conservative first
    // choice so a change to it is deliberate and shows up here.
    CHECK(BREW_MAX_DURATION_MS == 300000u);
    BrewGuard g;
    CHECK(g.maxMs() == 300000u);
}

static void latched_stop_refuses_valve_and_pump() {
    BrewGuard g(1000);
    g.onValveCommand(true, 0);
    CHECK(g.poll(1000));
    // The display has not heard yet, or resends its state after a reconnect.
    CHECK(!g.onValveCommand(true, 1100));
    CHECK(!g.allowPumpDemand(true));
    CHECK(g.allowPumpDemand(false)); // pump off is always applied
    CHECK(!g.brewing());
    CHECK(!g.poll(999999)); // a refused open starts no new timer
}

static void valve_close_clears_the_latch_and_a_new_brew_runs() {
    BrewGuard g(1000);
    g.onValveCommand(true, 0);
    CHECK(g.poll(1000));
    CHECK(!g.onValveCommand(false, 1200));
    CHECK(g.latched() == BrewStop::None);
    CHECK(g.allowPumpDemand(true));
    CHECK(g.onValveCommand(true, 5000));
    CHECK(!g.poll(5999)); // the timer restarted at the new open
    CHECK(g.poll(6000));
}

static void repeated_open_does_not_restart_the_timer() {
    BrewGuard g(10000);
    g.onValveCommand(true, 0);
    CHECK(g.onValveCommand(true, 8000));
    CHECK(g.poll(10000));
}

static void report_is_taken_once() {
    BrewGuard g(1000);
    CHECK(g.takeReport() == BrewStop::None);
    g.onValveCommand(true, 0);
    g.poll(1000);
    CHECK(g.takeReport() == BrewStop::TimeLimit);
    CHECK(g.takeReport() == BrewStop::None);
    CHECK(g.latched() == BrewStop::TimeLimit); // taking the report does not clear the latch
}

static void link_drop_during_a_brew_latches() {
    BrewGuard g;
    g.onValveCommand(true, 0);
    CHECK(g.onLinkDown());
    CHECK(g.latched() == BrewStop::LinkLost);
    CHECK(!g.onLinkDown()); // idempotent: the ping watchdog re-enters every 250 ms
    // The reconnect resends valve open and pump on: both refused.
    CHECK(!g.allowPumpDemand(true));
    CHECK(!g.onValveCommand(true, 2000));
    CHECK(g.takeReport() == BrewStop::LinkLost);
    // The display ends its process on the report and closes the valve.
    g.onValveCommand(false, 2100);
    CHECK(g.latched() == BrewStop::None);
    CHECK(g.onValveCommand(true, 3000));
}

static void link_drop_without_a_brew_latches_nothing() {
    BrewGuard g;
    CHECK(!g.onLinkDown());
    CHECK(g.latched() == BrewStop::None);
    CHECK(g.allowPumpDemand(true)); // steam and hot water carry on after a reconnect
    g.onValveCommand(true, 0);
    g.onValveCommand(false, 100);
    CHECK(!g.onLinkDown()); // a brew that already ended
}

static void rebooted_display_clears_without_a_report() {
    // A display that reboots mid-brew comes back idle and its first full
    // state says valve closed; that clears the latch and the pending report,
    // so the fresh display is not told about a brew it no longer has.
    BrewGuard g;
    g.onValveCommand(true, 0);
    g.onLinkDown();
    g.onValveCommand(false, 9000);
    CHECK(g.takeReport() == BrewStop::None);
    CHECK(g.latched() == BrewStop::None);
}

static void millis_wrap_is_handled() {
    BrewGuard g(10000);
    const uint32_t start = 0xFFFFF000u; // 4096 ms before the wrap
    g.onValveCommand(true, start);
    CHECK(!g.poll(start + 4096u)); // wrapped to 0
    CHECK(!g.poll(start + 9999u));
    CHECK(g.poll(start + 10000u));
}

static void pump_demand_is_free_before_any_stop() {
    BrewGuard g;
    CHECK(g.allowPumpDemand(true));
    CHECK(g.allowPumpDemand(false));
    CHECK(!g.poll(123456)); // no valve open, nothing to bound
}

int main() {
    incident_brew_ends_at_the_maximum();
    stops_exactly_at_the_maximum();
    default_is_300_seconds();
    latched_stop_refuses_valve_and_pump();
    valve_close_clears_the_latch_and_a_new_brew_runs();
    repeated_open_does_not_restart_the_timer();
    report_is_taken_once();
    link_drop_during_a_brew_latches();
    link_drop_without_a_brew_latches_nothing();
    rebooted_display_clears_without_a_report();
    millis_wrap_is_handled();
    pump_demand_is_free_before_any_stop();
    std::printf("brew_guard_check: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
