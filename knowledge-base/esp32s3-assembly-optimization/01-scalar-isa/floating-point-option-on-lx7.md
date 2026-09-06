---
title: "The floating-point option on the LX7"
id: 01-scalar-isa/floating-point-option-on-lx7
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, floating-point, fpu, codegen, fixed-point]
confidence: medium
---

# The floating-point option on the LX7

The ESP32-S3's Xtensa LX7 core is built with the single-precision
Floating-Point Coprocessor Option. This is a real hardware unit with its
own register file and instructions, not software emulation. But it is
smaller than most programmers expect: no divide, no square root, and no
double precision, all as single instructions. This page covers what the
option gives you, what it does not, and what GCC 14 actually does with
it, measured on the toolchain this project uses.

## Confirming the option is present

The vendored ESP-IDF Xtensa config header for this chip states the
option directly:

```
#define XCHAL_HAVE_FP            1   /* single prec floating point */
#define XCHAL_HAVE_FP_DIV         1   /* FP with DIV instructions */
#define XCHAL_HAVE_FP_RECIP       1   /* FP with RECIP instructions */
#define XCHAL_HAVE_FP_SQRT        1   /* FP with SQRT instructions */
#define XCHAL_HAVE_FP_RSQRT       1   /* FP with RSQRT instructions */
#define XCHAL_HAVE_DFP            0   /* double precision FP pkg */
```

read from
`framework-espidf/components/xtensa/esp32s3/include/xtensa/config/core-isa.h`,
lines 130 to 134 and 136, in the local PlatformIO package tree
`~/.platformio/packages/`. [measured]

Two things follow, and both are easy to misread. First,
`XCHAL_HAVE_FP_DIV`, `_RECIP`, `_SQRT` and `_RSQRT` being `1` does
**not** mean the core has one-instruction hardware divide or square
root; it means the *begin* instructions for an iterative refinement
sequence exist (below). Second, `XCHAL_HAVE_DFP` is `0`: there is no
double-precision hardware at all, not even a refinement sequence. Every
`double` operation is software.

GCC exposes no command-line switch to turn the FP option on or off for
Xtensa: the GCC 14 manual's Xtensa options page lists eleven flags
(`-mconst16`, `-mserialize-volatile`, `-mforce-no-pic`,
`-mtext-section-literals`, `-mauto-litpools`, `-mtarget-align`,
`-mlongcalls`, `-mabi=`, `-mextra-l32r-costs=`, `-mstrict-align`,
`-mforce-l32`) and none about floating point.[^1] Whether the compiler
can use the FP registers is baked into the target it was configured for
(`xtensa-esp32s3-elf-gcc`), not something toggled per file.

## The register file: 16 f registers, FCR, FSR

The option adds 16 single-precision registers, `f0` through `f15`, each
32 bits, separate from the `a0`-`a15` address/data registers used by the
rest of the ISA.[^2] They hold IEEE-754 single-precision values only;
there is no wider mode.

Two special registers travel with the option, both accessed as user
registers through `rur`/`wur` rather than as ordinary `a` or `f`
registers:

- **FCR** (Floating-Point Control Register), user register 232.
- **FSR** (Floating-Point Status Register), user register 233.

Both numbers come from the vendored assembly macros in
`.../xtensa/config/tie-asm.h`, lines 206 and 208 (`rur.FCR`, `rur.FSR`)
and 249 and 251 (`wur.FCR`, `wur.FSR`), commented `// ureg 232` and `//
ureg 233`. [measured] FCR carries the rounding mode; FSR carries the
sticky exception flags (invalid, divide-by-zero, overflow, underflow,
inexact), the same shape as the status register on most IEEE-754
FPUs.[^3]

## The instruction set

Grouped by what each does. All operate on `f` registers unless noted.

**Arithmetic:** `add.s`, `sub.s`, `mul.s` are the three basic
single-precision operations. `madd.s` (fused multiply-add, `acc +=
a*b`) and `msub.s` (fused multiply-subtract) fold a multiply and an
add/subtract into one instruction and one rounding step. `neg.s` and
`abs.s` flip or clear the sign bit; `mov.s` copies between `f`
registers; `moveqz.s` and its siblings conditionally move an `f`
register based on an **integer** register's comparison to zero, so a
scalar condition can drive a float select without a boolean-register
round trip.[^1]

**Compares:** `oeq.s`, `olt.s`, `ole.s` (ordered equal / less-than /
less-or-equal) and `ueq.s`, `ult.s`, `ule.s` (unordered forms, true when
either operand is NaN) each write a single-bit **boolean register**
(`b0`..`b15`), not a general register and not a condition-code flag.
Ordered and unordered forms exist because IEEE-754 NaN comparisons are
not simply each other's negation.[^1]

**Conversions:** `float.s`/`ufloat.s` convert signed/unsigned integer to
float; `trunc.s`, `utrunc.s`, `round.s`, `floor.s`, `ceil.s` convert
float to signed/unsigned integer under the named rounding rule. All six
take a 4-bit immediate, 0-15, that scales the value by a power of two as
part of the conversion: a free multiply or divide by `2^n` folded into
the int/float conversion, exactly what a Q-format fixed-point load or
store needs.[^1] See below for what GCC does with that immediate.

**Loads, stores, moves:** `lsi`/`ssi` (float load/store, immediate
offset) and `lsiu`/`ssiu` (same, base register updated afterward) move a
32-bit value directly between memory and an `f` register. `rfr`/`wfr`
move a value between an `f` register and an `a` register without
touching memory.[^1]

## What is not there

No divide instruction produces a correctly-rounded quotient in one step,
and there is no single-instruction square root. Instead there is a
**begin-then-refine** sequence: `div0.s`/`recip0.s` (reciprocal begin)
and `sqrt0.s`/`rsqrt0.s` (reciprocal square root begin) each produce a
low-precision seed, which a short Newton-Raphson-style loop then
sharpens using ordinary multiply-add instructions plus two
refinement-specific ones, `nexp01.s` and `maddn.s`.[^4] A worked example
chains `rsqrt0.s` into two refinement stages built from `mul.s`,
`const.s`, `msub.s`, `madd.s` and `maddn.s` before the result is accurate
enough to use.[^5] The seed alone is not a usable answer; the point of
having it in hardware is that the refinement converges in two or three
steps instead of the ten-plus a pure software bisection would need, not
that division becomes one instruction.

Double precision is absent entirely, seed instructions included
(`XCHAL_HAVE_DFP` is `0`). Every `double` add, multiply, compare and
conversion is a libgcc or newlib software routine.

## What GCC 14.2 actually emits

Architecture facts about what the option *could* do are only half the
picture; what the compiler *chooses* to do with a stock C expression is
the other half, and the two do not match as often as expected. The
following was compiled with the toolchain this project builds with,
`xtensa-esp32s3-elf-gcc (crosstool-NG esp-14.2.0_20241119) 14.2.0`, at
`-O2`, no extra flags beyond `-mlongcalls`. [measured, 2026-09-06]

**Division and square root are library calls, not hardware sequences.**
`float fdiv(float a, float b) { return a / b; }` compiles to `call8
__divsf3`. `float fsqrt(float a) { return __builtin_sqrtf(a); }`
compiles to `call8 sqrtf`. Neither uses `div0.s`/`recip0.s` or
`sqrt0.s`/`rsqrt0.s`, despite the seed instructions being present in the
target's configuration. GCC 14.2's Xtensa backend treats single-precision
divide and sqrt as ordinary library calls, the same as it would on a
target with no FP option at all; using the seed instructions means
writing the refinement loop by hand in inline assembly. "The FP option
has divide bits" is not, by itself, a performance argument for plain C
on this compiler. Double division and sqrt are fully software too, as
`XCHAL_HAVE_DFP` being 0 predicts: `a / b` on `double` compiles to `call8
__divdf3`, `sqrt(double)` to `call8 sqrt`, no FP registers touched at
all.

**`madd.s`/`msub.s` are emitted for plain `a*b+c`, even without
`-ffast-math`.** `float fma_(float a, float b, float c) { return a * b +
c; }` compiles to one `madd.s`, no library call, no separate multiply
and add. GCC's default floating-point contraction setting folds a
multiply-then-add into a fused instruction unless `-ffp-contract=off` is
given. This is a genuine single-rounding-step fused multiply-add: the
intermediate product is not rounded before the add, a correctness-
relevant difference from separate multiply and add, and it happens by
default.

**Comparisons produce a boolean register, then a conditional move or
branch, never a flags-register branch.** `int lt(float a, float b) {
return a < b; }` compiles to `olt.s b0, f0, f1` then `movf a2, a8, b0`
(move-if-false, between pre-loaded 0 and 1). A comparison used for
control flow instead compiles to the same `olt.s` then `bf`/`bt` (branch
if the boolean register is false/true), a direct branch on the boolean
register with no separate flag test. Either way, a float compare costs
one FP compare plus one integer conditional move or branch.

**`a < 0 ? -a : a` is a branch unless `-ffast-math` is given, in which
case it becomes `abs.s`.** Without `-ffast-math`, GCC emits `olt.s` +
`bf` + `neg.s`, a real conditional branch, not a select. With
`-ffast-math` the same source becomes one `abs.s`. The difference is
`-0.0` and NaN handling: the branch form preserves exact IEEE-754
semantics for those edge cases, and only `-ffast-math`'s relaxed rules
let GCC treat the ternary as `abs()`. Do not expect `abs.s` from
ordinary `-O2`.

**`fmaxf`/`fminf` called by name are library calls; the equivalent
ternary is not.** `float fmaxf_(float a, float b) { return fmaxf(a, b);
}` compiles to `call8 fmaxf` even at `-O2`. `float fmax_(float a, float
b) { return a > b ? a : b; }` compiles to `olt.s` + `movf`, no call at
all. GCC does not recognize its own library function by name and
replace it with the hardware compare-and-select; it only finds that
pattern in the ternary. Prefer the ternary over the libm name when the
exact NaN/signed-zero behavior of `fmaxf` is not required.

**The conversion scale immediate exists in the ISA but this compiler
does not use it to fold a preceding multiply.** `int
to_fixed_256(float a) { return (int)(a * 256.0f); }` was written
expecting GCC to fold the `*256.0f` into `trunc.s`'s scale immediate. It
does not: the code is `mul.s` against a loaded constant, then `trunc.s
a2, f0, 0`, scale immediate still zero, multiply still separate. The
scale immediate is real and documented, but this GCC version has no
peephole that reaches it from ordinary arithmetic; getting the free
scale means writing `trunc.s ..., 8` by hand in inline assembly.

**Simple float loads and stores skip the `f` registers entirely.**
`float loadf(float *p) { return *p; }` compiles to `l32i.n a2, a2, 0`, an
ordinary integer load, never `lsi`. A value enters the `f` register file
(via `wfr`) only immediately before an FP arithmetic or compare
instruction, and leaves it (via `rfr`) immediately after. Passing a
float through, loading it, storing it, or holding it in a struct costs
nothing more than an int would.

**A `float` literal and a `double` literal generate different code even
for the same value**, because the type decides the constant-pool entry
and the arithmetic path. `1.0f` loads one 32-bit literal; `1.0` (no
suffix) loads a 64-bit literal across two `a` registers and routes any
arithmetic on it through the software double path regardless of the FP
option. A bare numeric literal in C is `double` unless suffixed `f`; a
stray unsuffixed literal in an otherwise-`float` expression is what most
often pulls the whole expression into software.

## Cycle costs: what is and is not confirmed

The sources checked for this page, the Cadence ISA Summary[^4] and the
ISA Reference Manual's floating-point coprocessor chapter[^1], describe
instruction semantics and the divide/sqrt refinement structure, but no
per-instruction cycle-latency table turned up. Treat any specific cycle
count for `add.s`, `mul.s`, `madd.s` or a compare as `[uncertain]` until
measured on the device or in QEMU, the rule this KB applies to every
instruction class. What can be said with more confidence,
architecturally: `madd.s`/`msub.s` do a multiply and an add for roughly
the latency of one operation, not two, which is why the compiler's
default contraction behavior above matters even without a cycle number.

## The ESP-IDF rule: no FPU in most interrupt contexts

ESP-IDF's FreeRTOS port does not save FPU register state on every
context switch; it does so lazily, tied to whichever task last used the
FP registers. An interrupt can run on top of a task without that task's
FP registers being saved first, so touching a `float` inside an ISR can
corrupt the interrupted task's floating-point state.[^6] Only the lowest
interrupt priority level (level 1) can opt in, via
`CONFIG_FREERTOS_FPU_IN_ISR`; higher levels cannot use `float` at all.
`double` arithmetic never touches the FP coprocessor registers (it is
pure software on the integer pipeline), so this restriction does not
apply to it: a `double` computation is safe in any interrupt context
where the software routines are otherwise safe to call, at the cost of
being much slower than hardware single precision.

## Decision table: float versus Q-format fixed point

| Operation | Hardware float cost | Fixed point (Qn.m) cost | Prefer |
|---|---|---|---|
| Add, subtract | One `add.s`/`sub.s` | One integer add/subtract | Either; float is not a tax here |
| Multiply | One `mul.s` | Multiply then arithmetic shift to rescale | Either; float avoids the shift and the overflow bookkeeping |
| Multiply-accumulate / inner product | One `madd.s`/`msub.s` per term, single rounding step each | Wider integer accumulator, one shift at the end, exact until overflow | Either; float is simpler to get right first, fixed point is exact if sized correctly |
| Divide | Library call (`__divsf3`), tens of cycles, unless a hand-written seed+refine sequence | Reciprocal multiply if the divisor is constant or slow-varying; otherwise a slow software integer divide | Avoid at runtime for both; precompute a reciprocal when possible |
| Square root | Library call (`sqrtf`), unless hand-written `sqrt0.s`/`rsqrt0.s` refinement | Integer square-root algorithm or a fixed-point Newton iteration | Depends on precision needed; a bounded 2-step fixed-point Newton iteration can beat the library call |
| Compare and branch | One FP compare + one branch on the boolean register | One integer compare + branch | Either; both cost one compare and one branch |
| Compare and select | One FP compare + one conditional move, if written as a ternary | One integer compare + conditional move | Either, but write it as a ternary, not a call to `fmaxf`/`fminf` |
| Int-to-fixed / fixed-to-int with a power-of-two scale, or a table/LUT/palette index | `float.s`/`trunc.s` with a nonzero scale immediate, written by hand; GCC will not find it from `x * 256.0f` | A single shift, or already an integer index | Fixed point, unless the conversion is hand-written in inline assembly |
| Code that must run inside a higher-priority ISR | Not usable (see restriction above) | Fully usable | Fixed point, no exception |
| Code using `double` for readability, not range | Full software path regardless of the FP option; often the biggest single de-optimization available | Pick a fixed-point format sized for the actual range instead | Neither `double`; use `float` or fixed point |

## Footnotes

[^1]: Cadence/Tensilica, *Xtensa ISA Reference Manual*, Floating-Point
    Coprocessor Option chapter (search-indexed as section 4.3.11, with a
    "Divide and Square Root Sequences" subsection). The manual PDF
    exceeds this page's fetch-tool size limit, so the exact page numbers
    and edition are `[uncertain]`; mnemonics above are corroborated by
    GCC's own Xtensa backend output (`[measured]` sections) and the
    Cadence ISA Summary[^4]. Confirmed directly: GCC 14, "Xtensa
    Options", <https://gcc.gnu.org/onlinedocs/gcc/Xtensa-Options.html>,
    lists no floating-point-related flag.

[^2]: Register count/naming (`f0`-`f15`): public GCC development
    discussion of the Xtensa target (mail-archive.com/gcc-patches, 2021,
    "xtensa: Allow SImode GENERAL_REGS pseudos to spill into FP_REGS
    hardregs"). Not checked against the primary ISA manual, so
    `[uncertain]` pending that.

[^3]: FCR/FSR register numbers: local vendored file
    `.../xtensa/config/tie-asm.h`, lines 206, 208, 249, 251 (PlatformIO
    package tree). [measured] FCR/FSR's general shape (rounding mode;
    sticky IEEE-754 exception flags) follows the standard Xtensa FP
    option description; the bit layout itself is `[uncertain]`.

[^4]: Cadence, *Xtensa ISA Summary for all Xtensa LX Processors*,
    RI-2021.8, 04/2022. Search-indexed content names `DIV0.S` ("Divide
    Begin Single"), `RECIP0.S` ("Reciprocal Begin Single"), `RSQRT0.S`
    ("Reciprocal Sqrt Begin Single") as refinement starters, not
    complete operations. The PDF returned HTTP 403 when fetched
    directly, so exact wording is `[uncertain]` pending a direct read.

[^5]: "Xtensa LX Square Root/Reciprocal Square Root Inline ASM
    Sequence", Giga Bowser, <https://gigabowser.dev/posts/xtensa-rsqrt/>.
    A two-stage Newton-Raphson refinement built from `rsqrt0.s`,
    `mul.s`, `const.s`, `msub.s`, `madd.s`, `maddn.s`; states plainly
    that `rsqrt0.s` alone is an approximation.

[^6]: FPU-in-ISR restriction: Espressif developer blog, "Floating-Point
    Units on Espressif SoCs: Why (and when) they matter", October 2025,
    <https://developer.espressif.com/blog/2025/10/cores_with_fpu/>
    (single-precision-only hardware, software double); cross-checked
    against the esp32.com forum thread "Floating point in ISR?" and
    GitHub issue espressif/esp-idf#722 on the lazy FPU-context-switch
    design and `CONFIG_FREERTOS_FPU_IN_ISR`. Exact Kconfig path not
    opened against current ESP-IDF source: `[uncertain]`.
