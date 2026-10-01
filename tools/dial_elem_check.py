"""Device check for the dial tick-ring compositor element (gm-2cl.6).

Runs against a display-loadtest build on the bench board and compares the
two ways the dial ring reaches the panel: through the TickRing element
(/api/debug/anim?dials=1, production) and through LVGL (dials=0).

  python3 tools/dial_elem_check.py [--host 192.168.1.121] [--secs 30]

Part 1, pixels. The brew screen is opened, then the framebuffer is captured
several times in each mode, at step 2 (240x240: a quarter of the bytes, and
both modes are sampled the same way, so the comparison is unaffected. Step 1
is whole too since gm-6ivh; it used to return 4,800 of 460,800 bytes, which is
why this said step 2 was the only one the device delivered). Pixels that never
change across the captures of
one mode are the ring's interior and the rest of the UI (the animation
underneath moves, so everything it shows through changes). Those stable
pixels must be identical between the modes; the report prints how many
differ and the largest per-channel step among them. Edge pixels, where the
animation shows through the antialiasing, are reported as a histogram only.

Part 2, refreshes. With the synthetic brew running (the temperature and
pressure values change continuously) the overlay refresh rate, the frame
rate and the blend and element times are sampled in each mode. The element
path should take the refreshes to about zero while values change, and the
frame rate should not fall.

Leaves the board with dials=1 and the synthetic brew off.
"""
import argparse
import json
import statistics
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0] + "/settings_ui_tests")
from rig import Rig  # noqa: E402


def get_fb(rig):
    """One framebuffer capture as (width, height, RGB565 values). Rig.fb()
    raises on a body shorter than the X-FB-Size header promised, so a short
    read fails the run rather than shifting every pixel comparison below."""
    w, h, data = rig.fb(step=2)
    px = [data[i] | (data[i + 1] << 8) for i in range(0, len(data), 2)]
    return w, h, px


def channels(c):
    return (c >> 11) & 31, (c >> 5) & 63, c & 31


def stable_mask(frames):
    n = len(frames[0])
    return [all(f[i] == frames[0][i] for f in frames[1:]) for i in range(n)]


def sample(rig, secs):
    rows = []
    t_end = time.time() + secs
    while time.time() < t_end:
        j = rig.anim()
        rows.append((j["ov_refreshes"], j["anim_frames"], j["uptime_ms"], j["blend_us"], j["frame_us"],
                     j.get("elem_us", 0), j.get("elem_rings", -1)))
        time.sleep(1.0)
    dt = max(1e-3, (rows[-1][2] - rows[0][2]) / 1000.0)
    return {
        "refreshes_per_s": (rows[-1][0] - rows[0][0]) / dt,
        "fps": (rows[-1][1] - rows[0][1]) / dt,
        "blend_ms_med": statistics.median(r[3] for r in rows) / 1000.0,
        "frame_ms_med": statistics.median(r[4] for r in rows) / 1000.0,
        "elem_us_med": statistics.median(r[5] for r in rows),
        "elem_rings": rows[-1][6],
        "n": len(rows),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--secs", type=int, default=30)
    ap.add_argument("--captures", type=int, default=4)
    args = ap.parse_args()
    rig = Rig(args.host)
    report = {}

    rig.synth(brew=0)
    time.sleep(2)
    rig.tap(141, 141)  # menu tile: brew screen (or wake from standby first)
    time.sleep(3)
    d = rig.touchmap()
    print("screen_id", d.get("screen_id"))
    if d.get("screen_id") != 2:
        rig.tap(141, 141)
        time.sleep(3)
        d = rig.touchmap()
        print("screen_id", d.get("screen_id"))
    screen_ok = d.get("screen_id") == 2
    if not screen_ok:
        print("FAIL: could not hold the brew screen (screen_id 2), now", d.get("screen_id"))

    # Part 1: pixels.
    frames = {}
    elem_rings_at_mode = {}
    for mode in (1, 0, 1):
        rig.anim(dials=mode)
        time.sleep(2.5)
        j = rig.anim()
        print("dials=%d elem_rings=%s" % (mode, j.get("elem_rings")))
        elem_rings_at_mode[mode] = j.get("elem_rings")
        caps = []
        for _ in range(args.captures):
            w, h, px = get_fb(rig)
            caps.append(px)
            time.sleep(0.7)
        frames.setdefault(mode, []).extend(caps)
    a, b = frames[1], frames[0]
    stable_a = stable_mask(a)
    stable_b = stable_mask(b)
    both = [sa and sb for sa, sb in zip(stable_a, stable_b)]
    n_both = sum(both)
    diff_stable = 0
    max_step = 0
    hist = {}
    for i, ok in enumerate(both):
        if a[0][i] == b[0][i]:
            continue
        ra, ga, ba = channels(a[0][i])
        rb, gb, bb = channels(b[0][i])
        step = max(abs(ra - rb), abs(ga - gb) // 2, abs(ba - bb))
        if ok:
            diff_stable += 1
            max_step = max(max_step, step)
        else:
            hist[step] = hist.get(step, 0) + 1
    report["pixels"] = {
        "stable_in_both": n_both,
        "stable_differing": diff_stable,
        "stable_max_step": max_step,
        "unstable_diff_hist": dict(sorted(hist.items())),
    }
    print("stable pixels in both modes: %d, differing: %d, max step: %d" % (n_both, diff_stable, max_step))
    print("unstable (animation shows through) diff histogram by step:", dict(sorted(hist.items())))

    # Part 2: refreshes under changing values.
    rig.synth(brew=1)
    time.sleep(6)
    for mode in (0, 1):
        rig.anim(dials=mode)
        time.sleep(3)
        r = sample(rig, args.secs)
        report["churn_dials_%d" % mode] = r
        print("dials=%d: %.2f refreshes/s, %.1f fps, blend med %.1f ms, frame med %.1f ms, elem med %d us, rings %s (n=%d)" %
              (mode, r["refreshes_per_s"], r["fps"], r["blend_ms_med"], r["frame_ms_med"], r["elem_us_med"],
               r["elem_rings"], r["n"]))
    rig.synth(brew=0)
    rig.anim(dials=1)
    # The knob must have actually taken: at dials=1 the element must own at
    # least one ring. Without this, a firmware regression that ignores
    # dials=1 (LVGL keeps drawing every ring) would still pass on pixels (the
    # two paths agree on what a ring looks like) and only the refresh-rate
    # drop would ever have caught it.
    knob_ok = (elem_rings_at_mode.get(1) or 0) >= 1
    if not knob_ok:
        print("FAIL: dials=1 never reported elem_rings >= 1:", elem_rings_at_mode)
    verdict = (diff_stable == 0 and report["churn_dials_1"]["refreshes_per_s"] < 0.5 and
               knob_ok and screen_ok)
    report["verdict"] = "PASS" if verdict else "CHECK"
    print(json.dumps(report, indent=1))
    print("dial_elem_check:", report["verdict"])
    sys.exit(0 if verdict else 1)


if __name__ == "__main__":
    main()
