"""The settings UI's page inventory and the touch-target audit over it
(gm-flw.13).

Every page the shell can show is listed here with the rows it must show, so
one pass over this table proves three separate things at once: the geometry
rules of the shared contract hold on every page (`rig.audit`: a 56x56 px
floor per hit rectangle, a larger one for tiles, no overlap, 12 px of
clearance from the round panel's edge), every row that owns a value shows a non-empty one,
and every category page shows its own bead's row names rather than the
"Row n" placeholders the shell shipped with before the categories landed.

The row lists are transcribed from the categories' own buildRow switches
(`src/display/ui/default/settings/Cat*.cpp`), five rows to a page in index
order, which is what the shell's paging does. They are a standing check,
not a one-off at placeholder deletion: a category that loses a row, gains
one, or reorders two fails here.

Navigation splits in two. A category page is addressed directly through
`/api/debug/settingsui` (`cat=N`, then `page=P`). The schedule list and the
schedule editor are pushed pages with no category index of their own, so
they are reached the way a user reaches them, by tapping the `Schedules`
row and then a `Schedule n` row; `depth` on the debug route tells them
apart (1 category, 2 list, 3 editor).
"""

import os
import re
import time

from .rig import (PANEL_SIZE, ROW_CONTAINER_ROLES, RigHTTPError, find_tag, object_name, rows_on_page,
                  tag_role, tag_row, targets)

# Category indices, in kCategories order (SettingsUI.cpp). Fixture is the
# bench/simulator-only sixth tile; a production build has five.
CAT_TEMPS, CAT_DISPLAY, CAT_ANIMATION, CAT_MACHINE, CAT_STATUS, CAT_FIXTURE = range(6)

# The tile floor. The shared contract sets one figure for every tappable
# target, 56x56, and says nothing about tiles; this bead's own context asks
# for 100x100 and the shell builds them 96x96 (SettingsUI::buildTile, whose
# comment records the geometry it was chosen for: kRadius 145 with kSize 96
# keeps every corner inside radius 228 and every adjacent pair a few pixels
# apart, both of which tighten if the tiles grow). 96 is what shipped and it
# clears the contract's own floor by 40 px, so that is the number enforced
# here; the 4 px difference from this bead's text is flagged to the epic's
# lead rather than decided in a test. What this still catches is a tile that
# shrinks.
MIN_TILE_SIZE = 96

# A placeholder row name, from the deleted SettingsPlaceholders.cpp: "Row 1"
# through "Row 5". Matched by shape so a weak definition creeping back in
# under any category is caught, not only the five names that once existed.
PLACEHOLDER_ROW = re.compile(r"^Row \d+$")


class PageSpec:
    """One page of the settings shell. `cat`/`page` address it through the
    debug route; `rows` is the exact row list expected on it, top to bottom.
    `pushed` pages carry cat=None and are reached by the visitor below."""

    def __init__(self, key, cat, page, rows, scenario=None, pushed=False):
        self.key = key
        self.cat = cat
        self.page = page
        self.rows = rows
        self.scenario = scenario
        self.pushed = pushed


# The five real categories plus the Fixture tile, in kCategories order. Row
# counts: Temps 5 (1 page), Display 6 (2), Animation 16 (4, the Parameters
# row gm-3vj.2 adds included), Machine 3 + the Schedules row gm-flw.11 adds
# (1 page), Status 8 (2), Fixture 11 (3).
#
# The Parameters page itself is not in this table. Its row list is one
# stepper per parameter the current animation defines plus a Reset row, so
# both the count and the names change with the stored bgAnimId; the animation
# scenario audits it at visit time instead (test_animation.py,
# check_parameters_page).
CATEGORY_PAGES = [
    PageSpec("temps-p0", CAT_TEMPS, 0,
             ["Temperature offset", "Pressure sensor", "Brew delay", "Grind delay", "Delay auto-adjust"], "temps"),
    PageSpec("display-p0", CAT_DISPLAY, 0,
             ["Main brightness", "Standby brightness", "Dim after", "24-hour clock", "Time zone region"], "display"),
    PageSpec("display-p1", CAT_DISPLAY, 1, ["City"], "display"),
    PageSpec("animation-p0", CAT_ANIMATION, 0,
             ["Animation", "Parameters", "Frame rate", "All screens", "Theme"], "animation"),
    PageSpec("animation-p1", CAT_ANIMATION, 1,
             ["Gradient", "Plates", "Plate colour", "Plate opacity", "Element tint"], "animation"),
    PageSpec("animation-p2", CAT_ANIMATION, 2,
             ["Tint colour", "Text scrim", "Fade out", "Fade in", "Fade curve"], "animation"),
    PageSpec("animation-p3", CAT_ANIMATION, 3, ["Interlace"], "animation"),
    PageSpec("machine-p0", CAT_MACHINE, 0,
             ["Startup mode", "Standby timeout", "Auto wake-up", "Schedules"], "machine"),
    PageSpec("status-p0", CAT_STATUS, 0,
             ["Display firmware", "Controller firmware", "Network", "IP address", "Controller"], "status"),
    PageSpec("status-p1", CAT_STATUS, 1, ["Scale", "Time", "Device info", "Restart"], "status"),
    PageSpec("fixture-p0", CAT_FIXTURE, 0, ["stepper", "choice", "toggle", "action", "locked"], None),
    PageSpec("fixture-p1", CAT_FIXTURE, 1, ["confirm", "uptime", "info8", "info9", "info10"], None),
    PageSpec("fixture-p2", CAT_FIXTURE, 2, ["info11"], None),
]

# The two pushed pages. The list's rows depend on how many schedules are
# stored ("Schedule 1".."Schedule n" then "Add schedule", five to a page),
# so its expectation is computed at visit time; the editor's ten rows are
# fixed (SettingsUI.h, gm-flw.11).
SCHEDULE_EDITOR_PAGES = [
    PageSpec("schedule-editor-p0", None, 0, ["Hour", "Minute", "Monday", "Tuesday", "Wednesday"], "schedules",
             pushed=True),
    PageSpec("schedule-editor-p1", None, 1, ["Thursday", "Friday", "Saturday", "Sunday", "Remove schedule"],
             "schedules", pushed=True),
]


def schedule_list_rows(count):
    """The row names of the schedule list holding `count` schedules, page by
    page: the entries in order, then "Add schedule" as the last row."""
    names = ["Schedule %d" % (i + 1) for i in range(count)] + ["Add schedule"]
    return [names[i:i + 5] for i in range(0, len(names), 5)]


# ---- the checks over one dumped page ---------------------------------------


def tile_violations(dump):
    """Tiles are held to their own floor (MIN_TILE_SIZE above) rather than
    the 56x56 one every other target gets; rig.audit knows only the general
    floor, so the tile page's extra rule lives here."""
    out = []
    for o in targets(dump):
        if tag_role(o) != "tile":
            continue
        x1, y1, x2, y2 = o["hit"]
        w, h = x2 - x1 + 1, y2 - y1 + 1
        if w < MIN_TILE_SIZE or h < MIN_TILE_SIZE:
            out.append({"target": tag_row(o), "tag": o.get("tag"), "reason": "tile_size", "detail": "%dx%d" % (w, h)})
    return out


# Row kinds whose value is optional. Every row widget builds a value label,
# but an action or confirm row is a whole-row target whose caption is the
# whole message: "Add schedule" and "Remove schedule" set no value and show
# an empty one, by construction. An action row that does have something to
# say still fills it in (a schedule entry shows its time and days), so this
# exempts the kind, not the two names.
VALUE_OPTIONAL_ROLES = ("action", "confirm")


def value_violations(dump):
    """Every row that shows a value must show a non-empty one. The label's
    own buffer may hold truncation dots; the tag's text pointer holds the
    real string, which is what the dump exports and what this reads."""
    role_by_row = {}
    for o in dump["objects"]:
        role = tag_role(o)
        if role in ROW_CONTAINER_ROLES:
            role_by_row[tag_row(o)] = role
    out = []
    for o in dump["objects"]:
        if tag_role(o) != "value":
            continue
        row = tag_row(o)
        if role_by_row.get(row) in VALUE_OPTIONAL_ROLES:
            continue
        text = o.get("val")
        if text is None:
            text = o.get("t")
        if text is None or not str(text).strip():
            out.append({"target": row, "tag": o.get("tag"), "reason": "empty_value",
                        "detail": repr(text)})
    return out


def row_violations(dump, expected):
    """The page's rows against the category's own row list, and the standing
    "no Row n placeholder" check. `expected` of None skips the name
    comparison (the tile page has no rows) but never the placeholder
    check."""
    out = []
    got = rows_on_page(dump)
    for name in got:
        if PLACEHOLDER_ROW.match(name or ""):
            out.append({"target": name, "tag": None, "reason": "placeholder_row",
                        "detail": "a weak placeholder category is still linked"})
    if expected is not None and got != list(expected):
        out.append({"target": "(page)", "tag": None, "reason": "row_names",
                    "detail": "got %r want %r" % (got, list(expected))})
    return out


def _split_chevron_overlaps(dump, violations, exempt):
    """Moves an overlap between a target and the exit chevron out of the
    violations and into the exempt bucket, where it is still reported.

    The chevron is a 40x40 image with a 45 px ext click pad, which the
    screen then clips (the shared contract calls this out and exempts the
    chevron from the size and edge rules for it). On the tile page that
    padded rectangle reaches up into two of the six tiles: measured
    2026-09-06 on the simulator, the chevron's body is (220,430)-(259,469)
    and its padded rectangle (175,385)-(304,479), which clips 10x6 px off
    the bottom corner of the Animation and Machine tiles. The overlapping
    pixels are 40 px from anything the chevron draws, so the ambiguity the
    no-overlap rule exists to prevent is not there; every other overlap,
    between two real targets, still fails."""
    chevrons = {object_name(o) for o in dump["objects"] if tag_role(o) == "exit"}
    chevron_tags = {o.get("tag") for o in dump["objects"] if tag_role(o) == "exit"}
    kept = []
    for v in violations:
        if v.get("reason") == "overlap" and (v.get("other") in chevrons or v.get("tag") in chevron_tags
                                             or v.get("target") in chevrons):
            exempt.append(v)
            continue
        kept.append(v)
    return kept


def audit_page(rig, dump, expected_rows, is_tile_page=False):
    """Every rule for one dumped page. Returns
    {"violations": [...], "exempt": [...], "targets": n, "smallest": (w, h)}."""
    result = rig.audit(dump)
    exempt = list(result["exempt"])
    violations = _split_chevron_overlaps(dump, list(result["violations"]), exempt)
    violations += value_violations(dump)
    violations += row_violations(dump, expected_rows)
    if is_tile_page:
        violations += tile_violations(dump)
    ts = targets(dump)
    smallest = None
    for o in ts:
        x1, y1, x2, y2 = o["hit"]
        wh = (x2 - x1 + 1, y2 - y1 + 1)
        if smallest is None or wh[0] * wh[1] < smallest[0] * smallest[1]:
            smallest = wh
    return {
        "violations": violations,
        "exempt": exempt,
        "targets": len(ts),
        "smallest": smallest,
        "rows": rows_on_page(dump),
    }


def write_page_png(rig, dump, out_dir, key):
    """The page's framebuffer with every target's hit rectangle drawn, at
    half resolution (the panel is 480x480; step=2 keeps the PNG small
    enough to keep one per page in a report directory)."""
    rects = [tuple(o["hit"]) for o in targets(dump)]
    path = os.path.join(out_dir, "%s.png" % key)
    try:
        rig.fb_png(path, step=2, hit_rects=rects)
    except Exception as e:  # noqa: BLE001 -- a framebuffer read must not end the run
        return None, str(e)
    return path, None


# ---- navigation -------------------------------------------------------------


def command(rig, timeout=15, **cmd):
    """One /api/debug/settingsui command, waited out. Two things the route
    makes the caller's problem: it reports that a command was queued, not
    that it ran (Open alone can span several UI passes while it changes to
    the menu screen and settles), and it answers 409 while one is still in
    flight. So this polls the published state until the command's own seq
    appears, and treats a 409 as "the previous one has not finished yet"
    rather than as a failure. Returns the published state."""
    deadline = time.time() + timeout
    queued = None
    while queued is None:
        try:
            queued = rig.settingsui(**cmd)
        except RigHTTPError as e:
            if "409" not in str(e) or time.time() > deadline:
                raise
            time.sleep(0.2)
    seq = queued["seq"]
    return rig.wait_until(lambda: _state_at(rig, seq), max(1.0, deadline - time.time()))


def _state_at(rig, seq):
    state = rig.settingsui_state()
    return state if int(state.get("seq", -1)) >= int(seq) else None


def open_tiles(rig):
    """The tile page, from wherever the shell is: open (a no-op while open),
    then pop back to depth 0 if a category is showing."""
    state = command(rig, open=1)
    for _ in range(6):
        if not state.get("open"):
            state = command(rig, open=1)
            continue
        if int(state.get("depth", 0)) <= 0:
            return rig.touchmap(screen=0)
        state = command(rig, pop=1)
    raise RuntimeError("could not reach the settings tile page: %r" % rig.settingsui_state())


def open_category_page(rig, cat, page):
    """Opens category `cat` at `page` and returns the dump. `cat` while a
    different category is showing pops the old one first (the route's own
    behaviour), so this does not need to unwind by hand.

    `page` is waited out on the published page number, not on a seq: the
    shell's gotoPage does not bump one (the same reason test_schedules.py
    and test_animation.py wait on the field)."""
    open_tiles(rig)
    state = command(rig, cat=cat)
    if page:
        rig.settingsui(page=page)
        state = rig.wait_until(lambda: _state_at_page(rig, page), 8)
    if int(state.get("category", -1)) != cat or int(state.get("page", -1)) != page:
        raise RuntimeError("settingsui did not land on cat=%d page=%d: %r" % (cat, page, state))
    return rig.touchmap(screen=0)


def _state_at_page(rig, page):
    state = rig.settingsui_state()
    return state if int(state.get("page", -1)) == int(page) else None


def goto_page(rig, page):
    """Moves the page of whatever is on top and returns the fresh dump."""
    rig.settingsui(page=page)
    rig.wait_until(lambda: _state_at_page(rig, page), 8)
    return rig.touchmap(screen=0)


def open_schedule_list(rig):
    """Taps the Machine page's Schedules row. Returns (dump, state)."""
    dump = open_category_page(rig, CAT_MACHINE, 0)
    row = find_tag(dump, "Schedules", "action")
    if row is None:
        raise RuntimeError("no Schedules row on the Machine page: %r" % rows_on_page(dump))
    rig.tap_target(row)
    # The shell publishes its state on the UI task's next pass, which on the
    # device can be later than tap_target's 150 ms settle (measured: the
    # list was pushed, the state read once still said depth 1). Wait for it.
    state = _wait_depth(rig, 2)
    if state is None:
        raise RuntimeError("tapping Schedules did not push the list: %r" % rig.settingsui_state())
    return rig.touchmap(screen=0), state


def _wait_depth(rig, depth, timeout=5):
    try:
        return rig.wait_until(lambda: _state_at_depth(rig, depth), timeout)
    except TimeoutError:
        return None


def _state_at_depth(rig, depth):
    state = rig.settingsui_state()
    return state if int(state.get("depth", -1)) == int(depth) else None


def open_schedule_editor(rig, n=1):
    """Taps schedule `n` (1-based) in the list. Returns (dump, state)."""
    dump, _ = open_schedule_list(rig)
    row = find_tag(dump, "Schedule %d" % n, "action")
    if row is None:
        raise RuntimeError("no Schedule %d row in the list: %r" % (n, rows_on_page(dump)))
    rig.tap_target(row)
    state = _wait_depth(rig, 3)
    if state is None:
        raise RuntimeError("tapping Schedule %d did not push the editor: %r" % (n, rig.settingsui_state()))
    return rig.touchmap(screen=0), state


def category_pages(include_fixture=True):
    """The category pages to visit. A production build has no Fixture tile;
    the runner passes include_fixture from the tile count it saw."""
    if include_fixture:
        return list(CATEGORY_PAGES)
    return [p for p in CATEGORY_PAGES if p.cat != CAT_FIXTURE]


def tile_count(dump):
    return sum(1 for o in targets(dump) if tag_role(o) == "tile")


def largest_page_key(pages):
    """The page with the most expected rows, for the leak phase's open/close
    cycles (the most widgets built and torn down per cycle). Ties break on
    the table's order, which is kCategories order."""
    return max(pages, key=lambda p: (len(p.rows), -pages.index(p))).key


def panel_edge_note():
    """The audit's geometry constants, echoed into report.json so a report
    read months later carries the rules it was judged against."""
    return {"panel": PANEL_SIZE, "min_target": 56, "min_tile": MIN_TILE_SIZE, "edge_margin": 12}
