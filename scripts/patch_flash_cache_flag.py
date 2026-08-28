"""Expose "the flash cache is down" as a flag the LCD refill ISR can read.

Why this exists
---------------
With CONFIG_LCD_RGB_ISR_IRAM_SAFE enabled, the RGB panel's VSYNC and GDMA EOF
interrupts keep firing during a flash operation (they gain ESP_INTR_FLAG_IRAM,
so esp_intr_noniram_disable no longer masks them). That is the point of the
option: today a shot-log flush or settings save masks the refill outright and
the panel scans stale bounce buffers until the write finishes. But the refill's
memcpy reads the framebuffer out of PSRAM, and on the ESP32-S3 PSRAM sits
behind the same cache a flash operation disables, so a refill that runs during
the write would fault rather than merely show stale lines. The EOF handler
therefore needs one bit of truth: is the cache usable right now? When it is
not, the handler advances its accounting without copying (the copy=false path
the VSYNC resync already uses), which turns "crash" into "a few stale bands
with exact position tracking".

Why a patch on cache_utils.c rather than spi_flash_guard_set()
--------------------------------------------------------------
The guard struct looks like the sanctioned hook for exactly this, and it is
not: with the modern esp_flash driver (CONFIG_SPI_FLASH_ROM_IMPL unset, which
is this build), nothing in the write path ever calls spi_flash_guard_get().
The cache disable runs through spi_flash_os_func_app.c straight into
spi_flash_disable_interrupts_caches_and_other_cpu(). A guard installed with
spi_flash_guard_set() compiles, installs, and never fires. The only functions
actually on the hot path are the two patched here.

Window semantics
----------------
The flag is raised after the op lock is taken and before anything is stalled
or disabled, and lowered only after the cache is restored. The window is a
strict superset of the true cache-down window, so the worst false positive is
a bounce buffer skipped while the cache was actually fine, which costs a few
stale scanlines on one frame. The flag lives in internal DRAM, which is not
behind the cache, and both cores see a plain 32-bit store immediately.

Idempotent: guarded by a marker string. Anchored on exact upstream text so an
IDF bump that rewrites these functions fails the build loudly here rather than
silently shipping without the flag.
"""

import os
import sys

MARKER = "GM_FLASH_CACHE_FLAG_PATCH"

HUNKS = [
    # The flag itself, plus the raise, just inside the op lock. One flash op
    # runs at a time (the lock serialises them), so a plain bool is enough.
    (
        "void IRAM_ATTR spi_flash_disable_interrupts_caches_and_other_cpu(void)\n"
        "{\n"
        "    assert(esp_task_stack_is_sane_cache_disabled());\n"
        "\n"
        "    spi_flash_op_lock();\n",

        "// " + MARKER + ": read by the RGB panel's bounce-refill ISR (see\n"
        "// scripts/patch_flash_cache_flag.py in the application repo). True from\n"
        "// just before the cache goes down until just after it is restored.\n"
        "volatile bool gm_flash_cache_down = false;\n"
        "\n"
        "void IRAM_ATTR spi_flash_disable_interrupts_caches_and_other_cpu(void)\n"
        "{\n"
        "    assert(esp_task_stack_is_sane_cache_disabled());\n"
        "\n"
        "    spi_flash_op_lock();\n"
        "    gm_flash_cache_down = true; // " + MARKER + "\n",
    ),
    # The clear, after the cache is restored and before the other core is
    # released. Everything after this point runs with a working cache.
    (
        "    spi_flash_restore_cache(cpuid, s_flash_op_cache_state[cpuid]);\n"
        "#if SOC_IDCACHE_PER_CORE\n"
        "    //only needed if cache(s) is per core\n"
        "    const uint32_t other_cpuid = (cpuid == 0) ? 1 : 0;\n"
        "    spi_flash_restore_cache(other_cpuid, s_flash_op_cache_state[other_cpuid]);\n"
        "#endif\n",

        "    spi_flash_restore_cache(cpuid, s_flash_op_cache_state[cpuid]);\n"
        "#if SOC_IDCACHE_PER_CORE\n"
        "    //only needed if cache(s) is per core\n"
        "    const uint32_t other_cpuid = (cpuid == 0) ? 1 : 0;\n"
        "    spi_flash_restore_cache(other_cpuid, s_flash_op_cache_state[other_cpuid]);\n"
        "#endif\n"
        "    gm_flash_cache_down = false; // " + MARKER + "\n",
    ),
]


def find_driver():
    # __file__ is undefined when SCons execs a pre-script, so locate the
    # framework relative to the PlatformIO home the same way the esp_lcd
    # patch does.
    home = os.environ.get("PLATFORMIO_HOME_DIR") or os.path.expanduser("~/.platformio")
    path = os.path.join(home, "packages", "framework-espidf", "components",
                        "spi_flash", "cache_utils.c")
    return path if os.path.isfile(path) else None


def apply(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if MARKER in text:
        print("patch_flash_cache_flag: already patched (%s)" % path)
        return
    orig = path + ".gm-orig"
    if not os.path.exists(orig):
        with open(orig, "w", encoding="utf-8") as f:
            f.write(text)
    for old, new in HUNKS:
        if old not in text:
            sys.stderr.write(
                "patch_flash_cache_flag: anchor not found in %s; the IDF was "
                "updated and this patch needs review. Anchor begins: %r\n"
                % (path, old[:80]))
            sys.exit(1)
        if text.count(old) != 1:
            sys.stderr.write(
                "patch_flash_cache_flag: anchor not unique in %s: %r\n"
                % (path, old[:80]))
            sys.exit(1)
        text = text.replace(old, new)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    print("patch_flash_cache_flag: patched %s" % path)


def main():
    path = find_driver()
    if path is None:
        print("patch_flash_cache_flag: cache_utils.c not found; skipping")
        return
    apply(path)


main()
