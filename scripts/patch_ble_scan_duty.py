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
otherwise touch. The patch is idempotent and anchored on exact upstream text, so
a library bump fails the build loudly rather than silently reverting.
"""

import os

MARKER = "GM_BLE_SCAN_DUTY_PATCH"

HUNKS = [
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


def find_sources():
    """Every checked-out copy of remote_scales.cpp, one per PlatformIO env.

    Located from the working directory rather than from __file__, which SCons
    does not define for a script it exec()s as a pre-action.
    """
    root = os.path.join(os.getcwd(), ".pio", "libdeps")
    found = []
    if not os.path.isdir(root):
        return found
    for env in sorted(os.listdir(root)):
        path = os.path.join(root, env, "esp-arduino-ble-scales", "src", "remote_scales.cpp")
        if os.path.isfile(path):
            found.append(path)
    return found


def apply(path):
    with open(path, "r", encoding="utf-8", newline="") as handle:
        text = handle.read()

    if MARKER in text:
        return "already patched"

    for index, (old, new) in enumerate(HUNKS, start=1):
        count = text.count(old)
        if count != 1:
            raise SystemExit(
                "patch_ble_scan_duty: hunk %d matched %d times in %s, expected exactly 1.\n"
                "The library has changed upstream. Re-derive the patch against the new "
                "source rather than loosening this check: a silently skipped hunk brings "
                "back the scan-out disturbance it exists to fix." % (index, count, path)
            )
        text = text.replace(old, new)

    with open(path, "w", encoding="utf-8", newline="") as handle:
        handle.write(text)
    return "patched"


def main():
    paths = find_sources()
    if not paths:
        # The library is fetched on first build, after pre-scripts run. There is
        # nothing to patch yet and the next build will catch it.
        print("patch_ble_scan_duty: remote_scales.cpp not checked out yet, skipping")
        return
    for path in paths:
        print("patch_ble_scan_duty: %s (%s)" % (apply(path), path))


main()
