---
title: Fixed-point arithmetic on the Xtensa LX7
id: 06-kernel-patterns/fixed-point-arithmetic-on-lx7
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, fixed-point, pie, kernel-patterns]
confidence: medium
---

# Fixed-point arithmetic on the Xtensa LX7

The LX7 has a single-precision FPU, but a kernel that touches every pixel or
sample is usually cheaper in integers: integer work uses the AR registers the
loop already uses, needs no coprocessor context, and the PIE unit multiplies
eight 16-bit lanes at once while the FPU handles one float. Assembly marked
`[measured]` is `xtensa-esp32s3-elf-gcc` 14.2.0 output (crosstool-NG
esp-14.2.0_20241119, `-O2`): compiler behaviour, not timing.

## What the chip provides

The ESP32-S3 core configuration enables every integer option used here.[^coreisa]

| `core-isa.h` flag | Instructions |
|---|---|
| `XCHAL_HAVE_MUL16`, `MUL32`, `MUL32_HIGH` | `MUL16S`, `MUL16U`, `MULL`, `MULSH`, `MULUH` |
| `XCHAL_HAVE_DIV32` | `QUOS`, `QUOU`, `REMS`, `REMU` |
| `XCHAL_HAVE_MINMAX`, `CLAMPS` | `MIN`, `MAX`, `MINU`, `MAXU`, `CLAMPS` |
| `XCHAL_HAVE_SEXT`, `NSA` | `SEXT`, `NSA`, `NSAU` |
| `XCHAL_HAVE_FP` | FPU, including `FLOAT.S` and `TRUNC.S` |

Divide exists but is not a reason to use it: the ISA manual says the algorithm
is implementation-defined and that "some hardware implementations may be slower
than the software implementations for some operand values".[^isadiv] Treat
`QUOS` as a setup instruction, never a loop-body one.

## Choosing a format

Yates writes a signed format as `A(a,b)`: `a` integer bits, one sign bit, `b`
fractional bits, so `a+b+1` bits, resolution `2^-b`, range `-2^a` to `2^a -
2^-b`.[^yatesrange] Pick `b` from the precision needed and `a` from the largest
magnitude reachable, then check `a+b+1`. Fitting in 16 bits matters most here,
because that is what lets PIE work eight values at a time.

| Name | Yates | Bits | Range | Resolution |
|---|---|---|---|---|
| Q15 (Q1.15) | `A(0,15)` | 16 | -1 to 0.999969 | 3.05e-5 |
| Q8.8 | `A(7,8)` | 16 | -128 to 127.9961 | 3.91e-3 |
| Q16.16 | `A(15,16)` | 32 | -32768 to 32767.99998 | 1.53e-5 |
| Q1.31 | `A(0,31)` | 32 | -1 to 0.9999999995 | 4.66e-10 |

Two growth rules cover the overflow arithmetic.[^yatesrange] Adding `M` values
of `N` bits needs `N + ceil(log2 M)` bits, so a 240-term sum of Q15 values
needs 24 and a 16-bit accumulator wraps. A signed multiply grows as `A(a1,b1) *
A(a2,b2) = A(a1+a2+1, b1+b2)`. Work Q15 through, because it is the case that
bites: Q15 times Q15 is `A(1,30)`, exactly 32 bits, so `MUL16S` never overflows
its result register, but shifting right by 15 gives `A(1,15)`, needing 17 bits.
Only one input pair reaches that bit. `-1.0 * -1.0` is `0x8000 * 0x8000 =
0x40000000`, and `>> 15` gives `0x8000`, read back as `int16_t` as -32768
instead of +32767. Every other pair fits, so a Q15 multiply needs a clamp or a
promise that neither operand is ever -1.0.

## The multiply instructions

`MULL ar, as, at` writes the low 32 bits of the 32x32 product, and the low bits
do not depend on signedness, so it serves both. `MULSH` and `MULUH` write the
high 32 bits, signed and unsigned. `MUL16S` and `MUL16U` take the low 16 bits
of each source and write the full 32-bit product. The manual gives no cycle
counts: multiply timing comes from a configuration parameter named
`MulAlgorithm`, described as implementation-dependent, so instruction counts
here are claims about code shape only.[^isamul]

Which one you need depends on where the binary point sits relative to the
register boundary. When the product fits 32 bits, use `MULL` or `MUL16S` plus
one arithmetic shift; Q15 is the clean case, `MUL16S` then `SRAI 15`, with a
`SEXT ar, as, 15` after it whenever the result is stored back into an `int16_t`
and the compiler cannot see the top bits are already correct [measured]. When the
shift lands at the register boundary the high-word multiply is the whole
answer: Q1.31 squared is `A(1,62)`, whose top 32 bits are `A(1,30)`, one bit
short of Q1.31, so `MULSH` then `SLLI 1`, losing the low bit. When the shift
lands inside the 64-bit product you need both halves and a funnel shift.
Q16.16 times Q16.16 wants bits 47 down to 16, and `SRC ar, as, at` shifts the
concatenation of two registers right by `SAR` and keeps the low 32 bits, which
is that extraction, with `SSAI` setting `SAR`;[^isamisc] GCC emits
`mull; mulsh; ssai 16; src`, four instructions with no branch [measured].

The shortcut worth knowing: if one factor fits 16 bits, pre-shift it left by 16
and a single `MULSH` gives the Q16.16 product, because the high word of `a * (b
<< 16)` is `(a * b) >> 16`. Two things have to be true in the C for GCC to emit
it. The pre-shift must be a 32-bit shift, not a shift of a 64-bit value, and the
multiply must be written as the high word of a 64-bit product. Given both, a
loop over `(int32_t)(((int64_t)src[i] * (int64_t)s) >> 32)` with `s = scale <<
16` built above the loop compiles to one `slli` outside the loop and a body of
`l32i.n; addi.n; mulsh; s32i; addi.n` [measured]. Write the shift in 64-bit
instead and GCC builds the full 64x64 product: `srai; slli; ssai 16; src; srai;
mull; mull; muluh; add.n; add.n`, ten instructions where one would do
[measured]. So choose formats such that the scale factor sits in the high half
of a register and the multiply is `MULSH` alone.

## Rounding

An arithmetic right shift truncates toward negative infinity, so the result is
biased low by up to one unit in the last place, and across a chain of
multiplies that bias accumulates, because it is a bias and not noise. Round
half up by adding `1 << (s-1)` before a shift of `s`. On Q15 the constant is
`0x4000` and costs one instruction: `ADDMI` adds a sign-extended 8-bit
immediate shifted left by eight, so any multiple of 256 from -32768 to 32512 is
reachable, and GCC does emit `mul16s; addmi 0x4000; srai 15` for the obvious C
[measured].[^isamisc] For a constant that is not a multiple of 256, hoist a
`MOVI` out of the loop.

Round half up is not round to nearest even. The difference is half a unit of
bias on exact ties, which matters only when ties are common and correlated,
such as data that all lands on `.5` at your scale, or a repeated halving. Round
to nearest even costs a test of the bit below and the bit at the rounding
position, roughly three extra instructions, so pay it only after showing the
ties are systematic.

Espressif's own kernels split both ways. The Q15 constant multiply truncates,
with a bare `(int16_t)(acc >> 15)`.[^espdsp] The dot product seeds the
accumulator with the rounding term before the sum, so rounding costs nothing
per term: `long long acc = 0x7fff >> shift;` then one final shift.[^espdsp] Do
that whenever you sum before you shift. Rounding a 64-bit product is the
expensive case, because adding half a unit inside the low word can carry into
the high word and the carry needs a compare, so if you need rounding, pick a
format whose product fits one register.

## Saturation

`CLAMPS ar, as, imm` clamps to a signed field of `imm+1` bits in one
instruction, `imm` from 7 to 22, computing `min(max(x, -2^imm), 2^imm - 1)`, so
for the Q15 overflow above `CLAMPS ar, as, 15` is the entire fix, while `MIN`
and `MAX` handle arbitrary bounds in two instructions plus the bound
setup.[^isasat] GCC does not connect the two. For a plain C clamp to `[-32768,
32767]` it emits `l32r; min; l32r; max`, four instructions where `CLAMPS` would
do, and the code is the same whether the clamp is written with a conditional
operator or two `if` statements [measured]. As inline asm, `clamps a2, a2, 15`
is one instruction with identical values, so it is one of the few places where
inline asm pays for itself in a single line. On the PIE side saturation is
built in, since `EE.VADDS.S16` saturates each of its eight lanes as part of the
add.[^trmvadds] The TRM's rule is that an instruction which does not say it
saturates wraps around,[^trmsat] and `EE.VMUL.S16` does not say it.

## Division-free arithmetic

**Precompute the reciprocal.** If a divisor is constant across a row or frame,
compute `r = floor(2^k / d)` once and replace `n / d` with `(n * r) >> k`. The
error is bounded and one-sided: writing `r = (2^k - s) / d` with `0 <= s < d`
gives `n*r/2^k = n/d - n*s/(d*2^k)`, and `n*s/(d*2^k) < n/2^k`, so the real
value sits below `n/d` by less than `n/2^k` and the shift truncates at most one
more unit, putting the result in `(n/d - n/2^k - 1, n/d]`. Choose `k` so
`n_max/2^k` is well under one unit: with `n` below 2^16 and `k = 32` the
shortfall is under 2^-16, the answer is exact or one low, and the loop body is
a single `muluh` [measured].

**Refine per row with Newton-Raphson** when the setup divide is itself too
expensive, for example one divisor per scanline. Iterate `r' = r * (2 - d*r)`
at the same scale: writing `e = 1 - d*r`, the new error is `1 - d*r' = (1 -
d*r)^2 = e^2`, so each iteration squares the error and doubles the correct
bits. From an 8-bit table estimate, one iteration reaches about 16 bits and two
about 32, less the rounding each step adds, at about four instructions per
iteration, which beats a divide of unknown and data-dependent cost.

**Normalise first.** `NSAU at, as` returns the left shift that normalises an
unsigned value, 0 to 32, returning 32 for zero,[^isamisc] giving both the
exponent for a table-indexed first guess and the renormalising shift. GCC maps
`__builtin_clz` straight onto it, so the whole function body is `nsau a2, a2`
[measured].

**Square root: prefer a table.** The restoring shift-and-subtract square root
yields one result bit per iteration, so a 32-bit input costs 16 iterations of
compare, subtract and shift: fine at setup, far too slow per pixel. Instead
write `x = m * 2^e` using `NSAU` for `e`, then `sqrt(x) = sqrt(m) * 2^(e/2)`,
handling odd `e` by shifting `m` one place and decrementing `e`. A 256-entry
table indexed by the top 8 bits of the normalised mantissa gives about 8
significant bits for one `NSAU`, one shift, one load and one shift. Break even
is around 8 to 10 result bits; above that, the table plus a Newton-Raphson step
(`r' = (r + m/r)/2`, which needs a divide) usually loses to iteration.

## Sine and cosine from a phase accumulator

Keep the phase in a `uint32_t` and advance it by a constant increment. Wrapping
is free, because unsigned 32-bit addition already wraps at exactly one full
turn, so the loop needs no compare and no modulo. Index a table of `N = 2^L`
entries with the top `L` bits: `i = p >> (32 - L)`. Table size versus
interpolation is then arithmetic, not taste. For `N` entries over a turn the
spacing is `h = 2*pi/N`; a truncated lookup errs by up to `h` times the maximum
slope, which is `h` for sine, halved to `h/2` if the index is rounded to the
nearest sample, while a linear interpolant errs by at most `h^2 * max|f''| / 8`
with `max|f''| = 1`. So 1024 entries with a rounded index give `h/2 = pi/1024 =
3.07e-3`, about 8 bits, while 256 entries with 8-bit linear interpolation give
`h^2/8 = 7.5e-5`, about 13.7 bits from a table one quarter the size.
Interpolation costs one subtract, one multiply and one shift-add per sample and
buys roughly five bits, so where table placement decides whether a gather is
fast (see
[Caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)),
the smaller interpolated table usually wins.

## Linear interpolation and the sign trap

The standard form is `a + (((b - a) * t) >> s)` with `t` in `[0, 2^s]`. The
trap is that `>>` on a negative value rounds toward negative infinity, not
toward zero, so when `b < a` the interior of the ramp is biased low by up to
one unit. The endpoints stay exact: `t = 0` gives `a`, and `t = 2^s` gives `a +
(b-a)` exactly, since the shift undoes the multiply with no remainder. The
artefact is an interior half-unit bias whose sign flips with the direction of
the ramp, which shows up as a seam where two ramps meet. Do not fix it by
writing `/ (1 << s)`: C division rounds toward zero, so the compiler adds a
correction and the one-instruction `srai` becomes `movi; add.n; movgez; srai`
[measured]. Use the rounding term instead, `a + (((b - a) * t + half) >> s)`,
which costs one `ADDMI` and removes the direction dependence.

## Incremental evaluation

**Forward differences.** A linear field costs one add per step. For a quadratic
`f(x) = A*x^2 + B*x + C`, keep `f`, a first difference `d1 = A + B` and a
constant second difference `d2 = 2A`; each step is `f += d1; d1 += d2`. Two
adds, no multiply. In fixed point this is exact rather than drifting, on one
condition: the increments must be exact at the working scale. Adding integers
introduces no error, so if `d2` is exactly representable the recurrence
reproduces the polynomial exactly and the only risk is accumulator overflow,
which the growth rule above bounds. Round `d2` to fit and the error grows
quadratically in the step count.

**DDA and Bresenham error accumulators.** To advance `u` by `du/dv` per step
without dividing, keep an integer error term: `err += du; while (err >= dv) { u
+= 1; err -= dv; }`. This is Bresenham's construction, replacing a divide and a
modulo per step with an add and a compare.[^bresenham] The inner loop runs at
most `du/dv` times, so for a ratio under one it is a single predictable
compare, and `MOVGEZ` or `MOVLTZ` can make it branchless.[^isamisc]

**Per-row constant hoisting.** A field of the form `f(x,y) = g(y) + h(y)*x`
costs a multiply and an add per pixel written directly, and one add per pixel
if `g(y)` and `h(y)` are built once per row and `x` is stepped by forward
differences. The row-state builder is where multiplies and divides belong, but
in a kernel called once per small band of rows that setup is paid often, so
hoist without assuming it is free.

## Converting at the boundary, once

The FPU's conversion instructions carry a scale immediate, so float to fixed is
one instruction rather than a multiply plus a convert. `TRUNC.S ar, fs, 0..15`
scales by `2^t` and converts to signed integer with rounding toward zero,
saturating to `0x7fffffff` or `0x80000000` on overflow, infinity or NaN;
`ROUND.S` is the same with rounding to nearest; `FLOAT.S fr, as, 0..15`
converts an integer to float, scaling by `2^-t`.[^isafp] So `TRUNC.S a2, f0,
15` turns a float in `[-1,1)` straight into Q15, and since the immediate stops
at 15, Q16.16 and Q1.31 need an extra scale or a different split.

Convert at the edges of the kernel, never inside the loop: parameters arrive as
floats, are converted once in the row-state builder, and the loop body sees
only integers, which also keeps the FPU and its lazily saved coprocessor
context out of the way. `ROUND.S` is documented as rounding "toward the
nearest" without stating the tie rule, the manual's `rounds()` helper is not
defined, and the FCR rounding mode value 0 is likewise named only "round to
nearest",[^isafp] so if tie behaviour matters, test it [uncertain].

## PIE specifics

`EE.VMUL.S16 qz, qx, qy` multiplies eight 16-bit lanes, arithmetically
right-shifts each 32-bit intermediate by `SAR`, and writes the low 16 bits of
each shifted result into `qz`.[^trmvmul] That is a complete Qm.n multiply for
eight values in one instruction, with the shift free. Set `SAR` with `wsr.sar`
before the loop, as Espressif's own vector multiply kernel does before running
`ee.vmul.s16.ld.incp` inside a `loopnez`.[^espdsp] Four consequences follow.

- **`SAR` is the core's own register, not a PIE-private one**
  ([PIE register file, SAR and context](../02-pie-vector/pie-register-file-sar-and-context.md)
  carries the full register story). The TRM lists it
  as a plain "Special register" of 6 bits, against `SAR_BYTE` and the
  accumulators, which it labels "Customized special register".[^trmregs] The
  scalar `SRC`, `SLL` and `SRL` read the same register,[^isamisc] so a loop
  body cannot hold a vector scale in `SAR` and also use a scalar variable
  shift. Keep scalar shifts in the immediate forms `SRAI` and `SLLI`.
- **There is no 16-bit lane shift.** The only vector shifts are `EE.VSR.32` and
  `EE.VSL.32` on 32-bit lanes, and the rest of the shift group moves the whole
  128-bit register by bytes.[^trmregs] To scale 16-bit lanes by a power of
  two, multiply by a broadcast constant or fold the scale into `SAR`.
- **`EE.VMUL.S16` truncates**, with no rounding term and no saturation, so a
  scalar reference must also truncate or the two will disagree by one unit on
  about half the pixels.
- **Accumulate with the saturating adds.** `EE.VADDS.S16` and `EE.VSUBS.S16`
  clamp per lane, so an accumulation chain cannot wrap into the wrong
  sign.[^trmvadds]

## Cycle costs

The PIE chapter gives the stage at which each operand is read and written, plus
the interlock rule: if A defines a register at stage `SA` and B uses it at
stage `SB`, B issues `D = max(SA - SB + 1, 0)` cycles after A.[^trmpipe] The
operand table then gives real dependent-issue distances.[^trmpipe]

| Instruction | Def stage | Use stage | Dependent issue distance |
|---|---|---|---|
| `EE.VMUL.S16` | `qz` 2 | 1 | 2 |
| `EE.VADDS.S16`, `EE.VSUBS.S16` | `qa` 1 | 1 | 1 |
| `EE.VMIN.S16`, `EE.VMAX.S16` | `qa` 1 | 1 | 1 |
| `EE.VSR.32`, `EE.VSL.32` | `qa` 1 | 1 | 1 |
| `EE.VLD.128.IP` | `qu` 2 | 1 | 2 |

A chain of dependent vector multiplies stalls a cycle between each pair, and so
does a load feeding a multiply, while the saturating adds and the min/max chain
back to back. The fix is the usual one for an in-order machine: interleave two
independent lanes so one fills the other's stall slot. The ISA manual publishes
no scalar latencies and declares the multiply algorithm
implementation-dependent,[^isamul] so every scalar cycle count below is
`[uncertain]`.

| Operation | Scalar sequence | Scalar cycles | PIE equivalent |
|---|---|---|---|
| Q15 x Q15, truncating | `mul16s`, `srai 15` | [uncertain] | `ee.vmul.s16`, `SAR`=15, 8 lanes |
| Q15 x Q15, round half up | `mul16s`, `addmi 0x4000`, `srai 15` | [uncertain] | none, `EE.VMUL.S16` truncates |
| Q16.16 x Q16.16, general | `mull`, `mulsh`, `ssai 16`, `src` | [uncertain] | none, no 32-bit lane multiply |
| Q16.16 x 16-bit constant | `slli 16` hoisted, then `mulsh` | [uncertain] | none |
| Q1.31 x Q1.31 | `mulsh`, `slli 1` | [uncertain] | none |
| Unsigned high multiply | `muluh` | [uncertain] | none |
| Clamp to signed 16 bits | `clamps r, x, 15` (inline asm) | [uncertain] | inherent in `ee.vadds.s16` |
| Clamp to arbitrary bounds | `min`, `max`, plus bound setup | [uncertain] | `ee.vmin.s16`, `ee.vmax.s16` |
| Sign-extend a packed field | `sext r, x, n` | [uncertain] | none needed, lanes are typed |
| Lerp `a + ((b-a)*t >> s)` | `sub`, `mul16s`, `srai`, `add` | [uncertain] | `ee.vsubs.s16`, `ee.vmul.s16`, `ee.vadds.s16` |
| Normalise for reciprocal | `nsau` | [uncertain] | none |
| Float to Q15 | `trunc.s r, f, 15` | [uncertain] | none |
| Q15 to float | `float.s f, r, 15` | [uncertain] | none |
| Divide | `quos` | implementation-dependent[^isadiv] | none |

Two gaps remain, both [uncertain]. Table 1.7-2 gives the read side of the `SAR`
hand-off: `EE.VMUL.S16` uses `SAR` at stage 1.[^trmpipe] What is not published
is the write side, because a scalar `WSR.SAR` is a core instruction and the PIE
operand table covers extended instructions only, so the distance a kernel must
leave between setting `SAR` and the first multiply that reads it has to be
measured. And whether `MUL16S` and `MULL` share a latency here is not
published, which would change the format choice for 16-bit data if they
differ.

[^coreisa]: Espressif, ESP-IDF v5.5.1, `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`. The same file records `XCHAL_CORE_ID "LX7_ESP32_S3_MP"` and `XCHAL_HW_VERSION_NAME "LX7.0.12"`.
[^isadiv]: Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference Manual*, RC-2010.1 release, issue date 4/2010, section 4.3.6 "32-bit Integer Divide Option", page 59. <https://0x04.net/~mwk/doc/xtensa.pdf>
[^yatesrange]: Randy Yates, *Fixed-Point Arithmetic: An Introduction*, Digital Signal Labs technical reference, rev PA11, 14 August 2026: section 3.2 for the `A(a,b)` notation and `N = a+b+1`, section 4.4 for the signed range, section 4.5 for bit growth on addition, section 4.8 for signed multiplication, section 5.2 for resolution. <http://www.digitalsignallabs.com/downloads/fp.pdf>
[^isamul]: Xtensa ISA Reference Manual RC-2010.1, `MULL` page 450, `MULSH` page 455, `MULUH` page 456, `MUL16S` page 436, `MUL16U` page 437, section 4.3.4 page 57 and section 4.3.5 page 58, whose Table 4-32 on page 59 names `MulAlgorithm` as implementation-dependent.
[^isamisc]: Xtensa ISA Reference Manual RC-2010.1: `SRC` page 528, `SRAI` page 527, `ADDMI` page 253, `NSAU` page 462, and the conditional moves `MOVEQZ`, `MOVNEZ`, `MOVLTZ`, `MOVGEZ` in the core architecture instruction summary, section 4.2.
[^espdsp]: Espressif esp-dsp, `master` branch at sha 3c8ac0fdfec83740b783e200862c8d0c056de0ad (2026-05-12, one commit after tag `v1.8.2`): `modules/math/mulc/fixed/dsps_mulc_s16_ansi.c` for the truncating Q15 multiply, `modules/dotprod/fixed/dsps_dotprod_s16_ansi.c` for the seeded rounding term (its comment reads "To make correct round operation we have to shift round value"), and `modules/math/mul/fixed/dsps_mul_s16_aes3.S` for the `wsr.sar a9` before a `loopnez` over `ee.vmul.s16.ld.incp`. <https://github.com/espressif/esp-dsp>
[^isasat]: Xtensa ISA Reference Manual RC-2010.1, `CLAMPS` page 312, which the manual describes as intended for use with `ADD`, `SUB` and `MUL16S` to implement saturating arithmetic, plus `MAX` page 407 and `MIN` page 410.
[^trmvadds]: Espressif, *ESP32-S3 Technical Reference Manual*, version 1.8, section 1.8.70 `EE.VADDS.S16`, page 146. <https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf>
[^trmsat]: ESP32-S3 TRM v1.8, section 1.5.4 "Data Overflow and Saturation Handling", page 49.
[^bresenham]: J. E. Bresenham, "Algorithm for computer control of a digital plotter", *IBM Systems Journal*, vol. 4, no. 1, 1965, pages 25 to 30. DOI 10.1147/sj.41.0025.
[^isafp]: Xtensa ISA Reference Manual RC-2010.1, `TRUNC.S` page 548, `ROUND.S` page 497, `FLOAT.S` page 346, and Table 4-47 "FCR fields", announced on page 69 and printed on page 70; the manual's `rounds()` helper is used in the `ROUND.S` operation but is defined nowhere in the document.
[^trmvmul]: ESP32-S3 TRM v1.8, section 1.8.122 `EE.VMUL.S16`, page 198, and section 1.5.1.2 on `SAR`, page 46.
[^trmregs]: ESP32-S3 TRM v1.8, Table 1.5-1 "Register List of ESP32-S3 Extended Instruction Set", page 45, and Table 1.6-1 "Extended Instruction List", shift instruction group, section 1.6.7.
[^trmpipe]: ESP32-S3 TRM v1.8, section 1.7.1 "Data Hazard" and Table 1.7-2 "Extended Instruction Pipeline Stages", from page 65.
