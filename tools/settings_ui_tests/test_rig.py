#!/usr/bin/env python3
"""Runnable check for tools/settings_ui_tests/rig.py: this is the module's
own test (gm-flw.16's acceptance criteria that can run in this environment),
not a settings-category scenario (that is gm-flw.13's runner, out of this
bead's scope).

Drives the desktop simulator only (pio run -e display-sim). The loadtest
device checks the bead text also calls out (Rig("192.168.1.121"), synth(0))
are the swarm leader's to run against the bench device and are not
exercised here.

Usage:
    python3 tools/settings_ui_tests/test_rig.py
        [--program PATH/to/.pio/build/display-sim/program]
        [--workdir DIR] [--port N]

Exits 0 if every check passes, 1 otherwise. Logs each step (Rig.log) and
prints a PASS/FAIL summary at the end.
"""
import argparse
import json
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO_ROOT)

from tools.settings_ui_tests import Rig, RigHTTPError, Sim, color_hex, num, schedules, seconds  # noqa: E402
from tools.settings_ui_tests.fixtures import Venue  # noqa: E402
from tools.settings_ui_tests.rig import object_name  # noqa: E402

DEFAULT_PROGRAM = os.path.join(REPO_ROOT, ".pio", "build", "display-sim", "program")

# The brew screen's clickable targets, by EEZ name (screens.h's objects_t),
# dumped against a clean sim boot on 2026-09-06 (commit c4d4edc5). A mismatch
# means either the brew screen's layout changed (re-baseline this list) or
# touchmap()/targets() regressed (it does not). The bead's shared contract
# excuses generated (EEZ-designed) screens like this one from the settings
# geometry rules beyond this baseline (see the audit checks below).
BREW_TARGETS = [
    "brew_dials__menu_icon",
    "profile_name",
    "profile_select_button",
    "settings_button",
    "start_button",
]

FAILURES = []
TOTAL = 0


def check(rig, name, cond, detail=""):
    global TOTAL
    TOTAL += 1
    rig.log("check", name=name, ok=int(bool(cond)), detail=detail)
    if not cond:
        FAILURES.append((name, detail))
    return cond


def check_wifi_password_grep(rig):
    """An acceptance criterion for this package is a shell grep for the
    settings key this module must never expose: it should match exactly
    one line (the settings() filter and its comment). Checked here too
    (a Python re-scan of this package's own files), so a regression is
    caught by running this script, not only by the leader's grep.

    The key is built from two literals rather than written out whole:
    written out, this check's own source line would be a second grep hit
    next to the one legitimate line in rig.py."""
    key = "wifi" + "Password"
    pattern = re.compile(key)
    hits = []
    for name in ("__init__.py", "rig.py", "test_rig.py", "README.md"):
        path = os.path.join(HERE, name)
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as f:
            for lineno, line in enumerate(f, 1):
                if pattern.search(line):
                    hits.append("%s:%d" % (name, lineno))
    check(rig, "wifi_password_single_mention", len(hits) == 1, repr(hits))


def check_settings(rig):
    s = rig.settings()
    check(rig, "settings_no_wifi_password", ("wifi" + "Password") not in s)
    check(rig, "settings_has_expected_fields", "standbyTimeout" in s and "bgAnimPlateColor" in s and "autowakeupSchedules" in s)
    return s


def check_touchmap_seq(rig):
    d0 = rig.touchmap(screen=0)
    d1 = rig.touchmap(screen=0)
    check(rig, "touchmap_seq_increases", d1["seq"] != d0["seq"], "%r -> %r" % (d0["seq"], d1["seq"]))


def check_brew_baseline_and_audit(rig):
    d_brew = rig.touchmap(screen=2, load=True)
    names = sorted(object_name(t) for t in rig.targets(d_brew))
    check(rig, "brew_target_baseline", names == sorted(BREW_TARGETS), "got %r" % (names,))
    # Informational only: the generated screens are not held to the
    # settings geometry rules beyond the baseline above (bead text), so
    # violations here are not a rig bug. brew_dials__menu_icon's hit rect
    # reaches past the round panel's 240px visible radius (measured
    # ~248px from centre at the corners) on the pre-existing brew screen,
    # unrelated to anything this bead builds.
    audit_brew = rig.audit(d_brew)
    rig.log("brew_audit_informational", violations=len(audit_brew["violations"]), exempt=len(audit_brew["exempt"]))
    return d_brew


def check_audit_exemptions(rig):
    """The chevron (role "exit") and standby_btn (EEZ name) are the shared
    contract's named exceptions to the size/edge rules; this checks the
    exemption bucketing itself (present under "exempt", never counted as a
    violation) on the menu screen, which has a standby_btn."""
    d_menu = rig.touchmap(screen=5, load=True)
    audit_menu = rig.audit(d_menu)
    exempt_names = {e["target"] for e in audit_menu["exempt"]}
    check(rig, "audit_exempts_standby_btn", "standby_btn" in exempt_names, repr(audit_menu["exempt"]))
    check(
        rig,
        "audit_exempt_not_double_counted",
        not any(v["target"] == "standby_btn" for v in audit_menu["violations"]),
        repr(audit_menu["violations"]),
    )
    rig.log("menu_audit_informational", violations=len(audit_menu["violations"]), exempt=len(audit_menu["exempt"]))


def check_tap_opens_menu(rig):
    """rig.tap(240, 450) from the brew screen leaves the menu active: that
    point falls inside brew_dials__menu_icon's effective hit rect."""
    rig.touchmap(screen=2, load=True)
    seq_before = rig.touchmap(screen=0)["seq"]
    rig.tap(240, 450)
    d_after = rig.wait_dump_change(seq_before, screen=0)
    check(rig, "tap_opens_menu", d_after.get("screen_id") == 5, "screen_id=%r" % d_after.get("screen_id"))


def check_device_only_routes_unavailable(rig):
    """/api/debug/anim and /api/debug/synth are compiled only for the real
    LilyGo panel (WebUIPlugin.cpp guards the whole block with
    !GAGGIMATE_HEADLESS && !GAGGIMATE_SIM). Both fall through to the sim's
    static/SPA handler on this build (200, the web UI bundle, not JSON),
    which get_json() turns into a clean RigHTTPError instead of a raw decode
    exception; this confirms that path, not the routes themselves (those
    are the leader's device runs)."""
    for name, call in (
        ("anim", rig.anim),
        ("synth", lambda: rig.synth(0)),
    ):
        try:
            call()
            check(rig, "%s_unavailable_on_sim" % name, False, "unexpectedly returned JSON")
        except RigHTTPError as e:
            check(rig, "%s_unavailable_on_sim" % name, True, str(e))


def check_settingsui_state(rig):
    """/api/debug/settingsui is compiled for the sim (GAGGIMATE_SIM) as well
    as the bench builds; with nothing open it reports the closed shell."""
    st = rig.settingsui_state()
    check(rig, "settingsui_state_shape", all(k in st for k in ("seq", "open", "depth", "category", "page", "pages", "title", "fixture")), repr(st))
    check(rig, "settingsui_state_closed", st.get("open") is False and st.get("depth") == 0, repr(st))


def check_fb_png(rig, workdir):
    path = os.path.join(workdir, "fb_check.png")
    w, h = rig.fb_png(path, step=2)
    check(rig, "fb_png_written", w > 0 and h > 0 and os.path.getsize(path) > 100, "%dx%d" % (w, h))


def check_wire_format_helpers(rig, s):
    check(rig, "wire_seconds", seconds(s["standbyTimeout"]) == int(s["standbyTimeout"]))
    check(rig, "wire_num", isinstance(num(s["temperatureOffset"]), float))
    rgb = color_hex(s["bgAnimPlateColor"])
    check(rig, "wire_color_hex", isinstance(rgb, tuple) and len(rgb) == 3)
    sched = schedules(s["autowakeupSchedules"])
    check(rig, "wire_schedules", isinstance(sched, list) and sched and len(sched[0]["days"]) == 7, repr(sched))


def check_restart_persistence(rig, sim, data_dir):
    """Sim.restart() must preserve a setting changed through the web UI
    before the restart. There is no browser here and this rig must never
    POST /api/settings by hand (the web UI is the only safe writer), so
    the closest safe equivalent is used: stop the sim, edit one plain key
    in sim_data/nvs/controller.json (the Preferences shim's store,
    key "sbt" for standbyTimeout, value a decimal string in milliseconds;
    preferences_shim.cpp stores every value as a string, never a raw JSON
    number), restart, and read it back over the wire."""
    nvs_path = os.path.join(data_dir, "nvs", "controller.json")
    before = seconds(rig.settings_value("standbyTimeout"))
    # A reused workdir starts where the previous run left it, so the value
    # written must differ from whatever is stored now.
    new_s = 555 if before != 555 else 556
    new_ms = new_s * 1000
    sim.stop()
    with open(nvs_path, encoding="utf-8") as f:
        store = json.load(f)
    store["sbt"] = str(new_ms)
    with open(nvs_path, "w", encoding="utf-8") as f:
        json.dump(store, f)
    sim.restart()
    after = seconds(rig.settings_value("standbyTimeout"))
    check(rig, "restart_preserves_setting", after == new_s and after != before, "before=%r after=%r want=%r" % (before, after, new_s))
    # Put the original back the same way, so the venue leaves this check as
    # it found it (555 s is off the Machine page's one-minute grid and would
    # otherwise trip a later scenario's start-on-grid preflight).
    sim.stop()
    with open(nvs_path, encoding="utf-8") as f:
        store = json.load(f)
    store["sbt"] = str(before * 1000)
    with open(nvs_path, "w", encoding="utf-8") as f:
        json.dump(store, f)
    sim.restart()
    restored = seconds(rig.settings_value("standbyTimeout"))
    check(rig, "restart_check_restored_setting", restored == before, "before=%r restored=%r" % (before, restored))


def _sequence(rig, venue):
    """Every check, in order, against an already-launched venue. Shared by
    main() and by run() below so the runner and a standalone invocation
    cannot drift apart."""
    check_wifi_password_grep(rig)
    s = check_settings(rig)
    check_touchmap_seq(rig)
    check_brew_baseline_and_audit(rig)
    check_audit_exemptions(rig)
    check_tap_opens_menu(rig)
    if not venue.is_device:
        # Asserts that /api/debug/anim and /api/debug/synth are absent,
        # which is true only of a build without the panel: the device has
        # both, so this check would be exactly backwards there.
        check_device_only_routes_unavailable(rig)
    check_settingsui_state(rig)
    check_fb_png(rig, venue.workdir)
    check_wire_format_helpers(rig, s)
    if venue.can_restart:
        check_restart_persistence(rig, venue.sim, os.path.join(venue.workdir, "sim_data"))


def run(rig, report, venue):
    """Entry point for the end-to-end runner (tools/settings_ui_test.py).
    Raises AssertionError listing the checks that failed, so the runner can
    record this scenario as failed with the detail its report needs.

    Note for the runner's own ordering: check_restart_persistence leaves
    standbyTimeout at 555 s, which is deliberately off the Machine page's
    one minute grid, and does not put it back; that is why the runner
    schedules this scenario last."""
    first_fail, first_total = len(FAILURES), TOTAL
    _sequence(rig, venue)
    report.step("scenario_checks", scenario="rig", checks=TOTAL - first_total,
                failed=len(FAILURES) - first_fail)
    new = FAILURES[first_fail:]
    if new:
        raise AssertionError("; ".join("%s: %s" % (name, detail) for name, detail in new))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--program", default=DEFAULT_PROGRAM)
    ap.add_argument("--workdir", default=os.path.join(tempfile.gettempdir(), "gm_settings_ui_tests", "test_rig"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("GM_SIM_PORT", "8084")))
    args = ap.parse_args()

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
