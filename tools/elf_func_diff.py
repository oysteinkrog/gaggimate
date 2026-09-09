#!/usr/bin/env python3
"""Compare the instruction stream of every function in two firmware ELFs.

The check behind a refactor of a hot path that must not change codegen: the
probe hook extraction from SleepAnimation.cpp (gm-bzu.19) was verified with
it, and a kernel change can use it the same way. Addresses are stripped, so
a function that only moved reads as SAME; a function whose instructions
changed reads as DIFF with the number of differing lines, and --show prints
the diff for one of them.

    python3 tools/elf_func_diff.py base.elf new.elf --filter SleepAnimation
    python3 tools/elf_func_diff.py base.elf new.elf --show renderFrame
    python3 tools/elf_func_diff.py base.elf new.elf --loose   # ignore literal-pool noise

Needs the Xtensa objdump from the PlatformIO toolchain (found under
~/.platformio unless --objdump says otherwise).
"""
import argparse
import difflib
import os
import re
import subprocess
import sys

DEFAULT_OBJDUMP = os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump")

FUNC_RE = re.compile(r"^[0-9a-f]{8} <(.+)>:$")
INSN_RE = re.compile(r"^\s*[0-9a-f]+:\t[0-9a-f]+\s*\t?(.*)$")
# The literal pool slot an l32r reads moves with every layout change; what
# matters is the value objdump prints after it in parentheses.
L32R_SLOT_RE = re.compile(r"^(l32r\s+a\d+,\s+)[0-9a-f]+ <.*> \(")
ADDR_SYM_RE = re.compile(r"\b[0-9a-f]{8} <([^>]+)>")


def normalise(insn_text):
    # "call8 40380abc <foo>" -> "call8 <foo>", "j 4038000c <bar+0x1c>" -> "j <bar+0x1c>",
    # "l32r a8, 40374498 <_iram_text_start+0x94> (3fcb4c2c <g>)" -> "l32r a8, (<g>)".
    t = insn_text.replace("\t", " ").strip()
    t = L32R_SLOT_RE.sub(r"\1(", t)
    return ADDR_SYM_RE.sub(r"<\1>", t).strip()


RET_RE = re.compile(r"^(retw|ret)(\.n)?\b")
LIT_OFF_RE = re.compile(r"\(([0-9a-f]{8} )?<([^>+]+)\+0x[0-9a-f]+>\)")


def trim_literal_tail(insns):
    # objdump decodes the literal pool that follows a function's last return
    # as instructions; those bytes are addresses and move with every layout
    # change, so the stream is cut after the last return.
    last = -1
    for i, t in enumerate(insns):
        if RET_RE.match(t):
            last = i
    return insns[: last + 1] if last >= 0 else insns


def drop_literal_offsets(insns):
    # "l32r a11, (<table+0xb208>)" -> "l32r a11, (<table>)": a literal that
    # points into a data object whose neighbours moved is not a code change.
    return [LIT_OFF_RE.sub(r"(<\2>)", t) for t in insns]


def disassemble(objdump, elf, loose):
    out = subprocess.run([objdump, "-d", "-C", elf], capture_output=True, text=True, check=True).stdout
    funcs = {}
    cur = None
    for line in out.splitlines():
        m = FUNC_RE.match(line)
        if m:
            cur = m.group(1)
            funcs.setdefault(cur, [])
            continue
        if cur is None:
            continue
        m = INSN_RE.match(line)
        if m:
            funcs[cur].append(normalise(m.group(1)))
    # Drop the empty entries objdump emits for section starts.
    funcs = {k: v for k, v in funcs.items() if v}
    if loose:
        funcs = {k: drop_literal_offsets(trim_literal_tail(v)) for k, v in funcs.items()}
    return funcs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("base")
    ap.add_argument("new")
    ap.add_argument("--filter", default="", help="regex on the demangled function name")
    ap.add_argument("--show", default=None, help="print the diff of every function whose name contains this (empty string: all)")
    ap.add_argument("--objdump", default=DEFAULT_OBJDUMP)
    ap.add_argument("--all", action="store_true", help="list SAME functions too")
    ap.add_argument("--loose", action="store_true",
                    help="ignore the literal pool objdump decodes after the last return, and the offset part of a literal that points into a data object")
    args = ap.parse_args()

    a = disassemble(args.objdump, args.base, args.loose)
    b = disassemble(args.objdump, args.new, args.loose)
    flt = re.compile(args.filter) if args.filter else None
    names = sorted(set(a) | set(b))
    if flt:
        names = [n for n in names if flt.search(n)]
    same = diff = only_a = only_b = 0
    for n in names:
        if n not in a:
            only_b += 1
            print("ONLY-NEW  %s" % n)
            continue
        if n not in b:
            only_a += 1
            print("ONLY-BASE %s" % n)
            continue
        if a[n] == b[n]:
            same += 1
            if args.all:
                print("SAME      %s (%d insns)" % (n, len(a[n])))
            continue
        diff += 1
        changed = sum(1 for tag, *_ in difflib.SequenceMatcher(None, a[n], b[n]).get_opcodes() if tag != "equal")
        print("DIFF      %s (%d -> %d insns, %d hunks)" % (n, len(a[n]), len(b[n]), changed))
        if args.show is not None and args.show in n:
            for line in difflib.unified_diff(a[n], b[n], "base", "new", lineterm="", n=2):
                print("    " + line)
    print("functions: %d same, %d diff, %d only in base, %d only in new" % (same, diff, only_a, only_b))
    return 1 if (diff or only_a or only_b) else 0


if __name__ == "__main__":
    sys.exit(main())
