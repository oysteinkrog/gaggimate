"""The gradient-swatch oracle (gm-nov3.33, gm-nov3.38): given a device's
stored settings and the stops a row is supposed to show, decide whether the
pixels a swatch actually drew are that ramp.

Three callers read a swatch this way: tools/settings_ui_tests/test_animation.py
and test_gradientdraft.py (both scenarios against the simulator or a loadtest
device through Rig), and tools/picker_device_checks.py (a standalone script
that only runs against a real board). picker_device_checks.py imports nothing
else from the scenario files on purpose — it talks to a board and nothing
else — so this module holds the shared reading rather than test_animation.py,
which picker_device_checks.py does not import. Before gm-nov3.38 each caller
either had this or fell back to counting the swatch's distinct colours, which
every entry in the fixture library clears by 10x to 20x and so told apart
nothing (gm-nov3.33's and gm-nov3.38's bead text have the measurements).

Each caller keeps its own `check()` and FAILURES/TOTAL bookkeeping, so the
reporting wrapper around these (test_animation.py's check_swatch) stays local
to that file rather than moving here.
"""
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))


def hex_rgb(s):
    n = int(s, 16)
    return ((n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF)


def swatch_matches_stops(strip, stops, tol=20):
    """Whether a swatch strip is the ramp `stops` describes, judged at its
    two ends. Returns (ok, detail).

    The ends are the whole oracle on purpose. A swatch is drawn by
    GradientSwatch.cpp, which is a separate transcription of the palette
    arithmetic and is held to the real one on the host by
    tools/animbench/swatch_parity.cpp; re-deriving the interior here would
    be a third transcription with nothing checking it. What this has to
    tell apart is which entry a row drew, and the ends do that. The
    tolerance covers RGB565 quantisation (8 per channel at the ends) and
    the ramp not sampling its last stop exactly; measured on the simulator,
    "ff0000,ff2000" reads (255,0,0) to (255,28,0) against a nominal
    (255,32,0).

    A stop's @position does not change either end: swatchSampleRgb clamps,
    so the pixel at x0 is the first stop's colour however far in that stop
    sits, and the pixel at x1 is the last stop's. That is what lets the
    fixture's "Wide Ends" entry be read this way.

    Every check that asks which gradient a row drew goes through this
    (gm-nov3.33). Counting distinct colours does not: all twelve entries of
    the fixture library draw between 46 and 80 of them, so a row drawing
    any other entry passed a count."""
    want_first = hex_rgb(stops.split(",")[0].split("@")[0])
    want_last = hex_rgb(stops.split(",")[-1].split("@")[0])
    got_first, got_last = tuple(strip[0]), tuple(strip[-1])
    ok = (max(abs(a - b) for a, b in zip(got_first, want_first)) <= tol
          and max(abs(a - b) for a, b in zip(got_last, want_last)) <= tol)
    return ok, "%r..%r against %s (%r..%r)" % (got_first, got_last, stops, want_first, want_last)


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


_BUILTIN_STOPS = []


def builtin_stops(index):
    """The stop string of one built-in gradient, in the form a stored
    gradient carries it.

    Read from data/gradients.json, which is the one place the built-ins are
    written: scripts/gen_gradients.py turns that file into the firmware
    table, the web mirror and gradients_gen.py beside this script, so a
    reading taken from the source cannot drift from what the display draws.
    gradients_gen.py carries the names and the categories but not the stops,
    and it is generated, so this reads the source rather than asking for a
    fourth generated table."""
    if not _BUILTIN_STOPS:
        with open(os.path.join(REPO_ROOT, "data", "gradients.json"), encoding="utf-8") as fh:
            doc = json.load(fh)
        _BUILTIN_STOPS.extend(",".join(s.lstrip("#") for s in g["stops"]) for g in doc["gradients"])
    return _BUILTIN_STOPS[index] if 0 <= index < len(_BUILTIN_STOPS) else None


def stops_for_ref(settings, ref):
    """The stops the display draws for one ref, or None when the ref names
    nothing that exists. The same three-way reading gradient_name_for_ref
    (test_animation.py) does, returning the gradient instead of the name."""
    ref = str(ref or "")
    if ref == "":
        return None
    if ref.startswith("c"):
        if not ref[1:].isdigit():
            return None
        wanted = int(ref[1:])
        for entry_id, _name, gradient in library_entries(settings.get("bgAnimGradients", "")):
            if entry_id == wanted:
                return gradient
        return None
    if not ref.isdigit():
        return None
    return builtin_stops(int(ref))


def global_stops(settings):
    """The stops behind expected_global_gradient_text (test_animation.py):
    the global ref when it resolves, else what the legacy pair resolves to,
    which is either a built-in or the retained custom gradient."""
    stops = stops_for_ref(settings, settings.get("bgAnimGradientRef", ""))
    if stops is not None:
        return stops
    custom = str(settings.get("bgAnimCustomTheme", ""))
    builtin = legacy_builtin(int(settings["bgAnimTheme"]), custom_theme_valid(custom))
    if builtin < 0:
        # bg_parse_gradient takes commas or spaces; swatch_matches_stops
        # splits on commas only, so the separator is normalised here.
        return ",".join(p.lstrip("#") for p in re.split(r"[\s,]+", custom) if p)
    return builtin_stops(builtin) or builtin_stops(0)


def toned_stops(settings, stops):
    """`stops` after the tone the swatch is drawn with. Python mirror of
    GradientSwatch.cpp's swatchApplyTone, including the two integer
    conversions DefaultUI::updateState does on the way in.

    The stored tone is 100/100 by default, which is the identity, and no
    scenario here changes it; this exists so a device whose owner has moved
    either slider is read correctly rather than failing as if it drew the
    wrong gradient."""
    bright256 = int(settings.get("bgAnimBrightness", 100)) * 256 // 100
    knee = int(settings.get("bgAnimHighlightKnee", 100)) * 255 // 100
    bright256 = max(0, min(256, bright256))
    knee = max(0, min(255, knee))
    out = []
    for part in str(stops or "").split(","):
        colour = part.split("@")[0]
        chans = []
        for v in hex_rgb(colour):
            if v > knee:
                v = knee + ((v - knee) >> 2)
            chans.append(max(0, min(255, (v * bright256) >> 8)))
        out.append("%02x%02x%02x" % tuple(chans))
    return ",".join(out)
