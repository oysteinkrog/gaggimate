#!/usr/bin/env python3
"""Pull one translation unit's real compile command out of a PlatformIO /
ESP-IDF compile_commands.json and adapt it to emit assembly or an object
file in a chosen location, instead of the firmware's own .obj.

xtensa-asm14.sh calls this once for the .S output and once for the .o
proof-of-assembly, per animation. The point is that every flag, define and
include path comes from the entry PlatformIO actually built that file
with, so nothing here can drift out of sync with the firmware build by
hand-editing a flag list.
"""
import json
import os
import shlex
import sys


def load_entries(compiledb_path):
    with open(compiledb_path) as f:
        return json.load(f)


def find_entries(entries, suffix):
    return [e for e in entries if e.get("file", "").replace("\\", "/").endswith(suffix)]


def tokenize(entry):
    if "arguments" in entry:
        return list(entry["arguments"])
    if "command" in entry:
        # compile_commands.json commands are meant for a shell (sh -c), so
        # backslash-escaped quotes (e.g. -DIDF_VER=\"5.5.1\") are how a
        # define's literal quote characters are spelled; shlex unescapes
        # them the same way, giving the exact argv token the compiler
        # expects.
        return shlex.split(entry["command"])
    raise SystemExit("compile_commands.json entry has neither 'arguments' nor 'command'")


def strip_output_and_mode(tokens):
    """Drop PlatformIO's own '-o <obj>' / '-c' (order varies) and the
    trailing source path, keeping the compiler and every real build flag.
    Returns (compiler, flags, source_path)."""
    toks = list(tokens)
    if len(toks) < 2:
        raise SystemExit("compile command has too few tokens: %r" % (toks,))
    compiler = toks[0]
    src = toks[-1]
    middle = toks[1:-1]
    flags = []
    i = 0
    while i < len(middle):
        t = middle[i]
        if t == "-c":
            i += 1
            continue
        if t == "-o":
            i += 2  # skip the flag and its argument
            continue
        flags.append(t)
        i += 1
    return compiler, flags, src


def main(argv):
    if len(argv) != 5:
        print(
            "usage: xtensa_cmd_from_compiledb.py <compile_commands.json> "
            "<file-path-suffix> <asm|obj> <output-path>",
            file=sys.stderr,
        )
        return 2
    compiledb_path, suffix, mode, output_path = argv[1:5]

    if mode not in ("asm", "obj"):
        print("mode must be 'asm' or 'obj', got %r" % mode, file=sys.stderr)
        return 2

    entries = load_entries(compiledb_path)
    matches = find_entries(entries, suffix)
    if not matches:
        print(
            "no compile_commands.json entry's file ends with '%s'" % suffix,
            file=sys.stderr,
        )
        return 1
    if len(matches) > 1:
        print(
            "ambiguous: %d entries end with '%s':" % (len(matches), suffix),
            file=sys.stderr,
        )
        for m in matches:
            print("  %s" % m["file"], file=sys.stderr)
        return 1

    compiler, flags, src = strip_output_and_mode(tokenize(matches[0]))

    if not os.path.isfile(compiler) or not os.access(compiler, os.X_OK):
        print(
            "toolchain from compile_commands.json not found or not executable: %s" % compiler,
            file=sys.stderr,
        )
        return 1

    # Fallback include dir for the handful of Arduino-only symbols
    # (log_i/log_w/... in BgAnimCommon.cpp) that framework-arduinoespressif32
    # normally supplies; appended last so it only fills a gap, never shadows
    # a real header this compile command already resolves.
    shim_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "shim")

    out_flags = list(flags)
    out_flags.append("-I" + shim_dir)
    if mode == "asm":
        out_flags += ["-S", "-fverbose-asm"]
    else:
        out_flags += ["-c"]
    out_flags += ["-o", output_path, src]

    full = [compiler] + out_flags
    print(" ".join(shlex.quote(t) for t in full))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
