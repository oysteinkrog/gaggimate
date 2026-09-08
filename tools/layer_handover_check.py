#!/usr/bin/env python3
"""Layer handover check (gm-2cl.8): the uianim=3 motion test slides a
120 px orange plate back and forth through a compositor layer. This grabs
the framebuffer at random moments and counts the plate's pixels. One
plate is one count; a duplicate (LVGL copy and layer both on glass) reads
about twice it, a missing plate reads about zero. Also reports the loop
rate with the test running against the rate before it.

Loadtest build. Usage: python3 tools/layer_handover_check.py [--host H]
[--probes 20]. Leaves uianim off.
"""
import argparse
import os
import random
import statistics
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_ui_tests"))
from rig import Rig  # noqa: E402

PLATE = (0xF4, 0xA2, 0x61)  # DefaultUI.cpp uianim plate, lv_color_hex(0xF4A261)


def plate_pixels(fb, w, h):
    n = 0
    for i in range(w * h):
        v = fb[2 * i] | (fb[2 * i + 1] << 8)
        r = (v >> 11) << 3
        g = ((v >> 5) & 0x3F) << 2
        b = (v & 0x1F) << 3
        if abs(r - PLATE[0]) < 28 and abs(g - PLATE[1]) < 28 and abs(b - PLATE[2]) < 28:
            n += 1
    return n


def grab(rig, step=2):
    data = rig.get_bytes("/api/debug/fb?step=%d" % step, timeout=60)
    side = 480 // step
    if len(data) < side * side * 2:
        raise RuntimeError("short framebuffer: %d bytes" % len(data))
    return plate_pixels(data, side, side)


def fps_over(rig, secs):
    a0 = rig.anim()
    time.sleep(secs)
    a1 = rig.anim()
    return (a1["anim_frames"] - a0["anim_frames"]) / max(1e-3, (a1["uptime_ms"] - a0["uptime_ms"]) / 1000.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--probes", type=int, default=20)
    args = ap.parse_args()
    rig = Rig(args.host)
    # Whole-frame path on purpose: with interlace pinned the framebuffer
    # dump does not show the rows the direct path pushed (the plate read as
    # zero pixels in every probe on 2026-09-08), so the check runs on the
    # board's stored path and says so.
    rig.anim(interlace=-1)
    try:
        rig.synth(brew=0)
    except Exception:  # noqa: BLE001
        pass
    # Standby screen: the test plate is created on the active screen.
    try:
        rig.touchmap(screen=1, load=True)
    except Exception as e:  # noqa: BLE001
        print("standby load:", e, file=sys.stderr)
    time.sleep(3)
    base_fps = fps_over(rig, 6)
    background = grab(rig)
    # Parked plate through a layer (uianim=4) for the one-plate reference.
    rig.anim(uianim=4)
    time.sleep(2.5)
    ref = grab(rig) - background
    print("layers while parked:", rig.anim().get("layers"))
    rig.anim(uianim=0)
    time.sleep(1.5)
    print("plate reference %d px at step 2 (background %d), baseline %.1f fps, interlace %s"
          % (ref, background, base_fps, rig.anim().get("interlace")))
    rig.anim(uianim=3)
    time.sleep(1.0)
    counts = []
    t_end = time.time() + 30
    for i in range(args.probes):
        time.sleep(random.uniform(0.05, 0.9))
        c = grab(rig) - background
        counts.append(c)
        print("  probe %2d: %5d px (%.2f of one plate)" % (i + 1, c, c / max(1, ref)), flush=True)
    test_fps = fps_over(rig, 6)
    rig.anim(uianim=0)
    ratios = [c / max(1, ref) for c in counts]
    lo, hi = min(ratios), max(ratios)
    # A moving plate loses a little to motion between the two interlaced
    # fields and to the label; 0.6 to 1.4 is one plate, under 0.3 none, over
    # 1.6 two.
    verdict = "PASS" if 0.6 <= lo and hi <= 1.4 else "FAIL"
    print("%s: %d probes, plate ratio min %.2f median %.2f max %.2f; fps %.1f with the test against %.1f without"
          % (verdict, len(counts), lo, statistics.median(ratios), hi, test_fps, base_fps))
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
