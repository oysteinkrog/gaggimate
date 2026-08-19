# ruff: noqa: F821 — `env` is injected by PlatformIO/SCons at runtime
#
# Post-build guard against silent sdkconfig drift.
#
# The dual-framework (pioarduino) build compiles ESP-IDF from source, so any
# config we don't pin in sdkconfig.*.defaults falls back to IDF's Kconfig
# default — which differs from the prebuilt sdkconfig the old pure-Arduino build
# used. That bit us once already: IDF 5.5 defaults FATFS to LFN_NONE (8.3 names
# only), which made SD-card files with 4-char extensions (".slog"/".json")
# unreadable and broke shot loading in the web UI ("Bad magic"). This guard
# fails the build if a required invariant regresses, so the class can't ship
# again unnoticed. Add new invariants to REQUIRED below as they're discovered;
# invariants that need more than the config text (e.g. the board) go in their own
# _check_* function, called from assert_sdkconfig.
#
# On top of the named invariants, _check_defaults_applied verifies the weaker
# but broader property that every line in this env's sdkconfig.*.defaults chain
# actually reached the merged config -- see that function for why a line can
# silently do nothing.
import os
import re

Import("env")

# Each entry: (human description, predicate(text) -> bool, remediation hint).
# `text` is the merged sdkconfig.h emitted into the build's config/ dir.
REQUIRED = [
    (
        "FATFS long filenames enabled (LFN_HEAP or LFN_STACK; NOT LFN_NONE)",
        lambda t: ("#define CONFIG_FATFS_LFN_HEAP 1" in t
                   or "#define CONFIG_FATFS_LFN_STACK 1" in t)
        and "#define CONFIG_FATFS_LFN_NONE 1" not in t,
        "Set CONFIG_FATFS_LFN_HEAP=y + CONFIG_FATFS_MAX_LFN=255 in "
        "sdkconfig.common.defaults, then `pio run -e <env> -t fullclean`.",
    ),
    (
        "CPU at 240 MHz (IDF default 160 MHz leaks through if unpinned)",
        lambda t: "#define CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ 240" in t,
        "Set CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y in sdkconfig.common.defaults, "
        "delete the cached sdkconfig.<env>, then rebuild.",
    ),
    (
        "Compiler optimization = SIZE (-Os), not the slower IDF default -Og",
        lambda t: "#define CONFIG_COMPILER_OPTIMIZATION_SIZE 1" in t,
        "Set CONFIG_COMPILER_OPTIMIZATION_SIZE=y in sdkconfig.common.defaults, "
        "then `pio run -e <env> -t fullclean`.",
    ),
]


def _check_flash_size(text):
    """Board-aware invariant: merged CONFIG_ESPTOOLPY_FLASHSIZE == board flash size.

    Kept out of REQUIRED because it needs the board, not just the config text.
    PlatformIO does not derive this config from board.json — espidf.py only
    compares the two and prints "Warning! Flash memory size mismatch detected",
    which is easy to miss in a 2000-line build log. Getting it wrong is not
    cosmetic: the IDF-built bootloader writes the configured size into the image
    header and IDF clamps usable flash to it, so a 2 MB header on a 16 MB board
    leaves both OTA slots and the LittleFS partition unaddressable.

    Returns [] when satisfied or not checkable, else [(description, hint)].
    """
    board_size = env.BoardConfig().get("upload.flash_size", None)
    if not board_size:
        return []
    m = re.search(r'#define\s+CONFIG_ESPTOOLPY_FLASHSIZE\s+"([^"]+)"', text)
    if m is None:
        return [(
            "CONFIG_ESPTOOLPY_FLASHSIZE present in merged config",
            "The merged sdkconfig.h has no CONFIG_ESPTOOLPY_FLASHSIZE at all. "
            "Pin CONFIG_ESPTOOLPY_FLASHSIZE_<n>MB=y in this env's sdkconfig "
            "defaults.",
        )]
    idf_size = m.group(1)
    if idf_size.lower() == str(board_size).lower():
        return []
    return [(
        f"flash size matches board ({board_size}); merged config says {idf_size}",
        f"Pin CONFIG_ESPTOOLPY_FLASHSIZE_{board_size.upper().replace('MB', '')}MB=y "
        f"(and CONFIG_ESPTOOLPY_FLASHSIZE=\"{board_size}\") in the last "
        f"sdkconfig.*.defaults this env lists in SDKCONFIG_DEFAULTS, delete the "
        f"cached sdkconfig.<env>, then rebuild.",
    )]


# Symbols a Kconfig choice group is expected to drop, with the reason. Everything
# else that fails to reach the merged config is a finding.
ALLOW_UNAPPLIED = {
    # display-headless-8m lists flash8m.defaults after gaggimate.defaults, and
    # picking the 8 MB member of the FLASHSIZE choice unsets the 16 MB one.
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB",
}


def _defaults_chain():
    """The SDKCONFIG_DEFAULTS files this env feeds to IDF, in apply order."""
    try:
        extra = env.GetProjectOption("board_build.cmake_extra_args", "")
    except Exception:
        return []
    if isinstance(extra, (list, tuple)):
        extra = " ".join(extra)
    m = re.search(r"-DSDKCONFIG_DEFAULTS=([^\s]+)", extra or "")
    if not m:
        return []
    return [p for p in m.group(1).split(";") if p]


def _check_defaults_applied(text):
    """Every `CONFIG_X=...` line in this env's defaults reached the merged config.

    A defaults file is not validated against Kconfig. A symbol that was renamed
    between IDF releases, or one whose dependencies the rest of the config makes
    unreachable, is dropped in silence: the build succeeds and the line stays in
    the file looking authoritative. Worse, `sdkconfig.<env>` is a saved config
    whose existing values outrank the defaults, so editing a defaults file on a
    machine that already has one changes nothing at all -- while CI, checking out
    fresh, gets the new value. That divergence is invisible without this check.
    Three cases this caught: CONFIG_MBEDTLS_KEY_EXCHANGE_DHE_PSK was never
    reachable (it needs MBEDTLS_DHM_C, which IDF leaves off); the coredump block
    was inert for a whole build cycle behind a stale sdkconfig.<env>; and
    ESP_COREDUMP_STACK_SIZE=1024 was quietly raised to 1792 by a Kconfig range
    that narrows once task stacks may live in PSRAM. A clamped value counts as
    not applied, so the defaults file has to state what IDF will really use.

    Returns [] when satisfied, else [(description, hint)].
    """
    defines = dict(re.findall(r"^#define\s+(CONFIG_[A-Za-z0-9_]+)\s+(.*)$", text, re.M))

    wanted = {}
    for path in _defaults_chain():
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as handle:
            for lineno, line in enumerate(handle, 1):
                m = re.match(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$", line.strip())
                if m:
                    wanted[m.group(1)] = (m.group(2), path, lineno)

    failures = []
    for symbol, (value, path, lineno) in sorted(wanted.items()):
        if symbol in ALLOW_UNAPPLIED:
            continue
        got = defines.get(symbol)
        if value == "n":
            # Kconfig omits a disabled bool entirely; a #define means it is on.
            ok = got is None
        elif value == "y":
            ok = got == "1"
        else:
            ok = got is not None and got.strip() == value.strip()
        if not ok:
            shown = "absent" if got is None else got.strip()
            failures.append((
                f"{symbol}={value} reached the merged config (it is {shown})",
                f"{path}:{lineno} asks for this and the build did not take it. "
                f"Either the symbol no longer exists / is unreachable in this IDF "
                f"and the line should go, or a stale sdkconfig.<env> is winning -- "
                f"delete it and rebuild.",
            ))
    return failures


def assert_sdkconfig(*_args, **_kwargs):
    sdkconfig_h = os.path.join(env.subst("$BUILD_DIR"), "config", "sdkconfig.h")
    if not os.path.isfile(sdkconfig_h):
        # No merged config (e.g. native env) — nothing to assert.
        return
    with open(sdkconfig_h, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    failures = []
    for desc, predicate, hint in REQUIRED:
        if not predicate(text):
            failures.append((desc, hint))
    failures.extend(_check_flash_size(text))
    failures.extend(_check_defaults_applied(text))

    if failures:
        print("\n*** sdkconfig guard FAILED — merged config violates required invariants:")
        for desc, hint in failures:
            print(f"  - {desc}\n      fix: {hint}")
        print(f"  (checked {sdkconfig_h})\n")
        env.Exit(1)
    else:
        print(f"sdkconfig guard: OK ({len(REQUIRED) + 1} invariant(s) satisfied, "
              f"every defaults line applied)")


# Run after the firmware ELF is built, so the merged sdkconfig.h exists.
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", assert_sdkconfig)
