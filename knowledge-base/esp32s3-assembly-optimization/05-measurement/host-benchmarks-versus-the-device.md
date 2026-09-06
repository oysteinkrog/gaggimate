---
title: "Host benchmarks versus the device"
id: 05-measurement/host-benchmarks-versus-the-device
schema_version: 1
doc_type: explanation
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, measurement, benchmarking, host-vs-device, ooo, cache]
confidence: medium
---

# Host benchmarks versus the device

A kernel written for the ESP32-S3's Xtensa LX7 core is usually also compiled
and run on a laptop or CI machine, because a host build is instant and a
device flash is not. The timing from that host run is real, but it measures
a different machine. This page explains why the two numbers can disagree,
sometimes by a large factor and sometimes in the opposite direction, and
what a host run is still worth doing.

The rule: **a host time can locate cost; only the device can size it.** Use
the host to find which part of a kernel a change touches and whether the
output stays correct. Use the device, or at minimum a cycle-accurate
emulator, to find out whether the change is faster.

## 1. In-order, single-issue versus out-of-order, superscalar

The LX7 issues at most one instruction per cycle, in program order, through
a fixed 5-stage pipeline: fetch, decode, execute, memory access, write back.
The manual states "it is ideal that CPU issues one instruction onto the
pipeline per processor cycle" and "the processor cannot issue an instruction
to the pipeline until all the operands and hardware resources required for
the operation are ready" [Espressif2026a][^1]. When a later instruction
needs a value an earlier one has not produced, the core stalls the whole
pipeline (an interlock) until the value is ready, or forwards it early if
the timing lines up [Espressif2026a][^1]. Nothing lets the core skip ahead
to independent work while it waits.

A laptop or desktop x86-64 core does the opposite. Every core since the
mid-1990s, except the original Pentium and Pentium MMX, splits each
instruction into micro-operations and schedules them onto multiple execution
units as soon as their inputs are ready, regardless of program order
[AgnerFog2026][^2]. If a load misses the cache, the core finds later,
independent instructions whose inputs are already available and runs those
instead, retiring everything in original order once the load completes
[AgnerFog2026][^2]. This is the main reason a load-use stall or a short
dependency chain can cost real cycles on the LX7 and cost close to nothing
on a host: the host has other work to fill the gap, the device does not. A
microbenchmark that isolates one dependent chain is close to a worst case on
the device and a best case for exposing cost on the host; timing several
independent chains at once on the host and dividing by the count
systematically understates per-chain cost on an in-order target.

## 2. A windowed register view versus a renamed physical file

The LX7's Windowed Register option gives each function 16 visible registers
(`a0`-`a15`), a window into a file of up to 64 physical registers; a call
rotates the window, and it spills to memory only when call depth exceeds
what the physical file holds [Espressif2021a][^3]. Whatever a kernel does
inside one function, it has 16 registers to work with: running out means a
real spill, visible in the assembly as extra load/store instructions on the
critical path.

An x86-64 core exposes 16 architectural registers too, but that number
describes the instruction encoding, not the hardware. Every write to a
logical register is renamed onto a fresh physical register, so two writes to
the same architectural name can execute out of order without conflicting
[AgnerFog2026][^2]. The physical file behind those 16 names is large: AMD's
Zen 1 integer core holds 168 physical 64-bit registers, Zen 2 holds 180
[AgnerFog2026][^4]. A kernel with more live values than 16 registers often
still fits in that physical file on x86-64 with no spill at all; the same
kernel on the LX7 spills for real, because there is nowhere to rename to. A
register-pressure cliff on the device can be invisible in a host profile of
the identical source.

## 3. Divide, square root, and double precision

A host FPU executes IEEE 754 add, subtract, multiply, divide, and square
root, single- and double-precision, as single instructions (or, vectorized,
one AVX instruction over 4 or 8 doubles). The LX7's optional single-precision
floating-point coprocessor covers the arithmetic operations, but Cadence's
own instruction-set summary for the Xtensa LX family lists "Divide and
Square Root Sequences" as a topic separate from the coprocessor's
instruction list [CadenceISA2026][^7], consistent with those two operations
not being single hardware instructions and instead needing a short software
sequence to reach an IEEE-correct result.

This is confirmed, not merely suspected. The ESP32-S3 core configuration
enables `div0.s`, `divn.s`, `recip0.s`, `sqrt0.s`, `rsqrt0.s`, `nexp01.s`
and `maddn.s`, a begin-then-Newton-Raphson-refine family, but has no single
divide or square-root instruction (`div.s` does not exist on this core).
GCC 14.2 does not use the seed-and-refine sequence for a plain `float`
divide or square root: `a / b` compiles to `call8 __divsf3` and
`sqrtf(x)` to `call8 sqrtf`, a windowed library call of 30
instructions for the divide (`entry`, 28 FPU and integer instructions, `retw.n`), `[measured]` on this toolchain. See
[the floating-point option leaf](../01-scalar-isa/floating-point-option-on-lx7.md)
for the seed instructions and their use in a hand-written refinement
sequence, and
[the cost-model leaf](../00-foundations/lx7-core-pipeline-and-cost-model.md)
for the compiled call sequence and its instruction count. Treat divide and
square root on the LX7 as materially more expensive than an add or
multiply, and do not assume the host's single-instruction latency
transfers: on the host both are single hardware instructions, and on the
device both are library calls unless a kernel hand-writes the seed-and-refine
sequence itself.

Double precision is a sharper trap because it changes silently. Espressif's
own developer documentation states "all currently available [Espressif]
FPUs support single-precision only... any `double` calculations are still
handled in software" [Espressif2025a][^5], and GCC's Xtensa backend
documents a floating-point target option that the fused-multiply-add flag
depends on, confirming the hardware path is optional and single-precision
in the compiler's own model [GCC2026][^6]. A host has hardware double
precision throughout, so a stray `double` (a literal missing an `f` suffix,
an implicit promotion, a widened intermediate) costs nothing extra on the
host and can even be faster there than explicit `float`, while the same
source on the LX7 quietly falls into library calls for every such operation.
A host comparison between two kernel versions differing only in where a
`double` crept in can show the wrong version winning.

## 4. Cache and memory bandwidth, two or three orders of magnitude apart

The LX7's instruction cache configures to 16 KB or 32 KB and its data cache
to 32 KB or 64 KB, each backed by a small internal SRAM block that can also
serve as plain memory instead of cache [Espressif2026a][^8]. Behind that
cache sits SPI PSRAM: the in-package PSRAM chip itself is rated for a
maximum clock of 80 MHz [Espressif2026b][^9], the figure to use for a
default build. ESP-IDF also offers a 120 MHz `CONFIG_SPIRAM_SPEED` option,
stable for quad-mode PSRAM but marked experimental for octal mode, with a
documented risk of random access failures after a roughly 20-degree-Celsius
temperature swing; see
[caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)
for the full 40/80/120 MHz picture and the bandwidth arithmetic that
follows from it. Either way the bus is shared with flash and the DMA
engines. A
laptop or desktop CPU has two or three cache levels sized in the hundreds of
kilobytes to tens of megabytes, backed by DRAM delivering tens of gigabytes
per second, roughly two to three orders of magnitude more than the LX7's
PSRAM path. [experience]

A lookup table of a few kilobytes is a guaranteed hit in a host's
first-level cache regardless of access pattern, so a random gather from it
costs the same as a sequential read once warm. The same table on the device
competes for a cache an order of magnitude smaller, shared between
instructions and data, backed by a bus an order of magnitude slower than
host DRAM; a table a host benchmark reports as free can be the dominant
cost on the device once real miss traffic counts. Table placement is a
decision the host cannot inform at all: it has no equivalent of PSRAM
contention, no instruction cache sized in tens of kilobytes, and no table
competing with running code for the same cache.

## 5. SIMD width and gather

A host built with AVX2 has 256-bit vectors and dedicated gather instructions
that load several non-contiguous elements, indexed by an offset vector, in
one instruction; these are "efficient for gathering non-contiguous data into
vectors, and for vectorizing table-based lookup functions" from Haswell on,
improving further on Broadwell [AgnerFog2026][^10]. A table-driven kernel
vectorized with a gather has no true equivalent on the LX7's PIE vector
extension: narrower 128-bit vectors and no multi-lane gather instruction.
`EE.LDXQ.32` loads one 32-bit lane by an index taken from a q register,
one lane and one instruction at a time, not several non-contiguous
elements in a single instruction; see
[PIE load, store and alignment](../02-pie-vector/pie-load-store-and-alignment.md)
for the instruction. So the same algorithm must be restructured into
scalar-address, vector-arithmetic form, or into a sequence of one-lane
indexed loads, to use the vector unit. A host build that auto-vectorizes a
table lookup into a gather is measuring an execution strategy the device
cannot use in one instruction, which makes a direct timing comparison
meaningless for that kernel shape even before clock speed and cache
differences enter.

## 6. It is not the same compiler either

The same GCC frontend can target a host's x86-64 and the device's
`xtensa-esp-elf`, but each target is a separate backend with its own
scheduler, register allocator, and inlining heuristics, tuned to that
target's pipeline model. [experience] A scheduling reorder that helps GCC's
x86-64 backend fill gaps for an out-of-order core does nothing for the
Xtensa backend, which has no reordering hardware and instead lives or dies
on whether the compiler's static instruction order avoids interlocks;
conversely, a transform the Xtensa backend rewards, such as recognizing a
counted-down loop and emitting a zero-overhead hardware loop, has no host
equivalent. A change that improves one target's generated assembly is not
evidence about the other target's, and must be checked in that target's own
disassembly.

## 7. What a host run is still good for

- **Counting library calls per element.** A shim around `__adddf3`,
  `__divsf3`, and similar soft-float entry points, linked into the host
  build, counts exactly how many such calls a kernel makes per element,
  independent of how fast the host executes them. The count carries to the
  device even though the per-call cost does not.
- **Algorithmic complexity and relative attribution.** Which stage of a
  multi-stage kernel dominates, and how that scales with problem size, is
  usually the same shape on both machines even when the constants differ.
- **Fidelity and golden comparison.** Whether a rewritten kernel matches the
  reference output, bit for bit or within a stated tolerance, is a
  correctness question the host answers as well as the device, and faster.
- **Fuzzing with sanitizers.** AddressSanitizer and UndefinedBehaviorSanitizer
  find out-of-bounds reads, alignment violations, and undefined overflow on
  the host in seconds; these are correctness bugs on either machine, and
  far cheaper to find on the host than by a device crash.
- **Fast iteration on the reference implementation.** A portable reference
  kept alongside any hand-tuned version is worth developing on the host
  because rebuilds are fast; it is what later stages are checked against
  for correctness, not for speed.

## 8. The evidence ladder between host and device

Two intermediate checks sit between a laptop run and a board run:

1. **The cross-compiler's own assembly.** Building with the actual
   `xtensa-esp-elf` GCC and reading the emitted assembly shows, for that
   compiler version and flag set, how many instructions the loop body
   takes, whether a register spilled, whether a counted loop became a
   hardware loop, and whether a float operation became a libcall. An
   instruction count is not a cycle count, but it is a fact about the code
   the device will run, which a host x86-64 build of the same source is
   not.
2. **A cycle-accurate emulator.** Running the cross-compiled binary under an
   emulator that models the Xtensa instruction set, rather than translating
   to host instructions, checks the kernel's output for bit-exactness
   against a reference without a device. This answers correctness, not
   speed, unless the emulator also models pipeline timing.

Only a device measurement, or an emulator that models cycles, answers how
many cycles a kernel actually takes under real memory contention, real
cache state, and the real clock. That is the only number in this ladder
that sizes a cost; the host and a plain functional emulator only locate one.

## 9. Known reversal patterns

Recurring shapes, not device-specific numbers; check every row against the
actual target rather than applying it blindly. [experience] throughout,
since none of these come from a citable manual section.

| Pattern | Typical host result | Typical in-order device result | Why they diverge |
|---|---|---|---|
| Unrolling so more iterations' work is live at once | Faster: more independent chains for the out-of-order core to overlap | Neutral or slower: more live values than the 16-register window holds, forcing spills | The host's renamer absorbs extra live state; the in-order core has no such reserve |
| Replacing an indexed array with named locals | Neutral to faster: the renamer gives each local its own physical register | Faster only if it fits the window, otherwise unchanged or slower | Naming does not add registers, only whether the compiler keeps a value in one |
| Shrinking a table and computing more per element | Slower: the original table already fit a large cache for free | Can be faster: the smaller table now fits in a small cache or reserved fast memory | The host's cache was never the bottleneck; on the device it can be the whole bottleneck |
| Counting a loop down to zero instead of up | Neutral: no matching loop hardware to trigger | Can be a clear win: the compiler emits a zero-overhead hardware loop | The optimization exists only on the target with hardware for it |

## Footnotes

[^1]: Espressif Systems, *ESP32-S3 Technical Reference Manual*, v1.8,
    Chapter 1 "Processor Instruction Extensions (PIE)," Section 1.7
    "Instruction Performance," Table 1.7-1, Figure 1.7-1.
    `https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf`
    (fetched 2026-09-06).

[^2]: Agner Fog, *The microarchitecture of Intel, AMD, and VIA CPUs*, last
    updated 2026-05-23, Chapter 2 "Out-of-order execution (All processors
    except P1, PMMX)," Sections 2.1-2.2, pp. 11-13.
    `https://www.agner.org/optimize/microarchitecture.pdf` (fetched
    2026-09-06).

[^3]: Espressif Systems, *Overview of Xtensa Instruction Set Architecture*,
    v0021604, 2021-02-17, Sections 1.2-1.3 (Windowed Register option: 64
    physical AR registers behind a 16-register visible window).
    `https://dl.espressif.com/github_assets/espressif/xtensa-isa-doc/releases/download/latest/Xtensa.pdf`
    (fetched 2026-09-06).

[^4]: Agner Fog, same manual as [^2], Chapter 22 "AMD Zen 1-2 pipeline,"
    Section 22.8 "Register renaming and out-of-order schedulers," p. 234
    (168 physical integer registers on Zen 1, 180 on Zen 2).

[^5]: Espressif Developer Portal, "Floating-Point Units on Espressif SoCs:
    Why (and when) they matter," 2025-10.
    `https://developer.espressif.com/blog/2025/10/cores_with_fpu/` (fetched
    2026-09-06).

[^6]: GNU Compiler Collection manual, "Xtensa Options," `-mfused-madd` entry:
    "Enable or disable use of fused multiply/add and multiply/subtract
    instructions in the floating-point option. This has no effect if the
    floating-point option is not also enabled."
    `https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Xtensa-Options.html`
    (fetched 2026-09-06), the version matching the `xtensa-esp-elf` GCC
    14.2.0 toolchain this KB cites elsewhere. The flag predates this
    version too: same entry present in the GCC 9.1.0 revision of the page.
    The unversioned `.../gcc/Xtensa-Options.html` tracks the newest GCC
    release and no longer carries this flag as of 2026-09-06; cite the
    versioned URL, not the rolling one.

[^7]: Cadence Design Systems, *Xtensa Instruction Set Architecture (ISA)
    Summary for all Xtensa LX Processors*, contents entry "Divide and
    Square Root Sequences" under the Floating-Point Coprocessor Option.
    `https://www.cadence.com/content/dam/cadence-www/global/en_US/documents/tools/silicon-solutions/compute-ip/isa-summary.pdf`.
    [uncertain]: the document returned a bot-detection page on fetch
    2026-09-06; only the section title, from a search index, is confirmed.

[^8]: Espressif Systems, *ESP32-S3 Technical Reference Manual*, v1.8,
    Chapter 4 "System and Memory," Sections 4.3.2 and 4.3.3.2 "Cache."
    `https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf`
    (fetched 2026-09-06).

[^9]: Espressif Systems, *ESP32-S3 Series Datasheet*, v2.2, Table 5-12
    "PSRAM Specifications" (maximum clock frequency 80 MHz).
    `https://documentation.espressif.com/esp32-s3_datasheet_en.pdf` (fetched
    2026-09-06).

[^10]: Agner Fog, same manual as [^2], Chapter 10 "Intel Haswell and
    Broadwell pipeline," "Execution ports and execution units," pp. 149-150.
