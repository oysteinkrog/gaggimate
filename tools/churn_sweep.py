#!/usr/bin/env python3
"""Overlay refresh rate per screen at rest (gm-2cl.17, gm-2cl.18).

Loads each screen through /api/debug/touchmap, waits 4 s, then counts
ov_refreshes on /api/debug/anim over a window and lists what the render
task owns on that screen (icon layers, text elements, dial rings). A
screen at rest should read 0.00 a second; anything else is an LVGL pass
some widget still costs. Loadtest build.

    python3 tools/churn_sweep.py [--host 192.168.1.121] [--screens 1,2,3,5,6,7,8,9,10,11] [--window 15]
"""
import argparse
import os
import sys
import time
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_ui_tests"))
from rig import Rig  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--screens", default="1,2,3,5,6,7,8,9,10,11")
    ap.add_argument("--window", type=float, default=15.0)
    args = ap.parse_args()
    rig = Rig(args.host)
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
        a0 = rig.get_json("/api/debug/anim")
        time.sleep(args.window)
        a1 = rig.get_json("/api/debug/anim")
        dt = (a1["uptime_ms"] - a0["uptime_ms"]) / 1000.0
        print("screen %2d: refreshes %.2f/s fps %.1f ov_px %6d icon_layers %d marquees %d text_elems %d rings %d" % (
            sid, (a1["ov_refreshes"] - a0["ov_refreshes"]) / dt, (a1["anim_frames"] - a0["anim_frames"]) / dt,
            a1.get("ov_px", 0), len(a1.get("icon_layers", [])), len(a1.get("marquee_layers", [])),
            len(a1.get("text_elems", [])), a1.get("elem_rings", 0)))


if __name__ == "__main__":
    main()
