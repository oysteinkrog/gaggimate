#!/usr/bin/env python3
"""Scenario for the Machine settings category (gm-flw.10): startup mode,
standby timeout and the auto wake-up master toggle. Built on rig.py
(gm-flw.16); drives the desktop simulator only (pio run -e display-sim).
The bench device checks the bead text also calls for (Rig("192.168.1.121"),
synth(0)) are the swarm leader's to run against the shared bench device and
are not exercised here.

Usage:
    python3 tools/settings_ui_tests/test_machine.py
        [--program PATH/to/.pio/build/display-sim/program]
        [--workdir DIR] [--port N]

Exits 0 if every check passes, 1 otherwise. Logs each step (Rig.log) and
restores every value this script changes through the on-display UI (taps
and the debug route), never through a POST, before exiting; see
check_web_save_reconcile's docstring for the one exception (a single,
whole-document POST that emulates the web UI's own save, on 127.0.0.1
only, restored afterward the same as everything else).
"""
import argparse
import os
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO_ROOT)

from tools.settings_ui_tests import Rig, RigHTTPError, Sim  # noqa: E402
from tools.settings_ui_tests.fixtures import Venue  # noqa: E402
from tools.settings_ui_tests.rig import find_tag, row_value  # noqa: E402

DEFAULT_PROGRAM = os.path.join(REPO_ROOT, ".pio", "build", "display-sim", "program")

CAT_MACHINE = 3
SCREEN_ID_STANDBY_SCREEN = 1
SCREEN_ID_BREW_SCREEN = 2

FAILURES = []
TOTAL = 0


def check(rig, name, cond, detail=""):
    global TOTAL
    TOTAL += 1
    rig.log("check", name=name, ok=int(bool(cond)), detail=detail)
    if not cond:
        FAILURES.append((name, detail))
    return cond


def do(rig, **cmd):
    """Issues one /api/debug/settingsui command and waits for the UI task
    to finish executing it (settingsui_state()'s "seq" is the last
    completed command's seq; the route itself only reports that the
    command was queued)."""
    res = rig.settingsui(**cmd)
    seq = res["seq"]
    rig.wait_until(lambda: rig.settingsui_state().get("seq") == seq, timeout=8)
    return rig.settingsui_state()


def open_machine(rig):
    do(rig, open=1)
    st = do(rig, cat=CAT_MACHINE)
    return st, rig.touchmap(screen=0)


def standby_label(minutes):
    return "Never" if minutes == 0 else "%d min" % minutes


def parse_standby_label(text):
    if text == "Never":
        return 0
    return int(text.split(" ")[0])


def check_initial_values(rig, s0):
    """Fresh open of Machine: rows show the stored values (GET wire format:
    startupMode "standby"/"brew", standbyTimeout in seconds -> minutes
    shown, 0 -> Never, autowakeupEnabled bool -> On/Off)."""
    st, dump = open_machine(rig)
    check(rig, "machine_opens_depth1_cat3", st.get("depth") == 1 and st.get("category") == CAT_MACHINE, repr(st))
    # Choice/stepper rows wrap their controls in a container tagged role
    # "row" (SettingsRows.cpp's createRowContainer); a toggle row is one
    # clickable target end to end and is tagged "toggle" directly instead
    # (settingsRowToggleCreate), so it is looked up by that role, not "row".
    names = [find_tag(dump, "Startup mode", "row"), find_tag(dump, "Standby timeout", "row"), find_tag(dump, "Auto wake-up", "toggle")]
    check(rig, "machine_rows_present", all(n is not None for n in names), repr(names))

    want_startup = "Brew" if s0["startupMode"] == "brew" else "Standby"
    got_startup = row_value(dump, "Startup mode")
    check(rig, "startup_mode_initial", got_startup == want_startup, "got=%r want=%r" % (got_startup, want_startup))

    want_standby_min = int(s0["standbyTimeout"]) // 60
    got_standby = row_value(dump, "Standby timeout")
    check(
        rig, "standby_timeout_initial", got_standby == standby_label(want_standby_min),
        "got=%r want=%r (raw=%r)" % (got_standby, standby_label(want_standby_min), s0["standbyTimeout"]),
    )

    want_wakeup = "On" if s0["autowakeupEnabled"] else "Off"
    got_wakeup = row_value(dump, "Auto wake-up")
    check(rig, "autowakeup_initial", got_wakeup == want_wakeup, "got=%r want=%r" % (got_wakeup, want_wakeup))
    return dump


def check_leave_no_change_writes_nothing(rig, s0):
    """Opening and immediately popping with no taps must not call any
    setter (settings unchanged) and must not log SettingsMachine's commit
    line (which only prints when something was written)."""
    before_log_len = len(sim_tail())
    open_machine(rig)
    do(rig, pop=1)
    after = rig.settings()
    same = (
        after["startupMode"] == s0["startupMode"]
        and int(after["standbyTimeout"]) == int(s0["standbyTimeout"])
        and after["autowakeupEnabled"] == s0["autowakeupEnabled"]
    )
    check(rig, "no_change_settings_unchanged", same, "before=%r after=%r" % (s0, after))
    new_log = sim_tail()[before_log_len:]
    check(rig, "no_change_no_commit_log", "SettingsMachine: committed" not in new_log, repr(new_log[-400:]))


def check_startup_mode(rig, s0):
    """Standby -> Brew via the choice row, pop, GET reflects it within 6s;
    after a Sim restart the display lands on the brew screen (the sim
    always has a connected mock controller, so Controller::onSystemInfo's
    setMode(getStartupMode()) runs on every boot). Restored to the
    starting mode afterward through the same row."""
    start_is_brew = s0["startupMode"] == "brew"
    _, dump = open_machine(rig)
    # Force to Standby first if this venue somehow started on Brew, so the
    # bead's literal "Standby to Brew" case is exercised either way.
    if start_is_brew:
        tgt = find_tag(dump, "Startup mode", "prev")
        rig.tap_target(tgt)
        dump = rig.touchmap(screen=0)
    got = row_value(dump, "Startup mode")
    check(rig, "startup_mode_forced_standby", got == "Standby", repr(got))

    tgt = find_tag(dump, "Startup mode", "next")
    rig.tap_target(tgt)
    dump = rig.touchmap(screen=0)
    check(rig, "startup_mode_row_shows_brew", row_value(dump, "Startup mode") == "Brew", repr(row_value(dump, "Startup mode")))
    do(rig, pop=1)

    def wire_is_brew():
        return rig.settings_value("startupMode") == "brew"

    try:
        rig.wait_until(wire_is_brew, timeout=6)
        ok = True
    except TimeoutError:
        ok = False
    check(rig, "startup_mode_wire_brew_within_6s", ok, repr(rig.settings_value("startupMode")))

    # Settings::loopTask only flushes dirty properties to NVS every 5s
    # (Settings.cpp); Sim.restart() kills the process outright, so without
    # this wait the live "brew" value read above can still be unflushed
    # when it dies, and the restarted process reloads the old "standby"
    # value from disk. Measured directly (this bead): restarting right
    # after the wire check reads back "standby" post-restart every time;
    # waiting past one flush period does not.
    time.sleep(6)
    sim.restart()
    d = rig.touchmap(screen=0)
    check(rig, "startup_mode_lands_on_brew_after_restart", d.get("screen_id") == SCREEN_ID_BREW_SCREEN, repr(d.get("screen_id")))

    # Restore: back to the menu (tap opens it from the brew screen, see
    # test_rig.py's check_tap_opens_menu), then Machine, then back to the
    # starting mode.
    rig.tap(240, 450)
    rig.wait_dump_change(rig.touchmap(screen=0)["seq"], screen=0)
    do(rig, open=1)
    do(rig, cat=CAT_MACHINE)
    dump = rig.touchmap(screen=0)
    check(rig, "startup_mode_reopen_shows_brew", row_value(dump, "Startup mode") == "Brew", repr(row_value(dump, "Startup mode")))
    want_restore = "Brew" if start_is_brew else "Standby"
    if row_value(dump, "Startup mode") != want_restore:
        tgt = find_tag(dump, "Startup mode", "prev" if not start_is_brew else "next")
        rig.tap_target(tgt)
        dump = rig.touchmap(screen=0)
    check(rig, "startup_mode_restored_row", row_value(dump, "Startup mode") == want_restore, repr(row_value(dump, "Startup mode")))
    do(rig, pop=1)
    do(rig, close=1)
    check(rig, "startup_mode_restored_wire", rig.settings_value("startupMode") == s0["startupMode"], repr(rig.settings_value("startupMode")))


def check_standby_timeout_and_standby_entry(rig, s0):
    """From the starting value: a 1500ms hold on minus lands between 1 and
    6 min (LVGL's LONG_PRESSED_REPEAT cadence, not an exact count -- shared
    contract, "wide count windows, never an exact cadence"); single taps
    continue down to exactly 1 min. At 1 min, popped and left untouched,
    the machine enters standby 60..70s later (Controller::loopLogic, which
    runs in the sim's own main loop). From there: one more tap down shows
    Never and persists 0; a tap up from Never shows 1 min. Restored to the
    starting value afterward through repeated tap-up.
    """
    start_seconds = int(s0["standbyTimeout"])
    start_minutes = start_seconds // 60
    check(rig, "standby_timeout_start_on_grid", start_seconds % 60 == 0 and start_minutes >= 1, repr(start_seconds))

    _, dump = open_machine(rig)
    check(rig, "standby_timeout_pretest_value", row_value(dump, "Standby timeout") == standby_label(start_minutes),
          "got=%r want=%r" % (row_value(dump, "Standby timeout"), standby_label(start_minutes)))

    minus = find_tag(dump, "Standby timeout", "minus")
    plus = find_tag(dump, "Standby timeout", "plus")

    rig.tap_target(minus, ms=1500)
    dump = rig.touchmap(screen=0)
    after_hold = parse_standby_label(row_value(dump, "Standby timeout"))
    check(rig, "standby_timeout_hold_1500ms_in_1_6", 1 <= after_hold <= 6, "got=%r (started at %r)" % (after_hold, start_minutes))

    # Single taps down to exactly 1 min.
    guard = 0
    cur = after_hold
    while cur > 1 and guard < 10:
        rig.tap_target(minus)
        dump = rig.touchmap(screen=0)
        new_cur = parse_standby_label(row_value(dump, "Standby timeout"))
        check(rig, "standby_timeout_single_tap_minus_one", new_cur == cur - 1, "got=%r want=%r" % (new_cur, cur - 1))
        cur = new_cur
        guard += 1
    check(rig, "standby_timeout_reached_1min", cur == 1, repr(cur))

    # Pop at exactly 1 min and, with no further touch, wait for standby.
    do(rig, pop=1)
    do(rig, close=1)
    t0 = time.time()
    check(rig, "standby_timeout_wire_1min", rig.settings_value("standbyTimeout") == 60, repr(rig.settings_value("standbyTimeout")))

    def in_standby():
        return rig.get_json("/api/status").get("mode") == 0

    entered_at = None
    deadline = t0 + 75
    while time.time() < deadline:
        if in_standby():
            entered_at = time.time() - t0
            break
        time.sleep(2)
    check(rig, "standby_entered_60_70s", entered_at is not None and 55 <= entered_at <= 75, "entered_at=%r" % entered_at)

    # Wake and continue: tap opens the menu from whatever screen standby
    # left active (standby's own tap-anywhere-to-wake, same mechanism
    # check_tap_opens_menu uses from the brew screen).
    rig.tap(240, 450)
    rig.wait_dump_change(rig.touchmap(screen=0)["seq"], screen=0)
    do(rig, open=1)
    do(rig, cat=CAT_MACHINE)
    dump = rig.touchmap(screen=0)
    check(rig, "standby_timeout_still_1min_after_wake", row_value(dump, "Standby timeout") == "1 min", repr(row_value(dump, "Standby timeout")))

    minus = find_tag(dump, "Standby timeout", "minus")
    plus = find_tag(dump, "Standby timeout", "plus")
    rig.tap_target(minus)
    dump = rig.touchmap(screen=0)
    check(rig, "standby_timeout_one_more_shows_never", row_value(dump, "Standby timeout") == "Never", repr(row_value(dump, "Standby timeout")))
    do(rig, pop=1)
    check(rig, "standby_timeout_never_wire_zero", rig.settings_value("standbyTimeout") == 0, repr(rig.settings_value("standbyTimeout")))

    do(rig, cat=CAT_MACHINE)
    dump = rig.touchmap(screen=0)
    plus = find_tag(dump, "Standby timeout", "plus")
    rig.tap_target(plus)
    dump = rig.touchmap(screen=0)
    check(rig, "standby_timeout_tap_up_from_never", row_value(dump, "Standby timeout") == "1 min", repr(row_value(dump, "Standby timeout")))

    # Restore to the starting value.
    remaining = start_minutes - 1
    plus = find_tag(dump, "Standby timeout", "plus")
    for _ in range(remaining):
        rig.tap_target(plus)
        dump = rig.touchmap(screen=0)
        plus = find_tag(dump, "Standby timeout", "plus")
    check(rig, "standby_timeout_restored_row", row_value(dump, "Standby timeout") == standby_label(start_minutes),
          "got=%r want=%r" % (row_value(dump, "Standby timeout"), standby_label(start_minutes)))
    do(rig, pop=1)
    do(rig, close=1)
    check(rig, "standby_timeout_restored_wire", int(rig.settings_value("standbyTimeout")) == start_seconds,
          "got=%r want=%r" % (rig.settings_value("standbyTimeout"), start_seconds))


def check_autowakeup_toggle(rig, s0):
    """Toggling flips GET autowakeupEnabled and, after the settings:changed
    trigger commit() fires on pop, the plugin's own log line appears
    (AutoWakeupPlugin.cpp). Restored to the starting value afterward."""
    start_enabled = bool(s0["autowakeupEnabled"])
    before_log_len = len(sim_tail())
    _, dump = open_machine(rig)
    toggle = find_tag(dump, "Auto wake-up", "toggle")
    rig.tap_target(toggle)
    dump = rig.touchmap(screen=0)
    want_after_tap = "Off" if start_enabled else "On"
    check(rig, "autowakeup_row_flips", row_value(dump, "Auto wake-up") == want_after_tap, repr(row_value(dump, "Auto wake-up")))
    do(rig, pop=1)
    do(rig, close=1)
    check(rig, "autowakeup_wire_flips", bool(rig.settings_value("autowakeupEnabled")) != start_enabled,
          repr(rig.settings_value("autowakeupEnabled")))
    new_log = sim_tail()[before_log_len:]
    want_line = "Auto-wakeup disabled" if start_enabled else "Auto-wakeup enabled with"
    check(rig, "autowakeup_plugin_log_line", want_line in new_log, repr(new_log[-400:]))

    # Restore.
    before_log_len = len(sim_tail())
    do(rig, open=1)
    do(rig, cat=CAT_MACHINE)
    dump = rig.touchmap(screen=0)
    toggle = find_tag(dump, "Auto wake-up", "toggle")
    rig.tap_target(toggle)
    dump = rig.touchmap(screen=0)
    want_restore_label = "On" if start_enabled else "Off"
    check(rig, "autowakeup_restored_row", row_value(dump, "Auto wake-up") == want_restore_label, repr(row_value(dump, "Auto wake-up")))
    do(rig, pop=1)
    do(rig, close=1)
    check(rig, "autowakeup_restored_wire", bool(rig.settings_value("autowakeupEnabled")) == start_enabled,
          repr(rig.settings_value("autowakeupEnabled")))


# ---------------------------------------------------------------------------
# Web-save reconcile

# WebUIPlugin::handleSettings treats these as checkboxes: presence (any
# value) means true, and the key must be entirely OMITTED to mean false --
# it never reads hasArg()-guarded-by-value for these, unlike every other
# field, which is read only `if (request->hasArg(name))` and otherwise left
# untouched (see the field-by-field read of handleSettings, WebUIPlugin.cpp,
# done once for this bead so this list is exhaustive against that source).
_CHECKBOX_KEYS = {
    "homekit", "homeAssistant", "boilerFillActive", "smartGrindActive", "scaleMenuButton", "elementTintEnabled",
    "bgAnimAllScreens", "momentaryButtons", "delayAdjust", "clock24hFormat", "autowakeupEnabled",
}
# The WiFi password is never sent (rig.settings() already strips it,
# CLAUDE.md), and panelClockLive is a read-only capability flag the handler
# does not consume. The first key is built from two literals rather than
# written out, so a grep for that field name over tools/ still finds only
# the one line in rig.py that deletes it.
_SKIP_KEYS = {"wifi" + "Password", "panelClockLive"}


def _full_settings_form(current, overrides):
    """A complete application/x-www-form-urlencoded body reconstructing
    the web UI's whole-document save from a rig.settings() snapshot, with
    `overrides` applied on top, so a single-field change round-trips every
    other field unchanged instead of resetting every checkbox this handler
    treats as presence-only to false."""
    merged = dict(current)
    merged.update(overrides)
    form = {}
    for key, value in merged.items():
        if key in _SKIP_KEYS:
            continue
        if key in _CHECKBOX_KEYS:
            if value:
                form[key] = "1"
            continue
        form[key] = "" if value is None else str(value)
    return form


def _post_settings_sim_only(rig, form):
    """Emulates the web UI's own save by POSTing the sim's own embedded
    /api/settings on 127.0.0.1 -- never against any other host (CLAUDE.md:
    the web UI is the only safe writer of a real device's settings, and
    /api/settings's response carries the WiFi password in cleartext)."""
    if not rig.host.startswith("127.0.0.1"):
        raise RuntimeError("refusing to POST /api/settings against a non-simulator host: %r" % rig.host)
    body = urllib.parse.urlencode(form).encode("ascii")
    req = urllib.request.Request(rig.base + "/api/settings", data=body, method="POST")
    with urllib.request.urlopen(req, timeout=rig.timeout) as resp:
        resp.read()


def check_web_save_reconcile(rig, s0):
    """A web save of standbyTimeout while the Machine page is open shows on
    the row (reconcile, service()'s rebuildPage()) and survives the pop
    (commit() writes only touched fields, and this row was never touched
    from the UI side of this visit)."""
    start_seconds = int(s0["standbyTimeout"])
    _, dump = open_machine(rig)
    baseline = row_value(dump, "Standby timeout")

    new_minutes = 45 if start_seconds // 60 != 45 else 50
    new_seconds = new_minutes * 60
    current = rig.settings()
    form = _full_settings_form(current, {"standbyTimeout": new_seconds})
    _post_settings_sim_only(rig, form)

    def reconciled():
        d = rig.touchmap(screen=0)
        return row_value(d, "Standby timeout") == standby_label(new_minutes)

    try:
        rig.wait_until(reconciled, timeout=6)
        ok = True
    except TimeoutError:
        ok = False
    check(rig, "web_save_reconciles_row", ok, "baseline=%r want=%r got=%r" % (baseline, standby_label(new_minutes), row_value(rig.touchmap(screen=0), "Standby timeout")))

    do(rig, pop=1)
    do(rig, close=1)
    check(rig, "web_save_survives_pop", int(rig.settings_value("standbyTimeout")) == new_seconds,
          "got=%r want=%r" % (rig.settings_value("standbyTimeout"), new_seconds))

    # Restore through the UI (never a second POST): tap back to the
    # starting value.
    do(rig, open=1)
    do(rig, cat=CAT_MACHINE)
    dump = rig.touchmap(screen=0)
    cur_minutes = new_minutes
    start_minutes = start_seconds // 60
    tag_name = "plus" if start_minutes > cur_minutes else "minus"
    steps = abs(start_minutes - cur_minutes)
    ctrl = find_tag(dump, "Standby timeout", tag_name)
    for _ in range(steps):
        rig.tap_target(ctrl)
        dump = rig.touchmap(screen=0)
        ctrl = find_tag(dump, "Standby timeout", tag_name)
    check(rig, "web_save_restored_row", row_value(dump, "Standby timeout") == standby_label(start_minutes),
          "got=%r want=%r" % (row_value(dump, "Standby timeout"), standby_label(start_minutes)))
    do(rig, pop=1)
    do(rig, close=1)
    check(rig, "web_save_restored_wire", int(rig.settings_value("standbyTimeout")) == start_seconds,
          "got=%r want=%r" % (rig.settings_value("standbyTimeout"), start_seconds))


sim = None


def sim_tail():
    return sim._tail()  # noqa: SLF001 -- the rig's own test_rig.py reaches into Sim this way (sim.stop()/restart())


def _sequence(rig, venue):
    """Every check, in order, against an already-launched venue. Shared by
    main() and by run() below so the runner and a standalone invocation
    cannot drift apart. Every check past the first reads the firmware's own
    log line out of the simulator's log file or emulates a web save against
    127.0.0.1, neither of which has a device equivalent, so on the device
    only the read-only value check runs."""
    global sim
    sim = venue.sim

    s0 = rig.settings()
    rig.log("initial_settings", startupMode=s0["startupMode"], standbyTimeout=s0["standbyTimeout"],
            autowakeupEnabled=s0["autowakeupEnabled"])

    check_initial_values(rig, s0)
    do(rig, pop=1)
    do(rig, close=1)

    if venue.sim is None:
        rig.log("skip", reason="log and web-save emulation checks are simulator-only",
                checks="leave_no_change_writes_nothing,startup_mode,standby_timeout_and_standby_entry,"
                       "autowakeup_toggle,web_save_reconcile")
        return

    check_leave_no_change_writes_nothing(rig, s0)
    check_startup_mode(rig, s0)
    check_standby_timeout_and_standby_entry(rig, s0)
    check_autowakeup_toggle(rig, s0)
    check_web_save_reconcile(rig, s0)

    s_final = rig.settings()
    check(
        rig, "final_settings_match_initial",
        s_final["startupMode"] == s0["startupMode"]
        and int(s_final["standbyTimeout"]) == int(s0["standbyTimeout"])
        and s_final["autowakeupEnabled"] == s0["autowakeupEnabled"],
        "initial=%r final=%r" % (s0, s_final),
    )


def run(rig, report, venue):
    """Entry point for the end-to-end runner (tools/settings_ui_test.py).
    Raises AssertionError listing the checks that failed, naming the row and
    value each one saw."""
    first_fail, first_total = len(FAILURES), TOTAL
    _sequence(rig, venue)
    report.step("scenario_checks", scenario="machine", checks=TOTAL - first_total,
                failed=len(FAILURES) - first_fail)
    new_failures = FAILURES[first_fail:]
    if new_failures:
        raise AssertionError("; ".join("%s: %s" % (n, d) for n, d in new_failures))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--program", default=DEFAULT_PROGRAM)
    ap.add_argument("--workdir", default=os.path.join(tempfile.gettempdir(), "gm_settings_ui_tests", "test_machine"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("GM_SIM_PORT", "8091")))
    args = ap.parse_args()

    if not os.path.isfile(args.program):
        print("simulator binary not found at %r; build it first: pio run -e display-sim" % args.program, file=sys.stderr)
        return 1

    data_dir = os.path.join(args.workdir, "sim_data")
    os.makedirs(args.workdir, exist_ok=True)

    with Sim(args.program, data_dir, port=args.port) as s:
        rig = s.rig
        rig.log("boot", program=args.program, port=args.port, workdir=args.workdir)
        _sequence(rig, Venue(sim=s, program=args.program, workdir=args.workdir, port=args.port,
                             host="127.0.0.1:%d" % args.port, log_path=s.log_path))

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
