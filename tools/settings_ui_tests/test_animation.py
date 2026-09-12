#!/usr/bin/env python3
"""Scenario for the Animation settings category (gm-flw.9): animation, the
standby screen's own animation (gm-3vj.49), frame rate, all-screens, theme,
gradient, plates, plate colour/opacity, element tint/colour and text scrim,
every value row live, plus the Parameters child page the Parameters row
pushes (gm-3vj.2). Built on
tools/settings_ui_tests/rig.py (gm-flw.16); runs against the desktop
simulator by default (pio run -e display-sim) and against a loadtest device
with --host.

Usage:
    python3 tools/settings_ui_tests/test_animation.py
        [--program PATH/to/.pio/build/display-sim/program]
        [--workdir DIR] [--port N] [--host ip[:port]]

Exits 0 if every check passes, 1 otherwise. Logs each step (Rig.log) and
prints a PASS/FAIL summary at the end. A step that needs the real web UI (a
browser) or the render pipeline (frame counters, plate/gradient pixels,
memory figures) is not run here; see "Not verified" at the bottom of this
file for the exact list and the device-side commands that check them.
"""
import argparse
import os
import sys
import tempfile
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO_ROOT)

from tools.settings_ui_tests import Rig, Sim  # noqa: E402

DEFAULT_PROGRAM = os.path.join(REPO_ROOT, ".pio", "build", "display-sim", "program")

# Mirrors of the two firmware tables CatAnimation.cpp itself keeps a mirror
# of (see that file's comment): BgAnimRegistry.cpp's REGISTRY order
# (animation display names) and BgAnimThemes.cpp's THEMES order (built-in
# gradient names). Both compile only outside GAGGIMATE_SIM, so there is no
# device route this script could read them from instead.
#
# ANIM_NAMES was regenerated from the real structs at HEAD 096292af
# (gm-3vj.2), along with CatAnimation.cpp's own mirror. It had drifted twice
# over: 14 names against a roster of 44, and eight of the names it did carry
# were the pre-rename ones ("Brushed Metal" for "Brushed", and so on). A
# change to the real roster still needs the same edit made in three places
# (the Anim*.cpp structs, CatAnimation.cpp's mirror and this list) --
# flagged to the epic lead, with the note that generating the last two from
# the first is a few lines of Python.
ANIM_NAMES = [
    "Plasma", "Lava", "Silk", "Starfield", "Aurora", "Ripples", "Caustics", "Mandala", "Orbits",
    "Fireflies", "Steam", "Ember", "Nebula", "Silk 2", "Brushed", "Horizon", "Oculus", "Chevrons",
    "Mosaic", "Saddle", "Refraction", "Sundial", "Crescent", "Glint", "Tunnel", "Kaleido", "Shafts",
    "Weave", "Lens", "Tide", "Truchet", "Quilt", "Rain", "Stripes", "Ribbon", "Harmonograph",
    "Floor", "Hills", "Gyroid", "Barrel", "Grid", "Cells", "Dimples", "Cube",
]
THEME_NAMES = [
    "Espresso", "Ocean", "Violet Dusk", "Forest", "Sunset", "Fire", "Ice", "Mono", "Rose", "Gold", "Aurora", "Cyber",
    "Ember Coal", "Deep Space", "Teal Reef", "Sakura", "Lime", "Arctic Night",
]
THEME_MODE_LABELS = ["Dark", "Light"]
PLATES_LABELS = ["Keep", "Hide", "Custom"]
# SettingsModel.cpp's kPalette, in cycle order.
PALETTE = [
    ("White", 0xFFFFFF), ("Warm white", 0xFFE0B3), ("Amber", 0xFFBF00), ("Orange", 0xFF8000),
    ("Red", 0xFF0000), ("Pink", 0xFF80C0), ("Purple", 0x8000FF), ("Blue", 0x0000FF),
    ("Cyan", 0x00FFFF), ("Teal", 0x008080), ("Green", 0x00FF00), ("Black", 0x000000),
]

# The sixteen fields CatAnimation.cpp writes; the "visit changes nothing
# writes nothing" check compares these, byte for byte, before and after a
# no-op visit.
ANIMATION_FIELDS = [
    "bgAnimId", "bgAnimStandbyId", "bgAnimFps", "bgAnimAllScreens", "themeMode", "bgAnimThemeMap",
    "bgAnimClearPlates",
    "bgAnimPlateColor", "bgAnimPlateOpacity", "elementTintEnabled", "elementTintColor", "bgAnimScrim",
    "bgFadeOutMs", "bgFadeInMs", "bgFadeCurve", "bgAnimInterlace",
]
FADE_CURVE_LABELS = ["Linear", "Smooth"]

# web/src/pages/Settings/index.jsx's buildSubmitFormData: fields the web
# form omits entirely when unchecked, so presence (not a "0"/"false" value)
# is what handleSettings's POST branch reads for these (WebUIPlugin.cpp).
# Every other field there is `if (request->hasArg(...))`-gated, so carrying
# its current value through unchanged is safe to omit or include.
CHECKBOX_KEYS = {
    "homekit", "boilerFillActive", "smartGrindActive", "scaleMenuButton", "homeAssistant", "momentaryButtons",
    "delayAdjust", "clock24hFormat", "autowakeupEnabled", "bgAnimAllScreens", "elementTintEnabled",
}

FAILURES = []
TOTAL = 0


def check(rig, name, cond, detail=""):
    global TOTAL
    TOTAL += 1
    rig.log("check", name=name, ok=int(bool(cond)), detail=detail)
    if not cond:
        FAILURES.append((name, detail))
    return cond


def run_check(rig, name, fn):
    """Runs one check function, turning an unexpected exception into a
    single recorded failure instead of aborting the whole scenario (a bug
    in one check must not hide every check after it)."""
    try:
        fn(rig)
    except Exception as e:  # noqa: BLE001 -- a scenario check must never crash the run
        check(rig, name + "_exception", False, "%s: %s" % (type(e).__name__, e))


def map_ref(theme_map, anim_id):
    """Python mirror of SettingsModel.cpp's gradientMapReadRef: the
    ";"-separated slot for anim_id, "" if the map is shorter."""
    parts = theme_map.split(";") if theme_map else [""]
    return parts[anim_id] if 0 <= anim_id < len(parts) else ""


def map_write_ref(theme_map, anim_id, ref):
    """Python mirror of SettingsModel.cpp's gradientMapWriteRef: sets slot
    anim_id to ref in the ";"-separated map, padding shorter maps with
    empty slots, without disturbing any other slot."""
    parts = theme_map.split(";") if theme_map else [""]
    while len(parts) <= anim_id:
        parts.append("")
    parts[anim_id] = ref
    return ";".join(parts)


def expected_global_gradient_text(settings):
    """The Gradient all row's value: bgAnimGradientRef when it names a
    built-in, else bgAnimTheme. A library ref ("cN") returns None for the
    reason in expected_gradient_text."""
    ref = str(settings.get("bgAnimGradientRef", "") or "")
    if ref.startswith("c"):
        return None
    if ref != "":
        try:
            idx = int(ref)
        except ValueError:
            idx = -1
        if 0 <= idx < len(THEME_NAMES):
            return THEME_NAMES[idx]
    theme_idx = int(settings["bgAnimTheme"])
    return THEME_NAMES[theme_idx] if 0 <= theme_idx < len(THEME_NAMES) else THEME_NAMES[0]


def expected_gradient_text(settings, anim_id=None):
    """Python mirror of gradientChoices()/gradientChoiceIndexForRef() plus
    CatAnimation.cpp's "Global (<name>)" formatting, for the built-in-theme
    and no-override cases only. Returns None for a library ref ("cN"): the
    simulator's bg_library_valid always returns false (sim/platform/
    bganim_stub.cpp), so bgAnimGradients can never legitimately hold a
    library entry here and this script never needs to resolve one."""
    if anim_id is None:
        anim_id = int(settings["bgAnimId"])
    ref = map_ref(settings["bgAnimThemeMap"], anim_id)
    if ref.startswith("c"):
        return None
    if ref != "":
        try:
            idx = int(ref)
        except ValueError:
            idx = -1
        if 0 <= idx < len(THEME_NAMES):
            return THEME_NAMES[idx]
    # No override, or one naming something this build cannot resolve: the row
    # names the global rather than saying "Default" and leaving the reader to
    # find out where the default lives.
    gname = expected_global_gradient_text(settings)
    return None if gname is None else "Global (%s)" % gname


def expected_standby_text(settings):
    """Python mirror of CatAnimation.cpp's clampStandbyAnimId plus
    standbyChoiceLabel: the Standby anim row shows "Same" for -1 and for any
    id outside the roster, and the animation's name otherwise."""
    stored = int(settings["bgAnimStandbyId"])
    if stored < 0 or stored >= len(ANIM_NAMES):
        return "Same"
    return ANIM_NAMES[stored]


def expected_palette_text(color_int):
    for name, val in PALETTE:
        if val == color_int:
            return name
    return "#%06X" % (color_int & 0xFFFFFF)


def hex_to_int(hex_str):
    return int(hex_str.lstrip("#"), 16)


def web_save(rig, overrides):
    """Emulates a web UI save on the simulator only: GET the current
    settings, apply overrides, and POST the whole thing back in the form
    WebUIPlugin::handleSettings expects (CHECKBOX_KEYS included only when
    truthy; every other field there is `if (hasArg(...))`-gated, so an
    unchanged value round-trips safely, and an unknown key like
    panelClockLive is simply ignored -- the handler's own comment: "the
    form posts the whole document back ... echoing this is inert").

    Refuses to run against anything but a loopback host: the bead's rule is
    that /api/settings is never POSTed by hand against the real device, and
    this is the one place in this package that does POST it, on the
    simulator only, standing in for the browser that does not exist here.
    """
    host = rig.host
    if not (host == "127.0.0.1" or host.startswith("127.0.0.1:")):
        raise RuntimeError("web_save needs the web UI on this host (%r is not the simulator's loopback)" % host)
    current = rig.settings()
    current.update(overrides)
    form = {}
    for key, value in current.items():
        if key in CHECKBOX_KEYS:
            if value:
                form[key] = "1"
        else:
            form[key] = str(value)
    data = urllib.parse.urlencode(form).encode("utf-8")
    req = urllib.request.Request(rig.base + "/api/settings", data=data, method="POST")
    with urllib.request.urlopen(req, timeout=rig.timeout) as resp:
        resp.read()


def rgb565_pixel(data, x, y, w=480):
    off = (y * w + x) * 2
    val = data[off] | (data[off + 1] << 8)
    r5, g6, b5 = (val >> 11) & 0x1F, (val >> 5) & 0x3F, val & 0x1F
    return ((r5 * 255) // 31, (g6 * 255) // 63, (b5 * 255) // 31)


# ---------------------------------------------------------------------------
# Shell navigation helpers


def open_animation(rig):
    """Opens the shell and jumps straight to the Animation category (index 2:
    Temps, Display, Animation, Machine, Status, Fixture). Returns the
    touchmap dump of the page that lands (page 0)."""
    # A previous check may have left the shell open at the tile page (the
    # device path of the precedence check pops but never closes); open=1 on
    # an open shell is a 409, so close first.
    if rig.settingsui_state().get("open") is True:
        rig.settingsui(close=1)
        rig.wait_until(lambda: rig.settingsui_state().get("open") is False, timeout=5)
    rig.settingsui(open=1)
    rig.wait_until(lambda: rig.settingsui_state().get("open") is True, timeout=5)
    rig.settingsui(cat=2)
    rig.wait_until(lambda: rig.settingsui_state().get("category") == 2, timeout=5)
    return rig.touchmap(screen=0)


def goto_page(rig, page):
    rig.settingsui(page=page)
    rig.wait_until(lambda: rig.settingsui_state().get("page") == page, timeout=5)
    return rig.touchmap(screen=0)


def page_with_row(rig, name):
    """Turns to the page carrying the row called `name` and returns its dump.
    Addressed by name rather than by page number because this category's rows
    move across page boundaries whenever one is added: gm-3vj.2's Parameters
    row pushed five of them onto the next page and made a fourth page. The
    audit table (audit_pages.py) still pins the exact per-page row lists, so
    nothing here has to."""
    state = rig.settingsui_state()
    pages = int(state.get("pages", 1))
    for page in range(pages):
        dump = goto_page(rig, page)
        if rig.find_tag(dump, name, "value") is not None:
            return dump
    raise AssertionError("no row %r on any of the %d pages" % (name, pages))


def tap_row(rig, name, role):
    """Turns to the page carrying the row called `name` and taps its `role`
    control ("next", "prev", "plus", "minus", "toggle", "action"). Two rows
    this scenario alternates between, Animation and Gradient, are on
    different pages since gm-3vj.2."""
    dump = page_with_row(rig, name)
    target = rig.find_tag(dump, name, role)
    if target is None:
        raise AssertionError("row %r has no %r control on its page" % (name, role))
    rig.tap_target(target)


def close_animation(rig):
    rig.settingsui(close=1)
    rig.wait_until(lambda: rig.settingsui_state().get("open") is False, timeout=5)


# ---------------------------------------------------------------------------
# Checks


def all_row_values(rig):
    """Every row value on every page of the open category, keyed by row name,
    plus the per-page dumps. One sweep, so no check has to know which page a
    row landed on (they move whenever a row is added)."""
    pages = int(rig.settingsui_state().get("pages", 1))
    values, dumps = {}, []
    for page in range(pages):
        dump = goto_page(rig, page)
        dumps.append((page, dump))
        for o in dump["objects"]:
            tag = o.get("tag") or ""
            if tag.endswith("/value"):
                val = o.get("val")
                values[tag[: -len("/value")]] = val if val is not None else o.get("t")
    return values, dumps


def check_rows_match_settings(rig):
    """Acceptance: every row shows the stored value as GET /api/settings
    reports it, across all four pages, without a renderer (the model's
    animation/gradient/palette logic is host code)."""
    s = rig.settings()
    open_animation(rig)
    v, dumps = all_row_values(rig)
    check(rig, "row_animation", v["Animation"] == ANIM_NAMES[int(s["bgAnimId"])],
          "%r vs bgAnimId=%s" % (v["Animation"], s["bgAnimId"]))
    # The Parameters row names the animation whose parameters it opens, so it
    # tracks the row above it (CatAnimation.cpp).
    check(rig, "row_parameters", v["Parameters"] == ANIM_NAMES[int(s["bgAnimId"])],
          "%r vs bgAnimId=%s" % (v["Parameters"], s["bgAnimId"]))
    # "Same" for -1 or for an id past the end of this build's roster, the
    # animation's name otherwise (CatAnimation.cpp, clampStandbyAnimId).
    check(rig, "row_standby_anim", v["Standby anim"] == expected_standby_text(s),
          "%r vs bgAnimStandbyId=%s" % (v["Standby anim"], s["bgAnimStandbyId"]))
    check(rig, "row_frame_rate", v["Frame rate"] == "%d fps" % int(s["bgAnimFps"]), v["Frame rate"])
    check(rig, "row_all_screens", v["All screens"] == ("On" if s["bgAnimAllScreens"] else "Off"), v["All screens"])
    check(rig, "row_theme", v["Theme"] == THEME_MODE_LABELS[int(s["themeMode"])], v["Theme"])
    exp_grad = expected_gradient_text(s)
    if exp_grad is not None:
        check(rig, "row_gradient", v["Gradient"] == exp_grad, "%r vs %r" % (v["Gradient"], exp_grad))
    else:
        rig.log("row_gradient_skipped", reason="library ref not reachable on the simulator")
    exp_all = expected_global_gradient_text(s)
    if exp_all is not None:
        check(rig, "row_gradient_all", v["Gradient all"] == exp_all, "%r vs %r" % (v["Gradient all"], exp_all))
    else:
        rig.log("row_gradient_all_skipped", reason="library ref not reachable on the simulator")
    # The standby rows are live only while the standby animation is a
    # different one; the same id on both means one animation with one set of
    # parameters, and both rows say so (CatAnimation.cpp, refreshStandbyRows).
    standby = int(s["bgAnimStandbyId"])
    separate = 0 <= standby < len(ANIM_NAMES) and standby != int(s["bgAnimId"])
    if separate:
        check(rig, "row_standby_params", v["Standby params"] == ANIM_NAMES[standby],
              "%r vs bgAnimStandbyId=%s" % (v["Standby params"], s["bgAnimStandbyId"]))
        exp_sgrad = expected_gradient_text(s, standby)
        if exp_sgrad is not None:
            check(rig, "row_standby_gradient", v["Standby grad"] == exp_sgrad,
                  "%r vs %r" % (v["Standby grad"], exp_sgrad))
    else:
        check(rig, "row_standby_params", v["Standby params"] == "Same as main", v["Standby params"])
        check(rig, "row_standby_gradient", v["Standby grad"] == "Same as main", v["Standby grad"])

    check(rig, "row_plates", v["Plates"] == PLATES_LABELS[int(s["bgAnimClearPlates"])], v["Plates"])
    check(rig, "row_plate_colour", v["Plate colour"] == expected_palette_text(hex_to_int(s["bgAnimPlateColor"])),
          v["Plate colour"])
    check(rig, "row_plate_opacity", v["Plate opacity"] == "%d %%" % int(s["bgAnimPlateOpacity"]), v["Plate opacity"])
    check(rig, "row_element_tint", v["Element tint"] == ("On" if s["elementTintEnabled"] else "Off"),
          v["Element tint"])
    check(rig, "row_tint_colour", v["Tint colour"] == expected_palette_text(hex_to_int(s["elementTintColor"])),
          v["Tint colour"])

    check(rig, "row_text_scrim", v["Text scrim"] == "%d %%" % int(s["bgAnimScrim"]), v["Text scrim"])
    check(rig, "row_fade_out", v["Fade out"] == "%d ms" % int(s["bgFadeOutMs"]), v["Fade out"])
    check(rig, "row_fade_in", v["Fade in"] == "%d ms" % int(s["bgFadeInMs"]), v["Fade in"])
    check(rig, "row_fade_curve", v["Fade curve"] == FADE_CURVE_LABELS[int(s["bgFadeCurve"])], v["Fade curve"])
    check(rig, "row_interlace", v["Interlace"] == ("On" if int(s["bgAnimInterlace"]) else "Off"), v["Interlace"])

    for page, dump in dumps:
        a = rig.audit(dump)
        check(rig, "audit_page_%d_clean" % page, len(a["violations"]) == 0, repr(a["violations"]))

    close_animation(rig)


def check_no_op_visit(rig):
    """Acceptance: a visit that changes nothing writes nothing."""
    before = {k: rig.settings()[k] for k in ANIMATION_FIELDS}
    open_animation(rig)
    for page in range(1, int(rig.settingsui_state().get("pages", 1))):
        goto_page(rig, page)
    close_animation(rig)
    after = {k: rig.settings()[k] for k in ANIMATION_FIELDS}
    check(rig, "no_op_visit_writes_nothing", before == after, "before=%r after=%r" % (before, after))


def standby_choice(anim_id):
    """The row's cycle position for a stored id: 0 is "Same", n is animation
    n - 1 (CatAnimation.cpp, standbyChoiceIndex)."""
    return 0 if anim_id < 0 else anim_id + 1


def standby_id_for_choice(choice):
    return -1 if choice <= 0 else choice - 1


def standby_step(anim_id, direction):
    """One tap of the row's next/prev arrow, over "Same" plus the roster."""
    count = len(ANIM_NAMES) + 1
    return standby_id_for_choice((standby_choice(anim_id) + direction) % count)


def standby_label(anim_id):
    return "Same" if anim_id < 0 else ANIM_NAMES[anim_id]


def check_standby_anim(rig):
    """Acceptance (gm-3vj.49): the Standby anim row cycles through "Same" and
    then every animation, writes bgAnimStandbyId live, leaves bgAnimId alone,
    and its value survives closing and reopening the category. The rule the
    value drives (the standby screen playing that animation instead of the
    main one) is a render-path effect and is not observable here: the
    simulator has no renderer and no /api/debug/anim route (see "Not
    verified" at the bottom of this file)."""
    s0 = rig.settings()
    stored0 = int(s0["bgAnimStandbyId"])
    anim_id0 = int(s0["bgAnimId"])
    # The row shows "Same" for a stored id outside the roster, so the value
    # the arrows step from is the clamped one, not the raw stored one.
    start = stored0 if 0 <= stored0 < len(ANIM_NAMES) else -1

    open_animation(rig)
    d = page_with_row(rig, "Standby anim")
    check(rig, "standby_row_initial_text", rig.row_value(d, "Standby anim") == standby_label(start),
          "%r vs stored %d" % (rig.row_value(d, "Standby anim"), stored0))

    # Two taps forward: one crosses the "Same"/first-animation boundary from
    # wherever the fixture starts, the second lands on a name either way.
    expect = start
    for step in (1, 2):
        expect = standby_step(expect, +1)
        btn = rig.find_tag(page_with_row(rig, "Standby anim"), "Standby anim", "next")
        if btn is None:
            check(rig, "standby_next_found", False, "no next arrow on the Standby anim row")
            close_animation(rig)
            return
        rig.tap_target(btn)
        got = int(rig.settings()["bgAnimStandbyId"])
        check(rig, "standby_live_write_%d" % step, got == expect, "got %d want %d" % (got, expect))
        d = page_with_row(rig, "Standby anim")
        check(rig, "standby_row_value_%d" % step, rig.row_value(d, "Standby anim") == standby_label(expect),
              "%r want %r" % (rig.row_value(d, "Standby anim"), standby_label(expect)))

    check(rig, "standby_leaves_main_anim_alone", int(rig.settings()["bgAnimId"]) == anim_id0,
          "bgAnimId %r want %d" % (rig.settings()["bgAnimId"], anim_id0))

    for page, dump in all_row_values(rig)[1]:
        a = rig.audit(dump)
        check(rig, "standby_audit_page_%d_clean" % page, len(a["violations"]) == 0, repr(a["violations"]))

    # Persistence: close the category, reopen it, and the row reads back the
    # value the taps wrote rather than the one the visit opened on.
    close_animation(rig)
    persisted = int(rig.settings()["bgAnimStandbyId"])
    check(rig, "standby_persists_after_close", persisted == expect, "got %d want %d" % (persisted, expect))
    open_animation(rig)
    d = page_with_row(rig, "Standby anim")
    check(rig, "standby_row_after_reopen", rig.row_value(d, "Standby anim") == standby_label(expect),
          "%r want %r" % (rig.row_value(d, "Standby anim"), standby_label(expect)))

    # Restore, through the UI: two taps back the way they came. A stored id
    # outside the roster cannot be put back this way (the row cannot show
    # it), so that case is reported rather than forced.
    for _ in range(2):
        tap_row(rig, "Standby anim", "prev")
    close_animation(rig)
    final = int(rig.settings()["bgAnimStandbyId"])
    check(rig, "standby_restored", final == start, "got %d want %d" % (final, start))
    if final != stored0:
        rig.log("could_not_restore", bgAnimStandbyId=final, was=stored0)


def check_frame_rate_live_and_precedence(rig):
    """Acceptance: Frame rate writes live (stored value only, on the sim --
    the cap/frame-counter half of this check is device-only), and the
    touched-field precedence rule: this visit's edit outlives a web save
    that lands on the same field while the page is still open."""
    s0 = rig.settings()
    fps0 = int(s0["bgAnimFps"])
    direction = "minus" if fps0 >= 60 else "plus"
    delta = -5 if direction == "minus" else 5
    fps1_expected = max(5, min(60, fps0 + delta))

    open_animation(rig)
    # Frame rate moved off page 0 when the gradient and standby rows landed;
    # find it by name, the way every other row in this file is found.
    d0 = page_with_row(rig, "Frame rate")
    btn = rig.find_tag(d0, "Frame rate", direction)
    if btn is None:
        check(rig, "frame_rate_button_found", False, "no %r button tagged Frame rate" % direction)
        close_animation(rig)
        return
    rig.tap_target(btn)
    fps1 = int(rig.settings()["bgAnimFps"])
    check(rig, "frame_rate_live_write", fps1 == fps1_expected, "got %d want %d" % (fps1, fps1_expected))
    d0b = rig.touchmap(screen=0)
    check(rig, "frame_rate_row_value", rig.row_value(d0b, "Frame rate") == "%d fps" % fps1, rig.row_value(d0b, "Frame rate"))

    try:
        fps_web = next(c for c in (40, 45, 50, 55, 20, 25, 15, 10) if c not in (fps0, fps1))
        web_save(rig, {"bgAnimFps": fps_web})
        check(rig, "web_save_landed", int(rig.settings()["bgAnimFps"]) == fps_web,
              "got %r want %d" % (rig.settings()["bgAnimFps"], fps_web))

        rig.settingsui(pop=1)
        ok = rig.wait_until(lambda: int(rig.settings()["bgAnimFps"]) == fps1, timeout=6)
        check(rig, "touched_field_precedence", bool(ok), "expected %d after pop, got %r" % (fps1, rig.settings()["bgAnimFps"]))
    except RuntimeError as e:
        rig.log("touched_field_precedence_skipped", reason=str(e))
        rig.settingsui(pop=1)

    # Restore fps0 through the UI, never by POST.
    open_animation(rig)
    d = page_with_row(rig, "Frame rate")
    current = int(rig.settings()["bgAnimFps"])
    if current != fps0:
        restore_dir = "minus" if current > fps0 else "plus"
        restore_btn = rig.find_tag(d, "Frame rate", restore_dir)
        if restore_btn is not None:
            rig.tap_target(restore_btn)
    final = int(rig.settings()["bgAnimFps"])
    check(rig, "frame_rate_restored", final == fps0, "got %d want %d" % (final, fps0))
    close_animation(rig)


def check_theme_recolor(rig):
    """Acceptance: Theme Dark<->Light recolours the settings page's own
    labels within 500 ms. applyTheme()/change_color_theme() (DefaultUI.cpp)
    are not GAGGIMATE_SIM-guarded (only the Amoled-specific override inside
    applyTheme() is), so this is checkable on the simulator, unlike the
    animation/plate live-apply paths a few lines above it in the same file,
    which are guarded whole. Pixel differencing rather than an exact colour
    match: theme_colors[...][0]'s value is eez-generated and not worth
    hand-transcribing here just to duplicate what the framebuffer already
    proves."""
    s0 = rig.settings()
    mode0 = int(s0["themeMode"])
    open_animation(rig)
    # Theme moved off page 0 when the Standby anim row was added (gm-3vj.49),
    # so the row is reached by name, like every other row this file touches.
    d = page_with_row(rig, "Theme")
    row = rig.find_tag(d, "Theme", "row")
    if row is None:
        check(rig, "theme_row_found", False)
        close_animation(rig)
        return
    x1, y1, x2, y2 = row["hit"]
    points = [(x1 + int((x2 - x1) * f), (y1 + y2) // 2) for f in (0.3, 0.5, 0.7)]

    # step=2 (240x240): the device delivers the framebuffer only at step 2.
    fb_before = rig.get_bytes("/api/debug/fb?step=2")
    colors_before = [rgb565_pixel(fb_before, x // 2, y // 2, w=240) for x, y in points]

    next_btn = rig.find_tag(d, "Theme", "next")
    rig.tap_target(next_btn)
    rig.wait_until(lambda: int(rig.settings()["themeMode"]) != mode0, timeout=2)
    time.sleep(0.5)  # one more rerender pass for applyTheme()'s change_color_theme + the page's own rebuildPage
    fb_after = rig.get_bytes("/api/debug/fb?step=2")
    colors_after = [rgb565_pixel(fb_after, x // 2, y // 2, w=240) for x, y in points]

    check(rig, "theme_mode_flipped", int(rig.settings()["themeMode"]) == (1 - mode0))
    check(rig, "theme_recolor_pixels_changed", any(a != b for a, b in zip(colors_before, colors_after)),
          "before=%r after=%r" % (colors_before, colors_after))

    prev_btn = rig.find_tag(rig.touchmap(screen=0), "Theme", "prev")
    rig.tap_target(prev_btn)
    ok = rig.wait_until(lambda: int(rig.settings()["themeMode"]) == mode0, timeout=2)
    check(rig, "theme_restored", bool(ok), "got %r want %d" % (rig.settings()["themeMode"], mode0))
    close_animation(rig)


def check_gradient_default_and_builtin(rig):
    """Acceptance: choosing a gradient writes bgAnimThemeMap's entry for the
    current animation only; Default clears it. The library half of this
    criterion (a real library entry, created through the web UI first) is
    not run here: bg_library_valid always returns false on the simulator
    (sim/platform/bganim_stub.cpp), so no POST can ever put a nonempty
    library string into Settings there, and this scenario never POSTs by
    hand except through web_save's documented, narrow use above. The
    built-in-theme half exercises the same map-write code path."""
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    map0 = s0["bgAnimThemeMap"]

    open_animation(rig)
    d = page_with_row(rig, "Gradient")
    grad0_text = rig.row_value(d, "Gradient")
    next_btn = rig.find_tag(d, "Gradient", "next")
    if next_btn is None:
        check(rig, "gradient_next_found", False)
        close_animation(rig)
        return
    rig.tap_target(next_btn)
    d2 = rig.touchmap(screen=0)
    check(rig, "gradient_cycles_from_default", rig.row_value(d2, "Gradient") == THEME_NAMES[0], rig.row_value(d2, "Gradient"))

    map1 = rig.settings()["bgAnimThemeMap"]
    check(rig, "gradient_writes_current_anim_slot", map_ref(map1, anim0) == "0", map_ref(map1, anim0))
    others_untouched = all(map_ref(map1, i) == map_ref(map0, i) for i in range(len(ANIM_NAMES)) if i != anim0)
    check(rig, "gradient_writes_only_current_anim", others_untouched)

    rig.tap_target(rig.find_tag(rig.touchmap(screen=0), "Gradient", "prev"))
    d3 = rig.touchmap(screen=0)
    check(rig, "gradient_restores_default_text", rig.row_value(d3, "Gradient") == grad0_text, rig.row_value(d3, "Gradient"))
    map2 = rig.settings()["bgAnimThemeMap"]
    check(rig, "gradient_default_clears_entry", map_ref(map2, anim0) == "", map_ref(map2, anim0))
    close_animation(rig)


def check_gradient_precedence_across_animations(rig):
    """Regression for the gm-flw.9 review on eed10a33: CatAnimation.cpp
    originally tracked only the most recently touched animation's gradient
    edit (a single gradientTouchedAnimId/gradientLastRef pair). Touch
    Gradient for animation A, switch to B, touch Gradient for B too: both
    are now touched this visit, not just B, and commit() must write both
    slots (one setBgAnimThemeMap call covering every touched entry whose
    stored ref no longer matches this visit's last write), not just the
    last one touched.

    The review's own scenario adds a web save that replaces the whole map
    with a different value for A's slot while both are touched, then
    checks the display's value for A wins at pop. That step cannot run
    here: sim/platform/bganim_stub.cpp's bg_map_valid() always returns
    false, and WebUIPlugin.cpp gates every POST to bgAnimThemeMap behind
    it (`if (request->hasArg("bgAnimThemeMap") && bg_map_valid(...))
    settings->setBgAnimThemeMap(...)`), so no POST -- built-in ref or
    library ref alike -- can ever change that field on the simulator; the
    line below confirms this by attempting exactly the review's POST and
    asserting the sim's own gate holds. This is a wider version of the
    already-documented library-only gap: the sim cannot exercise an
    external write to bgAnimThemeMap at all, not just a library one. What
    is verified here instead: both A's and B's live-written slots survive
    an ordinary pop with no interference (the commit loop does not drop
    an earlier touched entry just because a later one was touched too),
    which is the one piece of this criterion the simulator can observe.
    The interference half needs the device (POST is not gated there)."""
    s0 = rig.settings()
    anim_a = int(s0["bgAnimId"])
    anim_b = (anim_a + 1) % len(ANIM_NAMES)
    # What the two slots held before this check touched them. Not assumed to
    # be "": the bench board carries overrides on fourteen animations, and a
    # restore is back to what was there, not to Default.
    map0 = s0["bgAnimThemeMap"]
    ref0_a = map_ref(map0, anim_a)
    ref0_b = map_ref(map0, anim_b)

    open_animation(rig)
    tap_row(rig, "Gradient", "next")
    ref_a = map_ref(rig.settings()["bgAnimThemeMap"], anim_a)
    check(rig, "precedence_setup_touch_a", ref_a != "", "anim %d ref=%r" % (anim_a, ref_a))

    tap_row(rig, "Animation", "next")
    check(rig, "precedence_moved_to_b", int(rig.settings()["bgAnimId"]) == anim_b, rig.settings()["bgAnimId"])

    tap_row(rig, "Gradient", "next")
    map_after_b = rig.settings()["bgAnimThemeMap"]
    ref_b = map_ref(map_after_b, anim_b)
    check(rig, "precedence_setup_touch_b", ref_b != "", "anim %d ref=%r" % (anim_b, ref_b))

    # The review's own interference step: a web save replacing the whole
    # map with a different value for A's slot. Confirmed rejected outright
    # by the sim's bg_map_valid() gate (see docstring); this asserts that
    # rejection rather than skipping it, so a future change loosening the
    # host stub is caught here instead of silently going untested.
    web_ref_a = "1" if ref_a != "1" else "2"
    web_map = map_write_ref(map_after_b, anim_a, web_ref_a)
    try:
        web_save(rig, {"bgAnimThemeMap": web_map})
        after_post = map_ref(rig.settings()["bgAnimThemeMap"], anim_a)
        check(rig, "webpost_bgAnimThemeMap_rejected_on_sim", after_post == ref_a,
              "anim %d ref after the POST attempt: got %r, want unchanged %r (a mismatch means the sim's "
              "bg_map_valid stub started accepting writes; revisit this test's device-only note)" %
              (anim_a, after_post, ref_a))
    except RuntimeError as e:
        rig.log("gradient_precedence_web_save_unavailable", reason=str(e))

    rig.settingsui(pop=1)

    def restored():
        m = rig.settings()["bgAnimThemeMap"]
        return map_ref(m, anim_a) == ref_a and map_ref(m, anim_b) == ref_b

    ok = rig.wait_until(restored, timeout=6)
    final_map = rig.settings()["bgAnimThemeMap"]
    check(rig, "gradient_precedence_a_survives", map_ref(final_map, anim_a) == ref_a,
          "anim %d got %r want %r" % (anim_a, map_ref(final_map, anim_a), ref_a))
    check(rig, "gradient_precedence_b_survives", map_ref(final_map, anim_b) == ref_b,
          "anim %d got %r want %r" % (anim_b, map_ref(final_map, anim_b), ref_b))
    check(rig, "gradient_precedence_both_survive", bool(ok))

    # Restore both through the UI, never by a second POST. This visit opens
    # on animId==anim_b (bgAnimId was written live by the "Animation next" tap
    # above), Gradient showing ref_b one step on from where it started; undo
    # it, step Animation back to A, undo A's edit the same way.
    open_animation(rig)
    tap_row(rig, "Gradient", "prev")
    tap_row(rig, "Animation", "prev")
    tap_row(rig, "Gradient", "prev")
    close_animation(rig)

    restored_map = rig.settings()["bgAnimThemeMap"]
    a_back = map_ref(restored_map, anim_a) == ref0_a
    b_back = map_ref(restored_map, anim_b) == ref0_b
    check(rig, "gradient_precedence_restored", a_back and b_back,
          "anim_a=%r want %r, anim_b=%r want %r" %
          (map_ref(restored_map, anim_a), ref0_a, map_ref(restored_map, anim_b), ref0_b))
    if not (a_back and b_back):
        rig.log("could_not_restore", bgAnimThemeMap=restored_map)


def check_plates_and_tint_coupling(rig):
    """Acceptance: Plate colour/opacity are enabled only when Plates is
    Custom; Tint colour only when Element tint is on; a tap on a disabled
    row's arrows is ignored (the setting does not change). Restores every
    value this check moved, through the UI."""
    s0 = rig.settings()
    plates0 = int(s0["bgAnimClearPlates"])
    opacity0 = int(s0["bgAnimPlateOpacity"])
    tint0 = bool(s0["elementTintEnabled"])

    open_animation(rig)
    d1 = page_with_row(rig, "Plates")

    steps_to_keep = (0 - plates0) % 3
    for _ in range(steps_to_keep):
        d1 = rig.touchmap(screen=0)
        rig.tap_target(rig.find_tag(d1, "Plates", "next"))
    d1 = rig.touchmap(screen=0)
    check(rig, "plates_at_keep", rig.row_value(d1, "Plates") == "Keep", rig.row_value(d1, "Plates"))

    before_color = rig.settings()["bgAnimPlateColor"]
    rig.tap_target(rig.find_tag(d1, "Plate colour", "next"))
    check(rig, "plate_color_disabled_ignored", rig.settings()["bgAnimPlateColor"] == before_color)

    opacity_dir = "minus" if opacity0 >= 100 else "plus"
    before_opacity = int(rig.settings()["bgAnimPlateOpacity"])
    rig.tap_target(rig.find_tag(d1, "Plate opacity", opacity_dir))
    check(rig, "plate_opacity_disabled_ignored", int(rig.settings()["bgAnimPlateOpacity"]) == before_opacity)

    for _ in range(2):  # Keep(0) -> Hide(1) -> Custom(2)
        d1 = rig.touchmap(screen=0)
        rig.tap_target(rig.find_tag(d1, "Plates", "next"))
    d1 = rig.touchmap(screen=0)
    check(rig, "plates_at_custom", rig.row_value(d1, "Plates") == "Custom", rig.row_value(d1, "Plates"))

    before_color = rig.settings()["bgAnimPlateColor"]
    rig.tap_target(rig.find_tag(d1, "Plate colour", "next"))
    after_color = rig.settings()["bgAnimPlateColor"]
    check(rig, "plate_color_enabled_changes", after_color != before_color, "%r -> %r" % (before_color, after_color))

    before_opacity = int(rig.settings()["bgAnimPlateOpacity"])
    rig.tap_target(rig.find_tag(rig.touchmap(screen=0), "Plate opacity", opacity_dir))
    after_opacity = int(rig.settings()["bgAnimPlateOpacity"])
    check(rig, "plate_opacity_enabled_changes", after_opacity != before_opacity, "%r -> %r" % (before_opacity, after_opacity))

    # Element tint and Tint colour are on different pages since gm-3vj.2, so
    # the two rows are reached by name from here on.
    dt = page_with_row(rig, "Element tint")
    if rig.row_value(dt, "Element tint") == "On":
        rig.tap_target(rig.find_tag(dt, "Element tint", "toggle"))
    dt = rig.touchmap(screen=0)
    check(rig, "tint_off_for_test", rig.row_value(dt, "Element tint") == "Off", rig.row_value(dt, "Element tint"))
    before_tc = rig.settings()["elementTintColor"]
    tap_row(rig, "Tint colour", "next")
    check(rig, "tint_color_disabled_ignored", rig.settings()["elementTintColor"] == before_tc)

    tap_row(rig, "Element tint", "toggle")
    dt = page_with_row(rig, "Element tint")
    check(rig, "tint_on_for_test", rig.row_value(dt, "Element tint") == "On", rig.row_value(dt, "Element tint"))
    before_tc = rig.settings()["elementTintColor"]
    tap_row(rig, "Tint colour", "next")
    after_tc = rig.settings()["elementTintColor"]
    check(rig, "tint_color_enabled_changes", after_tc != before_tc, "%r -> %r" % (before_tc, after_tc))

    # Restore, through the UI, in reverse.
    tap_row(rig, "Tint colour", "prev")  # undo the one "next" above
    if not tint0:
        tap_row(rig, "Element tint", "toggle")
    dt = page_with_row(rig, "Element tint")
    check(rig, "tint_enabled_restored", bool(rig.row_value(dt, "Element tint") == "On") == tint0)

    tap_row(rig, "Plate colour", "prev")  # undo the one "next" above
    restore_opacity_dir = "plus" if opacity_dir == "minus" else "minus"
    tap_row(rig, "Plate opacity", restore_opacity_dir)
    steps_from_custom = (plates0 - 2) % 3
    for _ in range(steps_from_custom):
        tap_row(rig, "Plates", "next")

    final = rig.settings()
    check(rig, "plates_restored", int(final["bgAnimClearPlates"]) == plates0,
          "got %r want %d" % (final["bgAnimClearPlates"], plates0))
    check(rig, "plate_opacity_restored", int(final["bgAnimPlateOpacity"]) == opacity0,
          "got %r want %d" % (final["bgAnimPlateOpacity"], opacity0))
    if int(final["bgAnimClearPlates"]) != plates0 or int(final["bgAnimPlateOpacity"]) != opacity0:
        rig.log("could_not_restore", plates=final["bgAnimClearPlates"], plateOpacity=final["bgAnimPlateOpacity"])
    close_animation(rig)


def params_group(packed, anim_id):
    """One animation's ';'-separated group of the bgAnimParams string, "" when
    the string does not reach that far (which reads as "every parameter at its
    default"; SettingsModel.h)."""
    parts = str(packed or "").split(";")
    return parts[anim_id] if 0 <= anim_id < len(parts) else ""


def other_groups(packed, skip_id, count):
    """Every group but `skip_id`, as text, for a "nothing else moved" check."""
    return {i: params_group(packed, i) for i in range(count) if i != skip_id}


def params_set_group(packed, anim_id, group):
    """`packed` with one animation's group replaced, for the web-save half of
    the Parameters check. The display's own writer does the same thing in C++
    (SettingsModel.cpp, bgParamsWriteGroup)."""
    parts = str(packed or "").split(";")
    while len(parts) <= anim_id:
        parts.append("")
    parts[anim_id] = group
    return ";".join(parts)


def wait_depth(rig, depth, timeout=5):
    def at():
        state = rig.settingsui_state()
        return state if int(state.get("depth", -1)) == depth else None

    return rig.wait_until(at, timeout)


def check_parameters_page(rig):
    """Acceptance (gm-3vj.2): the Parameters row opens a page of one stepper
    per parameter the current animation defines plus a Reset row; a step
    writes that animation's group of bgAnimParams and no other group; Reset
    puts the group back to the defaults; the chevron returns to the Animation
    page.

    The step and Reset halves are only exercised when the animation's group
    is unset to begin with, which is what a fresh device and the simulator
    fixture both store: Reset empties the group, so a run that started from
    stored parameter values could not put them back through the UI
    afterwards. The device runner hits that case if the bench board has
    edited parameters, and logs it rather than moving a stored value it
    cannot restore."""
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    packed0 = s0.get("bgAnimParams", "")
    group0 = params_group(packed0, anim0)
    others0 = other_groups(packed0, anim0, len(ANIM_NAMES))

    d0 = open_animation(rig)
    row = rig.find_tag(d0, "Parameters", "action")
    if row is None:
        check(rig, "params_row_found", False, "no Parameters action row on the Animation page")
        close_animation(rig)
        return
    rig.tap_target(row)
    state = wait_depth(rig, 2)
    if state is None:
        check(rig, "params_page_pushed", False, repr(rig.settingsui_state()))
        close_animation(rig)
        return
    check(rig, "params_page_pushed", True)

    # The title is the animation's name, with " params" behind it when the
    # shell's 144 px title box can hold both (CatAnimParams.cpp).
    name = ANIM_NAMES[anim0]
    check(rig, "params_title", state.get("title") in (name, name + " params"),
          "%r for animation %r" % (state.get("title"), name))

    # Every page of the pushed page: audited, and its row list read.
    pages = int(state.get("pages", 1))
    names, dumps = [], []
    for page in range(pages):
        dump = goto_page(rig, page)
        dumps.append((page, dump))
        names += rig.rows_on_page(dump)
    for page, dump in dumps:
        a = rig.audit(dump)
        check(rig, "params_audit_page_%d_clean" % page, len(a["violations"]) == 0, repr(a["violations"]))
    check(rig, "params_has_reset_row", names and names[-1] == "Reset to defaults", repr(names))
    param_names = names[:-1]
    check(rig, "params_row_count", 1 <= len(param_names) <= 8, repr(names))
    if not param_names:
        rig.settingsui(pop=1)
        close_animation(rig)
        return

    # Step the first parameter twice. Its row is the animation's slot 0: the
    # page lists the slots the animation defines in slot order, and no
    # animation in the roster leaves an undefined slot before a defined one.
    first = param_names[0]
    d = page_with_row(rig, first)
    at_open = int(rig.row_value(d, first))
    direction, delta = ("plus", 5) if at_open <= 90 else ("minus", -5)
    expected = max(0, min(100, at_open + 2 * delta))
    for _ in range(2):
        tap_row(rig, first, direction)
    d = page_with_row(rig, first)
    shown = int(rig.row_value(d, first))
    check(rig, "params_row_stepped", shown == expected, "got %d want %d (from %d)" % (shown, expected, at_open))

    packed1 = rig.settings().get("bgAnimParams", "")
    group1 = params_group(packed1, anim0)
    slots1 = group1.split(",")
    check(rig, "params_group_written", len(slots1) == 8, "%r" % group1)
    check(rig, "params_group_first_slot", slots1 and slots1[0] == str(expected), "%r" % group1)
    others1 = other_groups(packed1, anim0, len(ANIM_NAMES))
    check(rig, "params_other_groups_untouched", others1 == others0,
          "changed: %r" % {k: (others0[k], others1[k]) for k in others0 if others0[k] != others1[k]})

    if group0 == "":
        # Reset: the group goes back to being unset, and every row shows the
        # animation's own default again.
        d = page_with_row(rig, "Reset to defaults")
        target = rig.find_tag(d, "Reset to defaults", "confirm")
        if target is None:
            check(rig, "params_reset_target_found", False, repr(rig.rows_on_page(d)))
        else:
            rig.tap_target(target, ms=2500)
            packed2 = rig.settings().get("bgAnimParams", "")
            check(rig, "params_reset_clears_group", params_group(packed2, anim0) == "",
                  "%r" % params_group(packed2, anim0))
            d = page_with_row(rig, first)
            back = int(rig.row_value(d, first))
            check(rig, "params_reset_restores_row", back == at_open, "got %d want %d" % (back, at_open))
            others2 = other_groups(packed2, anim0, len(ANIM_NAMES))
            check(rig, "params_reset_leaves_others", others2 == others0)
    else:
        # Started from stored values: undo the two steps instead of resetting.
        undo = "minus" if direction == "plus" else "plus"
        for _ in range(2):
            tap_row(rig, first, undo)
        rig.log("params_reset_skipped", reason="animation %d has stored parameters (%r)" % (anim0, group0))

    # Out through the chevron: back on the Animation page, not closed.
    d = page_with_row(rig, "Reset to defaults") if "Reset to defaults" in names else rig.touchmap(screen=0)
    chevron = rig.find_tag(d, "exit", "exit")
    if chevron is None:
        check(rig, "params_chevron_found", False)
    else:
        rig.tap_target(chevron)
        back = wait_depth(rig, 1)
        check(rig, "params_chevron_returns_to_parent", back is not None, repr(rig.settingsui_state()))
        if back is not None:
            check(rig, "params_parent_page_rebuilt",
                  rig.find_tag(rig.touchmap(screen=0), "Animation", "value") is not None,
                  repr(rig.rows_on_page(rig.touchmap(screen=0))))
    close_animation(rig)

    final = rig.settings().get("bgAnimParams", "")
    check(rig, "params_visit_restored", params_group(final, anim0) == group0,
          "got %r want %r" % (params_group(final, anim0), group0))
    if params_group(final, anim0) != group0:
        rig.log("could_not_restore", bgAnimParams=final)


def check_parameters_web_precedence(rig):
    """Acceptance: a web save while the Parameters page is open is per-field
    last writer wins, per parameter. A slot this visit stepped keeps the
    visit's value and is re-asserted at pop; a slot it did not touch takes
    the web value on the next pass.

    Unlike bgAnimThemeMap, bgAnimParams is not gated behind a validity check
    in WebUIPlugin.cpp, so the simulator can run this whole criterion: the
    POST lands. Simulator only all the same, because web_save refuses any
    host but loopback."""
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    packed0 = s0.get("bgAnimParams", "")
    if params_group(packed0, anim0) != "":
        rig.log("params_web_precedence_skipped", reason="animation %d has stored parameters" % anim0)
        return

    d0 = open_animation(rig)
    row = rig.find_tag(d0, "Parameters", "action")
    if row is None:
        check(rig, "params_web_row_found", False)
        close_animation(rig)
        return
    rig.tap_target(row)
    if wait_depth(rig, 2) is None:
        check(rig, "params_web_page_pushed", False, repr(rig.settingsui_state()))
        close_animation(rig)
        return

    names = rig.rows_on_page(rig.touchmap(screen=0))
    param_names = [n for n in names if n != "Reset to defaults"]
    if len(param_names) < 2:
        rig.log("params_web_precedence_skipped", reason="animation %d defines fewer than two parameters" % anim0)
        rig.settingsui(pop=1)
        close_animation(rig)
        return
    touched_row, untouched_row = param_names[0], param_names[1]

    d = page_with_row(rig, touched_row)
    at_open = int(rig.row_value(d, touched_row))
    direction, delta = ("plus", 5) if at_open <= 95 else ("minus", -5)
    expected = max(0, min(100, at_open + delta))
    tap_row(rig, touched_row, direction)

    # A web save that moves both slots: the touched one to a third value,
    # the untouched one to something the page has never shown.
    packed1 = rig.settings().get("bgAnimParams", "")
    slots = params_group(packed1, anim0).split(",")
    web_touched = "5" if expected != 5 else "10"
    web_untouched = "15" if slots[1] != "15" else "20"
    slots[0], slots[1] = web_touched, web_untouched
    try:
        web_save(rig, {"bgAnimParams": params_set_group(packed1, anim0, ",".join(slots))})
    except RuntimeError as e:
        rig.log("params_web_precedence_unavailable", reason=str(e))
        rig.settingsui(pop=1)
        close_animation(rig)
        return

    def untouched_shows_web():
        dump = page_with_row(rig, untouched_row)
        return dump if rig.row_value(dump, untouched_row) == web_untouched else None

    ok = rig.wait_until(untouched_shows_web, timeout=6)
    check(rig, "params_untouched_slot_takes_web_value", ok is not None,
          "row %r shows %r, want %r" % (untouched_row, rig.row_value(page_with_row(rig, untouched_row), untouched_row),
                                        web_untouched))
    d = page_with_row(rig, touched_row)
    check(rig, "params_touched_slot_keeps_draft", int(rig.row_value(d, touched_row)) == expected,
          "row %r shows %r, want %d" % (touched_row, rig.row_value(d, touched_row), expected))

    rig.settingsui(pop=1)
    wait_depth(rig, 1)
    final = params_group(rig.settings().get("bgAnimParams", ""), anim0).split(",")
    check(rig, "params_commit_reasserts_touched_slot", final and final[0] == str(expected),
          "slot 0 is %r, want %r" % (final[0] if final else None, str(expected)))
    check(rig, "params_commit_keeps_web_slot", len(final) > 1 and final[1] == web_untouched,
          "slot 1 is %r, want %r" % (final[1] if len(final) > 1 else None, web_untouched))

    # Put the animation's group back to unset, through the UI.
    rig.tap_target(rig.find_tag(rig.touchmap(screen=0), "Parameters", "action"))
    if wait_depth(rig, 2) is not None:
        d = page_with_row(rig, "Reset to defaults")
        target = rig.find_tag(d, "Reset to defaults", "confirm")
        if target is not None:
            rig.tap_target(target, ms=2500)
        rig.settingsui(pop=1)
        wait_depth(rig, 1)
    close_animation(rig)

    restored = rig.settings().get("bgAnimParams", "")
    check(rig, "params_web_precedence_restored", params_group(restored, anim0) == "",
          "%r" % params_group(restored, anim0))
    if params_group(restored, anim0) != "":
        rig.log("could_not_restore", bgAnimParams=restored)


# Every check, in order. One list, read by main() and by run() below, so a
# standalone invocation and the end-to-end runner cannot drift apart.
CHECKS = [
    ("rows_match_settings", check_rows_match_settings),
    ("no_op_visit", check_no_op_visit),
    ("standby_anim", check_standby_anim),
    ("frame_rate_live_and_precedence", check_frame_rate_live_and_precedence),
    ("theme_recolor", check_theme_recolor),
    ("gradient_default_and_builtin", check_gradient_default_and_builtin),
    ("gradient_precedence_across_animations", check_gradient_precedence_across_animations),
    ("plates_and_tint_coupling", check_plates_and_tint_coupling),
    ("parameters_page", check_parameters_page),
    ("parameters_web_precedence", check_parameters_web_precedence),
]


def run(rig, report, venue):
    """Entry point for the end-to-end runner (tools/settings_ui_test.py).
    Raises AssertionError listing the checks that failed, naming the row and
    value each one saw. Every check here works on both venues; the ones the
    simulator cannot reach (a library gradient, an external write to the
    theme map) already assert the simulator's own refusal internally."""
    del venue  # nothing here restarts the process or reads the log file
    first_fail, first_total = len(FAILURES), TOTAL
    for name, fn in CHECKS:
        run_check(rig, name, fn)
    report.step("scenario_checks", scenario="animation", checks=TOTAL - first_total,
                failed=len(FAILURES) - first_fail)
    new_failures = FAILURES[first_fail:]
    if new_failures:
        raise AssertionError("; ".join("%s: %s" % (n, d) for n, d in new_failures))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--program", default=DEFAULT_PROGRAM)
    ap.add_argument("--workdir", default=os.path.join(tempfile.gettempdir(), "gm_settings_ui_tests", "test_animation"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("GM_SIM_PORT", "8090")))
    ap.add_argument("--host", default=None, help="run against a device/loadtest build instead of the simulator")
    args = ap.parse_args()

    checks = CHECKS

    if args.host:
        rig = Rig(args.host)
        rig.log("boot", host=args.host)
        for name, fn in checks:
            run_check(rig, name, fn)
    else:
        if not os.path.isfile(args.program):
            print("simulator binary not found at %r; build it first: pio run -e display-sim" % args.program, file=sys.stderr)
            return 1
        data_dir = os.path.join(args.workdir, "sim_data")
        os.makedirs(args.workdir, exist_ok=True)
        with Sim(args.program, data_dir, port=args.port) as sim:
            rig = sim.rig
            rig.log("boot", program=args.program, port=args.port, workdir=args.workdir)
            for name, fn in checks:
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

# ---------------------------------------------------------------------------
# Not verified here (device-only or out of this script's reach):
#
# - Animation cycling's device assertions: /api/debug/anim anim_id moving
#   within 500 ms, a full cycle through every animation leaving int_free
#   within 2 KB and hot_fail/uptime_ms unchanged (CLAUDE.md: the sim has no
#   renderer and no /api/debug/anim route at all). Command for the leader:
#   loop `rig.tap_target(find_tag(dump, "Animation", "next"))` once a
#   second across all 14 animations against Rig("192.168.1.121"), reading
#   rig.anim() and rig.heap() between taps.
# - The Standby anim row's own effect (gm-3vj.49): the standby screen
#   playing the chosen animation and the other screens keeping the main one,
#   and the switch on a standby entry or exit costing no more than a live
#   change of the main id does. Neither is observable here (no renderer, no
#   /api/debug/anim route on the simulator). Command for the leader against
#   the device: set Standby anim to an animation that differs from
#   Animation, read rig.anim()["anim_id"] on an active screen, let the
#   standby timeout land or force the standby screen, and read anim_id
#   again; then walk in and out of standby a few times watching
#   /api/debug/anim frames and /api/debug/heap int_free and hot_fail.
# - Frame rate's device assertion: /api/debug/anim cap reaching 60 within
#   500 ms, and the passive frame-counter rate check (orbits, all-screens
#   on, interlace confirmed active). Command: rig.anim() before/after the
#   Frame rate stepper reaches 60, plus a 5 s frame-counter delta at 30 vs
#   60 with All screens on.
# - Plates' pixel effect on the brew screen (device framebuffer at a plate
#   pixel after popping and opening brew): the sim has no plate rendering
#   (CLAUDE.md). Command: rig.fb_png(...) on the brew screen after setting
#   Plates=Custom with a distinct colour, against the device.
# - bganim:preview-end firing on an Animation/Gradient change: not
#   HTTP-observable from either venue with the routes this epic has;
#   inferred from the write path (CatAnimation.cpp calls
#   plugins().trigger("bganim:preview-end") in both onCycle handlers) but
#   not independently confirmed by a device test here.
# - The gradient library half of the Gradient acceptance criterion (a real
#   library entry created through the web UI, then chosen): the simulator's
#   bg_library_valid always returns false (sim/platform/bganim_stub.cpp), so
#   no POST can create one there; needs a browser and the loadtest device.
# - The touched-field-precedence half of the gradient regression (gm-flw.9
#   review on eed10a33): a web save landing on a touched animation's slot
#   while a different animation's slot is also touched, verifying the
#   display's value wins for both. Not just the library case: bg_map_valid
#   (sim/platform/bganim_stub.cpp) unconditionally returns false, so
#   WebUIPlugin.cpp's `hasArg("bgAnimThemeMap") && bg_map_valid(...)` gate
#   rejects every POST to that field on the simulator, built-in refs
#   included, not only library ones. check_gradient_precedence_across_
#   animations asserts that gate holds, then verifies only the interference
#   -free half (both slots survive an ordinary pop). Command for the
#   leader against the device: repeat that check's setup (touch Gradient
#   for animation A, tap Animation once, touch Gradient for B), POST a
#   third value for A's slot from the web UI while the category is still
#   open, pop, and confirm /api/settings bgAnimThemeMap holds A's and B's
#   display-set refs, not the POSTed one.
# - "the web UI still loads the Display tab" after a gradient change: needs
#   a real browser.
