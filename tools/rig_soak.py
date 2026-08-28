#!/usr/bin/env python3
"""Build, flash, and measure the display loadtest rig in one command.

Run from WSL in the repo root:

    tools/rig_soak.py                     # build + flash + 6 min soak
    tools/rig_soak.py --minutes 10        # longer soak
    tools/rig_soak.py --no-build --no-flash --minutes 2   # measure only
    tools/rig_soak.py --record            # flight recorder: stream serial to a
                                          # timestamped log until Ctrl-C

Why this exists (learned the hard way, 2026-08):

- Telemetry rides the SERIAL log (GM_SCANOUT/GM_SLIP lines, loadtest builds
  only), never HTTP. The rig's DMA ballast starves the WPA2 handshake, so the
  network drops for minutes exactly when the rig is doing its job; five HTTP
  soaks in a row died unreachable. Worse, HTTP could only sample while WiFi
  was healthy, which systematically undercounted radio-correlated display
  faults. The measurement channel must be independent of the subsystem under
  test.
- Rates are computed within one boot, from device time (t_us), starting at
  t_us >= 90 s: the first minute holds the BLE discovery boost, WiFi
  association, and heap-cliff churn, and folding it in inflates every rate.
  A t_us that goes backwards is a reboot; the window restarts.
- Run-to-run variance on this rig is about 2x. Comparing two configurations
  by flashing them alternately needs impractically long soaks below ~0.2
  events/s; prefer a within-run toggle (alternate the configurations inside
  one boot) and read the alternation out of one capture.

Windows-side tool paths are pinned because discovery cost real time:
Python313 lacks esptool and pyserial; Python310 has both.
"""

import argparse
import datetime
import os
import re
import subprocess
import sys

REPO_WIN = "C:\\work\\gaggimate"
WIN_PY = os.environ.get(
    "GM_RIG_PY", "C:\\Users\\oystein\\AppData\\Local\\Programs\\Python\\Python310\\python.exe"
)
PORT = os.environ.get("GM_RIG_PORT", "COM3")

SCANOUT = re.compile(
    r"GM_SCANOUT: t_us=(\d+) frames=(\d+) slips=(\d+) resyncs=(\d+) phy_defer=(\d+) "
    r"busy_max=(\d+) gap_max=(\d+) busy_hi=(\d+) gap_hi=(\d+) busy_top=(\d+) gap_top=(\d+) "
    r"catchups=(\d+) catchup_bufs=(\d+) catchup_max=(\d+)"
)


def run(cmd, **kw):
    print(f"+ {cmd}", flush=True)
    return subprocess.run(cmd, shell=True, **kw)


def capture_serial(seconds, out_path):
    # rig_serial.py runs on Windows Python; the repo is reachable from both
    # sides at the same path root.
    cmd = f'cmd.exe /c "{WIN_PY} {REPO_WIN}\\tools\\rig_serial.py {seconds} {PORT}"'
    with open(out_path, "w", encoding="utf-8", errors="replace") as out:
        print(f"+ capturing {seconds:.0f}s of serial to {out_path}", flush=True)
        subprocess.run(cmd, shell=True, stdout=out, stderr=subprocess.DEVNULL)


def analyze(path):
    rows = []
    slips = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = SCANOUT.search(line)
            if m:
                rows.append(tuple(int(x) for x in m.groups()))
            elif "GM_SLIP:" in line:
                slips.append(line.strip())
    if not rows:
        print("no GM_SCANOUT lines found; is this a loadtest build (GM_SYNTH_HANDSHAKE)?")
        return 1

    # Split on reboots (t_us going backwards), keep the longest segment.
    segments, seg = [], [rows[0]]
    for r in rows[1:]:
        if r[0] < seg[-1][0]:
            segments.append(seg)
            seg = [r]
        else:
            seg.append(r)
    segments.append(seg)
    seg = max(segments, key=lambda s: s[-1][0] - s[0][0])
    if len(segments) > 1:
        print(f"note: {len(segments) - 1} reboot(s) during capture; using longest segment")

    settled = [r for r in seg if r[0] >= 90_000_000] or seg
    a, b = settled[0], settled[-1]
    el = (b[0] - a[0]) / 1e6
    if el <= 0:
        print("settled window too short (device up less than ~100 s)")
        return 1

    def rate(i):
        return (b[i] - a[i]) / el

    print(f"window t={a[0] / 1e6:.0f}..{b[0] / 1e6:.0f}s ({el:.0f}s), boot churn excluded")
    print(f"frames    {rate(1):8.2f}/s   (43.4 = pclk div 7, 50.7 = div 6)")
    print(f"resyncs   {rate(3):8.4f}/s   total this boot: {b[3]}  <- the visible-band counter")
    print(f"catchups  {rate(11):8.3f}/s   depth max {b[13]} of 8  <- absorbed latency events")
    print(f"slips     {b[2] - a[2]:8d}     busy_max={b[5]}us gap_max={b[6]}us")
    print(f"phy_defer {rate(4):8.3f}/s")
    if slips:
        print(f"\n{len(slips)} GM_SLIP attribution line(s):")
        for s in slips[-10:]:
            print(f"  {s}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--env", default="display-loadtest")
    ap.add_argument("--minutes", type=float, default=6.0)
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--no-flash", action="store_true")
    ap.add_argument("--record", action="store_true", help="flight recorder: capture until Ctrl-C, no analysis")
    ap.add_argument("--analyze", metavar="LOG", help="re-analyze an existing capture and exit")
    args = ap.parse_args()

    if args.analyze:
        return analyze(args.analyze)

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    logdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "rig-logs")
    os.makedirs(logdir, exist_ok=True)
    log = os.path.join(logdir, f"rig-{stamp}.log")

    if args.record:
        print(f"flight recorder -> {log} (Ctrl-C to stop)")
        capture_serial(86400 * 7, log)
        return 0

    if not args.no_build:
        r = run(f"pio run -e {args.env}")
        if r.returncode != 0:
            return r.returncode
    if not args.no_flash:
        fw = f"{REPO_WIN}\\.pio\\build\\{args.env}\\firmware.bin"
        r = run(
            f'cmd.exe /c "{WIN_PY} -m esptool --chip esp32s3 --port {PORT} --baud 921600 '
            f'write-flash 0x10000 {fw}"'
        )
        if r.returncode != 0:
            return r.returncode

    capture_serial(args.minutes * 60, log)
    print(f"\ncapture: {log}\n")
    return analyze(log)


if __name__ == "__main__":
    sys.exit(main())
