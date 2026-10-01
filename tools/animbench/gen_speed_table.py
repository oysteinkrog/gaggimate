#!/usr/bin/env python3
"""Generate the Speed multiplier table in BgAnimCommon.h (gm-r5tc).

Every animation reads its Speed parameter (0..100) through speedMul(), the
curve 2^((sp - 50) / 18.2): 0.149x at 0, exactly 1x at 50, 6.7x at 100.
It used to call exp2f at run time, and exp2f is not the same function on
every side: the host bench's glibc, the device's newlib and a browser's
Math.pow disagree by up to 2 ulp on 57 of the 101 values, which puts a
truncated phase word one tick apart after an hour away from Speed 50.

So the curve is a table, computed here once with exact arithmetic, and
every consumer reads the table. No library is in the path any more.

Exact rounding, which any other consumer must reproduce bit for bit:

  k    = 1.0f / 18.2f        both operands float32, quotient rounded to
                              float32 (the old C expression's constant)
  x    = float32(sp - 50) * k, rounded to float32
  m[sp] = 2^x evaluated to 60 significant digits, rounded once to the
         nearest float32, ties to even

so each entry is the correctly rounded float32 of 2^x for the same x the
old code passed to exp2f. Entry 50 is exactly 1.0. The values are written
as C hex-float literals, which carry the float32 bits exactly.

Usage:
  gen_speed_table.py            rewrite the table between the markers
  gen_speed_table.py --check    fail if the header differs from a fresh
                                generation, then compile a host program
                                against the header and compare every entry
                                with this host's libm exp2f of the old
                                expression (must be within 1 ulp)
"""
import decimal
import fractions
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "src/display/ui/default/bganim/BgAnimCommon.h"
BEGIN = "// BEGIN gen_speed_table.py: do not edit by hand\n"
END = "// END gen_speed_table.py\n"

F32_MANT = 24  # significand bits including the implicit one


def round_f32(q):
    """Round a positive Fraction to the nearest float32, ties to even.
    Returns the exact value as a Fraction. Normal range only (all inputs
    here are between 0.1 and 10)."""
    assert q > 0
    e = 0
    while q >= 2:
        q /= 2
        e += 1
    while q < 1:
        q *= 2
        e -= 1
    scaled = q * (1 << (F32_MANT - 1))  # in [2^23, 2^24)
    n = scaled.numerator // scaled.denominator
    rem = scaled - n
    if rem > fractions.Fraction(1, 2) or (rem == fractions.Fraction(1, 2) and n % 2 == 1):
        n += 1
    return fractions.Fraction(n) * fractions.Fraction(2) ** (e - (F32_MANT - 1))


def round_f32_signed(q):
    if q == 0:
        return fractions.Fraction(0)
    return round_f32(q) if q > 0 else -round_f32(-q)


def f32_from_decimal(d):
    return round_f32_signed(fractions.Fraction(d))


def hexfloat(q):
    """C hex-float literal of a positive float32 value held as a Fraction."""
    e = 0
    while q >= 2:
        q /= 2
        e += 1
    while q < 1:
        q *= 2
        e -= 1
    frac = (q - 1) * (1 << 24)  # 23 mantissa bits, padded to 24 for 6 hex digits
    assert frac.denominator == 1
    return "0x1.%06xp%+dF" % (int(frac), e)


def table():
    ctx = decimal.Context(prec=60)
    k = round_f32(fractions.Fraction(1) / f32_from_decimal("18.2"))
    ln2 = ctx.ln(decimal.Decimal(2))
    out = []
    for sp in range(101):
        x = round_f32_signed(fractions.Fraction(sp - 50) * k)
        xd = ctx.divide(decimal.Decimal(x.numerator), decimal.Decimal(x.denominator))
        v = ctx.exp(ctx.multiply(xd, ln2))
        out.append(round_f32(fractions.Fraction(v)))
    assert out[50] == 1
    return out


def render():
    vals = table()
    lines = [BEGIN]
    lines.append("// Speed multiplier, one entry per Speed setting 0..100. Entry sp is the\n")
    lines.append("// correctly rounded float32 of 2^x, x = float32(sp - 50) * (1.0f / 18.2f)\n")
    lines.append("// in float32; rounding rules in tools/animbench/gen_speed_table.py.\n")
    lines.append("inline constexpr float kSpeedMulTable[101] = {\n")
    for i in range(0, 101, 4):
        row = ", ".join(hexfloat(v) for v in vals[i:i + 4])
        lines.append("    %s,\n" % row)
    lines.append("};\n")
    lines.append(END)
    return "".join(lines)


def splice(text, block):
    pat = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END), re.S)
    if not pat.search(text):
        sys.exit("markers not found in %s" % HEADER)
    return pat.sub(lambda _m: block, text, count=1)


HOST_CHECK = r"""
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "BgAnimCommon.h"
static int32_t bits(float f) { int32_t b; memcpy(&b, &f, 4); return b; }
int main() {
    int same = 0, off1 = 0, worse = 0, maxd = 0;
    for (int sp = 0; sp <= 100; ++sp) {
        volatile float x = (sp - 50) * (1.0f / 18.2f);
        float libm = exp2f(x);
        float tab = bganim::kSpeedMulTable[sp];
        int d = bits(libm) - bits(tab);
        if (d < 0) d = -d;
        if (d > maxd) maxd = d;
        if (d == 0) ++same; else if (d == 1) ++off1; else ++worse;
        if (bganim::speedMul((uint8_t)sp) != tab) { printf("speedMul(%d) does not read the table\n", sp); return 1; }
    }
    if (bganim::speedMul(50) != 1.0f) { printf("entry 50 is not 1\n"); return 1; }
    if (bganim::speedMul(255) != bganim::kSpeedMulTable[100]) { printf("speedMul(255) not clamped\n"); return 1; }
    printf("host exp2f vs table: %d equal, %d at 1 ulp, %d worse (max %d ulp)\n", same, off1, worse, maxd);
    return worse ? 1 : 0;
}
"""


def check():
    text = HEADER.read_text(encoding="utf-8")
    if splice(text, render()) != text:
        print("BgAnimCommon.h table differs from a fresh generation: run gen_speed_table.py")
        return 1
    print("header table matches the generator")
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "speed_check.cpp")
        exe = os.path.join(tmp, "speed_check")
        with open(src, "w") as f:
            f.write(HOST_CHECK)
        subprocess.run(["g++", "-O2", "-std=gnu++17", "-I", str(HEADER.parent), src, "-o", exe, "-lm"], check=True)
        return subprocess.run([exe]).returncode


def main():
    if "--check" in sys.argv[1:]:
        sys.exit(check())
    text = HEADER.read_text(encoding="utf-8")
    new = splice(text, render())
    if new != text:
        HEADER.write_text(new, encoding="utf-8")
        print("updated", HEADER.relative_to(ROOT))
    else:
        print("table already current")


if __name__ == "__main__":
    main()
