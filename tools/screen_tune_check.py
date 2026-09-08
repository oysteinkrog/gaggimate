#!/usr/bin/env python3
"""Generated-screen tuning A/B (gm-2cl.19): what zoomfix and clipcorner cost
and whether the pixel-exact mode (clipcorner=2) draws the same overlay.

For each knob setting the page is loaded from another page and the whole
snapshot's draw time is read (ov_whole_draw_us, the LVGL render into the
overlay). Then the overlay is dumped three times per mode and the pixel-exact
mode is compared with the generated one on the pixels that were stable in
both. Loadtest build. Usage:

    python3 tools/screen_tune_check.py [--host 192.168.1.121] [--screen 11]
        [--from 8] [--rounds 3]
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_ui_tests"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rig import Rig  # noqa: E402
from snapshot_clear_check import compare_stable, dumps  # noqa: E402

MODES = [
    ("generated", {"zoomfix": 0, "clipcorner": 1}),
    ("zoomfix", {"zoomfix": 1, "clipcorner": 1}),
    ("zoomfix+clip2", {"zoomfix": 1, "clipcorner": 2}),
    ("zoomfix+clip0", {"zoomfix": 1, "clipcorner": 0}),
]


def load_measure(rig, sid, other, rounds):
    draws = []
    for _ in range(rounds):
        rig.touchmap(screen=other, load=True)
        time.sleep(2.0)
        rig.touchmap(screen=sid, load=True)
        time.sleep(2.5)
        a = rig.anim()
        draws.append((a.get("ov_whole_draw_us") or 0) / 1000.0)
    return draws


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--screen", type=int, default=11)
    ap.add_argument("--from", dest="other", type=int, default=8)
    ap.add_argument("--rounds", type=int, default=3)
    args = ap.parse_args()
    rig = Rig(args.host)
    rig.synth(brew=0)
    caps = {}
    for name, knobs in MODES:
        rig.anim(**knobs)
        draws = load_measure(rig, args.screen, args.other, args.rounds)
        print("%-14s draw ms: %s  min %.1f" % (name, " ".join("%.1f" % d for d in draws), min(draws)))
        if name in ("generated", "zoomfix+clip2"):
            caps[name] = dumps(args.host)
    diffs = compare_stable(caps["zoomfix+clip2"], caps["generated"], "clip2 vs generated")
    # Leave the defaults behind.
    rig.anim(zoomfix=1, clipcorner=2)
    print("RESULT", "PASS" if diffs == 0 else "FAIL")
    return 0 if diffs == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
