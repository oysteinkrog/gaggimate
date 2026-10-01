#!/usr/bin/env python3
"""Framebuffer PNGs of named screens, from the simulator or a board.

For judging layout changes by eye. The simulator draws the same LVGL tree the
device does, so widget geometry is exactly what ships; what it does not draw is
the background animation, so a shot here shows the widgets over the plain screen
background rather than over a gradient.

    python3 tools/screen_shots.py --out shots/before
    python3 tools/screen_shots.py --out shots/after --screens brew,steam,water
    python3 tools/screen_shots.py --host 192.168.1.121 --out shots/device

--scale opens the runtime-built scale cover (grind screen plus the cover) and
shoots that too, since it is reachable only from the menu on a real device.
"""
import argparse
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_ui_tests"))
from rig import Rig, Sim  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_PROGRAM = os.path.join(REPO_ROOT, ".pio", "build", "display-sim", "program")

SCREENS = {
    "standby": 1, "brew": 2, "status": 3, "menu": 5, "steam": 6,
    "water": 7, "profile": 8, "grind": 9, "info": 10, "new_profile": 11,
}


def shoot(rig, name, sid, out_dir, step):
    for _ in range(3):
        try:
            rig.touchmap(screen=sid, load=True)
        except Exception as e:  # noqa: BLE001
            print("  %-12s load failed: %s" % (name, e))
            return False
        time.sleep(1.5)
        if rig.touchmap().get("screen_id") == sid:
            break
    else:
        print("  %-12s could not hold it" % name)
        return False
    path = os.path.join(out_dir, "%s.png" % name)
    rig.fb_png(path, step=step)
    print("  %-12s %s" % (name, path))
    return True


def shoot_scale(rig, out_dir, step):
    """The scale cover is built at runtime over the grind screen and is opened
    from the menu, so /api/debug/scalescreen stands in for the finger. On a
    build that also carries the synthetic scale, give it a weight first: the
    readout is a fixed 170 px box, so the digits show how a real number sits
    in it rather than how a zero does."""
    try:
        rig.get_json("/api/debug/scale?ramp=0.8")
    except Exception:  # noqa: BLE001
        pass  # no synthetic scale here; the cover still draws
    try:
        rig.get_json("/api/debug/scalescreen?on=1")
    except Exception as e:  # noqa: BLE001
        print("  scale        not available here: %s" % e)
        return False
    time.sleep(3)
    path = os.path.join(out_dir, "scale.png")
    rig.fb_png(path, step=step)
    print("  %-12s %s" % ("scale", path))
    for q in ("/api/debug/scale?ramp=0", "/api/debug/scalescreen?on=0"):
        try:
            rig.get_json(q)
        except Exception:  # noqa: BLE001
            pass
    return True


def run(rig, names, out_dir, step, want_scale):
    os.makedirs(out_dir, exist_ok=True)
    for name in names:
        shoot(rig, name, SCREENS[name], out_dir, step)
    if want_scale:
        shoot_scale(rig, out_dir, step)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", help="drive a board instead of launching the simulator")
    ap.add_argument("--out", required=True)
    ap.add_argument("--screens", default="brew,steam,water,grind")
    ap.add_argument("--step", type=int, default=1, help="1 is full 480x480")
    ap.add_argument("--scale", action="store_true", help="also shoot the scale cover")
    ap.add_argument("--sim-program", default=DEFAULT_PROGRAM)
    ap.add_argument("--sim-port", type=int, default=int(os.environ.get("GM_SIM_PORT", "8086")))
    args = ap.parse_args()

    names = [n.strip() for n in args.screens.split(",") if n.strip()]
    bad = [n for n in names if n not in SCREENS]
    if bad:
        print("unknown screen(s): %s; known: %s" % (", ".join(bad), ", ".join(SCREENS)))
        return 2
    out_dir = os.path.abspath(args.out)

    if args.host:
        run(Rig(args.host), names, out_dir, args.step, args.scale)
        return 0

    if not os.path.isfile(args.sim_program):
        print("no simulator at %r; build it first: pio run -e display-sim" % args.sim_program)
        return 2
    # A fresh sim_data every run, so a shot never depends on settings some
    # earlier run happened to leave behind.
    workdir = os.path.join(out_dir, "_sim")
    data_dir = os.path.join(workdir, "sim_data")
    shutil.rmtree(workdir, ignore_errors=True)
    os.makedirs(data_dir, exist_ok=True)
    with Sim(args.sim_program, data_dir, port=args.sim_port) as sim:
        run(sim.rig, names, out_dir, args.step, args.scale)
    return 0


if __name__ == "__main__":
    sys.exit(main())
