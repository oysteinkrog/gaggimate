"""Device check for the Text compositor elements (gm-2cl.5).

Runs against a display-loadtest build on the bench board and compares the
two ways a live label reaches the panel: as a Text element drawn from the
glyph atlas (/api/debug/anim?texts=1, production) and through LVGL
(texts=0).

  python3 tools/text_elem_check.py [--host 192.168.1.121] [--secs 30]

Part 1, pixels. The brew screen is opened with the synthetic brew running
for a moment so its value labels have changed once (that is what makes a
label live), then the brew is stopped and the easing is switched off so the
element shows the flow's exact text. The framebuffer is captured several
times in each mode on the whole-frame path (with interlace pinned the dump
does not show the rows the direct path pushed). Pixels that never change
across the captures of one mode are text, plates and dial interiors; those
must be identical between the modes. Edge pixels, where the animation shows
through the antialiasing, are reported as a histogram only.

Part 2, refreshes. With the synthetic brew running the overlay refresh
rate, the frame rate and the blend and element times are sampled in each
mode. The Text elements should take the refreshes to near zero while the
values change, and the frame rate should not fall.

Leaves the board with texts=1, textease=1, interlace released and the
synthetic brew off.
"""
import argparse
import json
import statistics
import sys
import time
import urllib.request

sys.path.insert(0, __file__.rsplit("/", 1)[0] + "/settings_ui_tests")
from rig import Rig  # noqa: E402


def get_fb(host):
    with urllib.request.urlopen("http://%s/api/debug/fb?step=2" % host, timeout=60) as resp:
        size = resp.headers.get("X-FB-Size", "480x480")
        data = resp.read()
    w, h = (int(v) for v in size.split("x"))
    if len(data) != w * h * 2:
        raise RuntimeError("fb: expected %d bytes, got %d" % (w * h * 2, len(data)))
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
        rows.append(j)
        time.sleep(1.0)
    dt = max(1e-3, (rows[-1]["uptime_ms"] - rows[0]["uptime_ms"]) / 1000.0)
    return {
        "n": len(rows),
        "refreshes_per_s": (rows[-1]["ov_refreshes"] - rows[0]["ov_refreshes"]) / dt,
        "fps": (rows[-1]["anim_frames"] - rows[0]["anim_frames"]) / dt,
        "blend_ms_med": statistics.median(r.get("blend_us", 0) for r in rows) / 1000.0,
        "frame_ms_med": statistics.median(r.get("frame_us", 0) for r in rows) / 1000.0,
        "elem_us_med": statistics.median(r.get("elem_us", 0) for r in rows),
        "elem_text": rows[-1].get("elem_text"),
        "elem_rings": rows[-1].get("elem_rings"),
        "atlas_glyphs": rows[-1].get("atlas_glyphs"),
        "atlas_bytes": rows[-1].get("atlas_bytes"),
    }


def open_brew(rig):
    for _ in range(3):
        d = rig.touchmap()
        if d.get("screen_id") == 2:
            return True
        try:
            rig.touchmap(screen=2, load=True)
        except Exception as e:  # noqa: BLE001
            print("load brew:", e, file=sys.stderr)
        time.sleep(3)
    return rig.touchmap().get("screen_id") == 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--secs", type=int, default=30)
    ap.add_argument("--captures", type=int, default=4)
    args = ap.parse_args()
    rig = Rig(args.host)
    report = {}

    rig.anim(interlace=-1, texts=1, textease=0)
    rig.synth(brew=1)
    time.sleep(2)
    screen_ok = open_brew(rig)
    print("brew screen loaded:", screen_ok)
    time.sleep(4)  # values change: the labels become live and are owned
    rig.synth(brew=0)
    time.sleep(3)
    j = rig.anim()
    print("elem_text %s, elem_rings %s, atlas %s glyphs %s bytes" %
          (j.get("elem_text"), j.get("elem_rings"), j.get("atlas_glyphs"), j.get("atlas_bytes")))
    report["owned_at_rest"] = j.get("elem_text")

    # Part 1: pixels.
    frames = {}
    elem_text_at_mode = {}
    for mode in (1, 0, 1):
        rig.anim(texts=mode)
        time.sleep(2.5)
        j = rig.anim()
        print("texts=%d elem_text=%s ov_px=%s" % (mode, j.get("elem_text"), j.get("ov_px")))
        elem_text_at_mode[mode] = j.get("elem_text")
        caps = []
        for _ in range(args.captures):
            w, h, px = get_fb(args.host)
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
    diff_pos = []
    for i, ok in enumerate(both):
        if a[0][i] == b[0][i]:
            continue
        ra, ga, ba = channels(a[0][i])
        rb, gb, bb = channels(b[0][i])
        step = max(abs(ra - rb), abs(ga - gb) // 2, abs(ba - bb))
        if ok:
            diff_stable += 1
            max_step = max(max_step, step)
            if len(diff_pos) < 12:
                diff_pos.append((i % w, i // w, a[0][i], b[0][i]))
        else:
            hist[step] = hist.get(step, 0) + 1
    report["pixels"] = {
        "stable_in_both": n_both,
        "stable_differing": diff_stable,
        "stable_max_step": max_step,
        "first_differing": diff_pos,
        "unstable_diff_hist": dict(sorted(hist.items())),
    }
    print("stable pixels in both modes: %d, differing: %d, max step: %d" % (n_both, diff_stable, max_step))
    if diff_pos:
        print("first differing (x, y at step 2, texts=1, texts=0):", diff_pos)
    print("unstable (animation shows through) diff histogram by step:", dict(sorted(hist.items())))

    # Part 2: refreshes under changing values.
    rig.anim(textease=1)
    rig.synth(brew=1)
    time.sleep(6)
    for mode in (0, 1):
        rig.anim(texts=mode)
        time.sleep(3)
        r = sample(rig, args.secs)
        report["churn_texts_%d" % mode] = r
        print("texts=%d: %.2f refreshes/s, %.1f fps, blend med %.1f ms, frame med %.1f ms, elem med %d us, "
              "texts %s rings %s (n=%d)" %
              (mode, r["refreshes_per_s"], r["fps"], r["blend_ms_med"], r["frame_ms_med"], r["elem_us_med"],
               r["elem_text"], r["elem_rings"], r["n"]))
    rig.synth(brew=0)
    rig.anim(texts=1, textease=1, interlace=-1)
    # The knob must have actually taken: at texts=0 the element must have
    # let go of every label (elem_text == 0, LVGL drawing them again), and at
    # rest with texts=1 it must own at least one (owned_at_rest > 0). Without
    # this, a firmware regression that ignores texts=0 would still pass on
    # pixels (the element and LVGL agree on what a label looks like) and only
    # the refresh-rate drop would ever have caught it.
    knob_ok = elem_text_at_mode.get(0) == 0
    if not knob_ok:
        print("FAIL: texts=0 never reported elem_text == 0:", elem_text_at_mode)
    if not screen_ok:
        print("FAIL: could not hold the brew screen (screen_id 2)")
    verdict = (diff_stable == 0 and report["churn_texts_1"]["refreshes_per_s"] < 0.5 and
               (report["owned_at_rest"] or 0) > 0 and knob_ok and screen_ok)
    report["verdict"] = "PASS" if verdict else "CHECK"
    print(json.dumps(report, indent=1))
    print("text_elem_check:", report["verdict"])
    sys.exit(0 if verdict else 1)


if __name__ == "__main__":
    main()
