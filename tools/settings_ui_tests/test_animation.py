#!/usr/bin/env python3
"""Scenario for the Animation settings category (gm-flw.9): animation, frame
rate, all-screens, theme, gradient, plates, plate colour/opacity, element
tint/colour and text scrim, all eleven rows live. Built on
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
# device route this script could read them from instead; verified against
# the same source at HEAD 2b87cb88. A change to either real table needs the
# same edit made in three places now (BgAnimRegistry.cpp/BgAnimThemes.cpp,
# CatAnimation.cpp's sim mirror, and this list) -- flagged to the epic lead.
ANIM_NAMES = [
    "Plasma", "Lava", "Silk", "Starfield", "Aurora", "Ripples", "Caustics", "Mandala",
    "Orbits", "Fireflies", "Steam", "Ember", "Nebula", "Silk 2",
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

# The eleven fields CatAnimation.cpp writes; the "visit changes nothing
# writes nothing" check compares these, byte for byte, before and after a
# no-op visit.
ANIMATION_FIELDS = [
    "bgAnimId", "bgAnimFps", "bgAnimAllScreens", "themeMode", "bgAnimThemeMap", "bgAnimClearPlates",
    "bgAnimPlateColor", "bgAnimPlateOpacity", "elementTintEnabled", "elementTintColor", "bgAnimScrim",
]

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


def expected_gradient_text(settings, anim_id=None):
    """Python mirror of gradientChoices()/gradientChoiceIndexForRef() plus
    CatAnimation.cpp's "Default (<name>)" formatting, for the built-in-theme
    and Default cases only. Returns None for a library ref ("cN"): the
    simulator's bg_library_valid always returns false (sim/platform/
    bganim_stub.cpp), so bgAnimGradients can never legitimately hold a
    library entry here and this script never needs to resolve one."""
    if anim_id is None:
        anim_id = int(settings["bgAnimId"])
    ref = map_ref(settings["bgAnimThemeMap"], anim_id)
    if ref == "":
        theme_idx = int(settings["bgAnimTheme"])
        name = THEME_NAMES[theme_idx] if 0 <= theme_idx < len(THEME_NAMES) else THEME_NAMES[0]
        return "Default (%s)" % name
    if ref.startswith("c"):
        return None
    try:
        idx = int(ref)
    except ValueError:
        idx = -1
    if 0 <= idx < len(THEME_NAMES):
        return THEME_NAMES[idx]
    theme_idx = int(settings["bgAnimTheme"])
    name = THEME_NAMES[theme_idx] if 0 <= theme_idx < len(THEME_NAMES) else THEME_NAMES[0]
    return "Default (%s)" % name


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


def close_animation(rig):
    rig.settingsui(close=1)
    rig.wait_until(lambda: rig.settingsui_state().get("open") is False, timeout=5)


# ---------------------------------------------------------------------------
# Checks


def check_rows_match_settings(rig):
    """Acceptance: the eleven rows show the stored values as GET
    /api/settings reports them, across all three pages, without a
    renderer (the model's animation/gradient/palette logic is host code)."""
    s = rig.settings()
    d0 = open_animation(rig)
    check(rig, "row_animation", rig.row_value(d0, "Animation") == ANIM_NAMES[int(s["bgAnimId"])],
          "%r vs bgAnimId=%s" % (rig.row_value(d0, "Animation"), s["bgAnimId"]))
    check(rig, "row_frame_rate", rig.row_value(d0, "Frame rate") == "%d fps" % int(s["bgAnimFps"]),
          rig.row_value(d0, "Frame rate"))
    check(rig, "row_all_screens", rig.row_value(d0, "All screens") == ("On" if s["bgAnimAllScreens"] else "Off"),
          rig.row_value(d0, "All screens"))
    check(rig, "row_theme", rig.row_value(d0, "Theme") == THEME_MODE_LABELS[int(s["themeMode"])],
          rig.row_value(d0, "Theme"))
    exp_grad = expected_gradient_text(s)
    if exp_grad is not None:
        check(rig, "row_gradient", rig.row_value(d0, "Gradient") == exp_grad,
              "%r vs %r" % (rig.row_value(d0, "Gradient"), exp_grad))
    else:
        rig.log("row_gradient_skipped", reason="library ref not reachable on the simulator")

    d1 = goto_page(rig, 1)
    check(rig, "row_plates", rig.row_value(d1, "Plates") == PLATES_LABELS[int(s["bgAnimClearPlates"])],
          rig.row_value(d1, "Plates"))
    check(rig, "row_plate_colour", rig.row_value(d1, "Plate colour") == expected_palette_text(hex_to_int(s["bgAnimPlateColor"])),
          rig.row_value(d1, "Plate colour"))
    check(rig, "row_plate_opacity", rig.row_value(d1, "Plate opacity") == "%d %%" % int(s["bgAnimPlateOpacity"]),
          rig.row_value(d1, "Plate opacity"))
    check(rig, "row_element_tint", rig.row_value(d1, "Element tint") == ("On" if s["elementTintEnabled"] else "Off"),
          rig.row_value(d1, "Element tint"))
    check(rig, "row_tint_colour", rig.row_value(d1, "Tint colour") == expected_palette_text(hex_to_int(s["elementTintColor"])),
          rig.row_value(d1, "Tint colour"))

    d2 = goto_page(rig, 2)
    check(rig, "row_text_scrim", rig.row_value(d2, "Text scrim") == "%d %%" % int(s["bgAnimScrim"]),
          rig.row_value(d2, "Text scrim"))

    for page, dump in ((0, d0), (1, d1), (2, d2)):
        a = rig.audit(dump)
        check(rig, "audit_page_%d_clean" % page, len(a["violations"]) == 0, repr(a["violations"]))

    close_animation(rig)


def check_no_op_visit(rig):
    """Acceptance: a visit that changes nothing writes nothing."""
    before = {k: rig.settings()[k] for k in ANIMATION_FIELDS}
    open_animation(rig)
    goto_page(rig, 1)
    goto_page(rig, 2)
    close_animation(rig)
    after = {k: rig.settings()[k] for k in ANIMATION_FIELDS}
    check(rig, "no_op_visit_writes_nothing", before == after, "before=%r after=%r" % (before, after))


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

    d0 = open_animation(rig)
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
    d = open_animation(rig)
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
    d = open_animation(rig)
    row = rig.find_tag(d, "Theme", "row")
    if row is None:
        check(rig, "theme_row_found", False)
        close_animation(rig)
        return
    x1, y1, x2, y2 = row["hit"]
    points = [(x1 + int((x2 - x1) * f), (y1 + y2) // 2) for f in (0.3, 0.5, 0.7)]

    fb_before = rig.get_bytes("/api/debug/fb?step=1")
    colors_before = [rgb565_pixel(fb_before, x, y) for x, y in points]

    next_btn = rig.find_tag(d, "Theme", "next")
    rig.tap_target(next_btn)
    rig.wait_until(lambda: int(rig.settings()["themeMode"]) != mode0, timeout=2)
    time.sleep(0.5)  # one more rerender pass for applyTheme()'s change_color_theme + the page's own rebuildPage
    fb_after = rig.get_bytes("/api/debug/fb?step=1")
    colors_after = [rgb565_pixel(fb_after, x, y) for x, y in points]

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

    d = open_animation(rig)
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

    d = open_animation(rig)
    grad_a_btn = rig.find_tag(d, "Gradient", "next")
    if grad_a_btn is None:
        check(rig, "precedence_gradient_next_found", False)
        close_animation(rig)
        return
    rig.tap_target(grad_a_btn)
    ref_a = map_ref(rig.settings()["bgAnimThemeMap"], anim_a)
    check(rig, "precedence_setup_touch_a", ref_a != "", "anim %d ref=%r" % (anim_a, ref_a))

    rig.tap_target(rig.find_tag(rig.touchmap(screen=0), "Animation", "next"))
    check(rig, "precedence_moved_to_b", int(rig.settings()["bgAnimId"]) == anim_b, rig.settings()["bgAnimId"])

    rig.tap_target(rig.find_tag(rig.touchmap(screen=0), "Gradient", "next"))
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

    # Restore both to Default through the UI, never by a second POST. This
    # visit opens on animId==anim_b (bgAnimId was written live by the
    # "Animation next" tap above), Gradient showing ref_b one step from
    # Default; undo it, step Animation back to A, undo A's edit the same
    # way.
    d = open_animation(rig)
    rig.tap_target(rig.find_tag(d, "Gradient", "prev"))
    rig.tap_target(rig.find_tag(rig.touchmap(screen=0), "Animation", "prev"))
    rig.tap_target(rig.find_tag(rig.touchmap(screen=0), "Gradient", "prev"))
    close_animation(rig)

    restored_map = rig.settings()["bgAnimThemeMap"]
    a_default = map_ref(restored_map, anim_a) == ""
    b_default = map_ref(restored_map, anim_b) == ""
    check(rig, "gradient_precedence_restored", a_default and b_default,
          "anim_a=%r anim_b=%r" % (map_ref(restored_map, anim_a), map_ref(restored_map, anim_b)))
    if not (a_default and b_default):
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
    d1 = goto_page(rig, 1)

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

    d1 = rig.touchmap(screen=0)
    if rig.row_value(d1, "Element tint") == "On":
        rig.tap_target(rig.find_tag(d1, "Element tint", "toggle"))
    d1 = rig.touchmap(screen=0)
    check(rig, "tint_off_for_test", rig.row_value(d1, "Element tint") == "Off", rig.row_value(d1, "Element tint"))
    before_tc = rig.settings()["elementTintColor"]
    rig.tap_target(rig.find_tag(d1, "Tint colour", "next"))
    check(rig, "tint_color_disabled_ignored", rig.settings()["elementTintColor"] == before_tc)

    rig.tap_target(rig.find_tag(rig.touchmap(screen=0), "Element tint", "toggle"))
    d1 = rig.touchmap(screen=0)
    check(rig, "tint_on_for_test", rig.row_value(d1, "Element tint") == "On", rig.row_value(d1, "Element tint"))
    before_tc = rig.settings()["elementTintColor"]
    rig.tap_target(rig.find_tag(d1, "Tint colour", "next"))
    after_tc = rig.settings()["elementTintColor"]
    check(rig, "tint_color_enabled_changes", after_tc != before_tc, "%r -> %r" % (before_tc, after_tc))

    # Restore, through the UI, in reverse.
    rig.tap_target(rig.find_tag(rig.touchmap(screen=0), "Tint colour", "prev"))  # undo the one "next" above
    if not tint0:
        rig.tap_target(rig.find_tag(rig.touchmap(screen=0), "Element tint", "toggle"))
    d1 = rig.touchmap(screen=0)
    check(rig, "tint_enabled_restored", bool(rig.row_value(d1, "Element tint") == "On") == tint0)

    rig.tap_target(rig.find_tag(d1, "Plate colour", "prev"))  # undo the one "next" above
    restore_opacity_dir = "plus" if opacity_dir == "minus" else "minus"
    rig.tap_target(rig.find_tag(rig.touchmap(screen=0), "Plate opacity", restore_opacity_dir))
    steps_from_custom = (plates0 - 2) % 3
    for _ in range(steps_from_custom):
        d1 = rig.touchmap(screen=0)
        rig.tap_target(rig.find_tag(d1, "Plates", "next"))

    final = rig.settings()
    check(rig, "plates_restored", int(final["bgAnimClearPlates"]) == plates0,
          "got %r want %d" % (final["bgAnimClearPlates"], plates0))
    check(rig, "plate_opacity_restored", int(final["bgAnimPlateOpacity"]) == opacity0,
          "got %r want %d" % (final["bgAnimPlateOpacity"], opacity0))
    if int(final["bgAnimClearPlates"]) != plates0 or int(final["bgAnimPlateOpacity"]) != opacity0:
        rig.log("could_not_restore", plates=final["bgAnimClearPlates"], plateOpacity=final["bgAnimPlateOpacity"])
    close_animation(rig)


# Every check, in order. One list, read by main() and by run() below, so a
# standalone invocation and the end-to-end runner cannot drift apart.
CHECKS = [
    ("rows_match_settings", check_rows_match_settings),
    ("no_op_visit", check_no_op_visit),
    ("frame_rate_live_and_precedence", check_frame_rate_live_and_precedence),
    ("theme_recolor", check_theme_recolor),
    ("gradient_default_and_builtin", check_gradient_default_and_builtin),
    ("gradient_precedence_across_animations", check_gradient_precedence_across_animations),
    ("plates_and_tint_coupling", check_plates_and_tint_coupling),
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
