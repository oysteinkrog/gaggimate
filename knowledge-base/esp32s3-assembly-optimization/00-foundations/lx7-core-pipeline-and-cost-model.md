---
title: LX7 core pipeline and cycle cost model
id: 00-foundations/lx7-core-pipeline-and-cost-model
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, pipeline, cost-model, cycles, load-use, branches]
confidence: medium
---

# LX7 core pipeline and cycle cost model

What the Xtensa LX7 core in the ESP32-S3 does in a cycle, and what an
optimizer may assume about cost. Cache and PSRAM behaviour is only
summarised here;
[caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)
carries it.

One limit shapes this whole file. The Xtensa ISA manual gives the pipeline
structure and the interlock model but no per-instruction cycle counts. It
says so directly: "Refer to a specific Xtensa processor data book for
detailed descriptions of processor performance and tables of pipeline
stages where operands are used and defined" [Tensilica 2010][^1]. The LX7
data book is not openly published; the Cadence product pages returned
HTTP 403 to an unauthenticated fetch on 2026-09-06. So every latency below
that is not structural is marked `[uncertain]` with a way to measure it.

## 1. The core as configured

| Property | Value | Source |
|---|---|---|
| Cores | 2, Xtensa 32-bit LX7 | [Espressif 2026a][^2] p. 3 |
| Clock | up to 240 MHz | [Espressif 2026a][^2] p. 3 |
| Pipeline | Five-stage | [Espressif 2026a][^2] p. 3 |
| Core version string | `LX7_ESP32_S3_MP`, hardware version `LX7.0.12` | [ESP-IDF 5.5.1][^4] `XCHAL_CORE_ID`, `XCHAL_HW_VERSION_NAME` |
| Register file | 64 physical address registers, windowed | [ESP-IDF 5.5.1][^4] `XCHAL_NUM_AREGS`, `XCHAL_HAVE_WINDOWED` |
| Instruction width | 24-bit base, 16-bit density option on | [ESP-IDF 5.5.1][^4] `XCHAL_HAVE_DENSITY`; [Tensilica 2010][^1] §1.2 |
| Instruction fetch | 4 bytes per cycle | [ESP-IDF 5.5.1][^4] `XCHAL_INST_FETCH_WIDTH` |
| Data-side pipeline delay | 1, which the header itself glosses as the 5-stage pipeline | [ESP-IDF 5.5.1][^4] `XCHAL_DATA_PIPE_DELAY` |
| Zero-overhead loop | Yes, 256-byte loop buffer | [ESP-IDF 5.5.1][^4] `XCHAL_HAVE_LOOPS`, `XCHAL_LOOP_BUFFER_SIZE` |
| 32-bit multiply | `MULL`, `MULSH`, `MULUH` | [ESP-IDF 5.5.1][^4] `XCHAL_HAVE_MUL32`, `XCHAL_HAVE_MUL32_HIGH` |
| 32-bit integer divide | Yes: `QUOS`, `QUOU`, `REMS`, `REMU` | [ESP-IDF 5.5.1][^4] `XCHAL_HAVE_DIV32` |
| Floating point | Single precision only, no double | [ESP-IDF 5.5.1][^4] `XCHAL_HAVE_FP`=1, `XCHAL_HAVE_DFP`=0 |
| Also on | `NSA`, `MIN`/`MAX`, `SEXT`, `CLAMPS`, `ADDX`, `MAC16`, Boolean registers | [ESP-IDF 5.5.1][^4] |

The cache entries in that header mislead. `XCHAL_ICACHE_SIZE` and
`XCHAL_DCACHE_SIZE` are both 0 and the line sizes read 4 and 16 bytes
[ESP-IDF 5.5.1][^4], because those describe Xtensa core caches, which this
configuration does not have. The real caches sit outside the core in the
memory controller and are shared by both cores: instruction cache 16 KB or
32 KB with a 16-byte or 32-byte block, data cache 32 KB or 64 KB with a 16,
32 or 64-byte block [Espressif 2026b][^3] §4.3.3.2. Take cache geometry
from the TRM, never from `core-isa.h`.

## 2. The pipeline

Xtensa implementations may use a 5-stage or a 7-stage load-store pipeline
[Tensilica 2010][^1] §1.2.8 p. 12. Three sources say which one this chip
has. The ESP32-S3 datasheet states five stages [Espressif 2026a][^2] p. 3.
The TRM names the five and what each does, "I (instruction fetch), R
(decode), E (execute), M (memory access), and W (write back)"
[Espressif 2026b][^3] §1.7 and Table 1.7-1 p. 65. And the generated core
configuration sets `XCHAL_DATA_PIPE_DELAY` to 1 with the inline comment
`(1 = 5-stage, 2 = 7-stage)` [ESP-IDF 5.5.1][^4]; the original ESP32 sets
the same macro to 2, so this is a real difference between the two parts
and not boilerplate.

The pipeline executes at most one instruction per cycle
[Tensilica 2010][^1] §8.4.2 p. 608. Stages [Tensilica 2010][^1]
Table 8-247 p. 608, with the stage numbers the TRM uses
[Espressif 2026b][^3] Table 1.7-1 p. 65:

| Stage | Number | Work |
|---|---|---|
| I | (none) | Instruction memory access, tag compare, instruction alignment |
| R | 0 | Address and `q` register file read, decode, interlocking, bypass, instruction cache miss recognition |
| E | 1 | Most ALU work, address generation for loads and stores, branch decision |
| M | 2 | Data memory access, tag compare, miss recognition, load data alignment |
| W | 3 | State writes, including the address register file write |

I is partly decoupled from R, and R is partly decoupled from E, M and W,
which move in lock-step. An interlock detected in R retries the instruction
in R next cycle and sends a no-op down to E [Tensilica 2010][^1] §8.4.2
p. 608.

### The interlock rule

The cost model is not a per-instruction latency number. It is a rule about
stages. If instruction A defines a value at the end of stage `SA`, and
instruction B uses it at the start of stage `SB`, then B can issue no
earlier than `max(SA - SB + 1, 0)` cycles after A issued
[Tensilica 2010][^1] §8.4.1 p. 605. The TRM restates the same formula for
this chip [Espressif 2026b][^3] §1.7.1 p. 65, and Table 1.7-2 (pp. 66-74)
gives the use and def stage of every operand of every extended (PIE)
instruction, which is the only published per-instruction stage table for
this part. Delaying B for this reason is called an interlock. A small set of dependencies, mainly around exception, interrupt
and memory-management special registers, does not interlock at all; those
are hazards and need an explicit `xSYNC` instruction
[Tensilica 2010][^1] §8.4.1 p. 606.

A second source of delay is a busy functional unit. If a unit is occupied
for several cycles, a later instruction needing it is held even when there
is no data dependency. The manual names integer and floating point division
as the usual case [Tensilica 2010][^1] §8.4.1 p. 606, Figure 8-55 p. 607.
The TRM gives the chip's own example and, with it, one hardware number:
"there are only eight 16-bit multipliers in the processor", so an
instruction wanting eight of them in M and another wanting four in E
collide, and the second is delayed a cycle [Espressif 2026b][^3] §1.7.2
p. 74.

## 3. Load-use

An ALU result is defined at the end of E and needed at the start of E, so a
dependent instruction runs with no delay [Tensilica 2010][^1] §8.4.2 p. 608.

A load result is defined at the end of M (stage 2) and needed at the start
of E (stage 1). By the rule above the dependent instruction issues no
earlier than `max(2 - 1 + 1, 0) = 2` cycles after the load. Back-to-back placement is
1 cycle after, so the consumer stalls for one cycle. The manual's own advice
is to put one independent instruction between the load and its consumer
[Tensilica 2010][^1] §8.4.2 p. 608.

This is the single most useful scheduling fact on this core. One filler
instruction is free. Two loads issued back to back, then their two
consumers, cost nothing extra; a load, its consumer, a load, its consumer
costs two stalls per pair.

Stores can also interlock. The core holds a store in R when the write
buffer is full, or could become full from stores already in E and M
[Tensilica 2010][^1] §8.4.2 p. 609.

## 4. Branches

The branch decision happens in E and must steer the I stage of the target
fetch. Two already-fetched fall-through instructions are killed on a taken
branch [Tensilica 2010][^1] §8.4.2 p. 608, Figure 8-56 p. 609. The TRM
says the same thing about this chip and puts a number on it: on a taken
branch "the instructions at the R and E stages on the pipeline will be
removed, which means the pipeline remains stagnant for 2 cycles"
[Espressif 2026b][^3] §1.7.3 p. 74. A branch that is not taken kills
nothing, because the instructions in flight are the ones that go on to
execute.

Fetches are 32-bit aligned. If a branch target instruction crosses a fetch
boundary, two fetches are needed before the whole instruction is available
and the target starts three cycles after the branch instead of two. Align
frequently taken 24-bit branch targets at 0 or 1 mod 4, and 16-bit targets
at 0, 1 or 2 mod 4 [Tensilica 2010][^1] §8.4.2 p. 609.

The zero-overhead loop (`LOOP`, `LOOPNEZ`, `LOOPGTZ`) exists to remove this
cost from the inner loop entirely, and the option is configured on
[ESP-IDF 5.5.1][^4]. It carries its own alignment rule: the first
instruction of the loop body must fit entirely inside a naturally aligned
power-of-two unit of at least 4 bytes, so a 24-bit first instruction must
sit at 0 or 1 mod 4 [Tensilica 2010][^1] §4.3.2.2 p. 55. The last
instruction of the loop must not be a call, `ISYNC`, `WAITI` or
`RSR.LCOUNT`, and a taken branch as the last instruction leaves `LCOUNT`
undefined [Tensilica 2010][^1] §4.3.2.2 p. 55. When and whether GCC 14.2
actually emits one is a separate question, covered in
[zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md).

## 5. Integer multiply and divide

`MULL` writes the low 32 bits of the product, `MULSH` the high 32 bits of a
signed product, `MULUH` the high 32 bits of an unsigned product
[Tensilica 2010][^1] Ch. 6, `MULL`, `MULSH`, `MULUH` entries. All three are
present [ESP-IDF 5.5.1][^4].

Integer divide is in hardware. `QUOS`, `QUOU`, `REMS` and `REMU` are
configured on [ESP-IDF 5.5.1][^4], and GCC 14.2 emits them directly rather
than calling a library routine: `int a/b` compiles to a single `quos`, and
`unsigned a%b` to a single `remu` [measured][^5]. Divide by zero raises an
Integer Divide by Zero exception [Tensilica 2010][^1] §4.3.6 p. 59.

Hardware divide is not the same as fast divide. The manual's functional
unit interlock discussion names integer division as the case where a unit
iterates for several cycles [Tensilica 2010][^1] §8.4.1 p. 606, and the
Integer Divide Option's own description warns that "various algorithms may
be used to implement these instructions, and some hardware implementations
may be slower than the software implementations for some operand values"
[Tensilica 2010][^1] §4.3.6 p. 59. The algorithm is a configuration
parameter, `DivAlgorithm`, whose valid values the manual calls
implementation-dependent [Tensilica 2010][^1] Table 4-34 p. 59. The actual
LX7 issue-to-issue cost of `QUOS` is `[uncertain]`.

## 6. Floating point

The FPU is single precision only. `XCHAL_HAVE_DFP` is 0
[ESP-IDF 5.5.1][^4], so every `double` operation is a library call.
Confirmed: `double a/b` compiles to `call8 __divdf3` [measured][^5].

The base Xtensa Floating-Point Coprocessor Option has no divide, no square
root and no reciprocal instruction. Its operation set is add, subtract,
multiply, multiply-add, multiply-subtract, absolute value, negate, moves,
conditional moves, integer conversions and compares
[Tensilica 2010][^1] Table 4-50 pp. 72-74. The strings `DIV.S`, `SQRT.S`,
`RSQRT.S` and `RECIP.S` do not occur anywhere in that 662-page manual
[measured][^5].

The ESP32-S3 has more than that base option, and this is where the sources
disagree. ESP-IDF's `core-isa.h` sets `XCHAL_HAVE_FP_DIV`,
`XCHAL_HAVE_FP_SQRT`, `XCHAL_HAVE_FP_RECIP` and `XCHAL_HAVE_FP_RSQRT` all
to 1, and also sets `XCHAL_HAVE_DFPU_SINGLE_ONLY` to 1
[ESP-IDF 5.5.1][^4]. But the shipping assembler rejects `div.s`, `sqrt.s`,
`recip.s` and `rsqrt.s` as unknown opcodes for this target, while
accepting a set of Newton-Raphson helper instructions: `div0.s`, `divn.s`,
`recip0.s`, `sqrt0.s`, `rsqrt0.s`, `nexp01.s`, `maddn.s`, `const.s`,
`addexp.s`, `addexpm.s`, `mkdadj.s`, `mksadj.s` [measured][^5].

So the practical reading is: there is hardware assistance for divide and
square root, but no single instruction that performs either. Support is a
sequence. The shipped `__divsf3` for this target is 30 instructions in
total: `entry`, 28 instructions between it and the closing `retw.n`, then
`retw.n`. The body moves the operands into FPU registers with `wfr`, runs
`div0.s`, `nexp01.s`, `const.s`, a chain of eleven `maddn.s`, `mkdadj.s`,
`addexp.s`, `addexpm.s` and a final `divn.s`, and moves the result back
with `rfr` [measured][^5]. The exact semantics of these helper instructions are
`[uncertain]`: they are not in the 2010 ISA manual, which predates the LX7,
and no primary Cadence or Espressif document describing them was reachable
on 2026-09-06.

[The floating-point option on the LX7](../01-scalar-isa/floating-point-option-on-lx7.md)
carries the instruction-by-instruction picture. What an optimizer should
take from this:

- A float multiply, add or multiply-add is a single instruction that the
  compiler will inline. GCC 14.2 emits `madd.s` for `a*b+c` [measured][^5].
- A float divide is a windowed call into that 30-instruction routine, and
  `-ffast-math` does not change that. GCC 14.2 still emits
  `call8 __divsf3`, and `sqrtf` is likewise a C library call [measured][^5].
- A divide or a square root that repeats every iteration is worth hoisting
  out of the loop, tabulating, or turning into a multiply by a value you
  computed once.

## 7. Where the operand lives

A cycle cost is meaningless without saying which memory the operand came
from. Ranked by cost:

| Location | Behaviour |
|---|---|
| Register | No memory access |
| Internal SRAM (IRAM or DRAM) | Accessed in M; the load-use rule above is the whole cost |
| Cached flash or cached PSRAM, hit | Same as an SRAM access as far as the pipeline is concerned |
| Cached flash or cached PSRAM, miss | The cache controller fetches a whole block over the external memory bus while the core stalls |

Flash and PSRAM share one external memory interface on this chip, the
SPI0 and SPI1 controllers reserved for exactly that
[Espressif 2026b][^3] §30.1 p. 1106, and the caches are shared by both
cores, with an arbiter deciding who wins between the two cores and again
between ICache and DCache for the external memory
[Espressif 2026b][^3] §4.3.3.2. That means a miss cost is not a property of
your code alone. It depends on what the other core, and any bus master, is
doing. The magnitude of a miss in core cycles is `[uncertain]` here; see
[caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)
for the measured picture.

## 8. Why instruction count is not time

Counting instructions in a disassembly predicts time only if every
instruction issues in one cycle. On this core three things break that, and
all three are invisible in the instruction count.

**Load-use stalls.** Two kernels with identical instruction counts differ
by one cycle per dependent load if one interleaves and the other does not
(§3). In a loop body dominated by table lookups this is the difference
between the versions, not the arithmetic.

**Register window spills.** The windowed ABI is on [ESP-IDF 5.5.1][^4].
A call rotates the register window rather than saving registers, which is
cheap, until the window wraps. Then the hardware raises a
`WindowOverflow4`, `WindowOverflow8` or `WindowOverflow12` exception and a
handler writes registers to the stack; the matching underflow on return
reads them back [Tensilica 2010][^1] §4.7.1.3 p. 184, §4.7.1.6 pp. 192-193.
[Register windows and the windowed ABI](./register-windows-and-windowed-abi.md)
carries the mechanism in full. None of
that appears in the caller's instruction stream. A kernel that calls out of
its inner loop can pay for spills at a rate set by the call depth of the
whole program, not by the loop.

**Cache misses.** A tighter loop that walks a table with a worse access
pattern can lose to a longer loop that walks memory in order (§7).

A fourth reason is not the core's fault. On a preemptive system an
interrupt or a task switch can land inside the region being timed. Handle
it by taking the minimum of several runs rather than the mean.

The practical consequence is the verification order. Static instruction
count tells you the code shape changed. Only a cycle measurement tells you
it got faster.

## 9. Cost table

Every row is either structural, and cited, or marked `[uncertain]`. Costs
are extra cycles beyond the one issue slot unless stated.

| Operation | Cost | Source |
|---|---|---|
| Any instruction, no dependency, no miss | 1 issue slot, at most one per cycle | [Tensilica 2010][^1] §8.4.2 p. 608 |
| ALU result consumed by the next instruction | 0 extra | [Tensilica 2010][^1] §8.4.2 p. 608 |
| Load result consumed by the next instruction | 1 stall cycle; 0 if one independent instruction is placed between | [Tensilica 2010][^1] §8.4.2 p. 608 |
| Load consumed two instructions later | 0 extra | Follows from the interlock rule, [Tensilica 2010][^1] §8.4.1 p. 605 |
| Store when the write buffer is full | Interlocks in R, duration depends on the buffer draining | [Tensilica 2010][^1] §8.4.2 p. 609 |
| Branch not taken | 0 extra | [Tensilica 2010][^1] Figure 8-56 p. 609 |
| Branch taken, aligned target | 2 cycles: the instructions in R and E are removed | [Espressif 2026b][^3] §1.7.3 p. 74; [Tensilica 2010][^1] §8.4.2 p. 608 |
| Branch taken, target crosses a fetch boundary | Target begins three cycles after the branch instead of two, so 1 extra cycle | [Tensilica 2010][^1] §8.4.2 p. 609 |
| Zero-overhead loop back edge | No branch, so no taken-branch cost | [Tensilica 2010][^1] §4.3.2 p. 54 |
| `MULL`, `MULSH`, `MULUH` result latency | `[uncertain]`, measure with CCOUNT | Not given in [Tensilica 2010][^1]; deferred to the processor data book, §8.4.2 p. 609 |
| `QUOS`, `QUOU`, `REMS`, `REMU` | `[uncertain]`, iterative and multi-cycle | [Tensilica 2010][^1] §8.4.1 p. 606 says division iterates; no count given |
| `ADD.S`, `SUB.S`, `MUL.S`, `MADD.S` result latency | `[uncertain]`, measure with CCOUNT | Instructions confirmed present [Tensilica 2010][^1] Table 4-50; no latency published |
| Float compare, `OLT.S` and friends, into a Boolean register | `[uncertain]` | Same |
| `FLOAT.S`, `TRUNC.S` conversions | `[uncertain]` | Same |
| Float divide, `a / b` | A windowed call plus a 30-instruction routine, 28 of them between `entry` and `retw.n`; total `[uncertain]` | [measured][^5] |
| `sqrtf` | A C library call; total `[uncertain]` | [measured][^5] |
| Any `double` operation | A library call | [ESP-IDF 5.5.1][^4] `XCHAL_HAVE_DFP`=0; [measured][^5] |
| Load or store, cache hit | As an SRAM access | [Espressif 2026b][^3] §4.3.3.2 |
| Load or store, cache miss to flash or PSRAM | `[uncertain]`, a block fill over the shared external bus | [Espressif 2026b][^3] §4.3.3.2; see the [memory hierarchy leaf](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) |
| Register window overflow or underflow | `[uncertain]`, an exception plus a handler that moves 4, 8 or 12 registers | [Tensilica 2010][^1] §4.7.1.6 pp. 192-193 |

## 10. Measuring what is uncertain

Every `[uncertain]` row above is measurable on the device. The core has a
cycle counter, `CCOUNT`, which increments once per processor clock
[Tensilica 2010][^1] §4.4.6.2 p. 111. ESP-IDF exposes it as
`esp_cpu_get_cycle_count()` [ESP-IDF 5.5.1][^6].

To get a latency you can trust:

1. Put the sequence under test in a loop long enough that loop overhead is
   a small share, and unroll enough that the loop's own back edge does not
   dominate.
2. Read `CCOUNT` before and after. Both reads should be in the same
   function, with no call between them.
3. Run the loop many times and take the minimum, not the mean. The minimum
   is the run that was not interrupted.
4. Run it with the operands in internal SRAM and again in cached external
   memory, and report both. A number without the memory named is not a
   result.
5. To separate a data dependency from a functional unit conflict, run the
   sequence twice: once with the operations dependent on each other, once
   with independent operands. Independent operations that still cost more
   than one cycle each are hitting a busy unit, not a dependency.

[Timing a kernel with CCOUNT](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md)
carries the full method, and
[what QEMU proves and cannot prove](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md)
covers what the emulator does and does not reproduce.

## Footnotes

[^1]: Tensilica, Inc., *Xtensa Instruction Set Architecture (ISA) Reference
    Manual*, "For All Xtensa Processor Cores", issue date 4/2010, release
    RC-2010.1, document PD-09-0801-10-01, 662 pages. Read from the mirror at
    <https://0x04.net/~mwk/doc/xtensa.pdf> on 2026-09-06. Sections used:
    1.2.8 (p. 12), 4.3.2 and 4.3.2.2 (pp. 54-55), 4.3.5 (p. 58), 4.3.6 with
    Table 4-34 (pp. 59-60), 4.3.9 with Tables 4-41 and 4-42 (pp. 63-64),
    4.3.11 and Table 4-50 (pp. 67, 72-74), 4.4.6.2 (p. 111), 4.7.1.3
    (p. 184), 4.7.1.6 (pp. 192-193), Ch. 6 instruction descriptions,
    8.4.1 (pp. 605-607), 8.4.2 with Table 8-247 and Figures 8-54 to 8-56
    (pp. 605-609). Note this edition predates the LX7
    (announced 2016), so it describes the base architecture and options but
    not LX7-specific additions such as the single-precision DFPU helper
    instructions or the PIE vector extension.
[^2]: Espressif Systems, *ESP32-S3 Series Datasheet*, version 2.2, PDF
    dated 2026-03-09. CPU and Memory summary, p. 3.
    <https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf>
[^3]: Espressif Systems, *ESP32-S3 Technical Reference Manual*, version 1.8,
    PDF dated 2026-03-04, 1531 pages. Section 4.3.3.2 "Cache" and
    Figure 4.3-1 "Cache Structure", p. 405; Section 1.7 "Instruction
    Performance" with Table 1.7-1 "Five-Stage Pipeline of Xtensa
    Processor" and Section 1.7.1 "Data Hazard", p. 65, Table 1.7-2
    "Extended Instruction Pipeline Stages", pp. 66-74, Section 1.7.2
    "Hardware Resource Hazard" and Section 1.7.3 "Control Hazard", p. 74;
    Section 30.1 "Overview",
    p. 1106 ("SPI0 and SPI1 controllers are primarily reserved for
    internal use to communicate with external flash and PSRAM memory").
    <https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf>
[^4]: ESP-IDF v5.5.1 (PlatformIO package `framework-espidf` 3.50501),
    `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`. Macros
    read on 2026-09-06: `XCHAL_CORE_ID`, `XCHAL_HW_VERSION_NAME`,
    `XCHAL_NUM_AREGS`, `XCHAL_HAVE_WINDOWED`, `XCHAL_HAVE_DENSITY`,
    `XCHAL_INST_FETCH_WIDTH`, `XCHAL_HAVE_LOOPS`, `XCHAL_LOOP_BUFFER_SIZE`,
    `XCHAL_DATA_PIPE_DELAY`, `XCHAL_HAVE_MUL32`, `XCHAL_HAVE_MUL32_HIGH`,
    `XCHAL_HAVE_DIV32`,
    `XCHAL_HAVE_NSA`, `XCHAL_HAVE_MINMAX`, `XCHAL_HAVE_SEXT`,
    `XCHAL_HAVE_CLAMPS`, `XCHAL_HAVE_ADDX`, `XCHAL_HAVE_MAC16`,
    `XCHAL_HAVE_BOOLEANS`, `XCHAL_HAVE_FP`, `XCHAL_HAVE_FP_DIV`,
    `XCHAL_HAVE_FP_SQRT`, `XCHAL_HAVE_FP_RECIP`, `XCHAL_HAVE_FP_RSQRT`,
    `XCHAL_HAVE_DFP`, `XCHAL_HAVE_DFPU_SINGLE_ONLY`, and the four
    `XCHAL_?CACHE_SIZE` and `XCHAL_?CACHE_LINESIZE` macros. The
    `XCHAL_DATA_PIPE_DELAY` comparison against the original ESP32 uses the
    same macro in `components/xtensa/esp32/include/xtensa/config/core-isa.h`,
    where it is 2.
[^5]: Toolchain observations, `xtensa-esp-elf-gcc` 14.2.0 (crosstool-NG
    `esp-14.2.0_20241119`) and its bundled binutils, target
    `xtensa-esp32s3-elf`, 2026-09-06. Compiler output taken with
    `-O2 -mlongcalls -S`, and separately with `-ffast-math` added.
    Assembler acceptance tested one opcode at a time with
    `xtensa-esp32s3-elf-as`. Library routines disassembled from the
    esp32s3 multilib at
    `lib/gcc/xtensa-esp-elf/14.2.0/esp32s3/libgcc.a` and
    `xtensa-esp-elf/lib/esp32s3/libm.a` with
    `xtensa-esp32s3-elf-objdump -d`; `__divsf3` disassembles to 30
    instructions, `entry` at offset 0 and `retw.n` at offset 0x57. The
    662-page manual in [^1] was searched for `DIV.S`, `SQRT.S`, `RSQRT.S`
    and `RECIP.S` after `pdftotext -layout`, with zero matches.
[^6]: ESP-IDF v5.5.1, `components/esp_hw_support/include/esp_cpu.h`,
    `esp_cpu_get_cycle_count()` at line 181, which forwards to
    `xt_utils_get_cycle_count()` on Xtensa targets.
