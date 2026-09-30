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
    python3 tools/elf_func_diff.py base.elf new.elf --loose

Exit status is non-zero whenever a DIFF, ONLY-BASE or ONLY-NEW line was
printed, in strict mode and in --loose alike: --loose narrows what counts as
noise, it never turns a real difference into a pass.

Without --loose (strict), the only normalisation applied is address
stripping (normalise(), below): a raw address is replaced by the symbol name
objdump already resolved it to, because relinking moves nearly everything by
a few bytes and an address-for-address compare would report every function
as different. Two things stay UNtouched in strict mode, and either one
changing is reported as a real DIFF: bytes disassembled past the function's
own linked size (objdump has no function-boundary information and happily
keeps decoding into whatever follows — usually a literal pool, sometimes the
next function if the linker left no gap), and the offset half of a literal
data reference like "(<table+0x18>)".

--loose relaxes exactly those two, and only where they are demonstrably not
code:

  - **Function-size truncation.** The ELF symbol table (read with `nm -S`)
    gives every function's real linked size; disassembly is cut at
    start+size, not at "the last return instruction" — a prior version of
    this script guessed the boundary that way, and the guess cut real code
    that happened to sit after what it took for the last return (a second
    exit block, a branch to a block placed after an early return) exactly as
    readily as it cut a trailing literal pool. Reading the boundary from the
    symbol table removes the guess: what is in bounds is the function,
    whatever shape it takes; what is out of bounds never was.
  - **Anonymous literal-offset folding.** A literal load's data pointer is
    folded from "(<name+0xNN>)" to "(<name>)" only when `name` is itself a
    linker-assigned section label (`.rodata.str1.4`, `.literal4`, and
    similar — see NOISE_SYM_RE): those names are reassigned for unrelated
    reasons (another constant merged into the same pool, `--gc-sections`
    reordering) without the referenced code changing, and disassembly gives
    no finer address for them. A literal against a real symbol — a global,
    a named struct instance, a static — keeps its offset and is compared
    exactly as in strict mode, because a different offset into a *named*
    object is a different field or element: a real change.

Every run with --loose prints one "loose:" line reporting exactly what it
folded (function count and line/reference count for each of the two rules
above), so a normalisation is never silent.

Needs the Xtensa objdump and nm from the PlatformIO toolchain (found under
~/.platformio unless --objdump/--nm say otherwise).
"""
import argparse
import difflib
import os
import re
import subprocess
import sys

DEFAULT_OBJDUMP = os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump")

FUNC_RE = re.compile(r"^([0-9a-f]+) <(.+)>:$")
INSN_RE = re.compile(r"^\s*([0-9a-f]+):\t[0-9a-f]+\s*\t?(.*)$")
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


# A literal data pointer's offset is folded only when it points into one of
# these linker-assigned, not-a-real-variable names: numbered section labels
# (".rodata.str1.4", ".literal4", ".rodata.cst16", a bare ".rodata"), and the
# compiler's own anonymous locals (".L123", "$d"-style mapping symbols). A
# named global or struct instance never matches this and keeps its offset.
NOISE_SYM_RE = re.compile(r"^(\.(rodata|srodata|data|sdata|bss|sbss|text|literal)([0-9]*|\.\S*)?|\.L[0-9A-Za-z_.]*|\$[a-z])$")
LIT_OFF_RE = re.compile(r"\(([0-9a-f]{8} )?<([^>+]+)\+0x[0-9a-f]+>\)")


def fold_literal_offsets(insn):
    """Fold the offset out of a literal data reference, but only when the
    symbol it points into is a linker-assigned label rather than a named
    object — see NOISE_SYM_RE and the module docstring. Returns (text,
    folded) where folded is True iff a substitution actually happened, so
    the caller can report what --loose normalised away."""
    folded = False

    def repl(m):
        nonlocal folded
        sym = m.group(2)
        if NOISE_SYM_RE.match(sym):
            folded = True
            return "(<%s>)" % sym
        return m.group(0)

    return LIT_OFF_RE.sub(repl, insn), folded


def function_sizes(nm_tool, elf):
    """address -> linked size, for defined function symbols (nm -S). Used to
    truncate a function's disassembly to its real extent instead of guessing
    the boundary from "the last return instruction" (see module docstring:
    that guess cut real code as readily as it cut a trailing literal pool)."""
    out = subprocess.run([nm_tool, "-S", "--defined-only", elf], capture_output=True, text=True, check=True).stdout
    sizes = {}
    for line in out.splitlines():
        parts = line.split(None, 3)
        if len(parts) != 4:
            continue
        addr_s, size_s, typ, _name = parts
        if typ.lower() not in ("t", "w"):  # text (local/global) or weak-text
            continue
        try:
            sizes[int(addr_s, 16)] = int(size_s, 16)
        except ValueError:
            continue
    return sizes


def disassemble(objdump, nm_tool, elf, loose):
    out = subprocess.run([objdump, "-d", "-C", elf], capture_output=True, text=True, check=True).stdout
    sizes = function_sizes(nm_tool, elf) if loose else {}
    funcs = {}
    cur = None
    cur_addr = None
    cur_size = None
    stats = {"trimmed_lines": 0, "trimmed_funcs": set(), "folded_refs": 0, "folded_funcs": set()}
    for line in out.splitlines():
        m = FUNC_RE.match(line)
        if m:
            cur_addr = int(m.group(1), 16)
            cur = m.group(2)
            cur_size = sizes.get(cur_addr)
            funcs.setdefault(cur, [])
            continue
        if cur is None:
            continue
        m = INSN_RE.match(line)
        if not m:
            continue
        if loose and cur_size is not None:
            insn_addr = int(m.group(1), 16)
            if insn_addr - cur_addr >= cur_size:
                # Past the function's own linked size: not code, whatever
                # objdump made of the bytes (almost always a literal pool).
                stats["trimmed_lines"] += 1
                stats["trimmed_funcs"].add(cur)
                continue
        insn = normalise(m.group(2))
        if loose:
            insn, folded = fold_literal_offsets(insn)
            if folded:
                stats["folded_refs"] += 1
                stats["folded_funcs"].add(cur)
        funcs[cur].append(insn)
    # Drop the empty entries objdump emits for section starts.
    funcs = {k: v for k, v in funcs.items() if v}
    return funcs, stats


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("base")
    ap.add_argument("new")
    ap.add_argument("--filter", default="", help="regex on the demangled function name")
    ap.add_argument("--show", default=None, help="print the diff of every function whose name contains this (empty string: all)")
    ap.add_argument("--objdump", default=DEFAULT_OBJDUMP)
    ap.add_argument("--nm", default=None, help="default: --objdump with its 'objdump' suffix swapped for 'nm'")
    ap.add_argument("--all", action="store_true", help="list SAME functions too")
    ap.add_argument(
        "--loose",
        action="store_true",
        help="truncate each function to its linked ELF size instead of comparing whatever objdump decoded past it, "
        "and fold the offset out of a literal reference into a linker-assigned (not a named) data symbol; "
        "never turns a real difference into SAME, and prints a 'loose:' line saying exactly what it folded",
    )
    args = ap.parse_args()
    nm_tool = args.nm or (args.objdump[: -len("objdump")] + "nm" if args.objdump.endswith("objdump") else args.objdump + "-nm")

    a, stats_a = disassemble(args.objdump, nm_tool, args.base, args.loose)
    b, stats_b = disassemble(args.objdump, nm_tool, args.new, args.loose)
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
    if args.loose:
        trimmed_lines = stats_a["trimmed_lines"] + stats_b["trimmed_lines"]
        trimmed_funcs = len(stats_a["trimmed_funcs"] | stats_b["trimmed_funcs"])
        folded_refs = stats_a["folded_refs"] + stats_b["folded_refs"]
        folded_funcs = len(stats_a["folded_funcs"] | stats_b["folded_funcs"])
        print(
            "loose: trimmed %d line(s) past the linked size in %d function(s); "
            "folded %d literal-offset reference(s) in %d function(s)" % (trimmed_lines, trimmed_funcs, folded_refs, folded_funcs)
        )
    return 1 if (diff or only_a or only_b) else 0


if __name__ == "__main__":
    sys.exit(main())
