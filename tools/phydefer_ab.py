"""A/B soak of the PHY PLL-track deferral within one boot.

Phases alternate phydefer=1 (deferred into vertical blanking, the shipped
behaviour) and phydefer=0 (stock inline tick). Each phase resets the scanout
counters at its start and reads them at its end over HTTP, and a serial
capture runs underneath with host timestamps so the GM_SCANOUT lines give an
independent count per phase.
"""
import json
import os
import re
import subprocess
import sys
import time
import urllib.request

# Usage, from WSL with the bench board on WiFi and its UART on COM3:
#   HOST=192.168.1.121 PHASE_S=300 python3 tools/phydefer_ab.py
# Writes phydefer_serial.log and phydefer_ab.json into the working directory.
# Ran 2026-09-09 at dividers 8 and 6 (CLAUDE.md, Open cleanups): 0 resyncs
# either way, which is what made the deferral's removal gm-bzu.24.
HOST = os.environ.get("HOST", "192.168.1.121")
PHASE_S = int(os.environ.get("PHASE_S", "300"))
PHASES = [1, 0, 1, 0]
LOG = os.environ.get("LOG", "phydefer_serial.log")
WIN_PY = os.environ.get(
    "GM_RIG_PY", "C:\\Users\\oystein\\AppData\\Local\\Programs\\Python\\Python310\\python.exe"
)
RIG = os.environ.get("GM_RIG_SERIAL", "C:\\work\\gaggimate\\tools\\rig_serial.py")

SCANOUT = re.compile(
    r"GM_SCANOUT: t_us=(\d+) frames=(\d+) slips=(\d+) resyncs=(\d+) phy_defer=(\d+) "
    r"busy_max=(\d+) gap_max=(\d+) busy_hi=(\d+) gap_hi=(\d+) busy_top=(\d+) gap_top=(\d+) "
    r"catchups=(\d+) catchup_bufs=(\d+) catchup_max=(\d+)"
)


def get(path):
    last = None
    for attempt in range(4):
        try:
            with urllib.request.urlopen("http://%s%s" % (HOST, path), timeout=15) as r:
                return json.loads(r.read().decode())
        except Exception as e:  # noqa: BLE001
            last = e
            print("get %s failed (%s), retry %d" % (path, e, attempt + 1), flush=True)
            time.sleep(5)
    raise last


def main():
    total = PHASE_S * len(PHASES) + 60
    cap = subprocess.Popen(
        ["cmd.exe", "/c", "%s %s %d COM3" % (WIN_PY, RIG, total)],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, errors="replace",
    )
    import threading
    logf = open(LOG, "w")

    def pump():
        for line in cap.stdout:
            logf.write("%.3f %s" % (time.time(), line))
            logf.flush()

    threading.Thread(target=pump, daemon=True).start()
    time.sleep(3)
    marks = []
    results = []
    for on in PHASES:
        get("/api/debug/scanout?phydefer=%d&reset=1" % on)
        t0 = time.time()
        marks.append((t0, on))
        print("phase phydefer=%d start" % on, flush=True)
        time.sleep(PHASE_S)
        j = get("/api/debug/scanout")
        t1 = time.time()
        el = t1 - t0
        row = {
            "on": on, "s": round(el, 1), "phy_defer_on": j.get("phy_defer_on"),
            "phy_defer": j.get("phy_defer"), "resyncs": j.get("resyncs"), "slips": j.get("slips"),
            "catchups": j.get("dma_catchups"), "catchup_max": j.get("dma_catchup_max"),
            "over_count": j.get("over_count"), "busy_max": j.get("busy_max"), "gap_max": j.get("gap_max"),
        }
        results.append(row)
        print("phase result", json.dumps(row), flush=True)
    marks.append((time.time(), None))
    get("/api/debug/scanout?phydefer=1")
    cap.wait()
    logf.close()

    # Serial cross-check: per phase, delta of the cumulative counters between
    # the first and the last GM_SCANOUT line inside the phase's host window.
    rows = []
    with open(LOG, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = SCANOUT.search(line)
            if m:
                rows.append((float(line.split(" ", 1)[0]),) + tuple(int(x) for x in m.groups()))
    print("\nserial cross-check (%d GM_SCANOUT lines)" % len(rows))
    for i, (t0, on) in enumerate(marks[:-1]):
        t1 = marks[i + 1][0]
        w = [r for r in rows if t0 + 5 <= r[0] <= t1]
        if len(w) < 2:
            print("phase %d phydefer=%s: too few lines" % (i, on))
            continue
        a, b = w[0], w[-1]
        el = (b[1] - a[1]) / 1e6
        print("phase %d phydefer=%d: %.0f s, frames %.2f/s, resyncs %d (%.4f/s), slips %d, catchups %d, phy_defer %d, busy_max %d gap_max %d" % (
            i, on, el, (b[2] - a[2]) / el, b[4] - a[4], (b[4] - a[4]) / el, b[3] - a[3], b[12] - a[12], b[5] - a[5], max(r[6] for r in w), max(r[7] for r in w)))
    with open("phydefer_ab.json", "w") as f:
        json.dump(results, f, indent=1)


if __name__ == "__main__":
    main()
