#!/usr/bin/env python3
"""Scenario for gm-flw.7, the Temperatures & timing settings category
(src/display/ui/default/settings/CatTemps.cpp): temperature offset and
pressure sensor rating behind hold-to-unlock steppers, brew/grind delay
steppers and the delay auto-adjust toggle.

Built on tools/settings_ui_tests/rig.py (gm-flw.16); no scenario logic
lives there, this file owns all of it. Runs against the desktop simulator
by default (pio run -e display-sim) and against the bench device with
--host (192.168.1.121); every value this script changes is put back
through the UI (taps and the /api/debug/settingsui route), never by
POSTing /api/settings, except standbyTimeout in the forced-external-leave
check, which this file does not own a row for (it belongs to the Display
category, a different bead) -- see check_forced_external_leave for how
that one field is restored and why.

Usage:
    python3 tools/settings_ui_tests/test_temps.py
        [--program PATH/to/.pio/build/display-sim/program]
        [--workdir DIR] [--port N] [--host ip[:port]]

Exits 0 if every check passes, 1 otherwise. Logs each step (Rig.log) and
prints a PASS/FAIL summary at the end.
"""
import argparse
import re
import os
import sys
import tempfile
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO_ROOT)

from tools.settings_ui_tests import Rig, Sim, num  # noqa: E402
from tools.settings_ui_tests.fixtures import Venue  # noqa: E402
from tools.settings_ui_tests.rig import tag_role, tag_row  # noqa: E402

DEFAULT_PROGRAM = os.path.join(REPO_ROOT, ".pio", "build", "display-sim", "program")

CAT_TEMPS = 0
ROW_NAMES = ["Temperature offset", "Pressure sensor", "Brew delay", "Grind delay", "Delay auto-adjust"]
COMMITTED_LOG_PREFIX = "SettingsTemps: committed"

FAILURES = []
SKIPPED = []
TOTAL = 0


def check(rig, name, cond, detail=""):
    global TOTAL
    TOTAL += 1
    rig.log("check", name=name, ok=int(bool(cond)), detail=detail)
    if not cond:
        FAILURES.append((name, detail))
    return cond


def skip(rig, name, reason):
    rig.log("skip", name=name, reason=reason)
    SKIPPED.append((name, reason))


# ---- display formats (SettingsModel.cpp's kTemperatureOffsetSpec/
# kPressureScalingSpec/kBrewDelaySpec/kGrindDelaySpec, mirrored here since
# this script has no way to call the C++ model) -----------------------------


def fmt_offset(value):
    return "%d C" % int(round(value))


def fmt_scaling(value):
    return "%.1f bar" % value


def fmt_ms(value):
    return "%d ms" % int(round(value))


# ---- web-save emulation (simulator only) -----------------------------------

# WebUIPlugin::handleSettings (src/display/plugins/WebUIPlugin.cpp) sets
# these nine booleans unconditionally from request->hasArg (present ==
# checked, absent == false, like an unchecked HTML checkbox); a "complete
# body" save must re-post every one GET shows true, or a save that only
# means to change one unrelated field silently clears all of these.
_PRESERVE_CHECKBOXES = (
    "homekit",
    "boilerFillActive",
    "smartGrindActive",
    "scaleMenuButton",
    "elementTintEnabled",
    "momentaryButtons",
    "delayAdjust",
    "clock24hFormat",
    "autowakeupEnabled",
)


def web_save(rig, changes):
    """Emulates the web UI's save against the simulator's own /api/settings:
    a form body built from the current GET plus `changes`, so no field the
    caller did not name is disturbed. Refuses any host but 127.0.0.1/
    localhost (CLAUDE.md: the web UI is the only safe writer against a real
    device, and its GET response carries the WiFi password in cleartext)."""
    host = rig.host.split(":")[0]
    if host not in ("127.0.0.1", "localhost"):
        raise RuntimeError("web-save emulation is simulator-only; refusing host %r" % rig.host)
    cur = rig.settings()
    form = {k: "1" for k in _PRESERVE_CHECKBOXES if cur.get(k)}
    form.update(changes)
    data = urllib.parse.urlencode(form).encode("ascii")
    req = urllib.request.Request(
        "http://%s/api/settings" % rig.host,
        data=data,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=rig.timeout) as resp:
        resp.read()


# ---- sim log helpers --------------------------------------------------------


def log_pos(sim):
    return os.path.getsize(sim.log_path)


def log_since(sim, pos):
    """Text appended to the sim's combined stdout/stderr log since byte
    offset pos (the log carries every ESP_LOGI line, including the
    SettingsTemps: committed line commit() emits)."""
    with open(sim.log_path, "r", encoding="utf-8", errors="replace") as f:
        f.seek(pos)
        return f.read()


# SettingsUI.cpp tags its own five generic row-slot containers "row0".."row4"
# under role "row" (buildCategoryPage's kRowNames), the same role every
# stepper/choice/locked/info row widget uses for its own outer container
# (SettingsRows.cpp); rig.rows_on_page (role=="row", no other filter) mixes
# the two, e.g. this page's own dump names ["row0", "Temperature offset",
# "row1", "Pressure sensor", ...] interleaved by matching y. This is a real
# SettingsUI.cpp/rig.py inconsistency (reported to the leader, see this
# bead's final report), not particular to this category: any category's
# rows_on_page() call is affected the same way.
_SHELL_SLOT_NAMES = frozenset("row%d" % i for i in range(5))


def rows_on_page_including_toggle(dump):
    """Workaround for two rig/shell gaps, both reported to the leader
    rather than fixed here (SettingsRows.h/SettingsUI.cpp/rig.py are not
    this bead's files): (1) the shell's own "row0".."row4" slot tags share
    role "row" with every category row widget's own container tag, see
    _SHELL_SLOT_NAMES above; (2) settingsRowToggleCreate tags its
    clickable container "toggle", not "row" (unlike stepper/choice/locked/
    info, which all use "row"), so a toggle row is invisible to
    rig.rows_on_page entirely. This collects every row-identifying tag a
    Temps-style page can actually carry (row, toggle, action, confirm),
    drops the shell's own slot names, and sorts by hit-rect y like
    rows_on_page itself does."""
    by_y = {}
    for o in dump["objects"]:
        if tag_role(o) not in ("row", "toggle", "action", "confirm"):
            continue
        name = tag_row(o)
        if name in _SHELL_SLOT_NAMES:
            continue
        y = o["hit"][1] if "hit" in o else o["y1"]
        by_y.setdefault(name, y)
    return [name for name, _ in sorted(by_y.items(), key=lambda kv: kv[1])]


# ---- shell navigation -------------------------------------------------------


def ensure_open(rig):
    st = rig.settingsui_state()
    if not st.get("open"):
        rig.settingsui(open=1)
        rig.wait_until(lambda: rig.settingsui_state().get("open") is True, timeout=5)


def enter_temps(rig):
    """From the tile page (depth 0, shell already open): pushes Temps and
    waits for the shell to publish it, then returns a fresh touchmap dump.
    Every call creates a fresh ctx (SettingsUI::openCategory), the same as
    a real close-and-reopen, so this is also what exercises "leaving and
    re-entering relocks the locked rows"."""
    rig.settingsui(cat=CAT_TEMPS)
    rig.wait_until(
        lambda: rig.settingsui_state().get("category") == CAT_TEMPS and rig.settingsui_state().get("depth") == 1,
        timeout=5,
    )
    return rig.touchmap(screen=0)


def pop_temps(rig):
    rig.settingsui(pop=1)
    rig.wait_until(lambda: rig.settingsui_state().get("depth") == 0, timeout=5)


def close_shell(rig):
    if rig.settingsui_state().get("open"):
        rig.settingsui(close=1)
        rig.wait_until(lambda: rig.settingsui_state().get("open") is False, timeout=5)


# ---- checks ------------------------------------------------------------------


def check_row_order_and_initial_values(rig):
    """Acceptance: the five rows, in order, show the current values parsed
    from GET /api/settings. The two locked rows start locked (relocked by
    enter(), CatTemps.cpp), so what they show at open is the "hold to
    unlock" affordance settingsRowLockedCreate always starts with, not the
    numeric value; that value is checked separately, right after each is
    unlocked (check_offset_unlock_and_step / check_pressure_step),
    before either is stepped."""
    d = enter_temps(rig)
    names = rows_on_page_including_toggle(d)
    check(rig, "temps_row_order", names == ROW_NAMES, "got %r want %r" % (names, ROW_NAMES))

    s = rig.settings()

    offset_val = rig.row_value(d, "Temperature offset")
    check(rig, "temps_offset_starts_locked", "hold to unlock" in offset_val.lower(), repr(offset_val))
    scaling_val = rig.row_value(d, "Pressure sensor")
    check(rig, "temps_scaling_starts_locked", "hold to unlock" in scaling_val.lower(), repr(scaling_val))

    brew_val = rig.row_value(d, "Brew delay")
    check(rig, "temps_brew_initial_value", brew_val == fmt_ms(s["brewDelay"]), "got %r settings=%r" % (brew_val, s["brewDelay"]))

    grind_val = rig.row_value(d, "Grind delay")
    check(rig, "temps_grind_initial_value", grind_val == fmt_ms(s["grindDelay"]), "got %r settings=%r" % (grind_val, s["grindDelay"]))

    adjust_val = rig.row_value(d, "Delay auto-adjust")
    want_adjust = "On" if s["delayAdjust"] else "Off"
    check(rig, "temps_adjust_initial_value", adjust_val == want_adjust, "got %r settings=%r" % (adjust_val, s["delayAdjust"]))

    pop_temps(rig)


def check_offset_locked_plus_noop(rig):
    """Acceptance: a tap on the offset row's plus does nothing while
    locked."""
    d = enter_temps(rig)
    plus = rig.find_tag(d, "Temperature offset", "plus")
    if not check(rig, "temps_offset_plus_found_locked", plus is not None):
        pop_temps(rig)
        return
    check(rig, "temps_offset_plus_hidden_while_locked", bool(plus.get("hidden")), repr(plus.get("hidden")))
    before = rig.row_value(d, "Temperature offset")
    s_before = rig.settings()["temperatureOffset"]
    rig.tap_target(plus)
    d2 = rig.touchmap(screen=0)
    after = rig.row_value(d2, "Temperature offset")
    s_after = rig.settings()["temperatureOffset"]
    check(rig, "temps_offset_plus_tap_noop_while_locked", after == before and s_after == s_before,
          "value before=%r after=%r; settings before=%r after=%r" % (before, after, s_before, s_after))
    pop_temps(rig)


def check_offset_unlock_step_and_commit(rig, sim):
    """Acceptance: a 1200 ms press on the row unlocks it and does not step;
    a plus tap then shows the value +1; /api/settings still shows the old
    value until settingsui(pop=1), after which it shows the new one within
    6 s and the log has one SettingsTemps: committed offset= line."""
    d = enter_temps(rig)
    s0 = rig.settings()
    original = int(num(s0["temperatureOffset"]))

    unlock = rig.find_tag(d, "Temperature offset", "unlock")
    if not check(rig, "temps_offset_unlock_found", unlock is not None):
        pop_temps(rig)
        return
    rig.tap_target(unlock, ms=1200)
    d2 = rig.touchmap(screen=0)
    val_after_unlock = rig.row_value(d2, "Temperature offset")
    check(rig, "temps_offset_unlock_does_not_step", val_after_unlock == fmt_offset(original),
          "got %r want %r" % (val_after_unlock, fmt_offset(original)))

    plus = rig.find_tag(d2, "Temperature offset", "plus")
    check(rig, "temps_offset_plus_visible_after_unlock", plus is not None and not plus.get("hidden"), repr(plus))
    if plus is None:
        pop_temps(rig)
        return

    rig.tap_target(plus)
    d3 = rig.touchmap(screen=0)
    val_after_step = rig.row_value(d3, "Temperature offset")
    check(rig, "temps_offset_plus_steps_by_one", val_after_step == fmt_offset(original + 1),
          "got %r want %r" % (val_after_step, fmt_offset(original + 1)))

    s_before_pop = rig.settings()
    check(rig, "temps_offset_not_committed_before_pop", int(num(s_before_pop["temperatureOffset"])) == original,
          "got %r want %r" % (s_before_pop["temperatureOffset"], original))

    pos = log_pos(sim)
    pop_temps(rig)

    def committed():
        cur = rig.settings()
        return int(num(cur["temperatureOffset"])) == original + 1

    ok = False
    try:
        ok = rig.wait_until(committed, timeout=6)
    except TimeoutError:
        pass
    check(rig, "temps_offset_committed_within_6s", bool(ok), "final=%r want=%r" % (rig.settings()["temperatureOffset"], original + 1))

    text = log_since(sim, pos)
    count = text.count(COMMITTED_LOG_PREFIX + " offset=")
    check(rig, "temps_offset_committed_log_once", count == 1, "count=%d log=%r" % (count, text))


def check_offset_relocks_on_second_visit(rig):
    """Acceptance: enter relocks both locked rows; leaving and re-entering
    shows them locked again. This is the second visit; the first is
    whatever check_offset_unlock_step_and_commit just left (popped, so the
    ctx from that visit is already destroyed and the shell is back at the
    tile page)."""
    d = enter_temps(rig)
    offset_val = rig.row_value(d, "Temperature offset")
    check(rig, "temps_offset_relocked_on_reentry", "hold to unlock" in offset_val.lower(), repr(offset_val))
    scaling_val = rig.row_value(d, "Pressure sensor")
    check(rig, "temps_scaling_relocked_on_reentry", "hold to unlock" in scaling_val.lower(), repr(scaling_val))
    pop_temps(rig)


def check_pressure_step_and_commit(rig, sim):
    """Acceptance: changing the pressure sensor from 16.0 to 16.1 and
    popping produces one SettingsTemps: committed ... scaling=16.1 line
    and /api/settings reads 16.1. The comms-trace sendPressureScale half
    of this criterion is device-only (needs the pressure capability and
    the BLE controller link); not checked here, see main()'s device notes."""
    d = enter_temps(rig)
    s0 = rig.settings()
    original = num(s0["pressureScaling"])

    unlock = rig.find_tag(d, "Pressure sensor", "unlock")
    if not check(rig, "temps_scaling_unlock_found", unlock is not None):
        pop_temps(rig)
        return
    rig.tap_target(unlock, ms=1200)
    d2 = rig.touchmap(screen=0)
    val_after_unlock = rig.row_value(d2, "Pressure sensor")
    check(rig, "temps_scaling_unlock_does_not_step", val_after_unlock == fmt_scaling(original),
          "got %r want %r" % (val_after_unlock, fmt_scaling(original)))

    plus = rig.find_tag(d2, "Pressure sensor", "plus")
    if not check(rig, "temps_scaling_plus_visible_after_unlock", plus is not None and not plus.get("hidden")):
        pop_temps(rig)
        return
    rig.tap_target(plus)
    d3 = rig.touchmap(screen=0)
    expected_new = round(original + 0.1, 1)
    val_after_step = rig.row_value(d3, "Pressure sensor")
    check(rig, "temps_scaling_plus_steps_by_tenth", val_after_step == fmt_scaling(expected_new),
          "got %r want %r" % (val_after_step, fmt_scaling(expected_new)))

    pos = log_pos(sim)
    pop_temps(rig)

    def committed():
        cur = num(rig.settings()["pressureScaling"])
        return abs(cur - expected_new) < 0.05

    ok = False
    try:
        ok = rig.wait_until(committed, timeout=6)
    except TimeoutError:
        pass
    check(rig, "temps_scaling_committed_within_6s", bool(ok), "final=%r want=%r" % (rig.settings()["pressureScaling"], expected_new))

    text = log_since(sim, pos)
    want_line = COMMITTED_LOG_PREFIX + " scaling=%.1f" % expected_new
    check(rig, "temps_scaling_committed_log_once", text.count(want_line) == 1, "want %r in log=%r" % (want_line, text))
    skip(rig, "temps_scaling_sendPressureScale_trace", "device-only: needs the pressure capability and a BLE comms trace")


def check_brew_delay_hold_and_clamp(rig, sim):
    """Acceptance: holding brew delay plus for 1500 ms moves it by 450..700
    ms (9..14 steps of 50) and the value text tracks (checked as the net
    delta after release: rig.tap() only reports pre/post state, not a
    mid-hold trace); popping persists it. At 4000 a plus tap leaves 4000."""
    d = enter_temps(rig)
    s0 = rig.settings()
    original = s0["brewDelay"]

    plus = rig.find_tag(d, "Brew delay", "plus")
    if not check(rig, "temps_brew_plus_found", plus is not None):
        pop_temps(rig)
        return

    rig.tap_target(plus, ms=1500)
    d2 = rig.touchmap(screen=0)
    val_text = rig.row_value(d2, "Brew delay")
    new_value = int(val_text.split()[0])
    delta = new_value - original
    check(rig, "temps_brew_hold_1500ms_delta_range", 450 <= delta <= 700 and delta % 50 == 0,
          "delta=%r new=%r original=%r" % (delta, new_value, original))

    pos = log_pos(sim)
    pop_temps(rig)

    def committed():
        return int(rig.settings()["brewDelay"]) == new_value

    ok = False
    try:
        ok = rig.wait_until(committed, timeout=6)
    except TimeoutError:
        pass
    check(rig, "temps_brew_hold_persisted", bool(ok), "final=%r want=%r" % (rig.settings()["brewDelay"], new_value))
    text = log_since(sim, pos)
    check(rig, "temps_brew_committed_log_once", text.count(COMMITTED_LOG_PREFIX + " brew=%d" % new_value) == 1, repr(text))

    # Second visit: a long hold clamps at the spec's 4000 ms ceiling, and a
    # further plus tap leaves it there (ClampMode::Clamp, kBrewDelaySpec).
    d3 = enter_temps(rig)
    plus2 = rig.find_tag(d3, "Brew delay", "plus")
    if not check(rig, "temps_brew_plus_found_for_clamp", plus2 is not None):
        pop_temps(rig)
        return
    rig.tap_target(plus2, ms=8000)
    d4 = rig.touchmap(screen=0)
    at_ceiling = rig.row_value(d4, "Brew delay")
    check(rig, "temps_brew_long_hold_reaches_4000", at_ceiling == fmt_ms(4000), repr(at_ceiling))
    plus3 = rig.find_tag(d4, "Brew delay", "plus")
    rig.tap_target(plus3)
    d5 = rig.touchmap(screen=0)
    still_ceiling = rig.row_value(d5, "Brew delay")
    check(rig, "temps_brew_plus_at_4000_stays_4000", still_ceiling == fmt_ms(4000), repr(still_ceiling))
    pop_temps(rig)

    def clamped_committed():
        return int(rig.settings()["brewDelay"]) == 4000

    ok2 = False
    try:
        ok2 = rig.wait_until(clamped_committed, timeout=6)
    except TimeoutError:
        pass
    check(rig, "temps_brew_4000_persisted", bool(ok2), "final=%r" % rig.settings()["brewDelay"])


def check_toggle_delay_adjust(rig, sim):
    """Acceptance: toggling auto-adjust and popping flips delayAdjust."""
    d = enter_temps(rig)
    before = rig.settings()["delayAdjust"]
    # role "toggle", not "row": see rows_on_page_including_toggle's comment.
    toggle_row = rig.find_tag(d, "Delay auto-adjust", "toggle")
    if not check(rig, "temps_adjust_row_found", toggle_row is not None):
        pop_temps(rig)
        return
    rig.tap_target(toggle_row)
    d2 = rig.touchmap(screen=0)
    shown = rig.row_value(d2, "Delay auto-adjust")
    check(rig, "temps_adjust_toggle_flips_display", shown == ("Off" if before else "On"), repr(shown))

    pos = log_pos(sim)
    pop_temps(rig)

    def committed():
        return rig.settings()["delayAdjust"] != before

    ok = False
    try:
        ok = rig.wait_until(committed, timeout=6)
    except TimeoutError:
        pass
    check(rig, "temps_adjust_committed_within_6s", bool(ok), "before=%r after=%r" % (before, rig.settings()["delayAdjust"]))
    text = log_since(sim, pos)
    want = COMMITTED_LOG_PREFIX + " adjust=%d" % (0 if before else 1)
    check(rig, "temps_adjust_committed_log_once", text.count(want) == 1, "want %r in %r" % (want, text))


def check_no_edit_writes_nothing(rig, sim):
    """Acceptance: leaving with no change writes nothing: no
    SettingsTemps: committed line after a pop that followed no edit, and
    /api/settings is byte-identical for the five fields before and
    after."""
    fields = ("temperatureOffset", "pressureScaling", "brewDelay", "grindDelay", "delayAdjust")
    before = {k: rig.settings()[k] for k in fields}
    enter_temps(rig)
    pos = log_pos(sim)
    pop_temps(rig)
    after = {k: rig.settings()[k] for k in fields}
    check(rig, "temps_no_edit_fields_unchanged", before == after, "before=%r after=%r" % (before, after))
    text = log_since(sim, pos)
    check(rig, "temps_no_edit_no_committed_line", COMMITTED_LOG_PREFIX not in text, repr(text))


def check_untouched_survives_external_write(rig, sim):
    """Acceptance: untouched fields survive external writes: with the page
    open, change grindDelay on the web UI; the row updates on the next
    pass (reconcile) and popping does not overwrite it. Also touches a
    different row (the toggle) in the same visit, so commit writes
    something and the untouched grindDelay's survival is a real check of
    reconcile/commit's touched-only rule, not trivially true because
    commit wrote nothing at all."""
    d = enter_temps(rig)
    s0 = rig.settings()
    old_grind = s0["grindDelay"]
    new_grind = old_grind + 123
    before_adjust = s0["delayAdjust"]

    web_save(rig, {"grindDelay": str(new_grind)})

    def reconciled():
        dd = rig.touchmap(screen=0)
        return rig.row_value(dd, "Grind delay") == fmt_ms(new_grind)

    ok = False
    try:
        ok = rig.wait_until(reconciled, timeout=5)
    except TimeoutError:
        pass
    check(rig, "temps_grind_external_write_reconciles", bool(ok), "want %r" % fmt_ms(new_grind))

    # Touch a different row in the same visit so commit has something to
    # write; the untouched grindDelay must not be re-asserted from the
    # stale enter()-time draft.
    d2 = rig.touchmap(screen=0)
    toggle_row = rig.find_tag(d2, "Delay auto-adjust", "toggle")
    if toggle_row is not None:
        rig.tap_target(toggle_row)
    pop_temps(rig)

    def settled():
        cur = rig.settings()
        return cur["grindDelay"] == new_grind and cur["delayAdjust"] != before_adjust

    ok2 = False
    try:
        ok2 = rig.wait_until(settled, timeout=6)
    except TimeoutError:
        pass
    cur = rig.settings()
    check(rig, "temps_grind_external_write_survives_pop", bool(ok2),
          "grindDelay=%r want=%r; delayAdjust=%r want!=%r" % (cur["grindDelay"], new_grind, cur["delayAdjust"], before_adjust))


def check_forced_external_leave(rig, sim):
    """Acceptance: a forced external leave (standby timeout set low
    beforehand, or the sim mock's brew start) while the offset is edited
    but not yet popped still persists the edit.

    Neither suggested trigger is reachable from this simulator: the sim
    boots with mode MODE_STANDBY (Settings.h's startupMode default) and
    Controller::loopLogic only ever calls activateStandby() when
    "mode != MODE_STANDBY" (Controller.cpp), so the standby timeout can
    never fire while idling on the menu screen with settings open, and
    /api/debug/synth (the only sim-reachable brew mock) does not exist on
    this build (WebUIPlugin.cpp guards it out under GAGGIMATE_SIM; see the
    module docstring's device-only routes and CLAUDE.md). What the
    criterion actually verifies -- an uncommitted edit still gets
    committed when the shell is torn down by a route other than
    settingsui(pop=1) -- is exercised here through settingsui(close=1)
    instead: SettingsUI::close() and SettingsUI::onExternalLeave()
    (SettingsUI.cpp) are byte-identical bodies, both just calling
    teardownAll(), so close=1 drives the same commit-on-teardown path a
    real forced leave would. The standby-timeout and brew-start triggers
    themselves are left for the leader to verify against the loadtest
    device, where mode does leave MODE_STANDBY for a real brew."""
    d = enter_temps(rig)
    s0 = rig.settings()
    original = int(num(s0["temperatureOffset"]))
    unlock = rig.find_tag(d, "Temperature offset", "unlock")
    if not check(rig, "temps_forced_leave_unlock_found", unlock is not None):
        return
    rig.tap_target(unlock, ms=1200)
    d2 = rig.touchmap(screen=0)
    plus = rig.find_tag(d2, "Temperature offset", "plus")
    rig.tap_target(plus)
    d3 = rig.touchmap(screen=0)
    edited_value = rig.row_value(d3, "Temperature offset")
    check(rig, "temps_forced_leave_edit_applied_to_draft", edited_value == fmt_offset(original + 1), repr(edited_value))

    # No settingsui(pop=1): close=1 tears the whole shell down (every open
    # page committed top-down, same as onExternalLeave()), never popping
    # just the category page.
    rig.settingsui(close=1)

    def left():
        return rig.settingsui_state().get("open") is False

    ok = False
    try:
        ok = rig.wait_until(left, timeout=5)
    except TimeoutError:
        pass
    check(rig, "temps_forced_leave_closes_shell", bool(ok), repr(rig.settingsui_state()))

    def committed():
        return int(num(rig.settings()["temperatureOffset"])) == original + 1

    ok2 = False
    try:
        ok2 = rig.wait_until(committed, timeout=6)
    except TimeoutError:
        pass
    check(rig, "temps_forced_leave_persists_edit", bool(ok2), "final=%r want=%r" % (rig.settings()["temperatureOffset"], original + 1))
    skip(rig, "temps_forced_leave_real_trigger", "standby-timeout/brew-start triggers are not reachable from the sim (see docstring); needs the loadtest device")

    # Re-open for whatever check runs next (close=1 tore down the whole
    # shell, not just the category page).
    ensure_open(rig)


def run_simulator_only_checks(rig, sim):
    check_untouched_survives_external_write(rig, sim)
    check_forced_external_leave(rig, sim)


# ---- restoration ---------------------------------------------------------------


def _row_number(text):
    """First signed number in a row's value text ("2 C", "16.1 bar",
    "1500 ms", "Hold to unlock" -> None)."""
    m = re.search(r"-?\d+(?:\.\d+)?", text)
    return float(m.group(0)) if m else None


def _step_row_to(rig, row, target, tolerance):
    """Drives one Temps stepper row back to `target` through its own
    buttons: unlocks the row if it is locked, holds the right button for
    fast steps while far away, then single-taps. Returns the final number
    the row shows. Bounded, so a row that cannot reach the target (a value
    off its grid) gives up after a fixed number of presses."""
    d = enter_temps(rig)
    unlock = rig.find_tag(d, row, "unlock")
    if unlock is not None and not unlock.get("hidden"):
        rig.tap_target(unlock, ms=1200)
        d = rig.touchmap(screen=0)
    current = _row_number(rig.row_value(d, row))
    presses = 0
    step = 2 * tolerance # the row's grid: offset 1, pressure 0.1, delays 50
    crossed = False      # once the target has been passed, single taps only
    while current is not None and abs(current - target) > tolerance and presses < 120:
        role = "plus" if current < target else "minus"
        btn = rig.find_tag(d, row, role)
        if btn is None or btn.get("hidden"):
            break
        # A 1500 ms hold is one press plus about eleven 100 ms repeats (fewer
        # on a slow UI pass), so it moves at most about 12 steps and never
        # reaches the 2 s fast-step threshold; use it only while the target
        # is well beyond that, so a hold cannot overshoot and oscillate.
        far = not crossed and abs(current - target) > 20 * step
        rig.tap_target(btn, ms=1500 if far else 80)
        presses += 1
        d = rig.touchmap(screen=0)
        nxt = _row_number(rig.row_value(d, row))
        if nxt is None or (nxt == current and not far):
            break # clamped at a limit, or the row stopped responding
        if (nxt - target) * (current - target) < 0:
            crossed = True
        current = nxt
    pop_temps(rig)
    return current


def restore_temps(rig, s0):
    """Puts back every field a check changed, through the UI only (the
    grind delay was moved by the emulated web save, and comes back through
    its stepper like the rest). Reports what it could not restore; never
    POSTs."""
    targets = (
        ("Temperature offset", "temperatureOffset", int(num(s0["temperatureOffset"])), 0.5),
        ("Pressure sensor", "pressureScaling", round(num(s0["pressureScaling"]), 1), 0.05),
        ("Brew delay", "brewDelay", int(s0["brewDelay"]), 25),
        ("Grind delay", "grindDelay", int(s0["grindDelay"]), 25),
    )
    ensure_open(rig)
    for row, key, target, tol in targets:
        shown = _step_row_to(rig, row, target, tol)

        def restored(key=key, target=target, tol=tol):
            return abs(num(rig.settings()[key]) - target) <= tol

        ok = False
        try:
            ok = rig.wait_until(restored, timeout=6)
        except TimeoutError:
            pass
        rig.log("restore", key=key, target=target, shown=shown, ok=int(bool(ok)))
        check(rig, "temps_restored_%s" % key, bool(ok), "want %r got %r" % (target, rig.settings()[key]))
    if s0["delayAdjust"] != rig.settings()["delayAdjust"]:
        d = enter_temps(rig)
        toggle = rig.find_tag(d, "Delay auto-adjust", "toggle")
        if toggle is not None:
            rig.tap_target(toggle)
        pop_temps(rig)
        check(rig, "temps_restored_delayAdjust", rig.wait_until(lambda: rig.settings()["delayAdjust"] == s0["delayAdjust"], timeout=6))


def _sequence(rig, venue):
    """Every check, in order, against an already-launched venue. Shared by
    main() and by run() below so the runner and a standalone invocation
    cannot drift apart. The commit checks read the firmware's own category
    log line out of the simulator's log file, which has no HTTP equivalent,
    so they are skipped (logged, not silently) against the device."""
    s_start = rig.settings()
    ensure_open(rig)
    check_row_order_and_initial_values(rig)
    check_offset_locked_plus_noop(rig)
    if venue.sim is None:
        skip(rig, "temps_device_commit_checks", "log/timing checks need the sim's log file; run this script on the simulator for those, and separately against the device for synth(0)/comms-trace checks the leader owns")
        close_shell(rig)
        return
    check_offset_unlock_step_and_commit(rig, venue.sim)
    check_offset_relocks_on_second_visit(rig)
    check_pressure_step_and_commit(rig, venue.sim)
    check_brew_delay_hold_and_clamp(rig, venue.sim)
    check_toggle_delay_adjust(rig, venue.sim)
    check_no_edit_writes_nothing(rig, venue.sim)
    run_simulator_only_checks(rig, venue.sim)
    restore_temps(rig, s_start)
    close_shell(rig)


def run(rig, report, venue):
    """Entry point for the end-to-end runner (tools/settings_ui_test.py).
    Raises AssertionError listing the checks that failed, naming the row
    and value each one saw."""
    first_fail, first_total = len(FAILURES), TOTAL
    _sequence(rig, venue)
    report.step("scenario_checks", scenario="temps", checks=TOTAL - first_total,
                failed=len(FAILURES) - first_fail)
    new = FAILURES[first_fail:]
    if new:
        raise AssertionError("; ".join("%s: %s" % (name, detail) for name, detail in new))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--program", default=DEFAULT_PROGRAM)
    ap.add_argument("--workdir", default=os.path.join(tempfile.gettempdir(), "gm_settings_ui_tests", "test_temps"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("GM_SIM_PORT", "8088")))
    ap.add_argument("--host", default=None, help="run against a real device (e.g. 192.168.1.121) instead of the simulator")
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
    if SKIPPED:
        print("SKIPPED (%d):" % len(SKIPPED))
        for name, reason in SKIPPED:
            print("  %s: %s" % (name, reason))
    if FAILURES:
        print("FAIL (%d/%d checks failed):" % (len(FAILURES), TOTAL))
        for name, detail in FAILURES:
            print("  %s: %s" % (name, detail))
        return 1
    print("PASS (%d/%d checks)" % (TOTAL, TOTAL))
    return 0


if __name__ == "__main__":
    sys.exit(main())
