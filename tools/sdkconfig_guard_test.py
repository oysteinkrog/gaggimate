#!/usr/bin/env python3
"""Host test for scripts/assert_sdkconfig.py (gm-bzu.29).

Runs the build guard outside PlatformIO against the tracked sdkconfig files
and against copies with one forbidden line injected each, and checks that it
passes the first and fails every copy on the assertion that line breaks.
Nothing in the checkout is modified; copies go to --workdir (default: a new
temporary directory).

    python3 tools/sdkconfig_guard_test.py [--workdir DIR]

Exit status 0 when every case behaves as expected.
"""
import argparse
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUARD = os.path.join(ROOT, "scripts", "assert_sdkconfig.py")
DISPLAY_FLAGS = "-std=gnu++20 -O2 -mlongcalls"

spec = importlib.util.spec_from_file_location("assert_sdkconfig", GUARD)
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)

failures = []


def run(pioenv, config, *extra):
    cmd = [sys.executable, GUARD, "--env", pioenv, "--config", config,
           "--board-flash-size", "16MB", *extra]
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    return p.returncode, p.stdout + p.stderr


def expect(name, cond, output=""):
    print(f"{'PASS' if cond else 'FAIL'}  {name}")
    if not cond:
        failures.append(name)
        if output:
            print("      " + output.replace("\n", "\n      "))


def edit(src, dst, replacements):
    """Copy src to dst with each (regex, replacement) applied once; the regex
    must match, so a renamed symbol cannot make a case pass vacuously."""
    with open(src, encoding="utf-8") as f:
        text = f.read()
    for pattern, repl in replacements:
        text, n = re.subn(pattern, repl, text, count=1, flags=re.M)
        if n != 1:
            raise SystemExit(f"test setup: {pattern!r} not found in {src}")
    with open(dst, "w", encoding="utf-8") as f:
        f.write(text)


def not_set(sym):
    return f"# {sym} is not set"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workdir")
    args = ap.parse_args()
    work = args.workdir or tempfile.mkdtemp(prefix="sdkguard-")
    os.makedirs(work, exist_ok=True)

    # 1. The tracked configs, and any other local cache the table covers.
    for pioenv in ("display", "display-loadtest", "display-kdev"):
        path = os.path.join(ROOT, f"sdkconfig.{pioenv}")
        if not os.path.isfile(path):
            print(f"skip  sdkconfig.{pioenv} (no such file)")
            continue
        rc, out = run(pioenv, path, f"--ccflags={DISPLAY_FLAGS}", "--unflags=-Os")
        lines = [ln for ln in out.splitlines() if ln.startswith("sdkconfig guard: ok")]
        expect(f"sdkconfig.{pioenv} passes, one line per assertion "
               f"({len(lines)} ok lines)", rc == 0 and len(lines) >= 14, out)

    header = os.path.join(ROOT, ".pio", "build", "display", "config", "sdkconfig.h")
    if os.path.isfile(header):
        rc, out = run("display", header, f"--ccflags={DISPLAY_FLAGS}", "--unflags=-Os")
        expect("the display build's merged sdkconfig.h passes", rc == 0, out)
    else:
        print("skip  .pio/build/display/config/sdkconfig.h (not built)")

    # 2. One forbidden line at a time in a copy of sdkconfig.display.
    src = os.path.join(ROOT, "sdkconfig.display")
    M, X, F, R = guard.MEMPROT, guard.XIP, guard.FETCH, guard.RODATA
    S, T, Q, B = guard.RT_STATS, guard.TRACE, guard.QIO, guard.SND_BUF
    cases = [
        ("memory protection off (=n)", M, [(rf"^{M}=y$", f"{M}=n")]),
        ("memory protection off (is not set)", M, [(rf"^{M}=y$", not_set(M))]),
        ("instruction fetch from PSRAM", F, [(rf"^{re.escape(not_set(F))}$", f"{F}=y")]),
        ("rodata in PSRAM", R, [(rf"^{re.escape(not_set(R))}$", f"{R}=y")]),
        ("XIP from PSRAM", X, [(rf"^{re.escape(not_set(X))}$", f"{X}=y")]),
        ("run-time stats", S, [(rf"^{re.escape(not_set(S))}$", f"{S}=y")]),
        ("trace facility", T, [(rf"^{re.escape(not_set(T))}$", f"{T}=y")]),
        ("DIO instead of QIO", Q, [(rf"^{Q}=y$", not_set(Q))]),
        ("TCP_SND_BUF 2880", B, [(rf"^{B}=5760$", f"{B}=2880")]),
    ]
    for i, (name, symbol, repl) in enumerate(cases):
        dst = os.path.join(work, f"sdkconfig.display.case{i}")
        edit(src, dst, repl)
        rc, out = run("display", dst, f"--ccflags={DISPLAY_FLAGS}", "--unflags=-Os")
        named = re.search(rf"^sdkconfig guard: FAIL display: {symbol}=", out, re.M)
        expect(f"display with {name} fails on {symbol}", rc != 0 and named, out)

    # The bench envs are held to their own table: memory protection off is
    # fine on kdev, and the stats are required on loadtest.
    dst = os.path.join(work, "sdkconfig.display-kdev.from-display")
    edit(src, dst, [(rf"^{M}=y$", f"{M}=n"),
                    (r"^CONFIG_APP_RETRIEVE_LEN_ELF_SHA=9$",
                     "CONFIG_APP_RETRIEVE_LEN_ELF_SHA=16")])
    rc, out = run("display-kdev", dst)
    expect("display-kdev accepts memory protection off", rc == 0, out)
    rc, out = run("display-loadtest", src, "--chain=sdkconfig.common.defaults")
    expect("display-loadtest rejects a config without run-time stats",
           rc != 0 and re.search(rf"FAIL display-loadtest: {S}=y", out), out)

    # 3. A missing chain file fails instead of printing OK.
    rc, out = run("display", src, "--chain=sdkconfig.common.defaults;sdkconfig.nope.defaults")
    expect("a missing chain file fails",
           rc != 0 and "FAIL defaults chain file sdkconfig.nope.defaults exists" in out, out)
    rc, out = run("display", src, "--chain=")
    expect("an empty chain fails", rc != 0, out)

    # 4. `# CONFIG_X is not set` in a defaults file is checked as =n.
    chain = os.path.join(work, "sdkconfig.notset.defaults")
    with open(chain, "w", encoding="utf-8") as f:
        f.write(not_set(M) + "\n")
    rc, out = run("display", src, f"--chain={chain}")
    expect("an `is not set` defaults line that did not apply fails",
           rc != 0 and f"FAIL {M}=n reached the merged config" in out, out)
    with open(chain, "w", encoding="utf-8") as f:
        f.write(not_set(F) + "\n")
    rc, out = run("display", src, f"--chain={chain}")
    expect("an `is not set` defaults line that applied passes", rc == 0, out)

    # 5. The optimization level comes from the flags, not the Kconfig symbol.
    size = {"CONFIG_COMPILER_OPTIMIZATION_SIZE": "y"}
    flags = ["-Os", "-std=gnu++20", "-O2", "-mlongcalls", "-Os"]
    kept = [f for f in flags if f != "-Os"]
    got = guard.effective_opt(kept, ["-Os"], size)
    expect(f"display flags: project and IDF at -O2 (got {got})", got == ("-O2", "-O2"))
    got = guard.effective_opt(["-O2"], [], size)
    expect(f"-O2 without the -Os unflag: IDF back at -Os (got {got})",
           got == ("-O2", "-Os"))
    res = guard.check_optimization("display", ["-O2"], [], size)
    expect("display fails when -Os is not unflagged", not all(ok for ok, _, _ in res))
    res = guard.check_optimization("controller", ["-Os"], [], size)
    expect("controller passes at -Os", all(ok for ok, _, _ in res))
    res = guard.check_optimization("display", ["-Os"], ["-Os"], size)
    expect("display fails with no -O2 left", not all(ok for ok, _, _ in res))

    if args.workdir is None:
        shutil.rmtree(work, ignore_errors=True)
    print(f"\n{'OK' if not failures else 'FAILED'}: "
          f"{len(failures)} failing case(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
