"""Where the overlay blend's time goes: bus or arithmetic (gm-2cl.14).

Runs against a display-loadtest build on the bench board. The blend probe
(/api/debug/anim?probe=N, SleepAnimation::benchSetBlendProbe) replaces the
per-band overlay blend with the production kernel over one captured row,
the same bytes and runs for every band, with the source and destination
placed by level:

  0  production (each band's own rows from PSRAM into the band buffer)
  5  captured row, PSRAM source, band buffer destination
  4  captured row, internal SRAM source, band buffer destination
  6  captured row, PSRAM source, internal scratch destination
  7  captured row, internal source, internal scratch destination

The picture is wrong while a level above 0 runs; this is a timing probe.
Each step is sampled over --secs seconds and reduced to a median. Every
level runs with the row blended once and twice per band (probereps), so the
difference is the cost of probe_px * rows pixels with every fixed cost
(scrim pass, row walk, timers) cancelled; that is the ns per pixel
reported. bpie=0 repeats the all-internal case with the scalar kernel. Levels 8 and 9
blend a planar copy of the row (RGB565 plane, 16-bit alpha plane with 255
stored as 256) through the whole-group vector kernel and the scalar one;
probe_mismatch is the device's own pixel compare of that kernel against
blendRow on the captured row (0 is exact).

  python3 tools/blend_probe.py [--host 192.168.1.121] [--secs 20] [--rounds 2]

Interlace is pinned on through the debug force for the sweep (the stored
setting is not touched) and released at the end. Leaves the board at
probe=0 with the synthetic brew off.
"""
import argparse
import json
import statistics
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0] + "/settings_ui_tests")
from rig import Rig  # noqa: E402

# (level, reps, bpie): the production kernel over the captured row.
STEPS = [
    (0, 1, 1),  # production, for reference
    (4, 1, 1), (4, 2, 1),  # SRAM source
    (5, 1, 1), (5, 2, 1),  # PSRAM source
    (7, 1, 1), (7, 2, 1),  # everything internal
    (7, 1, 0), (7, 2, 0),  # everything internal, scalar kernel
    (8, 1, 1), (8, 2, 1),  # planar row, whole-group vector kernel
    (9, 1, 1), (9, 2, 1),  # planar row, scalar kernel
]
NAMES = {
    0: "production",
    4: "SRAM src -> band",
    5: "PSRAM src -> band",
    6: "PSRAM src -> SRAM scratch",
    7: "SRAM src -> SRAM scratch",
    8: "planar SRAM, vector -> band",
    9: "planar SRAM, scalar -> band",
}


def sample(rig, secs):
    rows = []
    t_end = time.time() + secs
    while time.time() < t_end:
        j = rig.anim()
        rows.append((j["blend_us"], j["frame_us"], j["anim_frames"], j["uptime_ms"], j.get("probe_px", 0),
                     240 if j.get("interlace") else 480, j.get("probe_mismatch", -1)))
        time.sleep(1.0)
    dt = max(1e-3, (rows[-1][3] - rows[0][3]) / 1000.0)
    return {
        "blend_us_med": statistics.median(r[0] for r in rows),
        "frame_us_med": statistics.median(r[1] for r in rows),
        "fps": (rows[-1][2] - rows[0][2]) / dt,
        "probe_px": rows[-1][4],
        "rows_per_frame": statistics.median(r[5] for r in rows),
        "mismatch": rows[-1][6],
        "n": len(rows),
    }


def open_brew(rig):
    for _ in range(3):
        d = rig.touchmap()
        if d.get("screen_id") == 2:
            return True
        rig.tap(141, 141)
        time.sleep(3)
    return rig.touchmap().get("screen_id") == 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--secs", type=int, default=20)
    ap.add_argument("--rounds", type=int, default=2)
    args = ap.parse_args()
    rig = Rig(args.host)

    j = None
    for _ in range(5):  # the first requests after a boot can time out while WiFi settles
        try:
            j = rig.anim()
            break
        except Exception as e:  # noqa: BLE001
            print("retry:", e)
            time.sleep(5)
    if j is None:
        sys.exit("board not reachable")
    print("interlace", j.get("interlace"), "band_internal", j.get("band_internal"), "fps_cap", j.get("fps_cap"),
          "pclk", rig.get_json("/api/debug/pclk").get("div"))
    # The epic's numbers assume interlace on; the bench board's stored
    # setting may say off (gm-2cl.9). Pin it on for the sweep through the
    # debug force, which is not stored and clears at the end.
    rig.anim(interlace=1)
    rig.synth(brew=0)
    time.sleep(3)
    ok = open_brew(rig)
    print("brew screen:", ok)
    rig.synth(brew=1)
    time.sleep(4)

    report = {"rounds": []}
    for r in range(args.rounds):
        rd = {}
        for level, reps, bpie in STEPS:
            rig.anim(probe=level, probereps=reps, bpie=bpie)
            time.sleep(4)  # capture takes about three frames, then settle
            s = sample(rig, args.secs)
            rd[(level, reps, bpie)] = s
            print("round %d level %d reps %d bpie %d %-28s blend med %6.0f us, frame med %6.0f us, %5.1f fps, probe_px %4d, rows %d, mismatch %d" %
                  (r, level, reps, bpie, NAMES[level], s["blend_us_med"], s["frame_us_med"], s["fps"], s["probe_px"],
                   s["rows_per_frame"], s["mismatch"]))
        report["rounds"].append(rd)
    rig.anim(probe=0, probereps=1, bpie=1)
    rig.anim(interlace=-1)
    rig.synth(brew=0)

    # Per-pixel cost from the two rep counts: everything fixed (scrim pass,
    # row walk, timer reads) cancels. Median over rounds.
    def slope(level, bpie):
        vals = []
        for rd in report["rounds"]:
            a, b = rd[(level, 1, bpie)], rd[(level, 2, bpie)]
            px = b["probe_px"] * b["rows_per_frame"]
            if px > 0:
                vals.append((b["blend_us_med"] - a["blend_us_med"]) * 1000.0 / px)
        return statistics.median(vals) if vals else 0.0

    ns = {
        "sram_src_vector": slope(4, 1),
        "psram_src_vector": slope(5, 1),
        "all_sram_vector": slope(7, 1),
        "all_sram_scalar": slope(7, 0),
        "planar_vector": slope(8, 1),
        "planar_scalar": slope(9, 1),
    }
    report["ns_per_px"] = ns
    print(json.dumps({"ns_per_px": ns, "rounds": [{"%d/%d/%d" % k: v for k, v in rd.items()} for rd in report["rounds"]]},
                     indent=1))
    ratio = ns["psram_src_vector"] / max(1e-9, ns["sram_src_vector"])
    verdict = "bus-bound" if ratio >= 1.5 else "compute-bound"
    print("blend_probe: %.0f ns/px from SRAM, %.0f ns/px from PSRAM (ratio %.2f), scalar %.0f ns/px: %s" %
          (ns["sram_src_vector"], ns["psram_src_vector"], ratio, ns["all_sram_scalar"], verdict))
    print("planar row: vector %.0f ns/px, scalar %.0f ns/px, mismatch %d" %
          (ns["planar_vector"], ns["planar_scalar"], report["rounds"][-1][(8, 1, 1)]["mismatch"]))


if __name__ == "__main__":
    main()
