#!/usr/bin/env python3
"""Scenario for the auto wake-up schedule list and editor pages (gm-flw.11):
the Machine category's "Schedules" row, the list page (one row per
schedule plus "Add schedule") and the two-page editor (Hour, Minute, seven
day toggles, "Remove schedule"), all reachable only by tapping through from
the Machine page (list/editor are pushed pages, not entries in
/api/debug/settingsui's cat=N registry).

Built on tools/settings_ui_tests/rig.py (Rig, Sim), same shape as
test_machine.py and test_display.py. Runs against the desktop simulator by
default; --host points it at a running venue instead (the bench device, or
an already-running simulator) without launching one.

Usage:
    python3 tools/settings_ui_tests/test_schedules.py
        [--program PATH/to/.pio/build/display-sim/program]
        [--workdir DIR] [--port N] [--host ip[:port]]

Exits 0 if every check passes, 1 otherwise. Logs each step (Rig.log),
restores the schedule list to what it found before exiting (including two
fixtures it builds and tears down through the simulator-only web-save
emulation, the same mechanism the bead text sanctions for the acceptance
criteria's 9-entry import), and prints a PASS/FAIL summary at the end.

One check restarts the simulator. check_malformed_time_editor needs a
stored schedule with a malformed time, which no live route can write any
more, so it writes the simulator's own preferences file and relaunches the
process (plant_stored_schedules). Settings are flushed first, so the
restart loses nothing; the runner tolerates a scenario rebooting its venue
(close_shell in tools/settings_ui_test.py says so), and against a device
host the whole block is skipped.
"""
import argparse
import json
import os
import shutil
import sys
import tempfile
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO_ROOT)

from tools.settings_ui_tests import Rig, Sim, schedules, seconds  # noqa: E402
from tools.settings_ui_tests.rig import find_tag, row_value  # noqa: E402

DEFAULT_PROGRAM = os.path.join(REPO_ROOT, ".pio", "build", "display-sim", "program")

# Machine is category index 3 (0 Temps, 1 Display, 2 Animation, 3 Machine,
# 4 Status, 5 Fixture); the schedule list and editor are pushed pages
# beneath it and have no index of their own.
CAT_MACHINE = 3
SCREEN_ID_STANDBY_SCREEN = 1

FAILURES = []
TOTAL = 0


def check(rig, name, cond, detail=""):
    global TOTAL
    TOTAL += 1
    rig.log("check", name=name, ok=int(bool(cond)), detail=detail)
    if not cond:
        FAILURES.append((name, detail))
    return cond


def run_check(rig, name, fn, *args):
    """Runs one check function, turning an unexpected exception into a
    single recorded failure instead of aborting the whole scenario (a bug
    in one check must not hide every check after it). Returns fn's return
    value (None on exception), so a check that hands a fact forward to a
    later one (the throwaway schedule's position, for the remove check to
    clean up) still can."""
    try:
        return fn(rig, *args)
    except Exception as e:  # noqa: BLE001 -- a scenario check must never crash the run
        check(rig, name + "_exception", False, "%s: %s" % (type(e).__name__, e))
        return None


def do(rig, **cmd):
    """Issues one /api/debug/settingsui command (open/close/cat/pop) and
    waits for the UI task to finish executing it (settingsui_state()'s
    "seq" is the last completed command's seq; the route itself only
    reports that the command was queued)."""
    res = rig.settingsui(**cmd)
    seq = res["seq"]
    rig.wait_until(lambda: rig.settingsui_state().get("seq") == seq, timeout=8)
    return rig.settingsui_state()


def goto_page(rig, page):
    """page=N does not carry its own seq (SettingsUI::gotoPage never bumps
    one), so this waits on the published "page" field instead, same as
    test_animation.py's goto_page."""
    rig.settingsui(page=page)
    rig.wait_until(lambda: rig.settingsui_state().get("page") == page, timeout=5)
    return rig.touchmap(screen=0)


def is_web_save_host(rig):
    host = rig.host.split(":", 1)[0]
    return host in ("127.0.0.1", "localhost")


def web_save_change(rig, key, value):
    """Simulator-only web-save emulation (bead text): GET the current
    settings, override one key, POST the whole thing back to /api/settings
    in the form WebUIPlugin::handleSettings expects. Checkbox-style fields
    are included only when true (request->hasArg gates them, same
    convention the real web UI's buildSubmitFormData uses); every other
    field here is a plain string. Refuses anything but a 127.0.0.1
    simulator (CLAUDE.md: the web UI is the only safe writer of a real
    device's settings, and POSTing it by hand is never done)."""
    if not is_web_save_host(rig):
        raise RuntimeError("web_save_change refuses to POST /api/settings against %r (simulator only)" % rig.host)
    current = rig.settings()
    current[key] = value
    form = {}
    for k, v in current.items():
        if isinstance(v, bool):
            if v:
                form[k] = "1"
        else:
            form[k] = str(v)
    data = urllib.parse.urlencode(form).encode("utf-8")
    req = urllib.request.Request(rig.base + "/api/settings", data=data, method="POST")
    with urllib.request.urlopen(req, timeout=rig.timeout) as resp:
        return resp.status


def plant_stored_schedules(rig, sim, packed):
    """Writes `packed` into the simulator's stored settings and restarts it,
    returning the schedules string the restarted venue reports.

    For a fixture no live route can build any more. The web POST drops an
    entry whose time is not HH:MM in range (isScheduleTime, WebUIPlugin.cpp,
    2026-09-09) and the display's own commit always formats HH:MM, so
    settings stored before that change are the only way a malformed time
    still reaches the editor. The NVS codec keeps any entry that has a "|"
    in it (Settings.cpp), which is why such an entry survives the load.

    The simulator keeps its preferences as one JSON file per namespace
    (sim/platform/preferences_shim.cpp), so that file is this venue's NVS.
    Settings writes a key only when its value changes, so the marker save is
    what puts ab_schedules in the file at all; waiting for the file to carry
    the marker also proves the save task has flushed everything else this
    run has changed, which the terminate below would otherwise drop. The
    relaunch truncates the simulator's log, so the log so far is copied
    aside first."""
    nvs = os.path.join(sim.data_dir, "nvs", "controller.json")
    current = rig.settings_value("autowakeupSchedules")
    marker = "08:15|1010101" if current != "08:15|1010101" else "09:20|0101010"
    web_save_change(rig, "autowakeupSchedules", marker)

    def flushed():
        if not os.path.isfile(nvs):
            return False
        with open(nvs, "r", encoding="utf-8") as fp:
            return json.load(fp).get("ab_schedules") == marker

    rig.wait_until(flushed, timeout=15)
    with open(nvs, "r", encoding="utf-8") as fp:
        stored = json.load(fp)
    stored["ab_schedules"] = packed

    rig.log("plant_stored_schedules", packed=packed, nvs=nvs)
    sim.stop()
    try:
        shutil.copyfile(sim.log_path, sim.log_path + ".before_schedules_restart")
    except OSError:
        pass
    with open(nvs, "w", encoding="utf-8") as fp:
        json.dump(stored, fp)
    sim.restart()
    return rig.settings_value("autowakeupSchedules")


def py_days_summary(days):
    """Python mirror of SettingsModel.cpp's scheduleDaysSummary (host-tested
    in test_settings_model.cpp group G): "Every day"/"Never"/"Weekdays"/
    "Weekends", else the on days' three-letter abbreviations space-joined,
    Mon..Sun order."""
    abbrev = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    if all(days):
        return "Every day"
    if not any(days):
        return "Never"
    if all(days[:5]) and not any(days[5:]):
        return "Weekdays"
    if all(days[5:]) and not any(days[:5]):
        return "Weekends"
    return " ".join(abbrev[i] for i in range(7) if days[i])


def row_text(entry):
    return "%s %s" % (entry["time"], py_days_summary(entry["days"]))


def open_machine(rig):
    do(rig, open=1)
    do(rig, cat=CAT_MACHINE)
    return rig.touchmap(screen=0)


def open_schedule_list(rig):
    """From a freshly-opened Machine page, taps the "Schedules" row and
    waits for the list (depth 2) to land."""
    dump = open_machine(rig)
    row = find_tag(dump, "Schedules", "action")
    if row is None:
        return None
    rig.tap_target(row)
    rig.wait_until(lambda: rig.settingsui_state().get("depth") == 2, timeout=8)
    return rig.touchmap(screen=0)


def open_schedule_editor(rig, n, list_dump=None):
    """From the list page (or a fresh one, if list_dump is None), pages to
    wherever "Schedule n" (1-based) falls and taps it, waiting for the
    editor (depth 3) to land. Returns the editor's page-0 dump, or None if
    the row was never found."""
    if list_dump is None:
        list_dump = open_schedule_list(rig)
        if list_dump is None:
            return None
    name = "Schedule %d" % n
    page = (n - 1) // 5
    dump = goto_page(rig, page) if page != 0 else list_dump
    row = find_tag(dump, name, "action")
    if row is None:
        return None
    rig.tap_target(row)
    rig.wait_until(lambda: rig.settingsui_state().get("depth") == 3, timeout=8)
    return goto_page(rig, 0)


# ---------------------------------------------------------------------------
# Checks


def check_row_and_list_match_wire(rig, s0):
    """Acceptance: the "Schedules" row's value shows the count from
    /api/settings autowakeupSchedules; tapping it lists each schedule as
    "HH:MM " plus the days summary, matching that string, across however
    many pages the list needs."""
    wire = schedules(s0["autowakeupSchedules"])

    dump = open_machine(rig)
    got = row_value(dump, "Schedules")
    want = "%d schedules" % len(wire)
    check(rig, "schedules_row_shows_count", got == want, "got=%r want=%r" % (got, want))

    row = find_tag(dump, "Schedules", "action")
    check(rig, "schedules_row_present", row is not None, "")
    if row is None:
        do(rig, close=1)
        return
    rig.tap_target(row)
    rig.wait_until(lambda: rig.settingsui_state().get("depth") == 2, timeout=8)
    st = rig.settingsui_state()
    check(rig, "schedule_list_title", st.get("title") == "Schedules", repr(st))

    # The list is one row per schedule plus "Add schedule", five rows to a
    # page, so a one-entry list is a single page; asking goto_page for a
    # page the shell does not have never publishes that page number and
    # times out.
    want_pages = (len(wire) + 1 + 4) // 5
    check(rig, "list_page_count", st.get("pages") == want_pages, "got=%r want=%r" % (st.get("pages"), want_pages))

    seen = {}
    for page in range(want_pages):
        d = goto_page(rig, page)
        a = rig.audit(d)
        check(rig, "list_page%d_audit_clean" % page, not a["violations"], repr(a["violations"]))
        for i in range(len(wire)):
            name = "Schedule %d" % (i + 1)
            if name in seen:
                continue
            obj = find_tag(d, name, "value")
            if obj is not None:
                seen[name] = obj.get("val") or obj.get("t")
    for i, entry in enumerate(wire):
        name = "Schedule %d" % (i + 1)
        want = row_text(entry)
        got = seen.get(name)
        check(rig, "list_row_%d_text" % (i + 1), got == want, "row=%r got=%r want=%r" % (name, got, want))

    do(rig, close=1)


def check_hour_minute_wrap(rig, s0):
    """Acceptance: Hour wraps 23->0 and 0->23, Minute wraps 59->0. Uses its
    own throwaway schedule (default 07:00) so the wrap taps never touch a
    real entry, and removes it again afterward."""
    before = schedules(s0["autowakeupSchedules"])
    n = len(before) + 1

    list_dump = open_schedule_list(rig)
    if list_dump is None:
        check(rig, "wrap_list_opens", False, "")
        return
    page = (n - 1) // 5  # "Add schedule" is the list's last row, 0-based index n-1
    dump = goto_page(rig, page) if page != 0 else list_dump
    add_row = find_tag(dump, "Add schedule", "action")
    if not check(rig, "wrap_add_row_present", add_row is not None, ""):
        do(rig, close=1)
        return
    rig.tap_target(add_row)
    dump = rig.touchmap(screen=0)
    row = find_tag(dump, "Schedule %d" % n, "action")
    if not check(rig, "wrap_new_row_present", row is not None, ""):
        do(rig, close=1)
        return
    rig.tap_target(row)
    rig.wait_until(lambda: rig.settingsui_state().get("depth") == 3, timeout=8)
    dump = goto_page(rig, 0)

    # Default Hour is 07: 8 single taps on minus reach 23 (07->06->...->00,
    # the 8th tap wrapping 0 -> 23); one more plus tap then wraps 23 -> 0.
    last = None
    for _ in range(8):
        minus = find_tag(dump, "Hour", "minus")
        rig.tap_target(minus)
        dump = rig.touchmap(screen=0)
        last = row_value(dump, "Hour")
    check(rig, "hour_wraps_0_to_23", last == "23", repr(last))

    plus = find_tag(dump, "Hour", "plus")
    rig.tap_target(plus)
    dump = rig.touchmap(screen=0)
    check(rig, "hour_wraps_23_to_0", row_value(dump, "Hour") == "00", repr(row_value(dump, "Hour")))

    # Default Minute is 00: one minus tap wraps 0 -> 59, one plus tap wraps
    # it back to 0.
    minus = find_tag(dump, "Minute", "minus")
    rig.tap_target(minus)
    dump = rig.touchmap(screen=0)
    check(rig, "minute_wraps_0_to_59", row_value(dump, "Minute") == "59", repr(row_value(dump, "Minute")))
    plus = find_tag(dump, "Minute", "plus")
    rig.tap_target(plus)
    dump = rig.touchmap(screen=0)
    check(rig, "minute_wraps_59_to_0", row_value(dump, "Minute") == "00", repr(row_value(dump, "Minute")))

    # Clean up: remove the throwaway schedule (2500ms hold) and pop out.
    dump = goto_page(rig, 1)
    remove_row = find_tag(dump, "Remove schedule", "confirm")
    rig.tap_target(remove_row, ms=2500)
    rig.wait_until(lambda: rig.settingsui_state().get("depth") == 2, timeout=8)
    do(rig, pop=1)
    do(rig, close=1)

    def restored():
        return schedules(rig.settings_value("autowakeupSchedules")) == before

    try:
        rig.wait_until(restored, timeout=6)
        ok = True
    except TimeoutError:
        ok = False
    check(rig, "wrap_check_restores_list", ok, repr(schedules(rig.settings_value("autowakeupSchedules"))))


def check_add_edit_and_pop_persists(rig, s0):
    """Acceptance: Add creates a "07:00 Every day" row; one minus tap on
    Hour shows 06, a 1000ms hold on Minute plus then single taps reach 30,
    toggling Saturday off, then popping twice to the Machine page and once
    more, persists "06:30|1111101" as the new last entry within 6s with
    every other entry unchanged and in order. Also audits both editor
    pages. Returns the new schedule's position (1-based) for
    check_remove_restores to clean up, or None if it could not get that
    far."""
    before = schedules(s0["autowakeupSchedules"])
    n = len(before) + 1

    list_dump = open_schedule_list(rig)
    if not check(rig, "add_list_opens", list_dump is not None, ""):
        return None
    page = (n - 1) // 5
    dump = goto_page(rig, page) if page != 0 else list_dump
    add_row = find_tag(dump, "Add schedule", "action")
    if not check(rig, "add_row_present", add_row is not None, ""):
        do(rig, close=1)
        return None
    rig.tap_target(add_row)
    dump = rig.touchmap(screen=0)
    new_name = "Schedule %d" % n
    obj = find_tag(dump, new_name, "value")
    got_new = (obj.get("val") or obj.get("t")) if obj is not None else None
    check(rig, "add_creates_default_row", got_new == "07:00 Every day", "got=%r" % (got_new,))

    row = find_tag(dump, new_name, "action")
    if not check(rig, "add_new_row_present", row is not None, ""):
        do(rig, close=1)
        return None
    rig.tap_target(row)
    rig.wait_until(lambda: rig.settingsui_state().get("depth") == 3, timeout=8)
    st = rig.settingsui_state()
    check(rig, "editor_title_matches_position", st.get("title") == new_name, repr(st))

    dump = goto_page(rig, 0)  # Hour, Minute, Monday, Tuesday, Wednesday
    a0 = rig.audit(dump)
    check(rig, "editor_page0_audit_clean", not a0["violations"], repr(a0["violations"]))

    minus = find_tag(dump, "Hour", "minus")
    rig.tap_target(minus)
    dump = rig.touchmap(screen=0)
    check(rig, "hour_one_minus_tap_is_06", row_value(dump, "Hour") == "06", repr(row_value(dump, "Hour")))

    plus = find_tag(dump, "Minute", "plus")
    rig.tap_target(plus, ms=1000)
    dump = rig.touchmap(screen=0)
    try:
        after_hold = int(row_value(dump, "Minute"))
    except ValueError:
        after_hold = -1
    # Wide window, not an exact cadence (shared contract: LVGL delivers at
    # most one LONG_PRESSED_REPEAT per lv_task_handler pass).
    check(rig, "minute_hold_1000ms_moved", 1 <= after_hold <= 30, "got=%r" % after_hold)
    guard = 0
    cur = after_hold
    while cur != 30 and guard < 40:
        plus = find_tag(dump, "Minute", "plus")
        rig.tap_target(plus)
        dump = rig.touchmap(screen=0)
        cur = int(row_value(dump, "Minute"))
        guard += 1
    check(rig, "minute_reaches_30", cur == 30, repr(cur))

    dump = goto_page(rig, 1)  # Thursday, Friday, Saturday, Sunday, Remove schedule
    a1 = rig.audit(dump)
    check(rig, "editor_page1_audit_clean", not a1["violations"], repr(a1["violations"]))
    sat = find_tag(dump, "Saturday", "toggle")
    rig.tap_target(sat)
    dump = rig.touchmap(screen=0)
    check(rig, "saturday_off", row_value(dump, "Saturday") == "Off", repr(row_value(dump, "Saturday")))

    do(rig, pop=1)  # editor -> list
    do(rig, pop=1)  # list -> Machine
    st = rig.settingsui_state()
    check(rig, "two_pops_land_on_machine", st.get("depth") == 1, repr(st))
    do(rig, pop=1)  # Machine -> tiles: commit persists the touched schedules

    def reconciled():
        wire = schedules(rig.settings_value("autowakeupSchedules"))
        return len(wire) == n and wire[-1] == {
            "time": "06:30",
            "days": [True, True, True, True, True, False, True],
        }

    try:
        rig.wait_until(reconciled, timeout=6)
        ok = True
    except TimeoutError:
        ok = False
    after = schedules(rig.settings_value("autowakeupSchedules"))
    check(rig, "wire_has_new_entry_within_6s", ok, repr(after))
    check(
        rig, "other_entries_unchanged_and_ordered", after[: len(before)] == before,
        "before=%r after=%r" % (before, after),
    )

    do(rig, close=1)
    return n


def check_remove_restores(rig, s0, n):
    """Acceptance: a 2500ms hold on "Remove schedule" in the throwaway
    schedule's editor (added and persisted by check_add_edit_and_pop_persists)
    returns to the list without it; popping out shows the original list
    again."""
    before = schedules(s0["autowakeupSchedules"])

    dump = open_schedule_editor(rig, n)
    if not check(rig, "remove_target_editor_opens", dump is not None, ""):
        return
    dump = goto_page(rig, 1)
    remove_row = find_tag(dump, "Remove schedule", "confirm")
    rig.tap_target(remove_row, ms=2500)
    try:
        rig.wait_until(lambda: rig.settingsui_state().get("depth") == 2, timeout=8)
        ok = True
    except TimeoutError:
        ok = False
    check(rig, "remove_pops_to_list", ok, repr(rig.settingsui_state()))

    do(rig, pop=1)  # list -> Machine
    do(rig, close=1)  # Machine -> closed: commits the removal

    def restored():
        return schedules(rig.settings_value("autowakeupSchedules")) == before

    try:
        rig.wait_until(restored, timeout=6)
        ok = True
    except TimeoutError:
        ok = False
    after = schedules(rig.settings_value("autowakeupSchedules"))
    check(rig, "remove_restores_original_list", ok, "before=%r after=%r" % (before, after))


def check_remove_disabled_at_one(rig, s0):
    """Acceptance: with one schedule left, "Remove schedule" is disabled
    and a 2500ms hold does nothing. Uses a one-entry web-save fixture
    (simulator only) so this is independent of how many schedules the
    venue actually starts with, and restores the original list through the
    same mechanism afterward."""
    orig_packed = s0["autowakeupSchedules"]
    single_packed = "09:15|1111111"
    web_save_change(rig, "autowakeupSchedules", single_packed)
    check(rig, "single_fixture_wire", rig.settings_value("autowakeupSchedules") == single_packed, "")

    dump = open_schedule_editor(rig, 1)
    if not check(rig, "single_editor_opens", dump is not None, ""):
        web_save_change(rig, "autowakeupSchedules", orig_packed)
        return
    dump = goto_page(rig, 1)
    a1 = rig.audit(dump)
    check(rig, "one_schedule_editor_page1_audit_clean", not a1["violations"], repr(a1["violations"]))

    remove_row = find_tag(dump, "Remove schedule", "confirm")
    rig.tap_target(remove_row, ms=2500)
    st = rig.settingsui_state()
    check(rig, "remove_disabled_stays_in_editor", st.get("depth") == 3, repr(st))
    check(
        rig, "remove_disabled_noop_wire", rig.settings_value("autowakeupSchedules") == single_packed,
        repr(rig.settings_value("autowakeupSchedules")),
    )

    do(rig, close=1)
    web_save_change(rig, "autowakeupSchedules", orig_packed)
    check(rig, "single_fixture_restored", rig.settings_value("autowakeupSchedules") == orig_packed, "")


def check_eight_and_nine_and_audit(rig, s0):
    """Acceptance: with 8 schedules "Add schedule" is disabled; a stored
    list of 9 (created through the web UI beforehand, here the simulator-
    only web-save emulation the bead text sanctions for exactly this) shows
    all 9 and leaving without edits leaves the stored string byte-
    identical. Also audits both list pages at 8 and at 9."""
    orig_packed = s0["autowakeupSchedules"]

    eight_entries = [("%02d:00" % h, "1111111") for h in range(8)]
    eight_packed = ";".join("%s|%s" % e for e in eight_entries)
    web_save_change(rig, "autowakeupSchedules", eight_packed)
    check(rig, "eight_fixture_wire", rig.settings_value("autowakeupSchedules") == eight_packed, "")

    dump = open_machine(rig)
    check(rig, "eight_row_value", row_value(dump, "Schedules") == "8 schedules", repr(row_value(dump, "Schedules")))
    row = find_tag(dump, "Schedules", "action")
    if not check(rig, "eight_schedules_row_present", row is not None, ""):
        do(rig, close=1)
        web_save_change(rig, "autowakeupSchedules", orig_packed)
        return
    rig.tap_target(row)
    rig.wait_until(lambda: rig.settingsui_state().get("depth") == 2, timeout=8)

    d0 = goto_page(rig, 0)
    a0 = rig.audit(d0)
    check(rig, "eight_page0_audit_clean", not a0["violations"], repr(a0["violations"]))
    check(
        rig, "eight_page0_rows", all(find_tag(d0, "Schedule %d" % k, "action") is not None for k in range(1, 6)), "",
    )

    d1 = goto_page(rig, 1)
    a1 = rig.audit(d1)
    check(rig, "eight_page1_audit_clean", not a1["violations"], repr(a1["violations"]))
    check(
        rig, "eight_page1_rows", all(find_tag(d1, "Schedule %d" % k, "action") is not None for k in range(6, 9)), "",
    )

    add_row = find_tag(d1, "Add schedule", "action")
    check(rig, "add_row_present_at_eight", add_row is not None, "")
    if add_row is not None:
        rig.tap_target(add_row)
    d_after = rig.touchmap(screen=0)
    check(rig, "add_disabled_at_eight_noop", find_tag(d_after, "Schedule 9", "action") is None, "")

    nine_packed = eight_packed + ";23:45|0101010"
    web_save_change(rig, "autowakeupSchedules", nine_packed)
    check(rig, "nine_fixture_wire", rig.settings_value("autowakeupSchedules") == nine_packed, "")

    def nine_visible():
        d = goto_page(rig, 1)
        return find_tag(d, "Schedule 9", "action") is not None

    try:
        rig.wait_until(nine_visible, timeout=6)
        ok = True
    except TimeoutError:
        ok = False
    check(rig, "nine_reconciles_into_list", ok, "")

    d0 = goto_page(rig, 0)
    a0 = rig.audit(d0)
    check(rig, "nine_page0_audit_clean", not a0["violations"], repr(a0["violations"]))
    d1 = goto_page(rig, 1)
    a1 = rig.audit(d1)
    check(rig, "nine_page1_audit_clean", not a1["violations"], repr(a1["violations"]))
    present = all(find_tag(d0 if k <= 5 else d1, "Schedule %d" % k, "action") is not None for k in range(1, 10))
    check(rig, "nine_all_shown", present, "")

    do(rig, pop=1)  # list -> Machine
    do(rig, close=1)  # Machine -> closed: schedulesTouched was never set, so no write
    check(
        rig, "nine_left_byte_identical", rig.settings_value("autowakeupSchedules") == nine_packed,
        repr(rig.settings_value("autowakeupSchedules")),
    )

    web_save_change(rig, "autowakeupSchedules", orig_packed)
    check(rig, "eight_nine_restored", rig.settings_value("autowakeupSchedules") == orig_packed, "")


def check_web_save_reconcile_editor(rig, s0):
    """Acceptance: a web save that shortens the list to one entry while the
    editor of the last (now-missing) entry is open, untouched, pops the
    editor to the one-row list without a crash and without writing
    autowakeupSchedules itself; the same save while the editor has an edit
    leaves the draft alone (schedulesTouched skips the reconcile replace),
    and the eventual pop persists the display's own edited list, not the
    web save's. Uses a deterministic two-entry web-save fixture so this is
    independent of the venue's starting list, restored afterward the same
    way."""
    orig_packed = s0["autowakeupSchedules"]
    two_packed = "05:00|1111111;10:00|1111111"
    one_packed = "05:00|1111111"

    # --- untouched: reconcile pops the editor -----------------------------
    web_save_change(rig, "autowakeupSchedules", two_packed)
    check(rig, "reconcile_setup_two", rig.settings_value("autowakeupSchedules") == two_packed, "")

    dump = open_schedule_editor(rig, 2)
    if not check(rig, "reconcile_editor2_opens", dump is not None, ""):
        web_save_change(rig, "autowakeupSchedules", orig_packed)
        return

    web_save_change(rig, "autowakeupSchedules", one_packed)
    try:
        rig.wait_until(lambda: rig.settingsui_state().get("depth") == 2, timeout=8)
        ok = True
    except TimeoutError:
        ok = False
    check(rig, "reconcile_untouched_pops_to_list", ok, repr(rig.settingsui_state()))
    check(
        rig, "reconcile_untouched_no_extra_write", rig.settings_value("autowakeupSchedules") == one_packed,
        repr(rig.settings_value("autowakeupSchedules")),
    )

    do(rig, pop=1)  # list -> Machine
    do(rig, close=1)  # Machine -> closed: nothing touched, no-op commit
    check(
        rig, "reconcile_untouched_final_wire", rig.settings_value("autowakeupSchedules") == one_packed,
        repr(rig.settings_value("autowakeupSchedules")),
    )

    # --- touched: reconcile leaves the draft alone; the pop wins ----------
    web_save_change(rig, "autowakeupSchedules", two_packed)
    check(rig, "reconcile_setup_two_again", rig.settings_value("autowakeupSchedules") == two_packed, "")

    dump = open_schedule_editor(rig, 2)
    if not check(rig, "reconcile_editor2_reopens", dump is not None, ""):
        web_save_change(rig, "autowakeupSchedules", orig_packed)
        return
    minus = find_tag(dump, "Hour", "minus")
    rig.tap_target(minus)  # 10 -> 09, marks schedulesTouched
    dump = rig.touchmap(screen=0)
    check(rig, "reconcile_touch_applied", row_value(dump, "Hour") == "09", repr(row_value(dump, "Hour")))

    web_save_change(rig, "autowakeupSchedules", one_packed)
    time.sleep(1.0)  # give a reconcile pass the chance to run, if it were going to
    st = rig.settingsui_state()
    check(rig, "reconcile_touched_stays_in_editor", st.get("depth") == 3, repr(st))
    dump = rig.touchmap(screen=0)
    check(rig, "reconcile_touched_draft_unchanged", row_value(dump, "Hour") == "09", repr(row_value(dump, "Hour")))

    do(rig, pop=1)  # editor -> list: writes nothing, commit is nullptr
    do(rig, pop=1)  # list -> Machine
    do(rig, close=1)  # Machine -> closed: commits the touched (edited) draft

    def touched_wins():
        wire = schedules(rig.settings_value("autowakeupSchedules"))
        return wire == schedules("05:00|1111111;09:00|1111111")

    try:
        rig.wait_until(touched_wins, timeout=6)
        ok = True
    except TimeoutError:
        ok = False
    after = schedules(rig.settings_value("autowakeupSchedules"))
    check(rig, "reconcile_pop_writes_display_list", ok, repr(after))

    web_save_change(rig, "autowakeupSchedules", orig_packed)
    check(rig, "reconcile_restored", rig.settings_value("autowakeupSchedules") == orig_packed, "")


def check_malformed_time_editor(rig, s0, sim):
    """Regression for the substr crash, and for the web save that now keeps
    the crash out of reach.

    "|1111111" is a stored entry with an empty time. The editor's Hour and
    Minute rows used to slice that string, and substr(3, 2) on an empty one
    throws out_of_range, which aborts a firmware built without exceptions.
    Both rows read it through the model's scheduleTimeParts now, which
    treats anything malformed as midnight, so the editor must open on 00 and
    00 and the venue must still be answering afterwards. Leaving the entry
    untouched must write nothing.

    The web handler drops a malformed time. Since 2026-09-09 isScheduleTime
    (WebUIPlugin.cpp) keeps only entries whose time is HH:MM in range, so a
    POST of "07:00|1111111;|1111111" is stored as "07:00|1111111". This
    check used to assert that the POST came back unchanged, and failed on
    every run once that landed (gm-tany.6). Do not put that assertion back.
    The first half below asserts the drop instead. The second plants the
    malformed entry the way a real device still carries one, through stored
    settings written before that change (plant_stored_schedules), because
    the editor has to survive reading it either way."""
    orig_packed = s0["autowakeupSchedules"]
    broken_packed = "07:00|1111111;|1111111"
    web_save_change(rig, "autowakeupSchedules", broken_packed)
    got_post = rig.settings_value("autowakeupSchedules")
    check(rig, "malformed_web_post_drops_entry", got_post == "07:00|1111111", repr(got_post))

    if sim is None:
        rig.log("skip", reason="planting a malformed stored time needs the simulator process",
                checks="malformed_fixture_planted,malformed_editor_opens,malformed_hour_is_00,"
                       "malformed_minute_is_00,malformed_editor_still_open,malformed_left_byte_identical")
        web_save_change(rig, "autowakeupSchedules", orig_packed)
        return

    got_fixture = plant_stored_schedules(rig, sim, broken_packed)
    if not check(rig, "malformed_fixture_planted", got_fixture == broken_packed, repr(got_fixture)):
        web_save_change(rig, "autowakeupSchedules", orig_packed)
        return

    dump = open_schedule_editor(rig, 2)
    if not check(rig, "malformed_editor_opens", dump is not None, ""):
        web_save_change(rig, "autowakeupSchedules", orig_packed)
        return
    a0 = rig.audit(dump)
    check(rig, "malformed_editor_page0_audit_clean", not a0["violations"], repr(a0["violations"]))
    check(rig, "malformed_hour_is_00", row_value(dump, "Hour") == "00", repr(row_value(dump, "Hour")))
    check(rig, "malformed_minute_is_00", row_value(dump, "Minute") == "00", repr(row_value(dump, "Minute")))

    # A crash would show up as a refused connection here, not as a wrong
    # value: the state read is what proves the venue is still running.
    st = rig.settingsui_state()
    check(rig, "malformed_editor_still_open", st.get("depth") == 3, repr(st))

    do(rig, pop=1)  # editor -> list
    do(rig, pop=1)  # list -> Machine
    do(rig, close=1)  # nothing touched, so commit writes no schedules
    after = rig.settings_value("autowakeupSchedules")
    check(rig, "malformed_left_byte_identical", after == broken_packed, repr(after))

    web_save_change(rig, "autowakeupSchedules", orig_packed)
    check(rig, "malformed_fixture_restored", rig.settings_value("autowakeupSchedules") == orig_packed, "")


def check_web_save_reconciles_machine_row(rig, s0):
    """Regression for the top-page-only reconcile: SettingsUI::service()
    calls reconcile on the top page alone, and the schedule pages used to
    refresh only the draft's schedules. A web save that changed startup
    mode, standby timeout or auto wake-up while a schedule page was open
    therefore never reached the Machine draft: the Machine page showed the
    pre-save value when the schedule page popped, and the next tap on that
    row marked the field touched, so the visit's commit wrote the stale
    draft over the web edit. Both schedule pages now run the Machine
    category's whole untouched-field pass (machineDraftReconcile,
    CatMachine.h).

    Driven through standbyTimeout, whose wire value is in seconds and whose
    row reads "<minutes> min" (kStandbyTimeoutSpec: step 60000 ms, so one
    plus tap is one minute). Two visits: the first leaves the row alone and
    checks the displayed value plus that leaving writes nothing, the second
    taps the row once and checks the write starts from the web value rather
    than from the stale draft."""
    base = seconds(s0["standbyTimeout"])
    first, second = 900, 1200
    if base in (first, second):
        first, second = 1500, 1800

    def row_text_for(sec):
        return "%d min" % (sec // 60)

    # --- visit 1: untouched, the row must show the web value on pop -------
    dump = open_schedule_editor(rig, 1)
    if not check(rig, "machine_reconcile_editor_opens", dump is not None, ""):
        web_save_change(rig, "standbyTimeout", str(base))
        return
    web_save_change(rig, "standbyTimeout", str(first))
    try:
        rig.wait_until(lambda: seconds(rig.settings_value("standbyTimeout")) == first, timeout=6)
    except TimeoutError:
        pass
    time.sleep(1.0)  # let the shell's next pass consume settings:changed and reconcile
    st = rig.settingsui_state()
    check(rig, "machine_reconcile_editor_stays_open", st.get("depth") == 3, repr(st))

    do(rig, pop=1)  # editor -> list
    do(rig, pop=1)  # list -> Machine
    dump = goto_page(rig, 0)
    got = row_value(dump, "Standby timeout")
    check(
        rig, "machine_row_shows_web_value_after_pop", got == row_text_for(first),
        "got=%r want=%r" % (got, row_text_for(first)),
    )

    do(rig, close=1)  # untouched, so commit must not write standbyTimeout
    time.sleep(1.0)
    got_wire = seconds(rig.settings_value("standbyTimeout"))
    check(rig, "machine_untouched_leave_writes_nothing", got_wire == first, "got=%r want=%r" % (got_wire, first))

    # --- visit 2: one tap after the save must start from the web value ----
    dump = open_schedule_editor(rig, 1)
    if not check(rig, "machine_reconcile_editor_reopens", dump is not None, ""):
        web_save_change(rig, "standbyTimeout", str(base))
        return
    web_save_change(rig, "standbyTimeout", str(second))
    try:
        rig.wait_until(lambda: seconds(rig.settings_value("standbyTimeout")) == second, timeout=6)
    except TimeoutError:
        pass
    time.sleep(1.0)

    do(rig, pop=1)  # editor -> list
    do(rig, pop=1)  # list -> Machine
    dump = goto_page(rig, 0)
    plus = find_tag(dump, "Standby timeout", "plus")
    if not check(rig, "machine_standby_plus_present", plus is not None, ""):
        do(rig, close=1)
        web_save_change(rig, "standbyTimeout", str(base))
        return
    rig.tap_target(plus)
    dump = rig.touchmap(screen=0)
    want_row = row_text_for(second + 60)
    got = row_value(dump, "Standby timeout")
    check(rig, "machine_step_starts_from_web_value", got == want_row, "got=%r want=%r" % (got, want_row))

    do(rig, close=1)  # touched now, so commit writes the stepped value

    def stepped():
        return seconds(rig.settings_value("standbyTimeout")) == second + 60

    try:
        rig.wait_until(stepped, timeout=6)
        ok = True
    except TimeoutError:
        ok = False
    got_wire = seconds(rig.settings_value("standbyTimeout"))
    check(rig, "machine_step_does_not_overwrite_web_save", ok, "got=%r want=%r" % (got_wire, second + 60))

    web_save_change(rig, "standbyTimeout", str(base))
    check(
        rig, "machine_standby_restored", seconds(rig.settings_value("standbyTimeout")) == base,
        repr(rig.settings_value("standbyTimeout")),
    )


def check_external_leave_persists(rig, s0):
    """Acceptance: an external leave while an editor page is open persists
    the edits made so far. Forces the leave the fast way: a touchmap
    request for a different screen with load=1 makes DefaultUI::
    handleScreenChange call SettingsUI::onExternalLeave() before the
    screen actually changes (same teardown close() uses), rather than
    waiting out a real standby timeout as test_machine.py's standby-entry
    check does."""
    before = schedules(s0["autowakeupSchedules"])
    n = len(before) + 1

    list_dump = open_schedule_list(rig)
    if not check(rig, "leave_list_opens", list_dump is not None, ""):
        return
    page = (n - 1) // 5
    dump = goto_page(rig, page) if page != 0 else list_dump
    add_row = find_tag(dump, "Add schedule", "action")
    if not check(rig, "leave_add_row_present", add_row is not None, ""):
        do(rig, close=1)
        return
    rig.tap_target(add_row)
    dump = rig.touchmap(screen=0)
    row = find_tag(dump, "Schedule %d" % n, "action")
    if not check(rig, "leave_new_row_present", row is not None, ""):
        do(rig, close=1)
        return
    rig.tap_target(row)
    rig.wait_until(lambda: rig.settingsui_state().get("depth") == 3, timeout=8)
    dump = rig.touchmap(screen=0)
    minus = find_tag(dump, "Hour", "minus")
    rig.tap_target(minus)  # 07 -> 06, marks schedulesTouched
    dump = rig.touchmap(screen=0)
    check(rig, "leave_edit_applied", row_value(dump, "Hour") == "06", repr(row_value(dump, "Hour")))

    rig.touchmap(screen=SCREEN_ID_STANDBY_SCREEN, load=True)
    st = rig.settingsui_state()
    check(rig, "leave_closes_shell", st.get("open") is False, repr(st))

    def leave_persisted():
        wire = schedules(rig.settings_value("autowakeupSchedules"))
        return len(wire) == n and wire[-1]["time"] == "06:00"

    try:
        rig.wait_until(leave_persisted, timeout=6)
        ok = True
    except TimeoutError:
        ok = False
    after = schedules(rig.settings_value("autowakeupSchedules"))
    check(rig, "external_leave_persists_edit", ok, repr(after))

    # Wake (tap-anywhere) and remove the throwaway schedule, restoring the
    # original list.
    rig.tap(240, 450)
    rig.wait_dump_change(rig.touchmap(screen=0)["seq"], screen=0)
    dump = open_schedule_editor(rig, n)
    if dump is not None:
        dump = goto_page(rig, 1)
        remove_row = find_tag(dump, "Remove schedule", "confirm")
        rig.tap_target(remove_row, ms=2500)
        rig.wait_until(lambda: rig.settingsui_state().get("depth") == 2, timeout=8)
        do(rig, pop=1)
    else:
        check(rig, "leave_cleanup_row_present", False, "")
    do(rig, close=1)

    def restored():
        return schedules(rig.settings_value("autowakeupSchedules")) == before

    try:
        rig.wait_until(restored, timeout=6)
        ok = True
    except TimeoutError:
        ok = False
    check(rig, "leave_check_restores_list", ok, repr(schedules(rig.settings_value("autowakeupSchedules"))))


def _sequence(rig, venue=None, sim=None):
    """Every check, in order. Shared by main() and by run() below so the
    runner and a standalone invocation cannot drift apart. `sim` is the
    simulator process this is driving, where there is one: the malformed
    time check restarts it to plant a fixture no live route can write any
    more, so that check is skipped without it (a device host, which
    is_web_save_host skips anyway)."""
    if sim is None:
        sim = getattr(venue, "sim", None)
    s0 = rig.settings()
    rig.log("initial_settings", autowakeupSchedules=s0["autowakeupSchedules"])

    run_check(rig, "row_and_list_match_wire", check_row_and_list_match_wire, s0)
    run_check(rig, "hour_minute_wrap", check_hour_minute_wrap, s0)
    n = run_check(rig, "add_edit_persist", check_add_edit_and_pop_persists, s0)
    if n is not None:
        run_check(rig, "remove_restores", check_remove_restores, s0, n)
    if is_web_save_host(rig):
        run_check(rig, "remove_disabled_at_one", check_remove_disabled_at_one, s0)
        run_check(rig, "eight_and_nine_and_audit", check_eight_and_nine_and_audit, s0)
        run_check(rig, "web_save_reconcile_editor", check_web_save_reconcile_editor, s0)
        run_check(rig, "malformed_time_editor", check_malformed_time_editor, s0, sim)
        run_check(rig, "web_save_reconciles_machine_row", check_web_save_reconciles_machine_row, s0)
    else:
        rig.log(
            "skip", reason="web-save emulation is simulator-only",
            checks="remove_disabled_at_one,eight_and_nine_and_audit,web_save_reconcile_editor,"
                   "malformed_time_editor,web_save_reconciles_machine_row",
        )
    run_check(rig, "external_leave_persists", check_external_leave_persists, s0)

    s_final = rig.settings()
    same = schedules(s_final["autowakeupSchedules"]) == schedules(s0["autowakeupSchedules"])
    if not same and is_web_save_host(rig):
        # Safety net, not the primary restoration path: every check
        # above restores through the UI (or, where noted, the
        # bead-sanctioned web-save emulation) on its own before
        # returning; this only fires if one of them left the list
        # dirty despite that.
        rig.log("restore_safety_net", before=s0["autowakeupSchedules"], after=s_final["autowakeupSchedules"])
        web_save_change(rig, "autowakeupSchedules", s0["autowakeupSchedules"])
    check(
        rig, "final_schedules_match_initial",
        schedules(rig.settings_value("autowakeupSchedules")) == schedules(s0["autowakeupSchedules"]),
        "before=%r after=%r" % (s0["autowakeupSchedules"], rig.settings_value("autowakeupSchedules")),
    )
    # check_web_save_reconciles_machine_row is the only check here that
    # moves a Machine field other than the schedules, and it restores
    # standbyTimeout itself; this is the standing proof that it did.
    check(
        rig, "final_standby_timeout_matches_initial",
        seconds(rig.settings_value("standbyTimeout")) == seconds(s0["standbyTimeout"]),
        "before=%r after=%r" % (s0["standbyTimeout"], rig.settings_value("standbyTimeout")),
    )


def run(rig, report, venue):
    """Entry point for the end-to-end runner (tools/settings_ui_test.py).
    Raises AssertionError listing the checks that failed, naming the row and
    value each one saw."""
    first_fail, first_total = len(FAILURES), TOTAL
    _sequence(rig, venue)
    report.step("scenario_checks", scenario="schedules", checks=TOTAL - first_total,
                failed=len(FAILURES) - first_fail)
    new_failures = FAILURES[first_fail:]
    if new_failures:
        raise AssertionError("; ".join("%s: %s" % (n, d) for n, d in new_failures))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--program", default=DEFAULT_PROGRAM)
    ap.add_argument("--workdir", default=os.path.join(tempfile.gettempdir(), "gm_settings_ui_tests", "test_schedules"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("GM_SIM_PORT", "8093")))
    ap.add_argument("--host", default=None, help="run against a device/loadtest build instead of the simulator")
    args = ap.parse_args()

    if args.host:
        rig = Rig(args.host)
        rig.log("boot", host=args.host)
        _sequence(rig)
    else:
        if not os.path.isfile(args.program):
            print("simulator binary not found at %r; build it first: pio run -e display-sim" % args.program, file=sys.stderr)
            return 1
        data_dir = os.path.join(args.workdir, "sim_data")
        os.makedirs(args.workdir, exist_ok=True)
        with Sim(args.program, data_dir, port=args.port) as sim:
            rig = sim.rig
            rig.log("boot", program=args.program, port=args.port, workdir=args.workdir)
            _sequence(rig, sim=sim)

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

# ---------------------------------------------------------------------------
# Not verified here (device-only or out of this script's reach):
#
# - synth(0) and the loadtest device build in general: the bead text calls
#   for the same checks against Rig("192.168.1.121"); the device is shared
#   and flashed only by the swarm leader. Running this script with --host
#   192.168.1.121 covers everything except the web-save-emulation checks
#   (remove_disabled_at_one, eight_and_nine_and_audit,
#   web_save_reconcile_editor, malformed_time_editor, which also needs to
#   restart the process to plant its fixture, and
#   web_save_reconciles_machine_row), which are simulator-only by design
#   (is_web_save_host) and are skipped, logged, against a device host.
# - /api/debug/heap (int_free/dma_free) before and after this script's
#   heaviest state (the 9-entry list open, both list pages built): not
#   read here since the simulator's heap figures are not representative
#   (CLAUDE.md, memory rules are device-only). Command for the leader:
#   rig.heap() bracketing check_eight_and_nine_and_audit against the
#   loadtest device.
