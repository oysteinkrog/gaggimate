# The gradient picker checks that only a board can answer (gm-nov3.11).
#
# tools/settings_ui_tests/test_animation.py covers the picker on both venues.
# It could not answer two of these questions on a board when this script was
# written, and both causes are fixed now:
#
#   * Rig.rows_on_page() keyed a page's rows by name, so a library holding two
#     gradients with the same name read as one row. The bench board's library
#     does hold that pair (two entries both called "Custom"), and the check
#     that every saved gradient is reachable failed there for that reason
#     alone. gm-xiu6 keeps the repeats, so this script uses it rather than
#     walking the tagged objects itself.
#   * swatch_strip() reads /api/debug/fb?step=1, which used to come back as
#     4,800 of 460,800 bytes with a 200 and no error. That was the response
#     filler, not the heap and not the request count (gm-6ivh, 28cec8ec), and
#     every step is whole now, so the reads here are at step 1.
#
# What is left is the part that still needs a board: a real stored library,
# with the names its owner gave it, listed through the real panel.
#
# Usage (loadtest device only):
#
#   python3 tools/picker_device_checks.py --host 192.168.1.121
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.settings_ui_tests.rig import Rig  # noqa: E402

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
    keeping repeats, and the page each one landed on. Rig.rows_on_page()
    keeps repeats since gm-xiu6, so this is only the walk over the pages."""
    pages = int(rig.settingsui_state().get("pages", 1))
    out = []
    for page in range(pages):
        out.extend((page, name) for name in Rig.rows_on_page(goto_page(rig, page)))
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


def swatch_strip(rig, dump, row):
    """The row's swatch as RGB triples along the middle of its canvas, read
    from the full-resolution framebuffer. Rig.fb() raises on a body shorter
    than the X-FB-Size header promised, so a short read is a failure with
    both byte counts and never a narrower sample."""
    obj = rig.find_tag(dump, row, "swatch")
    if obj is None or obj.get("h"):
        return None
    w, _h, data = rig.fb(step=1)
    y = (obj["y1"] + obj["y2"]) // 2
    return [rgb565_pixel(data, x, y, w) for x in range(obj["x1"], obj["x2"] + 1)]


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

    # 2. A picker row's swatch really is a ramp.
    dump = goto_page(rig, 0)
    try:
        strip = swatch_strip(rig, dump, "Global")
        check("global_row_draws_a_ramp", strip is not None and len(set(strip)) > 4,
              "distinct colours: %r" % (None if strip is None else len(set(strip))))
        if strip:
            print("  swatch ends: %r ... %r, %d distinct" % (strip[0], strip[-1], len(set(strip))), flush=True)
    except Exception as e:  # noqa: BLE001 -- a short or missing dump is this check's failure
        check("global_row_draws_a_ramp", False, "%s: %s" % (type(e).__name__, e))

    # 3. The whole dump at step 1, recorded rather than assumed. This is the
    #    step the shared harness reads, and it returned 4,800 of 460,800
    #    bytes on this board until gm-6ivh.
    want = 480 * 480 * 2
    raw = rig.get_bytes("/api/debug/fb?step=1")
    check("fb_step1_is_whole", len(raw) == want, "%d of %d bytes" % (len(raw), want))
    print("  /api/debug/fb?step=1 delivered %d of %d bytes" % (len(raw), want), flush=True)

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
