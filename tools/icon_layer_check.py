#!/usr/bin/env python3
"""Blinking icon as layer sprites: does it still blink, in the same colours? (gm-2cl.17)

Loads the brew screen, then for icons=1 and icons=0 reads /api/debug/fb
(step 2) fourteen times, 300 ms apart, and prints the mean colour of the
brightest third of the icon's box each time. Both modes should alternate
between the same two colours, and icon_layers should list the icon only
with icons=1. Loadtest build, synthetic brew on (the icon blinks while
the machine heats).

    python3 tools/icon_layer_check.py [--host 192.168.1.121] [--box 135,420,40,40]
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


def region(data, x0, y0, w, h):
    out = []
    for y in range(y0 // 2, (y0 + h) // 2):
        for x in range(x0 // 2, (x0 + w) // 2):
            i = (y * 240 + x) * 2
            v = data[i] | (data[i + 1] << 8)
            out.append(((v >> 11) & 31, (v >> 5) & 63, v & 31))
    return out


def mean(px):
    return tuple(round(sum(p[k] for p in px) / len(px), 1) for k in range(3))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--box", default="135,420,40,40")
    ap.add_argument("--screen", type=int, default=2)
    args = ap.parse_args()
    x0, y0, w, h = (int(v) for v in args.box.split(","))
    rig = Rig(args.host)
    rig.touchmap(screen=args.screen, load=True)
    time.sleep(4)
    for icons in (1, 0):
        rig.anim(icons=icons)
        time.sleep(2)
        seen = []
        for _ in range(14):
            r = region(read_fb(rig), x0, y0, w, h)
            r.sort(key=lambda p: p[0] + p[1] + p[2], reverse=True)
            seen.append(mean(r[: len(r) // 3]))
            time.sleep(0.3)
        a = rig.get_json("/api/debug/anim")
        print("icons=%d icon_layers=%s" % (icons, a.get("icon_layers")))
        print("  bright-third means:", seen)
    rig.anim(icons=1)


if __name__ == "__main__":
    main()
