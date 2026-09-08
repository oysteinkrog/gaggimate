#!/usr/bin/env python3
"""Scrolling label as a looping layer: same picture, same speed, no refreshes? (gm-2cl.18)

Loads the info screen (its 250x21 label scrolls) and, for marquees=1 and
marquees=0, counts ov_refreshes over the sampling window and reads the
label box from /api/debug/fb (step 2) ten times. Each sample's column
brightness profile is matched against every sample of the other mode
over all horizontal shifts: the same text at another phase matches to a
mean error of a few units per column, a different picture does not. The
box row profile (vertical brightness) is printed for both modes too.
Loadtest build.

    python3 tools/marquee_check.py [--host 192.168.1.121] [--screen 10] [--box 115,100,250,21]
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_ui_tests"))
from rig import Rig  # noqa: E402


def read_fb(rig):
    with rig._open("/api/debug/fb?step=2", timeout=60) as resp:  # noqa: SLF001
        return resp.read()


def box(data, x0, y0, w, h):
    rows = []
    for y in range(y0 // 2, (y0 + h) // 2):
        row = []
        for x in range(x0 // 2, (x0 + w) // 2):
            i = (y * 240 + x) * 2
            v = data[i] | (data[i + 1] << 8)
            row.append(((v >> 11) & 31, (v >> 5) & 63, v & 31))
        rows.append(row)
    return rows


def rowprofile(b):
    return [round(sum(sum(p) for p in r) / len(r), 1) for r in b]


def colprofile(b):
    return [sum(sum(b[y][x]) for y in range(len(b))) for x in range(len(b[0]))]


def best(a, b):
    res = None
    for d in range(-60, 61):
        err = 0
        n = 0
        for i in range(len(a)):
            j = i + d
            if 0 <= j < len(b):
                err += abs(a[i] - b[j])
                n += 1
        if n > 50 and (res is None or err / n < res[1]):
            res = (d, round(err / n, 1))
    return res


def bestany(a, others):
    r = None
    for o in others:
        b = best(a, o)
        if b and (r is None or b[1] < r[1]):
            r = b
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--screen", type=int, default=10)
    ap.add_argument("--box", default="115,100,250,21")
    args = ap.parse_args()
    x0, y0, w, h = (int(v) for v in args.box.split(","))
    rig = Rig(args.host)
    rig.touchmap(screen=args.screen, load=True)
    time.sleep(4)
    sets = {}
    for marq in (1, 0):
        rig.anim(marquees=marq)
        time.sleep(3)
        a0 = rig.get_json("/api/debug/anim")
        boxes = [box(read_fb(rig), x0, y0, w, h) for _ in range(10)]
        a1 = rig.get_json("/api/debug/anim")
        dt = (a1["uptime_ms"] - a0["uptime_ms"]) / 1000.0
        sets[marq] = [colprofile(b) for b in boxes]
        print("marquees=%d: refreshes %.2f/s over %.1f s, marquee_layers %s" % (
            marq, (a1["ov_refreshes"] - a0["ov_refreshes"]) / dt, dt, a1.get("marquee_layers")))
        print("   row profile:", rowprofile(boxes[0]))
    print("layer sample -> best LVGL sample (shift in half pixels, mean error):",
          [bestany(a, sets[0]) for a in sets[1]])
    print("LVGL sample -> best other LVGL sample:",
          [bestany(a, [o for o in sets[0] if o is not a]) for a in sets[0]])
    rig.anim(marquees=1)


if __name__ == "__main__":
    main()
