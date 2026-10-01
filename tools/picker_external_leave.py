# A real brew start and a real standby timeout, taken while the gradient
# picker is open (gm-nov3.11).
#
# tools/settings_ui_tests/test_animation.py's picker teardown check drives
# settingsui(close=1), because neither real trigger exists on the simulator:
# it boots in MODE_STANDBY, so the standby timeout never fires there, and
# /api/debug/synth is compiled out. Both are reachable on a loadtest board,
# and this script is what reaches them.
#
# What a trigger has to do (DefaultUI::handleScreenChange, SettingsUI.cpp):
# call SettingsUI::onExternalLeave() before the flow changes screen, which
# commits the open category and tears the whole cover down, leaving nothing on
# the screen the flow engine is about to rebuild.
#
# Each case: set a field through the picker, leave the picker open (at the
# group level or the gradient level), fire the real trigger, then ask. Did the
# screen change? Did the shell close with it? Is the value still in
# /api/settings? Is any settings object left on the new screen? Does the next
# visit work, picker included?
#
# Two things this script learned on the bench and encodes:
#
#   * CatAnimation writes every visual field the moment it changes rather than
#     at commit, so a gradient chosen in the picker is in Settings before the
#     trigger fires. What the leave has to do is keep it, not write it.
#   * The board can sit in MODE_STANDBY while the menu screen is showing, and
#     Controller::loopLogic only calls activateStandby() when the mode is not
#     already standby. So the standby case takes the machine out of standby
#     first, by tapping the menu's brew tile, or the timeout it is waiting for
#     can never fire.
#
# --case none is the control: arm nothing, wait the same time, and show that
# the shell stays open. Without it a teardown cannot be attributed to the
# trigger that was armed.
#
# Usage (loadtest device only):
#
#   python3 tools/picker_external_leave.py --host 192.168.1.121 --case none
#   python3 tools/picker_external_leave.py --host 192.168.1.121 --case brew
#   python3 tools/picker_external_leave.py --host 192.168.1.121 --case standby
#
# --case standby needs standbyTimeout already low; the script refuses to run
# if it is longer than the wait. Lower and restore it through the web UI:
#   tools/web_set_field.py --field standbyTimeout --value 60
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.settings_ui_tests.rig import Rig  # noqa: E402

ANIMATION_CAT = 2
MODE_STANDBY = 0
# The menu's brew tile, the top-left of the four on the 140 px ring
# (DefaultUI's menu effect; CLAUDE.md's on-display settings section).
BREW_TILE = (141, 141)

FAILURES = []
TOTAL = 0


def check(name, cond, detail=""):
    global TOTAL
    TOTAL += 1
    if cond:
        print("  ok   %s" % name, flush=True)
    else:
        FAILURES.append(name)
        print("  FAIL %s %s" % (name, detail), flush=True)
    return bool(cond)


def depth(rig):
    return int(rig.settingsui_state().get("depth", 0))


def screen_id(rig):
    return rig.touchmap(screen=0)["screen_id"]


def mode(rig):
    return rig.get_json("/api/status").get("mode")


def goto_page(rig, page):
    rig.settingsui(page=page)
    rig.wait_until(lambda: rig.settingsui_state().get("page") == page, timeout=5)
    return rig.touchmap(screen=0)


def page_with_row(rig, name):
    pages = int(rig.settingsui_state().get("pages", 1))
    for page in range(pages):
        dump = goto_page(rig, page)
        if rig.find_tag(dump, name, "value") is not None or rig.find_tag(dump, name, "action") is not None:
            return dump
    raise AssertionError("no row %r on any of the %d pages" % (name, pages))


def open_animation(rig):
    if rig.settingsui_state().get("open") is True:
        rig.settingsui(close=1)
        rig.wait_until(lambda: rig.settingsui_state().get("open") is False, timeout=10)
    rig.settingsui(open=1)
    rig.wait_until(lambda: rig.settingsui_state().get("open") is True, timeout=30)
    rig.settingsui(cat=ANIMATION_CAT)
    rig.wait_until(lambda: rig.settingsui_state().get("category") == ANIMATION_CAT, timeout=10)


def close_shell(rig):
    if rig.settingsui_state().get("open") is True:
        rig.settingsui(close=1)
        rig.wait_until(lambda: rig.settingsui_state().get("open") is False, timeout=10)


def tap_row(rig, name):
    dump = page_with_row(rig, name)
    target = rig.find_tag(dump, name, "action")
    if target is None:
        raise AssertionError("row %r is not a whole-row target" % name)
    before = depth(rig)
    rig.tap_target(target)
    rig.wait_until(lambda: depth(rig) == before + 1, timeout=10)


def tap_choice(rig, name):
    """Taps a picker row that ends the picker. A gradient row pops both
    levels and the Global row on the first page pops one, so this waits for
    the picker to be gone rather than for a fixed drop."""
    dump = page_with_row(rig, name)
    target = rig.find_tag(dump, name, "action")
    if target is None:
        raise AssertionError("no picker row %r on its page" % name)
    rig.tap_target(target)
    rig.wait_until(lambda: depth(rig) == 1, timeout=10)


def map_slot(theme_map, anim_id):
    parts = (theme_map or "").split(";")
    return parts[anim_id] if anim_id < len(parts) else ""


def settings_objects(rig):
    """Every object on the active screen carrying a settings debug tag. After
    a teardown there must be none: only the shell's own objects have them."""
    try:
        dump = rig.touchmap(screen=0)
    except Exception as e:  # noqa: BLE001 -- a dropped read is not a result
        return ["touchmap failed: %s" % e]
    return [o.get("tag") for o in dump["objects"] if o.get("tag")]


def leave_standby(rig):
    """Takes the machine out of MODE_STANDBY by tapping the menu's brew tile,
    so the standby timeout has something to fire against."""
    if mode(rig) != MODE_STANDBY:
        return
    close_shell(rig)
    # open=1 is what brings the menu screen up; closing again leaves it there.
    rig.settingsui(open=1)
    rig.wait_until(lambda: rig.settingsui_state().get("open") is True, timeout=30)
    close_shell(rig)
    rig.tap(*BREW_TILE)
    left = rig.wait_until(lambda: mode(rig) != MODE_STANDBY, timeout=20)
    check("left_standby_before_the_case", bool(left), rig.get_json("/api/status"))


def wake(rig):
    """Taps the standby screen, which is one big wake target (the generated
    screen's action_on_wakeup), and waits for the screen to change."""
    before = screen_id(rig)
    rig.tap(240, 240)
    woke = rig.wait_until(lambda: screen_id(rig) != before, timeout=20)
    check("woke_from_standby", bool(woke), "screen stayed %r" % before)


def leave_case(rig, args, case, level):
    """One (trigger, picker level) pair."""
    label = "%s_level%d" % (case, level)
    print("\n%s" % label, flush=True)
    s0 = rig.settings()
    anim = int(s0["bgAnimId"])
    slot0 = map_slot(s0["bgAnimThemeMap"], anim)

    if case == "standby":
        leave_standby(rig)

    open_animation(rig)
    tap_row(rig, "Gradient")
    tap_row(rig, args.group)
    tap_choice(rig, args.name)
    stored_now = map_slot(rig.settings()["bgAnimThemeMap"], anim)
    check("%s_choice_reached_settings" % label, stored_now == args.ref,
          "stored %r, wanted %r (was %r)" % (stored_now, args.ref, slot0))

    # Back into the picker, left open at the wanted level.
    tap_row(rig, "Gradient")
    if level == 2:
        tap_row(rig, args.group)
    st = rig.settingsui_state()
    check("%s_picker_open_at_level" % label, int(st.get("depth", 0)) == 1 + level, st)
    scr_before = screen_id(rig)
    print("  screen before: %r, mode %r" % (scr_before, mode(rig)), flush=True)

    # Fire the trigger. No tap from here on: the brew path waits on the
    # synthetic lifecycle's clock and the standby path on the idle timer, and
    # a tap restarts both.
    if case == "brew":
        rig.synth(brew=True)
    wait_for = args.standby_wait if case == "standby" else args.brew_wait
    deadline = time.time() + wait_for
    trace = []
    last = None
    t0 = time.time()
    while time.time() < deadline:
        try:
            now = (mode(rig), screen_id(rig))
        except Exception:  # noqa: BLE001 -- a dropped read is not a result
            now = last
        if now != last:
            trace.append((round(time.time() - t0, 1), now))
            last = now
        if rig.settingsui_state().get("open") is not True:
            trace.append((round(time.time() - t0, 1), "shell closed"))
            break
        time.sleep(1.0)
    print("  (mode, screen) trace: %r" % trace, flush=True)

    st = rig.settingsui_state()
    if case == "none":
        check("%s_shell_stays_open_with_nothing_armed" % label, st.get("open") is True, st)
        check("%s_screen_did_not_change" % label, screen_id(rig) == scr_before, screen_id(rig))
        close_shell(rig)
        return

    check("%s_shell_closed_by_the_trigger" % label, st.get("open") is False, st)
    check("%s_depth_is_zero" % label, int(st.get("depth", 0)) == 0, st)
    scr_after = screen_id(rig)
    check("%s_screen_changed" % label, scr_after != scr_before, "%r -> %r" % (scr_before, scr_after))

    committed = map_slot(rig.settings()["bgAnimThemeMap"], anim)
    check("%s_kept_the_value_the_visit_set" % label, committed == args.ref,
          "slot %r, wanted %r" % (committed, args.ref))

    tags = settings_objects(rig)
    check("%s_no_settings_object_left_on_screen" % label, tags == [], tags[:8])

    # Recover, and prove the next visit is clean.
    if case == "brew":
        rig.synth(brew=False)
        time.sleep(2.0)
    else:
        wake(rig)
    open_animation(rig)
    st = rig.settingsui_state()
    check("%s_reopens_clean" % label, st.get("depth") == 1 and st.get("category") == ANIMATION_CAT, st)
    d = page_with_row(rig, "Gradient")
    check("%s_row_shows_what_was_kept" % label, rig.row_value(d, "Gradient") == args.name,
          rig.row_value(d, "Gradient"))
    tap_row(rig, "Gradient")
    check("%s_picker_opens_again" % label, depth(rig) == 2, rig.settingsui_state())
    rig.settingsui(pop=1)
    rig.wait_until(lambda: depth(rig) == 1, timeout=10)
    close_shell(rig)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--case", choices=("brew", "standby", "none"), required=True)
    ap.add_argument("--levels", default="1,2")
    ap.add_argument("--group", default="Coffee")
    ap.add_argument("--name", default="Espresso", help="the gradient to choose inside --group")
    ap.add_argument("--ref", default="0", help="the ref --name writes, checked against /api/settings")
    ap.add_argument("--brew-wait", type=float, default=90.0)
    ap.add_argument("--standby-wait", type=float, default=150.0)
    args = ap.parse_args(argv)

    rig = Rig(args.host)
    s0 = rig.settings()
    anim = int(s0["bgAnimId"])
    map0 = s0["bgAnimThemeMap"]
    print("host %s, animation %d, map slot %r, standbyTimeout %s, mode %r"
          % (args.host, anim, map_slot(map0, anim), s0.get("standbyTimeout"), mode(rig)), flush=True)

    if args.case == "standby":
        timeout = int(s0.get("standbyTimeout") or 0)
        if timeout <= 0 or timeout > args.standby_wait - 30:
            print("standbyTimeout is %s s; lower it through the web UI first "
                  "(tools/web_set_field.py --field standbyTimeout --value 60)" % timeout, flush=True)
            return 2
    if args.case in ("brew", "none"):
        print("synth before: %r" % rig.synth(), flush=True)

    for level in [int(v) for v in args.levels.split(",")]:
        # The slot must not already hold the target, or keeping it would
        # prove nothing. Clear it through the picker's own Global row.
        if map_slot(rig.settings()["bgAnimThemeMap"], anim) == args.ref:
            print("  slot already holds %r; clearing it through Global first" % args.ref, flush=True)
            open_animation(rig)
            tap_row(rig, "Gradient")
            tap_choice(rig, "Global")
            close_shell(rig)
        leave_case(rig, args, args.case, level)

    # Put the slot back the way it was found, through the picker.
    if map_slot(rig.settings()["bgAnimThemeMap"], anim) != map_slot(map0, anim):
        print("\nrestoring the map slot", flush=True)
        if map_slot(map0, anim) == "":
            open_animation(rig)
            tap_row(rig, "Gradient")
            tap_choice(rig, "Global")
            close_shell(rig)
        else:
            print("  slot was %r, which this script does not re-pick; restore it by hand"
                  % map_slot(map0, anim), flush=True)
    now = rig.settings()["bgAnimThemeMap"]
    check("map_restored", now == map0, "was %r now %r" % (map0, now))

    print("\n%s (%d checks, %d failed)" % ("PASS" if not FAILURES else "FAIL", TOTAL, len(FAILURES)),
          flush=True)
    for name in FAILURES:
        print("  failed: %s" % name, flush=True)
    return 0 if not FAILURES else 1


if __name__ == "__main__":
    sys.exit(main())
