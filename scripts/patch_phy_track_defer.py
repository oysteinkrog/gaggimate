"""Let the firmware run the RF PHY's PLL tracking inside the display's dead time.

The fault
---------
esp_phy arms a 1.000 s periodic esp_timer, phy-track-pll-timer, whose callback
(phy_param_track_tot inside the closed PHY blob) recalibrates the radio PLL
against temperature drift. The callback averages ~500 us and its code and data
live in flash, so each tick is a burst of instruction fetches over the MSPI
bus -- the same bus the RGB panel's bounce refill reads its PSRAM framebuffer
through under a 96 us-per-buffer deadline with ~670 us of pool slack. The tail
of those ticks overruns the slack: one displaced band per second, phase-locked
to the timer (slips at x.26-x.33 of every second, shortfall 8 buffers), fully
independent of BLE scanning and WiFi traffic. Named by esp_timer_dump with
CONFIG_ESP_TIMER_PROFILING=y, then confirmed against the slip log's timing.

Why not just turn it off: CONFIG_ESP_PHY_DISABLE_PLL_TRACK exists but is
marked experimental, and the tracking is what keeps the radio stable across
temperature change -- this board sits next to an espresso boiler. Why not
stretch CONFIG_ESP_PHY_PLL_TRACK_PERIOD_MS: that only spaces the glitches out.

The patch
---------
The timer callback first offers the tick to the firmware through a weak hook:

    gm_phy_track_defer()  -- weak, returns false here, so every build without
                             a display (the controller firmware) keeps stock
                             behaviour to the instruction.

The display firmware overrides it (src/display/drivers/common/PanelClock.cpp):
it records the tick as pending and returns true, and the panel's VSYNC handler
releases a small task that calls gm_phy_track_pll_run() -- same lock, same
work, but started at the top of vertical blanking, where the scan-out consumes
nothing for ~1.3 ms and the refill then still holds its full pool slack. The
tracking cadence stays 1 s (quantized to the 23 ms frame, which thermal drift
cannot see); only its bus traffic moves into the window where the panel does
not care. If the panel is not running (standby teardown, pre-init, a build
with radios but no screen), the override reports a stale heartbeat and the
callback falls through to stock inline behaviour, so tracking never starves.

Anchored on exact upstream text and idempotent, so an IDF bump that reshapes
phy_common.c fails the build loudly instead of silently reverting.
"""

import os

MARKER = "GM_PHY_TRACK_DEFER_PATCH"

HUNKS = [
    (
        "static void phy_track_pll_timer_callback(void* arg)\n"
        "{\n"
        "    _lock_t phy_lock = phy_get_lock();\n"
        "    _lock_acquire(&phy_lock);\n"
        "    phy_track_pll_internal();\n"
        "    _lock_release(&phy_lock);\n"
        "}\n",
        "// %s: the display firmware may take over the scheduling of this\n"
        "// tick, because the callback's flash fetches ride the same MSPI bus the\n"
        "// RGB panel refills its framebuffer over, and run at a moment the panel\n"
        "// has no slack for. The weak default keeps stock behaviour for every\n"
        "// firmware that does not override it. See scripts/patch_phy_track_defer.py.\n"
        "bool gm_phy_track_defer(void);\n"
        "bool __attribute__((weak)) gm_phy_track_defer(void)\n"
        "{\n"
        "    return false;\n"
        "}\n"
        "\n"
        "// %s: the deferred tick, exactly the work the timer callback did.\n"
        "void gm_phy_track_pll_run(void);\n"
        "void gm_phy_track_pll_run(void)\n"
        "{\n"
        "    _lock_t phy_lock = phy_get_lock();\n"
        "    _lock_acquire(&phy_lock);\n"
        "    phy_track_pll_internal();\n"
        "    _lock_release(&phy_lock);\n"
        "}\n"
        "\n"
        "static void phy_track_pll_timer_callback(void* arg)\n"
        "{\n"
        "    if (gm_phy_track_defer()) {\n"
        "        return;\n"
        "    }\n"
        "    _lock_t phy_lock = phy_get_lock();\n"
        "    _lock_acquire(&phy_lock);\n"
        "    phy_track_pll_internal();\n"
        "    _lock_release(&phy_lock);\n"
        "}\n" % (MARKER, MARKER),
    ),
]

PLATFORMIO_HOME = os.environ.get("PLATFORMIO_HOME_DIR", os.path.expanduser("~/.platformio"))
TARGET = os.path.join(PLATFORMIO_HOME, "packages", "framework-espidf", "components", "esp_phy", "src", "phy_common.c")


def apply(path):
    with open(path, "r", encoding="utf-8", newline="") as handle:
        text = handle.read()

    if MARKER in text:
        # The marker alone is not proof the rewrite landed whole (an
        # interrupted write or a hand edit can leave the marker without the
        # body), so verify every replacement is present before trusting it.
        for index, (_, new) in enumerate(HUNKS, start=1):
            if text.count(new) != 1:
                raise SystemExit(
                    "patch_phy_track_defer: %s carries the patch marker but hunk %d's "
                    "replacement is not intact. Restore the pristine source (%s.gm-orig, "
                    "or reinstall framework-espidf) and rebuild." % (path, index, path)
                )
        return "already patched"

    for index, (old, new) in enumerate(HUNKS, start=1):
        count = text.count(old)
        if count != 1:
            raise SystemExit(
                "patch_phy_track_defer: hunk %d matched %d times in %s, expected exactly 1.\n"
                "phy_common.c has changed upstream. Re-derive the patch against the new "
                "source rather than loosening this check: a silently skipped hunk brings "
                "back the once-per-second displaced band it exists to fix." % (index, count, path)
            )
        text = text.replace(old, new)

    orig = path + ".gm-orig"
    if not os.path.exists(orig):
        with open(path, "r", encoding="utf-8", newline="") as handle:
            pristine = handle.read()
        with open(orig, "w", encoding="utf-8", newline="") as handle:
            handle.write(pristine)

    # Temp-and-rename so a killed build can never leave phy_common.c half
    # written; the marker check above would then misread the truncation as
    # "already patched" and ship a broken PHY.
    tmp = path + ".gm-tmp"
    with open(tmp, "w", encoding="utf-8", newline="") as handle:
        handle.write(text)
    os.replace(tmp, path)
    return "patched"


def main():
    if not os.path.isfile(TARGET):
        raise SystemExit("patch_phy_track_defer: %s not found" % TARGET)
    print("patch_phy_track_defer: %s (%s)" % (apply(TARGET), TARGET))


main()
