#!/usr/bin/env python3
"""Summarize xtensa-esp32s3 GCC assembly per function: instruction counts,
zero-overhead loops, calls (soft-float libcalls flagged), and a rough LX7
cycle weight for the innermost loop body.

Weights (in-order LX7, best case, no cache misses):
  most ALU/branch-not-taken: 1    loads (l32i/l16ui/l8ui/l32r): 1 issue
  (+1 use-interlock, not modeled)  mul (mull/muluh): 2
  float add/mul/madd (FPU): 1     float compare+move: 2
  trunc/float<->int: 2            call8 overhead: ~4 + callee
Known callee costs (soft libm, newlib): sqrtf~90 sinf/cosf~150 expf/logf~200
powf~400 atan2f~300 fmodf~40 __divsf3 (if emitted)~30.
The point is not absolute accuracy — it's per-pixel instruction counts and
catching accidental libcalls/doubles in inner loops.
"""
import re
import subprocess
import sys
from collections import Counter

LIBCALL_COST = {
    "sqrtf": 90, "sinf": 150, "cosf": 150, "expf": 200, "exp2f": 150,
    "logf": 200, "powf": 400, "atan2f": 300, "fmodf": 40, "tanhf": 300,
    "floorf": 40, "lroundf": 40,
    "__divsf3": 30, "__divdf3": 250, "__adddf3": 60, "__muldf3": 70,
    "__extendsfdf2": 15, "__truncdfsf2": 15, "__fixdfsi": 20, "__floatsidf": 20,
}
DOUBLE_CALLS = {c for c in LIBCALL_COST if "df" in c}

def weight(op):
    if op.startswith(("mull", "muluh", "mulsh")):
        return 2
    if op.startswith(("trunc.s", "float.s", "ufloat.s", "utrunc.s", "round.s")):
        return 2
    if op.startswith(("quos", "quou", "rems", "remu")):
        return 12  # integer divide, iterative
    return 1

def main(path):
    funcs = {}
    cur = None
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"^([A-Za-z_][\w.$]*):\s*$", line)
        if m and not m.group(1).startswith(".L"):
            cur = m.group(1)
            funcs[cur] = []
            continue
        if cur is None:
            continue
        s = line.strip()
        if not s or s.startswith((".", "#", "//")) or s.endswith(":"):
            continue
        op = s.split()[0]
        arg = s[len(op):].strip()
        funcs[cur].append((op, arg))

    names = list(funcs)
    try:
        flt = subprocess.run(
            ["c++filt"], input="\n".join(names), capture_output=True, text=True
        ).stdout.splitlines()
        demangled = dict(zip(names, flt))
    except Exception:
        demangled = {n: n for n in names}

    print(f"== {path}")
    for name, insns in funcs.items():
        if not insns:
            continue
        pretty = demangled.get(name, name)
        ops = Counter(op for op, _ in insns)
        calls = Counter()
        for op, arg in insns:
            if op in ("call8", "callx8", "call0", "j.l"):
                tgt = arg.split(",")[0].strip()
                calls[tgt] += 1
        loops = sum(ops[o] for o in ("loop", "loopnez", "loopgtz"))
        cyc = sum(weight(op) for op, _ in insns)
        for tgt, n in calls.items():
            base = tgt.lstrip("_") if tgt.startswith("_Z") else tgt
            cyc += LIBCALL_COST.get(tgt, 4) * n
        flags = []
        if any(t in DOUBLE_CALLS for t in calls):
            flags.append("!! DOUBLE MATH")
        soft = [t for t in calls if t in LIBCALL_COST]
        if soft:
            flags.append("libcalls: " + ", ".join(f"{t}x{calls[t]}" for t in soft))
        if loops:
            flags.append(f"zero-overhead loops: {loops}")
        print(f"  {pretty}")
        print(f"    insns={len(insns)} est_weight={cyc} {' | '.join(flags)}")
        top = ", ".join(f"{o}:{n}" for o, n in ops.most_common(8))
        print(f"    top ops: {top}")
        othercalls = [t for t in calls if t not in LIBCALL_COST]
        if othercalls:
            print(f"    calls: {', '.join(othercalls)}")

if __name__ == "__main__":
    for p in sys.argv[1:]:
        main(p)
