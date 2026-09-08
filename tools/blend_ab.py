"""Production blend cost on the brew screen, for a before/after (gm-2cl.16).

Pins interlace on through the debug force, opens the brew screen at rest
(synthetic brew off) and samples blend_us, frame_us, elem_us and the frame
rate for three settings: the production blend, the same with the vector
kernel off (bpie=0), and probe level 1, which skips the overlay blend but
keeps the scrim pass and the row walk (so production minus probe 1 is what
the overlay pixels cost, and probe 1 is the fixed cost around them).

  python3 -u tools/blend_ab.py <label> [--host 192.168.1.121] [--secs 15]

Leaves the board at probe 0, bpie 1, interlace released, synthetic brew off,
on the brew screen.
"""
import argparse
import json
import statistics
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0] + "/settings_ui_tests")
from rig import Rig  # noqa: E402

STEPS = [("production", dict(probe=0, bpie=1)), ("scalar", dict(probe=0, bpie=0)), ("no-blend", dict(probe=1, bpie=1))]
# With --scrim two steps run again with the scrim off. scrim=N is an override
# that holds until scrim=-1 (the UI pass re-applies the stored setting every
# tick, so a plain set would not survive it), and the scrim pass falls out
# of the difference. blend_scrim_us reports it directly on loadtest builds.
SCRIM_STEPS = [("prod-noscrim", dict(probe=0, bpie=1, scrim=0)), ("noblend-noscrim", dict(probe=1, bpie=1, scrim=0))]


def sample(rig, secs):
    rows = []
    t_end = time.time() + secs
    while time.time() < t_end:
        j = rig.anim()
        rows.append((j["blend_us"], j["frame_us"], j.get("elem_us", 0), j["anim_frames"], j["uptime_ms"], j.get("ov_px", 0)))
        time.sleep(1.0)
    dt = max(1e-3, (rows[-1][4] - rows[0][4]) / 1000.0)
    return {
        "blend_us": statistics.median(r[0] for r in rows),
        "frame_us": statistics.median(r[1] for r in rows),
        "elem_us": statistics.median(r[2] for r in rows),
        "fps": round((rows[-1][3] - rows[0][3]) / dt, 1),
        "ov_px": rows[-1][5],
    }


def open_brew(rig):
    # touchmap?screen=2&load=1 asks the UI task to change to the brew screen
    # (serviceTouchMap), whatever screen is up now.
    for _ in range(3):
        if rig.touchmap().get("screen_id") == 2:
            return True
        try:
            rig.touchmap(screen=2, load=True)
        except Exception as e:  # noqa: BLE001
            print("load brew:", e)
        time.sleep(3)
    return rig.touchmap().get("screen_id") == 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("label")
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--secs", type=int, default=15)
    ap.add_argument("--scrim", action="store_true", help="also measure with the scrim off (needs the scrim knob)")
    args = ap.parse_args()
    rig = Rig(args.host)
    j = None
    for _ in range(6):
        try:
            j = rig.anim()
            break
        except Exception as e:  # noqa: BLE001
            print("retry:", e)
            time.sleep(5)
    if j is None:
        sys.exit("board not reachable")
    print(args.label, "fps_cap", j.get("fps_cap"), "pclk", rig.get_json("/api/debug/pclk").get("div"), "uptime_ms",
          j.get("uptime_ms"))
    rig.anim(interlace=1)
    rig.synth(brew=0)
    time.sleep(3)
    # The brew screen at rest, not the synthetic brew: the synthetic
    # lifecycle moves between the brew and status screens mid-sample.
    print("brew screen:", open_brew(rig))
    time.sleep(4)
    out = {}
    for name, knobs in STEPS + (SCRIM_STEPS if args.scrim else []):
        rig.anim(**knobs)
        time.sleep(4)
        out[name] = sample(rig, args.secs)
        try:
            out[name]["screen_id"] = rig.touchmap().get("screen_id")
        except Exception:  # noqa: BLE001
            out[name]["screen_id"] = None
        print("%s %-10s blend %6.0f us  frame %6.0f us  elem %5.0f us  %5.1f fps  ov_px %d  screen %s" %
              (args.label, name, out[name]["blend_us"], out[name]["frame_us"], out[name]["elem_us"], out[name]["fps"],
               out[name]["ov_px"], out[name]["screen_id"]))
    rig.anim(probe=0, bpie=1)
    if args.scrim:
        rig.anim(scrim=-1) # back to the stored setting
    rig.anim(interlace=-1)
    rig.synth(brew=0)
    print(json.dumps({args.label: out}))


if __name__ == "__main__":
    main()
