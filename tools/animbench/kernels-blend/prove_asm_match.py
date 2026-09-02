#!/usr/bin/env python3
"""Diff the mnemonic+operand instruction stream of blend_group8.S (the
standalone assembler-syntax probe, register-argument convention a2..a7)
against the `asm volatile` block inside blendGroup8General in
blend_pie_kernel.cpp (the pasteable integration form, %[name] operand
placeholders) -- the two are meant to be the EXACT same instruction
sequence, and this is the automated check that they have not silently
drifted apart, rather than relying on eyeballing two hand-written blocks.

Normalization: blend_group8.S's fixed pointer registers a2..a7 map to
blend_pie_kernel.cpp's %[rd]/%[wr]/%[col]/%[av]/%[iv]/%[ct] placeholders
per the documented convention (see blend_group8.S's header comment):
  a2=rd  a3=wr  a4=col  a5=av  a6=iv  a7=ct
q-register operands (q0..q7) and immediates (16, 0, 5, 8, 11) are compared
verbatim -- those are not supposed to differ at all.

Exit code 0 and "MATCH" iff every instruction (opcode + normalized operand
list), in order, is identical between the two sources.
"""
import re
import sys

REG_MAP = {"a2": "rd", "a3": "wr", "a4": "col", "a5": "av", "a6": "iv", "a7": "ct"}
KNOWN_OPS = {"ee.vld.128.ip", "ee.vst.128.ip", "ee.andq", "ee.orq", "ee.vmul.u16", "ee.vadds.s16", "ssai"}


def parse_line(line):
    line = line.split("//", 1)[0].strip()
    if not line or line.startswith(".") or line.endswith(":"):
        return None
    parts = line.split(None, 1)
    op = parts[0]
    if op not in KNOWN_OPS:
        return None
    args = []
    if len(parts) > 1:
        for tok in parts[1].split(","):
            args.append(tok.strip())
    return (op, tuple(args))


def normalize_standalone(instrs):
    out = []
    for op, args in instrs:
        norm = tuple(REG_MAP.get(a, a) for a in args)
        out.append((op, norm))
    return out


def normalize_inline(instrs):
    out = []
    for op, args in instrs:
        norm = []
        for a in args:
            m = re.match(r"%\[(\w+)\]", a)
            norm.append(m.group(1) if m else a)
        out.append((op, tuple(norm)))
    return out


def extract_standalone(path):
    instrs = []
    with open(path) as f:
        for line in f:
            r = parse_line(line)
            if r:
                instrs.append(r)
    return normalize_standalone(instrs)


def extract_inline_asm_block(path, func_name):
    with open(path) as f:
        text = f.read()
    # Find the function, then the asm volatile("...") string literal inside it.
    fn_idx = text.find(func_name)
    if fn_idx < 0:
        sys.exit(f"function {func_name} not found in {path}")
    asm_idx = text.find("asm volatile(", fn_idx)
    if asm_idx < 0:
        sys.exit(f"no asm volatile block found after {func_name} in {path}")
    # The block is a sequence of adjacent C string literals, each ending in
    # \n" -- collect lines until the closing paren that starts the operand
    # constraint section (a line that is just ":" prefixed after a
    # string literal's closing quote, i.e. we stop at the first line
    # starting with '"' immediately followed by stuff then a bare ':' line).
    body = text[asm_idx:]
    lines = []
    for raw in body.splitlines():
        s = raw.strip()
        # Tolerate a same-line prefix before the opening quote (the first
        # line of the block is literally `asm volatile("...\n"`, not a bare
        # string literal on its own line like every subsequent line).
        m = re.search(r'"(.*)\\n"\s*$', s)
        if m:
            lines.append(m.group(1))
            continue
        if s.startswith(":") and not s.startswith('"'):
            break
    instrs = []
    for line in lines:
        r = parse_line(line)
        if r:
            instrs.append(r)
    return normalize_inline(instrs)


def main():
    standalone = extract_standalone("blend_group8.S")
    inline = extract_inline_asm_block("blend_pie_kernel.cpp", "blendGroup8General")

    print(f"blend_group8.S: {len(standalone)} instructions")
    print(f"blend_pie_kernel.cpp (blendGroup8General asm block): {len(inline)} instructions")

    if standalone == inline:
        print("MATCH: instruction sequences are identical (opcode + normalized operands, in order)")
        return 0

    print("MISMATCH:")
    n = max(len(standalone), len(inline))
    mism = 0
    for i in range(n):
        a = standalone[i] if i < len(standalone) else None
        b = inline[i] if i < len(inline) else None
        if a != b:
            mism += 1
            print(f"  [{i}] blend_group8.S={a}  blend_pie_kernel.cpp={b}")
    print(f"{mism} differing instruction(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
