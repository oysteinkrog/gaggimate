# Device parity for every animation kernel: band() against its own portable
# bandRef() twin, on the board, through /api/debug/animtest (loadtest and
# bench builds only). mismatch_px must be 0 for every animation.
#
#   usage: python3 tools/anim_parity.py [ids...]      (default: all 44)
#          FRAMES=8 HOST=192.168.1.121 python3 tools/anim_parity.py 14 15
#
# Why the board and not the host: the host cannot show this class of fault.
# glibc malloc is 16-byte aligned, so a kernel whose vector stores run off the
# front of a 4-byte aligned ps_malloc block passes the goldens, the ASan fuzz,
# the lifecycle check and QEMU, and still corrupts the device heap (Truchet's
# blendLast, 2026-09-12). This endpoint is what found that one.
#
# The endpoint renders `frames` frames across 3 parameter sets through band()
# and bandRef() back to back, alternating which runs first band by band, and
# reports the first differing pixel. An animation whose bandRef slot is null
# has no second path: it reports has_ref false and is listed as such here,
# never counted as a pass.
import json
import os
import sys
import time
import urllib.request

HOST = os.environ.get("HOST", "192.168.1.121")
FRAMES = int(os.environ.get("FRAMES", "8"))
NAMES = [
    "plasma", "lava", "silk", "starfield", "aurora", "ripples", "caustics", "mandala",
    "orbits", "fireflies", "steam", "ember", "nebula", "silk2", "brushed", "horizon",
    "oculus", "chevrons", "mosaic", "saddle", "refraction", "sundial", "crescent", "glint",
    "tunnel", "kaleido", "shafts", "weave", "lens", "tide", "truchet", "quilt",
    "rain", "stripes", "ribbon", "harmonograph", "floor", "hills", "gyroid", "barrel",
    "grid", "cells", "dimples", "cube",
]
ids = [int(a) for a in sys.argv[1:]] or list(range(len(NAMES)))


def get(path, tries=4, timeout=30):
    for k in range(tries):
        try:
            return json.load(urllib.request.urlopen("http://%s%s" % (HOST, path), timeout=timeout))
        except Exception:
            if k == tries - 1:
                raise
            time.sleep(1.5)


def run_one(a):
    before = get("/api/debug/animtest")
    seq0 = before.get("seq", 0)
    get("/api/debug/animtest?anim=%d&frames=%d" % (a, FRAMES))
    # The render task runs the test between frames, so the result arrives a
    # few frames later. Poll for a new even seq rather than sleeping blind.
    deadline = time.time() + 120
    while time.time() < deadline:
        time.sleep(0.7)
        r = get("/api/debug/animtest")
        if not r.get("pending") and r.get("seq", 0) != seq0 and r.get("seq", 0) % 2 == 0 and r.get("anim") == a:
            return r
    raise SystemExit("anim %d: no result within 120 s" % a)


print("build/pclk: %s" % json.dumps(get("/api/debug/pclk")), flush=True)
print("%-3s %-13s %-7s %-6s %-6s %-11s %-9s %-9s %s" %
      ("id", "name", "has_ref", "bands", "mism", "first", "band_us", "ref_us", "ratio"), flush=True)
bad, noref = [], []
for a in ids:
    r = run_one(a)
    name = NAMES[a] if a < len(NAMES) else str(a)
    if not r.get("has_ref"):
        noref.append(name)
        print("%-3d %-13s %-7s %-6s %-6s %-11s %-9s %-9s %s" % (a, name, "NO", "-", "-", "-", "-", "-", "-"), flush=True)
        continue
    if r.get("init_failed"):
        bad.append((a, name, "init_failed"))
        print("%-3d %-13s %-7s %-6s %-6s %-11s %-9s %-9s %s" % (a, name, "yes", "-", "INIT", "-", "-", "-", "-"), flush=True)
        continue
    m = r.get("mismatch_px", 0)
    f = r.get("first", {})
    first = "-" if m == 0 else "f%d/p%d %d,%d %04x!=%04x" % (
        f.get("frame", -1), f.get("pset", -1), f.get("x", -1), f.get("y", -1), f.get("got", 0), f.get("want", 0))
    bu, ru = r.get("band_us", 0), r.get("ref_us", 0)
    ratio = ("%.2f" % (ru / bu)) if bu else "-"
    print("%-3d %-13s %-7s %-6d %-6d %-11s %-9d %-9d %s" % (a, name, "yes", r.get("bands", 0), m, first, bu, ru, ratio), flush=True)
    if m:
        bad.append((a, name, first))

print("", flush=True)
print("checked %d, no reference path: %s" % (len(ids), ", ".join(noref) if noref else "none"), flush=True)
if bad:
    print("FAIL: %s" % ", ".join("%d %s (%s)" % b for b in bad), flush=True)
    sys.exit(1)
print("PASS: every animation with a reference path reported mismatch_px 0", flush=True)
