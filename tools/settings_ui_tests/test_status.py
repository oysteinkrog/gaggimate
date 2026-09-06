#!/usr/bin/env python3
"""Scenario for gm-flw.12 (the Status settings category): read-only versions/
network/scale/time rows and the hold-to-restart row.

Drives the desktop simulator only (pio run -e display-sim), through
tools/settings_ui_tests/rig.py, the same as every other category scenario in
this epic. The bench device checks the bead text also calls for (the same
scenario against --host, synth(0), a Restart hold timed into a scratch
build's slowed periodic flush) are the swarm leader's to run against the
shared bench device and are not exercised here; see main()'s closing note.

Usage:
    python3 tools/settings_ui_tests/test_status.py
        [--program PATH/to/.pio/build/display-sim/program]
        [--workdir DIR] [--port N] [--host ip[:port]]

Exits 0 if every check passes, 1 otherwise. Logs each step (Rig.log) and
prints a PASS/FAIL summary at the end. --host switches every check that can
run against either venue onto a live device instead of launching a
simulator; the checks that need a fresh, isolated boot (the fail-flush
scenario, the destructive restart-persistence scenario) are simulator-only
regardless and are skipped with a logged reason when --host is given.
"""
import argparse
import os
import re
import sys
import tempfile
import time
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO_ROOT)

from tools.settings_ui_tests import Rig, RigHTTPError, Sim  # noqa: E402
from tools.settings_ui_tests.fixtures import Venue  # noqa: E402

DEFAULT_PROGRAM = os.path.join(REPO_ROOT, ".pio", "build", "display-sim", "program")
VERSION_H = os.path.join(REPO_ROOT, "src", "version.h")

STATUS_CATEGORY = 4
DISPLAY_CATEGORY = 1

FAILURES = []
TOTAL = 0


def check(rig, name, cond, detail=""):
    global TOTAL
    TOTAL += 1
    rig.log("check", name=name, ok=int(bool(cond)), detail=detail)
    if not cond:
        FAILURES.append((name, detail))
    return cond


def build_git_version():
    """BUILD_GIT_VERSION straight from the source it is compiled from, so
    the Display firmware row's oracle is the same string the compiler
    embedded, not a guess at its format."""
    with open(VERSION_H, encoding="utf-8") as f:
        src = f.read()
    m = re.search(r'#define BUILD_GIT_VERSION "([^"]*)"', src)
    if not m:
        raise RuntimeError("BUILD_GIT_VERSION not found in %r" % VERSION_H)
    return m.group(1)


def open_to_status_page(rig, page=0):
    """Opens the settings shell (a no-op if already open) and navigates to
    the Status category's given page, polling settingsui_state() after each
    command the same way DefaultUI::serviceSettingsUi documents (a command
    is accepted, then executed across one or more UI-task passes)."""
    rig.settingsui(open=1)
    rig.wait_until(lambda: rig.settingsui_state().get("open") is True, timeout=8)
    rig.settingsui(cat=STATUS_CATEGORY)
    rig.wait_until(lambda: rig.settingsui_state().get("category") == STATUS_CATEGORY, timeout=8)
    if page != 0:
        rig.settingsui(page=page)
        rig.wait_until(lambda: rig.settingsui_state().get("page") == page, timeout=8)
    time.sleep(0.2)  # one more UI pass so buildRow's initial fill is in the dump
    return rig.touchmap(screen=0)


def parse_screen10(dump):
    """screen 10 (the pre-existing info screen) combines display+controller
    version into one label ("<display> • <controller>") and network+ip
    into another ("SSID: <network> • IP: <ip>"), dumped 2026-09-06
    against a clean sim boot. Returns (display_fw, controller_fw, network,
    ip), any entry None if its label was not found."""
    display_fw = controller_fw = network = ip = None
    for o in dump["objects"]:
        t = o.get("t")
        if t is None:
            continue
        m = re.match(r"^SSID: (.*) • IP: (.*)$", t)
        if m:
            network, ip = m.group(1), m.group(2)
            continue
        m2 = re.match(r"^(.+) • (.+)$", t)
        if m2:
            display_fw, controller_fw = m2.group(1), m2.group(2)
    return display_fw, controller_fw, network, ip


def check_page0_rows(rig, expected_version):
    d0 = open_to_status_page(rig, page=0)
    names = ("Display firmware", "Controller firmware", "Network", "IP address", "Controller")
    for name in names:
        check(rig, "page0_row_present_%s" % name.replace(" ", "_"), rig.find_tag(d0, name, "row") is not None, name)

    display_fw = rig.row_value(d0, "Display firmware")
    controller_fw = rig.row_value(d0, "Controller firmware")
    network = rig.row_value(d0, "Network")
    ip = rig.row_value(d0, "IP address")
    ctrl = rig.row_value(d0, "Controller")

    check(rig, "display_firmware_matches_build", display_fw == expected_version, "%r vs %r" % (display_fw, expected_version))
    check(
        rig,
        "controller_firmware_reasonable",
        controller_fw == "Not connected" or len(controller_fw) > 0,
        repr(controller_fw),
    )
    check(rig, "network_value_shape", network in ("Disconnected", "Access point") or len(network) > 0, repr(network))
    check(rig, "ip_value_nonempty", len(ip) > 0, repr(ip))
    check(rig, "controller_value_shape", ctrl in ("Connected", "Disconnected"), repr(ctrl))

    # Cross-check against screen 10 (the pre-existing info screen), which
    # the acceptance criterion names directly: both screens are driven by
    # the same Controller/Settings/WiFi state, so their text must agree.
    d10 = rig.touchmap(screen=10, load=True)
    s10_display_fw, s10_controller_fw, s10_network, s10_ip = parse_screen10(d10)
    check(rig, "info_screen_display_version_found", s10_display_fw is not None, "screen 10 had no version label")
    check(rig, "info_screen_network_found", s10_network is not None, "screen 10 had no network label")
    if s10_display_fw is not None:
        check(rig, "display_firmware_matches_info_screen", display_fw == s10_display_fw, "%r vs %r" % (display_fw, s10_display_fw))
    if s10_controller_fw is not None:
        check(
            rig,
            "controller_firmware_matches_info_screen",
            controller_fw == s10_controller_fw,
            "%r vs %r" % (controller_fw, s10_controller_fw),
        )
    if s10_network is not None:
        check(rig, "network_matches_info_screen", network == s10_network, "%r vs %r" % (network, s10_network))
    if s10_ip is not None:
        check(rig, "ip_matches_info_screen", ip == s10_ip, "%r vs %r" % (ip, s10_ip))
    return d0


def check_page1_rows(rig):
    d1 = open_to_status_page(rig, page=1)
    check(rig, "page1_row_present_Scale", rig.find_tag(d1, "Scale", "row") is not None)
    check(rig, "page1_row_present_Time", rig.find_tag(d1, "Time", "row") is not None)
    check(rig, "page1_row_present_Restart", rig.find_tag(d1, "Restart", "confirm") is not None)

    scale = rig.row_value(d1, "Scale")
    m = re.match(r"^(\S+) (ok|no data)$", scale)
    check(rig, "scale_value_shape", m is not None, repr(scale))

    time_val = rig.row_value(d1, "Time")
    check(
        rig,
        "time_value_shape",
        time_val == "Not synchronised" or re.match(r"^\d{1,2}:\d{2}:\d{2}( [AP]M)?$", time_val) is not None,
        repr(time_val),
    )
    return d1


def check_audit(rig, d0, d1):
    for label, d in (("page0", d0), ("page1", d1)):
        a = rig.audit(d)
        check(rig, "audit_no_violations_%s" % label, len(a["violations"]) == 0, repr(a["violations"]))


def _time_to_seconds(s):
    parts = s.split(" ")[0].split(":")
    h, m, sec = (int(p) for p in parts)
    return h * 3600 + m * 60 + sec


def check_time_progression(rig):
    """Two Time dumps 3s apart: the row must advance by 2..4s (the bead's
    own tolerance) and, since getLocalTime() on the simulator is always the
    host's real wall clock (sim/platform/Arduino.h), match this script's own
    local time within 2s -- both venues read the same clock here."""
    d_a = open_to_status_page(rig, page=1)
    t_a = rig.row_value(d_a, "Time")
    py_a = datetime.now()
    check(rig, "time_synchronised_for_progression_check", t_a != "Not synchronised", repr(t_a))
    if t_a == "Not synchronised":
        return
    time.sleep(3)
    d_b = rig.wait_dump_change(d_a["seq"], screen=0)
    t_b = rig.row_value(d_b, "Time")
    check(rig, "time_advances_between_dumps", t_a != t_b, "%r == %r" % (t_a, t_b))

    sa, sb = _time_to_seconds(t_a), _time_to_seconds(t_b)
    delta = (sb - sa) % 86400
    check(rig, "time_advance_in_range", 2 <= delta <= 4, "delta=%r (%r -> %r)" % (delta, t_a, t_b))

    py_seconds = py_a.hour * 3600 + py_a.minute * 60 + py_a.second
    diff = min((sa - py_seconds) % 86400, (py_seconds - sa) % 86400)
    check(rig, "time_matches_host_clock", diff <= 2, "sim=%r host_seconds=%r diff=%r" % (t_a, py_seconds, diff))


def check_short_hold_noop(rig, d1):
    """A 1500ms hold on Restart is short of kSettingsRowConfirmHoldMs (2000ms
    in SettingsRows.h) and must not fire onConfirm: the row's value is
    unchanged and the process is still answering afterward."""
    before = rig.row_value(d1, "Restart")
    target = rig.find_tag(d1, "Restart", "confirm")
    rig.tap_target(target, ms=1500)
    time.sleep(0.3)
    d_after = rig.touchmap(screen=0)
    after = rig.row_value(d_after, "Restart")
    check(rig, "short_hold_does_not_restart", after == before, "%r vs %r" % (before, after))
    check(rig, "short_hold_process_still_alive", True, "reached here without a connection error")


def check_restart_persistence_and_relaunch(rig, sim):
    """Bumps Standby brightness in the Display category (a different
    category, left uncommitted to NVS: CatDisplay's field is deferred and
    only marked dirty in Settings on commit, never flushed immediately),
    then holds Restart in Status. Only flushNow() making it to NVS before
    ESP.restart() proves the change survives; Sim.restart() reboots the
    process from the same sim_data and re-reads Settings from NVS, so a
    stale value here would mean flushNow() did not actually persist the
    other category's pending write."""
    rig.settingsui(open=1)
    rig.wait_until(lambda: rig.settingsui_state().get("open") is True, timeout=8)
    rig.settingsui(cat=DISPLAY_CATEGORY)
    rig.wait_until(lambda: rig.settingsui_state().get("category") == DISPLAY_CATEGORY, timeout=8)
    time.sleep(0.3)
    d_disp = rig.touchmap(screen=0)
    current = int(rig.row_value(d_disp, "Standby brightness"))
    direction = "plus" if current < 16 else "minus"
    expected = current + 1 if direction == "plus" else current - 1
    btn = rig.find_tag(d_disp, "Standby brightness", direction)
    rig.tap_target(btn, ms=80)
    time.sleep(0.3)
    d_disp2 = rig.touchmap(screen=0)
    bumped = int(rig.row_value(d_disp2, "Standby brightness"))
    check(rig, "standby_brightness_bumped_in_ui", bumped == expected, "%r vs %r" % (bumped, expected))

    # cat= pops the Display page (committing it: the bumped field is now
    # dirty in Settings, unflushed) before pushing Status.
    rig.settingsui(cat=STATUS_CATEGORY)
    rig.wait_until(lambda: rig.settingsui_state().get("category") == STATUS_CATEGORY, timeout=8)
    rig.settingsui(page=1)
    rig.wait_until(lambda: rig.settingsui_state().get("page") == 1, timeout=8)
    time.sleep(0.3)
    d_status = rig.touchmap(screen=0)
    target = rig.find_tag(d_status, "Restart", "confirm")

    try:
        rig.tap_target(target, ms=2500)
        check(rig, "long_hold_restarts_process", False, "tap completed normally; process should have exited")
    except RigHTTPError:
        check(rig, "long_hold_restarts_process", True, "connection refused, as expected after ESP.restart()")

    sim.restart()
    after = int(sim.rig.settings_value("standbyBrightness"))
    check(
        rig,
        "standby_brightness_persists_across_restart",
        after == expected,
        "before=%r bumped_to=%r after_restart=%r" % (current, expected, after),
    )


def check_fail_flush_scenario(program, workdir, port):
    """GM_SIM_FAIL_FLUSH=1 (read once, lazily, on Settings::flushNow()'s own
    first call -- see Settings.cpp) arms a one-shot forced failure. Needs
    its own fresh simulator launch, since the env var must be present before
    the process starts; run in its own directory so it never shares
    sim_data with the other checks."""
    data_dir = os.path.join(workdir, "sim_data")
    os.makedirs(workdir, exist_ok=True)
    prior = os.environ.get("GM_SIM_FAIL_FLUSH")
    os.environ["GM_SIM_FAIL_FLUSH"] = "1"
    try:
        with Sim(program, data_dir, port=port) as sim:
            rig = sim.rig
            rig.log("fail_flush_boot", program=program, port=port, workdir=workdir)
            rig.settingsui(open=1)
            rig.wait_until(lambda: rig.settingsui_state().get("open") is True, timeout=8)
            rig.settingsui(cat=STATUS_CATEGORY)
            rig.wait_until(lambda: rig.settingsui_state().get("category") == STATUS_CATEGORY, timeout=8)
            rig.settingsui(page=1)
            rig.wait_until(lambda: rig.settingsui_state().get("page") == 1, timeout=8)
            time.sleep(0.3)
            d = rig.touchmap(screen=0)
            target = rig.find_tag(d, "Restart", "confirm")

            rig.tap_target(target, ms=2500)
            time.sleep(0.3)
            d2 = rig.touchmap(screen=0)
            after_first = rig.row_value(d2, "Restart")
            check(
                rig,
                "forced_flush_failure_shows_retry_text",
                after_first == "Save failed, hold to retry",
                repr(after_first),
            )
            check(rig, "forced_flush_failure_does_not_restart", True, "reached here without a connection error")

            target2 = rig.find_tag(d2, "Restart", "confirm")
            try:
                rig.tap_target(target2, ms=2500)
                check(rig, "second_hold_restarts_after_forced_failure", False, "tap completed normally; process should have exited")
            except RigHTTPError:
                check(rig, "second_hold_restarts_after_forced_failure", True, "connection refused, as expected")
    finally:
        if prior is None:
            os.environ.pop("GM_SIM_FAIL_FLUSH", None)
        else:
            os.environ["GM_SIM_FAIL_FLUSH"] = prior


def _sequence(rig, venue):
    """Every check, in order, against an already-launched venue. Shared by
    main() and by run() below so the runner and a standalone invocation
    cannot drift apart.

    The last two checks end the process: one holds Restart and relaunches
    the simulator from the same data directory, the other launches a second
    simulator with the forced-flush-failure environment variable set. Both
    need a Sim to relaunch, and both are skipped when the caller asked for
    no restarts. The forced-failure simulator takes the next port up,
    because the caller's own simulator is still listening on venue.port."""
    d0 = check_page0_rows(rig, build_git_version())
    d1 = check_page1_rows(rig)
    check_audit(rig, d0, d1)
    check_time_progression(rig)
    if not venue.can_restart:
        rig.log(
            "skipped",
            reason="restart-persistence and fail-flush scenarios need a simulator to relaunch and are destructive",
        )
        return
    check_short_hold_noop(rig, rig.touchmap(screen=0))
    check_restart_persistence_and_relaunch(rig, venue.sim)
    check_fail_flush_scenario(venue.program, os.path.join(venue.workdir, "fail_flush"), venue.port + 1)


def run(rig, report, venue):
    """Entry point for the end-to-end runner (tools/settings_ui_test.py).
    Raises AssertionError listing the checks that failed, naming the row and
    value each one saw.

    Note for the runner: check_restart_persistence_and_relaunch leaves
    Standby brightness one step from where it found it and does not put it
    back, on purpose (that surviving bump is what it proves). The runner
    reports the difference as a restoration failure for this scenario."""
    first_fail, first_total = len(FAILURES), TOTAL
    _sequence(rig, venue)
    report.step("scenario_checks", scenario="status", checks=TOTAL - first_total,
                failed=len(FAILURES) - first_fail)
    new_failures = FAILURES[first_fail:]
    if new_failures:
        raise AssertionError("; ".join("%s: %s" % (n, d) for n, d in new_failures))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--program", default=DEFAULT_PROGRAM)
    ap.add_argument("--workdir", default=os.path.join(tempfile.gettempdir(), "gm_settings_ui_tests", "test_status"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("GM_SIM_PORT", "8092")))
    ap.add_argument("--host", default=None, help="run the read-only checks against a live device/sim instead of launching one")
    args = ap.parse_args()

    if args.host:
        rig = Rig(args.host)
        rig.log("boot", host=args.host)
        _sequence(rig, Venue(host=args.host, workdir=args.workdir, is_device=True))
    else:
        if not os.path.isfile(args.program):
            print("simulator binary not found at %r; build it first: pio run -e display-sim" % args.program, file=sys.stderr)
            return 1

        data_dir = os.path.join(args.workdir, "sim_data")
        os.makedirs(args.workdir, exist_ok=True)

        with Sim(args.program, data_dir, port=args.port) as sim:
            rig = sim.rig
            rig.log("boot", program=args.program, port=args.port, workdir=args.workdir)
            _sequence(rig, Venue(sim=sim, program=args.program, workdir=args.workdir, port=args.port,
                                 host="127.0.0.1:%d" % args.port, log_path=sim.log_path))

    print()
    if FAILURES:
        print("FAIL (%d/%d checks failed):" % (len(FAILURES), TOTAL))
        for name, detail in FAILURES:
            print("  %s: %s" % (name, detail))
        return 1
    print("PASS (%d/%d checks)" % (TOTAL, TOTAL))
    return 0


if __name__ == "__main__":
    sys.exit(main())
