#!/usr/bin/env python3
"""Scenario for the global gradient's draft during a web save (gm-nov3.16).

CLAUDE.md's rule for the on-display settings is that a web save landing while
a category is open is per-field last writer wins: every row the user has not
touched refreshes to the web's value, and a field the user has touched keeps
this visit's value and wins at commit. The global gradient row broke the
second half of that. It was committed from the visit's draft but displayed
from the stored field, so between a web save and the exit the display named
one gradient, drew that gradient's ramp, marked it in the picker, and then
saved a different one.

The checks here drive the simulator's own web save route and read the row's
value text, the row's swatch pixels, the picker's marker and the field the
category commits, so a fix that corrects only one of the four is caught by
the rest. Every web save is read back from GET /api/settings before any
judgement is made about the UI, so no check here can pass by the POST being
rejected.

Usage:
    python3 tools/settings_ui_tests/test_gradientdraft.py
        [--program PATH/to/.pio/build/display-sim/program]
        [--workdir DIR] [--port N]

Exits 0 if every check passes, 1 otherwise. Simulator only: web_save refuses
to POST /api/settings against anything but the simulator's loopback, which is
the one venue where that stands in for the browser.
"""
import argparse
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO_ROOT)

from tools.settings_ui_tests import Sim  # noqa: E402
from tools.settings_ui_tests.gradients_gen import (  # noqa: E402
    GRADIENT_CATEGORIES,
    GRADIENT_CATEGORY_OF,
    GRADIENT_NAMES as THEME_NAMES,
)

# The navigation and reading helpers are test_animation.py's: this scenario
# drives the same category through the same rows and must not grow a second
# reading of them.
from tools.settings_ui_tests.test_animation import (  # noqa: E402
    close_animation,
    depth,
    map_write_ref,
    open_animation,
    open_picker,
    page_with_row,
    picker_cancel,
    picker_choose,
    picker_selected_rows,
    picker_tap,
    rows_across_pages,
    swatch_strip,
    web_save,
)

DEFAULT_PROGRAM = os.path.join(REPO_ROOT, ".pio", "build", "display-sim", "program")

# Everything the Animation category can write about gradients. The two
# "writes nothing" checks compare these byte for byte around a visit.
GRADIENT_FIELDS = ["bgAnimGradientRef", "bgAnimTheme", "bgAnimCustomTheme", "bgAnimThemeMap", "bgAnimGradients"]

FAILURES = []
TOTAL = 0

# The swatch each gradient draws on the "Gradient all" row, captured while it
# is the stored value and the visit has touched nothing. The touched-draft
# check compares against these rather than against a picker row's swatch, so
# it compares one widget with itself.
GROUND = {}

# What the venue held before the first check, so the last one can put it back.
# The scenarios run in one process against one NVS, and this one deliberately
# leaves the legacy custom fixture and a stepped frame rate behind.
STARTING = {}


def check(rig, name, cond, detail=""):
    global TOTAL
    TOTAL += 1
    rig.log("check", name=name, ok=int(bool(cond)), detail=detail)
    if not cond:
        FAILURES.append((name, detail))
    return cond


def close_shell(rig):
    """Leaves the settings cover closed, whatever the check did. A check that
    raised part way through a visit would otherwise hand the next one an open
    category, and the next check's first open would commit that visit's draft
    over the fixture it had just written."""
    try:
        if rig.settingsui_state().get("open") is True:
            close_animation(rig)
    except Exception as e:  # noqa: BLE001 -- reported, never fatal
        rig.log("close_shell_failed", reason=str(e))


def run_check(rig, name, fn):
    if not STARTING:
        s = rig.settings()
        STARTING.update({k: str(s.get(k, "")) for k in GRADIENT_FIELDS + ["bgAnimFps"]})
    try:
        fn(rig)
    except Exception as e:  # noqa: BLE001 -- one check's crash must not hide the rest
        check(rig, name + "_exception", False, "%s: %s" % (type(e).__name__, e))
    finally:
        close_shell(rig)


def three_builtins():
    """One built-in from each of the first three categories that has one:
    three distinct gradients with three distinct group rows, so a marker on
    the picker's group page names exactly one of them. Chosen by category
    rather than by index, so appending gradients to data/gradients.json does
    not move what this scenario drives."""
    picked = []
    for cat in GRADIENT_CATEGORIES:
        for i, name in enumerate(THEME_NAMES):
            if GRADIENT_CATEGORY_OF[i] == cat:
                picked.append({"ref": str(i), "name": name, "cat": cat})
                break
        if len(picked) == 3:
            break
    if len(picked) < 3:
        raise AssertionError("need three built-in categories, have %d" % len(picked))
    return picked


def store(rig, name, fields):
    """One web save, read back before anything is judged by it. Returns True
    when the write landed; every caller checks that first, because a rejected
    POST would otherwise leave the UI showing the old value and every
    assertion after it passing for the wrong reason."""
    web_save(rig, fields)
    landed = rig.wait_until(
        lambda: all(str(rig.settings().get(k, "")) == str(v) for k, v in fields.items()), timeout=6)
    now = rig.settings()
    return check(rig, name, bool(landed), "want %r got %r" % (fields, {k: now.get(k) for k in fields}))


def global_row(rig):
    """The "Gradient all" row's value text and its swatch pixels."""
    dump = page_with_row(rig, "Gradient all")
    return rig.row_value(dump, "Gradient all"), swatch_strip(rig, dump, "Gradient all")


def wait_row(rig, row, want, timeout=8):
    return rig.wait_until(lambda: rig.row_value(page_with_row(rig, row), row) == want, timeout=timeout)


def other_fps(current):
    for candidate in (35, 40, 45, 50, 25, 20):
        if candidate != int(current):
            return candidate
    return 30


def gradient_fields(rig):
    s = rig.settings()
    return {k: str(s.get(k, "")) for k in GRADIENT_FIELDS}


def swatch_detail(strip, draft, web):
    """What a swatch comparison saw, for the failure line. Empty on a match,
    so a passing check does not print a description of a failure."""
    if strip == draft:
        return ""
    if web is not None and strip == web:
        return "swatch moved to the web value"
    return "swatch is neither the draft's nor the web's"


def legacy_swatch_detail(strip, ground):
    """What a legacy swatch comparison saw. The interesting failure is the one
    the bead describes: the stored positions passed straight through, so the
    row drew the positional reading of a gradient the panel draws uniformly."""
    if strip == ground["uniform"]:
        return ""
    if strip is None:
        return "no swatch at all"
    if strip == ground["positioned"]:
        return "the stored positions were kept: this is the positional reading"
    return "neither the uniform nor the positional reading"


# ---------------------------------------------------------------------------
# Checks


def check_untouched_global_follows_web_save(rig):
    """Acceptance: with nothing touched, the row, the swatch and the picker's
    marker all follow a web save, and leaving the category writes nothing of
    its own. Also captures the two ground-truth swatches the next check
    compares against."""
    a, b, _c = three_builtins()

    if not store(rig, "untouched_setup_a", {"bgAnimGradientRef": a["ref"], "bgAnimTheme": int(a["ref"])}):
        return
    open_animation(rig)
    value_a, strip_a = global_row(rig)
    check(rig, "untouched_row_names_stored", value_a == a["name"], "%r want %r" % (value_a, a["name"]))
    check(rig, "untouched_row_has_swatch", strip_a is not None and len(set(strip_a)) > 1,
          "%r distinct colours" % (None if strip_a is None else len(set(strip_a))))
    GROUND[a["ref"]] = strip_a

    before = gradient_fields(rig)
    if not store(rig, "untouched_web_save_landed", {"bgAnimGradientRef": b["ref"], "bgAnimTheme": int(b["ref"])}):
        close_animation(rig)
        return
    followed = wait_row(rig, "Gradient all", b["name"])
    check(rig, "untouched_row_follows_web", bool(followed),
          rig.row_value(page_with_row(rig, "Gradient all"), "Gradient all"))
    value_b, strip_b = global_row(rig)
    GROUND[b["ref"]] = strip_b
    moved = strip_b is not None and strip_b != strip_a
    check(rig, "untouched_swatch_follows_web", moved, "" if moved else "swatch did not move off %s" % a["name"])

    open_picker(rig, "Gradient all")
    marked = picker_selected_rows(rig)
    check(rig, "untouched_picker_marks_web_group", marked == [b["cat"]], "%r want [%r]" % (marked, b["cat"]))
    picker_tap(rig, b["cat"])
    rig.wait_until(lambda: depth(rig) == 3, timeout=5)
    marked = picker_selected_rows(rig)
    check(rig, "untouched_picker_marks_web_gradient", marked == [b["name"]], "%r want [%r]" % (marked, b["name"]))
    picker_cancel(rig)
    picker_cancel(rig)
    value_after_cancel, _ = global_row(rig)
    check(rig, "untouched_row_after_cancel", value_after_cancel == b["name"],
          "%r want %r" % (value_after_cancel, b["name"]))

    close_animation(rig)
    after = gradient_fields(rig)
    want = dict(before, bgAnimGradientRef=b["ref"], bgAnimTheme=str(int(b["ref"])))
    check(rig, "untouched_exit_keeps_web_value", after == want, "%r want %r" % (after, want))


def check_touched_global_keeps_draft(rig):
    """Acceptance: the display picks A, the web saves B, B is confirmed in
    Settings, and the row, the swatch, the per-animation row's "Global (...)"
    text and the picker's marker all keep naming A until the exit, which
    commits A and its legacy mirror."""
    a, b, c = three_builtins()
    if a["ref"] not in GROUND:
        check(rig, "touched_ground_truth_available", False, "the untouched check captured no swatch")
        return

    s0 = rig.settings()
    anim = int(s0["bgAnimId"])
    # The per-animation Gradient row reads "Global (<name>)" only while that
    # animation has no override of its own, which is what makes it a second
    # witness of the global's draft.
    if not store(rig, "touched_setup", {
            "bgAnimGradientRef": b["ref"],
            "bgAnimTheme": int(b["ref"]),
            "bgAnimThemeMap": map_write_ref(s0["bgAnimThemeMap"], anim, "")}):
        return

    open_animation(rig)
    open_picker(rig, "Gradient all")
    picker_choose(rig, a["cat"], a["name"])
    live = rig.wait_until(lambda: rig.settings()["bgAnimGradientRef"] == a["ref"], timeout=6)
    check(rig, "touched_pick_writes_live", bool(live), rig.settings()["bgAnimGradientRef"])
    value, strip = global_row(rig)
    check(rig, "touched_row_after_pick", value == a["name"], "%r want %r" % (value, a["name"]))

    # The web save carries an untouched field as well, so waiting for that
    # row to move proves the category reconciled and rebuilt. Without it a
    # gradient row that had simply not been redrawn yet would read as a pass.
    fps_sentinel = other_fps(rig.settings()["bgAnimFps"])
    if not store(rig, "touched_web_save_landed", {
            "bgAnimGradientRef": b["ref"], "bgAnimTheme": int(b["ref"]), "bgAnimFps": fps_sentinel}):
        close_animation(rig)
        return
    rebuilt = wait_row(rig, "Frame rate", "%d fps" % fps_sentinel)
    check(rig, "touched_page_rebuilt", bool(rebuilt), rig.row_value(page_with_row(rig, "Frame rate"), "Frame rate"))

    value, strip = global_row(rig)
    check(rig, "touched_row_keeps_draft", value == a["name"], "%r want %r" % (value, a["name"]))
    check(rig, "touched_swatch_keeps_draft", strip == GROUND[a["ref"]], swatch_detail(strip, GROUND[a["ref"]],
                                                                                      GROUND.get(b["ref"])))
    per_anim = rig.row_value(page_with_row(rig, "Gradient"), "Gradient")
    check(rig, "touched_per_anim_row_names_draft", per_anim == "Global (%s)" % a["name"],
          "%r want %r" % (per_anim, "Global (%s)" % a["name"]))

    # Each picker level in turn, with another web save landing under the open
    # picker at the group level. That save also adds a library entry, so the
    # "My gradients" group appearing is proof the picker rebuilt on it: an
    # assertion about a marker that has simply not been redrawn yet would
    # pass whatever the code does.
    lib0 = rig.settings()["bgAnimGradients"]
    open_picker(rig, "Gradient all")
    marked = picker_selected_rows(rig)
    check(rig, "touched_picker_group_marks_draft", marked == [a["cat"]], "%r want [%r]" % (marked, a["cat"]))
    probe_lib = "|".join(("90", "Draft Probe", "000000,ffffff"))
    if store(rig, "touched_web_save_under_picker_landed", {
            "bgAnimGradientRef": c["ref"], "bgAnimTheme": int(c["ref"]), "bgAnimGradients": probe_lib}):
        rebuilt = rig.wait_until(lambda: "My gradients" in rows_across_pages(rig), timeout=8)
        check(rig, "touched_picker_rebuilt", bool(rebuilt), rows_across_pages(rig))
        marked = picker_selected_rows(rig)
        check(rig, "touched_picker_group_keeps_draft", marked == [a["cat"]], "%r want [%r]" % (marked, a["cat"]))
    picker_tap(rig, a["cat"])
    rig.wait_until(lambda: depth(rig) == 3, timeout=5)
    marked = picker_selected_rows(rig)
    check(rig, "touched_picker_gradient_marks_draft", marked == [a["name"]], "%r want [%r]" % (marked, a["name"]))
    picker_cancel(rig)
    picker_cancel(rig)
    value, strip = global_row(rig)
    check(rig, "touched_row_after_cancel", value == a["name"], "%r want %r" % (value, a["name"]))
    check(rig, "touched_swatch_after_cancel", strip == GROUND[a["ref"]], swatch_detail(strip, GROUND[a["ref"]],
                                                                                       GROUND.get(c["ref"])))

    close_animation(rig)
    store(rig, "touched_library_restored", {"bgAnimGradients": lib0})
    s1 = rig.settings()
    check(rig, "touched_exit_commits_draft", s1["bgAnimGradientRef"] == a["ref"],
          "%r want %r" % (s1["bgAnimGradientRef"], a["ref"]))
    # A built-in under 18 mirrors as itself (BgAnim.h, bg_legacy_mirror_for_ref),
    # so a build without bgAnimGradientRef draws the same gradient.
    check(rig, "touched_exit_mirrors_draft", int(s1["bgAnimTheme"]) == int(a["ref"]),
          "%r want %s" % (s1["bgAnimTheme"], a["ref"]))


# The retained legacy gradient the checks below drive, in three readings of
# the same three colours. The firmware's last fallback keeps the colours of
# bgAnimCustomTheme and throws its positions away, spacing them evenly on the
# uniform path (BgAnimThemes.cpp, bg_resolve_anim_theme step three). So the
# positioned string and the uniform library entry are the same gradient to the
# panel, and the positioned library entry is a different one: that is what lets
# the swatch checks below tell a correct preview from the passthrough that
# shipped (gm-nov3.18).
LEGACY_COLORS = "101828,3a4048,f5f7ff"
LEGACY_POSITIONED = "101828@0,3a4048@10,f5f7ff@255"
LEGACY_LIBRARY = "1|Uniform|%s;2|Positioned|%s" % (LEGACY_COLORS, LEGACY_POSITIONED)


def legacy_ground_truth(rig):
    """The two swatches the legacy checks compare against: the same colours
    drawn through the uniform path and through the positional one, both read
    off the "Gradient all" row so the comparison is one widget with itself."""
    out = {}
    for ref, key in (("c1", "uniform"), ("c2", "positioned")):
        if not store(rig, "legacy_ground_%s" % key, {
                "bgAnimGradients": LEGACY_LIBRARY,
                "bgAnimGradientRef": ref}):
            return None
        open_animation(rig)
        _value, strip = global_row(rig)
        close_animation(rig)
        if not check(rig, "legacy_ground_%s_has_swatch" % key, strip is not None and len(set(strip)) > 1,
                     "%r distinct colours" % (None if strip is None else len(set(strip)))):
            return None
        out[key] = strip
    ok = check(rig, "legacy_ground_truths_differ", out["uniform"] != out["positioned"],
               "the uniform and positional readings drew the same pixels, so the next checks prove nothing")
    return out if ok else None


def check_legacy_fallback_named_and_inert(rig):
    """Acceptance: with the pre-library custom gradient still the fallback,
    the row names it for what it is on both readings, every surface that
    follows the fallback samples the gradient the panel will actually draw,
    the picker marks nothing, and opening and cancelling writes no gradient
    setting."""
    ground = legacy_ground_truth(rig)
    if ground is None:
        return
    if not store(rig, "legacy_setup", {
            "bgAnimGradientRef": "",
            "bgAnimThemeMap": "",
            "bgAnimTheme": 18,
            "bgAnimCustomTheme": LEGACY_POSITIONED}):
        return
    before = gradient_fields(rig)

    open_animation(rig)
    value, strip = global_row(rig)
    check(rig, "legacy_row_named", value == "Custom (legacy)", "%r want 'Custom (legacy)'" % value)
    # No ref names the retained gradient, but the panel still draws it, so the
    # row samples it directly (CatAnimation.cpp, settingsGlobalGradientSwatch).
    check(rig, "legacy_row_has_swatch", strip is not None, strip)
    check(rig, "legacy_row_swatch_is_uniform", strip == ground["uniform"],
          legacy_swatch_detail(strip, ground))

    d = page_with_row(rig, "Gradient")
    per_anim = rig.row_value(d, "Gradient")
    check(rig, "legacy_per_anim_row_named", per_anim == "Global (Custom (legacy))",
          "%r want 'Global (Custom (legacy))'" % per_anim)
    per_anim_strip = swatch_strip(rig, d, "Gradient")
    check(rig, "legacy_per_anim_row_swatch_is_uniform", per_anim_strip == ground["uniform"],
          legacy_swatch_detail(per_anim_strip, ground))

    # The picker's Global entry, which only a per-animation picker offers: it
    # says what "follow the global" would draw.
    open_picker(rig, "Gradient")
    dump = page_with_row(rig, "Global")
    check(rig, "legacy_picker_global_named", rig.row_value(dump, "Global") == "Custom (legacy)",
          rig.row_value(dump, "Global"))
    picker_strip = swatch_strip(rig, dump, "Global")
    check(rig, "legacy_picker_global_swatch_is_uniform", picker_strip == ground["uniform"],
          legacy_swatch_detail(picker_strip, ground))
    picker_cancel(rig)

    open_picker(rig, "Gradient all")
    marked = picker_selected_rows(rig)
    check(rig, "legacy_picker_marks_nothing", marked == [], marked)
    picker_cancel(rig)
    close_animation(rig)

    after = gradient_fields(rig)
    check(rig, "legacy_visit_writes_nothing", after == before, "%r want %r" % (after, before))


def check_legacy_unparsable_custom_falls_back(rig):
    """Acceptance: bgAnimTheme 18 with a custom string the firmware's parser
    rejects is not the legacy stand-in at all. bg_resolve_theme sends an empty
    or malformed one to built-in 0, so the row names that built-in and draws
    its ramp, and nothing says "Custom (legacy)"."""
    builtin_zero = THEME_NAMES[0]
    for name, custom in (("empty", ""), ("malformed", "zzz"), ("one_stop", "101828")):
        if not store(rig, "legacy_%s_setup" % name, {
                "bgAnimGradientRef": "",
                "bgAnimThemeMap": "",
                "bgAnimTheme": 18,
                "bgAnimCustomTheme": custom}):
            continue
        before = gradient_fields(rig)
        open_animation(rig)
        value, strip = global_row(rig)
        check(rig, "legacy_%s_names_builtin_zero" % name, value == builtin_zero,
              "%r want %r" % (value, builtin_zero))
        check(rig, "legacy_%s_draws_a_ramp" % name, strip is not None and len(set(strip)) > 1,
              "%r distinct colours" % (None if strip is None else len(set(strip))))
        close_animation(rig)
        after = gradient_fields(rig)
        check(rig, "legacy_%s_visit_writes_nothing" % name, after == before, "%r want %r" % (after, before))


def check_starting_state_restored(rig):
    """Not an acceptance criterion: the scenarios share one NVS, and this one
    ends on the legacy custom fixture and a stepped frame rate, neither of
    which the next scenario should inherit."""
    store(rig, "starting_state_restored", STARTING)


CHECKS = [
    ("untouched_global_follows_web_save", check_untouched_global_follows_web_save),
    ("touched_global_keeps_draft", check_touched_global_keeps_draft),
    ("legacy_fallback_named_and_inert", check_legacy_fallback_named_and_inert),
    ("legacy_unparsable_custom_falls_back", check_legacy_unparsable_custom_falls_back),
    ("starting_state_restored", check_starting_state_restored),
]


def run(rig, report, venue):
    """Entry point for the end-to-end runner (tools/settings_ui_test.py).

    Simulator only. Every check here needs a web save, and web_save POSTs
    /api/settings, which the bead's rule forbids against a real device (that
    endpoint carries the WiFi password and the web UI is its only safe
    writer). On the device the scenario reports that it did not run rather
    than failing the run."""
    if venue is not None and venue.is_device:
        report.step("scenario_skipped", scenario="gradientdraft", reason="needs the simulator's web save route")
        return
    first_fail, first_total = len(FAILURES), TOTAL
    for name, fn in CHECKS:
        run_check(rig, name, fn)
    report.step("scenario_checks", scenario="gradientdraft", checks=TOTAL - first_total,
                failed=len(FAILURES) - first_fail)
    new_failures = FAILURES[first_fail:]
    if new_failures:
        raise AssertionError("; ".join("%s: %s" % (n, d) for n, d in new_failures))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--program", default=DEFAULT_PROGRAM)
    ap.add_argument("--workdir",
                    default=os.path.join(tempfile.gettempdir(), "gm_settings_ui_tests", "test_gradientdraft"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("GM_SIM_PORT", "8094")))
    args = ap.parse_args()

    if not os.path.isfile(args.program):
        print("simulator binary not found at %r; build it first: pio run -e display-sim" % args.program,
              file=sys.stderr)
        return 1
    data_dir = os.path.join(args.workdir, "sim_data")
    os.makedirs(args.workdir, exist_ok=True)
    with Sim(args.program, data_dir, port=args.port) as sim:
        rig = sim.rig
        rig.log("boot", program=args.program, port=args.port, workdir=args.workdir)
        for name, fn in CHECKS:
            run_check(rig, name, fn)

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
