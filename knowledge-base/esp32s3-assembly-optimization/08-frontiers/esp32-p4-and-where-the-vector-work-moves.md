---
title: "ESP32-P4: what a move to RISC-V does to hand-written kernel work"
id: 08-frontiers/esp32-p4-and-where-the-vector-work-moves
schema_version: 1
doc_type: explanation
status: draft
last_reviewed: 2026-09-06
tags: [esp32p4, riscv, pie, simd, hardware-loop, toolchain, psram, cache, frontier]
confidence: medium
---

# ESP32-P4: what a move to RISC-V does to hand-written kernel work

If a hot kernel written for the ESP32-S3's Xtensa LX7 and its PIE vector unit
has to run on the RISC-V ESP32-P4, what must be rewritten, what retuned, and
what of the method survives untouched.

The vector unit is still called PIE, still 128 bits wide, still integer only,
and still has eight vector registers, so a kernel's design carries over. How
you write it down changes completely: mnemonic prefix, calling convention,
loop instruction, how the unit is enabled, which compiler will assemble it.
The memory hierarchy changes more than the compute does, in the kernel
writer's favour. The P4 documents available on 2026-09-06 are pre-release and
the chapter specifying the PIE instructions is marked "[to be added later]"
[Espressif 2026c][^p4trm], so several claims below rest on vendor assembly.

## 1. The core, as documented

| Property | ESP32-S3 | ESP32-P4 | Source |
|---|---|---|---|
| Cores | 2 Xtensa LX7 | 2 RISC-V HP, plus 1 RISC-V LP core | [Espressif 2026a][^s3ds] p. 3; [Espressif 2026b][^p4ds] §4.1.1.1, §4.1.1.4 |
| Clock ceiling | 240 MHz | 400 MHz per the datasheet, 360 MHz per the TRM `[uncertain]` | [Espressif 2026a][^s3ds] p. 3; [Espressif 2026b][^p4ds] §4.1.1.1; [Espressif 2026c][^p4trm] §1.2 |
| Pipeline | five stage, in order | five stage, in order, scalar | [Espressif 2026a][^s3ds] p. 3; [Espressif 2026c][^p4trm] §1.1 |
| Branch handling | no predictor documented | BHT plus BTB plus RAS | [Espressif 2026c][^p4trm] §1.2, §1.12.1 |
| Base ISA | Xtensa, windowed registers | RV32IMAFC plus Zc (Zcb, Zcmp, Zcmt) | [Espressif 2026b][^p4ds] §4.1.1.1; [Espressif 2026c][^p4trm] §1.2 |
| Vector unit | PIE, `ee.*` mnemonics | PIE, `esp.*` mnemonics | [Espressif 2026c][^p4trm] §1.7.2; [esp-dsp][^espdsp] |
| Hardware loop | `LOOP` family, in the core ISA | separate custom extension | [Espressif 2026c][^p4trm] §1.7.1 |

Naming will confuse a document search. The datasheet calls the custom
extensions `XespV` and `XespLoop` in §4.1.1.1 and `Xai` and `Xhwlp` in
§4.1.1.3 [Espressif 2026b][^p4ds]; the toolchain uses neither, calling the
vector extension `xesppie`, which is also the `-march` suffix
[ESP-IDF][^idfgdb]. `[uncertain]` which is canonical; the `-march` string a
released P4 build passes would settle it. The clock figures disagree the same
way, probably by chip revision, though neither document says so `[uncertain]`.

The branch predictor is the one pipeline change that alters scheduling
judgement. The LX7 cost model treats a taken branch as a fixed penalty (see
[LX7 core pipeline and cycle cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md));
on the P4 a mispredicted branch flushes the pipeline instead. The TRM gives
that as the reason the hardware loop exists: it needs no "prediction of result
or target address" and so "has no penalty" [Espressif 2026c][^p4trm] §1.7.1.3.
A loop with an unpredictable trip count therefore costs differently on the two
parts, invisibly to an instruction count.

## 2. PIE on the P4: same shape, bigger accumulator, no manual

The TRM describes a superset of the S3's unit: eight 128-bit integer vector
registers usable as 16 by 8, 8 by 16 or 4 by 32 bits; loads and stores up to
128 bits straight to and from them; fused arithmetic-plus-memory instructions;
vector and scalar MAC over 16 8-bit or 8 16-bit lanes per cycle; a 512-bit
vector accumulator and a 40-bit scalar one; 16-bit FFT and complex-maths
instructions; configurable rounding, scaling shift and saturation
[Espressif 2026c][^p4trm] §1.7.2.2. Against the S3's PIE, in
[PIE register file, state registers and context save](../02-pie-vector/pie-register-file-sar-and-context.md),
the shapes match one for one. Only the vector accumulator grew: two 160-bit
halves on the S3, two 256-bit halves on the P4 [Espressif 2024][^piepost]. A
kernel bounded by 160 bits of accumulation gets headroom for free.

Unaligned 128-bit vector load and store is a documented P4 feature
[Espressif 2026b][^p4ds] §4.1.1.3, where the S3's 128-bit forms mask the low
address bits instead (see
[PIE load and store instructions and their alignment rules](../02-pie-vector/pie-load-store-and-alignment.md)).
Espressif's P4 kernels switch it on by reading a configuration register,
setting bit 1 and writing it back, commented "Enable analigned data access"
[esp-dsp][^espdsp]; that register's semantics are unpublished, so the bit is
`[uncertain]`. The unit also gains 128 bits of state named `UA_STATE` that
ESP-IDF saves with the vector registers [ESP-IDF][^idfport]; the name suggests
unaligned-access state, but no source examined says so `[uncertain]`. ESP-IDF
still sets the P4's preferred SIMD alignment to 16 bytes [ESP-IDF][^idfcaps],
so unaligned support is a convenience, not licence to stop aligning.

**What is missing is the instruction reference.** The TRM's PIE section ends
by pointing at a chapter marked "[to be added later]", and its progress table
shows that chapter unwritten [Espressif 2026c][^p4trm] §1.7.2.2. This
knowledge base's S3 rule is that instruction semantics need the manual or an
executed test. On the P4 the manual rung is absent, so the evidence is
Espressif's own assembly, the assembler's willingness to encode a mnemonic,
and execution on silicon. P4 kernel work starts one rung lower. The gap is not
total: the hardware loop is specified down to encodings, and both extensions'
state and enable registers are documented [Espressif 2026c][^p4trm] §1.7.1.6
to §1.7.2.3.

## 3. The hardware loop is not a drop-in for `LOOP`

Xtensa's `LOOP` family ships with the core, needs no enabling, and imposes
only a loop-buffer size limit (see
[Zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md)). The P4's is a
separate extension with a real rule set [Espressif 2026c][^p4trm] §1.7.1:

| Property | Value |
|---|---|
| Nesting | two loops, Loop0 is the inner one and has priority |
| Minimum body | six 32-bit instructions, or twelve 16-bit ones |
| Last instruction | not 16-bit, not atomic, not a Zc push, pop, popret or jt form |
| Branches inside | not supported |
| Alignment | start and end addresses must be word aligned |
| Penultimate slot | no CSR access and no `FENCE`, since both flush the pipeline |
| Enable | `mhwloop_state_reg.STATE` non-zero, else illegal instruction; `URW` for user mode |

The six-instruction minimum changes kernel shape: a tight S3 loop of two or
three fused vector instructions has no legal P4 equivalent until unrolled far
enough, and an S3 loop with an inner early exit must be restructured rather
than translated. Documented mnemonics are `lp.setupi`, `lp.setup`,
`lp.starti`, `lp.endi`, `lp.count` and `lp.counti`, each taking the loop index
first [Espressif 2026c][^p4trm] Table 1.7-2. Espressif's assembly writes them
with an `esp.` prefix and a label, as in `esp.lp.setup 0, t5, .main_loop`,
where the label marks the *last* instruction executed [esp-dsp][^espdsp].
Early silicon has a defect: ESP-IDF carries `SOC_CPU_HAS_HWLOOP_STATE_BUG`,
"HWLOOP state doesn't go to DIRTY after executing the last instruction of a
loop", with a workaround guarded on chip revision 1 or below
[ESP-IDF][^idfcaps][^idfport].

## 4. Enabling the unit, and the rule that does not change

On the S3, FPU and PIE are coprocessors enabled lazily per task through
`CPENABLE`, and the standing rule is that a kernel never writes `CPENABLE`
itself (see
[Coprocessors, CPENABLE, and lazy context switching](../00-foundations/coprocessors-cpenable-and-lazy-context.md)).
The P4 is the same idea in RISC-V clothing. PIE is off at reset and enabled
through `mext_pie_status.STATE`; a PIE instruction while it is off raises an
illegal instruction with `mcause` 0x2 and sets `mext_ill.PIE_ILL`. The state
field uses the OFF, INITIAL, CLEAN, DIRTY encoding, hardware moves it to DIRTY
on any PIE instruction, and a context switch consults it to decide whether to
save [Espressif 2026c][^p4trm] §1.7.2.3. ESP-IDF implements exactly that:
three coprocessors, PIE and the hardware loop among them, each with its own
save area, lazily switched, and a task using one from an interrupt handler
hits `vPortCoprocUsedInISR` [ESP-IDF][^idfcaps][^idfport].

So the rule carries over with the register renamed. A production kernel does
not write `mext_pie_status`, for the same reason it does not write `CPENABLE`:
that bypasses the mechanism saving another task's vector state. The saved
context is eight vector registers, four 128-bit pieces of the accumulator
halves, `UA_STATE`, and a packed word holding the scalar accumulator with
`SAR`, `SAR_BYTES` and `FFT_BIT_WIDTH` [ESP-IDF][^idfport]. One further trap:
a fused load-and-arithmetic instruction naming one destination for both halves
is ambiguous and raises a PIE illegal exception with `mcause` 0x1f.

## 5. What an S3 PIE kernel becomes

| Layer | On the S3 | On the P4 |
|---|---|---|
| Calling convention | windowed ABI, `entry` and `retw` (see [Register windows and the windowed ABI](../00-foundations/register-windows-and-windowed-abi.md)) | standard RISC-V: arguments `a0` to `a7`, return `a0`, callee-saved `s0` to `s11`, explicit stack adjustment, `ret` |
| Loop | `loopnez` or `loopgtz`, body may be one instruction | `esp.lp.setup`, under the six-instruction floor and no-branch rule above |
| Mnemonics | `ee.` prefix, scalar accumulator spelled `accx` | `esp.` prefix, scalar accumulator spelled `xacc` |
| Unaligned spans | scalar prefix, or a `SAR`-based shift-merge | the unaligned load and store support, once its configuration bit is understood |
| Arithmetic | lane widths, fixed-point formats, accumulate-then-shift-and-saturate readout, table gathers | unchanged |

The first row is mechanical and is where a port's transcription bugs will
live. Reading both sets of kernels in one library, the P4 side uses
`esp.vld.128.ip`, `esp.vst.128.ip`, `esp.vldbc.{8,16,32}.ip`,
`esp.vadd.{s8,s16,u8,u16}`, `esp.vsub.s16`, `esp.vmul.s16`,
`esp.vmulas.{s8,s16,u8,u16}.xacc.ld.ip`, `esp.cmul.s16`, `esp.vzip.32`,
`esp.vunzip.32`, `esp.zero.{qacc,xacc}` and `esp.srs.{s,u}.xacc`. The S3 side
also uses forms with no P4 counterpart there: `ee.src.q` and relatives, the
funnel shift that fakes unaligned vector loads, and the `.qup`
register-rotating MAC variants [esp-dsp][^espdsp]. That is one library's
usage, not an ISA diff. `[uncertain]` whether the P4 lacks those forms or has
no kernel needing them.

## 6. esp-dsp has P4 kernels, and its benchmarks are mixed

Espressif ships hand-written P4 assembly under the `_arp4` suffix, dispatched
by target the way the S3's `_aes3` files are (see
[Espressif's esp-dsp as a reference kernel library](../06-kernel-patterns/esp-dsp-as-a-reference-kernel-library.md)).
At commit `3c8ac0f` there are 21 such files covering dot products, image dot
products, FFT, matrix multiply, biquad and FIR [esp-dsp][^espdsp]. Unlike
several S3 files, which are kept compiler output, the P4 files are written by
hand. The published cycle-count table at `-O2` [Espressif 2026d][^dspbench]:

| Function | ESP32-S3 | ESP32-P4 |
|---|---|---|
| `dsps_dotprod_s16`, 256 points | 307 | 208 |
| `dsps_dotprod_f32`, 256 points | 432 | 1319 |
| `dsps_fft2r_sc16`, 64 points | 774 | 897 |
| `dsps_fft4r_fc32`, 256 points | 13213 | 18052 |
| `dspm_mult_s16`, 16 by 16 | 2004 | 2138 |
| `dspm_mult_f32`, 16 by 16 | 6280 | 28276 |

Fixed-point kernels are competitive or better; floating-point kernels are much
worse. The source shows why: P4 float kernels use scalar RV32F instructions
such as `fmadd.s` inside a hardware loop, with no vector instruction at all,
while the S3 float kernels use PIE's 64-bit paired float loads to feed the FPU
[esp-dsp][^espdsp]. Consistently, the TRM calls the P4 vector registers
integer registers and lists no floating-point vector operation
[Espressif 2026c][^p4trm] §1.7.2.2. `[uncertain]` whether the P4 PIE has any
float vector capability; the missing chapter would settle it on paper, a
portable-C twin timed against the assembly on silicon in practice.

Two cautions. The page gives cycle counts and no clocks, so they are not wall
time [Espressif 2026d][^dspbench]. At the datasheet ceilings of 240 and
400 MHz the s16 dot product's 208 against 307 cycles would be about a 2.5
times wall-clock win and the f32's 1319 against 432 about a 1.8 times loss,
but that assumes clocks the source does not give. And a row where the P4 sits
near the portable-C RISC-V column is probably one where dispatch fell through
to portable C, which the table does not distinguish from a slow kernel.

## 7. Memory: the constraint that bounded S3 kernels is looser

For most real kernels the S3's binding constraint is not the ISA but the
memory hierarchy: 16 KB of instruction cache, 32 KB of data cache, and flash
and PSRAM sharing one MSPI bus, so flash-resident code on one core steals
bandwidth from PSRAM reads on the other (see
[Caches, internal SRAM, PSRAM and the shared MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)).
Every one of those numbers improves.

| Property | ESP32-S3 | ESP32-P4 | Source |
|---|---|---|---|
| L1 instruction cache | 16 KB | 16 KB, 64 B lines, 4-way | [Espressif 2026b][^p4ds] §4.1.3.3 |
| L1 data cache | 32 KB | 64 KB, 64 B lines, 2-way, write-through or write-back | [Espressif 2026b][^p4ds] §4.1.3.3 |
| L2 cache | none | 128, 256 or 512 KB, 8-way, 64 or 128 B lines | [Espressif 2026b][^p4ds] §4.1.3.3 |
| Internal code and data memory | 512 KB SRAM | 768 KB L2MEM at 200 MHz, plus 8 KB scratchpad at CPU clock | [Espressif 2026a][^s3ds]; [Espressif 2026b][^p4ds] §4.1.3.1 |
| PSRAM | shares MSPI with flash | in-package, 16-bit DDR at up to 250 MHz, up to 64 MB | [Espressif 2026b][^p4ds] §4.1.3.1 |
| Flash and PSRAM buses | one shared MSPI | separate flash MSPI and PSRAM MSPI controllers | [Espressif 2026c][^p4trm] clock, reset and interrupt-map registers |

The last row matters most. The P4's clock and reset registers name
`RST_EN_MSPI_AXI` for flash and `RST_EN_DUAL_MSPI_AXI` for PSRAM as separate
resets, and the interrupt map carries `FLASH_MSPI_INTR` and `PSRAM_MSPI_INTR`
as separate sources [Espressif 2026c][^p4trm]. Two controllers, not one, so
the S3 failure mode where cached code fetched from flash contends with a
kernel's PSRAM reads has no obvious P4 equivalent. The datasheet works its
PSRAM bandwidth formula to 8 Gbit/s theoretical, from 16 data lines times
double-edge sampling times 250 MHz [Espressif 2026b][^p4ds] §4.1.3.1, a
ceiling and not a measurement.

What this does to the table-placement rule is open. On the S3, whether a hot
lookup table lives in internal SRAM or PSRAM is worth more than most
instruction-level work, and the answer is nearly always SRAM. The P4 has more
internal memory, twice the L1 data cache, an L2 behind it, and a faster
uncontended path to PSRAM. Whether a per-pixel table must still be pinned is
the kind of claim only a device measurement settles, and none is available.

## 8. What of the method survives

The ladder in
[Bit-exact reference tests and fuzzing](../05-measurement/bit-exact-reference-tests-and-fuzzing.md)
and
[Host benchmarks versus the device](../05-measurement/host-benchmarks-versus-the-device.md)
transfers almost intact, because almost none of it is Xtensa-specific.

**Untouched.** A portable C twin of every kernel with bit-exact agreement
required. Host-generated goldens. Fuzzing the twin with sanitizers on. Timing
assembly and twin back to back on the device under production conditions
rather than inferring a speedup. Distrusting host timings and instruction
counts as predictors, which matters more on a core with a branch predictor and
an L2, since both add ways for a host model to be wrong.


**With a substitution.** Xtensa's `CCOUNT` becomes `mcycle` in machine mode or
`cycle` through `rdcycle` in user mode, both documented for the P4's HP core
[Espressif 2026c][^p4trm] §1.5.1, and ESP-IDF's `esp_cpu_get_cycle_count()`
already forwards to whichever a target uses. The HP core also has three event
counters with selectors, `mhpmcounter8`, `mhpmcounter9` and `mhpmcounter13`,
but this TRM revision does not say what they can count `[uncertain]`. The
low-power core's table is documented and includes memory-access and
instruction-fetch wait cycles [Espressif 2026c][^p4trm] §3.7. If the HP
selectors reach comparable events that is a real gain over the S3, where a
stall must be inferred from a cycle count rather than counted.

**Lost.** The emulator rung. Espressif's QEMU fork has machines for the
ESP32-C3 and ESP32-C6 under `hw/riscv`, and its most recent release notes
describe continuing ESP32-S3 vector emulation work with no mention of the P4
[espressif/qemu][^qemu]. As of 2026-09-06 there is no P4 machine in that tree,
so a P4 kernel cannot be proven bit-exact in emulation before hardware.

## 9. The toolchain will assemble it, and that is all

ESP-IDF's P4 test sources name the vector extension `xesppie` and keep its
assembly in `.S` files [ESP-IDF][^idfgdb]. Evidence on what the compiler
offers beyond assembling those mnemonics is thin and consistent. Espressif's
introduction to P4 PIE shows inline `asm volatile` and standalone `.S` files
as the two usage routes, with no intrinsics and no automatic vectorisation
mentioned [Espressif 2024][^piepost]. Every P4 vector kernel in `esp-dsp` is
hand-written assembly, and a search of that repository found no intrinsics
header [esp-dsp][^espdsp]. ESP-IDF's P4 capability header carries the comment
"PIE coprocessor assembly is only supported with GCC compiler" directly above
`SOC_CPU_HAS_PIE` [ESP-IDF][^idfcaps]; Clang is otherwise a supported ESP-IDF
toolchain for the P4, so the LLVM assembler does not encode these instructions
as of that commit.

The position in 2026 matches the S3: the compiler will not generate vector
code for you, so a kernel is written by hand or not at all. On the P4 you also
give up Clang if you write one. `[uncertain]` whether GCC's auto-vectoriser
can target `xesppie`; compiling a vectorisable loop at `-O3` with the P4
`-march` string and inspecting the assembly would settle it in one command.
That was not run here: the RISC-V toolchain on this machine is GCC 8.4.0,
which predates the part.

## Open questions

1. Is the vector extension canonically `Xai`, `XespV` or `xesppie`?
2. What is the HP core's real clock ceiling, and does it vary by chip revision?
3. Does the P4 PIE have any floating-point vector capability? Every published
   float kernel is scalar and the TRM calls the registers integer registers.
4. What does the configuration bit set before unaligned access control, and
   what does it cost?
5. What events can the HP core's three counters be programmed to count?
6. Does the hot-table placement rule still hold with a 64 KB L1 data cache, an
   L2, and a PSRAM controller that no longer shares pins with flash?
7. Will a P4 machine appear in Espressif's QEMU fork, restoring the emulation
   rung?

## Sources

[^s3ds]: Espressif Systems, *ESP32-S3 Series Datasheet*, version 2.2. Page 3
    for cores, clock and pipeline; the internal memory list for the 512 KB of
    on-chip SRAM. https://documentation.espressif.com/esp32-s3_datasheet_en.pdf

[^p4ds]: Espressif Systems, *ESP32-P4 Series Datasheet*, Pre-release v0.7.
    Sections cited inline. Fetched 2026-09-06, converted with `pdftotext`.
    https://documentation.espressif.com/esp32-p4_datasheet_en.pdf

[^p4trm]: Espressif Systems, *ESP32-P4 Chip Revision v1.3 Technical Reference
    Manual*, Pre-release v0.4. Sections cited inline; §3.7 is the LP core's.
    The contents list marks "Processor Instruction Extensions [to be added
    later]". The MSPI reset and interrupt registers were found by text search
    of the same conversion. Fetched 2026-09-06, converted with `pdftotext`.
    https://documentation.espressif.com/esp32-p4-chip-revision-v1.3_technical_reference_manual_en.pdf

[^piepost]: Yan Ke, Espressif Systems, "Explore the PIE capabilities on the
    ESP32-P4", Espressif Developer Portal, 5 December 2024. Accumulator widths
    for both parts, the `q0` to `q7` registers, and inline assembly plus `.S`
    files as the two usage routes. Fetched 2026-09-06.
    https://developer.espressif.com/blog/2024/12/pie-introduction/

[^espdsp]: Espressif Systems, `esp-dsp`, commit
    `3c8ac0fdfec83740b783e200862c8d0c056de0ad` (2026-05-12), local clone.
    `find modules -name '*_arp4.S'`; `dsps_dotprod_s16_arp4.S` for the
    argument mapping and the unaligned-access configuration write;
    `dsps_dotprod_f32_arp4.S` for the scalar `fmadd.s` body;
    `dsps_fird_s16_arp4.S` for nested loops. Mnemonic inventories from
    `grep -rhoE "esp\.[a-z0-9._]+" --include=*.S .` and the same for `ee.`.
    https://github.com/espressif/esp-dsp

[^dspbench]: Espressif Systems, "Espressif DSP Library Benchmarks", `esp-dsp`
    documentation, latest, the `-O2` columns. Fetched 2026-09-06. https://docs.espressif.com/projects/esp-dsp/en/latest/esp32/esp-dsp-benchmarks.html

[^idfcaps]: Espressif Systems, ESP-IDF,
    `components/soc/esp32p4/include/soc/soc_caps.h`, commit `df2192b3`
    (2026-09-04): `SOC_CPU_COPROC_NUM`, `SOC_CPU_HAS_HWLOOP_STATE_BUG` with
    its comment, `SOC_CPU_HAS_PIE` with the preceding GCC-only comment, and
    `SOC_SIMD_PREFERRED_DATA_ALIGNMENT`. https://github.com/espressif/esp-idf

[^idfport]: Espressif Systems, ESP-IDF,
    `components/freertos/FreeRTOS-Kernel/portable/riscv/portasm.S`, commit
    `94b526c9` (2026-05-07): `generate_coprocessor_routine` and its abort via
    `vPortCoprocUsedInISR`; the `pie_save_regs` and `hwlp_save_regs` register
    lists; the revision-guarded hardware-loop workaround. The state encoding
    of CSR 0x7F2 is in `components/riscv/include/riscv/csr_pie.h`, commit
    `55acc5e5`. https://github.com/espressif/esp-idf

[^idfgdb]: Espressif Systems, ESP-IDF. `xesppie` appears as a filename and
    identifier under `components/esp_gdbstub/` and
    `tools/test_apps/system/gdbstub_runtime/`. Located by GitHub code search
    on 2026-09-06. https://github.com/espressif/esp-idf

[^qemu]: Espressif Systems, `qemu` fork, branch `esp-develop`: listing of
    `hw/riscv` (machines for `esp32c3` and `esp32c6`, none for the P4) and the
    release notes for `esp-develop-9.2.2-20260417`. Retrieved 2026-09-06.
    https://github.com/espressif/qemu
