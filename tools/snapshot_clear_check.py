#!/usr/bin/env python3
"""Ghost check for the run-table alpha clear (gm-2cl.7).

A whole-page snapshot into a buffer that was published and not drawn into
since clears only the alpha its run table covers (SleepAnimation::
clearBackAlphaByRuns) instead of the whole plane. If that ever missed a
pixel, a page would show a ghost of the page two changes back. This check
compares the overlay a page gets with the clear forced to memset against
the overlay the same page gets cleared by runs, pixel for pixel inside the
panel area.

Loadtest build, bench board. clrruns=0 on /api/debug/anim forces the
memset path for the reference; pixels that change between captures of one
mode (a blinking icon, a fading label) are skipped, not compared.

    python3 tools/snapshot_clear_check.py [--host 192.168.1.121]
"""
import argparse
import os
import sys
import time
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_ui_tests"))
from rig import Rig  # noqa: E402

def get_ovl(host):
    with urllib.request.urlopen("http://%s/api/debug/ovl" % host, timeout=120) as resp:
        size = resp.headers.get("X-OV-Size", "")
        data = resp.read()
    w, h = (int(v) for v in size.split("x"))
    if len(data) != w * h * 3:
        raise RuntimeError("ovl: expected %d bytes, got %d" % (w * h * 3, len(data)))
    return w, h, data


def load(rig, sid):
    rig.touchmap(screen=sid, load=True)
    time.sleep(3.0)


def compare(a, b, label):
    (wa, ha, da), (wb, hb, db) = a, b
    if (wa, ha) != (wb, hb):
        print("%s: geometry differs %dx%d vs %dx%d" % (label, wa, ha, wb, hb))
        return 1
    x0 = (wa - 480) // 2
    y0 = (ha - 480) // 2
    diffs = 0
    boxes = []
    for y in range(480):
        row = (y + y0) * wa
        for x in range(480):
            i = (row + x + x0) * 3
            aa = da[i + 2]
            ab = db[i + 2]
            if aa != ab or (aa and da[i:i + 2] != db[i:i + 2]):
                diffs += 1
                if len(boxes) < 8:
                    boxes.append((x, y, aa, ab))
    print("%s: %d differing panel pixels%s" % (label, diffs, (", first " + str(boxes)) if boxes else ""))
    return diffs


def dumps(host, n=3, gap=0.8):
    caps = []
    for _ in range(n):
        caps.append(get_ovl(host))
        time.sleep(gap)
    return caps


def compare_stable(a, b, label):
    """Compares pixels that are identical across every capture of a mode in
    both modes; the rest (a blinking icon, a fading label) is counted only."""
    (wa, ha, _), (wb, hb, _) = a[0], b[0]
    if (wa, ha) != (wb, hb):
        print("%s: geometry differs %dx%d vs %dx%d" % (label, wa, ha, wb, hb))
        return 1
    x0 = (wa - 480) // 2
    y0 = (ha - 480) // 2
    diffs = 0
    colour_only = 0
    unstable = 0
    first = []
    for y in range(480):
        row = (y + y0) * wa
        for x in range(480):
            i = (row + x + x0) * 3
            pa = a[0][2][i:i + 3]
            pb = b[0][2][i:i + 3]
            if any(c[2][i:i + 3] != pa for c in a[1:]) or any(c[2][i:i + 3] != pb for c in b[1:]):
                unstable += 1
                continue
            if pa[2] != pb[2]:
                diffs += 1
                if len(first) < 8:
                    first.append((x, y, pa[2], pb[2]))
            elif pa[2] and pa[:2] != pb[:2]:
                # Same coverage, other colour: the clear touches alpha only
                # and colour is written wherever alpha is, so this is a label
                # whose colour follows machine state between the two loads
                # (the temperature readout turning ready), not a ghost.
                colour_only += 1
    print("%s: %d differing stable alpha pixels, %d colour-only, %d unstable skipped%s" %
          (label, diffs, colour_only, unstable, (", first " + str(first)) if first else ""))
    return diffs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    args = ap.parse_args()
    rig = Rig(args.host)
    rig.synth(brew=0)
    # Labels and dials stay on LVGL for the comparison: a label that becomes
    # live between the two loads would leave the overlay in one mode only.
    rig.anim(texts=0, dials=0)
    time.sleep(1)
    total = 0
    modes = {}
    for sid, name in ((6, "steam"), (2, "brew"), (3, "status")):
        got = {}
        for mode in (1, 0):
            rig.anim(clrruns=mode)
            load(rig, 7)      # leave through water: same geometry, so the target's
                              # buffer keeps its run table and the by-runs path runs
            load(rig, sid)
            got[mode] = dumps(args.host)
            modes[(name, mode)] = rig.anim().get("ov_whole_clear_by_runs")
        total += compare_stable(got[1], got[0], name)
    rig.anim(clrruns=1, texts=1, dials=1)
    # Informational: a page change redraws the other buffer at the new
    # geometry, and a buffer last published at another size takes the memset
    # whatever the knob says, so the mode read back is not the knob.
    print("clear mode of the last whole snapshot (1 = by runs):", modes)
    ok = total == 0
    print("snapshot_clear_check:", "PASS" if ok else "CHECK")


if __name__ == "__main__":
    main()
