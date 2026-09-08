#!/usr/bin/env python3
"""Touch-to-pixel latency on the bench board (gm-2cl.4).

Injects taps through /api/debug/tap while a serial capture runs, then reads
the GM_TOUCHLAT lines the loadtest build logs (LV_Helper.cpp,
DefaultUI.cpp, SleepAnimation.cpp):

    press->overlay_publish   the LVGL path: edge to the overlay publish
    press->anim_frame        edge to the present of the first frame that
                             carries the response (the element write or
                             the publish; the line says which)

and reports median and p90 per kind, plus how many rectangles LVGL
invalidated per tap (dirty_total on /api/debug/anim; the probe build itself
forces one 20x20 rect per edge, so two per tap is the floor there).

The serial capture is tools/rig_serial.py on Windows Python (the UART is a
COM port) and it never touches DTR/RTS, so the board is not reset.

    python3 tools/touch_lat.py [--host 192.168.1.121] [--taps 20]
        [--screen 2] [--busy] [--x X --y Y]

--busy runs the synthetic brew during the taps (telemetry refreshes and
dial elements live), the case the touch task exists for. Without --x/--y
the first plate-sized clickable target of the screen is tapped.
"""
import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_ui_tests"))
from rig import Rig  # noqa: E402

WIN_PY = os.environ.get("GM_RIG_PY", r"C:\Users\oystein\AppData\Local\Programs\Python\Python310\python.exe")
REPO_WIN = r"C:\work\gaggimate\.claude\worktrees\gm-2cl-6-dials"
LAT = re.compile(r"GM_TOUCHLAT: (press|release)->([a-z_]+)(\([a-z]+\))? (-?\d+) us")


def pick_target(rig, screen):
    d = rig.touchmap(screen=screen, load=True)
    for t in d.get("objects", []):
        hit = t.get("hit")
        if not hit:
            continue
        w = hit[2] - hit[0]
        h = hit[3] - hit[1]
        # k: clickable, d: depth, h/hidden: hidden anywhere up the chain (rig.annotate).
        if 30 <= w <= 200 and 30 <= h <= 200 and t.get("k") and not t.get("hidden"):
            return (hit[0] + hit[2]) // 2, (hit[1] + hit[3]) // 2, t.get("tag") or t.get("c") or "?"
    raise RuntimeError("no plate-sized target on screen %d" % screen)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--taps", type=int, default=20)
    ap.add_argument("--screen", type=int, default=2)
    ap.add_argument("--busy", action="store_true")
    ap.add_argument("--x", type=int)
    ap.add_argument("--y", type=int)
    ap.add_argument("--gap", type=float, default=0.7)
    ap.add_argument("--settings-cat", type=int, help="open the settings cover and this category first "
                    "(5 is the Fixture page; its toggle row at 240,254 is a harmless repeat target)")
    ap.add_argument("--uianim", action="store_true", help="run the uianim=1 motion test during the taps "
                    "(LVGL refreshing 4 to 5 times a second) instead of the synthetic brew")
    ap.add_argument("--out", default=os.path.join(os.environ.get("CLAUDE_JOB_DIR", "/tmp"), "tmp", "touch_lat.log"))
    args = ap.parse_args()
    rig = Rig(args.host)
    rig.synth(brew=1 if args.busy else 0)
    time.sleep(1.5)
    if args.settings_cat is not None:
        rig.touchmap(screen=5, load=True)
        time.sleep(2.0)
        rig.settingsui(open=1)
        time.sleep(1.5)
        rig.settingsui(cat=args.settings_cat)
        time.sleep(2.0)
        x, y, name = args.x, args.y, "settings cat %d" % args.settings_cat
        if x is None or y is None:
            raise SystemExit("--settings-cat needs --x and --y")
    elif args.x is None or args.y is None:
        x, y, name = pick_target(rig, args.screen)
    else:
        x, y, name = args.x, args.y, "given"
        rig.touchmap(screen=args.screen, load=True)
    if args.uianim:
        rig.anim(uianim=1)
    time.sleep(2.0)
    seconds = int(args.taps * (args.gap + 0.15) + 10)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    cmd = 'cmd.exe /c "%s %s\\tools\\rig_serial.py %d %s"' % (WIN_PY, REPO_WIN, seconds, args.port)
    with open(args.out, "w", encoding="utf-8", errors="replace") as out:
        proc = subprocess.Popen(cmd, shell=True, stdout=out, stderr=subprocess.DEVNULL)
        time.sleep(3.0)
        j0 = rig.anim()
        for i in range(args.taps):
            rig.get_json("/api/debug/tap?x=%d&y=%d&ms=80" % (x, y))
            time.sleep(args.gap)
        time.sleep(1.0)
        j1 = rig.anim()
        proc.wait(timeout=seconds + 30)
    rig.synth(brew=0)
    if args.uianim:
        rig.anim(uianim=0)
    if args.settings_cat is not None:
        try:
            rig.settingsui(close=1)
        except Exception:  # noqa: BLE001
            pass
    lat = {}
    with open(args.out, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LAT.search(line)
            if m:
                kind = m.group(1) + "->" + m.group(2) + (m.group(3) or "")
                lat.setdefault(kind, []).append(int(m.group(4)) / 1000.0)
    report = {"target": name, "x": x, "y": y, "screen": args.screen, "busy": args.busy, "uianim": args.uianim,
              "taps": args.taps,
              "fps": (j1["anim_frames"] - j0["anim_frames"]) / max(1e-3, (j1["uptime_ms"] - j0["uptime_ms"]) / 1000.0),
              "dirty_per_tap": ((j1.get("dirty_total") or 0) - (j0.get("dirty_total") or 0)) / float(args.taps),
              "refreshes_per_tap": ((j1.get("ov_refreshes") or 0) - (j0.get("ov_refreshes") or 0)) / float(args.taps),
              "kinds": {}}
    for kind, vals in sorted(lat.items()):
        vals.sort()
        report["kinds"][kind] = {"n": len(vals), "median_ms": round(statistics.median(vals), 1),
                                 "p90_ms": round(vals[min(len(vals) - 1, int(len(vals) * 0.9))], 1),
                                 "max_ms": round(vals[-1], 1)}
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
