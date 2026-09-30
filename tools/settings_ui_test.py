#!/usr/bin/env python3
"""End-to-end runner for the on-display settings UI (gm-flw.13).

One command drives the whole feature through the synthetic tap and settings
debug routes: it audits every settings page's touch targets, runs every
category scenario, records the animation frame rate and the heap while
settings is open, checks that a value survives a restart, and prints a
timestamped pass/fail report.

    python3 tools/settings_ui_test.py
        [--host 192.168.1.121]
        [--sim-program .pio/build/display-sim/program] [--sim-port 8080]
        [--only rig,temps,display,animation,machine,schedules,status]
        [--skip-restart] [--report-dir DIR]

Without --host it launches the desktop simulator itself, in a fresh
directory under the report dir seeded from
`settings_ui_tests/fixtures/controller.json`, and tears it down at the end.
With --host it drives the bench device: the synthetic brew handshake is
stopped first (`synth(0)`, restored on exit) so the load rig's lifecycle
does not move the UI underneath the taps, and `/api/debug/pclk` is read and
recorded because the stored pixel-clock divider can differ from the build's
(CLAUDE.md).

Two venues, one instrument set, but not one set of assertions. The
simulator has no panel and no background animation renderer, and its heap
figures are shims, so frame rates and memory are recorded there and
asserted only on the device. Everything about navigation, values,
persistence and geometry is asserted on both.

Exit code 0 only when the run passed. `<report-dir>/report.json` holds every
number and every violation, and `<report-dir>/<page>.png` is each settings
page's framebuffer with its targets' hit rectangles drawn on it.
"""

import argparse
import json
import os
import sys
import tempfile
import time
import traceback
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, REPO_ROOT)

from tools.settings_ui_tests import Rig, RigHTTPError, RowMissing, Sim  # noqa: E402
from tools.settings_ui_tests import audit_pages, fixtures  # noqa: E402
from tools.settings_ui_tests.fixtures import Venue  # noqa: E402

DEFAULT_PROGRAM = os.path.join(REPO_ROOT, ".pio", "build", "display-sim", "program")

# Scenario order, which is not the order the scenarios are listed in
# anywhere else. Two of them end in a process restart -- Status holds the
# Restart row and relaunches, and the rig scenario stops the simulator to
# prove Sim.restart() preserves NVS -- so both run at the end. The rig
# scenario is last of all because its restart check leaves standbyTimeout
# at a deliberately odd 555 s, which is not on the Machine page's one
# minute grid; running it earlier would leave the Machine and Schedules
# scenarios starting from a value their own steppers cannot return to.
SCENARIO_ORDER = ("temps", "display", "animation", "machine", "schedules", "press", "status", "rig")

WARMUP_UPTIME_MS = 90_000
BASELINE_WINDOW_S = 10.0
PAGE_WINDOW_S = 5.0
LEAK_CYCLES = 20
LEAK_INT_FREE_TOLERANCE = 2048
RATE_FLOOR_FRACTION = 0.90
DEVICE_RESTART_TIMEOUT_S = 60.0
# How long the simulator gets to exit after the Restart hold. ESP.restart()
# is exit(0) there, and the hold's own release comes back first.
SIM_EXIT_TIMEOUT_S = 10.0
EXPECTED_PCLK_DIV = 6


def _iso_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


class Report:
    """The run's log and its machine-readable twin. Every step prints one
    line as it happens (a run against the device takes minutes and its
    operator watches the terminal) and lands in report.json at the end."""

    def __init__(self, report_dir):
        self.dir = report_dir
        self.started = _iso_now()
        self.steps = []
        self.pages = []
        self.scenarios = {}
        self.violations = []
        self.findings = []
        self.numbers = {}
        self.notes = []

    def step(self, name, /, **kv):
        line = "%s step=%s %s" % (_iso_now(), name, " ".join("%s=%s" % kv_ for kv_ in kv.items()))
        print(line.rstrip(), flush=True)
        self.steps.append({"time": _iso_now(), "step": name, **{k: _plain(v) for k, v in kv.items()}})
        return line

    def violation(self, kind, /, **kv):
        """A failed assertion: the run fails on it. Recorded and printed;
        never raises, so one bad page does not hide the rest of the run."""
        self.violations.append({"kind": kind, **{k: _plain(v) for k, v in kv.items()}})
        self.step(kind, **kv)

    def finding(self, kind, /, **kv):
        """Something the report must carry that is not one of the run's
        assertions. A scenario that did not put a value back is the main
        one: the bead lists the assertions the exit code answers to (the
        audit, the scenarios, the preflight, the restart round trip, and on
        the device the rates and the leak phase) and asks for a restoration
        failure to be logged without stopping the run. Several of these are
        a check's own deliberate residue, such as the Status scenario's
        surviving brightness bump, so failing on them would fail every
        healthy run."""
        self.findings.append({"kind": kind, **{k: _plain(v) for k, v in kv.items()}})
        self.step(kind, **kv)

    def note(self, text):
        self.notes.append(text)

    def number(self, key, value):
        self.numbers[key] = _plain(value)

    @property
    def passed(self):
        return not self.violations

    def write(self):
        os.makedirs(self.dir, exist_ok=True)
        path = os.path.join(self.dir, "report.json")
        payload = {
            "started": self.started,
            "finished": _iso_now(),
            "result": "PASS" if self.passed else "FAIL",
            "geometry": audit_pages.panel_edge_note(),
            "numbers": self.numbers,
            "pages": self.pages,
            "scenarios": self.scenarios,
            "violations": self.violations,
            "findings": self.findings,
            "notes": self.notes,
            "steps": self.steps,
        }
        with open(path, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2, sort_keys=False)
        return path


def _plain(v):
    """JSON-safe: tuples become lists, everything exotic becomes its repr."""
    if isinstance(v, (str, int, float, bool)) or v is None:
        return v
    if isinstance(v, (list, tuple)):
        return [_plain(x) for x in v]
    if isinstance(v, dict):
        return {str(k): _plain(x) for k, x in v.items()}
    return repr(v)


# ---- instruments ------------------------------------------------------------


def heap_sample(rig):
    """One /api/debug/heap read, reduced to the fields this runner compares.
    Never called inside a rate window: the largest-block query walks the
    whole heap and starves the panel's bounce refill for about 1.3 ms
    (WebUIPlugin.cpp), which would show up as a slip in the rate."""
    h = rig.heap()
    return {k: h.get(k) for k in
            ("int_free", "int_largest", "int_min", "psram_free", "dma_free", "dma_min", "hot_fail", "uptime_ms")}


def rate_window(rig, duration):
    """A passive frame-rate window: two /api/debug/anim reads `duration`
    apart with no other HTTP call in between, and the rate computed from
    the device's own clock rather than the host's (CLAUDE.md). Returns None
    when the counter or the clock went backwards, which means the venue
    rebooted inside the window and the sample says nothing."""
    a = rig.anim()
    time.sleep(duration)
    b = rig.anim()
    dt_ms = int(b.get("uptime_ms", 0)) - int(a.get("uptime_ms", 0))
    frames = int(b.get("anim_frames", 0)) - int(a.get("anim_frames", 0))
    if dt_ms <= 0 or frames < 0:
        return None
    return {"fps": round(frames * 1000.0 / dt_ms, 2), "frames": frames, "window_s": round(dt_ms / 1000.0, 3),
            "anim_id": b.get("anim_id")}


def instruments_answer(rig):
    """Every route the runner needs, after a restart or a relaunch. Returns
    the route that did not answer, or None."""
    for path in fixtures.INSTRUMENTS:
        try:
            rig.get_json(path)
        except RigHTTPError:
            return path
    return None


def close_shell(rig):
    """Leaves the settings shell however it is, ignoring a venue that is
    mid-restart (a scenario may have just rebooted it)."""
    try:
        rig.settingsui(close=1)
    except (RigHTTPError, TimeoutError):
        pass


# ---- phases -----------------------------------------------------------------


def warm_up(rig, report, venue, pages, schedule_pages):
    """Opens and closes every page in the run once, waits out the boot
    churn, and, on the device, records the menu-screen baseline the page
    rates are compared against."""
    for spec in pages:
        audit_pages.open_category_page(rig, spec.cat, spec.page)
    for spec in schedule_pages:
        spec.open(rig)
    close_shell(rig)
    report.step("warmup_pages", count=len(pages) + len(schedule_pages))

    def uptime_reached():
        h = rig.heap()
        return int(h.get("uptime_ms", 0)) >= WARMUP_UPTIME_MS

    t0 = time.time()
    rig.wait_until(uptime_reached, timeout=WARMUP_UPTIME_MS / 1000.0 + 30, every=2.0)
    report.step("warmup_uptime", waited_s=round(time.time() - t0, 1), floor_ms=WARMUP_UPTIME_MS)

    baseline = {"heap": heap_sample(rig), "rate": None, "all_screens_restored": None, "fps_cap": None}
    if not venue.is_device:
        report.step("baseline_rate", supported=0, reason="the simulator has no animation renderer")
        return baseline

    baseline["all_screens_restored"] = ensure_all_screens(rig, report)
    try:
        rig.anim()
    except RigHTTPError as e:
        report.violation("RENDERER OFF", detail=str(e))
        return baseline
    settings = rig.settings()
    baseline["fps_cap"] = int(settings.get("bgAnimFps", 0))
    if baseline["fps_cap"] != 30:
        report.step("baseline_cap_unexpected", bgAnimFps=baseline["fps_cap"], expected=30)
    baseline["heap"] = heap_sample(rig)
    baseline["rate"] = rate_window(rig, BASELINE_WINDOW_S)
    report.step("baseline", fps=(baseline["rate"] or {}).get("fps"), cap=baseline["fps_cap"],
                int_free=baseline["heap"]["int_free"], hot_fail=baseline["heap"]["hot_fail"])
    return baseline


def ensure_all_screens(rig, report):
    """Device only: the animation runs on the menu screen only with All
    screens on, and /api/debug/anim answers 409 otherwise. Turns it on
    through the Animation page if it is off and returns the original value
    so the caller can put it back."""
    original = str(rig.settings().get("bgAnimAllScreens", "")).lower() in ("1", "true")
    if original:
        return original
    dump = audit_pages.open_category_page(rig, audit_pages.CAT_ANIMATION, 0)
    toggle = rig.find_tag(dump, "All screens", "toggle")
    if toggle is None:
        report.violation("ALL SCREENS ROW MISSING", rows=rig.rows_on_page(dump))
        return original
    rig.tap_target(toggle)
    close_shell(rig)
    report.step("all_screens_enabled", original=int(original))
    return original


def restore_all_screens(rig, report, original):
    """Puts All screens back to `original` through the UI."""
    if original is None:
        return
    now = str(rig.settings().get("bgAnimAllScreens", "")).lower() in ("1", "true")
    if now == original:
        return
    dump = audit_pages.open_category_page(rig, audit_pages.CAT_ANIMATION, 0)
    toggle = rig.find_tag(dump, "All screens", "toggle")
    if toggle is not None:
        rig.tap_target(toggle)
    close_shell(rig)
    after = str(rig.settings().get("bgAnimAllScreens", "")).lower() in ("1", "true")
    if after != original:
        report.finding("RESTORE FAILED", key="bgAnimAllScreens", expected=original, actual=after)


class PushedPage:
    """A page with no category index: the schedule list and the schedule
    editor, reached by tapping rows. `open(rig)` returns its dump."""

    def __init__(self, key, opener, rows, scenario):
        self.key = key
        self.opener = opener
        self.rows = rows
        self.scenario = scenario
        self.cat = None
        self.page = None

    def open(self, rig):
        return self.opener(rig)


def schedule_pages_for(rig, only):
    """The pushed schedule pages of this run, with the list's row names
    computed from how many schedules are stored right now."""
    if only is not None and "schedules" not in only:
        return []
    count = len(fixtures.schedules(rig.settings().get("autowakeupSchedules", "")))
    pages = []
    for i, rows in enumerate(audit_pages.schedule_list_rows(count)):
        def opener(rig_, page=i):
            dump, _ = audit_pages.open_schedule_list(rig_)
            if page:
                dump = audit_pages.goto_page(rig_, page)
            return dump
        pages.append(PushedPage("schedule-list-p%d" % i, opener, rows, "schedules"))
    for spec in audit_pages.SCHEDULE_EDITOR_PAGES:
        def opener(rig_, page=spec.page):
            dump, _ = audit_pages.open_schedule_editor(rig_, 1)
            if page:
                dump = audit_pages.goto_page(rig_, page)
            return dump
        pages.append(PushedPage(spec.key, opener, spec.rows, "schedules"))
    return pages


def audit_all_pages(rig, report, venue, pages, schedule_pages, baseline, audit_tiles):
    """Opens every page, audits it, writes its PNG, and on the device
    records a passive rate window with no other HTTP call inside it."""
    floor = None
    if baseline["rate"] is not None:
        floor = baseline["rate"]["fps"] * RATE_FLOOR_FRACTION

    def record(key, dump, expected_rows, is_tile_page=False):
        result = audit_pages.audit_page(rig, dump, expected_rows, is_tile_page=is_tile_page)
        png, png_error = audit_pages.write_page_png(rig, dump, report.dir, key)
        rate = rate_window(rig, PAGE_WINDOW_S) if venue.is_device else None
        entry = {
            "page": key,
            "targets": result["targets"],
            "smallest": result["smallest"],
            "rows": result["rows"],
            "exempt": result["exempt"],
            "png": os.path.basename(png) if png else None,
            "rate": rate,
        }
        report.pages.append(entry)
        report.step("page", name=key, targets=result["targets"],
                    smallest="x".join(str(v) for v in (result["smallest"] or ("?", "?"))),
                    fps=(rate or {}).get("fps", "n/a"))
        if png_error:
            report.violation("FRAMEBUFFER READ FAILED", page=key, detail=png_error)
        for v in result["violations"]:
            report.violation("AUDIT", page=key, target=v.get("target"), tag=v.get("tag"),
                             reason=v.get("reason"), detail=v.get("detail"))
        if floor is not None:
            if rate is None:
                report.violation("RATE WINDOW INVALID", page=key, detail="uptime or counter went backwards")
            elif rate["fps"] < floor:
                report.violation("RATE BELOW BASELINE", page=key, fps=rate["fps"],
                                 floor=round(floor, 2), baseline=baseline["rate"]["fps"])

    if audit_tiles:
        dump = audit_pages.open_tiles(rig)
        tiles = audit_pages.tile_count(dump)
        report.number("tile_count", tiles)
        record("tiles", dump, None, is_tile_page=True)

    for spec in pages:
        dump = audit_pages.open_category_page(rig, spec.cat, spec.page)
        record(spec.key, dump, spec.rows)

    for spec in schedule_pages:
        dump = spec.open(rig)
        record(spec.key, dump, spec.rows)

    close_shell(rig)


def leak_phase(rig, report, venue, page_key, baseline):
    """Device only: open and close the largest page 20 times, then compare
    the menu screen's int_free and hot_fail against the warm-up baseline.
    Both figures are only comparable inside one boot, so an uptime that
    went backwards invalidates the whole phase."""
    if not venue.is_device:
        report.step("leak_phase", supported=0, reason="the simulator's heap figures are shims")
        return
    spec = next(p for p in audit_pages.CATEGORY_PAGES if p.key == page_key)
    # Each command is waited out (audit_pages.command): the route answers
    # 409 while the previous command is still in flight, and on the device
    # a UI pass is long enough for back-to-back commands to collide.
    for _ in range(LEAK_CYCLES):
        audit_pages.command(rig, open=1)
        audit_pages.command(rig, cat=spec.cat)
        if spec.page:
            audit_pages.command(rig, page=spec.page)
        audit_pages.command(rig, close=1)
    close_shell(rig)
    after = heap_sample(rig)
    report.number("leak_int_free_before", baseline["heap"]["int_free"])
    report.number("leak_int_free_after", after["int_free"])
    report.number("leak_hot_fail_before", baseline["heap"]["hot_fail"])
    report.number("leak_hot_fail_after", after["hot_fail"])
    report.step("leak_phase", cycles=LEAK_CYCLES, page=page_key,
                int_free_before=baseline["heap"]["int_free"], int_free_after=after["int_free"],
                hot_fail=after["hot_fail"])
    if int(after.get("uptime_ms", 0)) < int(baseline["heap"].get("uptime_ms", 0)):
        report.violation("LEAK PHASE INVALID", detail="uptime_ms reset during the phase; the venue rebooted")
        return
    drop = int(baseline["heap"]["int_free"]) - int(after["int_free"])
    if drop > LEAK_INT_FREE_TOLERANCE:
        report.violation("LEAK", key="int_free", before=baseline["heap"]["int_free"], after=after["int_free"],
                         tolerance=LEAK_INT_FREE_TOLERANCE)
    if int(after["hot_fail"]) != int(baseline["heap"]["hot_fail"]):
        report.violation("LEAK", key="hot_fail", before=baseline["heap"]["hot_fail"], after=after["hot_fail"])


# Every settings field a scenario is allowed to move, so the runner can tell
# a value a scenario failed to put back from one it never touched. Anything
# outside this list that changes is reported too, under the same heading.
def settings_snapshot(rig):
    s = rig.settings()
    return {k: v for k, v in s.items() if not isinstance(v, (dict, list))}


def report_restore_failures(report, scenario, before, after):
    for key in sorted(set(before) | set(after)):
        b, a = before.get(key), after.get(key)
        if b != a:
            report.finding("RESTORE FAILED", scenario=scenario, key=key, expected=b, actual=a)


def run_scenarios(rig, report, venue, selected, preflight_result):
    """Each scenario's run(rig, report, venue) in a try/finally, with a
    settings snapshot around it so a value it failed to put back is named
    in the report. A scenario the preflight blocked is failed without being
    run, so its unreachable starting value is never stepped away from."""
    import importlib

    for name in SCENARIO_ORDER:
        if name not in selected:
            continue
        entry = {"status": None, "checks": None, "failed": None, "seconds": None, "detail": None}
        report.scenarios[name] = entry
        if preflight_result.blocks(name):
            entry["status"] = "blocked"
            entry["detail"] = "preflight reported an unsupported fixture for this scenario"
            report.violation("SCENARIO BLOCKED", scenario=name)
            continue
        module = importlib.import_module("tools.settings_ui_tests.test_%s" % name)
        before = settings_snapshot(rig)
        t0 = time.time()
        extra_failed = 0
        try:
            module.run(rig, report, venue)
            entry["status"] = "pass"
        except RowMissing as e:
            # A row the scenario needed is not on the page: one failed check
            # naming the row, and the run goes on (gm-agh9).
            entry["status"] = "fail"
            entry["detail"] = str(e)
            extra_failed = 1
            report.violation("ROW MISSING", scenario=name, row=e.row, role=e.role, detail=str(e),
                             traceback=traceback.format_exc())
        except AssertionError as e:
            entry["status"] = "fail"
            entry["detail"] = str(e)
            report.violation("SCENARIO FAILED", scenario=name, detail=str(e))
        except Exception as e:  # noqa: BLE001 -- one scenario's crash must not end the run
            entry["status"] = "error"
            entry["detail"] = "%s: %s" % (type(e).__name__, e)
            report.violation("SCENARIO ERROR", scenario=name, detail=entry["detail"],
                             traceback=traceback.format_exc())
        finally:
            entry["seconds"] = round(time.time() - t0, 1)
            entry["checks"] = getattr(module, "TOTAL", None)
            entry["failed"] = len(getattr(module, "FAILURES", [])) + extra_failed
            close_shell(rig)
            report.step("scenario", name=name, status=entry["status"], seconds=entry["seconds"],
                        checks=entry["checks"], failed=entry["failed"])
        missing = instruments_answer(rig)
        if missing is not None:
            report.violation("MISSING INSTRUMENT", after_scenario=name, route=missing)
            return
        report_restore_failures(report, name, before, settings_snapshot(rig))


def restart_round_trip(rig, report, venue):
    """Steps Standby brightness one step through the UI, restarts through
    the Status page's confirm row, checks that the venue really rebooted
    and that the value survived, and puts it back the same way. A row the
    trip needs that is not on its page is one violation naming the row, and
    the run goes on to its report (gm-agh9)."""
    try:
        _restart_round_trip(rig, report, venue)
    except RowMissing as e:
        report.violation("RESTART ROW MISSING", row=e.row, role=e.role, detail=str(e))
        close_shell(rig)


def _uptime_ms(rig):
    return int(rig.heap()["uptime_ms"])


def _wait_for_restart(rig, report, venue, uptime_before):
    """After the Restart hold: waits for the venue to go down and come back,
    and returns its uptime_ms afterwards, or None when it never answered.

    The simulator is relaunched only once its process has exited on its
    own, which is what ESP.restart() does there; relaunching it regardless
    would make a hold that did not restart look like one that did. The
    device is polled until its uptime is below the one read before the
    hold, or until the timeout, and then read once more."""
    if venue.sim is not None:
        proc = venue.sim.proc
        t0 = time.time()
        while time.time() - t0 < SIM_EXIT_TIMEOUT_S and proc.poll() is None:
            time.sleep(0.2)
        if proc.poll() is None:
            report.step("restart_no_exit", waited_s=SIM_EXIT_TIMEOUT_S)
        else:
            report.step("restart_exit", code=proc.returncode)
            venue.sim.restart()
    else:
        t0 = time.time()
        while time.time() - t0 < DEVICE_RESTART_TIMEOUT_S:
            try:
                if _uptime_ms(rig) < uptime_before:
                    break
            except (RigHTTPError, KeyError, ValueError):
                pass
            time.sleep(2.0)
    try:
        return _uptime_ms(rig)
    except (RigHTTPError, KeyError, ValueError):
        return None


def _restart_round_trip(rig, report, venue):
    dump = audit_pages.open_category_page(rig, audit_pages.CAT_DISPLAY, 0)
    current = int(rig.row_value(dump, "Standby brightness"))
    direction = "plus" if current < 16 else "minus"
    expected = current + 1 if direction == "plus" else current - 1
    rig.tap_target(rig.require_tag(dump, "Standby brightness", direction))
    shown = int(rig.row_value(rig.touchmap(screen=0), "Standby brightness"))
    if shown != expected:
        report.violation("RESTART SETUP", detail="Standby brightness showed %d, wanted %d" % (shown, expected))
        close_shell(rig)
        return

    audit_pages.command(rig, cat=audit_pages.CAT_STATUS)
    audit_pages.command(rig, page=1)
    status_dump = rig.touchmap(screen=0)
    confirm = rig.require_tag(status_dump, "Restart", "confirm")
    uptime_before = _uptime_ms(rig)
    report.step("restart_hold", before=current, bumped_to=expected, uptime_ms=uptime_before)
    try:
        rig.tap_target(confirm, ms=2500)
    except (RigHTTPError, TimeoutError):
        pass  # the venue went away mid-tap, which is what a restart looks like

    uptime_after = _wait_for_restart(rig, report, venue, uptime_before)
    report.number("restart_uptime_ms_before", uptime_before)
    report.number("restart_uptime_ms_after", uptime_after)
    if uptime_after is None:
        report.violation("RESTART TIMEOUT", seconds=DEVICE_RESTART_TIMEOUT_S if venue.sim is None else SIM_EXIT_TIMEOUT_S)
        return
    report.step("restart_back", venue="simulator" if venue.sim is not None else "device",
                uptime_ms=uptime_after)
    restarted = uptime_after < uptime_before
    if not restarted:
        # The hold did not reboot the venue (a failed flush shows "Save
        # failed, hold to retry" and stays up), so the value check below
        # would prove nothing; the bump is still put back.
        report.violation("RESTART NOT OBSERVED", uptime_ms_before=uptime_before, uptime_ms_after=uptime_after)
        close_shell(rig)

    after = int(rig.settings_value("standbyBrightness"))
    report.number("restart_standby_brightness_before", current)
    report.number("restart_standby_brightness_after", after)
    if restarted and after != expected:
        report.violation("RESTART LOST VALUE", key="standbyBrightness", expected=expected, actual=after)

    back = _open_after_boot(rig, report, audit_pages.CAT_DISPLAY, 0)
    opposite = "minus" if direction == "plus" else "plus"
    rig.tap_target(rig.require_tag(back, "Standby brightness", opposite))
    close_shell(rig)
    # The close is queued to the UI task and the field is deferred, so the
    # write lands on the pass that tears the page down, not at the HTTP
    # return; poll for it (the device measured the immediate read stale).
    try:
        rig.wait_until(lambda: int(rig.settings_value("standbyBrightness")) == current, timeout=6)
    except TimeoutError:
        pass
    restored = int(rig.settings_value("standbyBrightness"))
    if restored != current:
        report.finding("RESTORE FAILED", scenario="restart", key="standbyBrightness",
                       expected=current, actual=restored)


def _open_after_boot(rig, report, cat, page, timeout=60.0):
    """open_category_page on a venue that has just booted. The device keeps
    changing screens for a while after HTTP is back (the controller link
    comes up and the flow picks its screen), and each change closes the
    cover through onExternalLeave; a fourth device run saw the open land
    and the following category command find the shell closed. So the open
    is retried until it holds, for up to `timeout` seconds."""
    deadline = time.time() + timeout
    attempt = 0
    while True:
        attempt += 1
        try:
            return audit_pages.open_category_page(rig, cat, page)
        except (RuntimeError, RigHTTPError, TimeoutError) as e:
            if time.time() > deadline:
                raise
            report.step("restart_open_retry", attempt=attempt, detail=str(e)[:120])
            time.sleep(3.0)


# ---- assembly ---------------------------------------------------------------


def summarise(report, venue):
    print()
    print("Pages")
    print("  %-22s %7s %11s %8s" % ("page", "targets", "smallest", "fps"))
    for p in report.pages:
        smallest = "x".join(str(v) for v in (p["smallest"] or ("?", "?")))
        fps = (p["rate"] or {}).get("fps", "n/a")
        print("  %-22s %7s %11s %8s" % (p["page"], p["targets"], smallest, fps))
    print()
    print("Scenarios")
    for name, e in report.scenarios.items():
        secs = "-" if e["seconds"] is None else "%.1fs" % e["seconds"]
        print("  %-12s %-8s %7s  checks=%s failed=%s" %
              (name, e["status"], secs, e["checks"] if e["checks"] is not None else "-",
               e["failed"] if e["failed"] is not None else "-"))
    print()
    for key in ("dma_free_sampled_min", "dma_min", "int_free_sampled_min", "hot_fail"):
        if key in report.numbers:
            print("  %s = %s" % (key, report.numbers[key]))
    if not venue.is_device:
        print("  frame rate and heap: recorded, not asserted (simulator)")
    print()
    if report.findings:
        print("Recorded, not asserted (%d):" % len(report.findings))
        for f in report.findings:
            rest = " ".join("%s=%s" % (k, val) for k, val in f.items() if k != "kind")
            print("  %s %s" % (f["kind"], rest))
        print()
    if report.violations:
        print("FAIL (%d violation(s)):" % len(report.violations))
        for v in report.violations:
            rest = " ".join("%s=%s" % (k, val) for k, val in v.items() if k != "kind")
            print("  %s %s" % (v["kind"], rest))
    else:
        print("PASS")


def collect_heap_samples(rig, report, samples):
    """One more heap read, folded into the run's sampled minima. `dma_min`
    is the venue's own boot-lifetime minimum and a different quantity from
    the minimum of these samples, so both go into the report under their
    own names."""
    s = heap_sample(rig)
    samples.append(s)
    dma = [int(x["dma_free"]) for x in samples if x.get("dma_free") is not None]
    ints = [int(x["int_free"]) for x in samples if x.get("int_free") is not None]
    if dma:
        report.number("dma_free_sampled_min", min(dma))
    if ints:
        report.number("int_free_sampled_min", min(ints))
    if s.get("dma_min") is not None:
        report.number("dma_min", s["dma_min"])
    if s.get("hot_fail") is not None:
        report.number("hot_fail", s["hot_fail"])
    return s


def run(args):
    report_dir = os.path.abspath(args.report_dir)
    os.makedirs(report_dir, exist_ok=True)
    report = Report(report_dir)
    only = None
    if args.only:
        only = {n.strip() for n in args.only.split(",") if n.strip()}
        unknown = only - set(SCENARIO_ORDER)
        if unknown:
            print("unknown scenario(s): %s (known: %s)" % (", ".join(sorted(unknown)), ", ".join(SCENARIO_ORDER)),
                  file=sys.stderr)
            return 2
    selected = set(SCENARIO_ORDER) if only is None else only

    sim = None
    try:
        if args.host:
            rig = Rig(args.host)
            venue = Venue(sim=None, program=None, workdir=report_dir, port=None, host=args.host,
                          log_path=None, is_device=True, skip_restart=args.skip_restart)
            report.step("venue", kind="device", host=args.host)
        else:
            if not os.path.isfile(args.sim_program):
                print("simulator binary not found at %r; build it first: pio run -e display-sim" % args.sim_program,
                      file=sys.stderr)
                report.violation("RUNNER ERROR", detail="simulator binary not found at %r" % args.sim_program)
                return 2
            workdir = os.path.join(report_dir, "sim")
            data_dir = os.path.join(workdir, "sim_data")
            os.makedirs(workdir, exist_ok=True)
            seeded = fixtures.seed(data_dir, args.fixture)
            report.step("seed", path=seeded)
            sim = Sim(args.sim_program, data_dir, port=args.sim_port)
            sim.__enter__()
            rig = sim.rig
            venue = Venue(sim=sim, program=args.sim_program, workdir=workdir, port=args.sim_port,
                          host="127.0.0.1:%d" % args.sim_port, log_path=sim.log_path, is_device=False,
                          skip_restart=args.skip_restart)
            report.step("venue", kind="simulator", port=args.sim_port, workdir=workdir)
    except BaseException as e:  # noqa: BLE001 -- a failed launch must not leave a PASS report behind
        report.violation("RUNNER ERROR", detail="%s: %s" % (type(e).__name__, e), traceback=traceback.format_exc())
        if sim is not None:
            sim.stop()
        path = report.write()
        print("report: %s" % path, flush=True)
        if not isinstance(e, Exception):
            raise
        return 1
    try:
        return drive_guarded(rig, report, venue, selected, only, args)
    finally:
        if sim is not None:
            sim.stop()
        path = report.write()
        print("report: %s" % path, flush=True)


def drive_guarded(rig, report, venue, selected, only, args):
    """drive(), with an exception anywhere in it recorded as a RUNNER ERROR
    violation carrying the traceback. Without this the report, written in
    run()'s finally, said PASS for a run that crashed half way: passed is
    only "no violations", and the crash had recorded none. An interrupt is
    recorded the same way and then re-raised."""
    try:
        return drive(rig, report, venue, selected, only, args)
    except BaseException as e:  # noqa: BLE001 -- recorded, then re-raised unless it is an ordinary error
        report.violation("RUNNER ERROR", detail="%s: %s" % (type(e).__name__, e),
                         traceback=traceback.format_exc())
        if venue.is_device:
            try:
                rig.synth(1)
            except (RigHTTPError, OSError):
                pass
        if not isinstance(e, Exception):
            raise
        try:
            summarise(report, venue)
        except Exception:  # noqa: BLE001 -- the report file is what matters now
            pass
        return 1


def drive(rig, report, venue, selected, only, args):
    if venue.is_device:
        try:
            rig.synth(0)
            report.step("synth", brew=0)
        except RigHTTPError as e:
            report.step("synth_unavailable", detail=str(e))
        try:
            pclk = rig.get_json("/api/debug/pclk")
            div = pclk.get("div", pclk.get("divider"))
            report.number("pclk_div", div)
            report.step("pclk", div=div)
            if div is not None and int(div) != EXPECTED_PCLK_DIV:
                report.note("pixel-clock divider is %s, not %d: rates in this report are not comparable with a "
                            "run at the build default (CLAUDE.md)" % (div, EXPECTED_PCLK_DIV))
                report.step("pclk_warning", div=div, expected=EXPECTED_PCLK_DIV)
        except RigHTTPError as e:
            report.step("pclk_unavailable", detail=str(e))

    # No log callback: every unmet condition is printed once, below, as the
    # violation it is, rather than twice.
    pre = fixtures.preflight(rig)
    report.numbers["preflight_unsupported"] = len(pre.unsupported)
    for missing in pre.missing_instruments:
        report.violation("MISSING INSTRUMENT", route=missing[0], detail=missing[1])
    if pre.missing_instruments:
        summarise(report, venue)
        return 1
    for u in pre.unsupported:
        if only is not None and u["scenario"] not in only:
            # A fixture for a scenario this run does not select is worth
            # knowing but is not this run's failure.
            report.note("unsupported fixture for unselected scenario %s: %s=%s (want %s)"
                        % (u["scenario"], u["key"], u["value"], u["want"]))
            continue
        report.violation("UNSUPPORTED FIXTURE", key=u["key"], value=u["value"], scenario=u["scenario"],
                         want=u["want"])
    if fixtures.RESTART_KEY in pre.blocked_keys:
        report.note("the restart round trip steps %s, which preflight reported unsupported" % fixtures.RESTART_KEY)

    samples = []
    collect_heap_samples(rig, report, samples)

    pages = audit_pages.category_pages(include_fixture=True)
    if only is not None:
        pages = [p for p in pages if p.scenario in only]
    sched_pages = schedule_pages_for(rig, only)

    warm = warm_up(rig, report, venue, pages, sched_pages)
    collect_heap_samples(rig, report, samples)

    audit_all_pages(rig, report, venue, pages, sched_pages, warm, audit_tiles=(only is None))
    collect_heap_samples(rig, report, samples)

    if pages:
        leak_phase(rig, report, venue, audit_pages.largest_page_key(pages), warm)
        collect_heap_samples(rig, report, samples)

    run_scenarios(rig, report, venue, selected, pre)
    collect_heap_samples(rig, report, samples)

    if args.skip_restart:
        report.step("restart", skipped=1, reason="--skip-restart")
    elif fixtures.RESTART_KEY in pre.blocked_keys:
        report.violation("RESTART SKIPPED", reason="preflight blocked %s" % fixtures.RESTART_KEY)
    else:
        restart_round_trip(rig, report, venue)
        collect_heap_samples(rig, report, samples)

    if venue.is_device:
        restore_all_screens(rig, report, warm.get("all_screens_restored"))
        try:
            rig.synth(1)
            report.step("synth", brew=1)
        except RigHTTPError:
            pass

    summarise(report, venue)
    return 0 if report.passed else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=None, help="drive the bench device at this ip[:port] instead of the simulator")
    ap.add_argument("--sim-program", default=DEFAULT_PROGRAM)
    ap.add_argument("--sim-port", type=int, default=int(os.environ.get("GM_SIM_PORT", "8080")))
    ap.add_argument("--only", default=None, help="comma-separated scenario names (%s)" % ",".join(SCENARIO_ORDER))
    ap.add_argument("--skip-restart", action="store_true", help="skip the restart round trip and every check that reboots")
    ap.add_argument("--fixture", default=fixtures.FIXTURE_PATH,
                    help="the NVS fixture the simulator is seeded from; an edited copy is how a value the "
                         "display cannot reach is put in front of the preflight")
    ap.add_argument("--report-dir",
                    default=os.path.join(tempfile.gettempdir(), "gm_settings_ui_tests", "runner"),
                    help="where report.json and the per-page PNGs are written")
    args = ap.parse_args(argv)
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
