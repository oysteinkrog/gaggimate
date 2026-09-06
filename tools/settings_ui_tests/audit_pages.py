"""The settings UI's page inventory and the touch-target audit over it
(gm-flw.13).

Every page the shell can show is listed here with the rows it must show, so
one pass over this table proves three separate things at once: the geometry
rules of the shared contract hold on every page (`rig.audit`: a 56x56 px
floor per hit rectangle, tiles 100x100, no overlap, 12 px of clearance from
the round panel's edge), every row that owns a value shows a non-empty one,
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

from .rig import PANEL_SIZE, find_tag, rows_on_page, tag_role, tag_row, targets

# Category indices, in kCategories order (SettingsUI.cpp). Fixture is the
# bench/simulator-only sixth tile; a production build has five.
CAT_TEMPS, CAT_DISPLAY, CAT_ANIMATION, CAT_MACHINE, CAT_STATUS, CAT_FIXTURE = range(6)

MIN_TILE_SIZE = 100

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
# counts: Temps 5 (1 page), Display 6 (2), Animation 11 (3), Machine 3 + the
# Schedules row gm-flw.11 adds (1 page), Status 8 (2), Fixture 11 (3).
CATEGORY_PAGES = [
    PageSpec("temps-p0", CAT_TEMPS, 0,
             ["Temperature offset", "Pressure sensor", "Brew delay", "Grind delay", "Delay auto-adjust"], "temps"),
    PageSpec("display-p0", CAT_DISPLAY, 0,
             ["Main brightness", "Standby brightness", "Dim after", "24-hour clock", "Time zone region"], "display"),
    PageSpec("display-p1", CAT_DISPLAY, 1, ["City"], "display"),
    PageSpec("animation-p0", CAT_ANIMATION, 0,
             ["Animation", "Frame rate", "All screens", "Theme", "Gradient"], "animation"),
    PageSpec("animation-p1", CAT_ANIMATION, 1,
             ["Plates", "Plate colour", "Plate opacity", "Element tint", "Tint colour"], "animation"),
    PageSpec("animation-p2", CAT_ANIMATION, 2, ["Text scrim"], "animation"),
    PageSpec("machine-p0", CAT_MACHINE, 0,
             ["Startup mode", "Standby timeout", "Auto wake-up", "Schedules"], "machine"),
    PageSpec("status-p0", CAT_STATUS, 0,
             ["Display firmware", "Controller firmware", "Network", "IP address", "Controller"], "status"),
    PageSpec("status-p1", CAT_STATUS, 1, ["Scale", "Time", "Restart"], "status"),
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
    """Tiles are held to a 100x100 floor rather than the 56x56 one every
    other target gets (the shared contract's touch-target rules); rig.audit
    knows only the general floor, so the tile page's extra rule lives here."""
    out = []
    for o in targets(dump):
        if tag_role(o) != "tile":
            continue
        x1, y1, x2, y2 = o["hit"]
        w, h = x2 - x1 + 1, y2 - y1 + 1
        if w < MIN_TILE_SIZE or h < MIN_TILE_SIZE:
            out.append({"target": tag_row(o), "tag": o.get("tag"), "reason": "tile_size", "detail": "%dx%d" % (w, h)})
    return out


def value_violations(dump):
    """Every row that carries a `row/value` object must show a non-empty
    canonical value. The label's own buffer may hold truncation dots; the
    tag's text pointer holds the real string, which is what the dump
    exports and what this reads."""
    out = []
    for o in dump["objects"]:
        if tag_role(o) != "value":
            continue
        text = o.get("val")
        if text is None:
            text = o.get("t")
        if text is None or not str(text).strip():
            out.append({"target": tag_row(o), "tag": o.get("tag"), "reason": "empty_value",
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


def audit_page(rig, dump, expected_rows, is_tile_page=False):
    """Every rule for one dumped page. Returns
    {"violations": [...], "exempt": [...], "targets": n, "smallest": (w, h)}."""
    result = rig.audit(dump)
    violations = list(result["violations"])
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
        "exempt": result["exempt"],
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


def open_tiles(rig):
    """The tile page, from wherever the shell is: open (a no-op while open),
    then pop back to depth 0 if a category is showing."""
    rig.settingsui(open=1)
    for _ in range(4):
        state = rig.settingsui_state()
        if not state.get("open"):
            rig.settingsui(open=1)
            continue
        if int(state.get("depth", 0)) <= 0:
            return rig.touchmap(screen=0)
        rig.settingsui(pop=1)
    raise RuntimeError("could not reach the settings tile page: %r" % rig.settingsui_state())


def open_category_page(rig, cat, page):
    """Opens category `cat` at `page` and returns the dump. cat while a
    different category is showing pops the old one first (the route's own
    behaviour), so this does not need to unwind by hand."""
    open_tiles(rig)
    rig.settingsui(cat=cat)
    if page:
        rig.settingsui(page=page)
    state = rig.settingsui_state()
    if int(state.get("category", -1)) != cat or int(state.get("page", -1)) != page:
        raise RuntimeError("settingsui did not land on cat=%d page=%d: %r" % (cat, page, state))
    return rig.touchmap(screen=0)


def open_schedule_list(rig):
    """Taps the Machine page's Schedules row. Returns (dump, state)."""
    dump = open_category_page(rig, CAT_MACHINE, 0)
    row = find_tag(dump, "Schedules", "action")
    if row is None:
        raise RuntimeError("no Schedules row on the Machine page: %r" % rows_on_page(dump))
    rig.tap_target(row)
    state = rig.settingsui_state()
    if int(state.get("depth", 0)) != 2:
        raise RuntimeError("tapping Schedules did not push the list: %r" % state)
    return rig.touchmap(screen=0), state


def open_schedule_editor(rig, n=1):
    """Taps schedule `n` (1-based) in the list. Returns (dump, state)."""
    dump, _ = open_schedule_list(rig)
    row = find_tag(dump, "Schedule %d" % n, "action")
    if row is None:
        raise RuntimeError("no Schedule %d row in the list: %r" % (n, rows_on_page(dump)))
    rig.tap_target(row)
    state = rig.settingsui_state()
    if int(state.get("depth", 0)) != 3:
        raise RuntimeError("tapping Schedule %d did not push the editor: %r" % (n, state))
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
