#!/usr/bin/env python3
"""Build, flash, and measure the display loadtest rig in one command.

Run from WSL in the repo root (or any worktree checkout):

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
- The board can boot the wrong image outright: rig_soak only ever writes
  app0. If otadata points at app1 (a rollback, or a previous OTA), the board
  boots the OLD image and every number below would silently describe it
  instead. The boot banner says which partition it loaded from and which
  git tree built it (scripts/auto_firmware_version.py's `git describe`,
  embedded as BUILD_GIT_VERSION and printed as "App version:"), so analyze()
  reads both and refuses rather than reporting numbers for the wrong build.

Windows-side tool paths are pinned because discovery cost real time:
Python313 lacks esptool and pyserial; Python310 has both. The repo path is
NOT pinned: it is derived from this script's own location, so the same
script works unmodified from the main checkout or from any worktree.
"""

import argparse
import datetime
import os
import re
import subprocess
import sys

# app0 (partitions/default_16mb.csv): rig_soak always flashes this slot.
FLASH_OFFSET = 0x10000


def _repo_root():
    """Repo root, derived from this script's own location (tools/..), so the
    tool works from any worktree instead of a hard-coded main-checkout path."""
    return os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def _win_path(path):
    """Windows-side spelling of a WSL path, via wslpath -w. The Windows-side
    subprocesses this script shells out to (esptool, the serial capture
    helper) run under cmd.exe and need a C:\\... path, not the WSL one."""
    out = subprocess.run(["wslpath", "-w", path], capture_output=True, text=True)
    if out.returncode != 0 or not out.stdout.strip():
        sys.exit(f"wslpath -w {path} failed (rc={out.returncode}): {out.stderr.strip()}")
    return out.stdout.strip()


REPO_ROOT = _repo_root()
REPO_WIN = _win_path(REPO_ROOT)
WIN_PY = os.environ.get(
    "GM_RIG_PY", "C:\\Users\\oystein\\AppData\\Local\\Programs\\Python\\Python310\\python.exe"
)
PORT = os.environ.get("GM_RIG_PORT", "COM3")

SCANOUT = re.compile(
    r"GM_SCANOUT: t_us=(\d+) frames=(\d+) slips=(\d+) resyncs=(\d+) "
    r"busy_max=(\d+) gap_max=(\d+) busy_hi=(\d+) gap_hi=(\d+) busy_top=(\d+) gap_top=(\d+) "
    r"catchups=(\d+) catchup_bufs=(\d+) catchup_max=(\d+)"
)

# ESP-IDF's own boot banner (app_init component): "App version:" is exactly
# BUILD_GIT_VERSION (scripts/auto_firmware_version.py's `git describe`
# output); "boot: Loaded app from partition at offset 0x..." says which OTA
# slot actually booted, independent of which slot was just flashed.
VERSION_RE = re.compile(r"App version:\s+(\S+)")
BOOT_PARTITION_RE = re.compile(r"boot: Loaded app from partition at offset 0x([0-9a-fA-F]+)")


def run(cmd, **kw):
    print(f"+ {cmd}", flush=True)
    return subprocess.run(cmd, shell=True, **kw)


def capture_serial(seconds, out_path):
    # rig_serial.py runs on Windows Python; the repo is reachable from both
    # sides at the same path root (REPO_WIN is this checkout's own path).
    cmd = f'cmd.exe /c "{WIN_PY} {REPO_WIN}\\tools\\rig_serial.py {seconds} {PORT}"'
    with open(out_path, "w", encoding="utf-8", errors="replace") as out:
        print(f"+ capturing {seconds:.0f}s of serial to {out_path}", flush=True)
        subprocess.run(cmd, shell=True, stdout=out, stderr=subprocess.DEVNULL)


def expected_version():
    """The BUILD_GIT_VERSION a build of the current tree embeds: the exact
    `git describe` scripts/auto_firmware_version.py runs at build time.
    Returns None if git describe fails (not a git checkout, no tags, etc.)."""
    ret = subprocess.run(
        ["git", "describe", "--tags", "--dirty", "--exclude", "nightly", "--exclude", "db"],
        cwd=REPO_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    v = ret.stdout.strip()
    return v or None


def analyze(path, expect_version=None, expect_offset=FLASH_OFFSET):
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()

    rows = []
    slips = []
    for line in text.splitlines():
        m = SCANOUT.search(line)
        if m:
            rows.append(tuple(int(x) for x in m.groups()))
        elif "GM_SLIP:" in line:
            slips.append(line.strip())

    # Boot identity, from the boot banner. Take the LAST occurrence of each:
    # a capture can span more than one boot, and the last boot is the one
    # that was running for whatever settled window gets analyzed below.
    versions = VERSION_RE.findall(text)
    offsets = BOOT_PARTITION_RE.findall(text)
    booted_version = versions[-1] if versions else None
    booted_offset = int(offsets[-1], 16) if offsets else None

    if expect_offset is not None and booted_offset is not None and booted_offset != expect_offset:
        print(
            f"refusing to analyze: booted from partition offset 0x{booted_offset:x}, "
            f"expected 0x{expect_offset:x} (rig_soak flashes app0 at 0x{FLASH_OFFSET:x}; "
            "otadata points at the other slot, so the board is running the OLD image, "
            "not the one just flashed)"
        )
        return 1
    if expect_version is not None:
        if booted_version is None:
            print(
                "refusing to analyze: no 'App version:' line in the capture "
                "(not an ESP-IDF boot banner, or the capture started after it scrolled past)"
            )
            return 1
        if booted_version != expect_version:
            print(
                f"refusing to analyze: boot banner says '{booted_version}', "
                f"this tree builds '{expect_version}' -- this capture is not of the build "
                "under test (pass --skip-sha-check to analyze it anyway)"
            )
            return 1

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

    filtered = [r for r in seg if r[0] >= 90_000_000]
    settled = filtered or seg
    a, b = settled[0], settled[-1]
    el = (b[0] - a[0]) / 1e6
    if el <= 0:
        print("settled window too short (device up less than ~100 s)")
        return 1

    def rate(i):
        return (b[i] - a[i]) / el

    # Only claim boot churn was excluded when the 90s filter actually
    # dropped rows; a run shorter than that falls back to the whole segment
    # and must say so instead of repeating the same (wrong) claim.
    churn_note = "boot churn excluded" if filtered else "boot churn NOT excluded (segment under 90s)"

    if booted_version:
        offset_note = f" (booted from offset 0x{booted_offset:x})" if booted_offset is not None else ""
        print(f"firmware: {booted_version}{offset_note}")
    print(f"window t={a[0] / 1e6:.0f}..{b[0] / 1e6:.0f}s ({el:.0f}s), {churn_note}")
    print(f"frames    {rate(1):8.2f}/s   (43.4 = pclk div 7, 50.7 = div 6)")
    print(f"resyncs   {rate(3):8.4f}/s   total this boot: {b[3]}  <- the visible-band counter")
    print(f"catchups  {rate(10):8.3f}/s   depth max {b[12]} of 8  <- absorbed latency events")
    print(f"slips     {b[2] - a[2]:8d}     busy_max={b[4]}us gap_max={b[5]}us")
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
    ap.add_argument(
        "--skip-sha-check",
        action="store_true",
        help="analyze without refusing on a firmware version or boot-partition mismatch "
        "(for re-analyzing an old capture against a tree that has since moved on)",
    )
    args = ap.parse_args()

    expect_version = None if args.skip_sha_check else expected_version()
    if expect_version is None and not args.skip_sha_check:
        print("warning: `git describe` failed; proceeding without a firmware version check", file=sys.stderr)
    expect_offset = None if args.skip_sha_check else FLASH_OFFSET

    if args.analyze:
        return analyze(args.analyze, expect_version=expect_version, expect_offset=expect_offset)

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    logdir = os.path.join(REPO_ROOT, "logs", "rig-logs")
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
            f'write-flash {FLASH_OFFSET:#x} {fw}"'
        )
        if r.returncode != 0:
            return r.returncode

    capture_serial(args.minutes * 60, log)
    print(f"\ncapture: {log}\n")
    return analyze(log, expect_version=expect_version, expect_offset=expect_offset)


if __name__ == "__main__":
    sys.exit(main())
