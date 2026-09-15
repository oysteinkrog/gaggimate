#!/usr/bin/env python3
"""Overlay refresh rate per screen, and what the render task owns there.

Loads each screen through /api/debug/touchmap, waits 4 s, then counts
ov_refreshes on /api/debug/anim over a window and lists what the render
task owns on that screen (icon layers, text elements, dial rings). A
screen should read 0.00 a second; anything else is an LVGL pass some
widget still costs. Loadtest build.

Two things the first version left out, both of which cost a reading
(2026-09-15).

A rate of 0.00 says nothing unless a value was changing during the
window: a screen whose readouts are frozen publishes nothing for a
reason that has nothing to do with the design. So every visible label's
text is read at both ends of the window and the "moving" column says
whether any of them changed. Read a 0.00 next to a "no" as "not
measured". The temperature and pressure ramp runs even with the
synthetic brew lifecycle off (/api/debug/synth?brew=0), and a weight
readout needs /api/debug/scale?ramp=0.5 as well, which brew and grind
are the only screens to carry.

And a label the compositor has stopped owning falls back to LVGL and
invalidates its own box on every value, which is a rate with no visible
cause. text_dbg carries the counters that name it: takes, refusals
(a glyph shape or font the atlas cannot carry) and releases. Both
refusal counters are sticky for the rest of a screen visit, so a single
failure explains a screen that churns for as long as it is up. atlas
reports the glyph arena, since a full arena is one way a refusal
happens (32 KB a font, 8 fonts; it never evicts, so it only fills).

    python3 tools/churn_sweep.py [--host 192.168.1.121] [--screens 1,2,3,5,6,7,8,9,10,11] [--window 15]
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_ui_tests"))
from rig import Rig  # noqa: E402


def visible_label_texts(rig, sid):
    """Every visible label's text on the screen, as one comparable blob."""
    tm = rig.touchmap()
    if tm.get("screen_id") != sid:
        return None
    return "|".join(o.get("t", "") for o in tm.get("objects", [])
                    if o.get("c") == "label" and not o.get("hidden"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--screens", default="1,2,3,5,6,7,8,9,10,11")
    ap.add_argument("--window", type=float, default=15.0)
    args = ap.parse_args()
    rig = Rig(args.host)

    # Three small GETs, because a board in the gm-t9ld state answers slowly and
    # every number taken from it is suspect (CLAUDE.md, Bench facts). One
    # sample misses it about a quarter of the time.
    for _ in range(3):
        t0 = time.time()
        rig.get_json("/api/debug/anim")
        if time.time() - t0 > 1.0:
            print("a GET took over a second: the board is in the gm-t9ld state, reset it first")
            return 1

    for sid in [int(x) for x in args.screens.split(",")]:
        held = False
        tm = {}
        for _ in range(2):
            try:
                rig.touchmap(screen=sid, load=True)
            except Exception as e:  # noqa: BLE001
                print("load", sid, e)
                break
            time.sleep(4)
            tm = rig.touchmap()
            if tm.get("screen_id") == sid:
                held = True
                break
        if not held:
            print("screen %d: could not hold it (now %s)" % (sid, tm.get("screen_id")))
            continue
        before = visible_label_texts(rig, sid)
        a0 = rig.get_json("/api/debug/anim")
        time.sleep(args.window)
        a1 = rig.get_json("/api/debug/anim")
        after = visible_label_texts(rig, sid)
        dt = (a1["uptime_ms"] - a0["uptime_ms"]) / 1000.0
        d0 = a0.get("text_dbg", [0] * 8)
        d1 = a1.get("text_dbg", [0] * 8)
        moving = "?" if (before is None or after is None) else ("yes" if before != after else "no")
        print("screen %2d: refreshes %.2f/s fps %.1f ov_px %6d icon_layers %d marquees %d text_elems %d rings %d"
              "  moving %-3s takes %d refused %d released %d atlas %d B / %d glyphs" % (
                  sid, (a1["ov_refreshes"] - a0["ov_refreshes"]) / dt,
                  (a1["anim_frames"] - a0["anim_frames"]) / dt, a1.get("ov_px", 0),
                  len(a1.get("icon_layers", [])), len(a1.get("marquee_layers", [])),
                  len(a1.get("text_elems", [])), a1.get("elem_rings", 0), moving,
                  d1[0] - d0[0], d1[2] - d0[2], d1[4] - d0[4],
                  a1.get("atlas_bytes", 0), a1.get("atlas_glyphs", 0)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
