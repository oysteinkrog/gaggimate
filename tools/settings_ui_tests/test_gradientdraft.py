#!/usr/bin/env python3
"""Scenario for a gradient's draft during a web save (gm-nov3.16, gm-nov3.23,
gm-nov3.36).

CLAUDE.md's rule for the on-display settings is that a web save landing while
a category is open is per-field last writer wins: every row the user has not
touched refreshes to the web's value, and a field the user has touched keeps
this visit's value and wins at commit. The global gradient row broke the
second half of that. It was committed from the visit's draft but displayed
from the stored field, so between a web save and the exit the display named
one gradient, drew that gradient's ramp, marked it in the picker, and then
saved a different one.

gm-nov3.16 fixed the display side and left the panel on the web's gradient
until the exit, so the page named one gradient while another was on screen.
gm-nov3.23 closed that: reconciliation puts the touched value back into the
stored field the panel resolves from, so there is one gradient everywhere for
the whole visit. check_touched_global_agrees_with_panel is that case.

The three per-animation rows write bgAnimThemeMap the same way, one slot per
animation, and had the same gap for the rest of the visit rather than for one
update pass. gm-nov3.36 closed it for every slot the visit touched, including
one whose animation is not the one on screen, while a slot the visit did not
touch still adopts whatever the web posted. The three checks named
touched_..._agrees_with_panel are those cases.

The checks here drive the simulator's own web save route and read the row's
value text, the row's swatch pixels, the picker's marker and the field the
category commits, so a fix that corrects only one of the four is caught by
the rest. Every web save is read back from GET /api/settings before any
judgement is made about the UI, so no check here can pass by the POST being
rejected. The saves that the open category is expected to overwrite are read
back on a field of the same form that the display never writes, because their
own gradient field is the thing under test (store_over_draft).

Since gm-nov3.31 the scenario also covers what a pick does to the picker
itself. A pick applies and stays on the page, so browsing is one tap per
gradient, and the three checks at the end of CHECKS are that: three picks in
one visit, the "Global" entry on the first page, and the one case that still
closes the picker under the user, the edited slot going away.

Usage:
    python3 tools/settings_ui_tests/test_gradientdraft.py
        [--program PATH/to/.pio/build/display-sim/program]
        [--workdir DIR] [--port N]

Exits 0 if every check passes, 1 otherwise. Simulator only: web_save refuses
to POST /api/settings against anything but the simulator's loopback, which is
the one venue where that stands in for the browser.
"""
import argparse
import json
import os
import sys
import tempfile
import time

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
    ANIM_NAMES,
    close_animation,
    depth,
    goto_page,
    gradient_name_for_ref,
    library_entries,
    map_ref,
    map_write_ref,
    open_animation,
    open_picker,
    page_with_row,
    picker_cancel,
    picker_choose,
    picker_selected_rows,
    picker_tap,
    restore_fields_exactly,
    rows_across_pages,
    swatch_strip,
    tap_row,
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
# leaves the legacy custom fixture and a stepped frame rate behind. The
# per-animation checks (gm-nov3.36) also step the Animation row and set a
# standby animation, so those two ids are captured here as well.
STARTING = {}
STARTING_FIELDS = GRADIENT_FIELDS + ["bgAnimFps", "bgAnimId", "bgAnimStandbyId"]

# The simulator's NVS file, sim_data/nvs/controller.json under the venue's
# working directory (preferences_shim.cpp hard-codes the relative path). Set
# by run() and by main(), and left None where there is none to watch, which is
# every device run. check_hold_repeats_without_flushing_per_step is the only
# user: it is the one question about these rows that the HTTP surface cannot
# answer, because GET /api/settings reads the in-memory properties and says
# nothing about what reached NVS.
NVS_PATH = None


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
        STARTING.update({k: str(s.get(k, "")) for k in STARTING_FIELDS})
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


def settled(rig, pred, timeout=8):
    """Rig.wait_until, returning False on a timeout instead of raising.

    A check that waits for several independent things has to report each of
    them: with the raising form the first one that never happens ends the
    check, and the rest of its assertions are never made at all, so a run
    against broken code says one thing failed when four did."""
    try:
        return bool(rig.wait_until(pred, timeout=timeout))
    except TimeoutError:
        return False


def store_over_draft(rig, name, fields, witness):
    """One web save whose gradient fields the open category is expected to
    overwrite within a UI pass, so those fields cannot be the proof that the
    POST landed.

    `witness` names the fields of the same form that the display never writes.
    The whole document goes in one request and WebUIPlugin::handleSettings
    applies it in one batchUpdate, so a witness arriving proves the request was
    accepted and applied. That the gradient ref carried with it is one the
    handler accepts rather than one bg_ref_valid drops is proved separately,
    against a closed shell, by the preflight in
    check_touched_global_agrees_with_panel."""
    web_save(rig, fields)
    landed = settled(rig, lambda: all(str(rig.settings().get(k, "")) == str(fields[k]) for k in witness), timeout=6)
    now = rig.settings()
    return check(rig, name, bool(landed), "witness %r got %r" % (
        {k: fields[k] for k in witness}, {k: now.get(k) for k in witness}))


def live_global(rig):
    """The two stored fields DefaultUI::updateState resolves the global
    gradient from and hands to the render task: bgAnimGradientRef and its
    legacy mirror bgAnimTheme.

    This is what "the live rendered gradient" means here. The simulator has no
    render loop at all (sim/platform/bganim_stub.cpp), so the panel cannot be
    sampled on this venue; these are the inputs it would be drawn from, and on
    the Animation page nothing but the global gradient row writes them."""
    s = rig.settings()
    return str(s.get("bgAnimGradientRef", "")), str(s.get("bgAnimTheme", ""))


def anim_slot(rig, anim_id):
    """The gradient ref bgAnimThemeMap holds for one animation.

    This is the live rendered gradient for that animation in the same sense
    live_global is for the global row: DefaultUI::updateState hands the stored
    map to bg_resolve_anim_theme, whose first step is this slot, and the
    simulator has no render loop to sample instead."""
    return map_ref(str(rig.settings().get("bgAnimThemeMap", "")), anim_id)


def anim_row_ground(rig, anim_id, base_map, refs):
    """The swatch the per-animation "Gradient" row draws for each of `refs`,
    captured while that ref is the stored value and the visit has touched
    nothing.

    Captured here rather than reused from GROUND because GROUND is the
    "Gradient all" row's swatch, and a comparison has to be one widget
    against itself. What it buys is a swatch check that says which gradient
    the row drew, not that it drew something."""
    ground = {}
    for ref in refs:
        if not store(rig, "anim_ground_ref_%s" % ref, {"bgAnimThemeMap": map_write_ref(base_map, anim_id, ref)}):
            continue
        open_animation(rig)
        ground[ref] = swatch_strip(rig, page_with_row(rig, "Gradient"), "Gradient")
        close_animation(rig)
    return ground


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
# Stepping from the row (gm-nov3.32)
#
# The three gradient rows carry three targets each now: a centre band that
# pushes the picker and a prev and a next arrow that step to the adjacent
# gradient without opening anything. The checks below are about the arrows;
# everything above still drives the band, through open_picker, which finds the
# row container by its "action" tag and taps its centre.


def step_order(rig, allow_global):
    """The refs a row's arrows walk, in order. Python mirror of
    CatAnimation.cpp's gradientStepOrder, which is CatGradientPicker's own
    order flattened: Global where the row offers it, then the saved
    gradients in stored order, then each declared category's built-ins in
    table order.

    Built from the stored library and the generated category table rather
    than from the picker's pages, so a walk that agreed with a broken picker
    would still fail here."""
    order = []
    if allow_global:
        order.append("")
    for entry_id, _name, _gradient in library_entries(rig.settings().get("bgAnimGradients", "")):
        order.append("c%d" % entry_id)
    for category in GRADIENT_CATEGORIES:
        for i in range(len(THEME_NAMES)):
            if GRADIENT_CATEGORY_OF[i] == category:
                order.append(str(i))
    return order


def arrow(rig, row, which):
    """One of a gradient row's two arrows ("prev" or "next") as a target."""
    dump = page_with_row(rig, row)
    target = rig.find_tag(dump, row, which)
    if target is None:
        raise AssertionError("row %r has no %r arrow" % (row, which))
    return target


def tap_arrow(rig, row, which):
    rig.tap_target(arrow(rig, row, which))


def category_of_ref(ref):
    """The picker group a ref belongs to, for a check that wants to open it."""
    if ref.startswith("c"):
        return "My gradients"
    return GRADIENT_CATEGORY_OF[int(ref)]


def middle_builtin(order):
    """A built-in with a neighbour on each side in the stepping order, so one
    step either way stays inside the list and neither direction is testing
    the wrap by accident."""
    for i, ref in enumerate(order):
        if i > 0 and i < len(order) - 1 and not ref.startswith("c") and ref != "":
            return i
    raise AssertionError("no built-in with neighbours in an order of %d" % len(order))


# The three rows this bead changed. Named once: two checks below walk pages
# looking for them.
GRADIENT_ROWS = ("Gradient all", "Gradient", "Standby grad")


def target_gap(a, b):
    """Pixels between two hit rects along whichever axis separates them, 0
    when they touch and negative when they overlap. Rig.audit already fails
    an overlap; this is the number that says how much room was left, which
    "no violations" does not."""
    dx = max(b[0] - a[2], a[0] - b[2])
    dy = max(b[1] - a[3], a[1] - b[3])
    return max(dx, dy) - 1


# The NVS key bgAnimThemeMap is stored under (Settings.h:410). Read rather
# than the whole file: Preferences::save() is fopen, fwrite, fclose, so a poll
# can catch a half-written file, and counting distinct file bytes would count
# torn reads as writes.
NVS_THEME_MAP_KEY = "bg_thm"


def nvs_theme_map():
    """What the simulator's NVS file says bgAnimThemeMap is, or None when
    there is nothing to read: no file for this venue, no key yet, or a poll
    that landed mid-write and did not parse."""
    if NVS_PATH is None:
        return None
    try:
        with open(NVS_PATH, "rb") as fp:
            return json.loads(fp.read().decode("utf-8")).get(NVS_THEME_MAP_KEY)
    except (OSError, ValueError, UnicodeDecodeError):
        return None


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
    # And the panel keeps it: an untouched field is adopted, so nothing in the
    # category may put the old value back (gm-nov3.23 must not reach a field
    # this visit has not touched).
    live_ref, live_theme = live_global(rig)
    check(rig, "untouched_panel_keeps_web", (live_ref, live_theme) == (b["ref"], str(int(b["ref"]))),
          "%r want %r" % ((live_ref, live_theme), (b["ref"], str(int(b["ref"])))))

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
    """Acceptance: the display picks A, the web saves B, the save is confirmed
    to have landed, and the row, the swatch, the per-animation row's
    "Global (...)" text and the picker's marker all keep naming A until the
    exit, which commits A and its legacy mirror.

    The confirmation is a witness field of the same form rather than B itself,
    because since gm-nov3.23 the category puts A back into the stored gradient
    field within a UI pass. That the gradient ref in the form is one the POST
    handler accepts is proved by the preflight in the check below."""
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
    if not store_over_draft(rig, "touched_web_save_landed", {
            "bgAnimGradientRef": b["ref"], "bgAnimTheme": int(b["ref"]), "bgAnimFps": fps_sentinel},
            ["bgAnimFps"]):
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
    if store_over_draft(rig, "touched_web_save_under_picker_landed", {
            "bgAnimGradientRef": c["ref"], "bgAnimTheme": int(c["ref"]), "bgAnimGradients": probe_lib},
            ["bgAnimGradients"]):
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


def check_touched_global_agrees_with_panel(rig):
    """Acceptance (gm-nov3.23): while the page names this visit's choice, the
    panel draws it too.

    gm-nov3.16 made the row, the swatch and the picker's marker describe the
    value the exit will commit. It left the panel resolving from the stored
    field, so a web save mid-visit made the page name gradient A while the
    panel drew gradient B, with nothing on screen to say so, and the exit then
    moved the panel back to A. What is pinned here is that reconciliation puts
    A back into the stored field: one gradient, in the row and on the panel,
    for every moment of the visit.

    The case is driven three times, once with the row on screen and once at
    each level of the picker, because the shell reconciles only the top page
    and the picker's own reconcile is the only thing that reaches the category
    underneath it."""
    a, b, c = three_builtins()
    if a["ref"] not in GROUND:
        check(rig, "agree_ground_truth_available", False, "the untouched check captured no swatch")
        return

    s0 = rig.settings()
    anim = int(s0["bgAnimId"])
    # Preflight: the ref the saves below carry is one the POST handler stores
    # rather than one bg_ref_valid drops. It has to be proved here, against a
    # closed shell, because inside the visit the display overwrites it by
    # design and a dropped ref would look exactly the same.
    if not store(rig, "agree_web_ref_is_accepted", {
            "bgAnimGradientRef": c["ref"], "bgAnimTheme": int(c["ref"])}):
        return
    if not store(rig, "agree_setup", {
            "bgAnimGradientRef": b["ref"],
            "bgAnimTheme": int(b["ref"]),
            "bgAnimThemeMap": map_write_ref(s0["bgAnimThemeMap"], anim, "")}):
        return

    open_animation(rig)
    open_picker(rig, "Gradient all")
    picker_choose(rig, a["cat"], a["name"])
    live = settled(rig, lambda: live_global(rig)[0] == a["ref"], timeout=6)
    check(rig, "agree_pick_writes_live", live, live_global(rig)[0])

    # 1. The save with the category's own page on top.
    fps_sentinel = other_fps(rig.settings()["bgAnimFps"])
    if not store_over_draft(rig, "agree_row_web_save_landed", {
            "bgAnimGradientRef": c["ref"], "bgAnimTheme": int(c["ref"]), "bgAnimFps": fps_sentinel},
            ["bgAnimFps"]):
        close_animation(rig)
        return
    rebuilt = settled(rig, lambda: rig.row_value(page_with_row(rig, "Frame rate"), "Frame rate")
                      == "%d fps" % fps_sentinel)
    check(rig, "agree_row_page_rebuilt", rebuilt, rig.row_value(page_with_row(rig, "Frame rate"), "Frame rate"))
    restored = settled(rig, lambda: live_global(rig)[0] == a["ref"])
    live_ref, live_theme = live_global(rig)
    check(rig, "agree_row_panel_back_to_draft", restored, "%r want %r" % (live_ref, a["ref"]))
    check(rig, "agree_row_mirror_back_to_draft", live_theme == str(int(a["ref"])),
          "%r want %s" % (live_theme, a["ref"]))
    value, strip = global_row(rig)
    check(rig, "agree_row_names_draft", value == a["name"], "%r want %r" % (value, a["name"]))
    check(rig, "agree_row_swatch_draws_draft", strip == GROUND[a["ref"]],
          swatch_detail(strip, GROUND[a["ref"]], GROUND.get(c["ref"])))

    # 2. The save with the picker's group page on top. The library entry in
    # the same form is what proves the picker rebuilt: "My gradients" appears
    # only after it did, so a marker that had simply not been redrawn yet
    # cannot read as a pass.
    lib0 = rig.settings()["bgAnimGradients"]
    open_picker(rig, "Gradient all")
    probe_lib = "|".join(("91", "Agree Probe", "000000,ffffff"))
    if store_over_draft(rig, "agree_group_web_save_landed", {
            "bgAnimGradientRef": c["ref"], "bgAnimTheme": int(c["ref"]), "bgAnimGradients": probe_lib},
            ["bgAnimGradients"]):
        rebuilt = settled(rig, lambda: "My gradients" in rows_across_pages(rig))
        check(rig, "agree_group_picker_rebuilt", rebuilt, rows_across_pages(rig))
        restored = settled(rig, lambda: live_global(rig)[0] == a["ref"])
        check(rig, "agree_group_panel_back_to_draft", restored,
              "%r want %r" % (live_global(rig)[0], a["ref"]))
        marked = picker_selected_rows(rig)
        check(rig, "agree_group_marker_keeps_draft", marked == [a["cat"]], "%r want [%r]" % (marked, a["cat"]))

    # 3. The save with one group's gradient list on top, which reconciles the
    # category through two levels (groupReconcile, then the parent's). The
    # restored ref is itself the proof that the reconcile ran: without one it
    # stays at the web's value for the rest of the visit.
    picker_tap(rig, a["cat"])
    rig.wait_until(lambda: depth(rig) == 3, timeout=5)
    fps_sentinel = other_fps(rig.settings()["bgAnimFps"])
    if store_over_draft(rig, "agree_list_web_save_landed", {
            "bgAnimGradientRef": c["ref"], "bgAnimTheme": int(c["ref"]), "bgAnimFps": fps_sentinel},
            ["bgAnimFps"]):
        restored = settled(rig, lambda: live_global(rig)[0] == a["ref"])
        check(rig, "agree_list_panel_back_to_draft", restored,
              "%r want %r" % (live_global(rig)[0], a["ref"]))
        marked = picker_selected_rows(rig)
        check(rig, "agree_list_marker_keeps_draft", marked == [a["name"]], "%r want [%r]" % (marked, a["name"]))
    picker_cancel(rig)
    picker_cancel(rig)

    close_animation(rig)
    store(rig, "agree_library_restored", {"bgAnimGradients": lib0})
    live_ref, live_theme = live_global(rig)
    check(rig, "agree_exit_saves_the_named_value", live_ref == a["ref"], "%r want %r" % (live_ref, a["ref"]))
    check(rig, "agree_exit_mirrors_the_named_value", live_theme == str(int(a["ref"])),
          "%r want %s" % (live_theme, a["ref"]))

    open_animation(rig)
    value, strip = global_row(rig)
    check(rig, "agree_reopen_names_the_same_value", value == a["name"], "%r want %r" % (value, a["name"]))
    check(rig, "agree_reopen_swatch_draws_it", strip == GROUND[a["ref"]],
          swatch_detail(strip, GROUND[a["ref"]], GROUND.get(c["ref"])))
    close_animation(rig)


# ---------------------------------------------------------------------------
# The per-animation gradient slots during a web save (gm-nov3.36)
#
# bgAnimThemeMap is one gradient ref per animation, and picking one writes the
# animation's slot at once (CatAnimation.cpp animGradientAssign), exactly as
# the global row writes bgAnimGradientRef at once. So the same rule applies to
# it: the panel resolves from the stored map, a web save replaces the whole
# string, and without reconciliation putting the touched slots back the row
# named this visit's gradient while the panel drew the web's, from the save
# until the exit. The global row's window was one update pass; this one was
# the rest of the visit.
#
# The checks below therefore read the stored slot, not only the row, and they
# read it before the category is popped. The existing precedence check in
# test_animation.py (check_gradient_precedence_across_animations) posts a
# conflicting map and then judges only after rig.settingsui(pop=1), so it
# passes either way.


def check_touched_anim_gradient_agrees_with_panel(rig):
    """Acceptance (gm-nov3.36): while the "Gradient" row names this visit's
    choice for the current animation, the map the panel resolves from holds it
    too, from the web save onwards rather than from the exit onwards.

    Driven three times, once with the category's own page on top and once at
    each level of the picker, because the shell reconciles only the top page
    and the picker's own reconcile is the only thing that reaches the category
    underneath it (gm-nov3.3)."""
    a, b, c = three_builtins()
    s0 = rig.settings()
    anim = int(s0["bgAnimId"])
    map0 = str(s0["bgAnimThemeMap"])

    # Preflight against a closed shell: a map string carrying these refs is
    # one WebUIPlugin's bg_map_valid accepts rather than one it drops. It has
    # to be proved here, because inside the visit the display overwrites the
    # slot by design and a dropped save would look exactly the same.
    if not store(rig, "anim_agree_web_map_is_accepted", {"bgAnimThemeMap": map_write_ref(map0, anim, c["ref"])}):
        return
    ground = anim_row_ground(rig, anim, map0, [a["ref"], c["ref"]])
    if not store(rig, "anim_agree_setup", {"bgAnimThemeMap": map_write_ref(map0, anim, b["ref"])}):
        return

    open_animation(rig)
    open_picker(rig, "Gradient")
    picker_choose(rig, a["cat"], a["name"])
    live = settled(rig, lambda: anim_slot(rig, anim) == a["ref"], timeout=6)
    check(rig, "anim_agree_pick_writes_live", live, anim_slot(rig, anim))

    # 1. The save with the category's own page on top. The frame rate moves
    # with it, so waiting for that row proves the category reconciled and
    # rebuilt: a slot that had simply not been reconciled yet would otherwise
    # read as a pass.
    fps_sentinel = other_fps(rig.settings()["bgAnimFps"])
    web_map = map_write_ref(str(rig.settings()["bgAnimThemeMap"]), anim, c["ref"])
    if not store_over_draft(rig, "anim_agree_row_web_save_landed",
                            {"bgAnimThemeMap": web_map, "bgAnimFps": fps_sentinel}, ["bgAnimFps"]):
        close_animation(rig)
        return
    rebuilt = settled(rig, lambda: rig.row_value(page_with_row(rig, "Frame rate"), "Frame rate")
                      == "%d fps" % fps_sentinel)
    check(rig, "anim_agree_row_page_rebuilt", rebuilt,
          rig.row_value(page_with_row(rig, "Frame rate"), "Frame rate"))
    restored = settled(rig, lambda: anim_slot(rig, anim) == a["ref"])
    check(rig, "anim_agree_row_panel_back_to_draft", restored, "%r want %r" % (anim_slot(rig, anim), a["ref"]))
    dump = page_with_row(rig, "Gradient")
    value = rig.row_value(dump, "Gradient")
    check(rig, "anim_agree_row_names_draft", value == a["name"], "%r want %r" % (value, a["name"]))
    if a["ref"] in ground:
        strip = swatch_strip(rig, dump, "Gradient")
        check(rig, "anim_agree_row_swatch_draws_draft", strip == ground[a["ref"]],
              swatch_detail(strip, ground[a["ref"]], ground.get(c["ref"])))

    # 2. The save with the picker's group page on top. A library entry in the
    # same form is what proves the picker rebuilt: "My gradients" appears only
    # after it did.
    lib0 = rig.settings()["bgAnimGradients"]
    open_picker(rig, "Gradient")
    probe_lib = "|".join(("92", "Anim Probe", "000000,ffffff"))
    web_map = map_write_ref(str(rig.settings()["bgAnimThemeMap"]), anim, c["ref"])
    if store_over_draft(rig, "anim_agree_group_web_save_landed",
                        {"bgAnimThemeMap": web_map, "bgAnimGradients": probe_lib}, ["bgAnimGradients"]):
        rebuilt = settled(rig, lambda: "My gradients" in rows_across_pages(rig))
        check(rig, "anim_agree_group_picker_rebuilt", rebuilt, rows_across_pages(rig))
        restored = settled(rig, lambda: anim_slot(rig, anim) == a["ref"])
        check(rig, "anim_agree_group_panel_back_to_draft", restored, "%r want %r" % (anim_slot(rig, anim), a["ref"]))
        marked = picker_selected_rows(rig)
        check(rig, "anim_agree_group_marker_keeps_draft", marked == [a["cat"]], "%r want [%r]" % (marked, a["cat"]))

    # 3. The save with one group's gradient list on top, which reaches the
    # category through two levels (groupReconcile, then the parent's).
    picker_tap(rig, a["cat"])
    rig.wait_until(lambda: depth(rig) == 3, timeout=5)
    fps_sentinel = other_fps(rig.settings()["bgAnimFps"])
    web_map = map_write_ref(str(rig.settings()["bgAnimThemeMap"]), anim, c["ref"])
    if store_over_draft(rig, "anim_agree_list_web_save_landed",
                        {"bgAnimThemeMap": web_map, "bgAnimFps": fps_sentinel}, ["bgAnimFps"]):
        restored = settled(rig, lambda: anim_slot(rig, anim) == a["ref"])
        check(rig, "anim_agree_list_panel_back_to_draft", restored, "%r want %r" % (anim_slot(rig, anim), a["ref"]))
        marked = picker_selected_rows(rig)
        check(rig, "anim_agree_list_marker_keeps_draft", marked == [a["name"]], "%r want [%r]" % (marked, a["name"]))
    picker_cancel(rig)
    picker_cancel(rig)

    close_animation(rig)
    store(rig, "anim_agree_library_restored", {"bgAnimGradients": lib0})
    check(rig, "anim_agree_exit_saves_the_named_value", anim_slot(rig, anim) == a["ref"],
          "%r want %r" % (anim_slot(rig, anim), a["ref"]))
    restore_fields_exactly(rig, "anim_agree_map_restored", {"bgAnimThemeMap": map0})


def check_touched_noncurrent_slot_agrees_with_panel(rig):
    """Acceptance (gm-nov3.36): a slot this visit touched for an animation
    that is no longer the one on screen is restored too, and an untouched slot
    in the same posted map keeps the web's value.

    Touching a slot and then stepping the Animation row away from it is the
    ordinary way to reach this state, and the animation can come back: the
    last part of the check steps back to it and asserts the row and the map
    still hold this visit's choice, because that is the moment a slot left at
    the web's value would start being drawn."""
    a, b, c = three_builtins()
    s0 = rig.settings()
    anim_a = int(s0["bgAnimId"])
    anim_b = (anim_a + 1) % len(ANIM_NAMES)
    anim_c = (anim_a + 2) % len(ANIM_NAMES)
    map0 = str(s0["bgAnimThemeMap"])

    open_animation(rig)
    open_picker(rig, "Gradient")
    picker_choose(rig, a["cat"], a["name"])
    touched_a = settled(rig, lambda: anim_slot(rig, anim_a) == a["ref"], timeout=6)
    check(rig, "noncurrent_touched_a", touched_a, anim_slot(rig, anim_a))

    tap_row(rig, "Animation", "next")
    moved = settled(rig, lambda: int(rig.settings()["bgAnimId"]) == anim_b)
    check(rig, "noncurrent_moved_to_b", moved, rig.settings()["bgAnimId"])
    open_picker(rig, "Gradient")
    picker_choose(rig, b["cat"], b["name"])
    touched_b = settled(rig, lambda: anim_slot(rig, anim_b) == b["ref"], timeout=6)
    check(rig, "noncurrent_touched_b", touched_b, anim_slot(rig, anim_b))

    # One save replacing all three slots: two this visit touched and one it
    # did not. Both touched slots must come back, and the untouched one must
    # not, so a fix that simply wrote the draft over the whole map fails here.
    fps_sentinel = other_fps(rig.settings()["bgAnimFps"])
    web_map = str(rig.settings()["bgAnimThemeMap"])
    for slot in (anim_a, anim_b, anim_c):
        web_map = map_write_ref(web_map, slot, c["ref"])
    if not store_over_draft(rig, "noncurrent_web_save_landed",
                            {"bgAnimThemeMap": web_map, "bgAnimFps": fps_sentinel}, ["bgAnimFps"]):
        close_animation(rig)
        return
    rebuilt = settled(rig, lambda: rig.row_value(page_with_row(rig, "Frame rate"), "Frame rate")
                      == "%d fps" % fps_sentinel)
    check(rig, "noncurrent_page_rebuilt", rebuilt,
          rig.row_value(page_with_row(rig, "Frame rate"), "Frame rate"))
    back_a = settled(rig, lambda: anim_slot(rig, anim_a) == a["ref"])
    check(rig, "noncurrent_offscreen_slot_back_to_draft", back_a,
          "anim %d got %r want %r" % (anim_a, anim_slot(rig, anim_a), a["ref"]))
    check(rig, "noncurrent_onscreen_slot_back_to_draft", anim_slot(rig, anim_b) == b["ref"],
          "anim %d got %r want %r" % (anim_b, anim_slot(rig, anim_b), b["ref"]))
    check(rig, "noncurrent_untouched_slot_keeps_web", anim_slot(rig, anim_c) == c["ref"],
          "anim %d got %r want the web's %r" % (anim_c, anim_slot(rig, anim_c), c["ref"]))

    # The touched animation becomes the one on screen again, inside the same
    # visit.
    tap_row(rig, "Animation", "prev")
    returned = settled(rig, lambda: int(rig.settings()["bgAnimId"]) == anim_a)
    check(rig, "noncurrent_returned_to_a", returned, rig.settings()["bgAnimId"])
    value = rig.row_value(page_with_row(rig, "Gradient"), "Gradient")
    check(rig, "noncurrent_row_names_draft_when_current", value == a["name"], "%r want %r" % (value, a["name"]))
    check(rig, "noncurrent_slot_is_draft_when_current", anim_slot(rig, anim_a) == a["ref"],
          "anim %d got %r want %r" % (anim_a, anim_slot(rig, anim_a), a["ref"]))

    close_animation(rig)
    restore_fields_exactly(rig, "noncurrent_restored", {"bgAnimThemeMap": map0, "bgAnimId": anim_a})


def check_touched_standby_slot_agrees_with_panel(rig):
    """Acceptance (gm-nov3.36): the standby animation's own slot, edited
    through the "Standby grad" row, is restored on the same terms, and stays
    restored across a second save once that animation becomes the one on
    screen."""
    a, b, c = three_builtins()
    s0 = rig.settings()
    anim_a = int(s0["bgAnimId"])
    standby_target = (anim_a + 1) % len(ANIM_NAMES)
    map0 = str(s0["bgAnimThemeMap"])

    # The row is disabled while Standby anim follows the main animation, so
    # the fixture has to name a different one before the visit starts.
    if not store(rig, "standby_slot_setup", {"bgAnimStandbyId": standby_target,
                                             "bgAnimThemeMap": map_write_ref(map0, standby_target, b["ref"])}):
        return

    open_animation(rig)
    open_picker(rig, "Standby grad")
    picker_choose(rig, a["cat"], a["name"])
    live = settled(rig, lambda: anim_slot(rig, standby_target) == a["ref"], timeout=6)
    check(rig, "standby_slot_pick_writes_live", live, anim_slot(rig, standby_target))

    fps_sentinel = other_fps(rig.settings()["bgAnimFps"])
    web_map = map_write_ref(str(rig.settings()["bgAnimThemeMap"]), standby_target, c["ref"])
    if not store_over_draft(rig, "standby_slot_web_save_landed",
                            {"bgAnimThemeMap": web_map, "bgAnimFps": fps_sentinel}, ["bgAnimFps"]):
        close_animation(rig)
        return
    rebuilt = settled(rig, lambda: rig.row_value(page_with_row(rig, "Frame rate"), "Frame rate")
                      == "%d fps" % fps_sentinel)
    check(rig, "standby_slot_page_rebuilt", rebuilt,
          rig.row_value(page_with_row(rig, "Frame rate"), "Frame rate"))
    restored = settled(rig, lambda: anim_slot(rig, standby_target) == a["ref"])
    check(rig, "standby_slot_back_to_draft", restored,
          "anim %d got %r want %r" % (standby_target, anim_slot(rig, standby_target), a["ref"]))
    value = rig.row_value(page_with_row(rig, "Standby grad"), "Standby grad")
    check(rig, "standby_slot_row_names_draft", value == a["name"], "%r want %r" % (value, a["name"]))

    # The standby animation becomes the main one, inside the same visit, and
    # another save lands on its slot. This is the case the bead names: the
    # animation whose slot was touched off screen is now the one being drawn.
    tap_row(rig, "Animation", "next")
    became = settled(rig, lambda: int(rig.settings()["bgAnimId"]) == standby_target)
    check(rig, "standby_slot_became_current", became, rig.settings()["bgAnimId"])
    fps_sentinel = other_fps(rig.settings()["bgAnimFps"])
    web_map = map_write_ref(str(rig.settings()["bgAnimThemeMap"]), standby_target, c["ref"])
    if store_over_draft(rig, "standby_slot_second_web_save_landed",
                        {"bgAnimThemeMap": web_map, "bgAnimFps": fps_sentinel}, ["bgAnimFps"]):
        restored = settled(rig, lambda: anim_slot(rig, standby_target) == a["ref"])
        check(rig, "standby_slot_back_to_draft_as_current", restored,
              "anim %d got %r want %r" % (standby_target, anim_slot(rig, standby_target), a["ref"]))
        value = rig.row_value(page_with_row(rig, "Gradient"), "Gradient")
        check(rig, "standby_slot_gradient_row_names_draft", value == a["name"], "%r want %r" % (value, a["name"]))

    close_animation(rig)
    check(rig, "standby_slot_exit_saves_the_named_value", anim_slot(rig, standby_target) == a["ref"],
          "%r want %r" % (anim_slot(rig, standby_target), a["ref"]))
    restore_fields_exactly(rig, "standby_slot_restored",
                           {"bgAnimThemeMap": map0, "bgAnimId": anim_a,
                            "bgAnimStandbyId": int(s0["bgAnimStandbyId"])})


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


# ---------------------------------------------------------------------------
# gm-nov3.31: a pick applies and leaves the picker open


def three_in_one_category():
    """Three built-ins out of one category, so one group page lists all three
    and a marker there names exactly one of them. Read out of the table rather
    than written down, so appending to data/gradients.json does not move what
    these checks drive. The first three of the category, so a category of
    seven or eight does not spread them across two pages of five."""
    for cat in GRADIENT_CATEGORIES:
        found = [{"ref": str(i), "name": n, "cat": cat}
                 for i, n in enumerate(THEME_NAMES) if GRADIENT_CATEGORY_OF[i] == cat]
        if len(found) >= 3:
            return found[:3]
    raise AssertionError("no gradient category holds three built-ins")


def check_pick_keeps_the_picker_open(rig):
    """Acceptance (gm-nov3.31): three picks in one visit are three taps. Each
    applies at once, moves the marker onto the row that was tapped, and leaves
    the picker on the page it was on. The chevron is what leaves, and the row
    underneath then shows the third pick, which is also what commits.

    The stored map is read after every tap, not only at the end: the owner's
    request is that the panel follows each tap, and the map is what the render
    task resolves a per animation gradient from."""
    picks = three_in_one_category()
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    # Start on the third pick, so every tap in the loop below is a real change
    # and none of the three can pass by the slot already holding it.
    start = map_write_ref(str(s0["bgAnimThemeMap"]), anim0, picks[2]["ref"])
    if not store(rig, "stay_open_setup", {"bgAnimThemeMap": start}):
        return

    open_animation(rig)
    open_picker(rig, "Gradient")
    at_group = depth(rig) + 1
    picker_tap(rig, picks[0]["cat"])
    if not check(rig, "stay_open_group_opened", settled(rig, lambda: depth(rig) == at_group),
                 "depth %d want %d" % (depth(rig), at_group)):
        close_animation(rig)
        return

    for n, pick in enumerate(picks):
        picker_tap(rig, pick["name"])
        applied = settled(rig, lambda p=pick: map_ref(rig.settings()["bgAnimThemeMap"], anim0) == p["ref"])
        check(rig, "stay_open_pick%d_applies" % n, applied,
              "slot %d is %r want %r" % (anim0, map_ref(rig.settings()["bgAnimThemeMap"], anim0), pick["ref"]))
        check(rig, "stay_open_pick%d_stays_open" % n, depth(rig) == at_group,
              "depth %d want %d" % (depth(rig), at_group))
        marked = picker_selected_rows(rig)
        check(rig, "stay_open_pick%d_marker_moved" % n, marked == [pick["name"]],
              "marked %r want %r" % (marked, [pick["name"]]))

    picker_cancel(rig)
    picker_cancel(rig)
    check(rig, "stay_open_chevron_leaves", depth(rig) == 1, "depth %d want 1" % depth(rig))
    value = rig.row_value(page_with_row(rig, "Gradient"), "Gradient")
    check(rig, "stay_open_row_under_shows_last_pick", value == picks[2]["name"],
          "row %r want %r" % (value, picks[2]["name"]))

    close_animation(rig)
    after = str(rig.settings()["bgAnimThemeMap"])
    check(rig, "stay_open_commits_last_pick", map_ref(after, anim0) == picks[2]["ref"],
          "slot %d is %r want %r" % (anim0, map_ref(after, anim0), picks[2]["ref"]))
    moved = [i for i in range(len(ANIM_NAMES)) if i != anim0 and map_ref(after, i) != map_ref(start, i)]
    check(rig, "stay_open_leaves_other_slots", not moved,
          "slots %r moved" % moved)


def check_global_entry_keeps_the_picker_open(rig):
    """Acceptance (gm-nov3.31): the "Global" entry on the picker's first page
    assigns and stays open with the marker moved. Someone comparing the global
    gradient against a per animation override should not be thrown out for
    picking one of them."""
    picks = three_in_one_category()
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    start = map_write_ref(str(s0["bgAnimThemeMap"]), anim0, picks[0]["ref"])
    if not store(rig, "global_entry_setup", {"bgAnimThemeMap": start}):
        return

    open_animation(rig)
    open_picker(rig, "Gradient")
    at_top = depth(rig)
    marked = picker_selected_rows(rig)
    check(rig, "global_entry_starts_on_the_override", marked == [picks[0]["cat"]],
          "marked %r want %r" % (marked, [picks[0]["cat"]]))

    picker_tap(rig, "Global")
    assigned = settled(rig, lambda: map_ref(rig.settings()["bgAnimThemeMap"], anim0) == "")
    check(rig, "global_entry_assigns", assigned,
          "slot %d is %r want %r" % (anim0, map_ref(rig.settings()["bgAnimThemeMap"], anim0), ""))
    check(rig, "global_entry_stays_open", depth(rig) == at_top, "depth %d want %d" % (depth(rig), at_top))
    marked = picker_selected_rows(rig)
    check(rig, "global_entry_marker_moved", marked == ["Global"], "marked %r want %r" % (marked, ["Global"]))

    picker_cancel(rig)
    check(rig, "global_entry_chevron_leaves", depth(rig) == 1, "depth %d want 1" % depth(rig))
    value = rig.row_value(page_with_row(rig, "Gradient"), "Gradient")
    check(rig, "global_entry_row_under_follows_global", value.startswith("Global ("),
          "row %r want a Global (...) reading" % value)
    close_animation(rig)


def check_picker_closes_when_the_slot_goes_away(rig):
    """Acceptance (gm-nov3.31): the one route that still closes the picker
    under the user works now that a pick does not. A web save turns the
    standby animation off while its own picker is open two levels deep, so
    pickerReconcile's stillValid pop has to free both ctxs and write nothing.

    Two levels deep on purpose: test_animation.py's own web interference check
    covers the same fixture at one level, and the two level pop is the one a
    pick used to perform on every visit and now never does."""
    picks = three_in_one_category()
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    map0 = str(s0["bgAnimThemeMap"])
    standby0 = int(s0["bgAnimStandbyId"])
    target = (anim0 + 1) % len(ANIM_NAMES)
    if not store(rig, "slot_gone_setup", {"bgAnimStandbyId": target, "bgAnimThemeMap": map0}):
        return

    open_animation(rig)
    open_picker(rig, "Standby grad")
    picker_tap(rig, picks[0]["cat"])
    if not check(rig, "slot_gone_group_opened", settled(rig, lambda: depth(rig) == 3),
                 "depth %d want 3" % depth(rig)):
        close_animation(rig)
        restore_fields_exactly(rig, "slot_gone_restored", {"bgAnimStandbyId": standby0, "bgAnimThemeMap": map0})
        return

    web_save(rig, {"bgAnimStandbyId": -1})
    landed = settled(rig, lambda: int(rig.settings()["bgAnimStandbyId"]) == -1, timeout=6)
    check(rig, "slot_gone_write_landed", landed, rig.settings()["bgAnimStandbyId"])
    closed = settled(rig, lambda: depth(rig) == 1, timeout=8)
    check(rig, "slot_gone_picker_closed", closed, "depth %d want 1" % depth(rig))
    now = str(rig.settings()["bgAnimThemeMap"])
    check(rig, "slot_gone_wrote_nothing", map_ref(now, target) == map_ref(map0, target),
          "slot %d is %r want %r" % (target, map_ref(now, target), map_ref(map0, target)))

    close_animation(rig)
    restore_fields_exactly(rig, "slot_gone_restored", {"bgAnimStandbyId": standby0, "bgAnimThemeMap": map0})


def check_arrows_step_in_the_pickers_order(rig):
    """Acceptance (gm-nov3.32): the main Gradient row's arrows step to the
    adjacent gradient in the picker's own flat order, wrapping at both ends,
    and each step applies at once.

    The order is the whole point of the check. Stepping over a list of its
    own would be easy and would put the user somewhere the picker does not
    agree with, so every expected ref here comes from step_order(), which is
    built from the stored library and the generated category table."""
    order = step_order(rig, allow_global=True)
    if not check(rig, "step_order_has_room", len(order) > 3, "%d entries" % len(order)):
        return
    at = middle_builtin(order)
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    start = map_write_ref(str(s0["bgAnimThemeMap"]), anim0, order[at])
    if not store(rig, "step_setup", {"bgAnimThemeMap": start}):
        return

    open_animation(rig)
    tap_arrow(rig, "Gradient", "next")
    moved = settled(rig, lambda: map_ref(rig.settings()["bgAnimThemeMap"], anim0) == order[at + 1])
    check(rig, "step_next_applies", moved, "slot %d is %r want %r" % (
        anim0, map_ref(rig.settings()["bgAnimThemeMap"], anim0), order[at + 1]))
    want_text = gradient_name_for_ref(rig.settings(), order[at + 1])
    shown = rig.row_value(page_with_row(rig, "Gradient"), "Gradient")
    check(rig, "step_next_row_text", shown == want_text, "row %r want %r" % (shown, want_text))

    tap_arrow(rig, "Gradient", "prev")
    tap_arrow(rig, "Gradient", "prev")
    back = settled(rig, lambda: map_ref(rig.settings()["bgAnimThemeMap"], anim0) == order[at - 1])
    check(rig, "step_prev_applies", back, "slot %d is %r want %r" % (
        anim0, map_ref(rig.settings()["bgAnimThemeMap"], anim0), order[at - 1]))
    close_animation(rig)

    # Both ends. The list wraps, so the step off the tail is the head and the
    # step off the head is the tail.
    for name, from_ref, which, want in (
            ("tail", order[-1], "next", order[0]),
            ("head", order[0], "prev", order[-1])):
        if not store(rig, "step_wrap_%s_setup" % name,
                     {"bgAnimThemeMap": map_write_ref(str(rig.settings()["bgAnimThemeMap"]), anim0, from_ref)}):
            continue
        open_animation(rig)
        tap_arrow(rig, "Gradient", which)
        wrapped = settled(rig, lambda w=want: map_ref(rig.settings()["bgAnimThemeMap"], anim0) == w)
        check(rig, "step_wraps_at_the_%s" % name, wrapped, "slot %d is %r want %r" % (
            anim0, map_ref(rig.settings()["bgAnimThemeMap"], anim0), want))
        close_animation(rig)

    restore_fields_exactly(rig, "step_restored", {"bgAnimThemeMap": str(s0["bgAnimThemeMap"])})


def check_a_stepped_row_marks_the_picker(rig):
    """Acceptance (gm-nov3.32): stepping to a gradient and then opening the
    picker shows the marker on that gradient. This is what the shared order
    buys, and it is the half a list of its own would silently get wrong: the
    step would look right on the row and put the marker somewhere else."""
    order = step_order(rig, allow_global=True)
    at = middle_builtin(order)
    want = order[at + 1]
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    if not store(rig, "mark_setup",
                 {"bgAnimThemeMap": map_write_ref(str(s0["bgAnimThemeMap"]), anim0, order[at])}):
        return

    open_animation(rig)
    tap_arrow(rig, "Gradient", "next")
    stepped = settled(rig, lambda: map_ref(rig.settings()["bgAnimThemeMap"], anim0) == want)
    if not check(rig, "mark_step_applied", stepped, "slot %d is %r want %r" % (
            anim0, map_ref(rig.settings()["bgAnimThemeMap"], anim0), want)):
        close_animation(rig)
        restore_fields_exactly(rig, "mark_restored", {"bgAnimThemeMap": str(s0["bgAnimThemeMap"])})
        return

    open_picker(rig, "Gradient")
    group = category_of_ref(want)
    marked_group = picker_selected_rows(rig)
    check(rig, "mark_group_marked", marked_group == [group], "marked %r want %r" % (marked_group, [group]))
    picker_tap(rig, group)
    name = gradient_name_for_ref(rig.settings(), want)
    marked = settled(rig, lambda: picker_selected_rows(rig) == [name])
    check(rig, "mark_entry_marked", marked, "marked %r want %r" % (picker_selected_rows(rig), [name]))
    picker_cancel(rig)
    picker_cancel(rig)
    close_animation(rig)
    restore_fields_exactly(rig, "mark_restored", {"bgAnimThemeMap": str(s0["bgAnimThemeMap"])})


def check_all_three_rows_step(rig):
    """Acceptance (gm-nov3.32): all three gradient rows step, not just the
    one. They write three different fields (the global ref, and two slots of
    the map), so a fix wired into one of them passes nothing here.

    The global row's arrows walk a list with no Global entry in it, because
    the global is what "Global" means and its own picker offers no such row."""
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    standby = (anim0 + 1) % len(ANIM_NAMES)
    map0 = str(s0["bgAnimThemeMap"])
    ref0 = str(s0.get("bgAnimGradientRef", ""))
    theme0 = str(s0.get("bgAnimTheme", ""))

    per_anim = step_order(rig, allow_global=True)
    global_order = step_order(rig, allow_global=False)
    check(rig, "three_rows_global_order_has_no_global", "" not in global_order,
          "global order starts %r" % global_order[:3])

    at = middle_builtin(per_anim)
    gat = middle_builtin(global_order)
    seeded = {
        "bgAnimStandbyId": standby,
        "bgAnimThemeMap": map_write_ref(map_write_ref(map0, anim0, per_anim[at]), standby, per_anim[at]),
        "bgAnimGradientRef": global_order[gat],
    }
    if not store(rig, "three_rows_setup", seeded):
        return

    open_animation(rig)
    tap_arrow(rig, "Gradient all", "next")
    moved = settled(rig, lambda: str(rig.settings().get("bgAnimGradientRef", "")) == global_order[gat + 1])
    check(rig, "three_rows_global_steps", moved, "ref %r want %r" % (
        rig.settings().get("bgAnimGradientRef"), global_order[gat + 1]))

    tap_arrow(rig, "Gradient", "next")
    moved = settled(rig, lambda: map_ref(rig.settings()["bgAnimThemeMap"], anim0) == per_anim[at + 1])
    check(rig, "three_rows_main_steps", moved, "slot %d is %r want %r" % (
        anim0, map_ref(rig.settings()["bgAnimThemeMap"], anim0), per_anim[at + 1]))

    tap_arrow(rig, "Standby grad", "prev")
    moved = settled(rig, lambda: map_ref(rig.settings()["bgAnimThemeMap"], standby) == per_anim[at - 1])
    check(rig, "three_rows_standby_steps", moved, "slot %d is %r want %r" % (
        standby, map_ref(rig.settings()["bgAnimThemeMap"], standby), per_anim[at - 1]))

    close_animation(rig)
    restore_fields_exactly(rig, "three_rows_restored", {
        "bgAnimStandbyId": int(s0["bgAnimStandbyId"]),
        "bgAnimThemeMap": map0,
        "bgAnimGradientRef": ref0,
        "bgAnimTheme": theme0,
    })


def check_standby_arrows_are_inert_when_the_row_is(rig):
    """Acceptance (gm-nov3.32): the standby row is inert while the standby
    animation follows the main one, and its arrows are inert with it. A row
    that reads "Same as main" and steps a value nobody can see is worse than
    one that does nothing."""
    s0 = rig.settings()
    standby0 = int(s0["bgAnimStandbyId"])
    map0 = str(s0["bgAnimThemeMap"])
    if not store(rig, "inert_setup", {"bgAnimStandbyId": -1, "bgAnimThemeMap": map0}):
        return

    open_animation(rig)
    shown = rig.row_value(page_with_row(rig, "Standby grad"), "Standby grad")
    check(rig, "inert_row_says_same_as_main", shown == "Same as main", "row %r" % shown)
    tap_arrow(rig, "Standby grad", "next")
    tap_arrow(rig, "Standby grad", "prev")
    now = str(rig.settings()["bgAnimThemeMap"])
    check(rig, "inert_arrows_wrote_nothing", now == map0, "map %r want %r" % (now, map0))
    shown = rig.row_value(page_with_row(rig, "Standby grad"), "Standby grad")
    check(rig, "inert_row_still_says_same_as_main", shown == "Same as main", "row %r" % shown)
    close_animation(rig)
    restore_fields_exactly(rig, "inert_restored", {"bgAnimStandbyId": standby0, "bgAnimThemeMap": map0})


def check_hold_repeats_without_flushing_per_step(rig):
    """Acceptance (gm-nov3.32): holding an arrow repeats at LVGL's own
    cadence, and five seconds of it does not write NVS once per step.

    The write question is what makes the hold safe. A step is one
    Property::set, which stores the value and raises a dirty flag
    (src/display/core/Property.h:85); NVS is written by the periodic flush,
    Settings::loopTask calling doSave() and then waiting 5000 ms
    (src/display/core/Settings.cpp:496), and doSave returns early when
    nothing is dirty. So ten steps a second cost ten property writes a second
    and at most one NVS write per five seconds.

    That is checked here rather than asserted: the simulator's NVS is one
    JSON file, so the value it holds for this field is sampled ten times a
    second through the hold and the distinct readings counted. One per step
    would be dozens.

    The bound is the venue's own flush period. The simulator does not run
    Settings::loopTask at all; sim/main.cpp flushes from its main loop every
    2000 ms, so a five second hold spans at most three flushes, and the
    reading taken before the hold is a fourth value. One more is allowed for
    a flush landing on the boundary. The device's period is the 5000 ms above,
    which is stricter, so a device run passing this is saying more than a
    simulator run is. The step count comes out of the same hold, so a pass
    means both halves held at once."""
    order = step_order(rig, allow_global=True)
    at = middle_builtin(order)
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    map0 = str(s0["bgAnimThemeMap"])
    if not store(rig, "hold_setup", {"bgAnimThemeMap": map_write_ref(map0, anim0, order[at])}):
        return

    open_animation(rig)
    target = arrow(rig, "Gradient", "next")
    x1, y1, x2, y2 = target["hit"]
    hold_ms = 5000
    seen = set()
    first = nvs_theme_map()
    if first is not None:
        seen.add(first)
    rig.get_json("/api/debug/tap?x=%d&y=%d&ms=%d" % ((x1 + x2) // 2, (y1 + y2) // 2, hold_ms))
    deadline = time.time() + hold_ms / 1000.0
    while time.time() < deadline:
        stored = nvs_theme_map()
        if stored is not None:
            seen.add(stored)
        time.sleep(0.1)
    rig.wait_until(lambda: bool(rig.get_json("/api/debug/tap").get("released_at_ms")), timeout=10)
    time.sleep(0.3)

    now = map_ref(str(rig.settings()["bgAnimThemeMap"]), anim0)
    # LVGL repeats a held button after 400 ms and then every 100 ms, so five
    # seconds is one press plus about 46 repeats. The list is 60-odd long and
    # wraps, so the landing place is counted as a distance forward from the
    # start rather than as an exact index.
    steps = (order.index(now) - at) % len(order) if now in order else -1
    check(rig, "hold_landed_on_a_real_gradient", now in order, "slot %d is %r" % (anim0, now))
    check(rig, "hold_repeats", steps >= 10, "slot %d moved %d steps to %r" % (anim0, steps, now))
    if NVS_PATH is None:
        rig.log("hold_nvs_not_watched", reason="no simulator NVS file for this venue")
    else:
        check(rig, "hold_did_not_flush_per_step", len(seen) <= 5,
              "%d distinct NVS readings of %s over %d ms of hold and %d steps: %r" % (
                  len(seen), NVS_THEME_MAP_KEY, hold_ms, steps, sorted(seen)))

    close_animation(rig)
    restore_fields_exactly(rig, "hold_restored", {"bgAnimThemeMap": map0})


def check_step_survives_an_anim_id_past_the_registry(rig):
    """Acceptance (gm-nov3.32), in the shape 91cb0ed5 fixed: a stored
    bgAnimId from a longer registry used to index past the end of this
    category's two per-animation vectors on the first Gradient arrow press,
    because nothing range-checks that field on the way in. clampAnimId at
    enter is what stops it, and the arrows are back, so the clamp is load
    bearing again.

    The check is the original crash's shape: store an id past the roster, open
    the category, press the arrow. A pass is the simulator still answering
    afterwards and the write landing in the clamped slot, the last animation
    of the live roster, rather than anywhere else."""
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    map0 = str(s0["bgAnimThemeMap"])
    clamped = len(ANIM_NAMES) - 1
    if not store(rig, "clamp_setup", {"bgAnimId": len(ANIM_NAMES) + 7, "bgAnimThemeMap": map0}):
        return

    order = step_order(rig, allow_global=True)
    open_animation(rig)
    shown = rig.row_value(page_with_row(rig, "Animation"), "Animation")
    check(rig, "clamp_row_names_the_last_animation", shown == ANIM_NAMES[clamped],
          "row %r want %r" % (shown, ANIM_NAMES[clamped]))
    before = map_ref(map0, clamped)
    tap_arrow(rig, "Gradient", "next")
    landed = settled(rig, lambda: map_ref(str(rig.settings()["bgAnimThemeMap"]), clamped) != before)
    check(rig, "clamp_step_wrote_the_clamped_slot", landed, "slot %d is %r, was %r" % (
        clamped, map_ref(str(rig.settings()["bgAnimThemeMap"]), clamped), before))
    now = map_ref(str(rig.settings()["bgAnimThemeMap"]), clamped)
    check(rig, "clamp_step_wrote_a_real_gradient", now in order, "slot %d is %r" % (clamped, now))
    # The simulator answering this at all is half the check: the old failure
    # was a write past the end of a heap vector on that press.
    check(rig, "clamp_survived_the_press", rig.settingsui_state().get("open") is True,
          "shell state %r" % rig.settingsui_state())

    close_animation(rig)
    restore_fields_exactly(rig, "clamp_restored", {"bgAnimId": anim0, "bgAnimThemeMap": map0})


def check_the_rows_audit_clean(rig):
    """Acceptance (gm-nov3.32): three targets in one 320x56 row, none
    overlapping, each at least 56x56 effective, on every page that carries a
    gradient row. This is what gm-nov3.3 said could not be done with the
    arrows on, and it is why the band is the row's left 200 px rather than
    the whole row.

    The report carries the smallest target and the closest non overlapping
    pair, because "no violations" does not say how much room was left."""
    open_animation(rig)
    pages = int(rig.settingsui_state().get("pages", 1))
    worst = None
    closest = None
    rows_seen = 0
    violations = []
    for page in range(pages):
        dump = goto_page(rig, page)
        names = rig.rows_on_page(dump)
        if not any(n in GRADIENT_ROWS for n in names):
            continue
        violations.extend(rig.audit(dump)["violations"])
        for slot, name in zip(rig.row_slots(dump), names):
            if name not in GRADIENT_ROWS:
                continue
            rows_seen += 1
            # The three targets of this row, found by their y band rather
            # than by the tree, so a target that escaped the row's subtree
            # would still be measured against the others.
            band = [o for o in rig.targets(dump)
                    if o["hit"][1] >= slot["hit"][1] and o["hit"][3] <= slot["hit"][3]]
            check(rig, "audit_%s_has_three_targets" % name.replace(" ", "_"), len(band) == 3,
                  "%r" % [o.get("tag") for o in band])
            for o in band:
                w, h = o["hit"][2] - o["hit"][0] + 1, o["hit"][3] - o["hit"][1] + 1
                if worst is None or min(w, h) < min(worst[1], worst[2]):
                    worst = (o.get("tag"), w, h)
            for i in range(len(band)):
                for j in range(i + 1, len(band)):
                    gap = target_gap(band[i]["hit"], band[j]["hit"])
                    if closest is None or gap < closest[0]:
                        closest = (gap, band[i].get("tag"), band[j].get("tag"))
    check(rig, "audit_saw_all_three_rows", rows_seen == 3, "%d gradient rows" % rows_seen)
    check(rig, "audit_clean_on_the_gradient_pages", not violations, repr(violations))
    check(rig, "audit_smallest_target_is_big_enough", worst is not None and min(worst[1], worst[2]) >= 56,
          "smallest %r" % (worst,))
    rig.log("audit_geometry", smallest=repr(worst), closest=repr(closest))
    check(rig, "audit_closest_pair_in_a_row_has_a_gap", closest is not None and closest[0] >= 1,
          "closest %r" % (closest,))
    close_animation(rig)


def check_the_band_still_opens_the_picker(rig):
    """Acceptance (gm-nov3.32): a tap on the row's centre band still opens the
    picker, on all three rows. The arrows took the row's right-hand 116 px, so
    this is the half of the row that had to keep working."""
    s0 = rig.settings()
    standby0 = int(s0["bgAnimStandbyId"])
    if not store(rig, "band_setup", {"bgAnimStandbyId": (int(s0["bgAnimId"]) + 1) % len(ANIM_NAMES)}):
        return
    open_animation(rig)
    for row in ("Gradient all", "Gradient", "Standby grad"):
        before = depth(rig)
        open_picker(rig, row)
        check(rig, "band_opens_%s" % row.replace(" ", "_"), depth(rig) == before + 1,
              "depth %d want %d" % (depth(rig), before + 1))
        picker_cancel(rig)
        settled(rig, lambda b=before: depth(rig) == b)
    close_animation(rig)
    restore_fields_exactly(rig, "band_restored", {"bgAnimStandbyId": standby0})


def check_starting_state_restored(rig):
    """Not an acceptance criterion: the scenarios share one NVS, and this one
    ends on the legacy custom fixture and a stepped frame rate, neither of
    which the next scenario should inherit."""
    store(rig, "starting_state_restored", STARTING)


CHECKS = [
    ("untouched_global_follows_web_save", check_untouched_global_follows_web_save),
    ("touched_global_keeps_draft", check_touched_global_keeps_draft),
    ("touched_global_agrees_with_panel", check_touched_global_agrees_with_panel),
    ("touched_anim_gradient_agrees_with_panel", check_touched_anim_gradient_agrees_with_panel),
    ("touched_noncurrent_slot_agrees_with_panel", check_touched_noncurrent_slot_agrees_with_panel),
    ("touched_standby_slot_agrees_with_panel", check_touched_standby_slot_agrees_with_panel),
    ("legacy_fallback_named_and_inert", check_legacy_fallback_named_and_inert),
    ("legacy_unparsable_custom_falls_back", check_legacy_unparsable_custom_falls_back),
    ("pick_keeps_the_picker_open", check_pick_keeps_the_picker_open),
    ("global_entry_keeps_the_picker_open", check_global_entry_keeps_the_picker_open),
    ("picker_closes_when_the_slot_goes_away", check_picker_closes_when_the_slot_goes_away),
    ("arrows_step_in_the_pickers_order", check_arrows_step_in_the_pickers_order),
    ("a_stepped_row_marks_the_picker", check_a_stepped_row_marks_the_picker),
    ("all_three_rows_step", check_all_three_rows_step),
    ("standby_arrows_are_inert_when_the_row_is", check_standby_arrows_are_inert_when_the_row_is),
    ("hold_repeats_without_flushing_per_step", check_hold_repeats_without_flushing_per_step),
    ("step_survives_an_anim_id_past_the_registry", check_step_survives_an_anim_id_past_the_registry),
    ("the_rows_audit_clean", check_the_rows_audit_clean),
    ("the_band_still_opens_the_picker", check_the_band_still_opens_the_picker),
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
    global NVS_PATH
    if venue is not None and getattr(venue, "workdir", None):
        NVS_PATH = os.path.join(venue.workdir, "sim_data", "nvs", "controller.json")
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
    global NVS_PATH
    NVS_PATH = os.path.join(data_dir, "nvs", "controller.json")
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
