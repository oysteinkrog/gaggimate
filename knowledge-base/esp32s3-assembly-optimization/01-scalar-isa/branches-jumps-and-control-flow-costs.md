---
title: Branches, jumps and control flow on the ESP32-S3 (LX7)
id: 01-scalar-isa/branches-jumps-and-control-flow-costs
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, branch, jump, call, control-flow, gcc, pipeline]
confidence: medium
---

# Branches, jumps and control flow on the ESP32-S3

Every branch and jump the Xtensa Core Architecture defines, the immediate
tables that make the encoded-constant branches useful, what a taken branch
costs on this chip, and how GCC 14 turns an `if`, a `switch` and a bit test
into one of these instructions. The base ISA has no separate compare step:
a compare and a conditional jump are one instruction. This page covers the
non-loop control flow; the zero-overhead loop instructions (`LOOP`,
`LOOPNEZ`, `LOOPGTZ`) that remove a loop's back edge entirely are a
different mechanism, covered in
[zero-overhead loops](./zero-overhead-loops.md). The cycle cost model this
page's numbers come from, including the pipeline stage table, is
[the LX7 pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md).

## 1. The three encoding sizes

Every branch or jump on this core is one of three widths. `[obvious]`

| Format | Width | Offset field | Used by |
|---|---|---|---|
| `RRI8` | 24 bits (3 bytes) | signed 8-bit, or 4-bit encoded constant | register-compare and bit-test branches |
| `BRI12` | 24 bits (3 bytes) | signed 12-bit | `BEQZ`, `BNEZ`, `BLTZ`, `BGEZ` |
| `RI6` (narrow) | 16 bits (2 bytes) | unsigned 6-bit, forward only | `BEQZ.N`, `BNEZ.N` |
| `CALL` | 24 bits (3 bytes) | signed 18-bit | `J`, `CALL0`, `CALL4`, `CALL8`, `CALL12` |
| `CALLX` | 24 bits (3 bytes) | none (register indirect) | `JX`, `CALLX0`, `CALLX4`, `CALLX8`, `CALLX12` |

All conditional-branch and jump target addresses are computed as
`PC + sign_extend(offset) + 4`, where `PC` is the address of the branch
instruction itself, except the narrow `.N` forms, which zero-extend an
unsigned offset (forward branches only)
[Tensilica 2010][^isa-beq][^isa-beqzn]. The `+4` is the length of the
branch instruction plus one; every worked range below already includes it.

## 2. Register-compare branches: `BEQ`, `BNE`, `BLT`, `BGE` and unsigned forms

`RRI8` format, three bytes, comparing two address registers. The signed
forms are `BEQ`, `BNE`, `BLT` (signed less than), `BGE` (signed greater or
equal); the unsigned forms are `BLTU`, `BGEU`
[Tensilica 2010][^isa-cond-table] p. 40. Each is its own inverse pair:
`BEQ`/`BNE`, `BLT`/`BGE`, `BLTU`/`BGEU` [Tensilica 2010][^isa-beq] p. 272,
[^isa-bge] p. 277, [^isa-blt] p. 282.

Offset field is a signed 8-bit immediate, so the range relative to the
branch instruction's own address is **-124 to +131 bytes**
(`-128 + 4` to `127 + 4`) [Tensilica 2010][^isa-beq] p. 272 (`BEQ`'s
operation equation is representative of the whole `RRI8` family). The
assembler substitutes an equivalent multi-instruction sequence when a
label is out of range for any of these, unless the mnemonic is prefixed
with an underscore (`_BEQ`, `_BNE`, ...), which forces an assembly error
instead [Tensilica 2010][^isa-beq] p. 272.

Measured codegen for a comparison against a small constant, not zero
(`xtensa-esp32s3-elf-gcc -O2 -mlongcalls -S`, 14.2.0,
esp-14.2.0_20241119):

```c
extern void side_a(void);
extern void side_b(void);
int small_if(int a) {
    if (a == 3) { side_a(); return 10; }
    side_b();
    return 20;
}
```

```
small_if:
	entry	sp, 32
	bnei	a2, 3, .L2
	call8	side_a
	movi.n	a2, 0xa
	j	.L1
.L2:
	call8	side_b
	movi.n	a2, 0x14
.L1:
	retw.n
```

GCC picked `BNEI` (the encoded-immediate form, §3) directly rather than
loading 3 into a register and using `BNE`; a compare against zero would
use `BEQZ`/`BNEZ` instead (§4). When the branch is followed by two
different constant results and no side effects, GCC drops the branch
entirely and uses a conditional move (`MOVNEZ`) instead: an unrelated test
of `if (a == 3) return 10; return 20;` (no calls) compiled to
`addi a2,a2,-3; movi.n a8,0x14; movi.n a9,0xa; movnez a9,a8,a2` with no
branch instruction at all `[measured]`. Whether a branch is emitted at all
depends on what the two arms do, not just on the source having an `if`.

## 3. Encoded-immediate branches: `BEQI`, `BNEI`, `BLTI`, `BGEI` and unsigned forms

Same `RRI8` word as §2, but the second operand is a 4-bit field decoded
through a lookup table instead of a register, so a compare against one of
sixteen common constants costs no register load
[Tensilica 2010][^isa-beqi] p. 273. `BEQI`/`BNEI` and `BGEI`/`BLTI` are
inverse pairs, likewise `BGEUI`/`BLTUI`
[Tensilica 2010][^isa-beqi] p. 273, [^isa-bgei] p. 278,
[^isa-bgeui] p. 280. Offset range is the same `RRI8` -124 to +131 bytes.
The assembler will fold a `BEQI`/`BNEI` against zero down to `BEQZ`,
`BNEZ` or the narrow `BEQZ.N` automatically unless the mnemonic has the
disabling underscore prefix [Tensilica 2010][^isa-beqi] p. 273.

The 4-bit field is not `0..15`. It is looked up in one of two 16-entry
tables, reproduced in full
[Tensilica 2010][^isa-b4const] pp. 41-42:

**Table 3-17, `b4const` (signed forms: `BEQI`, `BNEI`, `BGEI`, `BLTI`):**

| Encoding | Value | Encoding | Value |
|---|---|---|---|
| 0 | -1 | 8 | 8 |
| 1 | 1 | 9 | 10 |
| 2 | 2 | 10 | 12 |
| 3 | 3 | 11 | 16 |
| 4 | 4 | 12 | 32 |
| 5 | 5 | 13 | 64 |
| 6 | 6 | 14 | 128 |
| 7 | 7 | 15 | 256 |

**Table 3-18, `b4constu` (unsigned forms: `BGEUI`, `BLTUI`):**

| Encoding | Value | Encoding | Value |
|---|---|---|---|
| 0 | 32768 | 8 | 8 |
| 1 | 65536 | 9 | 10 |
| 2 | 2 | 10 | 12 |
| 3 | 3 | 11 | 16 |
| 4 | 4 | 12 | 32 |
| 5 | 5 | 13 | 64 |
| 6 | 6 | 14 | 128 |
| 7 | 7 | 15 | 256 |

Notice encodings 2-15 are identical between the two tables; only 0 and 1
differ (-1/1 for the signed table versus the two large powers of two,
32768 and 65536, for the unsigned one). Any constant not in the relevant
table needs a register load (`MOVI` or `L32R`) and a plain `BEQ`/`BLT`/etc
from §2. This is why `small_if` above got `BNEI a2, 3, ...` for free: 3 is
encoding 3 in Table 3-17.

## 4. Compare against zero: `BEQZ`, `BNEZ`, `BLTZ`, `BGEZ`, and the narrow forms

These use the wider `BRI12` word: a signed 12-bit offset instead of the
8-bit field the rest of the family gets, because comparing against zero is
common enough to spend the extra format on more reach
[Tensilica 2010][^isa-beqz] p. 274 ("`BEQZ` provides 12 bits of target
range instead of the eight bits available in most conditional branches").
`BEQZ`/`BNEZ` are inverses, as are `BGEZ`/`BLTZ`
[Tensilica 2010][^isa-beqz] p. 274, [^isa-bnez] p. 290,
[^isa-bgez] p. 281, [^isa-bltz] p. 286. Range: **-2044 to +2051 bytes**
(`-2048 + 4` to `2047 + 4`).

**Narrow forms, `BEQZ.N` and `BNEZ.N`.** With the Code Density Option on
(it is, `XCHAL_HAVE_DENSITY 1`[^coreisa]), the assembler can pick a
16-bit encoding for these two only. The instruction word is `RI6`: a
6-bit unsigned, zero-extended offset, so the branch can only go **forward**,
by **+4 to +67 bytes** from its own address
[Tensilica 2010][^isa-beqzn] p. 275. There is no narrow `BLTZ.N` or
`BGEZ.N`; only equal-to-zero and not-equal-to-zero get the narrow
encoding. The assembler substitutes the narrow form automatically when the
target is in range and the wide form was not forced with the `_BEQZ`
underscore prefix [Tensilica 2010][^isa-beqz] p. 274.

Measured: a bit test compiled with side effects, so the compiler could not
fold it to a plain `EXTUI` (see §9):

```c
int bit_test(unsigned a) {
    if (a & (1u << 5)) { side_a(); return 1; }
    return 0;
}
```

```
bit_test:
	entry	sp, 32
	movi.n	a8, 0
	bbci	a2, 5, .L5
	call8	side_a
	movi.n	a8, 1
.L5:
	mov.n	a2, a8
	retw.n
```

GCC chose `BBCI` (§5) here rather than an `EXTUI` plus `BEQZ`, because the
mask is a single bit known at compile time.

## 5. Bit-test branches: `BBCI`, `BBSI`, `BBC`, `BBS`

`RRI8` format. `BBCI`/`BBSI` test one bit of a register against a 5-bit
immediate index (0 to 31, split across two word fields), `BBC`/`BBS` test
one bit of a register against an index held in another register
[Tensilica 2010][^isa-bbci] p. 267, [^isa-bbc] p. 266. All four use the
core's bit-numbering convention: little-endian processors (this one) treat
bit 0 as least significant [Tensilica 2010][^isa-bbc] p. 266. `BBCI` is
the inverse of `BBSI`; `BBC` is the inverse of `BBS`
[Tensilica 2010][^isa-bbci] p. 267, [^isa-bbc] p. 266. Offset range is the
same `RRI8` -124 to +131 bytes as §2. `BBCI.L` and `BBSI.L` are assembler
macros over `BBCI`/`BBSI` that force little-endian bit numbering
explicitly; on this little-endian core they assemble identically to the
plain form [Tensilica 2010][^isa-bbcil] p. 268.

## 6. Mask branches: `BALL`, `BANY`, `BNALL`, `BNONE`

`RRI8` format, testing several bits at once against a mask held in a
second register, useful for "any of these flags set" checks without a
separate `AND` [Tensilica 2010][^isa-ball] p. 264. `BALL` branches when
every masked bit is set, `BANY` when at least one is, and their inverses
`BNALL` and `BNONE` do the opposite [Tensilica 2010][^isa-ball] p. 264,
[^isa-bany] p. 265, [^isa-bnall] p. 287, [^isa-bnone] p. 292. `BALL`'s
test is `(NOT as) AND at = 0`; `BANY`'s is `as AND at != 0`
[Tensilica 2010][^isa-ball] p. 264, [^isa-bany] p. 265. Same -124 to +131
byte range as the rest of the `RRI8` family.

## 7. Unconditional jumps: `J` and `JX`

`J` is `CALL` format: a signed 18-bit PC-relative offset, giving a range of
**-131068 to +131075 bytes**
[Tensilica 2010][^isa-j] p. 366. `JX` is `CALLX` format: it jumps to
whatever address sits in a general register, so its reach is the full
32-bit address space [Tensilica 2010][^isa-jx] p. 368. `J.L` is an
assembler macro: it emits a plain `J` when the target is in range, and
falls back to a literal load (`L32R`) into a register followed by `JX` on
that register when it is not [Tensilica 2010][^isa-jl] p. 367.

Measured: GCC's own switch-statement lowering uses exactly that
`L32R` + `JX` pattern once the case values do not simplify to arithmetic
(five distinct `case` labels, each calling a different function so no
single formula covers them):

```c
extern int callee(int);
int small_switch(int a) {
    switch (a) {
        case 0: return callee(1);
        case 1: return callee(17);
        case 2: return callee(93);
        case 3: return callee(4);
        case 4: return callee(255);
        default: return callee(-1);
    }
}
```

```
	.literal_position
	.literal .LC0, .L12        # .LC0 is a literal pool slot holding &.L12
small_switch:
	entry	sp, 32
	bgeui	a2, 5, .L10        # range check: default case
	l32r	a8, .LC0           # a8 = address of the jump table (.L12)
	addx4	a2, a2, a8         # a2 = &table[a] (4 bytes per entry)
	l32i	a8, a2, 0          # a8 = table[a]
	jx	a8                 # jump to the case
	.section	.rodata
.L12:
	.word	.L16
	.word	.L15
	.word	.L14
	.word	.L13
	.word	.L11
	.text
.L16:
	movi.n	a10, 1
	call8	callee
	mov.n	a2, a10
	j	.L9
	...                    # .L15, .L14, .L13, .L11 follow the same shape
.L10:
	movi.n	a10, -1
	call8	callee
	mov.n	a2, a10
.L9:
	retw.n
```

`BGEUI` (§3) does the bounds check for the default case with the encoded
constant 5, `ADDX4` scales the index by 4 without a separate shift, and
the table itself lives in `.rodata`, loaded once with `L32R`
`[measured]`. Contrast this with the arithmetic-only switch GCC produced
in an earlier attempt at the same idea (four small, evenly spaced return
values and no calls): the compiler recognised `100 * (a + 1)` covered
every case and emitted no jump table and no indirect jump at all, only a
`BGEUI` range check plus multiply-by-constant arithmetic (`ADDX4` used
twice, then `SLLI`) `[measured]`. Whether a `switch` becomes a jump table,
a chain of compares, or pure arithmetic is a code-shape decision GCC makes
per switch, not a fixed lowering.

## 8. Calls: `CALL0`/`CALLX0` and the windowed `CALL4`/`CALL8`/`CALL12`

All five call instructions store a return address in `a0` (`CALL0`) or in
`a4`/`a8`/`a12` of the caller's window plus a window-increment tag
(`CALL4`/`CALL8`/`CALL12`), and none of them touch the register window
themselves; the corresponding `ENTRY` instruction at the callee's first
address performs the actual rotation
[Tensilica 2010][^isa-call0] p. 297, [^isa-call4] p. 298. `CALL0` needs no
`Windowed Register Option`; `CALL4`/`CALL8`/`CALL12` do
[Tensilica 2010][^isa-call4] p. 298.
[Register windows and the windowed ABI](../00-foundations/register-windows-and-windowed-abi.md)
covers the overflow and underflow mechanics that a windowed call can
trigger; this page only covers the call instruction's own encoding.

| Instruction | Format | Return via | Requires |
|---|---|---|---|
| `CALL0` | `CALL` | `a0`, plain address | Core Architecture [^isa-call0] p. 297 |
| `CALL4` | `CALL` | `a4` (caller) / `a0` (callee), plus window-increment tag | Windowed Register Option [^isa-call4] p. 298 |
| `CALL8` | `CALL` | `a8` / `a0`, same tag mechanism | Windowed Register Option [^isa-call8] p. 300 |
| `CALL12` | `CALL` | `a12` / `a0`, same tag mechanism | Windowed Register Option [^isa-call12] p. 302 |
| `CALLX0` | `CALLX` | same as `CALL0`, register-indirect target | Core Architecture [^isa-callx0] p. 304 |
| `CALLX4`/`CALLX8`/`CALLX12` | `CALLX` | same as their non-X counterparts, register-indirect target | Windowed Register Option [^isa-callx4] p. 305, [^isa-callx8] p. 307, [^isa-callx12] p. 309 |

All four `CALL`-format instructions (`J`, `CALL0`, `CALL4`, `CALL8`,
`CALL12`) require their target to be 32-bit aligned, which lets the
offset field be interpreted as a word count and shifted left by two before
being added. That gives the call forms four times the reach of `J`'s raw
18-bit field: **-524284 to +524288 bytes**
[Tensilica 2010][^isa-call0] p. 297. `CALLX0`/`CALLX4`/`CALLX8`/`CALLX12`
have no offset field at all; the target comes entirely from a register, so
reach is unlimited [Tensilica 2010][^isa-callx0] p. 304.

`RET`/`RET.N` return from a `CALL0`/`CALLX0` call; `RETW`/`RETW.N` return
from a windowed call and restore the two window-increment bits that the
call packed into the high bits of the return-address register
[Tensilica 2010][^isa-call4] p. 298. Measured across every example on
this page: `xtensa-esp32s3-elf-gcc -O2` always paired an `ENTRY` at
function entry with a `RETW.N`, because the toolchain's default ABI for
ESP32-S3 is the windowed one; `CALL0`-ABI code exists in ESP-IDF (interrupt
vectors, some low-level assembly) but is not what a normal C function
compiles to `[measured]`.

## 9. Cost: what happens on a taken branch

The base ISA has no separate compare instruction: `BEQ` and its relatives
both compare and, in the same instruction, redirect the fetch stream. The
compare itself happens in the E pipeline stage, the same stage that
decides whether the branch is taken
[LX7 pipeline and cost model, §4](../00-foundations/lx7-core-pipeline-and-cost-model.md).
On this chip a taken branch stalls the pipeline for **2 cycles**: the TRM
states that "the instructions at the R and E stages on the pipeline will
be removed, which means the pipeline remains stagnant for 2 cycles"
[Espressif 2026b][^trm-branch] §1.7.3 p. 74. If the branch target crosses
a 32-bit instruction-fetch boundary, one more cycle is added, because a
second fetch is needed before the whole target instruction is available
[Tensilica 2010][^isa-pipeline] §8.4.2 p. 609. A not-taken branch costs
nothing extra: the instructions already in flight are the ones that
execute [Tensilica 2010][^isa-pipeline] §8.4.2 p. 608. Both numbers are
restated in full, with the pipeline stage table they come from, in
[the pipeline and cost model leaf](../00-foundations/lx7-core-pipeline-and-cost-model.md#4-branches).

**There is no branch predictor configured on this chip.** The core
configuration header sets both prediction-related options off:
`XCHAL_HAVE_PREDICTED_BRANCHES` is 0, meaning the optional
`BEQT`/`BEQZT`/`BNET`/`BNEZT` predicted-branch instructions are not built
into this core, and `XCHAL_HAVE_WIDE_BRANCHES` is also 0, meaning the
wide `B*.W18`/`B*.W15` forms are absent too
[ESP-IDF 5.5.1][^coreisa] lines 72-73. Every branch on this chip resolves
in the E stage with the fixed 2-cycle taken cost above; there is no
static-prediction hint to give the assembler and no dynamic predictor to
warm up. `[uncertain]` Whether any other microarchitectural mechanism
(instruction prefetch buffering, for instance) softens this in practice is
not stated in either the ISA manual or the TRM; treat it as the number to
beat with a device measurement, not as a proven floor.

The zero-overhead loop instructions exist specifically to avoid paying
this cost on a loop's own back edge; see
[zero-overhead loops](./zero-overhead-loops.md) for when GCC 14 actually
emits one instead of a compare-and-branch pair.

## 10. Reading generated code for this family

Grep a `.s` file for the mnemonics in this page's tables (`beq`, `bne`,
`blt`, `bge`, `bltu`, `bgeu`, `beqi`, `bnei`, `blti`, `bgei`, `bgeui`,
`bltui`, `bbci`, `bbsi`, `bbc`, `bbs`, `ball`, `bany`, `bnall`, `bnone`,
`beqz`, `bnez`, `bltz`, `bgez`, `beqz.n`, `bnez.n`, `j`, `jx`, `call0`,
`call4`, `call8`, `call12`, `callx0`, `callx4`, `callx8`, `callx12`) to
see which family GCC picked for a given `if`, `switch` or bit test. The
`.n` suffix on a branch, or on a nearby `movi.n`/`addi.n`, is the density
option narrowing a 24-bit instruction to 16 bits; it does not change which
branch family is in use, only its encoding width.

## Open questions

- The exact functional-unit or fetch-buffering behaviour that could reduce
  the 2-cycle taken-branch cost below what the TRM states is not
  documented in either primary source; a `CCOUNT`-based taken-versus-not-taken
  loop measurement (method in
  [timing a kernel with CCOUNT](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md))
  would confirm or refute the 2-cycle figure directly on this chip, which
  this page's author could not run.
- Whether GCC 14.2 ever emits `BALL`/`BANY`/`BNALL`/`BNONE` from ordinary C
  source (as opposed to hand-written asm) was not confirmed; the pattern
  that would trigger it, comparing a register against a mask held in
  another register with a single bitwise-and-and-compare, was not tried
  here.
- The interaction between `-mlongcalls` and the `CALL`-format range limits
  in §8 is not confirmed against a primary source; `-mlongcalls` is known
  to affect `CALL8`-based subroutine calls to functions outside the
  `CALL`-format's own reach, but the exact threshold GCC uses to switch
  was not measured for this page.

## Sources

[^isa-cond-table]: Tensilica, Inc., *Xtensa Instruction Set Architecture
    (ISA) Reference Manual*, issue date 4/2010, release RC-2010.1,
    document PD-09-0801-10-01. Table 3-16, "Conditional Branch
    Instructions", p. 40. Mirror read on 2026-09-06:
    <https://0x04.net/~mwk/doc/xtensa.pdf>
[^isa-b4const]: Same manual, Table 3-17 "Branch Immediate (b4const)
    Encodings" and Table 3-18 "Branch Unsigned Immediate (b4constu)
    Encodings", pp. 41-42.
[^isa-beq]: Same manual, `BEQ` instruction description, Chapter 6, p. 272.
[^isa-beqi]: Same manual, `BEQI`, p. 273.
[^isa-beqz]: Same manual, `BEQZ`, p. 274.
[^isa-beqzn]: Same manual, `BEQZ.N`, p. 275.
[^isa-bge]: Same manual, `BGE`, p. 277.
[^isa-bgei]: Same manual, `BGEI`, p. 278.
[^isa-bgeu]: Same manual, `BGEU`, p. 279.
[^isa-bgeui]: Same manual, `BGEUI`, p. 280.
[^isa-bgez]: Same manual, `BGEZ`, p. 281.
[^isa-blt]: Same manual, `BLT`, p. 282.
[^isa-blti]: Same manual, `BLTI`, p. 283.
[^isa-bltu]: Same manual, `BLTU`, p. 284.
[^isa-bltui]: Same manual, `BLTUI`, p. 285.
[^isa-bltz]: Same manual, `BLTZ`, p. 286.
[^isa-bnall]: Same manual, `BNALL`, p. 287.
[^isa-bne]: Same manual, `BNE`, p. 288.
[^isa-bnei]: Same manual, `BNEI`, p. 289.
[^isa-bnez]: Same manual, `BNEZ`, p. 290.
[^isa-bnezn]: Same manual, `BNEZ.N`, p. 291.
[^isa-bnone]: Same manual, `BNONE`, p. 292.
[^isa-bbc]: Same manual, `BBC`, p. 266.
[^isa-bbci]: Same manual, `BBCI`, p. 267.
[^isa-bbcil]: Same manual, `BBCI.L`, p. 268.
[^isa-ball]: Same manual, `BALL`, p. 264.
[^isa-bany]: Same manual, `BANY`, p. 265.
[^isa-j]: Same manual, `J`, p. 366.
[^isa-jl]: Same manual, `J.L`, p. 367.
[^isa-jx]: Same manual, `JX`, p. 368.
[^isa-call0]: Same manual, `CALL0`, p. 297.
[^isa-call4]: Same manual, `CALL4`, pp. 298-299.
[^isa-call8]: Same manual, `CALL8`, pp. 300-301.
[^isa-call12]: Same manual, `CALL12`, pp. 302-303.
[^isa-callx0]: Same manual, `CALLX0`, p. 304.
[^isa-callx4]: Same manual, `CALLX4`, pp. 305-306.
[^isa-callx8]: Same manual, `CALLX8`, pp. 307-308.
[^isa-callx12]: Same manual, `CALLX12`, pp. 309-310.
[^isa-pipeline]: Same manual, Sections 8.4.1-8.4.2, "Processor
    Performance Terminology and Modeling" and "Xtensa Processor Family",
    Table 8-247 and Figures 8-54 to 8-56, pp. 605-609.
[^trm-branch]: Espressif Systems, *ESP32-S3 Technical Reference Manual*,
    version 1.8, PDF dated 2026-03-04. Section 1.7.3, p. 74.
    <https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf>
[^coreisa]: ESP-IDF 5.5.1 (PlatformIO package `framework-espidf`
    3.50501), `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`,
    lines 55 (`XCHAL_HAVE_DENSITY`), 72 (`XCHAL_HAVE_WIDE_BRANCHES`), 73
    (`XCHAL_HAVE_PREDICTED_BRANCHES`). Read 2026-09-06.

`[measured]` entries: `xtensa-esp32s3-elf-gcc` 14.2.0 (crosstool-NG
esp-14.2.0_20241119), `-O2 -mlongcalls -S`, run 2026-09-06 on three small
translation units covering an `if` on a small non-zero constant with call
side effects, a single-bit test with a call side effect, an `if` with no
side effects returning two distinct constants, and a five-case `switch`
with a distinct function call per case versus a four-case `switch` with
arithmetically related return values and no calls. Full command:
`xtensa-esp32s3-elf-gcc -O2 -mlongcalls -S <file>.c -o <file>.s`.
