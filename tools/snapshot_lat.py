#!/usr/bin/env python3
"""Page-change snapshot cost per screen (gm-2cl.7).

Loads every generated screen and every settings category through the debug
routes and reads, right after each load, the last whole-buffer snapshot's
render time (ov_snap_us) and publish time (ov_pub_us) from
/api/debug/anim. A page change renders the new screen whole into the
overlay, so these are the numbers the fade has to hide; the target is
snapshot plus publish under 120 ms on every page.

Each page is loaded twice from a different page so the second number is a
warm one (fonts and images already in cache). The synthetic brew is
stopped first so the board stays on the page it was sent to.

Loadtest build. Usage:

    python3 tools/snapshot_lat.py [--host 192.168.1.121] [--rounds 2]
        [--out tools/overlay_footprint]

Writes <out>/snapshot-<date>-<host>.json and prints the table.
"""
import argparse
import datetime
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_ui_tests"))
from rig import Rig  # noqa: E402

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
CATEGORIES = ["temps", "display", "animation", "machine", "status", "fixture"]


def load_and_read(rig, loader, settle=2.5):
    """Runs loader(), waits for the snapshot, returns (snap_us, pub_us, refreshes_added)."""
    before = rig.anim()
    loader()
    time.sleep(settle)
    after = rig.anim()
    # The whole-buffer snapshot's own numbers: on a telemetry screen the
    # small refreshes that follow the load overwrite ov_snap_us within a
    # second, so the plain "last snapshot" fields are not the page change.
    return {
        "snap_us": after.get("ov_whole_snap_us"),
        "pub_us": after.get("ov_whole_pub_us"),
        "clear_us": after.get("ov_whole_clear_us"),
        "draw_us": after.get("ov_whole_draw_us"),
        "scan_us": after.get("ov_whole_scan_us"),
        "scrim_us": after.get("ov_whole_scrim_us"),
        "whole_at_ms": after.get("ov_whole_at_ms"),
        "clear_by_runs": after.get("ov_whole_clear_by_runs"),
        "scrim_stages_us": after.get("ov_scrim_stages_us"),
        "refreshes": (after.get("ov_refreshes") or 0) - (before.get("ov_refreshes") or 0),
        "ov_px": after.get("ov_px"),
        "fps": after.get("fps_cap"),
    }


def fmt_row(name, r):
    def ms(k):
        return (r.get(k) or 0) / 1000.0
    st = r.get("scrim_stages_us") or []
    return "| %-16s | %8.1f | %8.1f | %8.1f | %6.1f | %6.1f | %6.1f | %6.1f | %6s | %s%s |" % (
        name, ms("snap_us"), ms("pub_us"), ms("snap_us") + ms("pub_us"), ms("clear_us"), ms("draw_us"),
        ms("scan_us"), ms("scrim_us"), r.get("ov_px"), "R" if r.get("clear_by_runs") else "M",
        " " + "/".join("%.0f" % (v / 1000.0) for v in st) if st else "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--rounds", type=int, default=2)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "overlay_footprint"))
    ap.add_argument("--skip-settings", action="store_true")
    args = ap.parse_args()
    rig = Rig(args.host)
    try:
        rig.synth(brew=0)
    except Exception as e:  # noqa: BLE001
        print("synth off:", e, file=sys.stderr)
    j = rig.anim()
    meta = {"host": args.host, "date": datetime.date.today().isoformat(), "uptime_ms": j.get("uptime_ms"),
            "fps_cap": j.get("fps_cap"), "interlace": j.get("interlace"), "anim_id": j.get("anim_id"),
            "pclk_div": rig.get_json("/api/debug/pclk").get("div")}
    print("board:", json.dumps(meta))
    results = {"meta": meta, "screens": {}, "settings": {}}
    print("| %-16s | %8s | %8s | %8s | %6s | %6s | %6s | %6s | %6s |" %
          ("page", "snap ms", "pub ms", "total", "clear", "draw", "scan", "scrim", "ov_px"))
    print("|" + "-" * 18 + "|" + "-" * 10 + "|" + "-" * 10 + "|" + "-" * 10 + ("|" + "-" * 8) * 5 + "|")
    prev = None
    for sid, name in SCREENS:
        rounds = []
        for r in range(args.rounds):
            # Leave first so the load is a real page change.
            other = 1 if sid != 1 else 5
            try:
                rig.touchmap(screen=other, load=True)
            except Exception as e:  # noqa: BLE001
                print("  leave to %d: %s" % (other, e), file=sys.stderr)
            time.sleep(2.0)
            try:
                res = load_and_read(rig, lambda: rig.touchmap(screen=sid, load=True))
            except Exception as e:  # noqa: BLE001
                print("  load %s: %s" % (name, e), file=sys.stderr)
                continue
            rounds.append(res)
        if not rounds:
            continue
        best = min(rounds, key=lambda x: (x["snap_us"] or 0) + (x["pub_us"] or 0))
        results["screens"][name] = {"rounds": rounds, "best": best}
        print(fmt_row(name, best), flush=True)
    if not args.skip_settings:
        try:
            rig.touchmap(screen=5, load=True)
            time.sleep(2)
            res = load_and_read(rig, lambda: rig.settingsui(open=1))
            results["settings"]["home"] = res
            print(fmt_row("settings home", res), flush=True)
            for i, cat in enumerate(CATEGORIES):
                try:
                    res = load_and_read(rig, lambda i=i: rig.settingsui(cat=i))
                except Exception as e:  # noqa: BLE001
                    print("  cat %d: %s" % (i, e), file=sys.stderr)
                    continue
                results["settings"][cat] = res
                print(fmt_row("settings " + cat, res), flush=True)
                rig.settingsui(pop=1)
                time.sleep(1)
            rig.settingsui(close=1)
        except Exception as e:  # noqa: BLE001
            print("settings:", e, file=sys.stderr)
    os.makedirs(args.out, exist_ok=True)
    stem = os.path.join(args.out, "snapshot-%s-%s" % (meta["date"], args.host.replace(".", "-")))
    with open(stem + ".json", "w") as f:
        json.dump(results, f, indent=1)
    print("wrote", stem + ".json")


if __name__ == "__main__":
    main()
