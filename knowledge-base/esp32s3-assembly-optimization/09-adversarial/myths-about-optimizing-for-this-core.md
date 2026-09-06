---
title: Myths about optimizing for this core
id: 09-adversarial/myths-about-optimizing-for-this-core
schema_version: 1
doc_type: explanation
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, adversarial, myths, pie, gcc, measurement, cache]
confidence: high
sources: [xtensa-isa-rm-2010, esp32s3-trm-v18, esp-idf-5.5.1, gcc-14.2-xtensa]
---

# Myths about optimizing for this core

Most people arrive at the ESP32-S3 carrying an optimization model built
somewhere else: x86 or ARM, a general-purpose compiler course, an older
Xtensa part, or a forum thread. A good part of that model is wrong here, and
the wrong parts are not obvious, because the tools accept the code and it
runs. This file collects the pieces that fail, states what is true instead,
and points at the leaf that carries the detail.

Each entry names the myth in one line, gives the verdict, and shows the
evidence. Where a claim is something I ran, the command is given and the
claim is tagged `[measured]`. Toolchain results below are from
`xtensa-esp-elf` GCC 14.2.0 (crosstool-NG `esp-14.2.0_20241119`) with GNU
binutils 2.43.1, and ESP-IDF 5.5.1 headers, on 2026-09-06.[^tc]

## Summary

| Myth | Verdict |
|---|---|
| Fewer instructions means faster | False. Stalls and memory decide, not instruction count |
| A taken branch is nearly free | False. Two cycles, and there is no branch predictor |
| Hand-written assembly beats the compiler on principle | False. Parity is the normal outcome, and losses are common |
| Unroll 4x, it is free | False. Two hard limits, and a measured loss |
| Use the vector gather | False. There is no multi-lane gather |
| Shift 8-bit lanes | False. Lane shifts exist for 32-bit lanes only |
| Enable the vector unit by writing CPENABLE | False, and it corrupts other tasks |
| Name a vector register with the `"q"` constraint | False. On Xtensa `q` is the stack pointer |
| The compiler will vectorize with the right flags | False. GCC 14 cannot emit a PIE instruction at all |
| Prefetch or lock the table in cache | Half true, and not through the instructions you know |
| PSRAM runs at 120 MHz, so it is fast | Misleading. The default is 40 MHz quad, and 120 MHz octal is experimental |
| `memcpy` is a C loop you can beat | Mostly false. It is a ROM assembly routine |
| The FPU has divide and square root | False. Only seed instructions, and GCC calls a library |
| The core configuration header says what the compiler will emit | False. They disagree in both directions |
| `-O3` or `-funroll-loops` always helps | False. Measured loss on a hardware-loop kernel |
| `-mcpu=esp32s3` selects the core | False. No such option exists |
| `volatile` makes timing deterministic | False. It is neither a barrier nor a scheduler |
| A QEMU cycle count is a device cycle count | False. It is virtual time scaled by 40 MHz |
| The FreeRTOS tick is CCOUNT | False on this chip. The tick is the systimer |

## Cost model

### 1. Fewer instructions means faster

**False.** The LX7 is a five-stage in-order pipeline executing "at most one
instruction per cycle", so two kernels with the same instruction count can
differ by a large margin in cycles, and the shorter one can be the slower
one.[^isa-pipeline] The two costs that decide it are both invisible in a
static count. An instruction that consumes a load result "must issue two
cycles after the load", so a load feeding the next instruction stalls unless
an independent instruction is placed between them.[^isa-pipeline] A cache
miss on a table in external memory costs far more than any instruction the
loop contains, which is why table placement outranks loop-body tuning
([caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)).

The corpus records this reversing real decisions. Two hand-written kernels
each had fewer static instructions than the compiler's own loop for the same
work and each still ran slower on silicon, so both were retired:
"kernels that won on static instruction count lost on the chip ... because
the device pays for load-use stalls and cache misses, not instructions"
([host versus device reversals measured here](../07-our-work/host-versus-device-reversals-measured-here.md)).
Detail: [the LX7 pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md).

### 2. A taken branch is nearly free

**False.** The branch decision happens in the E stage and must redirect the
instruction fetch, so "there are two fetched fall-through instructions that
are killed on taken branches".[^isa-pipeline] The TRM says the same thing
about this part in its own words: on a taken branch "the instructions at the
R and E stages on the pipeline will be removed, which means the pipeline
remains stagnant for 2 cycles".[^trm-hazard] A not-taken branch costs
nothing extra.

There is also nothing to predict with. The core configuration sets
`XCHAL_HAVE_PREDICTED_BRANCHES` to 0, so the optional predicted-branch
instructions are not built and there is no hint to give.[^core-isa] Two
cycles per taken branch is a flat, unavoidable cost, which is why counting a
loop down into the zero-overhead `LOOP` instruction is worth doing.
Detail: [branches, jumps and control-flow costs](../01-scalar-isa/branches-jumps-and-control-flow-costs.md).

### 3. Hand-written assembly beats the compiler on principle

**False.** GCC 14's schedule is the baseline to beat, not the thing you are
guaranteed to improve on. In this repo's own kernel pass, several
hand-written kernels lost to the compiler's generated code on the device,
including one written specifically to transcribe the compiler's loop
mnemonic for mnemonic: it still measured slower, and the compiler's version
was shipped instead
([host versus device reversals measured here](../07-our-work/host-versus-device-reversals-measured-here.md),
[the animation kernel pass and what the device decided](../07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md)).
The wins that did land came from something the compiler structurally cannot
do, such as a closed loop across a call boundary or an addressing form it has
no pattern for, not from writing the same loop by hand.

The useful reading of this: assembly is worth it when you can name the
specific thing the compiler is unable to express. "I will schedule it better"
is not that thing.

### 4. Unroll 4x, it is free

**False**, and it has two hard limits here that other targets do not have.

The zero-overhead loop's body cannot exceed 256 bytes, because `LEND` is
formed from an unsigned 8-bit immediate; unrolling inside a hardware loop
eats that budget.[^core-isa][^loops-leaf] And the windowed ABI leaves roughly
13 to 14 usable address registers inside one call, so an unrolled body
multiplies live values against a fixed and small register file, and a spilled
value reintroduces exactly the load-use stall the unroll was supposed to
hide.[^spills-leaf]

**[measured]** On a plain 16-bit add loop, adding `-funroll-loops` removed
the hardware loop entirely and grew the function from 14 to 84 instructions,
at both `-O2` and `-O3`:[^tc-unroll]

```
-O2                    loop=1  insns=14
-O2 -funroll-loops     loop=0  insns=84
-O3                    loop=1  insns=14
-O3 -funroll-loops     loop=0  insns=84
```

The corpus records a manual 4x unroll being reverted for the same reason
after the device disagreed with the host bench that liked it
([host versus device reversals measured here](../07-our-work/host-versus-device-reversals-measured-here.md)).
Detail: [zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md) and
[loop shapes, scheduling and per-call setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md).

## The vector unit

### 5. Use the vector gather

**False. There is no multi-lane gather.** The nearest instruction is
`EE.LDXQ.32`, which loads one 32-bit word into one lane, using one 16-bit
lane of a `q` register scaled by 4 as the index. Filling four lanes takes
four of them plus the index setup, and the store side (`EE.STXQ.32`) is the
same one-lane shape.[^trm-ldxq] A design ported from an AVX2 `vpgatherdd`
loop, or from any SIMD kernel whose whole point is the gather, has no
equivalent here and should be reconsidered as a scalar loop.
Detail: [PIE load, store and alignment](../02-pie-vector/pie-load-store-and-alignment.md)
and [LUT gathers, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md).

### 6. Shift 8-bit lanes

**False.** Lane shifts exist for 32-bit lanes only: `EE.VSL.32` and
`EE.VSR.32`, both taking the amount from `SAR`. There is no 8-bit or 16-bit
lane shift in the set at all.[^trm-shift] The substitute is a multiply by a
vector of `2^k`, using `SAR` to place the result, which costs a register to
hold the multiplier vector and puts the operation on the multiply path rather
than the cheap arithmetic path.
Detail: [PIE arithmetic, multiply, saturate and shuffle](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md).

### 7. Enable the vector unit by writing CPENABLE

**False, and it can corrupt an unrelated task.** FreeRTOS on this port enables
each coprocessor lazily, through the Coprocessor Disabled exception, and it
tracks which task currently owns the unit in a separate owner array. That
scheme depends on each task's `CPENABLE` bit being exactly what the exception
handler last left it. A direct write makes the hardware and the owner array
disagree, and the next context switch can then save one task's registers over
another task's live state, or skip a save it needed to
make.[^cpenable-leaf] Let the exception fire; that is the mechanism, not an
overhead to route around.
Detail: [coprocessors, CPENABLE and lazy context switching](../00-foundations/coprocessors-cpenable-and-lazy-context.md).

### 8. Name a vector register with the `"q"` constraint

**False, and it fails loudly.** On Xtensa the `q` constraint is the stack
pointer `a1`, marked internal in the backend's constraint file. GCC 14 has no
vector register class at all and no `q` register in `REGISTER_NAMES`, so the
allocator cannot place a value in `q0` to `q7` and there is nothing to
declare.[^asm-leaf]

**[measured]** `__asm__("mov %0, %1" : "=q"(y) : "r"(x))` on an `int` is
rejected with `error: inconsistent operand constraints in an 'asm'`.[^tc-q]

The practical consequence is the opposite of a limitation: because no C value
can ever live in a `q` register, a block using PIE needs no clobber list for
them. What it does owe is the coprocessor state, and `SAR` and `SAR_BYTE`,
which have no constraint name either, so a block that sets a shift amount and
the block that consumes it must be one block.[^asm-leaf]
Detail: [GCC extended inline assembly on the Xtensa LX7](../04-toolchain-and-codegen/gcc-extended-inline-asm-on-xtensa.md)
and [the PIE register file, SAR and context](../02-pie-vector/pie-register-file-sar-and-context.md).

### 9. The compiler will vectorize with the right flags

**False. GCC 14.2 cannot print a PIE mnemonic.** The backend has no vector
patterns, no `__builtin_xtensa` intrinsics, and the shipped `cc1` contains no
string beginning `ee.`; the assembler learns PIE from the dynconfig and the
compiler never learns it at all.[^headers-leaf]

**[measured]** At `-O3` a byte-wise add loop does report "loop vectorized
using 4 byte vectors", which is four bytes packed into one 32-bit address
register, not a vector instruction. The resulting assembly contains zero
`ee.*` instructions.[^tc-vec] Every PIE instruction in an ESP32-S3 binary
came from hand-written assembly or from a library that already contains it.
Detail: [where the headers and the tools disagree](../04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do.md).

## The memory system

### 10. Prefetch or lock the table in cache

**Half true, and not through the instructions you know.** The Xtensa cache
instructions do not exist on this target. The core configuration reports the
Data Cache Option itself as absent (`XCHAL_DCACHE_SIZE` 0,
`XCHAL_HAVE_PREFETCH` 0),[^core-isa] and the toolchain was built without the
mnemonics.

**[measured]** `dpfr` and the rest of that family fail with
`Error: unknown opcode or format name 'dpfr'`.[^tc-cache] You cannot reach
them even from hand-written assembly.

What the chip does have is a separate cache peripheral with its own preload,
lock and occupy operations, reached through ROM functions such as
`Cache_Start_DCache_Preload` and `Cache_Lock_Addr`. Nearly all of them carry
the header comment "Please do not call this function in your SDK
application", and this repo's search of ESP-IDF 5.5.1 found no caller of the
lock functions anywhere in the framework, so there is no worked example of a
safe calling convention for lock.[^cachectl-leaf] Treat preload as usable and
unmeasured, and lock as a mechanism the SDK deliberately does not surface.
Detail: [cache control from software](../03-memory-hierarchy/cache-control-preload-lock-and-writeback.md).

### 11. PSRAM runs at 120 MHz, so it is fast

**Misleading on three counts.** First, the ESP-IDF default for this chip is
quad mode at 40 MHz, not octal at 120.[^psram-kconfig] Second, 120 MHz octal
is an experimental feature whose own Kconfig help says that after a
temperature swing of roughly 20 degrees Celsius "the accesses to / from PSRAM
will crash randomly".[^psram-kconfig] Third, even the fast configuration is
slow next to internal SRAM: the peak data-phase arithmetic gives 20 MB/s for
the default quad 40 MHz and 160 MB/s for octal DDR at 80 MHz, and those are
ceilings that ignore command, address and read latency
([caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)).

The number that matters for a kernel is the floor per miss. At a 32-byte line
on octal DDR 80 MHz, "a gather that misses on every pixel cannot beat about
48 cycles per pixel ... whatever the loop body does"
([caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)).
Flash and PSRAM also share the MSPI bus, so a loop reading a PSRAM table
competes with instruction fetch from flash.

### 12. `memcpy` is a C loop you can beat

**Mostly false.** On the ESP32-S3, `memcpy` and its siblings are absolute ROM
addresses assigned by a linker fragment whenever the target declares
`ESP_ROM_HAS_NEWLIB`, so no library member supplying `memcpy` is ever linked.
The ROM body is newlib's Xtensa algorithm, a `loopnez` moving 16 bytes an
iteration with an `SSA8L`/`SRC` misaligned path.[^flags-leaf] A hand loop
beats it only in three narrow cases: a short constant length, a length and
alignment that make its tail handling dead weight, or a throughput need that
only PIE can meet.[^loopshapes-leaf]
Detail: [loop shapes, scheduling and per-call setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md).

## The toolchain

### 13. The FPU has divide and square root

**False**, and the header that suggests otherwise is the trap. The core
configuration sets `XCHAL_HAVE_FP_DIV`, `XCHAL_HAVE_FP_SQRT` and
`XCHAL_HAVE_FP_RSQRT` all to 1.[^core-isa] Those macros mean the seed
instructions exist, not that a one-instruction divide does.

**[measured]** `sqrt.s` and `div.s` fail with `unknown opcode or format name`,
while `sqrt0.s` and `recip0.s`, the begin-then-refine seeds, assemble
fine.[^tc-fp] And GCC does not use the seeds anyway: `a/b` on floats compiles
to `call8 __divsf3` and `__builtin_sqrtf(a)` to `call8 sqrtf`.[^tc-fp] Both
are windowed library calls costing tens of cycles. Precompute a reciprocal
where you can; do not put a runtime float divide in a pixel loop.
Detail: [the floating-point option on the LX7](../01-scalar-isa/floating-point-option-on-lx7.md).

### 14. The core configuration header says what the compiler will emit

**False, and it fails in both directions.** The FPU macros above overstate the
hardware. `CLAMPS` understates it: ESP-IDF's header sets `XCHAL_HAVE_CLAMPS`
to 1, the assembler accepts `clamps`, and the compiler's own predefined macro
`__XCHAL_HAVE_CLAMPS` is 0, so GCC has the pattern, refuses to select it, and
lowers a saturating clamp to `MIN` plus `MAX`
instead.[^headers-leaf][^tc-cache] Read the header for what the silicon may
have, and the disassembly for what you actually got.
Detail: [where the headers and the tools disagree](../04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do.md).

### 15. `-O3` or `-funroll-loops` always helps

**False.** ESP-IDF's Kconfig offers no `-O3` at all, so getting it on a
component takes deliberate work.[^flags-leaf] `-funroll-loops` is not part of
`-O2` or `-O3` by itself, only of profile-guided builds, and GCC's own manual
says of it that it "makes code larger, and may or may not make it run
faster"; of the stronger form it says plainly that it "usually makes programs
run more slowly".[^gcc-unroll] The measurement in myth 4 above is one
instance: on this target the flag cost a hardware loop.

### 16. `-mcpu=esp32s3` selects the core

**False. There is no such option.** The selector is `-mdynconfig=`, which the
chip's compiler driver passes for you; the dynconfig shared object is where
the assembler learns the PIE opcodes and where the CLAMPS disagreement above
lives.[^headers-leaf] A build script that adds a plausible-looking `-mcpu`
will be rejected, and one that adds nothing is already correct.

### 17. `volatile` makes timing deterministic

**False in both senses people mean it.** On the code side, `asm volatile`
stops GCC discarding a block whose outputs are unused and stops it hoisting
the block out of a loop. It is not a memory barrier and it does not pin the
block between neighbouring statements: GCC's own manual shows an addition
moving back across a `volatile` block.[^asm-leaf] Use a `"memory"` clobber for
the barrier, and an artificial data dependency for ordering.

On the timing side, nothing in the C language makes an interval on this chip
deterministic. A FreeRTOS tick interrupt can arrive up to `configTICK_RATE_HZ`
times a second, a coprocessor-disabled exception can fire on a contended
handoff, and the other core is running independently.[^rtos-leaf] The answer
is min-of-n over repeated runs, which is built to discard exactly those
events, not a keyword.
Detail: [what the runtime does to a kernel's measured time](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md).

## Measurement

### 18. A QEMU cycle count is a device cycle count

**False, twice over.** QEMU's `CCOUNT` is not a count of work: the helper
samples the virtual clock and converts nanoseconds to ticks, so instructions
executed in between never enter the arithmetic. The frequency is wrong for
the part as well, because the ESP32-S3 core config sets `clock_freq_khz` to
40000, so `CCOUNT` advances as though the core ran at 40 MHz rather than
240.[^qemu-leaf] And the whole memory hierarchy that decides a real kernel's
speed is absent: the cache device models address translation, with no wait
states, no miss penalty and no bus arbitration.[^qemu-leaf] A kernel whose
real cost is table placement looks identical in QEMU whichever memory the
table is in. `-icount` does not fix this; QEMU's own documentation says it
"does not provide cycle accurate emulation".[^qemu-leaf]

What QEMU is genuinely for here is bit-exactness: running a kernel against a
portable reference and proving the outputs match.
Detail: [what QEMU proves and what it cannot](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md).

### 19. The FreeRTOS tick is CCOUNT

**False on this chip.** `CCOUNT` is a cycle counter, and the Xtensa
`CCOMPARE` timers can drive a tick, but ESP-IDF only defaults to that on the
ESP32 and ESP32-S2. `FREERTOS_TICK_SUPPORT_CORETIMER` is `default y if
IDF_TARGET_ESP32 || IDF_TARGET_ESP32S2`, and everything else, the ESP32-S3
included, defaults to `FREERTOS_CORETIMER_SYSTIMER_LVL1`.[^tick-kconfig] So
the tick is a systimer interrupt, and `CCOUNT` is free for your own
measurement.

Two related traps stay true. `CCOUNT` is per core, and ESP-IDF rescales it
when the CPU frequency changes, so a delta spanning a frequency switch or a
light-sleep entry is wrong; hold the frequency fixed while
measuring.[^ccount-leaf] And `esp_timer_get_time` is the correct clock across
those events but has about 240 cycles of granularity at 240 MHz, so it is a
whole-frame instrument, not a kernel one.[^ccount-leaf]
Detail: [CCOUNT and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md).

## Open questions

- **Whether a taken branch really costs exactly 2 cycles in every case.**
  Both primary sources agree on 2, but neither covers the fetch-boundary case
  the ISA manual raises separately, where a branch target crossing a fetch
  boundary starts "three cycles after the branch instead of two".[^isa-pipeline]
  `[uncertain]` how often GCC 14's own target alignment avoids that. A
  `CCOUNT` loop over aligned and deliberately misaligned branch targets on
  real hardware would settle it.
- **What preload actually buys.** No published throughput number for a
  hardware preload against an ordinary demand miss was found, so myth 10's
  "usable" verdict is permissive rather than evidenced. `[uncertain]`. A
  `CCOUNT`-timed sweep over a PSRAM-resident table, with and without
  `Cache_Start_DCache_Preload`, would settle it.
- **Whether `-O3` would help any kernel here.** The claim above is only that
  ESP-IDF does not offer it and that unrolling specifically hurt one loop.
  `[uncertain]` whether `-O3` without `-funroll-loops` is a net win on a
  kernel that is not hardware-loop bound. A per-file A/B on the device would
  settle it.
- **Whether PIE inside an ISR is supported.** The FPU escape hatch
  (`CONFIG_FREERTOS_FPU_IN_ISR`) gates shared assembly that would mechanically
  cover a PIE exception too, but Espressif documents the option only for the
  FPU.[^cpenable-leaf] `[uncertain]`. An Espressif statement or a documented
  test would settle it. Until then, treat PIE in an ISR as unsupported.

## Sources

[^tc]: Toolchain and framework under test throughout this file: `xtensa-esp-elf-gcc` 14.2.0 (crosstool-NG `esp-14.2.0_20241119`) with GNU binutils 2.43.1, invoked as `xtensa-esp32s3-elf-gcc` and `xtensa-esp32s3-elf-as`; ESP-IDF 5.5.1 (`tools/cmake/version.cmake`, major 5, minor 5, patch 1). All commands run 2026-09-06.
[^isa-pipeline]: Cadence/Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference Manual*, issue 4/2010, release RC-2010.1, Section 8.4.2 "Xtensa Processor Family", pp. 608-609, including Table 8-247 and Figure 8-56. Verbatim: "capable of executing at most one instruction per cycle"; "Instructions that depend on load instruction results must issue two cycles after the load"; "there are two fetched fall-through instructions that are killed on taken branches"; and, on fetch boundaries, "the target instruction will begin three cycles after the branch instead of two". Copy consulted: https://0x04.net/~mwk/doc/xtensa.pdf
[^trm-hazard]: Espressif Systems, *ESP32-S3 Technical Reference Manual*, version 1.8, Section 1.7.3 "Control Hazard", p. 74: "the instructions at the R and E stages on the pipeline will be removed, which means the pipeline remains stagnant for 2 cycles".
[^core-isa]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`. Read 2026-09-06, values verified by grep: line 50 `XCHAL_HAVE_WINDOWED 1`, line 51 `XCHAL_NUM_AREGS 64`, line 56 `XCHAL_HAVE_LOOPS 1`, line 57 `XCHAL_LOOP_BUFFER_SIZE 256`, line 62 `XCHAL_HAVE_CLAMPS 1`, line 73 `XCHAL_HAVE_PREDICTED_BRANCHES 0`, line 131 `XCHAL_HAVE_FP_DIV 1`, line 133 `XCHAL_HAVE_FP_SQRT 1`, line 247 `XCHAL_DCACHE_SIZE 0`, line 252 `XCHAL_HAVE_PREFETCH 0`.
[^trm-ldxq]: TRM[^trm-hazard], Sections 1.8.37 `EE.LDXQ.32` (p. 113) and 1.8.69 `EE.STXQ.32` (p. 145), as summarised in [PIE load, store and alignment](../02-pie-vector/pie-load-store-and-alignment.md): "There is no multi-lane gather: `EE.LDXQ.32` picks the 16-bit lane of `qs` named by the second immediate".
[^trm-shift]: TRM[^trm-hazard], PIE instruction listing, as summarised in [PIE arithmetic, multiply, saturate and shuffle](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md): "There is no 8-bit or 16-bit lane shift at all", with `EE.VSL.32` and `EE.VSR.32` the only lane shifts, both taking `SAR[5:0]`.
[^loops-leaf]: [Zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md): "**Body size: 256 bytes.** `LEND` is `PC + 4 + imm8` with an unsigned 8-bit [immediate]".
[^spills-leaf]: [Register pressure, spills and reading the assembly](../04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md), which measures a 4x-unrolled accumulator loop at 6.25 instructions per element with 2 spills and no hardware loop, against 4.5 per element with 0 spills and a `loop` at 2x.
[^tc-unroll]: `[measured]` 2026-09-06, toolchain[^tc]. Source: `void f(unsigned short *d, const unsigned short *s, int n, unsigned short k){ for (int i = 0; i < n; i++) d[i] = (unsigned short)(s[i] + k); }`. Compiled four ways with `-mlongcalls -S`, counting lines matching `^\sloop` and instruction lines: `-O2` gives 1 hardware loop and 14 instructions; `-O2 -funroll-loops` gives 0 and 84; `-O3` gives 1 and 14; `-O3 -funroll-loops` gives 0 and 84.
[^tc-q]: `[measured]` 2026-09-06, toolchain[^tc]. `int f(int x){ int y; __asm__("mov %0, %1" : "=q"(y) : "r"(x)); return y; }` compiled with `-O2` gives `error: inconsistent operand constraints in an 'asm'`.
[^tc-vec]: `[measured]` 2026-09-06, toolchain[^tc]. `void addb(unsigned char *d, const unsigned char *s, int n){ for(int i=0;i<n;i++) d[i]=(unsigned char)(d[i]+s[i]); }` at `-O3 -mlongcalls`. `-fopt-info-vec` reports "loop vectorized using 4 byte vectors"; the generated `.s` file contains 0 lines matching `ee\.` out of 87.
[^tc-cache]: `[measured]` 2026-09-06, toolchain[^tc], `xtensa-esp32s3-elf-as` one instruction per file. Rejected with "Error: unknown opcode or format name": `dpfr`, `dpfl`, `ipf`. Accepted: `clamps a2, a3, 7`.
[^tc-fp]: `[measured]` 2026-09-06, toolchain[^tc]. Assembler: `sqrt.s f0, f1` and `div.s f0, f1, f2` give "Error: unknown opcode or format name"; `sqrt0.s f0, f1` and `recip0.s f0, f1` assemble. Compiler: `float dv(float a, float b){ return a/b; }` and `float sq(float a){ return __builtin_sqrtf(a); }` at `-O2 -mlongcalls -S` emit `call8 __divsf3` and `call8 sqrtf`.
[^cpenable-leaf]: [Coprocessors, CPENABLE and lazy context switching on the ESP32-S3](../00-foundations/coprocessors-cpenable-and-lazy-context.md): "`CPENABLE` must never be written directly by C code, a hand-written kernel, or inline assembly running under FreeRTOS", and, on the ISR question, "treat PIE in an ISR as unsupported".
[^asm-leaf]: [GCC extended inline assembly on the Xtensa LX7](../04-toolchain-and-codegen/gcc-extended-inline-asm-on-xtensa.md): "on Xtensa `q` is the stack pointer, not a PIE vector register"; "The assembler knows the instructions; the compiler does not know the registers exist"; and on `volatile`, "It is not a memory barrier and it does not pin the block between neighbouring statements".
[^headers-leaf]: [Where the Xtensa headers and the tools disagree](../04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do.md), summary table and Sections 1, 2 and 5: `CLAMPS` present in the header and 0 in `__XCHAL_HAVE_CLAMPS`; `div.s`/`sqrt.s` not real; `-mcpu=esp32s3` does not exist and `-mdynconfig=` is the selector; "GCC 14.2's Xtensa backend has no vector instruction patterns, no vector builtins, and no way to print a PIE mnemonic".
[^cachectl-leaf]: [Cache control from software: preload, lock and writeback](../03-memory-hierarchy/cache-control-preload-lock-and-writeback.md): "This search found no caller of either function anywhere in the ESP-IDF 5.5.1 component tree, so there is no worked example, in the framework itself, of a safe calling convention for locking from application code".
[^psram-kconfig]: ESP-IDF 5.5.1, `components/esp_psram/esp32s3/Kconfig.spiram`, read 2026-09-06: line 13 `default SPIRAM_MODE_QUAD`, line 85 `default SPIRAM_SPEED_40M`, and the `SPIRAM_SPEED_120M` help text at lines 96 to 102, "Octal PSRAM 120 MHz is an experimental feature, it works when the temperature is stable ... the accesses to / from PSRAM will crash randomly".
[^flags-leaf]: [GCC 14 Xtensa flags and what they cost](../04-toolchain-and-codegen/gcc14-xtensa-flags-and-what-they-cost.md): "On the ESP32-S3, `memcpy` and friends are ROM addresses from a linker fragment, not ESP-IDF code. Kconfig has no `-O3`."
[^loopshapes-leaf]: [Loop shapes, scheduling and per-call setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md): "A hand loop beats it only for a short constant length, a length and alignment that make the tail handling dead weight, or a throughput need only PIE can meet."
[^gcc-unroll]: Free Software Foundation, *GCC* 14.2.0, "Optimize Options": `-funroll-loops` "makes code larger, and may or may not make it run faster", enabled by `-fprofile-use` and `-fauto-profile` rather than by `-O2` or `-O3`; `-funroll-all-loops` "usually makes programs run more slowly". https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Optimize-Options.html
[^rtos-leaf]: [What the runtime does to a kernel's measured time](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md), on the tick arriving "up to 100 times" a second at the default rate and on the coprocessor exception being paid "at most once per contended handoff ... exactly what `min-of-n` is built to discard".
[^qemu-leaf]: [What QEMU proves and what it cannot](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md): "CCOUNT therefore advances as if the core ran at 40 MHz, not 240 MHz. A CCOUNT delta from QEMU is not the device's cycle count and is not even a fixed multiple of it"; and, quoting QEMU's own "Invocation" documentation for `-icount`, it "does not provide cycle accurate emulation".
[^tick-kconfig]: `[measured]` ESP-IDF 5.5.1, `components/freertos/Kconfig`, read 2026-09-06 by grep and by reading lines 470 to 530. `FREERTOS_TICK_SUPPORT_CORETIMER` is `default y if IDF_TARGET_ESP32 || IDF_TARGET_ESP32S2`; `FREERTOS_TICK_SUPPORT_SYSTIMER` is `default y if !FREERTOS_TICK_SUPPORT_CORETIMER`; the `FREERTOS_CORETIMER` choice is `default FREERTOS_CORETIMER_SYSTIMER_LVL1 if FREERTOS_TICK_SUPPORT_SYSTIMER`; and `FREERTOS_SYSTICK_USES_CCOUNT` is `default y if FREERTOS_CORETIMER_0 || FREERTOS_CORETIMER_1`. The ESP32-S3 is neither ESP32 nor ESP32-S2, so it takes the systimer path.
[^ccount-leaf]: [CCOUNT and timing a kernel on the ESP32-S3](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md), on per-core counters, the rescale on frequency change ("Re-calculate the ccount to make time calculation correct"), and `esp_timer_get_time` granularity of "about 240 CPU cycles at 240 MHz".