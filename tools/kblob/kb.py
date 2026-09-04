#!/usr/bin/env python3
"""Build, upload and bench a hot-loadable animation blob on the display-kdev
build (src/display/ui/default/bganim/KBlob.h).

    kb.py info                          what the device has loaded, buffer addresses
    kb.py build  AnimFoo.cpp [-o X]     compile + link one animation source into X.kblob
    kb.py upload X.kblob                install it (the device checks it was linked
                                        against the firmware it is running)
    kb.py bench  --anim N [--n 8] [--frames 2] [--which 15]
                                        cycle-count bench, prints a table
    kb.py run    AnimFoo.cpp --anim N   build + upload + bench in one go
    kb.py useblob 0|1                   route the live render loop into the blob

The source is compiled with the exact command PlatformIO uses for the
animation sources of the target env (compile_commands.json), so the object is
what the firmware would have contained. It is then linked at the device's two
blob buffers with every undefined symbol resolved to the running firmware's
address (from `nm` of .pio/build/<env>/firmware.elf), so the blob calls the
firmware's own bganim helpers, libc and libm. The device refuses a blob linked
against a different ELF than it is running: flash first, then build.

Cycle numbers: min_ms is the sum over all bands of the best of n runs, per
frame; it is the deterministic compute + memory cost with interrupts filtered
out. mean_ms includes them. A blob runs from IRAM while the firmware's kernels
run from flash through the instruction cache, so compare blob against blob
(upload the unchanged source once to get the placement offset) before calling
a delta a code win, and confirm any win with the production A/B
(/api/debug/anim?useblob=1 against useblob=0) before flashing it.

Device access (upload, bench, useblob) is serialised across processes with a
lock file, so parallel workers can share one board.
"""

import argparse
import fcntl
import hashlib
import json
import os
import re
import shlex
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
TOOLCHAIN = os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp-elf/bin")
PREFIX = os.path.join(TOOLCHAIN, "xtensa-esp32s3-elf-")
DEFAULT_DEVICE = os.environ.get("GM_DEVICE", "192.168.1.121")
DEFAULT_ENV = os.environ.get("GM_KBLOB_ENV", "display-kdev")
LOCK_PATH = os.path.join(REPO, ".pio", "kblob.lock")


def die(msg):
    print("kb: " + msg, file=sys.stderr)
    sys.exit(1)


def run(cmd, cwd=REPO, env=None, capture=False):
    e = dict(os.environ)
    e["PATH"] = TOOLCHAIN + os.pathsep + e.get("PATH", "")
    if env:
        e.update(env)
    r = subprocess.run(cmd, cwd=cwd, env=e, text=True, capture_output=capture)
    if r.returncode != 0:
        if capture:
            sys.stderr.write(r.stdout or "")
            sys.stderr.write(r.stderr or "")
        die("command failed: " + " ".join(shlex.quote(c) for c in cmd[:3]) + " ...")
    return r.stdout if capture else None


# ---- device -----------------------------------------------------------------


class Device:
    def __init__(self, host):
        self.base = "http://" + host

    def get(self, path, timeout=10):
        return self._req(path, None, timeout)

    def post(self, path, body, timeout=20):
        return self._req(path, body, timeout)

    def _req(self, path, body, timeout):
        req = urllib.request.Request(self.base + path, data=body, method="POST" if body is not None else "GET")
        if body is not None:
            req.add_header("Content-Type", "application/octet-stream")
        for attempt in range(3):
            try:
                with urllib.request.urlopen(req, timeout=timeout) as r:
                    return json.loads(r.read().decode())
            except urllib.error.HTTPError as e:
                try:
                    return json.loads(e.read().decode())
                except Exception:
                    die("%s -> HTTP %d" % (path, e.code))
            except (urllib.error.URLError, ConnectionError, TimeoutError) as e:
                if attempt == 2:
                    die("%s unreachable: %s" % (path, e))
                time.sleep(1.0)


class DeviceLock:
    def __init__(self):
        os.makedirs(os.path.dirname(LOCK_PATH), exist_ok=True)
        self.f = open(LOCK_PATH, "w")

    def __enter__(self):
        t0 = time.time()
        while True:
            try:
                fcntl.flock(self.f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return self
            except OSError:
                if time.time() - t0 > 600:
                    die("could not take the device lock in 10 minutes: " + LOCK_PATH)
                time.sleep(0.5)

    def __exit__(self, *a):
        fcntl.flock(self.f, fcntl.LOCK_UN)
        self.f.close()


# ---- build ------------------------------------------------------------------


def env_paths(env):
    build = os.path.join(REPO, ".pio", "build", env)
    return build, os.path.join(build, "firmware.elf")


def elf_sha_prefix(elf):
    h = hashlib.sha256()
    with open(elf, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.digest()[:8]


def work_dir(env):
    # Outside .pio/build/<env>: PlatformIO owns that tree and its compiledb
    # target recreates it, ELF included.
    d = os.path.join(REPO, ".pio", "kblob", env)
    os.makedirs(d, exist_ok=True)
    return d


def compile_command(env):
    """The SCons compile command for an animation source of `env`, minus its
    -o and input. Taken from compile_commands.json when that describes the env
    and cached per env, because `pio run -t compiledb` is one file for the
    whole project and wipes the env's build tree, so it cannot be run casually
    (and never automatically here)."""
    cache = os.path.join(work_dir(env), "compile_cmd.json")
    db_path = os.path.join(REPO, "compile_commands.json")
    marker = ".pio/build/%s/" % env
    toks = None
    if os.path.exists(db_path):
        for e in json.load(open(db_path)):
            if "bganim/Anim" in e["file"] and e["file"].endswith(".cpp"):
                t = shlex.split(e.get("command") or " ".join(e["arguments"]))
                if any(marker in x for x in t):
                    toks = t
                    break
    if toks is not None:
        tmp = cache + ".%d" % os.getpid()
        json.dump(toks, open(tmp, "w"))
        os.replace(tmp, cache)
    elif os.path.exists(cache):
        toks = json.load(open(cache))
    else:
        die(
            "no compile command for env %s. Run `pio run -e %s -t compiledb` once, BEFORE building the\n"
            "    firmware (it recreates .pio/build/%s), then `pio run -e %s`, flash, and retry." % (env, env, env, env)
        )
    out = []
    skip = False
    for t in toks:
        if skip:
            skip = False
            continue
        if t == "-o":
            skip = True
            continue
        if t.endswith(".cpp") and "bganim/Anim" in t:
            continue
        out.append(t)
    return out


def nm(path, extra=()):
    txt = run([PREFIX + "nm"] + list(extra) + [path], capture=True)
    syms = {}
    for line in txt.splitlines():
        parts = line.split()
        if len(parts) == 3:
            addr, typ, name = parts
            syms[name] = (int(addr, 16), typ)
        elif len(parts) == 2:
            syms[parts[1]] = (None, parts[0])
    return syms


def firmware_symbols(env, elf, sha_hex):
    """Defined symbols of the firmware the device runs, keyed by its ELF sha so
    the index survives the ELF being rebuilt or wiped underneath us."""
    cache = os.path.join(work_dir(env), "syms-%s.json" % sha_hex)
    if os.path.exists(cache):
        return json.load(open(cache))
    if not (elf and os.path.exists(elf)):
        die("no symbol index for firmware %s and no ELF at %s to build one from: rebuild and reflash" % (sha_hex, elf))
    if not elf_sha_prefix(elf).hex().startswith(sha_hex):
        die("device runs firmware %s but %s is %s: flash that build first" % (sha_hex, elf, elf_sha_prefix(elf).hex()))
    print("kb: indexing firmware symbols (once per firmware)")
    syms = {k: v[0] for k, v in nm(elf, ["--defined-only"]).items() if v[0] is not None}
    tmp = cache + ".%d" % os.getpid()
    json.dump(syms, open(tmp, "w"))
    os.replace(tmp, cache)
    return syms


LD_SCRIPT = """\
/* generated by tools/kblob/kb.py */
{assignments}
SECTIONS
{{
  . = {text_base:#x};
  .kbtext :
  {{
    /* Every literal ahead of every instruction: L32R only reaches backwards. */
    *(.literal .literal.* .iram1.literal .iram1.*.literal)
    *(.text .text.* .iram1 .iram1.*)
  }}
  . = {data_base:#x};
  .kbdata :
  {{
    KEEP(*(.kblob.desc))
    *(.rodata .rodata.* .srodata .srodata.*)
    *(.data .data.* .sdata .sdata.*)
  }}
  .kbbss (NOLOAD) :
  {{
    *(.bss .bss.* .sbss .sbss.* COMMON)
  }}
  .kbinit :
  {{
    *(.init_array .init_array.* .ctors .ctors.* .preinit_array*)
  }}
  /DISCARD/ :
  {{
    *(.comment) *(.xtensa.info) *(.xt.*) *(.debug*) *(.eh_frame*) *(.gcc_except_table*) *(.note*)
  }}
}}
ASSERT(SIZEOF(.kbinit) == 0, "blob has static constructors; nothing would run them")
ASSERT(. <= {data_base:#x} + {data_cap}, "blob data + bss exceeds the device buffer")
ASSERT(ADDR(.kbtext) + SIZEOF(.kbtext) <= {text_base:#x} + {text_cap}, "blob text exceeds the device buffer")
"""

DESC_C = """\
extern const char %s[];
__attribute__((section(".kblob.desc"), used)) const void *const kblob_desc = %s;
"""


def section_info(elf):
    txt = run([PREFIX + "readelf", "-S", "-W", elf], capture=True)
    info = {}
    for m in re.finditer(r"\[\s*\d+\]\s+(\.\S+)\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)", txt):
        info[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return info


def build(args):
    source = os.path.abspath(args.source)
    if not os.path.exists(source):
        die("no such source: " + source)
    build_dir, elf = env_paths(args.env)
    dev = Device(args.device)
    info = dev.get("/api/debug/kblob")
    if "text_base" not in info:
        die("device does not answer /api/debug/kblob; is it running a display-kdev build?")
    # The device reports CONFIG_APP_RETRIEVE_LEN_ELF_SHA hex digits (9 by
    # default) of sha256(firmware.elf); the blob header carries 8 bytes.
    sha_hex = info["fw_sha"]
    if len(sha_hex) < 8:
        die("device reports no firmware sha (%r)" % sha_hex)
    fw = firmware_symbols(args.env, elf, sha_hex)
    # The device compares the header field against its own digits, so carry
    # exactly those, zero-padded to the field.
    sha = bytes.fromhex(sha_hex.ljust(16, "0")[:16])

    work = work_dir(args.env)
    name = os.path.splitext(os.path.basename(source))[0]
    obj = os.path.join(work, name + ".o")
    cmd = compile_command(args.env) + ["-o", obj, "-c", source]
    if args.define:
        cmd = cmd[:1] + ["-D" + d for d in args.define] + cmd[1:]
    run(cmd)

    obj_syms = nm(obj)
    anim_syms = [s for s, (a, t) in obj_syms.items() if a is not None and s.startswith("bg_anim_") and t in "DdRr"]
    anim_sym = args.anim_symbol or (anim_syms[0] if len(anim_syms) == 1 else None)
    if anim_sym is None:
        die("expected exactly one bg_anim_* descriptor in %s, found %r (use --anim-symbol)" % (source, anim_syms))
    undefined = [s for s, (a, t) in obj_syms.items() if t == "U"]
    weak_undef = [s for s, (a, t) in obj_syms.items() if t in "wv" and a is None]
    missing = [s for s in undefined if s not in fw]
    if missing:
        die("blob references symbols the firmware does not export: " + ", ".join(sorted(missing)[:20]))
    lines = ["%s = %#x;" % (s, fw[s]) for s in sorted(undefined)]
    # Weak references the firmware does not define stay null, as they would in
    # the firmware's own link.
    lines += ["%s = %#x;" % (s, fw.get(s, 0)) for s in sorted(weak_undef)]
    assignments = "\n".join(lines)

    desc_c = os.path.join(work, name + ".desc.c")
    desc_o = os.path.join(work, name + ".desc.o")
    open(desc_c, "w").write(DESC_C % (anim_sym, anim_sym))
    run([PREFIX + "gcc", "-mlongcalls", "-O2", "-ffunction-sections", "-fdata-sections", "-c", desc_c, "-o", desc_o])

    ld_path = os.path.join(work, name + ".ld")
    open(ld_path, "w").write(
        LD_SCRIPT.format(
            assignments=assignments,
            text_base=info["text_base"],
            text_cap=info["text_cap"],
            data_base=info["data_base"],
            data_cap=info["data_cap"],
        )
    )
    blob_elf = os.path.join(work, name + ".elf")
    run([PREFIX + "ld", "-T", ld_path, "--gc-sections", "-o", blob_elf, desc_o, obj])

    secs = section_info(blob_elf)
    text_addr, text_size = secs.get(".kbtext", (info["text_base"], 0))
    data_addr, data_size = secs.get(".kbdata", (info["data_base"], 0))
    # The span the device must zero runs from the end of the data image to
    # the end of .kbbss, alignment gap included: the section itself starts
    # on a 4-byte boundary, so its size alone leaves the last bytes of bss
    # holding whatever the previous blob left there (a stale high byte in a
    # table pointer made init() skip its allocations and frame() dereference
    # null, on real hardware, twice).
    bss_addr, bss_sec = secs.get(".kbbss", (data_addr + data_size, 0))
    if bss_addr < data_addr + data_size:
        die("bss starts inside the data image (%#x < %#x)" % (bss_addr, data_addr + data_size))
    bss_size = (bss_addr + bss_sec) - (data_addr + data_size)
    if text_size == 0:
        die("blob has no text")
    text_bin = os.path.join(work, name + ".text.bin")
    data_bin = os.path.join(work, name + ".data.bin")
    run([PREFIX + "objcopy", "-O", "binary", "-j", ".kbtext", blob_elf, text_bin])
    run([PREFIX + "objcopy", "-O", "binary", "-j", ".kbdata", blob_elf, data_bin])
    text = open(text_bin, "rb").read()
    data = open(data_bin, "rb").read()
    if len(text) != text_size or len(data) != data_size:
        die("section dump size mismatch (text %d/%d, data %d/%d)" % (len(text), text_size, len(data), data_size))
    desc_off = nm(blob_elf)["kblob_desc"][0] - data_addr
    if desc_off != 0:
        print("kb: note: descriptor at data offset %d" % desc_off)
    text_padded = text + b"\0" * (-len(text) % 4)
    crc = zlib.crc32(data, zlib.crc32(text)) & 0xFFFFFFFF
    header = struct.pack(
        "<4sIIIIIII8s16sII",
        b"GMKB",
        1,
        text_addr,
        len(text),
        data_addr,
        len(data),
        bss_size,
        desc_off,
        sha,
        name.encode()[:15].ljust(16, b"\0"),
        crc,
        0,
    )
    out = args.output or os.path.join(work, name + ".kblob")
    open(out, "wb").write(header + text_padded + data)
    print(
        "kb: built %s: text %d B (cap %d), data %d B + bss %d B (cap %d), anim %s"
        % (out, len(text), info["text_cap"], len(data), bss_size, info["data_cap"], anim_sym)
    )
    return out


# ---- device operations ----------------------------------------------------------


def upload(args, path=None):
    path = path or args.blob
    body = open(path, "rb").read()
    with DeviceLock():
        r = Device(args.device).post("/api/debug/kblob", body)
    if not r.get("ok"):
        die("device refused the blob: %s" % r.get("error", r))
    print("kb: installed %s gen=%s text=%s data=%s bss=%s" % (r["name"], r["gen"], r["text_size"], r["data_size"], r["bss_size"]))
    return r


def bench(args, hold_lock=True):
    dev = Device(args.device)
    q = "/api/debug/kbench?anim=%d&n=%d&frames=%d&which=%d" % (args.anim, args.n, args.frames, args.which)

    def go():
        st = dev.get("/api/debug/kbench")
        before = st.get("seq", 0)
        gen = dev.get("/api/debug/kblob").get("gen", 0)
        dev.get(q)
        deadline = time.time() + 120
        while time.time() < deadline:
            time.sleep(0.3)
            r = dev.get("/api/debug/kbench")
            seq = r.get("seq", 0)
            if seq < before or (seq == 0 and before == 0 and dev.get("/api/debug/kblob").get("gen", 0) < gen):
                die("the device rebooted during the bench: the blob crashed it (check the serial log for the backtrace)")
            if not r.get("pending") and seq != before:
                if r.get("anim") != args.anim:
                    die("kbench result is for anim %s, not %d: another run got in between" % (r.get("anim"), args.anim))
                return r
        die("kbench did not complete in 120 s (is the animation running on the panel?)")

    if hold_lock:
        with DeviceLock():
            r = go()
    else:
        r = go()
    print_bench(r)
    return r


def print_bench(r):
    mhz = r.get("cpu_mhz") or 240
    frames = max(1, r.get("frames", 1))
    print("kbench %s (anim %d) n=%d frames=%d, ms per frame at %d MHz" % (r.get("id"), r["anim"], r["n"], frames, mhz))
    print("%-8s %9s %9s %9s %6s %s" % ("variant", "min_ms", "first_ms", "mean_ms", "bands", "vs band()"))
    base = r["band"]["min_cyc"] if r["band"]["ran"] and r["band"]["min_cyc"] else None
    for k in ("band", "ref", "blob", "blobref"):
        v = r[k]
        if not v["ran"]:
            continue
        if v["init_failed"]:
            print("%-8s init failed" % k)
            continue
        ms = lambda c: c / (mhz * 1000.0) / frames
        rel = "%.2fx" % (base / v["min_cyc"]) if base and v["min_cyc"] else ""
        mm = ""
        if k != "band" and v["mismatch_bands"]:
            mm = "  MISMATCH %d bands vs band() (first frame %d y %d)" % (v["mismatch_bands"], v["first_mismatch_frame"], v["first_mismatch_y"])
        elif k != "band" and base:
            mm = "  same pixels as band()"
        if k == "blobref" and r["blob"]["ran"] and not r["blob"]["init_failed"]:
            mm += "; MISMATCH %d bands vs blob" % v["mismatch_vs_blob"] if v.get("mismatch_vs_blob") else "; same pixels as blob"
        print("%-8s %9.2f %9.2f %9.2f %6d %s%s" % (k, ms(v["min_cyc"]), ms(v["first_cyc"]), ms(v["mean_cyc"]), v["bands"], rel, mm))
    # Hot-slab hygiene. A table without a matching release() pins the slab's
    # live count above zero, and until the bench reset every later init() on
    # the board would have landed in PSRAM.
    if r.get("hot_leak_fw"):
        print("kb: LEAK: the firmware's %s left %d B in the hot slab after release() (bench reset the slab)" % (r.get("id"), r["hot_leak_fw"]))
    if r.get("hot_leak_blob"):
        print("kb: LEAK: the blob left %d B in the hot slab after release(): a table allocated in init() has no release() (bench reset the slab)" % r["hot_leak_blob"])
    if r.get("hot_leak_before"):
        print("kb: note: %d B of hot slab were still allocated before this bench (leaked by an earlier run on this boot); reset" % r["hot_leak_before"])


def info(args):
    r = Device(args.device).get("/api/debug/kblob")
    print(json.dumps(r, indent=2))


def useblob(args):
    with DeviceLock():
        r = Device(args.device).get("/api/debug/anim?useblob=%d" % args.on)
    print("useblob=%s blob_resident=%s" % (r.get("useblob"), r.get("blob_resident")))


def run_all(args):
    path = build(args)
    with DeviceLock():
        args.blob = path
        body = open(path, "rb").read()
        r = Device(args.device).post("/api/debug/kblob", body)
        if not r.get("ok"):
            die("device refused the blob: %s" % r.get("error", r))
        print("kb: installed %s gen=%s" % (r["name"], r["gen"]))
        bench(args, hold_lock=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--device", default=DEFAULT_DEVICE, help="device host (default %s, env GM_DEVICE)" % DEFAULT_DEVICE)
    ap.add_argument("--env", default=DEFAULT_ENV, help="PlatformIO env whose ELF the device runs (default %s)" % DEFAULT_ENV)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def add_build_args(p):
        p.add_argument("source")
        p.add_argument("-o", "--output")
        p.add_argument("-D", "--define", action="append", help="extra -D for the blob compile (repeatable)")
        p.add_argument("--anim-symbol", help="bg_anim_* descriptor symbol when the source defines several")

    def add_bench_args(p):
        p.add_argument("--anim", type=int, required=True, help="registry id the bench inits and parameterises")
        p.add_argument("--n", type=int, default=8, help="runs per band (min-of-n)")
        p.add_argument("--frames", type=int, default=2)
        p.add_argument("--which", type=int, default=15, help="bit mask: 1 band, 2 ref, 4 blob, 8 blobref")

    p = sub.add_parser("info")
    p.set_defaults(fn=info)
    p = sub.add_parser("build")
    add_build_args(p)
    p.set_defaults(fn=build)
    p = sub.add_parser("upload")
    p.add_argument("blob")
    p.set_defaults(fn=upload)
    p = sub.add_parser("bench")
    add_bench_args(p)
    p.set_defaults(fn=bench)
    p = sub.add_parser("run")
    add_build_args(p)
    add_bench_args(p)
    p.set_defaults(fn=run_all)
    p = sub.add_parser("useblob")
    p.add_argument("on", type=int, choices=(0, 1))
    p.set_defaults(fn=useblob)

    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
