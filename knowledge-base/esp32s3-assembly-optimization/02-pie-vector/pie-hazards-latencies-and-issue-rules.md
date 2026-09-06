---
title: "PIE hazards, latencies and issue rules"
id: 02-pie-vector/pie-hazards-latencies-and-issue-rules
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, pie, simd, pipeline, latency, hazards, scheduling]
confidence: medium
---

# PIE hazards, latencies and issue rules

Section 1.7 of the ESP32-S3 Technical Reference Manual, "Instruction
Performance", carries the only published timing model for the PIE vector
unit.[^trm-perf] It gives the five pipeline stages, the arithmetic that turns
a dependency into a delay, and a table of the stage at which every extended
instruction reads and writes each operand. It gives no cycle count for any
instruction and no throughput number anywhere.

This file turns that table into rules a kernel author can apply. The
instruction set itself is in the sibling leaves for
[register file and state](./pie-register-file-sar-and-context.md),
[load, store and alignment](./pie-load-store-and-alignment.md) and
[compute instructions](./pie-arithmetic-multiply-saturate-and-shuffle.md), and
the scalar side of the same pipeline is in
[the LX7 cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md).

## 1. The model

The core is a five-stage pipeline: I fetch, R decode, E execute, M memory,
W write back, numbered R=0, E=1, M=2, W=3.[^trm-perf] If instruction A
produces a value at the end of stage `SA` and instruction B consumes it at the
start of stage `SB`, B cannot issue earlier than

```
D = max(SA - SB + 1, 0)
```

cycles after A issued.[^trm-perf][^isa-perf] The processor enforces this: if B
arrives sooner it is held in R and retried, which the manuals call an
interlock.[^isa-perf] Values are forwarded from the stage that produced them,
so the table's numbers are already the bypassed ones.[^trm-perf] Table 1.7-2
uses only stage numbers 1 and 2, so the model collapses to four cases:

| Producer defines at | Consumer uses at | D | Independent instructions needed between them |
|---|---|---|---|
| 1 | 1 | 1 | 0, back to back is free |
| 2 | 1 | 2 | 1 |
| 2 | 2 | 1 | 0, back to back is free |
| 1 | 2 | 0 | 0, no constraint at all |

So the whole model reduces to one question for any pair of PIE instructions:
does the producer define at stage 2 and the consumer use at stage 1? Only that
pairing costs a slot. Consumers that use at stage 2 are rare and always free:
the only ones in the table are the four store forms that read `qv` at stage 2,
`EE.CMUL.S16.ST.INCP`, `EE.FFT.CMUL.S16.ST.XP`, `EE.FFT.AMS.S16.ST.INCP` and
`EE.FFT.VST.R32.DECP`, which can therefore follow the load that produced the
stored register with nothing in between.[^trm-perf] Every other store form
reads its `qv` at stage 1.

## 2. What defines at stage 2

These are the producers that carry a one instruction gap before a stage 1
consumer. Everything else in the table defines at stage 1.[^trm-perf]

| Producer group | Operand defined at 2 |
|---|---|
| Every 128-bit and 64-bit vector load, broadcast and fused load forms included, plus `EE.LDXQ.32` | `qu`, and `qu1` as well for `EE.VLDHBC.16.INCP`, the one form that loads two |
| `EE.LDF.128.*` and `EE.LDF.64.*` | `fu0` to `fu3` |
| `EE.VMUL.S8/U8/S16/U16`, `EE.CMUL.S16`, the FFT multiplies, `EE.VPRELU.S8/S16` | `qz`, `qz1` |
| `EE.VRELU.S8/S16` | `qs`, which is also its input |
| Multiply accumulate into `ACCX`, `QACC_H`, `QACC_L` | the accumulator state |
| `EE.LD.ACCX.IP`, `EE.LD.QACC_*`, `EE.LDQA.*`, `EE.LD.UA_STATE.IP` | the state register they load |
| `EE.FFT.AMS.S16.ST.INCP` | `as0`, its second address register, the only address register in the table defined late |

Two consequences are easy to get wrong. **Multiplies are load-shaped**:
`EE.VMUL.S16` defines `qz` at stage 2 exactly as a load does, while adds,
subtracts, min, max, compares, the bitwise ops, the vector shifts, the zips and
unzips and `EE.SRC.Q` all define `qa` at stage 1 and chain with no gap.
**`EE.VRELU` is in place and slow**: it uses `qs` at stage 1 and defines the
same `qs` at stage 2, so a second `EE.VRELU` on that register, or any stage 1
read of it, needs the gap.[^trm-perf] The table above is the whole of it:
apart from the special registers, `qu`, `qu1`, `qz`, `qz1`, `fu0` to `fu3`,
`EE.VRELU`'s `qs` and that one `as0` are the only operands the table defines
at stage 2.[^trm-perf]

## 3. Accumulators

The multiply accumulate instructions read and write their accumulator at
stage 2, so a chain of them issues one per cycle with no gap:
`D = max(2 - 2 + 1, 0) = 1`. That holds for `ACCX` with `EE.VMULAS.*.ACCX` and
for `QACC_H` and `QACC_L` with `EE.VMULAS.*.QACC` and
`EE.VSMULAS.*.QACC`.[^trm-perf]

Reading it out is where the gap appears: `EE.SRS.ACCX`, `EE.ST.ACCX.IP`,
`EE.ST.QACC_*` and the two `EE.SRCMB.*.QACC` forms use the accumulator at
stage 1, so put one independent instruction after the last accumulate.
Clearing costs nothing, because `EE.ZERO.ACCX`, `EE.ZERO.QACC` and the
`EE.MOV.*.QACC` preloads all define at stage 1, giving a following accumulate
`D = 0`.[^trm-perf]

## 4. SAR and SAR_BYTE

`SAR` holds a shift in bits, read by the vector shifts and by every PIE
multiply to right shift the intermediate product. `SAR_BYTE` holds a shift in
bytes, read by the `EE.SRC.Q` family and the `.QUP` fused forms.[^trm-sar]
Every PIE instruction that reads either register reads it at stage 1, and every
PIE instruction that writes `SAR_BYTE` writes it at stage 1, so within PIE the
pairing is always `D = 1`.[^trm-perf] The case that matters is the unaligned
load idiom:

```
ee.ld.128.usar.ip  q0, a2, 16    ; SAR_BYTE def 1, q0 def 2
ee.ld.128.usar.ip  q1, a2, 16    ; SAR_BYTE def 1, q1 def 2
ee.src.q           q2, q0, q1    ; SAR_BYTE use 1, q0 and q1 use 1
```

The shift amount is ready in time. The data is not: `q1` is defined at stage 2
and used one instruction later, so `EE.SRC.Q` stalls one cycle unless something
independent goes between it and the second load.

Nothing writes `SAR` inside Table 1.7-2. It is written by the base instructions
`WSR.SAR`, `SSL`, `SSR`, `SSAI`, `SSA8B` and `SSA8L`, which the table does not
cover, so their def stage here is `[uncertain]`.[^isa-sar] What is documented
is correctness, not timing: the Xtensa manual says the point at which `WSR.*`
takes effect is undefined for most special registers but that `SAR` and `ACC`
are exceptions, so no synchronisation is needed after writing `SAR`.[^isa-sync]
The PIE state registers are user registers reached with `RUR.*` and `WUR.*`,
which the manual says are fully interlocked in hardware and never need a sync
instruction either.[^isa-user] Their stages are `[uncertain]` for the same
reason: those are base instructions too.

## 5. Pointers and shuffles are free

Every post-increment form uses `as` at stage 1 and defines `as` at stage 1, so
a walking pointer costs no interlock however tight the loop. The one exception
is `EE.FFT.AMS.S16.ST.INCP`, which defines its second address register `as0` at
stage 2 while defining `as` at stage 1, so a stage 1 read of `as0` right after
it needs the gap.[^trm-perf] `EE.VZIP.*`,
`EE.VUNZIP.*`, `EE.SLCI.2Q`, `EE.SRCI.2Q`, `EE.SLCXXP.2Q` and `EE.SRCXXP.2Q`
use `qs0` and `qs1` at stage 1 and define both at stage 1, so a run of them
chains with no gap. `EE.MOVI.32.A` defines its `au` result at stage 1, so
pulling a lane into a scalar register carries no load-like penalty.[^trm-perf]

## 6. What the operand table does not cover

**These are interlocks, not hazards.** Get the order wrong and the code is
still correct, just slower. The Xtensa manual reserves the word hazard for a
small set of dependencies, mainly the special registers controlling exceptions,
interrupts and memory management, where the processor does not interlock and
software must insert an `xSYNC` instruction.[^isa-perf] No PIE state register
is in that set, by the two rules quoted in section 4, so a PIE schedule carries
no correctness obligation. The obligations are alignment and state, which the
sibling leaves cover.

**Resource hazards.** The core has eight 16-bit multipliers, and when more
instructions want a unit than there are copies the processor delays the later
one.[^trm-perf] The manual never says which PIE instruction reserves which
unit, for how many cycles, or in which stage, so whether two `EE.VMUL.S16`
instructions can issue on consecutive cycles is `[uncertain]`. `EE.VMUL.S8`
produces sixteen 8-bit products against eight documented 16-bit multipliers,
which hints at a multi-cycle reservation, but the manual does not say so. See
[timing a kernel with CCOUNT](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md).

**Control hazards.** A taken branch re-fetches from the target and the
instructions already in R and E are removed, so the pipeline is stagnant for
two cycles.[^trm-perf] That is a property of the branch, not of PIE, and a
zero-overhead `LOOP` has no branch on the back edge and pays none of it. See
[zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md).

**Issue width is one.** The chapter's ideal is one instruction per cycle, and
the Xtensa manual says this pipeline is "capable of executing at most one
instruction per cycle".[^trm-perf][^isa-perf] The configuration agrees:
`XCHAL_HAVE_FLIX3` is 0 and `XCHAL_INST_FETCH_WIDTH` is 4, so there is no VLIW
bundling here, and neither manual describes any instruction fusion.[^coreisa]
The fused forms are the ISA's answer: `EE.VADDS.S16.LD.INCP` carries the
operands of both an add and a load in one row of Table 1.7-2, with one issue
point. Whether it avoids a resource stall that two separate instructions would
have is `[uncertain]`, for the reason above.

The table also leaves out:

- **Base instructions.** The table lists only `EE.*` mnemonics.[^trm-perf] A
  scalar instruction interleaved into a PIE loop takes its stages from the
  general Xtensa model: ALU result at the end of E, load result at the end of
  M.[^isa-perf]
- **`LD.QR`, `ST.QR` and `MV.QR`,** which have their own descriptions in TRM
  section 1.8 and no row in the table,[^trm-ldqr] and `RUR.*` and `WUR.*`, as
  noted in section 4.
- **Any cycle count.** The table gives relative issue distances only, and
  assumes a `qu` defined at stage 2 was serviced in M. A miss to PSRAM is a
  different order of cost; see
  [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md).

## 7. Schedule-checking a loop

The procedure is mechanical. For each instruction in the body, look up its def
operands and stages in Table 1.7-2; for each later instruction using one of
those operands, compute `D` and check that at least `D - 1` instructions sit
between them. A hardware loop has no branch on the back edge, so the body is a
cycle: carry the check from the last instruction into the first of the next
iteration. Take a saturating multiply then add over signed 16-bit lanes, in the
order the C source suggests:

```
ee.vld.128.ip  q0, a2, 16    ; q0 def 2
ee.vmul.s16    q2, q0, q1    ; q0 use 1  -> D=2, one cycle stall
ee.vadds.s16   q3, q2, q4    ; q2 def 2, use 1 -> D=2, one cycle stall
ee.vst.128.ip  q3, a5, 16    ; q3 def 1, use 1 -> D=1, no stall
```

Four instructions, two stalls, six cycles per eight lanes. Rotating the load
one iteration ahead fills both slots without adding any instruction:

```
        ee.vld.128.ip  q1, a3, 16     ; prologue: constant operand
        ee.zero.q      q4
        ee.vld.128.ip  q0, a2, 16     ; prologue: first input
        loopgtz        a4, .Lend
        ee.vmul.s16    q2, q0, q1     ; q0 was defined at least 2 cycles ago
        ee.vld.128.ip  q0, a2, 16     ; independent of q2, fills the gap
        ee.vadds.s16   q3, q2, q4     ; q2 defined 2 cycles ago, D=2 met
        ee.vst.128.ip  q3, a5, 16     ; q3 defined 1 cycle ago, D=1 met
.Lend:
```

Checking the wrap: the `ee.vld` at position 2 defines `q0` at stage 2 and the
next iteration's `ee.vmul` is three instructions later, which more than covers
`D = 2`. Four instructions, no stall, four cycles per eight lanes on the data
hazard model, subject to the resource question in section 6. Both fragments
assemble as written with the ESP32-S3 dynamic configuration,[^m-asm] which
proves the mnemonics and the operand order, not the cycle claim. Confirm that
on the device or in QEMU;
[the verification ladder](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md)
is the route.

## 8. What the compiler does with this

GCC 14.2 for Xtensa schedules for the base load-use distance. A scalar dot
product at `-O2` gets an independent pointer increment between the second load
and the multiply that consumes it, which is the `D = 2` rule applied to `l32i`.
It does none of this for PIE, because it cannot see PIE: the toolchain exposes
no PIE builtins, and `__builtin_xtensa_ee_vld_128_ip` is not
declared.[^m-gcc] PIE reaches the compiler only as text inside an extended asm
block, which the scheduler treats as opaque. So the schedule inside such a
block is entirely the author's, and this table is its only reference. See
[extended inline asm](../04-toolchain-and-codegen/gcc-extended-inline-asm-on-xtensa.md)
and
[loop shapes and scheduling](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md).

## Open questions

- **Per-instruction resource reservation is undocumented,** so back-to-back
  throughput of `EE.VMUL.*` and `EE.VMULAS.*` is `[uncertain]`. A CCOUNT run
  comparing a dense multiply chain against one with independent fillers would
  settle it.
- **`LD.QR`, `ST.QR` and `MV.QR` have no row in Table 1.7-2.**[^trm-ldqr]
  Their stages are `[uncertain]`; a timing test against `EE.VLD.128.IP` would
  settle it.
- **`RUR.*` and `WUR.*` stages are `[uncertain]`,** which matters for a kernel
  that clears `ACCX` with `wur.accx_0` rather than `EE.ZERO.ACCX`. The
  correctness rule is documented, the timing is not.
- **`SAR` write to PIE use is `[uncertain]`.** No sync is required, but the
  issue distance from `SSAI` or `WSR.SAR` to a following `EE.VMUL.S16` is not
  published.
- **`RSR` latency.** The Xtensa manual warns that on some implementations
  `RSR` takes more than one cycle and its result should be scheduled away from
  the read.[^isa-sync] Whether that applies here is `[uncertain]`, and it
  affects any kernel reading `CCOUNT` around a short region.
- **Section 1.7 is never qualified by core,** and nothing here was checked on
  both.

## Footnotes

[^trm-perf]: Espressif Systems, 2026. *ESP32-S3 Technical Reference Manual*,
    version 1.8, section 1.7 "Instruction Performance", pages 65 to 75:
    Table 1.7-1 and the interlock formula page 65, Figure 1.7-1 page 66,
    Table 1.7-2 "Extended Instruction Pipeline Stages" pages 66 to 74,
    section 1.7.2 "Hardware Resource Hazard" and Figure 1.7-2 page 74,
    section 1.7.3 "Control Hazard" page 74 and Figure 1.7-3 page 75.
    <https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf>

[^trm-sar]: Same manual, section 1.5.1.2 "Special Registers", the `SAR` and
    `SAR_BYTE` entries, pages 46 to 48.

[^trm-ldqr]: Same manual, section 1.8.218 `LD.QR` page 301, section 1.8.219
    `ST.QR` page 302, section 1.8.220 `MV.QR` page 303. The `ST.QR` entry
    prints its assembler syntax line as `LD.QR qs, as, imm`, which is a defect
    in the manual.

[^isa-perf]: Tensilica (Cadence Design Systems), April 2010. *Xtensa
    Instruction Set Architecture (ISA) Reference Manual*, release RC-2010.1.
    Section 8.4.1, pages 605 to 607, for the `D = max(SA - SB + 1, 0)` rule,
    the definitions of interlock and hazard, the `xSYNC` obligation and
    Figures 8-54 and 8-55; section 8.4.2, pages 608 to 609, for "capable of
    executing at most one instruction per cycle", Table 8-247, the ALU and
    load-use implications and Figure 8-56.

[^isa-sar]: Same manual, Table 5-135 "SAR - Special Register #3", page 215:
    `SSL`, `SSR`, `SSAI`, `SSA8B` and `SSA8L` are the other writers of `SAR`,
    and `SLL`, `SRL`, `SRA` and `SRC` its other readers.

[^isa-sync]: Same manual, section 3.8.10, page 45: "The point at which WSR.*
    or XSR.* to most Special Registers affects subsequent instructions is not
    defined (SAR and ACC are exceptions)", plus the warning on the same page
    that on some implementations the latency of `RSR` exceeds one cycle.

[^isa-user]: Same manual, section 5.4.1 "Reading and Writing User Registers",
    pages 237 to 238: "The User Registers are fully interlocked in hardware and
    do not need SYNC instructions."

[^coreisa]: ESP-IDF 5.5.1,
    `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`:
    `XCHAL_MAX_INSTRUCTION_SIZE` 4 (line 53),
    `XCHAL_HAVE_CONNXD2_DUALLSFLIX` 0 (line 162), `XCHAL_HAVE_FLIX3` 0
    (line 175), `XCHAL_INST_FETCH_WIDTH` 4 (line 195).

[^m-asm]: `[measured]` 2026-09-06. Both fragments in section 7, plus the
    accumulator and `SAR_BYTE` sequences above, assembled with
    `xtensa-esp-elf-as --dynconfig=<toolchain>/lib/xtensa_esp32s3.so`, GNU
    assembler 2.43.1 (crosstool-NG esp-14.2.0_20241119), and disassembled with
    `xtensa-esp32s3-elf-objdump -d`, which is the objdump that knows the
    extension: the plain `xtensa-esp-elf-objdump` prints `excw` for every
    `EE.*` word. Each mnemonic in the fragments encoded to a single 24-bit
    instruction and disassembled back to what was written.

[^m-gcc]: `[measured]` 2026-09-06. `xtensa-esp-elf-gcc -O2 -mlongcalls -S`,
    GCC 14.2.0 from the same toolchain, on a four-line integer dot product. The
    emitted `loop` body is `l32i`, `l32i`, `addi.n`, `mull`, `addi.n`,
    `add.n`: the pointer increment sits between the second load and the
    multiply reading it. A call to `__builtin_xtensa_ee_vld_128_ip` in the same
    session failed with "implicit declaration of function".
