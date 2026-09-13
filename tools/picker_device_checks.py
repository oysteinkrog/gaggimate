# The gradient picker checks that only a board can answer, and the two the
# shared harness cannot answer correctly on one (gm-nov3.11).
#
# tools/settings_ui_tests/test_animation.py covers the picker on both venues,
# but two of its helpers are written for the simulator:
#
#   * rows_across_pages() drops a repeated row name, so a library holding two
#     gradients with the same name reads as one. The bench board's library does
#     (two entries both called "Custom"), and the check that every saved
#     gradient is reachable failed there for that reason alone.
#   * swatch_strip() reads /api/debug/fb?step=1. This board answers that with a
#     480x480 header and 4,800 of the 460,800 bytes, every time, on a board one
#     minute out of reset, so the read runs off the end of the buffer and the
#     check raises instead of failing. step=2 is whole (gm-6ivh; CLAUDE.md's
#     settings section already recorded that the device only delivers step 2).
#
# This script asks both questions in a way that works on the board, so the
# device answer is on record without editing the shared harness (which another
# agent is changing at the same time).
#
# Usage (loadtest device only):
#
#   python3 tools/picker_device_checks.py --host 192.168.1.121
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.settings_ui_tests.rig import ROW_CONTAINER_ROLES, Rig, tag_role, tag_row  # noqa: E402

ANIMATION_CAT = 2

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


def goto_page(rig, page):
    rig.settingsui(page=page)
    rig.wait_until(lambda: rig.settingsui_state().get("page") == page, timeout=5)
    return rig.touchmap(screen=0)


def rows_with_repeats(rig):
    """Every row on the page on top, across all its pages, top to bottom,
    keeping repeats, and the page each one landed on.

    Rig.rows_on_page() cannot be used here: it collects rows into a dict keyed
    by row name, so two rows with the same name on one page come back as one.
    This walks the tagged objects instead, which is the same rule without the
    dict."""
    pages = int(rig.settingsui_state().get("pages", 1))
    out = []
    for page in range(pages):
        dump = goto_page(rig, page)
        rows = [(o.get("y1", 0), tag_row(o)) for o in dump["objects"]
                if tag_role(o) in ROW_CONTAINER_ROLES]
        out.extend((page, name) for _y, name in sorted(rows))
    return out


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


def tap_row(rig, name):
    dump = page_with_row(rig, name)
    target = rig.find_tag(dump, name, "action")
    if target is None:
        raise AssertionError("row %r is not a whole-row target" % name)
    before = depth(rig)
    rig.tap_target(target)
    rig.wait_until(lambda: depth(rig) == before + 1, timeout=10)


def library_names(rig):
    packed = rig.settings()["bgAnimGradients"] or ""
    out = []
    for entry in packed.split(";"):
        if not entry:
            continue
        parts = entry.split("|")
        if len(parts) >= 2:
            out.append(parts[1])
    return out


def rgb565_pixel(data, x, y, w):
    off = (y * w + x) * 2
    val = data[off] | (data[off + 1] << 8)
    r5, g6, b5 = (val >> 11) & 0x1F, (val >> 5) & 0x3F, val & 0x1F
    return ((r5 * 255) // 31, (g6 * 255) // 63, (b5 * 255) // 31)


def swatch_strip_step2(rig, dump, row):
    """The row's swatch as RGB triples along the middle of its canvas, read
    from the half-resolution framebuffer the device can actually deliver. The
    tag's coordinates are full-panel, so they are halved here."""
    obj = rig.find_tag(dump, row, "swatch")
    if obj is None or obj.get("h"):
        return None
    data = rig.get_bytes("/api/debug/fb?step=2")
    w = 240
    if len(data) < w * w * 2:
        raise AssertionError("/api/debug/fb?step=2 returned %d of %d bytes" % (len(data), w * w * 2))
    y = ((obj["y1"] + obj["y2"]) // 2) // 2
    x0, x1 = obj["x1"] // 2, obj["x2"] // 2
    return [rgb565_pixel(data, x, y, w) for x in range(x0, x1 + 1)]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="192.168.1.121")
    args = ap.parse_args(argv)

    rig = Rig(args.host)
    want = library_names(rig)
    print("stored library: %d entries, names %r" % (len(want), want), flush=True)

    # 1. Every saved gradient is listed, repeated names included.
    open_animation(rig)
    tap_row(rig, "Gradient")
    tap_row(rig, "My gradients")
    listed = rows_with_repeats(rig)
    got = [name for _page, name in listed]
    check("library_lists_every_entry", got == want, "got %r want %r" % (got, want))
    check("library_row_count", len(got) == len(want), "%d rows for %d entries" % (len(got), len(want)))
    # The last page of a full library is a partial one: twelve entries, five
    # to a page, so pages of 5, 5 and 2.
    per_page = {}
    for page, _name in listed:
        per_page[page] = per_page.get(page, 0) + 1
    counts = [per_page[p] for p in sorted(per_page)]
    check("library_pages_end_in_a_partial_page",
          len(counts) > 1 and counts[-1] < 5 and all(c == 5 for c in counts[:-1]), counts)
    print("  rows per page: %r" % counts, flush=True)
    rig.settingsui(pop=1)
    rig.wait_until(lambda: depth(rig) == 2, timeout=10)

    # 2. A picker row's swatch really is a ramp, read at the step the device
    #    delivers whole.
    dump = goto_page(rig, 0)
    try:
        strip = swatch_strip_step2(rig, dump, "Global")
        check("global_row_draws_a_ramp", strip is not None and len(set(strip)) > 4,
              "distinct colours: %r" % (None if strip is None else len(set(strip))))
        if strip:
            print("  swatch ends: %r ... %r, %d distinct" % (strip[0], strip[-1], len(set(strip))), flush=True)
    except AssertionError as e:
        check("global_row_draws_a_ramp", False, str(e))

    # 3. What the harness's own step tries to read, recorded rather than
    #    assumed, so the reason the shared check raises is on the record.
    raw = rig.get_bytes("/api/debug/fb?step=1")
    check("fb_step1_is_truncated_on_this_board", len(raw) < 480 * 480 * 2,
          "%d bytes, expected the endpoint to be short" % len(raw))
    print("  /api/debug/fb?step=1 delivered %d of %d bytes" % (len(raw), 480 * 480 * 2), flush=True)

    rig.settingsui(pop=1)
    rig.wait_until(lambda: depth(rig) == 1, timeout=10)
    rig.settingsui(close=1)
    rig.wait_until(lambda: rig.settingsui_state().get("open") is False, timeout=10)

    print("\n%s (%d checks, %d failed)" % ("PASS" if not FAILURES else "FAIL", TOTAL, len(FAILURES)),
          flush=True)
    for name in FAILURES:
        print("  failed: %s" % name, flush=True)
    return 0 if not FAILURES else 1


if __name__ == "__main__":
    sys.exit(main())
