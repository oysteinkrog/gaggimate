---
title: "01-scalar-isa: bucket index"
id: 01-scalar-isa/readme
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, scalar-isa, core-isa, loop, floating-point, index]
confidence: high
---

# 01-scalar-isa: bucket index

What the ESP32-S3's Xtensa LX7 core can do without the vector unit: which
ISA options Espressif configured in, the zero-overhead loop, the scalar
instruction forms and their immediate ranges, and the floating-point
option and what it lacks.

Two rules govern reading anything here. First, the ISA manual describes
the architecture and `core-isa.h` says which parts of it this chip got;
you need both, and the header wins on the question of what exists.
Second, when the two disagree with the assembler, the assembler decides,
because it is the thing that will accept or reject your code.

## Leaves

| File | Topic | Key claims |
|---|---|---|
| [core-isa-and-configured-options.md](./core-isa-and-configured-options.md) | The `XCHAL_HAVE_*` option table for the ESP32-S3 (LX7) beside the original ESP32 (LX6) | Both chips have Density, Loop (256-byte buffer), NSA, MIN/MAX, SEXT, CLAMPS, MUL16, MUL32, MUL32_HIGH, DIV32, windowed registers with 64 physical `a` registers, Booleans, MAC16, S32C1I, THREADPTR, L32R and ADDX; neither has CONST16, absolute literals, wide branches or predicted branches. Both have a single-precision FPU and neither has double precision. The S3 differs from the ESP32 in data width (16 versus 4 bytes), d-side pipeline delay (1 versus 2), maximum instruction length (4 versus 3 bytes) and d-cache line size (16 versus 4). The cache size and way fields read 0 and 1 on both chips and are not the real geometry: that is an ESP-IDF build choice. The PIE vector unit is coprocessor 3, named `cop_ai` in `tie.h`, and the string "PIE" appears in neither header. |
| [zero-overhead-loops.md](./zero-overhead-loops.md) | `LOOP`, `LOOPNEZ`, `LOOPGTZ`, their restrictions, and when GCC 14 emits one | The body is at most 256 bytes because `LEND` is `PC + 4 + imm8` and the body starts at `PC + 3`. Loops cannot nest, the last body instruction must not be a call, `ISYNC`, `WAITI` or `RSR.LCOUNT`, a taken branch there leaves `LCOUNT` undefined, and loopback is disabled while `PS.EXCM` is set. `LBEG`, `LEND` and `LCOUNT` are saved and restored by the ESP-IDF context switch, so preemption is safe. GCC 14 has no `-mloops` switch: `TARGET_LOOPS` is `XCHAL_HAVE_LOOPS`, the patterns require `optimize`, so `-O0` never emits one, and the only output template is `loop\t%0, %l1_LEND`, so `loopnez` and `loopgtz` are never emitted. Any inline asm in the body kills the loop through `hwloop_optimize`'s `has_asm` test. An oversize body is silently relaxed into a nine-instruction entry sequence with an `L32R` and an `ISYNC` unless the mnemonic carries a leading underscore. |
| [scalar-arithmetic-shifts-and-bit-tricks.md](./scalar-arithmetic-shifts-and-bit-tricks.md) | Loads, stores, address arithmetic, shifts, sign extension, clamping, min/max, conditional moves, multiply, divide, branches, narrow encodings, and hot-loop idioms | Load and store offsets are zero-extended and scaled by the access width, so `l32i` reaches 0 to 1020 in steps of 4 and `l16ui` 0 to 510 in steps of 2. There is no post-increment addressing and no sign-extending byte load. `l32r` reaches -262141 to -4 bytes and never forward. `srli` only covers shifts of 0 to 15 and the assembler substitutes `extui` above that; `slli` with a zero shift becomes a register move. `sext` and `clamps` both take a bit position of 7 to 22, encoded in a 4-bit field. RRI8 branches reach `PC - 124` to `PC + 131`, BRI12 branches roughly `PC - 2044` to `PC + 2051`, and `beqz.n`/`bnez.n` only forward. The assembler picks narrow forms on its own and a leading underscore forces the wide one. No per-instruction cycle counts: Cadence does not publish them and the manual leaves multiply and divide speed to the implementation. |
| [floating-point-option-on-lx7.md](./floating-point-option-on-lx7.md) | The single-precision FP coprocessor, its registers, and what GCC 14.2 does with it | 16 `f` registers of 32 bits plus FCR (user 232) and FSR (user 233); FCR holds the rounding mode and five exception-enable bits, FSR the five matching flags, and the manual says current implementations set neither. There is no `div.s` and no `sqrt.s`: only the refinement seeds `div0.s`, `recip0.s`, `sqrt0.s`, `rsqrt0.s` with `nexp01.s` and `maddn.s`. GCC 14.2 compiles `a / b` to `call8 __divsf3` and `sqrtf` to a call, so the seeds are only reachable from inline assembly. It does emit `madd.s` for plain `a * b + c` by default, undone by `-ffp-contract=off` or `-mno-fused-madd`. A float compare writes a boolean register and then needs a `movf`/`movt` or a `bf`/`bt`. `fmaxf(a, b)` is a library call while `a > b ? a : b` is not. The conversion instructions carry a free power-of-two scale immediate that GCC never reaches from ordinary arithmetic. `float` values live in `a` registers until the instruction before they are used. Only level 1 interrupt handlers may touch `float`, and only with `CONFIG_FREERTOS_FPU_IN_ISR`. |
| [branches-jumps-and-control-flow-costs.md](./branches-jumps-and-control-flow-costs.md) | Every branch, jump and call form with its encoding and reach, the taken-branch cost, and what GCC 14 emits for an `if`, a bit test and a `switch` | RRI8 branches reach -124 to +131 bytes, BRI12 compare-to-zero forms -2044 to +2051, the narrow `beqz.n`/`bnez.n` +4 to +67 forward only, `call0` to `call12` about half a megabyte either way. The B4CONST and B4CONSTU tables are reproduced. A taken branch costs 2 cycles (TRM 1.7.3) and there is no branch predictor (`XCHAL_HAVE_PREDICTED_BRANCHES` 0). GCC uses `bnei`, `bbci` and `movnez` directly and builds an `l32r`/`addx4`/`l32i`/`jx` jump table only for dissimilar case bodies. |
| [mac16-boolean-and-other-configured-options.md](./mac16-boolean-and-other-configured-options.md) | Per-option detail for MAC16, Booleans, MUL32, DIV32, NSA, MINMAX, SEXT, CLAMPS, S32C1I and RSIL, each with what GCC 14 emits | GCC emits `mull`, `mulsh`, `muluh`, `quos`, `quou`, `rems`, `remu`, `nsau`, `nsa`, `min`, `max`, `sext` and `s32c1i` from plain C. It emits MAC16 (`mula.aa.ll` with `wsr.acclo`/`rsr.acclo`) for one shape only, a 16-bit times 16-bit accumulate into 32 bits at `-O2` or `-Os`. It never emits `clamps`: its own `__XCHAL_HAVE_CLAMPS` is 0 although `core-isa.h` says 1, and the assembler accepts the mnemonic. The ISA manual warns that `rsil` and `rsr` can take more than one cycle; no LX7 number exists. |

## Not yet covered

Gaps found while verifying these four leaves. None is covered elsewhere in
the topic as of 2026-09-06.

- **MAC16 beyond the one shape GCC reaches.** The MAC16 leaf shows the
  one loop GCC compiles to `mula.aa.ll`; still missing are the
  `MUL.DA`/`MUL.DD` operand model with the `MR` registers, the
  `ACCLO`/`ACCHI` context-save cost, and whether a 40-bit accumulator ever
  beats `MUL32_HIGH` for a fixed-point inner product.
- **The windowed ABI's cost at the instruction level.** The option table
  states that `ENTRY` and `RETW` make calls cheap and interrupt entry
  expensive. What a window overflow actually costs, and when `-mabi=call0`
  is the better trade for a leaf kernel, is not written down.
- **`S32C1I` and the release-ordered pair.** The option table names
  `S32C1I`, `L32AI` and `S32RI` and stops there. What the compiler emits
  for a C11 atomic, and what ordering the pair actually gives on a
  dual-core part, is uncovered.
- **The narrow-encoding effect on real code size and fetch traffic.** The
  claim that density forms cut instruction-fetch traffic is stated but
  never measured against a 16 KB icache.
- **How much of the seed-and-refine divide is worth writing.** The FP leaf
  names the refinement shape and cites one worked example, but nothing
  measures a hand-written `recip0.s` sequence against `__divsf3` on the
  device, so "hand-write it" remains advice without a number.
- **The Extended L32R Option and `LITBASE`.** The manual documents it as
  the fix for literal pools in instruction RAM over 256 KB. Whether this
  core has it, and whether the toolchain would use it, is not recorded
  anywhere in this bucket.
- **Where the LX7's zero-overhead loop buffer helps.**
  `XCHAL_LOOP_BUFFER_SIZE` is 256 on both chips, and what that buffer does
  for instruction fetch is still marked uncertain.
