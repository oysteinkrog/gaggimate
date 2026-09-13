#!/usr/bin/env python3
"""Scenario for the Animation settings category (gm-flw.9): animation, the
standby screen's own animation (gm-3vj.49), frame rate, all-screens, theme,
gradient, plates, plate colour/opacity, element tint/colour and text scrim,
every value row live, plus the Parameters child page the Parameters row
pushes (gm-3vj.2) and the gradient picker the three gradient rows push
(gm-nov3.3). Built on
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
import re
import sys
import tempfile
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO_ROOT)

from tools.settings_ui_tests import Rig, Sim  # noqa: E402
from tools.settings_ui_tests.gradients_gen import (  # noqa: E402
    GRADIENT_CATEGORIES,
    GRADIENT_CATEGORY_OF,
    GRADIENT_NAMES as THEME_NAMES,
)

DEFAULT_PROGRAM = os.path.join(REPO_ROOT, ".pio", "build", "display-sim", "program")

# THEME_NAMES above is BgAnimThemes.cpp's THEMES order (the built-in gradient
# names), generated from data/gradients.json by scripts/gen_gradients.py along
# with the firmware and web tables, so the runner cannot drift from what the
# display shows. tools/animbench make check fails on a stale copy.
#
# ANIM_NAMES below is still hand written: it mirrors BgAnimRegistry.cpp's
# REGISTRY order (animation display names), which CatAnimation.cpp also keeps
# a mirror of (see that file's comment). REGISTRY compiles only outside
# GAGGIMATE_SIM, so there is no device route this script could read it from
# instead.
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


def library_entries(packed):
    """Python mirror of SettingsModel.cpp's parseGradientLibrary: the
    (id, name, gradient) triples of the "id|name|gradient;..." string, in
    stored order, dropping anything that does not have the three parts."""
    out = []
    for entry in str(packed or "").split(";"):
        if not entry:
            continue
        parts = entry.split("|", 2)
        if len(parts) != 3 or not parts[0].isdigit() or int(parts[0]) <= 0 or not parts[1]:
            continue
        out.append((int(parts[0]), parts[1], parts[2]))
    return out


def gradient_name_for_ref(settings, ref):
    """The name the display shows for one ref: a decimal index names a
    built-in, "cN" names a saved gradient, and anything that resolves to
    neither returns None. Library refs really do resolve on the simulator
    since gm-nov3.3: BgAnimThemes.cpp compiles on the host now, so the sim
    runs the same gradient rules the device does."""
    ref = str(ref or "")
    if ref == "":
        return None
    if ref.startswith("c"):
        if not ref[1:].isdigit():
            return None
        wanted = int(ref[1:])
        for entry_id, name, _gradient in library_entries(settings.get("bgAnimGradients", "")):
            if entry_id == wanted:
                return name
        return None
    if not ref.isdigit():
        return None
    idx = int(ref)
    return THEME_NAMES[idx] if 0 <= idx < len(THEME_NAMES) else None


# The legacy bgAnimTheme namespace, frozen at 18 to match
# BG_THEME_LEGACY_CUSTOM in src/display/ui/default/bganim/BgAnim.h. It is not
# len(THEME_NAMES): the built-in table grows, and a device that stored 18 means
# the custom gradient, not whatever gradient is appended at index 18.
BG_THEME_LEGACY_CUSTOM = 18


def custom_theme_valid(custom):
    """Whether bgAnimCustomTheme is a gradient the firmware's parser accepts.
    Python mirror of bg_custom_valid (BgAnimThemes.cpp, parseGradient): two to
    sixteen six-digit colours separated by commas or spaces, each with an
    optional @position of one to four digits that may not exceed 255. A leading
    # is tolerated on each colour, as the parser tolerates it."""
    parts = [p for p in re.split(r"[\s,]+", str(custom or "")) if p]
    if not 2 <= len(parts) <= 16:
        return False
    for part in parts:
        m = re.fullmatch(r"#?[0-9a-fA-F]{6}(?:@(\d{1,4}))?", part)
        if m is None:
            return False
        if m.group(1) is not None and int(m.group(1)) > 255:
            return False
    return True


def legacy_builtin(theme_id, custom_valid):
    """Python mirror of bg_legacy_builtin (BgAnim.h): the built-in the legacy
    pair resolves to, or -1 when the custom gradient is what it draws."""
    if theme_id == BG_THEME_LEGACY_CUSTOM:
        return -1 if custom_valid else 0
    return theme_id if 0 <= theme_id < BG_THEME_LEGACY_CUSTOM else 0


def expected_global_gradient_text(settings):
    """The Gradient all row's value: bgAnimGradientRef when it names something
    that exists, else what the legacy pair resolves to. The legacy integer goes
    through the frozen namespace rather than indexing THEME_NAMES directly, so
    a stored 18 reads as the retained custom gradient the way the display reads
    it (CatAnimation.cpp, globalGradientLabel) instead of naming the built-in
    that lands at index 18."""
    name = gradient_name_for_ref(settings, settings.get("bgAnimGradientRef", ""))
    if name is not None:
        return name
    builtin = legacy_builtin(int(settings["bgAnimTheme"]),
                             custom_theme_valid(settings.get("bgAnimCustomTheme", "")))
    if builtin < 0:
        return "Custom (legacy)"
    return THEME_NAMES[builtin] if 0 <= builtin < len(THEME_NAMES) else THEME_NAMES[0]


def expected_gradient_text(settings, anim_id=None):
    """Python mirror of gradientChoices()/gradientChoiceIndexForRef() plus
    CatAnimation.cpp's "Global (<name>)" formatting."""
    if anim_id is None:
        anim_id = int(settings["bgAnimId"])
    name = gradient_name_for_ref(settings, map_ref(settings["bgAnimThemeMap"], anim_id))
    if name is not None:
        return name
    # No override, or one naming something this build cannot resolve: the row
    # names the global rather than saying "Default" and leaving the reader to
    # find out where the default lives.
    return "Global (%s)" % expected_global_gradient_text(settings)


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
# The gradient picker (gm-nov3.3)
#
# The three gradient rows are whole-row targets that push a two-level picker:
# a page of groups (Global, My gradients, the built-in categories) and, under
# each, the gradients in it. Choosing one returns all the way to Animation;
# the exit chevron pops one level and chooses nothing.


def depth(rig):
    return int(rig.settingsui_state().get("depth", 0))


def rows_across_pages(rig):
    """Every row name on the page currently on top, across all its pages, in
    page order. The picker's group and gradient lists run past five rows."""
    pages = int(rig.settingsui_state().get("pages", 1))
    names = []
    for page in range(pages):
        dump = goto_page(rig, page)
        for name in rig.rows_on_page(dump):
            if name not in names:
                names.append(name)
    return names


def open_picker(rig, row):
    """Taps one of the three gradient rows and waits for the picker's first
    page. Returns its dump."""
    dump = page_with_row(rig, row)
    target = rig.find_tag(dump, row, "action")
    if target is None:
        raise AssertionError("row %r is not a whole-row target" % row)
    before = depth(rig)
    rig.tap_target(target)
    rig.wait_until(lambda: depth(rig) == before + 1, timeout=5)
    return rig.touchmap(screen=0)


def picker_tap(rig, name):
    """Turns to the picker page carrying `name` and taps it. Returns the dump
    of whatever is on screen afterwards."""
    dump = page_with_row(rig, name)
    target = rig.find_tag(dump, name, "action")
    if target is None:
        raise AssertionError("no picker row %r on its page" % name)
    rig.tap_target(target)
    return rig.touchmap(screen=0)


def picker_choose(rig, group, name):
    """Opens a group and chooses one gradient in it, then waits for the
    picker to be gone (both levels: a choice returns to the category that
    opened it)."""
    before = depth(rig)
    picker_tap(rig, group)
    rig.wait_until(lambda: depth(rig) == before + 1, timeout=5)
    picker_tap(rig, name)
    rig.wait_until(lambda: depth(rig) == before - 1, timeout=5)


def picker_cancel(rig):
    """The exit chevron: pops one level without choosing."""
    dump = rig.touchmap(screen=0)
    target = rig.find_tag(dump, "exit", "exit")
    if target is None:
        raise AssertionError("no exit chevron on the picker page")
    before = depth(rig)
    rig.tap_target(target)
    rig.wait_until(lambda: depth(rig) == before - 1, timeout=5)


def picker_selected_rows(rig):
    """The picker rows whose marker dot is showing, across all pages of the
    page on top."""
    pages = int(rig.settingsui_state().get("pages", 1))
    marked = []
    for page in range(pages):
        dump = goto_page(rig, page)
        for name in rig.rows_on_page(dump):
            obj = rig.find_tag(dump, name, "selected")
            if obj is not None and not obj.get("h"):
                marked.append(name)
    return marked


def swatch_strip(rig, dump, row, data=None):
    """The row's swatch as a list of RGB triples, read straight out of the
    framebuffer along the middle of the canvas. None when the row has no
    visible swatch. `data` reuses a framebuffer already read."""
    obj = rig.find_tag(dump, row, "swatch")
    if obj is None or obj.get("h"):
        return None
    if data is None:
        data = rig.get_bytes("/api/debug/fb?step=1")
    y = (obj["y1"] + obj["y2"]) // 2
    return [rgb565_pixel(data, x, y) for x in range(obj["x1"], obj["x2"] + 1)]


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
    check(rig, "row_gradient", v["Gradient"] == exp_grad, "%r vs %r" % (v["Gradient"], exp_grad))
    exp_all = expected_global_gradient_text(s)
    check(rig, "row_gradient_all", v["Gradient all"] == exp_all, "%r vs %r" % (v["Gradient all"], exp_all))
    # The standby rows are live only while the standby animation is a
    # different one; the same id on both means one animation with one set of
    # parameters, and both rows say so (CatAnimation.cpp, refreshStandbyRows).
    standby = int(s["bgAnimStandbyId"])
    separate = 0 <= standby < len(ANIM_NAMES) and standby != int(s["bgAnimId"])
    if separate:
        check(rig, "row_standby_params", v["Standby params"] == ANIM_NAMES[standby],
              "%r vs bgAnimStandbyId=%s" % (v["Standby params"], s["bgAnimStandbyId"]))
        exp_sgrad = expected_gradient_text(s, standby)
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


def pick_ref(rig, row, settings, ref):
    """Chooses `ref` through the picker opened from `row`: "" taps Global on
    the first page, a decimal index opens its category, "cN" opens My
    gradients. Used both to make a choice and to put one back."""
    open_picker(rig, row)
    if ref == "":
        before = depth(rig)
        picker_tap(rig, "Global")
        rig.wait_until(lambda: depth(rig) == before - 1, timeout=5)
        return
    name = gradient_name_for_ref(settings, ref)
    if name is None:
        raise AssertionError("ref %r names nothing the picker can choose" % ref)
    group = "My gradients" if ref.startswith("c") else GRADIENT_CATEGORY_OF[int(ref)]
    picker_choose(rig, group, name)


def picker_choose_from_row(rig, row, group, name):
    """Opens the picker from one of the three entry rows and chooses one
    gradient out of one group, in one call."""
    open_picker(rig, row)
    picker_choose(rig, group, name)


def restore_fields_exactly(rig, name, fields):
    """Puts stored strings back byte for byte with one web save, for the
    restorations the picker has no route for: Gradient all cannot be set
    back to "no ref at all" because the picker only ever offers a concrete
    gradient, and clearing map slots through it leaves the separators
    behind (";;" and "" resolve the same but are not the same string).
    Checks the write landed, so a silent failure here is a failed run and
    not a venue the next scenario inherits in a different state."""
    if all(str(rig.settings().get(k, "")) == str(v) for k, v in fields.items()):
        return
    try:
        web_save(rig, fields)
        rig.wait_until(lambda: all(str(rig.settings().get(k, "")) == str(v) for k, v in fields.items()), timeout=5)
    except Exception as e:  # noqa: BLE001 -- reported by the check below
        rig.log("restore_web_save_failed", fields=fields, reason=str(e))
    now = rig.settings()
    check(rig, name, all(str(now.get(k, "")) == str(v) for k, v in fields.items()),
          "want %r got %r" % (fields, {k: now.get(k) for k in fields}))


def builtin_ref_other_than(ref, category=None):
    """A built-in ref that is not `ref`, from `category` when one is named."""
    for idx, _name in enumerate(THEME_NAMES):
        if str(idx) == str(ref):
            continue
        if category is not None and GRADIENT_CATEGORY_OF[idx] != category:
            continue
        return str(idx)
    raise AssertionError("no built-in other than %r in %r" % (ref, category))


def check_gradient_picker_navigation(rig):
    """Acceptance: the three gradient rows are whole-row targets that push
    the picker; Global comes first on a per-animation picker and names the
    current global; choosing a built-in or a saved gradient writes that
    exact ref into the current animation's map slot and nothing else;
    choosing Global clears that slot. Replaces the prev/next arrow walk
    this check used to do (gm-nov3.3): the arrows are gone, and the map
    isolation and live-write assertions they carried are kept here."""
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    map0 = s0["bgAnimThemeMap"]
    ref0 = map_ref(map0, anim0)
    lib = library_entries(s0["bgAnimGradients"])
    check(rig, "picker_fixture_has_library", len(lib) >= 1, "bgAnimGradients has %d entries" % len(lib))

    open_animation(rig)
    d = page_with_row(rig, "Gradient")
    check(rig, "gradient_row_has_no_arrows",
          rig.find_tag(d, "Gradient", "next") is None and rig.find_tag(d, "Gradient", "prev") is None,
          "the row must be a whole-row target, not arrows under a cover")

    open_picker(rig, "Gradient")
    st = rig.settingsui_state()
    check(rig, "picker_pushed_one_level", st.get("depth") == 2, st.get("depth"))
    check(rig, "picker_title_is_the_row", st.get("title") == "Gradient", st.get("title"))

    groups = rows_across_pages(rig)
    check(rig, "picker_global_first", groups[:1] == ["Global"], groups[:3])
    d0 = goto_page(rig, 0)
    check(rig, "picker_global_names_the_global",
          rig.row_value(d0, "Global") == expected_global_gradient_text(s0),
          "got %r want %r" % (rig.row_value(d0, "Global"), expected_global_gradient_text(s0)))
    check(rig, "picker_library_group_present", "My gradients" in groups, groups)
    wanted_cats = [c for c in GRADIENT_CATEGORIES if c in GRADIENT_CATEGORY_OF]
    check(rig, "picker_lists_every_category", [g for g in groups if g in GRADIENT_CATEGORIES] == wanted_cats,
          "got %r want %r" % ([g for g in groups if g in GRADIENT_CATEGORIES], wanted_cats))
    counts_ok = True
    for cat in wanted_cats:
        dump = page_with_row(rig, cat)
        n = sum(1 for c in GRADIENT_CATEGORY_OF if c == cat)
        want = "%d gradient%s" % (n, "" if n == 1 else "s")
        if rig.row_value(dump, cat) != want:
            counts_ok = False
            rig.log("picker_group_count_mismatch", group=cat, got=rig.row_value(dump, cat), want=want)
    check(rig, "picker_group_counts", counts_ok)

    strip = swatch_strip(rig, goto_page(rig, 0), "Global")
    check(rig, "picker_row_draws_a_ramp", strip is not None and len(set(strip)) > 4,
          "distinct colours across the swatch: %r" % (None if strip is None else len(set(strip))))

    # A built-in, from a category that is not the first row on the page.
    want_ref = builtin_ref_other_than(ref0, category="Fire and Heat")
    want_name = THEME_NAMES[int(want_ref)]
    picker_choose(rig, "Fire and Heat", want_name)
    check(rig, "picker_returns_to_animation", depth(rig) == 1, depth(rig))
    d = page_with_row(rig, "Gradient")
    check(rig, "picker_builtin_row_text", rig.row_value(d, "Gradient") == want_name, rig.row_value(d, "Gradient"))
    map1 = rig.settings()["bgAnimThemeMap"]
    check(rig, "picker_builtin_writes_exact_ref", map_ref(map1, anim0) == want_ref,
          "got %r want %r" % (map_ref(map1, anim0), want_ref))
    others = all(map_ref(map1, i) == map_ref(map0, i) for i in range(len(ANIM_NAMES)) if i != anim0)
    check(rig, "picker_writes_only_current_anim", others)

    # Reopening marks what is in force, in its own group and nowhere else.
    open_picker(rig, "Gradient")
    picker_tap(rig, "Fire and Heat")
    rig.wait_until(lambda: depth(rig) == 3, timeout=5)
    marked = picker_selected_rows(rig)
    check(rig, "picker_marks_current", marked == [want_name], marked)
    d = page_with_row(rig, want_name)
    check(rig, "picker_marks_current_value_text", rig.row_value(d, want_name) == "Selected",
          rig.row_value(d, want_name))
    picker_cancel(rig)
    check(rig, "picker_chevron_pops_one_level", depth(rig) == 2, depth(rig))
    picker_cancel(rig)
    check(rig, "picker_chevron_leaves_picker", depth(rig) == 1, depth(rig))
    check(rig, "picker_cancel_changes_nothing", rig.settings()["bgAnimThemeMap"] == map1,
          rig.settings()["bgAnimThemeMap"])

    # A saved gradient: the same picker, a "cN" ref, and no legacy mirror.
    lib_id, lib_name, _g = lib[0]
    theme_before = rig.settings()["bgAnimTheme"]
    picker_choose_from_row(rig, "Gradient", "My gradients", lib_name)
    d = page_with_row(rig, "Gradient")
    check(rig, "picker_library_row_text", rig.row_value(d, "Gradient") == lib_name, rig.row_value(d, "Gradient"))
    s2 = rig.settings()
    check(rig, "picker_library_writes_cref", map_ref(s2["bgAnimThemeMap"], anim0) == "c%d" % lib_id,
          map_ref(s2["bgAnimThemeMap"], anim0))
    check(rig, "picker_library_does_not_mirror_legacy", s2["bgAnimTheme"] == theme_before,
          "bgAnimTheme %r -> %r" % (theme_before, s2["bgAnimTheme"]))

    # Global clears the slot and returns from the first page, with no second tap.
    open_picker(rig, "Gradient")
    picker_tap(rig, "Global")
    rig.wait_until(lambda: depth(rig) == 1, timeout=5)
    s3 = rig.settings()
    check(rig, "picker_global_clears_slot", map_ref(s3["bgAnimThemeMap"], anim0) == "",
          map_ref(s3["bgAnimThemeMap"], anim0))
    d = page_with_row(rig, "Gradient")
    check(rig, "picker_global_row_text", rig.row_value(d, "Gradient") == expected_gradient_text(s3),
          "got %r want %r" % (rig.row_value(d, "Gradient"), expected_gradient_text(s3)))
    check(rig, "picker_global_clears_only_this_slot",
          all(map_ref(s3["bgAnimThemeMap"], i) == map_ref(map0, i) for i in range(len(ANIM_NAMES)) if i != anim0))

    if ref0 != "":
        pick_ref(rig, "Gradient", s0, ref0)
    close_animation(rig)
    final_ref = map_ref(rig.settings()["bgAnimThemeMap"], anim0)
    check(rig, "picker_navigation_restored", final_ref == ref0, "got %r want %r" % (final_ref, ref0))


def check_gradient_all_and_standby_pickers(rig):
    """Acceptance: Gradient all uses the same picker but offers no Global
    and writes bgAnimGradientRef (mirroring bgAnimTheme for a built-in and
    leaving it alone for a saved gradient, gm-nov3.7); Standby grad is
    disabled while the standby screen follows the main animation, shows
    "Same as main", and changes nothing when tapped; with a separate
    standby animation it edits that animation's slot, not the main one."""
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    ref0 = s0["bgAnimGradientRef"]
    theme0 = s0["bgAnimTheme"]
    standby0 = int(s0["bgAnimStandbyId"])
    map0 = s0["bgAnimThemeMap"]
    lib = library_entries(s0["bgAnimGradients"])

    open_animation(rig)
    open_picker(rig, "Gradient all")
    st = rig.settingsui_state()
    check(rig, "all_picker_title", st.get("title") == "Gradient all", st.get("title"))
    groups = rows_across_pages(rig)
    check(rig, "all_picker_has_no_global", "Global" not in groups, groups)

    want_ref = builtin_ref_other_than(ref0, category="Water and Ice")
    want_name = THEME_NAMES[int(want_ref)]
    picker_choose(rig, "Water and Ice", want_name)
    s1 = rig.settings()
    check(rig, "all_picker_writes_global_ref", s1["bgAnimGradientRef"] == want_ref, s1["bgAnimGradientRef"])
    check(rig, "all_picker_mirrors_builtin", str(s1["bgAnimTheme"]) == want_ref,
          "bgAnimTheme %r want %r" % (s1["bgAnimTheme"], want_ref))
    d = page_with_row(rig, "Gradient all")
    check(rig, "all_picker_row_text", rig.row_value(d, "Gradient all") == want_name,
          rig.row_value(d, "Gradient all"))

    if lib:
        lib_id, lib_name, _g = lib[0]
        mirror_before = rig.settings()["bgAnimTheme"]
        picker_choose_from_row(rig, "Gradient all", "My gradients", lib_name)
        s2 = rig.settings()
        check(rig, "all_picker_library_ref", s2["bgAnimGradientRef"] == "c%d" % lib_id, s2["bgAnimGradientRef"])
        check(rig, "all_picker_library_no_mirror", s2["bgAnimTheme"] == mirror_before,
              "bgAnimTheme %r -> %r" % (mirror_before, s2["bgAnimTheme"]))

    # Standby grad while the standby animation is the main one. Set from
    # outside the visit, because the row reads the draft and the point here
    # is the stored state, not a value this visit stepped.
    close_animation(rig)
    restore_fields_exactly(rig, "standby_same_as_main_setup", {"bgAnimStandbyId": anim0})
    open_animation(rig)
    d = page_with_row(rig, "Standby grad")
    check(rig, "standby_grad_same_id_text", rig.row_value(d, "Standby grad") == "Same as main",
          rig.row_value(d, "Standby grad"))
    before_same = rig.settings()["bgAnimThemeMap"]
    same_target = rig.find_tag(d, "Standby grad", "action")
    if same_target is not None:
        rig.tap_target(same_target)
    check(rig, "standby_grad_same_id_no_push", depth(rig) == 1, depth(rig))
    check(rig, "standby_grad_same_id_no_write", rig.settings()["bgAnimThemeMap"] == before_same)
    close_animation(rig)
    restore_fields_exactly(rig, "standby_same_as_main_cleared", {"bgAnimStandbyId": standby0})
    open_animation(rig)

    # Standby grad while the standby screen follows the main animation.
    if standby0 < 0:
        d = page_with_row(rig, "Standby grad")
        check(rig, "standby_grad_same_as_main_text", rig.row_value(d, "Standby grad") == "Same as main",
              rig.row_value(d, "Standby grad"))
        before = rig.settings()["bgAnimThemeMap"]
        target = rig.find_tag(d, "Standby grad", "action")
        if target is not None:
            rig.tap_target(target)
        check(rig, "standby_grad_disabled_no_push", depth(rig) == 1, depth(rig))
        check(rig, "standby_grad_disabled_no_write", rig.settings()["bgAnimThemeMap"] == before)

    # A separate standby animation: the row edits that animation's slot.
    standby_target = (anim0 + 1) % len(ANIM_NAMES)
    steps = 0
    while int(rig.settings()["bgAnimStandbyId"]) != standby_target and steps < len(ANIM_NAMES) + 2:
        tap_row(rig, "Standby anim", "next")
        steps += 1
    separate = int(rig.settings()["bgAnimStandbyId"]) == standby_target
    check(rig, "standby_anim_set_for_picker", separate, rig.settings()["bgAnimStandbyId"])
    if separate:
        want2 = builtin_ref_other_than(map_ref(rig.settings()["bgAnimThemeMap"], standby_target), category="Nature")
        picker_choose_from_row(rig, "Standby grad", "Nature", THEME_NAMES[int(want2)])
        s3 = rig.settings()
        check(rig, "standby_grad_writes_standby_slot", map_ref(s3["bgAnimThemeMap"], standby_target) == want2,
              map_ref(s3["bgAnimThemeMap"], standby_target))
        check(rig, "standby_grad_leaves_main_slot", map_ref(s3["bgAnimThemeMap"], anim0) == map_ref(map0, anim0),
              map_ref(s3["bgAnimThemeMap"], anim0))
        d = page_with_row(rig, "Standby grad")
        check(rig, "standby_grad_row_text", rig.row_value(d, "Standby grad") == THEME_NAMES[int(want2)],
              rig.row_value(d, "Standby grad"))
        # Put the standby slot and the standby animation back.
        pick_ref(rig, "Standby grad", s3, map_ref(map0, standby_target))
        steps = 0
        while int(rig.settings()["bgAnimStandbyId"]) != standby0 and steps < len(ANIM_NAMES) + 2:
            tap_row(rig, "Standby anim", "prev")
            steps += 1

    pick_ref(rig, "Gradient all", s0, ref0 if ref0 != "" else str(theme0))
    close_animation(rig)
    s9 = rig.settings()
    check(rig, "all_and_standby_restored",
          map_ref(s9["bgAnimThemeMap"], anim0) == map_ref(map0, anim0) and int(s9["bgAnimStandbyId"]) == standby0,
          "map=%r standby=%r" % (s9["bgAnimThemeMap"], s9["bgAnimStandbyId"]))
    restore_fields_exactly(rig, "all_picker_global_ref_restored",
                           {"bgAnimGradientRef": ref0, "bgAnimTheme": theme0, "bgAnimThemeMap": map0})


def library_renumbered(lib, new_ids):
    """The stored library with its last entries renumbered to `new_ids`, in
    order, plus the (id, name, gradient) triples that came out.

    How many entries a library may hold (twelve) is not what bounds the ids
    they carry: the web allocates a new entry as the largest existing id plus
    one, so a few rounds of copying and deleting leave a small library whose
    ids are large. That is the state this builds, without adding an entry."""
    entries = library_entries(lib)
    if len(entries) < len(new_ids):
        raise AssertionError("the stored library has %d entries, %d are needed" % (len(entries), len(new_ids)))
    head = entries[:len(entries) - len(new_ids)]
    kept = [e[0] for e in head]
    clash = [i for i in new_ids if i in kept]
    if clash:
        raise AssertionError("ids %r are already in the library" % clash)
    tail = [(new_id, name, gradient) for new_id, (_old, name, gradient) in zip(new_ids, entries[len(head):])]
    return ";".join("%d|%s|%s" % e for e in head + tail), tail


def check_gradient_picker_high_library_ids(rig):
    """Regression for gm-nov3.15: a saved gradient whose id is above 13 can
    be chosen through all three gradient rows, and the choice lands as the
    exact ref.

    The picker validates a tap by re-resolving the ref (CatGradientPicker.cpp,
    applyPick), and the resolver behind it refused every library id above
    BG_GRADIENT_LIB_MAX + 1, which confused the library's capacity with the
    range of the ids in it. Such an entry was listed in My gradients with no
    swatch, and tapping it closed the picker without writing anything.

    The library is set up through the web save the simulator provides, so
    this reports and returns on a device."""
    s0 = rig.settings()
    lib0 = s0["bgAnimGradients"]
    map0 = s0["bgAnimThemeMap"]
    ref0 = s0["bgAnimGradientRef"]
    theme0 = s0["bgAnimTheme"]
    standby0 = int(s0["bgAnimStandbyId"])
    anim0 = int(s0["bgAnimId"])

    # 14 is the first id the old resolver refused; 99999 is the largest the
    # ref grammar accepts.
    high_lib, high = library_renumbered(lib0, [14, 99999])
    try:
        web_save(rig, {"bgAnimGradients": high_lib})
    except RuntimeError as e:
        rig.log("high_library_ids_skipped", reason=str(e))
        return
    landed = rig.wait_until(lambda: rig.settings()["bgAnimGradients"] == high_lib, timeout=5)
    check(rig, "high_id_library_write_landed", bool(landed), rig.settings()["bgAnimGradients"])
    if not landed:
        restore_fields_exactly(rig, "high_id_library_restored", {"bgAnimGradients": lib0})
        return
    high_id, high_name, _hg = high[0]
    top_id, top_name, _tg = high[1]

    # 1. The per-animation row. The swatch is read inside the picker as well
    #    as on the row, because a refused ref showed there first: the entry
    #    was listed with the canvas still hidden.
    open_animation(rig)
    theme_before = rig.settings()["bgAnimTheme"]
    open_picker(rig, "Gradient")
    picker_tap(rig, "My gradients")
    rig.wait_until(lambda: depth(rig) == 3, timeout=5)
    d = page_with_row(rig, high_name)
    picker_strip = swatch_strip(rig, d, high_name)
    check(rig, "high_id_picker_row_draws_a_ramp", picker_strip is not None and len(set(picker_strip)) > 4,
          "distinct colours across the swatch: %r" % (None if picker_strip is None else len(set(picker_strip))))
    picker_tap(rig, high_name)
    chosen = rig.wait_until(lambda: depth(rig) == 1, timeout=5)
    check(rig, "high_id_pick_returns_to_animation", bool(chosen), depth(rig))
    s1 = rig.settings()
    check(rig, "high_id_writes_exact_ref", map_ref(s1["bgAnimThemeMap"], anim0) == "c%d" % high_id,
          "got %r want %r" % (map_ref(s1["bgAnimThemeMap"], anim0), "c%d" % high_id))
    check(rig, "high_id_writes_only_current_anim",
          all(map_ref(s1["bgAnimThemeMap"], i) == map_ref(map0, i) for i in range(len(ANIM_NAMES)) if i != anim0))
    check(rig, "high_id_no_legacy_mirror", s1["bgAnimTheme"] == theme_before,
          "bgAnimTheme %r -> %r" % (theme_before, s1["bgAnimTheme"]))
    d = page_with_row(rig, "Gradient")
    check(rig, "high_id_row_text", rig.row_value(d, "Gradient") == high_name, rig.row_value(d, "Gradient"))
    row_strip = swatch_strip(rig, d, "Gradient")
    check(rig, "high_id_row_draws_the_chosen_ramp", row_strip is not None and row_strip == picker_strip,
          "row %r picker %r" % (None if row_strip is None else row_strip[:4],
                                None if picker_strip is None else picker_strip[:4]))

    # 2. Gradient all, at the top of the id range. A saved gradient is not
    #    mirrored into bgAnimTheme, which is the rollback policy for a ref
    #    no older build could read (gm-nov3.7).
    theme_before = rig.settings()["bgAnimTheme"]
    picker_choose_from_row(rig, "Gradient all", "My gradients", top_name)
    s2 = rig.settings()
    check(rig, "high_id_all_writes_exact_ref", s2["bgAnimGradientRef"] == "c%d" % top_id, s2["bgAnimGradientRef"])
    check(rig, "high_id_all_no_legacy_mirror", s2["bgAnimTheme"] == theme_before,
          "bgAnimTheme %r -> %r" % (theme_before, s2["bgAnimTheme"]))
    d = page_with_row(rig, "Gradient all")
    check(rig, "high_id_all_row_text", rig.row_value(d, "Gradient all") == top_name,
          rig.row_value(d, "Gradient all"))
    all_strip = swatch_strip(rig, d, "Gradient all")
    check(rig, "high_id_all_row_draws_a_ramp", all_strip is not None and len(set(all_strip)) > 4,
          "distinct colours across the swatch: %r" % (None if all_strip is None else len(set(all_strip))))

    # 3. Standby grad, which edits the standby animation's own slot. The
    #    standby animation is set from outside the visit for the reason the
    #    web-interference check gives: a field this visit stepped keeps its
    #    draft against a web save.
    close_animation(rig)
    standby_target = (anim0 + 1) % len(ANIM_NAMES)
    web_save(rig, {"bgAnimStandbyId": standby_target})
    ready = rig.wait_until(lambda: int(rig.settings()["bgAnimStandbyId"]) == standby_target, timeout=5)
    check(rig, "high_id_standby_setup_landed", bool(ready), rig.settings()["bgAnimStandbyId"])
    if ready:
        open_animation(rig)
        picker_choose_from_row(rig, "Standby grad", "My gradients", top_name)
        s3 = rig.settings()
        check(rig, "high_id_standby_writes_standby_slot",
              map_ref(s3["bgAnimThemeMap"], standby_target) == "c%d" % top_id,
              map_ref(s3["bgAnimThemeMap"], standby_target))
        check(rig, "high_id_standby_leaves_main_slot",
              map_ref(s3["bgAnimThemeMap"], anim0) == "c%d" % high_id, map_ref(s3["bgAnimThemeMap"], anim0))
        d = page_with_row(rig, "Standby grad")
        check(rig, "high_id_standby_row_text", rig.row_value(d, "Standby grad") == top_name,
              rig.row_value(d, "Standby grad"))
        standby_strip = swatch_strip(rig, d, "Standby grad")
        check(rig, "high_id_standby_row_draws_a_ramp", standby_strip is not None and len(set(standby_strip)) > 4,
              "distinct colours across the swatch: %r" % (None if standby_strip is None else len(set(standby_strip))))
        close_animation(rig)

    restore_fields_exactly(rig, "high_id_restored", {
        "bgAnimGradients": lib0,
        "bgAnimThemeMap": map0,
        "bgAnimGradientRef": ref0,
        "bgAnimTheme": theme0,
        "bgAnimStandbyId": standby0,
    })


def check_gradient_picker_reachability(rig):
    """Acceptance: every built-in is reachable through its category,
    including the last partial page; every library entry is reachable; an
    empty library hides My gradients. The empty-library half goes through a
    real web save and asserts the write landed first, so it cannot pass by
    the POST being rejected."""
    s0 = rig.settings()
    lib0 = s0["bgAnimGradients"]
    lib = library_entries(lib0)

    open_animation(rig)
    open_picker(rig, "Gradient all")
    seen = []
    for group in rows_across_pages(rig):
        picker_tap(rig, group)
        rig.wait_until(lambda: depth(rig) == 3, timeout=5)
        names = rows_across_pages(rig)
        if group == "My gradients":
            want = [name for _id, name, _g in lib]
            check(rig, "picker_library_all_reachable", names == want, "got %r want %r" % (names, want))
        else:
            seen.extend(names)
            want = [n for i, n in enumerate(THEME_NAMES) if GRADIENT_CATEGORY_OF[i] == group]
            check(rig, "picker_group_%s_complete" % group.replace(" ", "_"), names == want,
                  "got %r want %r" % (names, want))
        picker_cancel(rig)
    check(rig, "picker_every_builtin_reachable", sorted(seen) == sorted(THEME_NAMES),
          "missing %r extra %r" % (sorted(set(THEME_NAMES) - set(seen)), sorted(set(seen) - set(THEME_NAMES))))
    picker_cancel(rig)

    # An empty library hides the group. The POST has to land for this to
    # mean anything, so read it back before believing the UI.
    try:
        web_save(rig, {"bgAnimGradients": ""})
    except RuntimeError as e:
        rig.log("picker_empty_library_skipped", reason=str(e))
        close_animation(rig)
        return
    landed = rig.wait_until(lambda: rig.settings()["bgAnimGradients"] == "", timeout=5)
    check(rig, "picker_empty_library_write_landed", bool(landed), rig.settings()["bgAnimGradients"])
    open_picker(rig, "Gradient all")
    groups = rows_across_pages(rig)
    check(rig, "picker_empty_library_hides_group", "My gradients" not in groups, groups)
    picker_cancel(rig)

    web_save(rig, {"bgAnimGradients": lib0})
    back = rig.wait_until(lambda: rig.settings()["bgAnimGradients"] == lib0, timeout=5)
    check(rig, "picker_library_restored", bool(back))
    close_animation(rig)


def check_gradient_picker_pagination_and_cancel(rig):
    """Acceptance: header arrows and horizontal swipes paginate inside a
    picker page, they neither select nor leave; the chevron cancels one
    level; and a visit that chooses nothing leaves every gradient setting
    byte for byte as it was."""
    s0 = rig.settings()
    watched = ANIMATION_FIELDS + ["bgAnimGradientRef", "bgAnimTheme", "bgAnimCustomTheme"]
    before = {k: s0[k] for k in watched if k in s0}

    open_animation(rig)
    open_picker(rig, "Gradient")
    st = rig.settingsui_state()
    check(rig, "picker_paginates_at_all", int(st.get("pages", 1)) > 1, st.get("pages"))
    d = goto_page(rig, 0)
    arrow = rig.find_tag(d, "page_next", "page_next")
    check(rig, "picker_has_page_arrows", arrow is not None)
    if arrow is not None:
        rig.tap_target(arrow)
        ok = rig.wait_until(lambda: rig.settingsui_state().get("page") == 1, timeout=5)
        check(rig, "picker_arrow_turns_page", bool(ok), rig.settingsui_state().get("page"))
        check(rig, "picker_arrow_stays_in_picker", depth(rig) == 2, depth(rig))

    # A swipe across a row paginates and must not select the row it began on.
    d = goto_page(rig, 1)
    rows = rig.rows_on_page(d)
    hit = rig.find_tag(d, rows[0], "action")["hit"]
    y = (hit[1] + hit[3]) // 2
    rig.swipe(hit[0] + 40, y, hit[0] + 40 + 160, y)
    ok = rig.wait_until(lambda: rig.settingsui_state().get("page") == 0, timeout=5)
    check(rig, "picker_swipe_turns_page", bool(ok), rig.settingsui_state().get("page"))
    check(rig, "picker_swipe_does_not_select", depth(rig) == 2, depth(rig))

    # Into a group, then back out one level at a time, choosing nothing.
    picker_tap(rig, "My gradients")
    rig.wait_until(lambda: depth(rig) == 3, timeout=5)
    check(rig, "picker_group_paginates", int(rig.settingsui_state().get("pages", 1)) > 1,
          rig.settingsui_state().get("pages"))
    d = goto_page(rig, 1)
    rows = rig.rows_on_page(d)
    hit = rig.find_tag(d, rows[0], "action")["hit"]
    y = (hit[1] + hit[3]) // 2
    rig.swipe(hit[0] + 40, y, hit[0] + 40 + 160, y)
    rig.wait_until(lambda: rig.settingsui_state().get("page") != 1, timeout=5)
    check(rig, "picker_group_swipe_does_not_select", depth(rig) == 3, depth(rig))
    picker_cancel(rig)
    check(rig, "picker_group_chevron_pops_one", depth(rig) == 2, depth(rig))
    picker_cancel(rig)
    check(rig, "picker_first_chevron_pops_one", depth(rig) == 1, depth(rig))
    close_animation(rig)

    after = rig.settings()
    changed = {k: (before[k], after[k]) for k in before if str(before[k]) != str(after[k])}
    check(rig, "picker_no_choice_changes_nothing", not changed, changed)


def check_gradient_picker_web_interference(rig):
    """Acceptance: a web save while each picker level is open reaches the
    parent's draft, the picker rebuilds on what is stored now, a deleted
    library entry cannot be selected, and a web change of the edited
    animation does not retarget an open picker. Every step reads the POST
    back before judging the UI, so nothing here can pass by the write
    being rejected."""
    s0 = rig.settings()
    anim_a = int(s0["bgAnimId"])
    anim_b = (anim_a + 1) % len(ANIM_NAMES)
    map0 = s0["bgAnimThemeMap"]
    lib0 = s0["bgAnimGradients"]
    fps0 = int(s0["bgAnimFps"])
    standby0 = int(s0["bgAnimStandbyId"])
    lib = library_entries(lib0)

    open_animation(rig)
    open_picker(rig, "Gradient")

    # 1. An untouched parent field changes under an open picker.
    new_fps = 45 if fps0 != 45 else 50
    try:
        web_save(rig, {"bgAnimFps": new_fps})
    except RuntimeError as e:
        rig.log("picker_web_interference_skipped", reason=str(e))
        close_animation(rig)
        return
    landed = rig.wait_until(lambda: int(rig.settings()["bgAnimFps"]) == new_fps, timeout=5)
    check(rig, "picker_web_fps_landed", bool(landed), rig.settings()["bgAnimFps"])
    picker_cancel(rig)
    d = page_with_row(rig, "Frame rate")
    check(rig, "picker_parent_draft_reconciled", rig.row_value(d, "Frame rate") == "%d fps" % new_fps,
          "got %r want %r" % (rig.row_value(d, "Frame rate"), "%d fps" % new_fps))

    # 2. The displayed library entry is deleted while its page is open.
    if len(lib) >= 2:
        lib_id, lib_name, _g = lib[0]
        open_picker(rig, "Gradient")
        picker_tap(rig, "My gradients")
        rig.wait_until(lambda: depth(rig) == 3, timeout=5)
        check(rig, "picker_deleted_entry_listed_first", lib_name in rows_across_pages(rig))
        remaining = ";".join("%d|%s|%s" % e for e in lib[1:])
        # The same save also moves an untouched parent field, so the second
        # child level's reconcile is checked as well as the first one's.
        deeper_fps = 35 if new_fps != 35 else 40
        web_save(rig, {"bgAnimGradients": remaining, "bgAnimFps": deeper_fps})
        landed = rig.wait_until(
            lambda: rig.settings()["bgAnimGradients"] == remaining and int(rig.settings()["bgAnimFps"]) == deeper_fps,
            timeout=5)
        check(rig, "picker_delete_write_landed", bool(landed))
        gone = rig.wait_until(lambda: lib_name not in rows_across_pages(rig), timeout=6)
        check(rig, "picker_drops_deleted_entry", bool(gone), rows_across_pages(rig))
        # Choosing whatever took its place must write that entry's ref, not
        # the deleted one's.
        names = rows_across_pages(rig)
        picker_tap(rig, names[0])
        rig.wait_until(lambda: depth(rig) == 1, timeout=5)
        chosen = map_ref(rig.settings()["bgAnimThemeMap"], anim_a)
        check(rig, "picker_never_writes_deleted_ref", chosen != "c%d" % lib_id, chosen)
        wanted = next("c%d" % e[0] for e in lib[1:] if e[1] == names[0])
        check(rig, "picker_writes_surviving_ref", chosen == wanted, "got %r want %r" % (chosen, wanted))
        d = page_with_row(rig, "Frame rate")
        check(rig, "picker_second_level_reconciles_parent",
              rig.row_value(d, "Frame rate") == "%d fps" % deeper_fps,
              "got %r want %r" % (rig.row_value(d, "Frame rate"), "%d fps" % deeper_fps))
        web_save(rig, {"bgAnimGradients": lib0})
        check(rig, "picker_library_restored_after_delete",
              bool(rig.wait_until(lambda: rig.settings()["bgAnimGradients"] == lib0, timeout=5)))

    # 3. The edited animation changes under an open picker: the picker keeps
    #    the slot it captured.
    open_picker(rig, "Gradient")
    web_save(rig, {"bgAnimId": anim_b})
    landed = rig.wait_until(lambda: int(rig.settings()["bgAnimId"]) == anim_b, timeout=5)
    check(rig, "picker_web_anim_change_landed", bool(landed), rig.settings()["bgAnimId"])
    want_ref = builtin_ref_other_than(map_ref(rig.settings()["bgAnimThemeMap"], anim_a), category="Night Sky")
    picker_choose(rig, "Night Sky", THEME_NAMES[int(want_ref)])
    s1 = rig.settings()
    check(rig, "picker_writes_captured_slot", map_ref(s1["bgAnimThemeMap"], anim_a) == want_ref,
          "anim %d got %r want %r" % (anim_a, map_ref(s1["bgAnimThemeMap"], anim_a), want_ref))
    check(rig, "picker_leaves_other_slot", map_ref(s1["bgAnimThemeMap"], anim_b) == map_ref(map0, anim_b),
          "anim %d got %r" % (anim_b, map_ref(s1["bgAnimThemeMap"], anim_b)))

    # 4. The standby animation is turned off while its own picker is open:
    #    the picker closes rather than editing a slot nothing reads. The
    #    standby animation is set before the visit, not stepped inside it:
    #    a field this visit touched keeps its draft against a web save, by
    #    the same rule the precedence check covers, so a stepped Standby
    #    anim row would (correctly) keep the picker open.
    close_animation(rig)
    standby_target = (anim_b + 1) % len(ANIM_NAMES)
    web_save(rig, {"bgAnimStandbyId": standby_target})
    ready = rig.wait_until(lambda: int(rig.settings()["bgAnimStandbyId"]) == standby_target, timeout=5)
    check(rig, "picker_standby_setup_landed", bool(ready), rig.settings()["bgAnimStandbyId"])
    open_animation(rig)
    open_picker(rig, "Standby grad")
    web_save(rig, {"bgAnimStandbyId": -1})
    landed = rig.wait_until(lambda: int(rig.settings()["bgAnimStandbyId"]) == -1, timeout=5)
    check(rig, "picker_standby_off_landed", bool(landed), rig.settings()["bgAnimStandbyId"])
    closed = rig.wait_until(lambda: depth(rig) == 1, timeout=8)
    check(rig, "picker_closes_when_slot_invalid", bool(closed), depth(rig))
    check(rig, "picker_invalid_slot_wrote_nothing",
          map_ref(rig.settings()["bgAnimThemeMap"], standby_target) == map_ref(map0, standby_target),
          map_ref(rig.settings()["bgAnimThemeMap"], standby_target))

    close_animation(rig)
    web_save(rig, {"bgAnimThemeMap": map0, "bgAnimId": anim_a, "bgAnimFps": fps0, "bgAnimStandbyId": standby0})
    restored = rig.wait_until(
        lambda: rig.settings()["bgAnimThemeMap"] == map0 and int(rig.settings()["bgAnimId"]) == anim_a and
        int(rig.settings()["bgAnimFps"]) == fps0 and int(rig.settings()["bgAnimStandbyId"]) == standby0,
        timeout=5)
    s9 = rig.settings()
    check(rig, "picker_web_interference_restored", bool(restored),
          "map=%r anim=%r fps=%r standby=%r" %
          (s9["bgAnimThemeMap"], s9["bgAnimId"], s9["bgAnimFps"], s9["bgAnimStandbyId"]))


def check_gradient_picker_teardown(rig):
    """Acceptance: a route out of the category that is not the chevron tears
    the whole cover down from under an open picker, commits what the visit
    touched, and leaves nothing stale behind.

    The real triggers named in the bead, a brew start and a standby timeout,
    are not reachable from this simulator (test_temps.py's
    check_forced_external_leave has the reasons). close=1 is the same code:
    SettingsUI::close() and SettingsUI::onExternalLeave() both just call
    teardownAll(). So this covers the teardown and leaves the triggers
    themselves to the device."""
    s0 = rig.settings()
    anim0 = int(s0["bgAnimId"])
    map0 = s0["bgAnimThemeMap"]
    ref0 = map_ref(map0, anim0)
    want_ref = builtin_ref_other_than(ref0, category="Coffee")

    open_animation(rig)
    picker_choose_from_row(rig, "Gradient", "Coffee", THEME_NAMES[int(want_ref)])
    open_picker(rig, "Gradient")
    picker_tap(rig, "Water and Ice")
    rig.wait_until(lambda: depth(rig) == 3, timeout=5)

    rig.settingsui(close=1)
    closed = rig.wait_until(lambda: rig.settingsui_state().get("open") is False, timeout=5)
    check(rig, "picker_teardown_closes_shell", bool(closed), rig.settingsui_state())
    check(rig, "picker_teardown_commits_touched_slot",
          map_ref(rig.settings()["bgAnimThemeMap"], anim0) == want_ref,
          map_ref(rig.settings()["bgAnimThemeMap"], anim0))

    # Nothing stale: the shell opens again on the tile page and the category
    # rebuilds with the value the teardown committed.
    open_animation(rig)
    st = rig.settingsui_state()
    check(rig, "picker_teardown_reopens_clean", st.get("depth") == 1 and st.get("category") == 2, st)
    d = page_with_row(rig, "Gradient")
    check(rig, "picker_teardown_row_shows_committed", rig.row_value(d, "Gradient") == THEME_NAMES[int(want_ref)],
          rig.row_value(d, "Gradient"))
    if ref0 != "":
        pick_ref(rig, "Gradient", s0, ref0)
    close_animation(rig)
    restore_fields_exactly(rig, "picker_teardown_restored", {"bgAnimThemeMap": map0})


def check_gradient_picker_audit(rig):
    """Acceptance: Rig.audit passes on every picker page shape, with the
    full library loaded, and no exemption is added beyond the exit
    chevron's existing size and edge one."""
    open_animation(rig)
    for row in ("Gradient all", "Gradient"):
        tag = "all" if "all" in row else "anim"
        open_picker(rig, row)
        for page in range(int(rig.settingsui_state().get("pages", 1))):
            dump = goto_page(rig, page)
            violations = rig.audit(dump)["violations"]
            check(rig, "picker_audit_%s_page%d" % (tag, page), not violations, violations)
        # The library and the longest category: the longest labels there are.
        for group in ("My gradients", "Water and Ice"):
            picker_tap(rig, group)
            rig.wait_until(lambda: depth(rig) == 3, timeout=5)
            for page in range(int(rig.settingsui_state().get("pages", 1))):
                dump = goto_page(rig, page)
                violations = rig.audit(dump)["violations"]
                check(rig, "picker_audit_%s_%s_page%d" % (tag, group.split()[0].lower(), page),
                      not violations, violations)
            picker_cancel(rig)
        picker_cancel(rig)
    close_animation(rig)


def check_gradient_precedence_across_animations(rig):
    """Regression for the gm-flw.9 review on eed10a33: CatAnimation.cpp
    originally tracked only the most recently touched animation's gradient
    edit (a single gradientTouchedAnimId/gradientLastRef pair). Choose a
    gradient for animation A, switch to B, choose one for B too: both are
    touched this visit, not just B, and commit() must write both slots.

    The review's own interference step runs here now (gm-nov3.3): the
    simulator links the real BgAnimThemes.cpp, so bg_map_valid accepts a
    well-formed map and a web save really does replace the whole string
    while both slots are touched. What this check used to assert was the
    opposite, that the POST bounced off the host stub, which meant the
    step could not fail. The rule under test is the per-field one: a slot
    this visit touched keeps the display's value at commit, and a slot it
    did not touch keeps the web's.

    The rows are the gm-nov3.3 picker rather than the old prev/next
    arrows, so each touch here is a walk into the picker and a choice."""
    s0 = rig.settings()
    anim_a = int(s0["bgAnimId"])
    anim_b = (anim_a + 1) % len(ANIM_NAMES)
    anim_c = (anim_a + 2) % len(ANIM_NAMES)
    map0 = s0["bgAnimThemeMap"]
    ref0_a = map_ref(map0, anim_a)
    ref0_b = map_ref(map0, anim_b)
    ref0_c = map_ref(map0, anim_c)

    open_animation(rig)
    ref_a = builtin_ref_other_than(ref0_a, category="Coffee")
    picker_choose_from_row(rig, "Gradient", "Coffee", THEME_NAMES[int(ref_a)])
    check(rig, "precedence_setup_touch_a", map_ref(rig.settings()["bgAnimThemeMap"], anim_a) == ref_a,
          map_ref(rig.settings()["bgAnimThemeMap"], anim_a))

    tap_row(rig, "Animation", "next")
    check(rig, "precedence_moved_to_b", int(rig.settings()["bgAnimId"]) == anim_b, rig.settings()["bgAnimId"])

    ref_b = builtin_ref_other_than(ref0_b, category="Neon")
    picker_choose_from_row(rig, "Gradient", "Neon", THEME_NAMES[int(ref_b)])
    map_after_b = rig.settings()["bgAnimThemeMap"]
    check(rig, "precedence_setup_touch_b", map_ref(map_after_b, anim_b) == ref_b, map_ref(map_after_b, anim_b))

    # The review's interference step: one web save replacing the whole map,
    # with a different value in A's touched slot and in C's untouched one.
    web_ref_a = builtin_ref_other_than(ref_a, category="Pastel")
    web_ref_c = builtin_ref_other_than(ref0_c, category="Metal and Stone")
    web_map = map_write_ref(map_write_ref(map_after_b, anim_a, web_ref_a), anim_c, web_ref_c)
    interference = True
    try:
        web_save(rig, {"bgAnimThemeMap": web_map})
    except RuntimeError as e:
        interference = False
        rig.log("gradient_precedence_web_save_unavailable", reason=str(e))
    if interference:
        landed = rig.wait_until(lambda: rig.settings()["bgAnimThemeMap"] == web_map, timeout=5)
        check(rig, "precedence_web_map_landed", bool(landed),
              "the web save has to reach Settings before its effect can be judged: %r" %
              rig.settings()["bgAnimThemeMap"])

    rig.settingsui(pop=1)

    def committed():
        m = rig.settings()["bgAnimThemeMap"]
        return map_ref(m, anim_a) == ref_a and map_ref(m, anim_b) == ref_b

    ok = rig.wait_until(committed, timeout=6)
    final_map = rig.settings()["bgAnimThemeMap"]
    check(rig, "gradient_precedence_a_survives", map_ref(final_map, anim_a) == ref_a,
          "anim %d got %r want %r" % (anim_a, map_ref(final_map, anim_a), ref_a))
    check(rig, "gradient_precedence_b_survives", map_ref(final_map, anim_b) == ref_b,
          "anim %d got %r want %r" % (anim_b, map_ref(final_map, anim_b), ref_b))
    check(rig, "gradient_precedence_both_survive", bool(ok))
    if interference:
        check(rig, "gradient_precedence_untouched_slot_keeps_web", map_ref(final_map, anim_c) == web_ref_c,
              "anim %d got %r want the web's %r" % (anim_c, map_ref(final_map, anim_c), web_ref_c))

    # Restore through the UI. This visit opens on animId==anim_b.
    open_animation(rig)
    pick_ref(rig, "Gradient", s0, ref0_b)
    tap_row(rig, "Animation", "prev")
    pick_ref(rig, "Gradient", s0, ref0_a)
    close_animation(rig)

    restored_map = rig.settings()["bgAnimThemeMap"]
    if interference and map_ref(restored_map, anim_c) != ref0_c:
        try:
            web_save(rig, {"bgAnimThemeMap": map_write_ref(restored_map, anim_c, ref0_c)})
            rig.wait_until(lambda: map_ref(rig.settings()["bgAnimThemeMap"], anim_c) == ref0_c, timeout=5)
        except Exception as e:  # noqa: BLE001 -- best effort; the check below reports what is left
            rig.log("could_not_restore_untouched_slot", reason=str(e))
        restored_map = rig.settings()["bgAnimThemeMap"]
    a_back = map_ref(restored_map, anim_a) == ref0_a
    b_back = map_ref(restored_map, anim_b) == ref0_b
    c_back = map_ref(restored_map, anim_c) == ref0_c
    check(rig, "gradient_precedence_restored", a_back and b_back and c_back,
          "anim_a=%r want %r, anim_b=%r want %r, anim_c=%r want %r" %
          (map_ref(restored_map, anim_a), ref0_a, map_ref(restored_map, anim_b), ref0_b,
           map_ref(restored_map, anim_c), ref0_c))
    if not (a_back and b_back and c_back):
        rig.log("could_not_restore", bgAnimThemeMap=restored_map)
    # The slots resolve back to what they were, but clearing one through the
    # picker leaves the separators in the string; put the stored bytes back.
    restore_fields_exactly(rig, "gradient_precedence_map_string_restored", {"bgAnimThemeMap": map0})


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
    ("gradient_picker_navigation", check_gradient_picker_navigation),
    ("gradient_all_and_standby_pickers", check_gradient_all_and_standby_pickers),
    ("gradient_picker_high_library_ids", check_gradient_picker_high_library_ids),
    ("gradient_picker_reachability", check_gradient_picker_reachability),
    ("gradient_picker_pagination_and_cancel", check_gradient_picker_pagination_and_cancel),
    ("gradient_picker_web_interference", check_gradient_picker_web_interference),
    ("gradient_picker_teardown", check_gradient_picker_teardown),
    ("gradient_picker_audit", check_gradient_picker_audit),
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
# - The gradient library and the map interference used to be listed here as
#   unreachable, because sim/platform/bganim_stub.cpp made bg_library_valid
#   and bg_map_valid always return false and WebUIPlugin.cpp gates every
#   POST to those fields behind them. gm-nov3.3 deleted those stubs: the
#   simulator links the real BgAnimThemes.cpp, so a well-formed library
#   string and a well-formed map both land, and the checks above exercise
#   them for real. Each such step reads the field back before judging the
#   UI, so a future change that starts rejecting these POSTs fails the run
#   instead of quietly passing it.
# - How a swatch looks next to the panel. The picker checks read the
#   framebuffer only far enough to prove a row draws a ramp and not a flat
#   block; that the ramp is the one the animation would paint is proved on
#   the host instead, by tools/animbench/swatch_parity.cpp, which compares
#   all 256 entries against the production palette code for every built-in
#   and for library strings with 2 to 16 stops, repeated positions and flat
#   endpoint runs, at seven tone settings.
# - Picker frame rates. Whether a picker page costs the animation more than
#   an ordinary settings page is a device question (the simulator has no
#   renderer and no /api/debug/anim). Command for the leader: the runner's
#   own page-rate phase against the loadtest board, with the animation,
#   parameters, tone, frame cap, interlace and panel divider held, a
#   repeated menu-screen baseline for the run-to-run spread, and the picker
#   pages read the same way.
# - "the web UI still loads the Display tab" after a gradient change: needs
#   a real browser.
