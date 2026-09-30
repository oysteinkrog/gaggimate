# ruff: noqa: F821 -- `Import` and `env` are injected by PlatformIO/SCons
#
# Post-build guard against silent sdkconfig drift.
#
# The dual-framework (pioarduino) build compiles ESP-IDF from source, so any
# config we don't pin in sdkconfig.*.defaults falls back to IDF's Kconfig
# default, which differs from the prebuilt sdkconfig the old pure-Arduino build
# used. That bit us once already: IDF 5.5 defaults FATFS to LFN_NONE (8.3 names
# only), which made SD-card files with 4-char extensions (".slog"/".json")
# unreadable and broke shot loading in the web UI ("Bad magic"). This guard
# fails the build if a required invariant regresses, so the class can't ship
# again unnoticed.
#
# What it checks, one printed line per assertion:
#   - REQUIRED: invariants every env with this script must hold.
#   - _check_optimization: the -O level the compiler really gets, read from the
#     env's CCFLAGS, not from CONFIG_COMPILER_OPTIMIZATION_* (gm-bzu.29).
#   - ENV_INVARIANTS: per-env symbol values keyed on PIOENV, including the ones
#     that must be OFF. A symbol no defaults file names can still drift in the
#     saved sdkconfig.<env> (menuconfig, a copy from a bench cache), and the
#     defaults check below cannot see that.
#   - _check_flash_size: the merged flash size matches the board.
#   - _check_defaults_applied: every line of the env's defaults chain reached
#     the merged config, `# CONFIG_X is not set` lines included, and every
#     chain file exists.
#
# The logic also runs outside SCons, for the host test and for checking a saved
# sdkconfig by hand:
#   python3 scripts/assert_sdkconfig.py --env display --config sdkconfig.display
# (see main() for the options; the build-flag check needs --ccflags there).
import argparse
import configparser
import os
import re
import sys

try:
    Import("env")
except NameError:  # imported by the host test or run from the command line
    env = None


# --- Reading a config ------------------------------------------------------

def parse_sdkconfig_h(text):
    """Symbol -> value from a merged sdkconfig.h. A bool that is on reads "1";
    a bool that is off has no #define at all."""
    return {
        k: v.strip()
        for k, v in re.findall(r"^#define\s+(CONFIG_[A-Za-z0-9_]+)\s+(.*)$", text, re.M)
    }


def parse_sdkconfig(text):
    """Symbol -> value from a saved sdkconfig or a defaults file.
    `# CONFIG_X is not set` reads as "n"."""
    values = {}
    for line in text.splitlines():
        line = line.strip()
        m = re.match(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$", line)
        if m:
            values[m.group(1)] = m.group(2).strip()
            continue
        m = re.match(r"^#\s*(CONFIG_[A-Za-z0-9_]+) is not set$", line)
        if m:
            values[m.group(1)] = "n"
    return values


def parse_config_text(text):
    """Either format; sdkconfig.h is recognised by its #define lines."""
    if re.search(r"^#define\s+CONFIG_", text, re.M):
        return parse_sdkconfig_h(text)
    return parse_sdkconfig(text)


def value_matches(want, got):
    """`want` is a defaults-file value ("y", "n", a number, a quoted string);
    `got` is what the merged config holds, or None when the symbol is absent."""
    if want == "n":
        return got is None or got == "n"
    if want == "y":
        return got in ("1", "y")
    return got is not None and got.strip() == want.strip()


def shown(got):
    return "absent (off)" if got is None else got


# --- Invariants every env holds -------------------------------------------

# Each entry: (description, predicate(values) -> bool, remediation hint).
REQUIRED = [
    (
        "FATFS long filenames enabled (LFN_HEAP or LFN_STACK; NOT LFN_NONE)",
        lambda v: (value_matches("y", v.get("CONFIG_FATFS_LFN_HEAP"))
                   or value_matches("y", v.get("CONFIG_FATFS_LFN_STACK")))
        and not value_matches("y", v.get("CONFIG_FATFS_LFN_NONE")),
        "Set CONFIG_FATFS_LFN_HEAP=y + CONFIG_FATFS_MAX_LFN=255 in "
        "sdkconfig.common.defaults, then `pio run -e <env> -t fullclean`.",
    ),
    (
        "CPU at 240 MHz (IDF default 160 MHz leaks through if unpinned)",
        lambda v: v.get("CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ") == "240",
        "Set CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y in sdkconfig.common.defaults, "
        "delete the cached sdkconfig.<env>, then rebuild.",
    ),
]


# --- The optimization level the compiler really gets ----------------------

# The level each env must compile at, IDF components and project sources
# alike. The display envs add -O2 to build_flags and strip every -Os with
# build_unflags (platformio.ini, [display_common] and [env:display]); the
# controller keeps IDF's -Os. An env not listed is reported, not checked.
EXPECTED_OPT = {
    "controller": "-Os",
}
EXPECTED_OPT_PREFIX = (("display", "-O2"),)

# The flag IDF's CMake emits for each CONFIG_COMPILER_OPTIMIZATION_* choice.
KCONFIG_OPT_FLAG = {
    "CONFIG_COMPILER_OPTIMIZATION_SIZE": "-Os",
    "CONFIG_COMPILER_OPTIMIZATION_PERF": "-O2",
    "CONFIG_COMPILER_OPTIMIZATION_DEBUG": "-Og",
    "CONFIG_COMPILER_OPTIMIZATION_NONE": "-O0",
}

_OPT_RE = re.compile(r"^-O[0-3sgz]?$")


def _flat(flags):
    out = []
    for f in flags or []:
        if isinstance(f, (list, tuple)):
            out.extend(str(x) for x in f)
        else:
            out.append(str(f))
    return out


def last_opt(flags):
    opts = [f for f in _flat(flags) if _OPT_RE.match(f)]
    return opts[-1] if opts else None


def effective_opt(ccflags, unflags, values):
    """(project level, IDF component level) as GCC sees them: the last -O wins.

    Project sources compile with the env's CCFLAGS, which PlatformIO has
    already passed through build_unflags. Each IDF component compiles with a
    clone of the same env plus the flags IDF's CMake emits for it, among them
    the -O for CONFIG_COMPILER_OPTIMIZATION_*, appended if not already present
    and then filtered by the same build_unflags (prepare_build_envs in the
    platform's espidf.py). So the Kconfig symbol decides the level only when
    nothing strips its flag.
    """
    flags = _flat(ccflags)
    unset = set(_flat(unflags))
    project = last_opt(flags)
    kconfig = next((flag for sym, flag in KCONFIG_OPT_FLAG.items()
                    if value_matches("y", values.get(sym))), None)
    component = list(flags)
    if kconfig and kconfig not in component:
        component.append(kconfig)
    component = [f for f in component if f not in unset]
    return project, last_opt(component)


def expected_opt(pioenv):
    if pioenv in EXPECTED_OPT:
        return EXPECTED_OPT[pioenv]
    for prefix, level in EXPECTED_OPT_PREFIX:
        if pioenv and pioenv.startswith(prefix):
            return level
    return None


def check_optimization(pioenv, ccflags, unflags, values):
    """[(ok, description, hint)] for the project and the IDF component level."""
    want = expected_opt(pioenv)
    project, component = effective_opt(ccflags, unflags, values)
    results = []
    for what, got in (("project sources", project), ("IDF components", component)):
        if want is None:
            results.append((True, f"{what} compile at {got or 'no -O'} "
                                  f"(no expected level for env {pioenv})", ""))
            continue
        results.append((
            got == want,
            f"{what} compile at {want} (last -O in the real flags is {got or 'none'})",
            "The effective level comes from build_flags and build_unflags in "
            "platformio.ini, not from CONFIG_COMPILER_OPTIMIZATION_*. For the "
            "display envs, -O2 in [display_common] and -Os in build_unflags "
            "must travel together.",
        ))
    return results


# --- Per-env values, the ones that must be off included -------------------

MEMPROT = "CONFIG_ESP_SYSTEM_MEMPROT_FEATURE"
XIP = "CONFIG_SPIRAM_XIP_FROM_PSRAM"
FETCH = "CONFIG_SPIRAM_FETCH_INSTRUCTIONS"
RODATA = "CONFIG_SPIRAM_RODATA"
RT_STATS = "CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS"
TRACE = "CONFIG_FREERTOS_USE_TRACE_FACILITY"
QIO = "CONFIG_ESPTOOLPY_FLASHMODE_QIO"
SND_BUF = "CONFIG_LWIP_TCP_SND_BUF_DEFAULT"

# What production ships, and why each line is here:
#   MEMPROT on: only the kdev bench turns it off, to run hot-loaded code.
#   XIP, FETCH, RODATA off: owner's decision of 2026-09-09
#     (sdkconfig.gaggimate.defaults, sdkconfig.xip.defaults).
#   RT_STATS, TRACE off: the bench's /api/debug/tasks instrumentation
#     (sdkconfig.loadtest.defaults) adds a timer read to every context switch.
#   QIO: gm-2cl.19 (sdkconfig.qio.defaults).
#   SND_BUF 5760: the web UI bundle load time (sdkconfig.gaggimate.defaults).
PRODUCTION = {
    MEMPROT: "y",
    XIP: "n",
    FETCH: "n",
    RODATA: "n",
    RT_STATS: "n",
    TRACE: "n",
    QIO: "y",
    SND_BUF: "5760",
}

# The bench envs differ from production only where their chain says so.
LOADTEST = {**PRODUCTION, RT_STATS: "y", TRACE: "y"}
ENV_INVARIANTS = {
    "display": PRODUCTION,
    "display-loadtest": LOADTEST,
    "display-loadtest-xip": {**LOADTEST, FETCH: "y"},
    "display-kdev": {**PRODUCTION, MEMPROT: "n"},
}


def check_env_invariants(pioenv, values):
    """[(ok, description, hint)], one per symbol this env pins."""
    results = []
    for symbol, want in ENV_INVARIANTS.get(pioenv, {}).items():
        got = values.get(symbol)
        results.append((
            value_matches(want, got),
            f"{pioenv}: {symbol}={want} (merged config: {shown(got)})",
            f"The env {pioenv} must build with {symbol}={want}. If it is set "
            f"in the saved sdkconfig.{pioenv}, a hand edit or a copied cache "
            f"put it there: delete that file and rebuild so the defaults chain "
            f"decides. If the chain changed on purpose, change ENV_INVARIANTS "
            f"in scripts/assert_sdkconfig.py in the same commit.",
        ))
    return results


# --- Flash size ------------------------------------------------------------

def check_flash_size(board_size, values):
    """Board-aware invariant: merged CONFIG_ESPTOOLPY_FLASHSIZE == board flash size.

    PlatformIO does not derive this config from board.json: espidf.py only
    compares the two and prints "Warning! Flash memory size mismatch detected",
    which is easy to miss in a 2000-line build log. Getting it wrong is not
    cosmetic: the IDF-built bootloader writes the configured size into the image
    header and IDF clamps usable flash to it, so a 2 MB header on a 16 MB board
    leaves both OTA slots and the LittleFS partition unaddressable.
    """
    if not board_size:
        return []
    got = values.get("CONFIG_ESPTOOLPY_FLASHSIZE")
    if got is None:
        return [(False, "CONFIG_ESPTOOLPY_FLASHSIZE present in merged config",
                 "Pin CONFIG_ESPTOOLPY_FLASHSIZE_<n>MB=y in this env's sdkconfig "
                 "defaults.")]
    idf_size = got.strip('"')
    return [(
        idf_size.lower() == str(board_size).lower(),
        f"flash size matches board ({board_size}); merged config says {idf_size}",
        f"Pin CONFIG_ESPTOOLPY_FLASHSIZE_{str(board_size).upper().replace('MB', '')}MB=y "
        f"(and CONFIG_ESPTOOLPY_FLASHSIZE=\"{board_size}\") in the last "
        f"sdkconfig.*.defaults this env lists in SDKCONFIG_DEFAULTS, delete the "
        f"cached sdkconfig.<env>, then rebuild.",
    )]


# --- The defaults chain ----------------------------------------------------

# Symbols a Kconfig choice group is expected to drop, with the reason. Everything
# else that fails to reach the merged config is a finding.
ALLOW_UNAPPLIED = {
    # display-headless-8m lists flash8m.defaults after gaggimate.defaults, and
    # picking the 8 MB member of the FLASHSIZE choice unsets the 16 MB one.
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB",
}


def chain_from_cmake_args(extra):
    """The SDKCONFIG_DEFAULTS files in a board_build.cmake_extra_args value."""
    if isinstance(extra, (list, tuple)):
        extra = " ".join(extra)
    m = re.search(r"-DSDKCONFIG_DEFAULTS=([^\s]+)", extra or "")
    if not m:
        return []
    return [p for p in m.group(1).split(";") if p]


def check_defaults_applied(chain, project_dir, values):
    """Every line in this env's defaults chain reached the merged config.

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

    `# CONFIG_X is not set` lines count as CONFIG_X=n. A chain file that does
    not exist, or an env with no chain at all, is a failure: IDF would build
    without it and the check would have nothing to compare.

    Returns [(ok, description, hint)]: one line for the chain, one per failure.
    """
    if not chain:
        return [(False, "SDKCONFIG_DEFAULTS chain found in board_build.cmake_extra_args",
                 "Every env this script runs for names its defaults chain in "
                 "board_build.cmake_extra_args (-DSDKCONFIG_DEFAULTS=a;b;c).")]
    results = []
    wanted = {}
    for name in chain:
        path = name if os.path.isabs(name) else os.path.join(project_dir, name)
        if not os.path.isfile(path):
            results.append((False, f"defaults chain file {name} exists",
                            f"{path} is listed in SDKCONFIG_DEFAULTS and is missing. "
                            f"Restore it or remove it from the chain."))
            continue
        with open(path, encoding="utf-8") as handle:
            for lineno, line in enumerate(handle, 1):
                parsed = parse_sdkconfig(line)
                for symbol, value in parsed.items():
                    wanted[symbol] = (value, name, lineno)

    for symbol, (value, name, lineno) in sorted(wanted.items()):
        if symbol in ALLOW_UNAPPLIED:
            continue
        got = values.get(symbol)
        if not value_matches(value, got):
            results.append((
                False,
                f"{symbol}={value} reached the merged config (it is {shown(got)})",
                f"{name}:{lineno} asks for this and the build did not take it. "
                f"Either the symbol no longer exists / is unreachable in this IDF "
                f"and the line should go, or a stale sdkconfig.<env> is winning -- "
                f"delete it and rebuild.",
            ))
    if not any(not ok for ok, _, _ in results):
        results.append((True, f"all {len(wanted)} lines of the defaults chain applied "
                              f"({';'.join(chain)})", ""))
    return results


# --- Running it ------------------------------------------------------------

def run_checks(pioenv, values, chain, project_dir, ccflags=None, unflags=None,
               board_size=None):
    """Every assertion as (ok, description, hint). ccflags None skips the
    compile-flag check (a host run that was not given the flags)."""
    results = [(pred(values), desc, hint) for desc, pred, hint in REQUIRED]
    if ccflags is not None:
        results += check_optimization(pioenv, ccflags, unflags or [], values)
    results += check_env_invariants(pioenv, values)
    results += check_flash_size(board_size, values)
    results += check_defaults_applied(chain, project_dir, values)
    return results


def report(results, source):
    """Print one guard line per assertion; return True when all passed."""
    for ok, desc, _ in results:
        print(f"sdkconfig guard: {'ok  ' if ok else 'FAIL'} {desc}")
    failures = [(desc, hint) for ok, desc, hint in results if not ok]
    if failures:
        print("\n*** sdkconfig guard FAILED -- merged config violates required invariants:")
        for desc, hint in failures:
            print(f"  - {desc}\n      fix: {hint}")
        print(f"  (checked {source})\n")
        return False
    print(f"sdkconfig guard: OK ({len(results)} assertion(s), checked {source})")
    return True


def _scons_assert(*_args, **_kwargs):
    sdkconfig_h = os.path.join(env.subst("$BUILD_DIR"), "config", "sdkconfig.h")
    if not os.path.isfile(sdkconfig_h):
        # No merged config (e.g. native env): nothing to assert.
        return
    with open(sdkconfig_h, "r", encoding="utf-8", errors="replace") as f:
        values = parse_sdkconfig_h(f.read())
    try:
        extra = env.GetProjectOption("board_build.cmake_extra_args", "")
    except Exception:
        extra = ""
    ccflags = (_flat(env.get("CCFLAGS")) + _flat(env.get("CFLAGS"))
               + _flat(env.get("CXXFLAGS")))
    results = run_checks(
        pioenv=env["PIOENV"],
        values=values,
        chain=chain_from_cmake_args(extra),
        project_dir=env.subst("$PROJECT_DIR"),
        ccflags=ccflags,
        unflags=env.get("BUILD_UNFLAGS"),
        board_size=env.BoardConfig().get("upload.flash_size", None),
    )
    if not report(results, sdkconfig_h):
        env.Exit(1)


# --- Command line (host test, checking a saved sdkconfig) -----------------

def ini_option(ini_path, pioenv, option):
    """An option of [env:<pioenv>], following `extends` and falling back to
    [env]. No ${...} interpolation, which the cmake args never use."""
    cp = configparser.ConfigParser(interpolation=None, strict=False)
    cp.read(ini_path, encoding="utf-8")
    section, seen = f"env:{pioenv}", set()
    while section and section not in seen and cp.has_section(section):
        seen.add(section)
        if cp.has_option(section, option):
            return cp.get(section, option)
        parent = cp.get(section, "extends", fallback="").strip()
        section = parent if cp.has_section(parent) else f"env:{parent}" if parent else ""
    if cp.has_option("env", option):
        return cp.get("env", option)
    return ""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__ or "sdkconfig guard")
    ap.add_argument("--env", required=True, help="PIOENV to check against")
    ap.add_argument("--config", required=True,
                    help="a saved sdkconfig.<env> or a merged sdkconfig.h")
    ap.add_argument("--project-dir", default=os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    ap.add_argument("--chain", help="semicolon list; default: from platformio.ini")
    ap.add_argument("--ccflags", help="compile flags, space separated; omit to skip")
    ap.add_argument("--unflags", default="", help="build_unflags, space separated")
    ap.add_argument("--board-flash-size", help="e.g. 16MB; omit to skip")
    args = ap.parse_args(argv)

    with open(args.config, encoding="utf-8", errors="replace") as f:
        values = parse_config_text(f.read())
    if args.chain is not None:
        chain = [p for p in args.chain.split(";") if p]
    else:
        extra = ini_option(os.path.join(args.project_dir, "platformio.ini"),
                           args.env, "board_build.cmake_extra_args")
        chain = chain_from_cmake_args(extra)
    results = run_checks(
        pioenv=args.env,
        values=values,
        chain=chain,
        project_dir=args.project_dir,
        ccflags=args.ccflags.split() if args.ccflags is not None else None,
        unflags=args.unflags.split(),
        board_size=args.board_flash_size,
    )
    return 0 if report(results, args.config) else 1


if env is not None:
    # Run after the firmware ELF is built, so the merged sdkconfig.h exists.
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _scons_assert)
elif __name__ == "__main__":
    sys.exit(main())
