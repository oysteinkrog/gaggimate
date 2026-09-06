---
title: "The floating-point option on the LX7"
id: 01-scalar-isa/floating-point-option-on-lx7
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, floating-point, fpu, codegen, fixed-point]
confidence: high
---

# The floating-point option on the LX7

The ESP32-S3's Xtensa LX7 core is built with the single-precision
Floating-Point Coprocessor Option. This is a real hardware unit with its
own register file and instructions, not software emulation. But it is
smaller than most programmers expect. There is no divide instruction, no
square-root instruction, and no double precision at all. This page covers
what the option gives you, what it does not, and what GCC 14 actually does
with it, measured on the toolchain this project uses.

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
lines 130 to 135, in the local PlatformIO package tree
`~/.platformio/packages/`. [measured]

Two things follow, and both are easy to misread. First,
`XCHAL_HAVE_FP_DIV`, `_RECIP`, `_SQRT` and `_RSQRT` being `1` does
**not** mean the core has one-instruction hardware divide or square
root; it means the *begin* instructions for an iterative refinement
sequence exist (below). The local assembler settles it: `div.s` and
`sqrt.s` are rejected as unknown opcodes, while `div0.s`, `recip0.s`,
`sqrt0.s`, `rsqrt0.s`, `nexp01.s`, `maddn.s`, `divn.s` and `const.s` all
assemble.[^7] Second, `XCHAL_HAVE_DFP` is `0`: there is no
double-precision hardware at all, not even a refinement sequence. Every
`double` operation is software.

GCC has no command-line switch that turns the FP option on or off for
Xtensa. The GCC 14.2 manual's Xtensa options page lists eleven distinct
options: `-mconst16`, `-mfused-madd`, `-mserialize-volatile`,
`-mforce-no-pic`, `-mtext-section-literals`, `-mauto-litpools`,
`-mtarget-align`, `-mlongcalls`, `-mabi=`, `-mextra-l32r-costs=` and
`-mstrict-align`.[^1] Exactly one of them is about floating point:
`-mfused-madd`/`-mno-fused-madd`, which enables or disables the fused
multiply-add and multiply-subtract instructions and "has no effect if the
floating-point option is not also enabled".[^1] Whether the compiler can
use the FP registers at all is baked into the target it was configured
for (`xtensa-esp32s3-elf-gcc`), not something toggled per file.

## The register file: 16 f registers, FCR, FSR

The option adds a register file `FR` of 16 registers, 32 bits each,
named `f0` through `f15`, separate from the `a0`-`a15` address/data
registers used by the rest of the ISA.[^2] They hold IEEE-754
single-precision values only; there is no wider mode.

Two special registers travel with the option, both accessed as user
registers through `rur`/`wur` rather than as ordinary `a` or `f`
registers:[^2]

- **FCR** (Floating-Point Control Register), user register 232.
- **FSR** (Floating-Point Status Register), user register 233.

Both numbers are also in the vendored assembly macros in
`.../xtensa/config/tie-asm.h`, lines 206 and 208 (`rur.FCR`, `rur.FSR`)
and 249 and 251 (`wur.FCR`, `wur.FSR`), commented `// ureg 232` and `//
ureg 233`. [measured]

FCR holds a 2-bit rounding mode (0 nearest, 1 toward zero, 2 toward
+infinity, 3 toward -infinity) and five exception-enable bits, one each
for invalid, divide-by-zero, overflow, underflow and inexact. FSR holds
the five matching IEEE-754 status flags.[^3] One caveat matters more than
the layout: the manual states that current implementations neither raise
the exceptions FCR enables nor set the flag bits in FSR.[^3] Do not build
a numerical check on reading FSR back.

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
float to signed/unsigned integer under the named rounding rule. All seven
take a 4-bit immediate `t`, range 0 to 15, that scales the value by a
power of two as part of the conversion. The sign of the exponent depends
on which way you are converting: the two integer-to-float instructions
multiply by `2^-t`, the five float-to-integer ones multiply by `2^t`
before rounding.[^1]
That is a free multiply or divide by a power of two folded into the
conversion, exactly what a Q-format fixed-point load or store needs. The
assembler accepts 0 through 15 and rejects 16.[^7] See below for what GCC
does with that immediate.

**Loads, stores, moves:** `lsi`/`ssi` (float load/store, immediate
offset) and `lsx`/`ssx` (indexed by a second `a` register) move a 32-bit
value directly between memory and an `f` register. `rfr`/`wfr` move a
value between an `f` register and an `a` register without touching
memory.[^1] The manual also defines base-update forms `lsiu`, `ssiu`,
`lsxu` and `ssxu` for this option, but all four are rejected as unknown
opcodes by the toolchain this project builds with, so they are not
reachable from inline assembly here.[^7]

## What is not there

There is no divide instruction and no square-root instruction. The 2010
ISA manual's tables for this option list neither: Table 4-46 (the option
summary), Table 4-49 (loads and stores) and Table 4-50 (operations) name
add, subtract, multiply, multiply-add, multiply-subtract, negate,
absolute value, moves, compares and conversions, and nothing else.[^1]
The strings `DIV.S` and `SQRT.S` do not occur anywhere in that 662-page
manual, and the shipping assembler rejects both mnemonics.[^7]

Instead there is a **begin-then-refine** sequence. `div0.s` and
`recip0.s` (divide begin and reciprocal begin) and `sqrt0.s` and
`rsqrt0.s` (square root begin and reciprocal square root begin) each
produce a low-precision seed, which a short Newton-Raphson-style loop then
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

What the software divide costs, and how it compares with a multiply, is
in [the cost model leaf](../00-foundations/lx7-core-pipeline-and-cost-model.md).
The full option table for this core is in
[the configured-options leaf](./core-isa-and-configured-options.md).

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
multiply-then-add into a fused instruction. Two flags undo it, and both
produce the same `mul.s` then `add.s` pair: `-ffp-contract=off`, and the
Xtensa-specific `-mno-fused-madd`. This is a genuine single-rounding-step
fused multiply-add: the intermediate product is not rounded before the
add, a correctness-relevant difference from separate multiply and add,
and it happens by default.

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
ISA Reference Manual's floating-point coprocessor option[^1], describe
instruction semantics, but no per-instruction cycle-latency table turned
up: section 4.3.11 has no timing table at all. Treat any specific cycle
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
The Kconfig entry is "Use float in Level 1 ISR", default off, offered
only on the ESP32 and the ESP32-S3, and its help text says plainly that
"usage of float types in higher level interrupts is still not
permitted".[^7]
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

[^1]: Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference
    Manual*, issue date 4/2010 (for all Xtensa processor cores), section
    4.3.11 "Floating-Point Coprocessor Option", pages 67 to 74: 4.3.11.1
    with Table 4-45 (processor state) and Table 4-46 (instruction
    additions) on pages 67 to 68, 4.3.11.2 "Floating-Point
    Representation" and 4.3.11.3 "Floating-Point State" on pages 69 to
    70, 4.3.11.4 "Floating-Point Exceptions" on page 71, and 4.3.11.5
    "Floating-Point Instructions" with Table 4-49 (load/store) and Table
    4-50 (operations) on pages 71 to 74. Public mirror:
    <https://0x04.net/~mwk/doc/xtensa.pdf>. Also cited for the GCC
    option list: GNU Project, *Using the GNU Compiler Collection (GCC)*
    14.2.0, "Xtensa Options",
    <https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Xtensa-Options.html>,
    fetched 2026-09-06, which is the source of the eleven-option list and
    of the quoted `-mfused-madd` description.

[^2]: Same manual, Table 4-45 "Floating-Point Coprocessor Option
    Processor-State Additions", section 4.3.11.1, page 67: `FR`,
    quantity 16, width 32 bits; `FCR`, user register 232; `FSR`, user
    register 233. Section 4.3.11.3 on page 69 repeats that the FR file
    "consists of 16 registers of 32 bits each".

[^3]: Same manual, section 4.3.11.3 "Floating-Point State", pages 69 to
    70, with Table 4-47 (FCR fields: `RM` rounding mode plus `I`, `U`,
    `O`, `Z`, `V` exception-enable bits) and Table 4-48 (FSR fields: the
    five matching status flags), and section 4.3.11.4 "Floating-Point
    Exceptions", page 71: "Current implementations neither raise
    exceptions enabled by FCR bits nor set flag bits in FSR." Register
    numbers cross-checked in the local vendored file
    `.../xtensa/config/tie-asm.h`, lines 206, 208, 249, 251 (PlatformIO
    package tree). [measured]

[^4]: Cadence, *Xtensa ISA Summary for all Xtensa LX Processors*,
    RI-2021.8, 04/2022. Search-indexed content names `DIV0.S` ("Divide
    Begin Single"), `RECIP0.S` ("Reciprocal Begin Single"), `RSQRT0.S`
    ("Reciprocal Sqrt Begin Single") as refinement starters, not
    complete operations. The PDF returned HTTP 403 when fetched
    directly, so exact wording is `[uncertain]` pending a direct read.
    The seed instructions are absent from the 2010 manual in [^1]
    entirely, so a later ISA revision added them; which one is
    `[uncertain]`. Their presence on this target is settled by the
    assembler instead[^7].

[^5]: "Xtensa LX Square Root/Reciprocal Square Root Inline ASM
    Sequence", Giga Bowser, <https://gigabowser.dev/posts/xtensa-rsqrt/>.
    A two-stage Newton-Raphson refinement built from `rsqrt0.s`,
    `mul.s`, `const.s`, `msub.s`, `madd.s`, `maddn.s`; states plainly
    that `rsqrt0.s` alone is an approximation.

[^6]: FPU-in-ISR restriction: Espressif developer blog, "Floating-Point
    Units on Espressif SoCs: Why (and when) they matter", October 2025,
    <https://developer.espressif.com/blog/2025/10/cores_with_fpu/>
    (single-precision-only hardware, software double); cross-checked
    against GitHub issue espressif/esp-idf#722 on the lazy
    FPU-context-switch design.

[^7]: [measured] 2026-09-06, `xtensa-esp32s3-elf-as` and
    `xtensa-esp32s3-elf-gcc` from `xtensa-esp-elf` GCC 14.2.0
    (crosstool-NG esp-14.2.0_20241119, GNU assembler 2.43.1). One
    mnemonic per source file, assembled and disassembled. Rejected as
    "unknown opcode or format name": `div.s`, `sqrt.s`, `lsiu`, `ssiu`,
    `lsxu`, `ssxu`. Accepted: `div0.s` (two operands), `recip0.s`,
    `sqrt0.s`, `rsqrt0.s`, `nexp01.s`, `maddn.s`, `divn.s`, `const.s`,
    `addexp.s`, `addexpm.s`, `mkdadj.s`, `mksadj.s`, `lsi`, `ssi`,
    `lsx`, `ssx`, `rfr`, `wfr`, `rur.fcr`, `wur.fsr`, and every
    arithmetic, compare, move and conversion mnemonic named on this
    page. `float.s f0, a2, 15` and `trunc.s a2, f0, 15` assemble;
    both fail at 16 with "operand 3 has invalid value '16'". Kconfig
    citation: ESP-IDF 5.5.1, `components/freertos/Kconfig`, lines 464 to
    470, `config FREERTOS_FPU_IN_ISR`.
