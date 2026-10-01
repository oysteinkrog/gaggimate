# Does a gradient picker page cost the animation more than an ordinary
# settings page? (gm-nov3.11)
#
# The settings cover is transparent and carries no page plate, so in principle
# a picker page is no more work than any other list of rows. It does draw
# swatches, which an ordinary row does not, so the question is worth a number
# rather than an argument.
#
# The trap this script is built around is that one rate window says nothing:
# run-to-run variance on this rig is about 2x (CLAUDE.md). So the baseline is
# measured repeatedly, interleaved with the pages, inside one boot, and a
# picker cost is only a cost if it sits outside the baseline's own spread.
#
# Everything that sets the rate is held, not set: the animation, its
# parameters, the tone, the frame cap, the interlace path and the panel's
# pixel-clock divider are read once and reported, and the script writes no
# setting at all. Run it on a board whose state you have already recorded.
#
# Usage (device only; the simulator has no renderer and no /api/debug/anim):
#
#   python3 tools/picker_rate_check.py --host 192.168.1.121 [--rounds 5]
#       [--window 6] [--out report.json]
import argparse
import json
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.settings_ui_tests.rig import Rig  # noqa: E402

ANIMATION_CAT = 2


def rate_window(rig, duration):
    """Two /api/debug/anim reads `duration` apart with no other HTTP call in
    between, on the device's own clock. None if the board rebooted inside the
    window."""
    a = rig.anim()
    time.sleep(duration)
    b = rig.anim()
    dt_ms = int(b.get("uptime_ms", 0)) - int(a.get("uptime_ms", 0))
    frames = int(b.get("anim_frames", 0)) - int(a.get("anim_frames", 0))
    if dt_ms <= 0 or frames < 0:
        return None
    return round(frames * 1000.0 / dt_ms, 2)


def overlay_sample(rig):
    """What the compositor is carrying for whatever is on screen. ov_px is the
    overlay pixels inside the composite's run spans, counted at publish
    (CLAUDE.md, gm-2cl.15), so it is the number that says whether a page put a
    translucent plate behind itself: the settings pages measured 7.7k to 13.3k
    and the two pages with large translucent panels measured 41k and 104k."""
    a = rig.anim()
    return {k: a.get(k) for k in ("ov_px", "ov_refreshes", "blend_us", "elem_us", "band_us",
                                  "frame_us", "anim_id")}


def state(rig):
    """Everything a rate depends on, so a run can be compared with another
    only when these match."""
    s = rig.settings()
    anim = rig.anim()
    pclk = rig.get_json("/api/debug/pclk")
    return {
        "bgAnimId": s.get("bgAnimId"),
        "bgAnimStandbyId": s.get("bgAnimStandbyId"),
        "bgAnimParams_for_id": (s.get("bgAnimParams") or "").split(";")[int(s.get("bgAnimId", 0))]
        if s.get("bgAnimParams") else "",
        "bgAnimFps": s.get("bgAnimFps"),
        "bgAnimInterlace": s.get("bgAnimInterlace"),
        "bgAnimHalfRes": s.get("bgAnimHalfRes"),
        "bgAnimBrightness": s.get("bgAnimBrightness"),
        "bgAnimHighlightKnee": s.get("bgAnimHighlightKnee"),
        "bgAnimScrim": s.get("bgAnimScrim"),
        "bgAnimClearPlates": s.get("bgAnimClearPlates"),
        "bgAnimAllScreens": s.get("bgAnimAllScreens"),
        "bgAnimGradientRef": s.get("bgAnimGradientRef"),
        "anim_id_live": anim.get("anim_id"),
        "fps_cap": anim.get("fps_cap"),
        "interlace_live": anim.get("interlace"),
        "pclk_div": pclk.get("div"),
        "pclk_hz": pclk.get("hz"),
        "uptime_ms": anim.get("uptime_ms"),
    }


def depth(rig):
    return int(rig.settingsui_state().get("depth", 0))


def goto_page(rig, page):
    rig.settingsui(page=page)
    rig.wait_until(lambda: rig.settingsui_state().get("page") == page, timeout=5)
    return rig.touchmap(screen=0)


def page_with_row(rig, name):
    pages = int(rig.settingsui_state().get("pages", 1))
    for page in range(pages):
        dump = goto_page(rig, page)
        if rig.find_tag(dump, name, "value") is not None or rig.find_tag(dump, name, "action") is not None:
            return dump
    raise AssertionError("no row %r on any of the %d pages" % (name, pages))


def open_animation(rig):
    if rig.settingsui_state().get("open") is True:
        rig.settingsui(close=1)
        rig.wait_until(lambda: rig.settingsui_state().get("open") is False, timeout=5)
    rig.settingsui(open=1)
    rig.wait_until(lambda: rig.settingsui_state().get("open") is True, timeout=5)
    rig.settingsui(cat=ANIMATION_CAT)
    rig.wait_until(lambda: rig.settingsui_state().get("category") == ANIMATION_CAT, timeout=5)


def open_picker(rig, row="Gradient"):
    dump = page_with_row(rig, row)
    target = rig.find_tag(dump, row, "action")
    if target is None:
        raise AssertionError("row %r is not a whole-row target" % row)
    before = depth(rig)
    rig.tap_target(target)
    rig.wait_until(lambda: depth(rig) == before + 1, timeout=8)


def picker_tap(rig, name):
    dump = page_with_row(rig, name)
    target = rig.find_tag(dump, name, "action")
    if target is None:
        raise AssertionError("no picker row %r on its page" % name)
    before = depth(rig)
    rig.tap_target(target)
    rig.wait_until(lambda: depth(rig) == before + 1, timeout=8)


def cancel(rig):
    dump = rig.touchmap(screen=0)
    target = rig.find_tag(dump, "exit", "exit")
    before = depth(rig)
    rig.tap_target(target)
    rig.wait_until(lambda: depth(rig) == before - 1, timeout=8)


def spread(values):
    vals = [v for v in values if v is not None]
    if not vals:
        return None
    return {
        "n": len(vals),
        "min": min(vals),
        "max": max(vals),
        "median": round(statistics.median(vals), 2),
        "mean": round(statistics.fmean(vals), 2),
        "ratio_max_min": round(max(vals) / min(vals), 3) if min(vals) > 0 else None,
        "values": vals,
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--rounds", type=int, default=5)
    ap.add_argument("--window", type=float, default=6.0)
    ap.add_argument("--group", default="Coffee", help="the picker group whose gradient page is measured")
    ap.add_argument("--out", default=None)
    ap.add_argument("--png-dir", default=None, help="write one framebuffer capture per venue here")
    args = ap.parse_args(argv)

    rig = Rig(args.host)
    before = state(rig)
    print("held constant: " + json.dumps(before, sort_keys=True), flush=True)

    venues = ("baseline_menu", "settings_animation_p0", "picker_groups", "picker_gradients")
    samples = {k: [] for k in venues}
    overlay = {k: [] for k in venues}
    if args.png_dir:
        os.makedirs(args.png_dir, exist_ok=True)

    def sample(key, round_no):
        samples[key].append(rate_window(rig, args.window))
        overlay[key].append(overlay_sample(rig))
        if args.png_dir and round_no == 1:
            path = os.path.join(args.png_dir, "%s.png" % key)
            try:
                rig.fb_png(path)
                print("  wrote %s" % path, flush=True)
            except Exception as e:  # noqa: BLE001 -- a capture failure is not a measurement failure
                print("  framebuffer capture failed for %s: %s" % (key, e), flush=True)

    for r in range(1, args.rounds + 1):
        # Baseline: the shell closed, the menu screen showing. Measured every
        # round, not once, because that spread is the yardstick.
        if rig.settingsui_state().get("open") is True:
            rig.settingsui(close=1)
            rig.wait_until(lambda: rig.settingsui_state().get("open") is False, timeout=5)
        time.sleep(1.0)
        sample("baseline_menu", r)

        open_animation(rig)
        goto_page(rig, 0)
        time.sleep(1.0)
        sample("settings_animation_p0", r)

        open_picker(rig, "Gradient")
        goto_page(rig, 0)
        time.sleep(1.0)
        sample("picker_groups", r)

        picker_tap(rig, args.group)
        goto_page(rig, 0)
        time.sleep(1.0)
        sample("picker_gradients", r)

        cancel(rig)
        cancel(rig)
        rig.settingsui(close=1)
        rig.wait_until(lambda: rig.settingsui_state().get("open") is False, timeout=5)
        print("round %d: %s" % (r, ", ".join("%s=%s" % (k, samples[k][-1]) for k in venues)), flush=True)

    after = state(rig)
    result = {
        "host": args.host,
        "rounds": args.rounds,
        "window_s": args.window,
        "group": args.group,
        "state_before": before,
        "state_after": after,
        "rebooted": int(after["uptime_ms"]) < int(before["uptime_ms"]),
        "spread": {k: spread(v) for k, v in samples.items()},
        "overlay": overlay,
    }
    base = result["spread"]["baseline_menu"]
    print("\n%-24s %8s %8s %8s %8s" % ("venue", "min", "median", "max", "max/min"), flush=True)
    for k in venues:
        s = result["spread"][k]
        if s is None:
            print("%-24s  no valid window" % k, flush=True)
            continue
        print("%-24s %8.2f %8.2f %8.2f %8s" % (k, s["min"], s["median"], s["max"], s["ratio_max_min"]),
              flush=True)
    if base:
        print("\nbaseline spread is %.2f to %.2f fps (%.2fx). A page whose whole range sits"
              % (base["min"], base["max"], base["ratio_max_min"] or 0), flush=True)
        print("inside that is not distinguishable from run-to-run noise.", flush=True)
        for k in venues[1:]:
            s = result["spread"][k]
            if s is None:
                continue
            inside = s["min"] >= base["min"] and s["max"] <= base["max"]
            overlaps = s["min"] <= base["max"] and base["min"] <= s["max"]
            verdict = "inside the baseline spread" if inside else (
                "overlaps the baseline spread" if overlaps else "OUTSIDE the baseline spread")
            print("  %-24s %s (median %.2f vs baseline %.2f, %+.1f%%)"
                  % (k, verdict, s["median"], base["median"],
                     100.0 * (s["median"] - base["median"]) / base["median"]), flush=True)
    if result["rebooted"]:
        print("\nWARNING: the board rebooted during the run; the numbers are not one boot's.",
              flush=True)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(result, f, indent=1, sort_keys=True)
        print("\nwrote %s" % args.out, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
