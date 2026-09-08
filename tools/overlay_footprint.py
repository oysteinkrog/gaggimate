#!/usr/bin/env python3
"""Per-screen overlay footprint on the bench board (gm-2cl.15).

For every generated screen and every on-display settings category, this
loads the page through the debug routes, lets it settle, and records what
the render task moves for it every frame: ov_px (overlay pixels inside
the composite's run spans), ov_px_rows, blend_us, blend_scrim_us,
elem_us, frame_us and the loop rate. The synthetic brew is stopped first
so the board stays on the page it was sent to, and interlace is pinned on
(the bench board stores 0) so the numbers match the rest of the epic.

The counter is checked once, on the brew screen, against an offline
count of /api/debug/ovl: the same run rule as the device (a run starts at
the first pixel with alpha above zero, and a gap of RUN_GAP_MERGE pixels
or less joins two runs), so ov_px should match to well under 1%. The
plain count of pixels with alpha above zero is printed next to it: ov_px
is larger than that by the merged gaps, which the blend reads too.

Loadtest build required (GM_TOUCH_PROBE routes). Usage:

    python3 tools/overlay_footprint.py --host 192.168.1.121 [--secs 8]
        [--out tools/overlay_footprint]

Writes <out>/<date>-<host>.md and .json and prints the table.
"""
import argparse
import datetime
import json
import os
import statistics
import sys
import time
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_ui_tests"))
from rig import Rig  # noqa: E402

# eez/screens.h. 4 (the old menu) is not reachable from the running UI and
# is skipped; 5 is the menu the device shows.
SCREENS = [
    (1, "standby"),
    (5, "menu"),
    (2, "brew"),
    (6, "steam"),
    (7, "water"),
    (3, "status"),
    (9, "grind"),
    (8, "profile"),
    (11, "new_profile"),
    (10, "info"),
]
# SettingsUI.cpp kCategories order; the Fixture tile (5) exists only on
# loadtest and sim builds and is a test category, so it is included last.
CATEGORIES = ["temps", "display", "animation", "machine", "status", "fixture"]
RUN_GAP_MERGE = 4  # SleepAnimation.cpp emitRun


def sample(rig, secs):
    rows = []
    t_end = time.time() + secs
    while time.time() < t_end:
        j = rig.anim()
        rows.append(j)
        time.sleep(1.0)
    dt = max(1e-3, (rows[-1]["uptime_ms"] - rows[0]["uptime_ms"]) / 1000.0)

    def med(key):
        return statistics.median(r.get(key, 0) for r in rows)

    return {
        "ov_px": rows[-1].get("ov_px", 0),
        "ov_px_rows": rows[-1].get("ov_px_rows", 0),
        "blend_us": med("blend_us"),
        "blend_scrim_us": med("blend_scrim_us"),
        "elem_us": med("elem_us"),
        "band_us": med("band_us"),
        "push_us": med("push_us"),
        "frame_us": med("frame_us"),
        "fps": round((rows[-1]["anim_frames"] - rows[0]["anim_frames"]) / dt, 1),
        "running": rows[-1].get("running", None),
        "anim_id": rows[-1].get("anim_id"),
    }


def load_screen(rig, screen_id, tries=3):
    for _ in range(tries):
        try:
            if rig.touchmap().get("screen_id") == screen_id:
                return True
            rig.touchmap(screen=screen_id, load=True)
        except Exception as e:  # noqa: BLE001
            print("  load screen %d: %s" % (screen_id, e), file=sys.stderr)
        time.sleep(3)
    try:
        return rig.touchmap().get("screen_id") == screen_id
    except Exception:  # noqa: BLE001
        return False


def fetch_ovl(host):
    req = urllib.request.urlopen("http://%s/api/debug/ovl" % host, timeout=60)
    size = req.headers.get("X-OV-Size", "")
    data = req.read()
    w, h = (int(v) for v in size.split("x"))
    return w, h, data


def offline_counts(w, h, data, panel=480):
    """(span pixels with the device's run rule, pixels with alpha > 0)."""
    ox = (w - panel) // 2
    oy = (h - panel) // 2
    span_px = 0
    alpha_px = 0
    for y in range(panel):
        base = ((oy + y) * w + ox) * 3
        runs = []
        start = -1
        for x in range(panel):
            a = data[base + x * 3 + 2]
            if a != 0:
                alpha_px += 1
                if start < 0:
                    start = x
                continue
            if start >= 0:
                if runs and start - runs[-1][1] <= RUN_GAP_MERGE:
                    runs[-1][1] = x
                else:
                    runs.append([start, x])
                start = -1
        if start >= 0:
            if runs and start - runs[-1][1] <= RUN_GAP_MERGE:
                runs[-1][1] = panel
            else:
                runs.append([start, panel])
        span_px += sum(b - a for a, b in runs)
    return span_px, alpha_px


def fmt_row(name, s):
    return "| %-16s | %7d | %4d | %7.0f | %7.0f | %7.0f | %7.0f | %5.1f |" % (
        name, s["ov_px"], s["ov_px_rows"], s["blend_us"], s["blend_scrim_us"], s["elem_us"], s["frame_us"], s["fps"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--secs", type=float, default=8.0)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "overlay_footprint"))
    ap.add_argument("--skip-settings", action="store_true")
    args = ap.parse_args()
    rig = Rig(args.host)
    j = rig.anim()
    pclk = rig.get_json("/api/debug/pclk")
    meta = {
        "host": args.host,
        "date": datetime.date.today().isoformat(),
        "uptime_ms": j.get("uptime_ms"),
        "fps_cap": j.get("fps_cap"),
        "pclk_div": pclk.get("div"),
        "stored_interlace": j.get("interlace"),
        "anim_id": j.get("anim_id"),
        "secs": args.secs,
    }
    print("board:", json.dumps(meta))
    if meta["uptime_ms"] is not None and meta["uptime_ms"] < 90000:
        print("warning: uptime under 90 s, boot churn inflates the numbers", file=sys.stderr)
    results = {"meta": meta, "screens": {}, "settings": {}, "check": {}}
    rig.anim(interlace=1, probe=0)
    try:
        rig.synth(brew=0)
    except Exception as e:  # noqa: BLE001
        print("synth off:", e, file=sys.stderr)
    time.sleep(2)
    header = ("| %-16s | %7s | %4s | %7s | %7s | %7s | %7s | %5s |" %
              ("page", "ov_px", "rows", "blend", "scrim", "elem", "frame", "fps"))
    rule = "|" + "|".join("-" * len(c) for c in header.split("|")[1:-1]) + "|"
    print(header)
    print(rule)
    try:
        for sid, name in SCREENS:
            ok = load_screen(rig, sid)
            time.sleep(4)
            s = sample(rig, args.secs)
            s["loaded"] = ok
            results["screens"][name] = s
            print(fmt_row(name + ("" if ok else " (not loaded)"), s), flush=True)
            if name == "brew" and ok:
                try:
                    w, h, data = fetch_ovl(args.host)
                    span_px, alpha_px = offline_counts(w, h, data)
                    dev = rig.anim().get("ov_px", 0)
                    results["check"] = {
                        "ovl_size": "%dx%d" % (w, h), "device_ov_px": dev, "offline_span_px": span_px,
                        "offline_alpha_px": alpha_px,
                        "span_error_pct": round(abs(dev - span_px) * 100.0 / max(1, span_px), 3),
                    }
                    print("check: device ov_px %d, offline span %d (%.2f%% off), alpha>0 %d" %
                          (dev, span_px, results["check"]["span_error_pct"], alpha_px), flush=True)
                except Exception as e:  # noqa: BLE001
                    print("check failed:", e, file=sys.stderr)
        if not args.skip_settings:
            if load_screen(rig, 5):
                rig.settingsui(open=1)
                time.sleep(4)
                s = sample(rig, args.secs)
                results["settings"]["home"] = s
                print(fmt_row("settings home", s), flush=True)
                for i, cat in enumerate(CATEGORIES):
                    try:
                        rig.settingsui(cat=i)
                    except Exception as e:  # noqa: BLE001
                        print("  cat %d: %s" % (i, e), file=sys.stderr)
                        continue
                    time.sleep(4)
                    s = sample(rig, args.secs)
                    results["settings"][cat] = s
                    print(fmt_row("settings " + cat, s), flush=True)
                    rig.settingsui(pop=1)
                    time.sleep(1)
                rig.settingsui(close=1)
            else:
                print("menu screen not loaded, settings skipped", file=sys.stderr)
    finally:
        rig.anim(interlace=-1)
        try:
            rig.settingsui(close=1)
        except Exception:  # noqa: BLE001
            pass
    os.makedirs(args.out, exist_ok=True)
    stem = os.path.join(args.out, "%s-%s" % (meta["date"], args.host.replace(".", "-")))
    with open(stem + ".json", "w") as f:
        json.dump(results, f, indent=1)
    with open(stem + ".md", "w") as f:
        f.write("# Overlay footprint per page, %s, %s\n\n" % (meta["host"], meta["date"]))
        f.write("Loadtest build, interlace pinned on, synthetic brew off, cap %s, pixel-clock divider %s, "
                "animation %s, %.0f s per page. ov_px is the overlay pixels inside the composite's run spans; "
                "times are per frame medians in microseconds.\n\n" %
                (meta["fps_cap"], meta["pclk_div"], meta["anim_id"], args.secs))
        f.write(header + "\n" + rule + "\n")
        for name, s in results["screens"].items():
            f.write(fmt_row(name + ("" if s.get("loaded") else " (not loaded)"), s) + "\n")
        for name, s in results["settings"].items():
            f.write(fmt_row("settings " + name, s) + "\n")
        if results["check"]:
            c = results["check"]
            f.write("\nCounter check on the brew screen: device ov_px %d, offline span count %d (%.2f%% off), "
                    "pixels with alpha above zero %d, overlay dump %s.\n" %
                    (c["device_ov_px"], c["offline_span_px"], c["span_error_pct"], c["offline_alpha_px"], c["ovl_size"]))
    print("wrote", stem + ".md")


if __name__ == "__main__":
    main()
