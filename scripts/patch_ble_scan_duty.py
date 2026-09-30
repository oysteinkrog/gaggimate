"""Hand the BLE scale scanner's cadence to the firmware, so scanning stops
disturbing scan-out.

The fault
---------
esp-arduino-ble-scales scans continuously with a 100 ms window every 500 ms, an
active scan, while no scale is connected. That is a 20% duty cycle on the BLE
receiver, and the BT controller executes from flash (sdkconfig.gaggimate.defaults
says so where it sizes the duplicate cache), so its instruction-cache misses are
MSPI traffic. Flash and PSRAM share the MSPI controller, and the RGB panel's
bounce refill reads the framebuffer from PSRAM under a hard deadline, so the scan
window steals exactly the bandwidth the refill needs and the refill lands late.

This was measured rather than reasoned. The refill's late frames arrive on a wall
clock period, not a frame period: about 470 ms, holding while the pixel clock
changed the frame rate from 43.4 to 60.8 Hz. They were indifferent to WiFi
traffic, to the animation's frame rate, and to the band-push DMA. No armed
esp_timer had that period. Changing the scan interval from 500 ms to 1000 ms
moved the period to 990 ms, which is the whole proof: the scanner is the source.

The cost is per window, not per millisecond of window. Halving the window at a
fixed interval, 40 ms to 20 ms, changed nothing: 0.82/s against 0.92/s at 43.4 Hz,
inside the noise. Doubling the interval at a fixed duty cycle halved the rate
every time. So what disturbs the refill is opening and closing the receiver, not
holding it open, and the parameter worth spending is the interval. Late frames
per second, animation running, 120 s windows:

    Hz     stock 100/500   40/1000   80/2000
    60.8       5.77          2.85      1.91
    50.7       3.26          1.70      0.92
    43.4       2.37          0.82      0.57
    38.0       0.73          0.04      0.12

The patch
---------
The library sets its interval and window as literals inside
RemoteScalesScanner::initializeAsyncScan(), which runs every time an async scan
starts. This patch replaces the literals with two globals the firmware owns,
gm_ble_scan_interval_ms and gm_ble_scan_window_ms, defined in
src/display/plugins/BLEScalePlugin.cpp. That is where the scan policy lives:
which cadence runs when, and the measurements above that justify it. The
library keeps deciding WHEN to scan; the firmware decides HOW.

Why a patch rather than a fork: it is a few lines in a library we do not
otherwise touch. Anchored on exact upstream text, so a library bump fails the
build loudly rather than silently reverting.

Applied through scripts/gm_patch.py: the patched file is always derived from a
hash-verified pristine remote_scales.cpp (kept beside it as
remote_scales.cpp.gm-orig) and carries a gm-patch-vN stamp, so a tree patched
by an older version of this script is brought up to date instead of being
taken as done.

Only the env being built is patched, like every other patch script. The first
version walked every env's libdeps from one build, which rewrote files another
env's build might be compiling at that moment; an env that has not been built
since this change is patched by its own next build.

The marker-based version of this script wrote no .gm-orig, so a tree it
patched has no pristine copy on disk. recover_gm_orig() rebuilds one by
reversing that version's hunks, and keeps it only if the result has the pinned
upstream hash; otherwise gm_patch fails the build as for any unknown baseline.

Versions
--------
v1 (2026-10-01, the first stamped version): the extern declarations and the
two setters reading the firmware's globals, text unchanged from the
marker-based version. Bump VERSION whenever a hunk below changes; leave
LEGACY_HUNKS alone, it describes what old trees on disk contain.
"""

import os
import sys

try:
    Import("env")  # noqa: F821 -- provided by SCons
except NameError:  # imported by scripts/test_gm_patch.py
    env = None
if env is not None:
    sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "scripts"))
import gm_patch  # noqa: E402

OWNER = "patch_ble_scan_duty"
VERSION = 1
# sha256 of src/remote_scales.cpp at gaggimate/esp-arduino-ble-scales tag v2.0.0
# (the git pin in platformio.ini), checked against raw.githubusercontent.com on
# 2026-10-01.
PRISTINE_SHA256 = {"272e1d41d6565531b7b05302d3ff62ba3e961e49829e9c36746d9605de9c745e"}

MARKER = "GM_BLE_SCAN_DUTY_PATCH"

# The hunks as the marker-based version applied them, (anchor, replacement).
LEGACY_HUNKS = [
    (
        '#include "remote_scales.h"\n'
        '#include "remote_scales_plugin_registry.h"\n',
        '#include "remote_scales.h"\n'
        '#include "remote_scales_plugin_registry.h"\n'
        "\n"
        "// %s: the scan cadence is a firmware policy decision, not a library\n"
        "// constant. The globals live in src/display/plugins/BLEScalePlugin.cpp\n"
        "// together with the policy and the measurements behind it; they are read\n"
        "// below each time an async scan starts. See scripts/patch_ble_scan_duty.py.\n"
        'extern "C" {\n'
        "extern uint16_t gm_ble_scan_interval_ms;\n"
        "extern uint16_t gm_ble_scan_window_ms;\n"
        "}\n" % MARKER,
    ),
    (
        "  NimBLEDevice::getScan()->setInterval(500);\n"
        "  NimBLEDevice::getScan()->setWindow(100);\n",
        "  // %s: cadence owned by the firmware; see the declaration at the top of\n"
        "  // this file. esp-nimble-cpp 2.x takes both values in milliseconds.\n"
        "  NimBLEDevice::getScan()->setInterval(gm_ble_scan_interval_ms);\n"
        "  NimBLEDevice::getScan()->setWindow(gm_ble_scan_window_ms);\n" % MARKER,
    ),
]

# (anchor, replacement, expected occurrence count)
HUNKS = [(old, new, 1) for old, new in LEGACY_HUNKS]

PATCHES = [gm_patch.Patch(MARKER, VERSION, HUNKS)]


def unpatch_legacy(text):
    """The pristine text under a marker-based patch, or None if it is not one."""
    if MARKER not in text or gm_patch.stamps_in(text):
        return None
    for old, new in reversed(LEGACY_HUNKS):
        if text.count(new) != 1:
            return None
        text = text.replace(new, old)
    return text


def recover_gm_orig(path, log=print):
    """Write path.gm-orig from a marker-patched path if it reverses to upstream."""
    orig = path + ".gm-orig"
    if os.path.exists(orig):
        return False
    with open(path, "rb") as handle:
        text = handle.read().decode("utf-8")
    pristine = unpatch_legacy(text)
    if pristine is None:
        return False
    data = pristine.encode("utf-8")
    if gm_patch.sha256(data) not in PRISTINE_SHA256:
        return False
    tmp = orig + ".gm-tmp"
    with open(tmp, "wb") as handle:
        handle.write(data)
    os.replace(tmp, orig)
    log("gm-patch: %s: recovered .gm-orig from a marker-patched file (%s)" % (OWNER, orig))
    return True


def main():
    path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),  # noqa: F821
                        "esp-arduino-ble-scales", "src", "remote_scales.cpp")
    if not os.path.isfile(path):
        # The library is fetched on first build, after pre-scripts run. There is
        # nothing to patch yet and the next build will catch it.
        print("gm-patch: %s: remote_scales.cpp not present for this env; skipping" % OWNER)
        return
    recover_gm_orig(path)
    gm_patch.run(OWNER, path, PRISTINE_SHA256, PATCHES)


if env is not None:
    main()
