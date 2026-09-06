---
title: "ESP32-S3 assembly optimization: the compendium"
id: compendium
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, pie, compendium, synthesis, index]
confidence: high
sources: [xtensa-isa-rm-2010, esp32s3-trm-v18, esp32s3-datasheet-v22, esp-idf-5.5.1, gcc-14.2-xtensa, esp-dsp-1.8.2, qemu-espressif]
---

# ESP32-S3 assembly optimization: the compendium

The one file to open first. It carries the forty facts from this corpus most
likely to change what you do to a kernel, in bucket order, each linking the leaf
that holds it. No fact here carries its own footnote, so read the leaf before you
rely on a number. Facts corrected in the September 2026 verification pass give
the corrected version and link
[the contradiction log](./09-adversarial/contradiction-log-sources-that-disagree.md).

## 00 Foundations: the core, the ABI, the memory map

1. **The LX7 is in-order, single-issue and five stages deep, and its cost model is an
   issue distance rather than a latency.** An instruction that uses a value in stage SB
   issues no earlier than `max(SA - SB + 1, 0)` cycles after the one that defines it in
   SA. [TRM 1.7] [the cost model](./00-foundations/lx7-core-pipeline-and-cost-model.md)
2. **A load defines its result one stage after an arithmetic instruction uses it, so a
   dependent instruction stalls one cycle unless an independent instruction sits between
   them.** That single gap is why most hand-written kernels here interleave two streams.
   [TRM 1.7] [the cost model](./00-foundations/lx7-core-pipeline-and-cost-model.md)
3. **A taken branch costs two cycles and there is no branch predictor.** The chip manual
   gives the number; an earlier draft stated it in fetch slots and used a deeper core's
   stage numbers. [TRM 1.7]
   [the cost model](./00-foundations/lx7-core-pipeline-and-cost-model.md),
   [branch costs](./01-scalar-isa/branches-jumps-and-control-flow-costs.md),
   [the correction](./09-adversarial/contradiction-log-sources-that-disagree.md)
4. **The windowed ABI leaves fourteen allocatable address registers in a leaf function,
   and after a `call8` the return value arrives in the caller's `a10` to `a13`, not `a2`
   to `a5`.** Return values rotate with the window as arguments do; the earlier draft
   had this wrong. [GCC 14.2]
   [register windows](./00-foundations/register-windows-and-windowed-abi.md),
   [the correction](./09-adversarial/contradiction-log-sources-that-disagree.md)
5. **Never write `CPENABLE` from application code, and never assume vector register
   contents survive a call.** The runtime saves coprocessor state lazily on the disabled
   exception and tracks one owner per coprocessor per core, and every vector and
   floating-point register here counts as caller-saved. [ESP-IDF 5.5.1]
   [coprocessors and lazy context](./00-foundations/coprocessors-cpenable-and-lazy-context.md)

## 01 Scalar ISA: the core without the vector unit

6. **The core configuration header says which ISA options this chip got, and the shipped
   assembler is the tiebreak when a header and a tool disagree.** The header is the
   authority on what exists, the assembler on what your code may contain.
   [ESP-IDF 5.5.1]
   [configured options](./01-scalar-isa/core-isa-and-configured-options.md)
7. **The zero-overhead loop takes a body of at most 256 bytes, cannot nest and cannot
   end in a call, and GCC 14 emits only the plain form, never at `-O0`.** Any inline
   assembly inside the body removes the loop, so an `asm` block placed for speed can
   cost the loop that was free. [ISA RM] [GCC 14.2]
   [hardware loops](./01-scalar-isa/zero-overhead-loops.md)
8. **An oversize loop body is not refused by the compiler; the assembler relaxes it into
   a nine-instruction entry sequence that destroys the loop count register.** Neither
   compiler pass tests the body's byte size, which the earlier draft got wrong.
   [GCC 14.2]
   [reading the assembly](./04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md),
   [the correction](./09-adversarial/contradiction-log-sources-that-disagree.md)
9. **The chip has `CLAMPS` and GCC 14.2 is configured as though it does not, so a
   saturating clamp costs a `min` and a `max` instead of one instruction.** Reaching it
   needs inline assembly. [measured]
   [headers versus tools](./04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do.md),
   [MAC16 options](./01-scalar-isa/mac16-boolean-and-other-configured-options.md)
10. **There is no `div.s` and no `sqrt.s`: float divide, `sqrtf` and every `double`
    operation are library calls, and only the refinement seed instructions exist.** GCC
    does emit `madd.s` for `a * b + c` by default. [ISA RM] [GCC 14.2]
    [the FP option](./01-scalar-isa/floating-point-option-on-lx7.md)

## 02 The PIE vector unit

11. **PIE is coprocessor 3 with eight 128-bit `q` registers, and GCC 14.2 has no
    register class, no name and no builtin for any of them.** You cannot name a `q`
    register as an operand or a clobber, `vector_size(16)` compiles to four scalar adds,
    and hand-written assembly is the only route to the unit. [TRM 1.5] [GCC 14.2]
    [PIE registers](./02-pie-vector/pie-register-file-sar-and-context.md)
12. **Every PIE memory access forces the low address bits to zero before the access and
    never traps.** A misaligned pointer silently reads or overwrites the aligned block
    below it, which is why a vector table is allocated aligned rather than checked at
    runtime. [TRM 1.8] [measured]
    [PIE load and store](./02-pie-vector/pie-load-store-and-alignment.md),
    [code and data placement](./04-toolchain-and-codegen/code-and-data-placement-in-esp-idf.md)
13. **There is no multi-lane gather.** `EE.LDXQ.32` loads one 32-bit lane using one
    16-bit lane as the index, so a table-driven kernel stays scalar in its lookup step
    whatever the rest of it does. [TRM 1.8]
    [PIE load and store](./02-pie-vector/pie-load-store-and-alignment.md)
14. **Multiplies and loads define their result one stage late, while adds, compares,
    shifts, zips and the funnel shift chain freely.** The whole published timing model
    reduces to that one question, and every entry in it is an interlock, so a bad
    schedule is slow and never wrong. [TRM 1.7]
    [PIE hazards](./02-pie-vector/pie-hazards-latencies-and-issue-rules.md)
15. **Saturation is the exception, not the rule, and there is no 8-bit or 16-bit lane
    shift, no select, no and-not and no vector divide.** Only instructions whose
    description mentions saturation clamp, and a narrow lane shift is written as a
    multiply by a power of two, which is exact. [TRM 1.8]
    [PIE arithmetic](./02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md)

## 03 Memory hierarchy

16. **The default geometry is a 16 KB instruction cache and a 32 KB data cache, eight
    ways, 32-byte lines, both shared by the two cores.** External flash and PSRAM are
    reachable only through two cache windows, and that data cache is the working-set
    budget for a kernel's tables. [ESP-IDF 5.5.1] [TRM 4.3]
    [caches and the MSPI bus](./03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md),
    [the memory map](./00-foundations/esp32s3-memory-map-and-address-spaces.md)
17. **Flash and PSRAM share one bus, so code fetched from flash and data read from PSRAM
    compete, and a flash write disables both caches on both cores.** A timing taken
    while another task writes flash measures the stall, not the kernel. [TRM 4.3]
    [caches and the MSPI bus](./03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md),
    [runtime effects](./08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md)
18. **No cache-miss cycle cost is published for flash or for PSRAM.** The datasheet's
    peak pin bandwidth gives a line-fill floor of roughly 24 to 96 cycles per line at
    240 MHz, which is a floor and not a miss cost. [uncertain]
    [caches and the MSPI bus](./03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)
19. **This core configures no Xtensa cache instructions at all, and the eighteen
    architectural cache mnemonics do not assemble.** Preload, lock, writeback and
    invalidate live only in the chip's own cache peripheral, reached through ROM
    functions and the cache sync call. [measured]
    [cache control](./03-memory-hierarchy/cache-control-preload-lock-and-writeback.md)

## 04 Toolchain and codegen

20. **ESP-IDF gives you `-O2` at best, with link-time optimization compiled in and then
    switched off, no jump tables, and `-mlongcalls` on every file.** There is no `-O3`
    in the configuration menu, so the `-O2` schedule is a kernel's baseline.
    [ESP-IDF 5.5.1]
    [the IDF flag set](./04-toolchain-and-codegen/gcc14-xtensa-flags-and-what-they-cost.md)
21. **`memcpy` on this chip is an absolute symbol at a boot ROM address, assigned by a
    linker fragment, and never occupies the instruction cache.** The earlier draft said
    it resolved to a linked library object, and neither of the two files that settle it
    had been opened. [ESP-IDF 5.5.1]
    [the IDF flag set](./04-toolchain-and-codegen/gcc14-xtensa-flags-and-what-they-cost.md),
    [the correction](./09-adversarial/contradiction-log-sources-that-disagree.md)
22. **`SAR`, the byte shift register, the loop registers and the vector registers have
    no clobber name, so an `asm` block that touches them owns them silently and a
    comment is the only record.** A walking pointer needs `+r`, a temporary written
    before the last input is read needs `=&r`, and a store needs the memory clobber.
    [measured]
    [inline asm](./04-toolchain-and-codegen/gcc-extended-inline-asm-on-xtensa.md)
23. **Read the compiler's own assembly for the loop before writing any, looking for four
    faults: stack references in the body, a lost hardware loop, a library call, and a
    load next to its consumer.** Three of the four are cheaper to fix in C than to
    replace with hand assembly. [GCC 14.2]
    [reading the assembly](./04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md)

## 05 Measurement

24. **`CCOUNT` is per-core, wraps in about 17.9 seconds at 240 MHz, and moves with the
    CPU frequency.** Frequency scaling, light sleep and a start-up rescale all change
    it, so a reading compares only inside one power state on one core. [ISA RM]
    [ESP-IDF 5.5.1]
    [CCOUNT](./05-measurement/ccount-cycle-counter-and-timing-a-kernel.md)
25. **Minimum of n is the right statistic for deterministic compute and the wrong one
    for cache-miss-bound code, where the mean and the spread are the honest report.** A
    minimum over repeats hides exactly the misses that dominate a table-driven kernel.
    [experience] [CCOUNT](./05-measurement/ccount-cycle-counter-and-timing-a-kernel.md)
26. **The emulator times nothing: its cycle counter tracks a 40 MHz virtual clock and
    its cache model has no wait states.** It proves what a kernel computes and never
    what it costs, and six open vector-divergence issues cover saturation, shift counts
    and destination aliasing. [measured] [uncertain]
    [what QEMU proves](./05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md),
    [QEMU versus silicon](./09-adversarial/qemu-versus-silicon-known-and-suspected-divergences.md)
27. **Keep a portable reference twin beside every hand-written kernel and treat the twin
    as the specification when the two disagree.** Fuzz the pair with the sanitizers on:
    without them an overrun of a padded table reads a plausible neighbouring byte and
    passes. [experience]
    [bit-exact tests and fuzzing](./05-measurement/bit-exact-reference-tests-and-fuzzing.md)

## 06 Kernel patterns

28. **The multiply you need follows from where the binary point sits, and `Q15 * Q15`
    overflows for exactly one input pair.** Use the 16-bit multiply with a shift when
    the product fits 32 bits, the high multiply when the shift lands at the register
    boundary, and the funnel shift when it lands inside. [ISA RM]
    [fixed point](./06-kernel-patterns/fixed-point-arithmetic-on-lx7.md),
    [scalar arithmetic](./01-scalar-isa/scalar-arithmetic-shifts-and-bit-tricks.md)
29. **One 32-bit multiply weights all three channels of an RGB565 pixel at once, when
    the pixel is duplicated into both halves of a word and masked.** Green is the
    binding field, so the weight caps at 32, and an exhaustive host check found no
    mismatch against per-channel arithmetic. [measured]
    [LUT gathers and RGB565](./06-kernel-patterns/lut-gathers-palettes-and-rgb565.md)
30. **A two-way interleave fits inside the fourteen usable address registers and a
    four-way one often spills, which puts the load-use stall straight back on the
    reload.** Unrolling here buys scheduling room rather than loop overhead, and the
    256-byte body cap bounds it. [experience]
    [loop shapes](./06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md)

## 07 What this repository's kernel pass proved

31. **A pixel budget is arithmetic you can do before writing anything: a full-screen 30
    fps target on this chip works out to a few dozen cycles per pixel for the whole
    render path.** Frame time then quantises to the display's own period, so missing the
    period by a little costs a whole frame. [measured]
    [the frame budget](./07-our-work/frame-budget-and-measured-band-costs.md)
32. **In the one recorded pass over thirteen hand-written kernels the device reversed
    the host's verdict on four of them.** Clear wins reached 2.0x and the worst loss ran
    at 0.73x of the compiler's own code, and every verdict came from an A/B on hardware
    at the end of a four-rung ladder. [measured]
    [what the device decided](./07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md),
    [the reversals](./07-our-work/host-versus-device-reversals-measured-here.md),
    [the verification ladder](./07-our-work/verification-ladder-host-asm-qemu-device.md),
    [host versus device](./05-measurement/host-benchmarks-versus-the-device.md)
33. **The same kernel ran 1.3x to 2x slower with its tables in PSRAM than in internal
    SRAM.** Table placement is therefore a decision made in source, ranked by reads per
    frame against a fixed internal budget and settled on the device with a hot-load
    bench. [measured]
    [table placement](./07-our-work/hot-slab-table-placement-and-the-dram-budget.md),
    [the hot-load rig](./07-our-work/kblob-hot-loading-kernels-on-the-device.md)
34. **A fuzz run with the sanitizers on found a real table overrun that every earlier
    test had passed.** The padding on a lookup table had been sized against the
    amplitude the code usually used rather than the maximum its own producer allows.
    [measured]
    [the palette pad defect](./07-our-work/fuzzing-the-fleet-and-the-silk-palette-pad.md)

## 08 Frontiers

35. **Espressif's Clang fork already has the full vector instruction set, a chip
    definition and 252 builtins, 213 of them vector ones, where upstream LLVM has an
    experimental target with none of that.** Nobody has measured the fork against GCC 14
    on a kernel, so it is a possibility rather than an option. [uncertain]
    [LLVM and Clang](./08-frontiers/llvm-clang-for-xtensa-status-and-what-it-changes.md)
36. **Every codegen claim in this corpus is a claim about GCC 14.2, and ESP-IDF 6.0 and
    6.1 move to 15.2.** Every vector claim is about this chip's unit, which the ESP32-P4
    replaces with one whose instruction reference is unpublished. [ESP-IDF 5.5.1]
    [GCC lineage](./08-frontiers/gcc-xtensa-lineage-and-what-newer-releases-bring.md),
    [the ESP32-P4](./08-frontiers/esp32-p4-and-where-the-vector-work-moves.md)
37. **The vendor's neural-network and deep-learning libraries hold the largest body of
    hand-written ESP32-S3 vector assembly available to read.** Two rules from their
    authoring guide travel: never place a kernel in instruction RAM, and always ship an
    aligned and an unaligned entry point. [esp-dsp 1.8.2]
    [vendor kernel libraries](./08-frontiers/esp-dsp-esp-nn-and-vendor-kernel-libraries.md),
    [esp-dsp as reference](./06-kernel-patterns/esp-dsp-as-a-reference-kernel-library.md)

## 09 Adversarial: what to distrust

38. **Fewer instructions is not faster on this core.** An in-order, single-issue
    pipeline with a load-use gap and no predictor pays for stalls and misses, and
    unrolling by flag removed a hardware loop and grew a fourteen-instruction function
    to eighty-four. [measured]
    [myths](./09-adversarial/myths-about-optimizing-for-this-core.md)
39. **Inside the chip manual, an instruction entry's description and operation section
    outrank its syntax line and the surrounding prose.** Four defects follow that
    pattern, two of them wrong immediate ranges. When an entry disagrees with itself,
    the assembler settles a range and the emulator settles semantics. [TRM 1.8] [measured]
    [the contradiction log](./09-adversarial/contradiction-log-sources-that-disagree.md)
40. **The corpus carries 146 open uncertainties, and the largest group is
    per-instruction latency for the scalar and floating-point instructions.** Cadence
    does not publish the data book that would give them, so that group closes with a
    measurement campaign or not at all. [uncertain]
    [the uncertainty register](./09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md)

## How to use this topic

Work in three steps. First query the collection: `qmd query "<question>" -c
esp32s3-assembly-optimization-kb --limit 10`, with `-c` always pinned, asking in
the words of the problem rather than the words of the answer. Then read the
bucket README, which indexes its leaves with their key claims and ends with what
the bucket knows it is missing. Then read the leaf, which carries the citation,
the caveat and the experiment, none of which survive the trip into this file.

Someone new to the chip reads buckets 00 to 02, then 03 and 04, then 05 before
touching a kernel; [the topic README](./README.md) lists them in that order.

**The citation tags.** Every claim carries one, and the short tag beside each fact
above names the kind of source the leaf cites.

| Tag | What it means for you |
|---|---|
| A source and year with a footnote | A primary document says it. Follow the footnote if the decision is expensive. |
| `[measured]` | A number from this repository's hardware or from the emulator, with the commit, file or log named. Real, and one setup. |
| `[experience]` | An observation with no formal source behind it. Useful, not citable. |
| `[uncertain]` | Nobody here could confirm it against a primary source. An open question, never a fact. |
| `[obvious]` | Stated without a citation on purpose. |

**The 07 rule.** [07-our-work](./07-our-work/README.md) is the only bucket that
names this repository's files, tools, rigs and measurements, and everything else
is usable on any ESP32-S3 project. A claim that needs a repository file name to
be true belongs in 07; a claim that does not must not carry one.

**Check 09 before you trust a number.**
[09-adversarial](./09-adversarial/README.md) attacks the rest of the corpus: the
contradiction log says where two sources disagree and which won, the myths leaf
tests seventeen pieces of received advice, the emulator leaf lists how a
bit-exact pass can be wrong, and the register says what nobody here knows.

## What the corpus does not know

The gaps are recorded rather than hidden, and they are concentrated. The
[uncertainty register](./09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md)
holds all 146 open tags from buckets 00 to 08 in ten groups, each with the
experiment or the document that would settle it. Four groups hold most of them:
scalar and floating-point latencies, vector-unit resource reservation and
undocumented stages, vendor roadmap questions, and cache-miss cost.

Its ranked top ten is the honest summary of this corpus's limits. Seven are speed
questions: no scalar multiply or divide latency, no back-to-back issue rate for
the vector multiply, no answer on whether the vector unit shares the scalar
load-store port on a miss, no cache-miss cost, no register window overflow cost,
no issue distance from a scalar shift-register write to the vector multiply that
reads it, and no sweep of unaligned stores in cached external memory. Three are
correctness questions and deserve the measurement time first: whether relaxing
the alignment flag is safe project-wide, whether vector instructions work inside
an interrupt handler, and what the vector state registers hold at reset.

Two structural limits sit behind that list, both recorded in
[the contradiction log](./09-adversarial/contradiction-log-sources-that-disagree.md)
and in [the cost model leaf](./00-foundations/lx7-core-pipeline-and-cost-model.md).
The architecture manual refuses per-instruction latency on purpose and defers to
a processor data book Cadence does not publish, so a whole class of question here
needs a cycle-counter campaign rather than another document. And the chip
manual's stage table covers the extended vector instructions only, so there is no
published timing model at all for the base integer instructions on this core.
