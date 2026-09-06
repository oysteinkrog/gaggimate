---
title: Scalar Arithmetic, Shifts, and Bit Tricks
id: 01-scalar-isa/scalar-arithmetic-shifts-and-bit-tricks
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, scalar-isa, instruction-encoding, shifts, branches]
confidence: high
---

# Scalar arithmetic, shifts, and bit tricks

This page lists the scalar Xtensa instructions a kernel writer on the LX7 core
reaches for most often: loads and stores, address arithmetic, shifts,
sign extension and clamping, min/max, conditional moves, multiply and divide,
and branches. For each group it gives the exact encoding form, the operand
ranges, and a short use note. A closing section lists small idioms that come
up when hand-tuning a hot loop.

Every semantic claim cites the Tensilica *Xtensa Instruction Set Architecture
(ISA) Reference Manual* (Issue Date 4/2010, "For All Xtensa Processor Cores"),
by section number for architectural options and by mnemonic for Chapter 6
instruction descriptions, plus a page number where the source text made the
page unambiguous[^1]. Assembler behavior (narrow-instruction selection, the
literal pool) cites the GNU binutils `as` manual's Xtensa chapter[^2] and the
GCC Xtensa options page[^3], and is cross-checked by assembling short test
programs with the local `xtensa-esp32s3-elf-as`, crosstool-NG
`esp-14.2.0_20241119`, GNU assembler 2.43.1[^4]. Cycle costs are out of
scope here: Cadence does not publish per-instruction latencies for Xtensa
cores, and any specific number needs a device or QEMU measurement[^5]
[uncertain].

## Which instructions exist on this core

The ESP32-S3's LX7 configuration enables the Code Density Option, the
Boolean Option, the 16-bit and 32-bit Integer Multiply Options (including the
Mul32High sub-option for `mulsh`/`muluh`), the 32-bit Integer Divide Option,
and the Miscellaneous Operations Option (`min`/`max`/`minu`/`maxu`, `nsa`/
`nsau`, `sext`, `clamps`). Each of those is an `XCHAL_HAVE_*` macro set to 1
in this chip's `core-isa.h`[^6], and all of the mnemonics in this page
assembled without error against the local toolchain[^4]; none needed a
workaround.

## Loads and stores

The core has no post-increment addressing mode and no sign-extending byte
load: an 8-bit signed value must be loaded with `l8ui` and then sign-extended
separately (`sext`, or a shift pair). Immediate offsets are always
zero-extended and, for the 16- and 32-bit forms, scaled by the access width,
so the assembler syntax gives a byte offset but the encoded field holds that
offset divided by 2 or 4.

| Instruction | Format | Offset range (assembler syntax) | Notes |
|---|---|---|---|
| `l8ui at, as, 0..255` | RRI8 | 0 to 255, unscaled | Zero-extends the byte. No sign-extending byte load exists[^1] (Core Architecture, p. 369). |
| `l16ui at, as, 0..510` | RRI8 | 0 to 510, multiples of 2 | Zero-extends the halfword[^1] (p. 372). |
| `l16si at, as, 0..510` | RRI8 | 0 to 510, multiples of 2 | Sign-extends the halfword[^1] (p. 370). Without the Unaligned Exception Option the low address bit is ignored rather than faulting. |
| `l32i at, as, 0..1020` | RRI8 | 0 to 1020, multiples of 4 | One of the few load forms that can also read instruction RAM/ROM[^1]. |
| `l32r at, label` | RI16 | -262141 to -4 bytes from the `l32r` instruction | PC-relative load from the literal pool; see below[^1] (p. 382). |
| `s8i at, as, 0..255` | RRI8 | 0 to 255, unscaled | [^1] (p. 504). |
| `s16i at, as, 0..510` | RRI8 | 0 to 510, multiples of 2 | [^1] (p. 505). |
| `s32i at, as, 0..1020` | RRI8 | 0 to 1020, multiples of 4 | [^1] (p. 510). |

`l32r`'s range is asymmetric and PC-relative rather than register-relative:
the target address is `(PC + 3)` with its low two bits cleared, plus a
16-bit one-extended, left-shifted-by-2 immediate, so the literal must sit no
more than roughly 256 KiB behind the instruction and never ahead of it[^1]
(p. 382). The immediate is not an offset in the assembler syntax; the
operand written in the source is the address of the literal, and the
assembler computes the encoded delta itself[^1].

Every offset above is checked at assemble time: an out-of-range immediate on
any of these is a hard assembler error, not a silent truncation, because the
field width is fixed by the instruction format.

## Address arithmetic

| Instruction | Format | Operation | Notes |
|---|---|---|---|
| `addx2 ar, as, at` | RRR | `ar = (as << 1) + at` | Frequently used for address calculation and to multiply by small constants[^1] (p. 254). |
| `addx4 ar, as, at` | RRR | `ar = (as << 2) + at` | [^1] (p. 255). |
| `addx8 ar, as, at` | RRR | `ar = (as << 3) + at` | [^1] (p. 256). |
| `subx2 ar, as, at` | RRR | `ar = (as << 1) - at` | Subtracting variant of `addx2`[^1]. |
| `subx4 ar, as, at` | RRR | `ar = (as << 2) - at` | [^1]. |
| `subx8 ar, as, at` | RRR | `ar = (as << 3) - at` | [^1]. |
| `addi at, as, -128..127` | RRI8 | `at = as + imm8` | 24-bit wide form; the assembler may substitute `addi.n` when the immediate and the Code Density Option allow it, or force the wide form with a leading underscore (`_addi`)[^1] (p. 251). |
| `addmi at, as, -32768..32512` | RRI8 | `at = as + (imm8 << 8)` | Immediate must be a multiple of 256; extends the range of constant addition, typically to widen a load/store base[^1] (p. 253). |

None of these three `addx*`/`subx*` pairs detect overflow; they exist purely
for cheap scaled address computation, since the core has no separate
"multiply by small constant" instruction outside the 16-/32-bit multiply
options.

## Shifts

Two shift families exist: immediate-encoded shifts with a fixed amount, and
SAR-based shifts whose amount comes from the 6-bit Shift Amount Register.
`extui` is technically a shift-then-mask rather than a plain shift, and is
the standard way to pull a bit field out of a word.

| Instruction | Format | Range | Notes |
|---|---|---|---|
| `slli ar, as, 1..31` | RRR | 1 to 31 | Left shift by a constant. The `sa` field is encoded as `32 - shift`, so a shift amount of 0 has an undefined result; the assembler converts `slli ar, as, 0` into a register move instead, emitting `mov.n` with the Code Density Option on. Prefixing `_slli` makes a zero shift an error[^1] (p. 525), confirmed against the assembler[^4]. |
| `srli ar, at, 0..15` | RRR | 0 to 15 only | There is no `srli` for shifts of 16 or above; the manual says outright "EXTUI replaces these shifts". The assembler substitutes `extui` automatically (`srli a2, a3, 16` assembles as `extui a2, a3, 16, 16`) and errors if prefixed `_srli`[^1] (p. 530), confirmed against the assembler[^4]. |
| `srai ar, at, 0..31` | RRR | 0 to 31 | Arithmetic (sign-preserving) right shift[^1] (p. 527). |
| `extui ar, at, shiftimm, maskimm` | RRR | `shiftimm` 0..31, `maskimm` 1..16 | Shifts `at` right by `shiftimm`, then masks to the low `maskimm` bits. `shiftimm + maskimm > 31` is undefined[^1] (p. 344, Core Architecture). |

The SAR-based family lets a shift amount be computed once and reused, or
lets two registers be combined into one wider shift (a funnel shift):

| Instruction | Format | Effect on SAR / result | Notes |
|---|---|---|---|
| `ssl as` | RRR | `SAR = 32 - as[4:0]` | Sets up SAR for a left shift. Used before `sll` or a left funnel shift on `src`[^1] (p. 538). |
| `ssr as` | RRR | `SAR = as[4:0]` | Sets up SAR for a right shift (`srl`, `sra`, `src`)[^1] (p. 539). |
| `ssai 0..31` | RRR | `SAR = imm` | Sets SAR to a constant; mainly useful ahead of `src`, since `slli`/`srli`/`srai` already have immediate forms[^1] (p. 533). |
| `ssa8l as` | RRR | `SAR = (as[1:0] << 3)` | Sets SAR for a little-endian byte-multiple right shift, ahead of `src`, to extract a 32-bit value from an unaligned byte address[^1] (p. 532). |
| `ssa8b as` | RRR | `SAR = 32 - (as[1:0] << 3)` | Big-endian counterpart of `ssa8l`, for a left shift by a byte multiple[^1] (p. 531). |
| `sll ar, as` | RRR | `ar = (as << (32-SAR))`, i.e. left shift by the amount `ssl` set | Implemented as a right funnel shift on `{as, 0}` internally; undefined if SAR > 32[^1] (p. 524). |
| `srl ar, at` | RRR | `ar = at >> SAR` (logical) | [^1] (p. 529). |
| `sra ar, at` | RRR | `ar = at >> SAR` (arithmetic) | [^1] (p. 526). |
| `src ar, as, at` | RRR | `ar = ({as, at} >> SAR)[31:0]` | The funnel shift: a 64-bit right shift of the concatenation of two registers, keeping the low 32 result bits. Left funnel shifts are done by swapping `as`/`at` and using `ssl` or `ssa8b` to load `32 - shift` into SAR[^1] (p. 528). |

## Sign extension, clamping, and min/max

All four belong to the Miscellaneous Operations Option, which is enabled
per-instruction-group on the processor configuration; this core has all
four groups (`InstructionSEXT`, `InstructionCLAMPS`, `InstructionMINMAX`,
`InstructionNSA`) turned on[^1] (§4.3.8 on p. 62, Table 4-39 on p. 62 and
Table 4-40 on p. 63; the header macros are `XCHAL_HAVE_SEXT`,
`XCHAL_HAVE_CLAMPS`, `XCHAL_HAVE_MINMAX` and `XCHAL_HAVE_NSA`, all 1 on
this core[^6]).

| Instruction | Format | Operation |
|---|---|---|
| `sext ar, as, 7..22` | RRR | Sign-extends `as` from the named bit upward: `ar = sign-extend(as[imm:0])`. The assembler takes the bit position, 7 to 22, and encodes it in the 4-bit `t` field as 0 to 15[^1] (p. 518). Anything outside 7 to 22 is an assemble-time error, `0` and `23` included[^4]. |
| `clamps ar, as, 7..22` | RRR | Tests whether `as` fits as a signed value of `imm+1` bits and, if not, writes the largest value of `imm+1` bits with the same sign. The manual states the function as `y = min(max(x, -2^imm), 2^imm - 1)`. Same immediate encoding and same 7 to 22 range as `sext`[^1] (p. 312), confirmed against the assembler[^4]. |
| `min ar, as, at` / `max ar, as, at` | RRR | Signed minimum/maximum of two registers[^1] (pp. 410, 407). |
| `minu ar, as, at` / `maxu ar, as, at` | RRR | Unsigned minimum/maximum[^1] (pp. 411, 408). |
| `nsa ar, as` | RRR | Number of redundant sign bits: how many leading bits (not counting the sign bit) equal the sign bit, or 31 if `as` is 0 or -1. Usable as a left-shift amount before the value overflows 32 bits[^1] (p. 461). |
| `nsau ar, as` | RRR | Number of leading zero bits, result in the range 0 to 32, or 32 if `as` is 0. The manual notes that shifting `as` left by the `nsau` result with `ssl`/`sll` yields the smallest value with bit 31 set, unless `as` is 0[^1] (p. 462). |

`clamps` is documented in the manual as pairing with the MAC16 Option, to
clamp an accumulator result to 16 bits before storing it to memory, though
nothing about `clamps` itself requires MAC16 to be present[^1] (§4.3.7.2
"Use With CLAMPS Instruction", p. 62). The `clamps` description itself says
it "may be used in conjunction with instructions such as ADD, SUB, MUL16S,
and so forth to implement saturating arithmetic"[^1] (p. 312).

## Conditional moves

Four are core architecture (no option gate); two more require the Boolean
Option, which this core has.

| Instruction | Format | Condition | Option |
|---|---|---|---|
| `moveqz ar, as, at` | RRR | Move `as` to `ar` if `at == 0` | Core Architecture[^1] (Table 4-26, pp. 51-53; description p. 415). |
| `movnez ar, as, at` | RRR | Move `as` to `ar` if `at != 0` | Core Architecture[^1] (p. 425). |
| `movltz ar, as, at` | RRR | Move `as` to `ar` if `at < 0` (signed) | Core Architecture[^1] (p. 423). |
| `movgez ar, as, at` | RRR | Move `as` to `ar` if `at >= 0` (signed) | Core Architecture[^1] (p. 419). |
| `movt ar, as, bt` | RRR | Move `as` to `ar` if Boolean register `bt` is true | Boolean Option[^1] (§4.3.10 on p. 65; description p. 428). |
| `movf ar, as, bt` | RRR | Move `as` to `ar` if Boolean register `bt` is false | Boolean Option[^1] (§4.3.10 on p. 65; description p. 417). |

Every conditional move is a full no-op on the untaken side: the destination
register is left completely unchanged, not merged bit-by-bit, so a
conditional move can replace a short forward branch without needing to
recompute the "keep as-is" value.

## Multiply and divide

Three separate, independently configurable options; a given core can have
any subset. This one has all three, plus MAC16, though MAC16's 72
multiply-accumulate instructions are out of scope for this page.

| Instruction | Format | Option | Operation |
|---|---|---|---|
| `mul16s ar, as, at` | RRR | 16-bit Integer Multiply[^1] (§4.3.4 on p. 57; description p. 436) | Signed 16x16 multiply of the low 16 bits of `as` and `at`, full 32-bit product. |
| `mul16u ar, as, at` | RRR | 16-bit Integer Multiply[^1] (p. 437) | Unsigned counterpart. |
| `mull ar, as, at` | RRR | 32-bit Integer Multiply[^1] (§4.3.5 on p. 58; description p. 450) | Low 32 bits of a 32x32 product. Valid for both signed and unsigned inputs, since the low half does not depend on sign. |
| `mulsh ar, as, at` | RRR | 32-bit Integer Multiply, Mul32High sub-option[^1] (p. 455) | High 32 bits of a signed 32x32 product. |
| `muluh ar, as, at` | RRR | 32-bit Integer Multiply, Mul32High sub-option[^1] (p. 456) | High 32 bits of an unsigned 32x32 product. |
| `quos ar, as, at` | RRR | 32-bit Integer Divide[^1] (§4.3.6 on p. 59; description p. 471) | Signed quotient; truncates toward zero (the manual specifies the quotient times the divisor stays smaller in magnitude than the dividend). Raises Integer Divide by Zero if `at` is 0. |
| `quou ar, as, at` | RRR | 32-bit Integer Divide[^1] (p. 472) | Unsigned quotient. |
| `rems ar, as, at` | RRR | 32-bit Integer Divide[^1] (p. 475) | Signed remainder. |
| `remu ar, as, at` | RRR | 32-bit Integer Divide[^1] (p. 476) | Unsigned remainder. |

The manual gives semantics but never a cycle count or a pipelining model for
any of these: the ISA reference explicitly leaves algorithm and speed to the
implementation ("various algorithms may be used... some hardware
implementations may be slower than the software implementations for some
operand values")[^1] (§4.3.5, §4.3.6). Cadence has not published per-core
LX7 latency figures, and the only way to get one is a device or QEMU
measurement[^5] [uncertain]. Do not treat `mull` or `quos` as fixed-cost
instructions when budgeting a hot loop without such a measurement.

## Branches

Two branch encodings cover the instructions in scope here: RRI8 (an 8-bit
signed immediate) for two-register and zero-comparison-with-mask branches,
and BRI12 (a 12-bit signed immediate) for the four plain zero-comparison
branches, which get extra range because they are common enough to be worth
the wider immediate.

For an RRI8 branch the target is `PC + sign_extend(imm8) + 4`, so with
`imm8` ranging -128..127 the reachable target is between `PC - 124` and
`PC + 131` bytes, not a symmetric window around the branch[^1] (p. 272,
`beq`, as a representative case). For a BRI12 branch the target is
`PC + sign_extend(imm12) + 4`, giving roughly `PC - 2044` to `PC + 2051`
bytes[^1] (p. 274, `beqz`).

| Instruction | Format | Condition |
|---|---|---|
| `beqz as, label` | BRI12 | `as == 0` |
| `bnez as, label` | BRI12 | `as != 0` |
| `bltz as, label` | BRI12 | `as < 0` (signed) |
| `bgez as, label` | BRI12 | `as >= 0` (signed) |
| `beq as, at, label` | RRI8 | `as == at` |
| `bne as, at, label` | RRI8 | `as != at` |
| `blt as, at, label` | RRI8 | `as < at` (signed) |
| `bge as, at, label` | RRI8 | `as >= at` (signed) |
| `bltu as, at, label` | RRI8 | `as < at` (unsigned) |
| `bgeu as, at, label` | RRI8 | `as >= at` (unsigned) |
| `bbci as, 0..31, label` | RRI8 | bit `imm` of `as` is clear |
| `bbsi as, 0..31, label` | RRI8 | bit `imm` of `as` is set |
| `ball as, at, label` | RRI8 | all bits set in mask `at` are also set in `as` |
| `bnall as, at, label` | RRI8 | some bit set in mask `at` is clear in `as` (inverse of `ball`) |
| `bany as, at, label` | RRI8 | any bit set in mask `at` is set in `as` |
| `bnone as, at, label` | RRI8 | no bit set in mask `at` is set in `as` (inverse of `bany`) |

All of these are Core Architecture, no option gate[^1] (Table 4-26, pp.
51-53; individual descriptions pp. 263-291). Every instruction in this table
assembled and produced the expected bit pattern against the local
toolchain[^4]. When a branch target is out of range the assembler
substitutes an equivalent instruction sequence rather than failing;
prefixing the mnemonic with an underscore disables that and makes an
out-of-range target a hard assemble-time error instead of a silent
multi-instruction expansion[^1].

## Narrow (`.n`) encodings and the literal pool

The Code Density Option adds a second, 16-bit-wide encoding for a handful of
very common instructions, each with a correspondingly smaller immediate
range[^1] (§4.3.1 on p. 53, Table 4-27 on p. 54):

| 16-bit form | Format | Range vs. the 24-bit form |
|---|---|---|
| `add.n ar, as, at` | RRRN | Same as `add`, no immediate to shrink. |
| `addi.n ar, as, imm` | RRRN | Immediate is -1 or 1..15 (0 is not encodable; -1 is encoded as the field value 0)[^1] (p. 252). `addi.n a2, a3, 0` is an assemble-time error[^4]. |
| `l32i.n at, as, 0..60` | RRRN | 4-bit offset, multiples of 4, so 0 to 60 instead of `l32i`'s 0 to 1020[^1]. |
| `s32i.n at, as, 0..60` | RRRN | Same restriction as `l32i.n`[^1]. |
| `mov.n ar, as` | RRRN | Plain register move, no immediate. |
| `movi.n ar, imm` | RI7 | Immediate -32..95, versus `movi`'s -2048..2047[^1] (Table 4-27, p. 54; `movi` p. 421, `movi.n` p. 422). Both ends confirmed: -32 and 95 assemble, -33 and 96 do not[^4]. `movi` with a wider immediate becomes a literal load[^1]. |
| `beqz.n as, label` | RI6 | 6-bit **unsigned** offset, forward only: target is `PC + imm6 + 4`, so 4 to 67 bytes forward, and only forward[^1] (p. 275). |
| `bnez.n as, label` | RI6 | Same restriction as `beqz.n`[^1] (p. 291). |
| `ret.n` | RRRN | Same as `ret`, 16-bit encoding. |

The assembler, not the programmer, decides which form to emit. Writing the
24-bit mnemonic (`l32i`, `s32i`, `addi`, `beqz`, `bnez`) with an immediate
and a branch target that both fit the narrow form's tighter range causes the
assembler to silently substitute the 16-bit encoding; a leading underscore
on the mnemonic (`_l32i`, `_s32i`, `_addi`, `_beqz`, `_bnez`) disables the
substitution and forces the wide form, erroring instead if the wide form
itself cannot reach[^1]. This was confirmed directly: assembling
`l32i a2, a3, 40` produced the `l32i.n` opcode bytes because an offset of 40
fits the narrow form's 0-60 range, and `_s32i a2, a3, 40` / `_addi a2, a3, 5`
in the same file produced the wide `s32i`/`addi` opcode bytes instead of the
narrow ones[^4]. A backward `bnez` to an already-defined label stayed in the
24-bit form in the same test, consistent with the manual's "forward only"
restriction on `bnez.n`[^4].

The literal pool backs `l32r`. By default (`--no-text-section-literals`,
`--no-auto-litpools`) the assembler places literals in a separate `.literal`
section, out of the way of the instruction stream[^2]. `--text-section-literals`
interleaves literals directly into `.text`, near the `l32r` instructions that
reference them, which matters once a function's code plus its literals would
otherwise exceed `l32r`'s roughly 256 KiB reach[^2][^3]. `--auto-litpools`
goes further: it lets the assembler place literal pools automatically at
multiple points within a large function using `MOVI`-based sequences, rather
than requiring the programmer to place an explicit `.literal_position`[^3].
Both were exercised directly: assembling a `movi a2, 0x12345` (too wide for
`movi`'s signed 12-bit immediate) with `--text-section-literals` produced an
`l32r` whose literal landed in `.text` immediately before the referencing
function, matching the documented behavior[^4]. GCC exposes the same two
choices as `-mtext-section-literals`/`-mno-text-section-literals` and
`-mauto-litpools`/`-mno-auto-litpools`, defaulting to neither (a separate
literal section, one pool per compilation unit)[^3]. Code density itself is
not a command-line flag on either the assembler or GCC: it is a fixed
property of the core configuration, and the assembler's automatic narrow-form
substitution is unconditional whenever the target supports it[^2][^3].

## Idioms

- **Pack two 16-bit values into one word.** `slli`/`extui` plus `or`: shift
  the high half into position with `slli ar, ah, 16` (or leave it if it is
  already the high half of a 32-bit load), mask the low half with
  `extui ar2, al, 0, 16` if it might carry garbage above bit 15, then
  `or ar, ar, ar2`. A single `s32i` stores both halves in one instruction,
  which is the point: a byte- or halfword-oriented buffer wants one store,
  not two.
- **Clamp with `min`/`max` instead of branches.** `max a, x, lo` then
  `min a, a, hi` clamps `x` into `[lo, hi]` with two straight-line
  instructions and no branch misprediction risk, versus a `blt`/`bge` pair
  that has to guess.
- **Replace a variable divide with a reciprocal multiply-high.** When the
  divisor is a compile-time constant, `mulsh`/`muluh` against a precomputed
  fixed-point reciprocal (plus a correction shift) avoids `quos`/`quou`
  entirely; this is the same transformation GCC itself applies for constant
  divisors, and stays available by hand for a divisor that is only a
  runtime constant (invariant across a loop, not a compile-time literal).
- **Use `extui` for field extraction instead of shift-then-mask as two
  instructions.** `extui ar, at, shift, width` is one instruction and one
  cycle slot where a naive `srli`+`andi` sequence is two; it also covers the
  case where the mask is wider than what an immediate `andi`-style operation
  could encode directly (`andi` here loosely refers to what other ISAs
  provide; Xtensa's own equivalent is exactly `extui`).
- **Prefer unsigned-compare instructions over sign-fixing a subtraction.**
  `bltu`/`bgeu` (and `minu`/`maxu`) test the unsigned relation directly; a
  common wrong shortcut is computing `a - b` and branching on the sign,
  which breaks for operands that differ across the signed/unsigned wrap
  boundary. When the two things being compared are addresses or unsigned
  counters, reach for the `u` forms rather than reasoning about subtraction
  overflow.
- **Use the narrow forms on purpose, not by accident.** Because the
  assembler substitutes `.n` forms automatically, keeping loop-carried
  offsets and immediates inside the narrow ranges (0-60 for `l32i.n`/
  `s32i.n`, -1 or 1-15 for `addi.n`) is a free code-size win with no
  semantic change; it costs nothing to check before assuming a hot loop's
  code size, and the underscore prefix is there for the rare case where the
  wide form's larger reach or fixed size is required (for example, patchable
  code, or a location whose address must stay fixed across a rebuild).

## Sources consulted

- Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference Manual*,
  Issue Date 4/2010, "For All Xtensa Processor Cores". Public mirror used
  for page citations: <https://0x04.net/~mwk/doc/xtensa.pdf> (662 pages;
  page numbers above match the manual's own printed pagination, read from
  the page footers in the extracted text).
- Espressif Systems, *Overview of Xtensa ISA*, Version 0021604, 2021-02-17:
  <https://dl.espressif.com/github_assets/espressif/xtensa-isa-doc/releases/download/latest/Xtensa.pdf>.
  Consulted for the instruction-format catalogue (RRR, RRI8, BRI12, RI16,
  RRRN, RI6, RI7); superseded by the full manual for instruction semantics
  where the two overlap.
- GNU Binutils, `as` manual, "Xtensa Options":
  <https://sourceware.org/binutils/docs/as/Xtensa-Options.html>, and GCC
  documentation, "Xtensa Options":
  <https://gcc.gnu.org/onlinedocs/gcc/Xtensa-Options.html> (both fetched
  2026-09-06; neither page is itself dated, so treat both as documenting
  whatever release sourceware.org and gcc.gnu.org currently serve).
- `esp32.com` forum thread on integer division performance, consulted only
  to confirm that Cadence does not publish per-instruction Xtensa latency
  figures; not cited for any semantic or numeric claim.

Two neighbouring leaves carry what this page leaves out: which of these
options the ESP32-S3 was actually configured with is in
[the configured-options leaf](./core-isa-and-configured-options.md), and the
`LOOP` family and its restrictions are in
[the zero-overhead loops leaf](./zero-overhead-loops.md). Floating-point
scalar instructions are in
[the floating-point leaf](./floating-point-option-on-lx7.md).

[^1]: Tensilica, Inc., *Xtensa Instruction Set Architecture (ISA) Reference Manual*, Issue Date 4/2010, "For All Xtensa Processor Cores". Section and page numbers given inline above; public mirror <https://0x04.net/~mwk/doc/xtensa.pdf>.
[^2]: GNU Binutils, `as` manual, "Xtensa Options" and neighboring Xtensa sections, <https://sourceware.org/binutils/docs/as/Xtensa-Options.html>, fetched 2026-09-06.
[^3]: GCC online documentation, "Xtensa Options", version 14.2.0, <https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Xtensa-Options.html>, fetched 2026-09-06. The version-pinned page lists eleven distinct options: `-mconst16`, `-mfused-madd`, `-mserialize-volatile`, `-mforce-no-pic`, `-mtext-section-literals`, `-mauto-litpools`, `-mtarget-align`, `-mlongcalls`, `-mabi=`, `-mextra-l32r-costs=` and `-mstrict-align`.
[^4]: [measured] 2026-09-06, `xtensa-esp32s3-elf-as` / `xtensa-esp32s3-elf-objdump`, crosstool-NG `esp-14.2.0_20241119`, GNU assembler 2.43.1, one instruction per source file, disassembled back. Accepted and rejected immediates, each checked at both ends: `sext` and `clamps` accept 7 and 22 and reject 0, 6, 23 and 24; `movi.n` accepts -32 and 95 and rejects -33 and 96; `addi.n` accepts -1 and 15 and rejects 0. Substitutions observed with transforms on: `slli a2, a3, 0` becomes `mov.n a2, a3`; `srli a2, a3, 16` becomes `extui a2, a3, 16, 16`; `l32i a2, a3, 40` becomes `l32i.n`; `addi a2, a3, 5` becomes `addi.n`; a forward `beqz` becomes `beqz.n` while a backward `bnez` stays in the 24-bit form. With `_` prefixes (`_slli`, `_srli`, `_s32i`, `_addi`) the substitutions do not happen and the out-of-range cases become errors. Test sources not retained in this repository.
[^6]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`, the `XCHAL_HAVE_*` option macros. The full table for this core is in [the configured-options leaf](./core-isa-and-configured-options.md).
[^5]: Tensilica ISA Reference Manual, §4.3.5 and §4.3.6 (multiply/divide algorithm and speed left to the implementation)[^1]; corroborated by an esp32.com forum thread noting Cadence does not publish Xtensa per-instruction cycle counts, consulted for that observation only, not as a semantic source.
