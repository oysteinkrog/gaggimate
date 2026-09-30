#!/usr/bin/env python3
"""Scenario for gm-bzu.51: a press stays with the page it started on, and a
horizontal swipe at the first or last page clicks nothing.

Three things this checks, each against the failure the idf5 branch review
found (settings F1, F2, F4 and F5):

- A page change while a finger is down. The shell deletes the page under
  the finger; before the fix LVGL handed the still-held press to whatever
  the new page had at that point and the release clicked it. The check
  holds a tile (or a row) and, mid-hold, changes the page through
  /api/debug/settingsui, then asserts that the release clicked nothing on
  the new page. The Display tile sits exactly over the Fixture page's
  Toggle row, so a leaked press would turn it On.
- Plain tile taps and holds that open a category. The release must not
  click a row on the page that opens.
- Edge swipes. A swipe right on the first page and a swipe left on the
  last one turn nothing, and before the fix they also clicked the row the
  swipe started on. Checked on the Fixture page (toggle and action rows)
  and on the one-page Machine category (its "Auto wake-up" toggle, a real
  setting, put back by tapping if a swipe ever flips it).

It also audits the pages it visits and checks that the page title stays in
the same place on every page (the header arrows keep their slots).

Needs the Fixture category, so it runs on the simulator and on
GM_TOUCH_PROBE device builds. It writes no setting.

Usage:
    python3 tools/settings_ui_tests/test_press.py
        [--program PATH/to/.pio/build/display-sim/program]
        [--workdir DIR] [--port N] [--host ip[:port]]
"""
import argparse
import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO_ROOT)

from tools.settings_ui_tests import Rig, Sim  # noqa: E402
from tools.settings_ui_tests.audit_pages import (  # noqa: E402
    CAT_DISPLAY,
    CAT_FIXTURE,
    CAT_MACHINE,
    command,
    goto_page,
    open_category_page,
    open_tiles,
)
from tools.settings_ui_tests.fixtures import Venue  # noqa: E402
from tools.settings_ui_tests.rig import find_tag, row_value  # noqa: E402

DEFAULT_PROGRAM = os.path.join(REPO_ROOT, ".pio", "build", "display-sim", "program")

TILE_ROUNDS = 20
FIXTURE_PAGES = 3

FAILURES = []
TOTAL = 0


def check(rig, name, cond, detail=""):
    global TOTAL
    TOTAL += 1
    rig.log("check", name=name, ok=int(bool(cond)), detail=detail)
    if not cond:
        FAILURES.append((name, detail))
    return cond


def _centre(obj):
    x1, y1, x2, y2 = obj["hit"]
    return (x1 + x2) // 2, (y1 + y2) // 2


def _inside(obj, x, y):
    x1, y1, x2, y2 = obj["hit"]
    return x1 <= x <= x2 and y1 <= y <= y2


def _wait_release(rig, ms):
    deadline = time.time() + ms / 1000.0 + 10
    while time.time() < deadline:
        if rig.get_json("/api/debug/tap").get("released_at_ms"):
            # Same settle margin as Rig.tap: the UI task acts on the release
            # on its next pass.
            time.sleep(0.3)
            return
        time.sleep(0.02)
    raise TimeoutError("injected hold did not release")


def hold_and(rig, x, y, ms, after_s, action):
    """Starts an injected hold at (x, y) for ms, runs action() after_s into
    it, and returns once the hold has been released and acted on."""
    rig.get_json("/api/debug/tap?x=%d&y=%d&ms=%d" % (x, y, ms))
    time.sleep(after_s)
    action()
    _wait_release(rig, ms)


def fixture_state(rig):
    return rig.settingsui_state().get("fixture", {})


def check_hold_tile_then_open(rig, rnd):
    """Holds the Display tile and opens the Fixture category under the
    finger. The tile's centre is on the Fixture page's Toggle row, so a
    press that leaked to the new page would turn it On at the release."""
    dump = open_tiles(rig)
    tile = find_tag(dump, "Display", "tile")
    if not check(rig, "hold_open_r%d_tile_found" % rnd, tile is not None, ""):
        return
    x, y = _centre(tile)
    hold_and(rig, x, y, 900, 0.35, lambda: command(rig, cat=CAT_FIXTURE))
    st = rig.settingsui_state()
    dump = rig.touchmap(screen=0)
    toggle = find_tag(dump, "toggle", "toggle")
    if rnd == 0 and toggle is not None:
        # The check below only means something if the point is on the row.
        check(rig, "hold_open_point_on_toggle", _inside(toggle, x, y), "point=(%d,%d) toggle=%r" % (x, y, toggle["hit"]))
    check(rig, "hold_open_r%d_on_fixture" % rnd, st.get("category") == CAT_FIXTURE and st.get("depth") == 1, repr(st))
    value = row_value(dump, "toggle") if toggle is not None else None
    check(rig, "hold_open_r%d_toggle_untouched" % rnd, value == "Off", "toggle=%r" % value)
    fx = st.get("fixture", {})
    check(rig, "hold_open_r%d_no_action" % rnd, fx.get("action") == 0 and fx.get("confirm") == 0, repr(fx))


def check_tap_tile(rig, rnd, ms):
    """Taps (or holds, for larger ms) the Fixture tile itself and checks
    that its release opened the category and clicked nothing on it."""
    dump = open_tiles(rig)
    tile = find_tag(dump, "Fixture", "tile")
    if not check(rig, "tap_tile_r%d_tile_found" % rnd, tile is not None, ""):
        return
    before = rig.settingsui_state().get("seq")
    rig.tap_target(tile, ms=ms)
    st = rig.wait_until(
        lambda: (lambda s: s if s.get("category") == CAT_FIXTURE else None)(rig.settingsui_state()), 5
    )
    dump = rig.touchmap(screen=0)
    check(rig, "tap_tile_r%d_opened" % rnd, st.get("depth") == 1, "seq %r -> %r" % (before, st))
    value = row_value(dump, "toggle")
    check(rig, "tap_tile_r%d_toggle_untouched" % rnd, value == "Off", "toggle=%r" % value)
    fx = st.get("fixture", {})
    check(rig, "tap_tile_r%d_no_action" % rnd, fx.get("action") == 0 and fx.get("confirm") == 0, repr(fx))


def check_rebuild_mid_hold(rig):
    """Holds the Fixture page's Action row and rebuilds the page under the
    finger (page=0 on the debug route runs gotoPage, which rebuilds). The
    rebuilt Action row must not be clicked by the release."""
    dump = open_category_page(rig, CAT_FIXTURE, 0)
    action = find_tag(dump, "action", "action")
    if not check(rig, "rebuild_hold_action_found", action is not None, ""):
        return
    x, y = _centre(action)
    hold_and(rig, x, y, 1200, 0.4, lambda: rig.settingsui(page=0))
    fx = fixture_state(rig)
    check(rig, "rebuild_hold_no_click", fx.get("action") == 0, repr(fx))
    # And a normal tap on the same row still works afterwards: the dropped
    # press must not swallow the next one.
    rig.tap(x, y)
    fx = fixture_state(rig)
    check(rig, "rebuild_hold_next_tap_counts", fx.get("action") == 1, repr(fx))


def _swipe_row(rig, dump, row, role, direction):
    obj = find_tag(dump, row, role)
    if obj is None:
        return None
    x1, y1, x2, y2 = obj["hit"]
    y = (y1 + y2) // 2
    a, b = x1 + 30, x2 - 30
    if direction == "right":
        rig.swipe(a, y, b, y, ms=250)
    else:
        rig.swipe(b, y, a, y, ms=250)
    return obj


def check_fixture_edge_swipes(rig):
    """Swipe right on page 1 (the first page, nothing before it) from the
    Toggle row and from the Action row; swipe left on the last page. None
    turns the page and none clicks."""
    dump = open_category_page(rig, CAT_FIXTURE, 0)
    obj = _swipe_row(rig, dump, "toggle", "toggle", "right")
    if not check(rig, "edge_first_toggle_found", obj is not None, ""):
        return
    st = rig.settingsui_state()
    dump = rig.touchmap(screen=0)
    check(rig, "edge_first_stays", st.get("page") == 0, repr(st))
    check(rig, "edge_first_toggle_untouched", row_value(dump, "toggle") == "Off", row_value(dump, "toggle"))

    _swipe_row(rig, dump, "action", "action", "right")
    fx = fixture_state(rig)
    check(rig, "edge_first_action_untouched", fx.get("action") == 0, repr(fx))

    # A swipe that does turn the page clicks nothing either.
    _swipe_row(rig, dump, "action", "action", "left")
    st = rig.wait_until(lambda: (lambda s: s if s.get("page") == 1 else None)(rig.settingsui_state()), 5)
    check(rig, "swipe_turns_to_page_2", st is not None and st.get("page") == 1, repr(st))
    check(rig, "swipe_turn_action_untouched", st.get("fixture", {}).get("action") == 0, repr(st))

    dump = goto_page(rig, FIXTURE_PAGES - 1)
    rows = [o for o in dump["objects"] if (o.get("tag") or "").endswith("/row")]
    if not check(rig, "edge_last_has_row", bool(rows), ""):
        return
    x1, y1, x2, y2 = rows[0]["hit"]
    y = (y1 + y2) // 2
    rig.swipe(x2 - 30, y, x1 + 30, y, ms=250)
    st = rig.settingsui_state()
    check(rig, "edge_last_stays", st.get("page") == FIXTURE_PAGES - 1, repr(st))
    fx = st.get("fixture", {})
    check(rig, "edge_last_no_action", fx.get("action") == 0 and fx.get("confirm") == 0, repr(fx))


def check_machine_edge_swipes(rig):
    """Machine is one page, so both directions are an edge. Swiping across
    "Auto wake-up" must leave it as it was."""
    dump = open_category_page(rig, CAT_MACHINE, 0)
    if find_tag(dump, "Auto wake-up", "toggle") is None:
        check(rig, "machine_toggle_found", False, "no Auto wake-up toggle")
        return
    before = row_value(dump, "Auto wake-up")
    for direction in ("left", "right"):
        _swipe_row(rig, dump, "Auto wake-up", "toggle", direction)
        dump = rig.touchmap(screen=0)
        after = row_value(dump, "Auto wake-up")
        ok = check(rig, "machine_edge_%s_untouched" % direction, after == before, "%r -> %r" % (before, after))
        if not ok:
            # Put the setting back before the Machine page commits it.
            rig.tap_target(find_tag(dump, "Auto wake-up", "toggle"))
            dump = rig.touchmap(screen=0)
    command(rig, pop=1)


def check_audit_and_title(rig):
    """Every Fixture page audits clean, the header arrows are targets only
    where there is a page to go to, and the title keeps its place."""
    title_x = []
    for page in range(FIXTURE_PAGES):
        dump = open_category_page(rig, CAT_FIXTURE, page) if page == 0 else goto_page(rig, page)
        res = rig.audit(dump)
        check(rig, "audit_fixture_p%d" % page, not res["violations"], repr(res["violations"]))
        names = {o.get("tag") for o in rig.targets(dump)}
        check(rig, "prev_arrow_p%d" % page, ("page_prev/page_prev" in names) == (page > 0), repr(sorted(n for n in names if n)))
        check(rig, "next_arrow_p%d" % page, ("page_next/page_next" in names) == (page < FIXTURE_PAGES - 1),
              repr(sorted(n for n in names if n)))
        slots = [o for o in rig.targets(dump) if (o.get("tag") or "").endswith("/slot")]
        check(rig, "slots_not_targets_p%d" % page, not slots, repr([o.get("tag") for o in slots]))
        titles = [o for o in dump["objects"] if o.get("t") == "Fixture"]
        if titles:
            title_x.append(titles[0]["x1"])
    check(rig, "title_does_not_move", len(title_x) == FIXTURE_PAGES and len(set(title_x)) == 1, repr(title_x))


def _sequence(rig, venue):
    for rnd in range(TILE_ROUNDS):
        if rnd % 2 == 0:
            check_hold_tile_then_open(rig, rnd)
        else:
            check_tap_tile(rig, rnd, ms=(80, 400, 700)[(rnd // 2) % 3])
    check_rebuild_mid_hold(rig)
    check_fixture_edge_swipes(rig)
    check_machine_edge_swipes(rig)
    check_audit_and_title(rig)
    open_tiles(rig)
    command(rig, close=1)


def run(rig, report, venue):
    """Entry point for the end-to-end runner (tools/settings_ui_test.py)."""
    first_fail, first_total = len(FAILURES), TOTAL
    _sequence(rig, venue)
    report.step("scenario_checks", scenario="press", checks=TOTAL - first_total, failed=len(FAILURES) - first_fail)
    new_failures = FAILURES[first_fail:]
    if new_failures:
        raise AssertionError("; ".join("%s: %s" % (n, d) for n, d in new_failures))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--program", default=DEFAULT_PROGRAM)
    ap.add_argument("--workdir", default=os.path.join(tempfile.gettempdir(), "gm_settings_ui_tests", "test_press"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("GM_SIM_PORT", "8093")))
    ap.add_argument("--host", default=None, help="run against a live GM_TOUCH_PROBE device or sim instead of launching one")
    args = ap.parse_args()

    if args.host:
        rig = Rig(args.host)
        _sequence(rig, Venue(host=args.host, workdir=args.workdir, is_device=True))
    else:
        if not os.path.isfile(args.program):
            print("simulator binary not found at %r; build it first: pio run -e display-sim" % args.program, file=sys.stderr)
            return 1
        os.makedirs(args.workdir, exist_ok=True)
        with Sim(args.program, os.path.join(args.workdir, "sim_data"), port=args.port) as sim:
            rig = sim.rig
            rig.log("boot", program=args.program, port=args.port)
            _sequence(rig, Venue(sim=sim, program=args.program, workdir=args.workdir, port=args.port,
                                 host="127.0.0.1:%d" % args.port, log_path=sim.log_path))

    print("%d checks, %d failed" % (TOTAL, len(FAILURES)))
    for name, detail in FAILURES:
        print("FAIL %s: %s" % (name, detail))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
