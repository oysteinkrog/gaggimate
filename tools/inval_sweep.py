#!/usr/bin/env python3
"""Who invalidates what, per screen, while the values are moving (gm-61v).

churn_sweep.py answers a different question: how much a screen costs at rest.
It reads 0.00 refreshes a second on nearly every screen and still misses the
whole class of bug this exists for, because a widget that invalidates the whole
page on every value only does it while a value is changing. So this sweep turns
the synthetic brew and the synthetic scale on first, then visits each screen and
reads the inval_src ring: the object, its class, both rectangles and
lv_obj_invalidate's own return address for every invalidation at or above
--invmin. Addresses are resolved against the build's ELF, so a line of output
names a call site in LVGL or in our own code.

What to look for. A readout redrawing its own box is the design working. A
whole-page rectangle on a screen that is only showing a number is not, and the
caller says why: lv_obj_clear_flag means something re-asserts a hidden flag
every pass, lv_obj_refresh_style means a style is being written whether or not
it changed, a widget setter means an owned element is not owned after all.

Loadtest build (the ring is GM_TOUCH_PROBE only, and both synthetic sources
need GM_SYNTH_HANDSHAKE).

    python3 tools/inval_sweep.py [--host 192.168.1.121] [--window 20] [--invmin 200]
"""
import argparse
import collections
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_ui_tests"))
from rig import Rig  # noqa: E402

ELF = ".pio/build/display-loadtest/firmware.elf"
ADDR2LINE = os.path.expanduser(
    "~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line")


def resolve(addrs):
    """Map return addresses to source locations, or {} if the tools are absent."""
    addrs = sorted(a for a in addrs if a)
    if not addrs or not os.path.exists(ELF) or not os.path.exists(ADDR2LINE):
        return {}
    out = subprocess.run([ADDR2LINE, "-pfiaC", "-e", ELF] + ["0x%08x" % a for a in addrs],
                         capture_output=True, text=True).stdout
    got = {}
    for line in out.splitlines():
        if not line.startswith("0x"):
            continue  # an "(inlined by)" continuation; the first line is enough
        addr, _, rest = line.partition(": ")
        fn = rest.split(" at ")[0]
        where = rest.split(" at ")[-1]
        got[int(addr, 16)] = "%s (%s)" % (fn, os.path.basename(where))
    return got


def sample(rig, seconds, seen):
    """Collect ring entries by their absolute index, so a poll cannot double-count."""
    t0 = time.time()
    first = rig.get_json("/api/debug/anim")
    start = first.get("inval_src_total", 0)
    while time.time() - t0 < seconds:
        d = rig.get_json("/api/debug/anim")
        total = d.get("inval_src_total", 0)
        ring = d.get("inval_src", [])
        base = total - len(ring)  # the ring is oldest first
        for i, e in enumerate(ring):
            if base + i >= start:
                seen[base + i] = e
        time.sleep(2.5)
    last = rig.get_json("/api/debug/anim")
    dt = time.time() - t0
    return last.get("inval_src_total", 0) - start, dt, first, last


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--screens", default="1,2,3,5,6,7,8,9,10,11")
    ap.add_argument("--window", type=float, default=20.0)
    ap.add_argument("--invmin", type=int, default=200,
                    help="smallest invalidation to record, pixels a side")
    ap.add_argument("--ramp", type=float, default=0.5, help="synthetic scale, grams a second")
    ap.add_argument("--no-load", action="store_true",
                    help="measure the screen the board is already on")
    args = ap.parse_args()
    rig = Rig(args.host)

    # Three small GETs first: a board whose radio has degraded in place answers
    # these in seconds and every number after it is noise (gm-t9ld).
    for _ in range(3):
        t = time.time()
        rig.get_json("/api/debug/anim")
        if time.time() - t > 1.0:
            print("board is answering slowly; reset it before measuring (gm-t9ld)")
            return 1

    rig.get_json("/api/debug/anim?invmin=%d" % args.invmin)
    rig.get_json("/api/debug/synth?brew=1")
    rig.get_json("/api/debug/scale?ramp=%g&tare=1" % args.ramp)
    print("invmin %d px, synthetic brew on, scale %g g/s, %.0f s a screen\n"
          % (args.invmin, args.ramp, args.window))

    addrs = set()
    rows = []
    for sid in [int(x) for x in args.screens.split(",")]:
        if not args.no_load:
            held = False
            for _ in range(2):
                try:
                    rig.touchmap(screen=sid, load=True)
                except Exception as e:  # noqa: BLE001
                    print("load %d: %s" % (sid, e))
                    break
                time.sleep(3)
                if rig.touchmap().get("screen_id") == sid:
                    held = True
                    break
            if not held:
                print("screen %2d: could not hold it" % sid)
                continue
        seen = {}
        total, dt, a0, a1 = sample(rig, args.window, seen)
        fps = (a1["anim_frames"] - a0["anim_frames"]) / dt
        pubs = (a1["ov_refreshes"] - a0["ov_refreshes"]) / dt
        print("screen %2d: %5.2f invalidations/s  %5.2f publishes/s  %4.1f fps"
              % (sid, total / dt, pubs, fps))
        g = collections.Counter()
        for e in seen.values():
            g[(e["cls"], tuple(e["area"]), e["caller"])] += 1
        for (cls, area, caller), n in g.most_common(6):
            w, h = area[2] - area[0] + 1, area[3] - area[1] + 1
            rows.append((sid, n, cls, w, h, area, caller))
            addrs.add(caller)

    names = resolve(addrs)
    print("\nsampled entries, per screen, most seen first")
    for sid, n, cls, w, h, area, caller in rows:
        print("  %2d x%-4d %-6s %3dx%-3d %-24s %s"
              % (sid, n, cls, w, h, str(area), names.get(caller, "0x%08x" % caller)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
