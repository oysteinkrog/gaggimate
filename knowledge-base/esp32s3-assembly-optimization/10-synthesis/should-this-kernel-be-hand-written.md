---
title: "Should this kernel be hand-written? A decision flow"
id: 10-synthesis/should-this-kernel-be-hand-written
schema_version: 1
doc_type: how-to
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, pie, decision-flow, optimization, measurement, synthesis]
confidence: high
sources: []
---

# Should this kernel be hand-written? A decision flow

This page adds no new facts. It is the order in which the facts already in
this topic get applied, from the moment a loop looks slow to the moment a
hand-written kernel either ships or is deleted. Every step below is a gate
with a stop answer and a go answer, and every sentence links the leaf that
holds the claim behind it.

Read it as six gates and one checklist. Steps 1 and 2 decide whether the
loop body is even where the time is. Step 3 decides whether the compiler
has already done the work. Step 4 decides whether the vector unit can
express the idea at all. Step 5 prices the maintenance. Step 6 is the list
of things a kernel must never do, whatever the earlier answers were.

The single most important property of the flow is that the go answers get
harder as you descend. The corpus records one full pass over a fleet of
kernels in which the device reversed four of thirteen predictions made from
instruction counts and host timings, so the late gates exist because the
early ones are not reliable on their own
([myths about optimizing for this core](../09-adversarial/myths-about-optimizing-for-this-core.md),
[the animation kernel pass](../07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md)).

## Step 1. Is the time in the loop body at all?

**Stop if you have not measured.** The counter is `CCOUNT`, special
register 234, reached through `esp_cpu_get_cycle_count()`, and the leaf on
it gives the wrapping rule, the per-core rule, the frequency-scaling trap
and a working harness with a calibration loop, a sink and a compiler
barrier
([CCOUNT and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md)).
Take the minimum of n repetitions for deterministic compute. For
cache-miss-bound code the minimum is the wrong statistic and the honest
report is a mean with a spread
([CCOUNT and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md)).

**Stop if the caller hands the kernel small units of work and you have not
measured the per-call setup separately.** When an interface passes a few
rows per call instead of a whole frame, the setup runs at the frequency of
the outer loop level, not once per frame, and a setup step that is
invisible against a long body is not invisible against one or two rows
([loop shapes and per-call setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md)).
One recorded production interface calls its pixel kernel 240 times per
frame with two rows each, which is what makes the row-state builder as hot
as the pixel loop
([the band contract](../07-our-work/band-contract-interlace-and-row-independence.md)).

**Stop if the row-state builder is the cost and you were about to rewrite
the pixel loop.** One animation's two per-row tables ran 255 and 256
iterations to feed a pixel loop only 240 wide, and deleting them outright
moved the device from 33.6 to 44.8 frames per second, after earlier passes
had gone at the pixel loop
([the animation kernel pass](../07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md)).
A per-pixel body can be one add, one shift, one gather and one store and
still not be where the frame goes: probe blobs on the device showed the
per-cell setup was a third of one frame, and the fix was a wider sampling
grid, not a better inner loop
([host versus device reversals](../07-our-work/host-versus-device-reversals-measured-here.md)).

**Stop if the body still contains a library call.** Float divide, `sqrtf`
and every `double` operation are windowed library calls on this target, not
instructions, and a libcall in the body is visible in the assembly
([the FP option on LX7](../01-scalar-isa/floating-point-option-on-lx7.md),
[register pressure and reading the assembly](../04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md)).
Removing one is cheaper than any kernel and it can restore a hardware loop
at the same time
([register pressure and reading the assembly](../04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md)).

**Go when a measured profile puts a named fraction of the time inside a
loop body you can see.**

## Step 2. Is it memory-bound rather than instruction-bound?

**Stop and fix placement first.** The largest single effect recorded in
this corpus is not in any kernel. The same kernels ran 1.3 to 2 times
slower with their per-pixel tables in PSRAM than in internal SRAM, measured
by pinning every table to each memory in turn with the same firmware
([the hot slab and table placement](../07-our-work/hot-slab-table-placement-and-the-dram-budget.md)).
The generic rule behind the measurement is that the data cache is the
working-set budget for a kernel's tables, and a PSRAM table behaves like an
SRAM table only while the whole working set fits, because a hit and a miss
differ by roughly two orders of magnitude
([caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)).

**Rank tables by reads per output element, not by size.** A small palette
read once per pixel matters more than a large texture swept sequentially,
and a size threshold alone is a reasonable first cut that is still wrong
([the hot slab and table placement](../07-our-work/hot-slab-table-placement-and-the-dram-budget.md),
[caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)).
Placement is a source-level decision made with the ESP-IDF placement
attributes and an aligned allocation, and the linker map is the only ground
truth for where a symbol actually landed
([code and data placement in ESP-IDF](../04-toolchain-and-codegen/code-and-data-placement-in-esp-idf.md)).

**Make the hot budget a build-time constant, not a query against free
memory.** Deciding placement at init time by asking the heap what is free
produces a different answer on different boots when the pool idles near the
threshold, which makes the same kernel twice as slow on some boots with no
other symptom. A fixed slab, sized in the linker's own figure, with a
counted fallback when a table does not fit, is the shape that removed the
variance
([the hot slab and table placement](../07-our-work/hot-slab-table-placement-and-the-dram-budget.md)).
Two rules generalise out of that design: bound what is left for the rest of
the system rather than what the kernel takes, because the consumer that
fails first is the one with the smallest and most urgent allocations; and a
static array is not a way around the budget, because it comes out of the
same internal pool
([the hot slab and table placement](../07-our-work/hot-slab-table-placement-and-the-dram-budget.md)).

**Expect contention you did not put there.** Flash and PSRAM share the MSPI
bus, in octal mode down to every pin except chip select, so instruction
fetches from flash, another core's PSRAM traffic and DMA all compete with
your table reads
([caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)).
A flash write disables both caches on both cores, so any flash-cached code
or PSRAM access anywhere in the system waits
([RTOS and runtime effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md)).

**Do not plan on cache control instructions.** This core configures no
Xtensa Cache Option: the eighteen cache instructions do not assemble, and
preload, lock, writeback and invalidate exist only in the chip's cache
peripheral reached through ROM functions
([cache control, preload, lock and writeback](../03-memory-hierarchy/cache-control-preload-lock-and-writeback.md)).

**Go when the working set fits the placement you have chosen and the
remaining time is arithmetic.** No published cache-miss cycle cost exists
for this part, so if you need the number, measure it with the external
memory hit and miss counters and a differential CCOUNT loop
([caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md),
[the uncertainty register](../09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md)).

## Step 3. Has the compiler already done it?

Read the assembly before writing any. Produce it with the build's own
toolchain and the build's own flags, because a different compiler version
is a different set of decisions and is not evidence about what ships
([register pressure and reading the assembly](../04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md),
[the verification ladder](../07-our-work/verification-ladder-host-asm-qemu-device.md)).
Check the optimisation level first: the ESP-IDF Kconfig default is `-Og`,
not `-O2`, and there is no `-O3` choice in the framework at all, so a
kernel measured on the default build is measured on the wrong build
([GCC 14 Xtensa flags and what they cost](../04-toolchain-and-codegen/gcc14-xtensa-flags-and-what-they-cost.md)).

Four faults are visible in the file, and all four have a fix cheaper than
assembly
([register pressure and reading the assembly](../04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md)):

1. **Spills.** Stack-pointer references inside the body. A leaf function
   gets `a2` to `a15`; a function that makes a call keeps only `a2` to
   `a7`, and the windowed ABI leaves about 14 usable registers in the best
   case
   ([register windows and the windowed ABI](../00-foundations/register-windows-and-windowed-abi.md)).
2. **A lost hardware loop.** Two separate GCC passes can refuse it, and
   inline asm is caught only by the late one, so a loop can look accepted
   in the earlier dump and still come out as a compare and branch. Any
   inline asm anywhere in the body kills it, a call or a return kills it,
   the body is capped at 256 bytes, and no hardware loop is emitted at
   `-O0` at all
   ([zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md)).
3. **A libcall in the body**, as in step 1.
4. **A load next to its consumer.** A load defines its result in the M
   stage and arithmetic uses operands in E, so a dependent instruction
   stalls one cycle unless one independent instruction sits between them
   ([the LX7 pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md)).

Know what the compiler will emit unprompted, so you do not write it again.
From plain C, GCC 14.2 emits the multiply, divide, count-leading-zeros,
min, max, sign-extend and compare-and-swap instructions directly, and it
emits the MAC16 accumulate for one shape only, a 16-bit by 16-bit multiply
accumulated into 32 bits, at `-O2` or `-Os`
([MAC16, Booleans and other configured options](../01-scalar-isa/mac16-boolean-and-other-configured-options.md)).
It never emits `clamps`, because its own configuration disagrees with the
chip's header, although the assembler accepts the mnemonic
([what the headers say versus what the tools do](../04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do.md),
[the contradiction log](../09-adversarial/contradiction-log-sources-that-disagree.md)).
It emits `madd.s` for a multiply-add by default, and it turns a
byte-indexed palette lookup into six instructions per pixel while keeping
the hardware loop
([the FP option on LX7](../01-scalar-isa/floating-point-option-on-lx7.md),
[LUT gathers, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md)).
It emits no PIE instruction at all and has no Xtensa vector builtin, so
anything vector is yours to write
([what the headers say versus what the tools do](../04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do.md)).

Work the source-level interventions before the assembly, cheapest first:
drop an unroll, hoist a call out, make a stride a compile-time constant,
pack several tables into one allocation, split a spilling loop into two
passes, or move the body into a `noinline` leaf function so it gets the
high registers back
([register pressure and reading the assembly](../04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md)).
Some of these run against host intuition. A manual four-way unroll and a
switch to named locals both looked faster on a host bench and both lost on
this target, one to spills and one to a lost hardware loop; packing two
byte tables into one halfword table and counting a loop down to zero both
looked slower on the host and were kept
([host versus device reversals](../07-our-work/host-versus-device-reversals-measured-here.md)).
Unrolling here buys scheduling room, not loop overhead, and the 256-byte
body cap limits how far it can go
([loop shapes and per-call setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md)).

**Stop if the assembly is not yet clean.** Fix the fault instead.
**Go when the assembly is clean and you still want an edge.** The edge has
to be structural: a closed hardware loop the compiler would not form, a
walking pointer instead of a base plus index, or an instruction it never
generates. Transcribing the compiler's own loop usually reproduces its
speed and nothing more, because its schedule is already reasonable for this
latency model
([register pressure and reading the assembly](../04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md)).
That prediction held: the two transcription kernels in the recorded pass
measured a tie and a small win, and parity was the honest expectation for
both
([the animation kernel pass](../07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md)).

## Step 4. Can the vector unit express the idea?

Answer this before writing a line, because the extension has real holes and
a kernel designed around one of them cannot be rescued later.

**Lane widths and what is missing.** A `q` register has no fixed element
type; the instruction picks 16 lanes of 8 bits, 8 of 16 or 4 of 32
([the PIE register file, SAR and context](../02-pie-vector/pie-register-file-sar-and-context.md)).
There is no 32-bit lane multiply and no widening multiply into a register
pair, so the accumulators are the only route to a full product. Adds,
subtracts, compares, min and max are signed only. Lane shifts exist for
32-bit lanes only, so an 8-bit or 16-bit lane shift has to be written as a
multiply by a power of two, which is exact. There is no select, no and-not,
no vector divide, no float lane arithmetic and no lane broadcast from a
register
([PIE arithmetic, multiply, saturate and shuffle](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md)).

**There is no multi-lane gather.** `EE.LDXQ.32` loads one 32-bit lane using
one 16-bit lane as the index, and the vendor DSP library never uses it
([PIE load, store and alignment](../02-pie-vector/pie-load-store-and-alignment.md),
[LUT gathers, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md)).
A data-dependent per-pixel lookup therefore stays scalar. Two kernels in
the recorded pass tried decoding indices into a vector scratch buffer and
both lost to keeping the index in a register, because the scratch costs a
store per element out of the vector kernel and a load per element back into
the gather
([host versus device reversals](../07-our-work/host-versus-device-reversals-measured-here.md)).
A host bench that auto-vectorises the same source with a hardware gather is
measuring a strategy this device does not have
([host benchmarks versus the device](../05-measurement/host-benchmarks-versus-the-device.md)).

**Saturation is the exception, not the rule.** Only instructions whose
description mentions it clamp; everything else wraps
([PIE arithmetic, multiply, saturate and shuffle](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md)).
`EE.VMUL.*` has no shift operand: the product is right-shifted by `SAR`
inside the instruction and the low bits are written back to a lane as wide
as the inputs, which makes it a complete fixed-point multiply with a free
scale, but a truncating one
([PIE arithmetic, multiply, saturate and shuffle](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md),
[fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md)).

**Alignment is a silent correctness hazard, not a fault.** Every PIE access
forces the low address bits to zero before the access, four bits for a
128-bit form, and it never traps, so a misaligned pointer quietly reads or
writes the aligned block below
([PIE load, store and alignment](../02-pie-vector/pie-load-store-and-alignment.md)).
A span whose start is an arbitrary edge therefore needs a scalar prefix and
suffix around the vector middle, which is exactly the shape one shipped
kernel uses
([PIE and scalar idioms in this firmware](../07-our-work/pie-and-scalar-idioms-in-this-firmware.md)).
Get the alignment from an aligned allocation, not from an assumption
([code and data placement in ESP-IDF](../04-toolchain-and-codegen/code-and-data-placement-in-esp-idf.md)).

**SAR is shared and the compiler does not model it.** `SAR` is the base
ISA's own shift register, shared with scalar funnel shifts, and GCC
re-emits `ssr` before every variable shift, so a `SAR` value set inside one
asm block does not survive to the next. GCC has no register class for `q0`
to `q7` either, so they cannot be named as clobbers
([the PIE register file, SAR and context](../02-pie-vector/pie-register-file-sar-and-context.md),
[GCC extended inline asm on Xtensa](../04-toolchain-and-codegen/gcc-extended-inline-asm-on-xtensa.md)).
The consequence for a loop is concrete: it cannot hold a vector scale in
`SAR` and use a scalar variable shift in the same body
([fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md)).

**Schedule to the stage table.** Every vector load, every `EE.VMUL.*` and
`EE.CMUL.*` and the multiply accumulates on their accumulator define at
stage 2, while plain arithmetic uses at stage 1, so a PIE loop loads one
iteration ahead and puts one independent instruction between a multiply and
its consumer. Adds, min, max, compares, bitwise operations, vector shifts,
zips and the funnel shift all define at stage 1 and chain freely. All of it
is an interlock, so a bad schedule is slow and not wrong
([PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md)).

**Go when the idea maps onto forms that exist.** The recorded wins that
came from PIE were stores, not arithmetic: a flat fill and a small tile
pattern, where one 128-bit store replaced eight scalar stores and every
loop branch with them
([the animation kernel pass](../07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md)).
For reference on what a finished vector kernel looks like, the vendor DSP
library is the largest readable body of ESP32-S3 assembly, and its
transferable part is the machinery: per-chip dispatch at zero runtime cost,
a documented alignment contract with a fallback rather than a fix-up, tests
against a portable reference, and benchmarks that state input size and
compiler flag
([esp-dsp as a reference kernel library](../06-kernel-patterns/esp-dsp-as-a-reference-kernel-library.md),
[esp-dsp, esp-nn and vendor kernel libraries](../08-frontiers/esp-dsp-esp-nn-and-vendor-kernel-libraries.md)).

## Step 5. Is the hand kernel worth the maintenance?

A hand kernel is not one artifact. It is the kernel, a portable twin, a
golden set, an emulator test and a device test, and all five have to be
maintained together.

**The twin is mandatory and it is the specification.** Keep a portable
implementation next to every hand-written kernel; on disagreement the twin
is the specification until proven otherwise
([bit-exact reference tests and fuzzing](../05-measurement/bit-exact-reference-tests-and-fuzzing.md)).
In the recorded practice the twin is a field on the kernel's own registry
entry, which is what lets the same device endpoint run kernel and twin back
to back over the same inputs
([PIE and scalar idioms in this firmware](../07-our-work/pie-and-scalar-idioms-in-this-firmware.md),
[the verification ladder](../07-our-work/verification-ladder-host-asm-qemu-device.md)).

**Host goldens catch a wrong picture, not a wrong bit.** They are the first
rung and their tolerance is deliberately loose; exact comparison between a
kernel and its twin happens on the emulator and on the device, not here
([the verification ladder](../07-our-work/verification-ladder-host-asm-qemu-device.md)).
Choose inputs from four classes, a full-range sweep, seeded random,
parameter extremes and adversarial shapes, because they catch different bug
classes
([bit-exact reference tests and fuzzing](../05-measurement/bit-exact-reference-tests-and-fuzzing.md)).

**QEMU is a semantics check and never a timer.** The emulator's `CCOUNT`
tracks a 40 MHz virtual clock rather than the part's real frequency and the
cache model has no wait states, so it cannot produce a speed number
([QEMU for the ESP32-S3](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md)).
It also has open, hardware-confirmed divergences in exactly the vector
instructions a kernel is likely to use: saturation direction, indexed
address offset, shift at 32, destination aliasing and unmasked dynamic
shift counts
([QEMU versus silicon](../09-adversarial/qemu-versus-silicon-known-and-suspected-divergences.md)).
One recorded kernel wrote its reference to the emulator's saturation floor
and the device test then found the two pixels where silicon disagreed; the
reference is now written to the silicon and the emulator test carries both
floors
([the animation kernel pass](../07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md)).

**The device A/B is the only evidence about speed.** Run the same
production loop twice, once through the kernel and once through the twin,
and compare. That is a measurement, not an estimate from an instruction
count or a host timing
([the verification ladder](../07-our-work/verification-ladder-host-asm-qemu-device.md)).
An instruction count is evidence about code shape only: one compiled loop
measured 15 instructions per pixel pair inside a hardware loop and still
cost about 14 cycles per pixel on silicon, which puts instruction count at
roughly half the story with scheduling and memory the other half
([host versus device reversals](../07-our-work/host-versus-device-reversals-measured-here.md),
[the frame budget and measured band costs](../07-our-work/frame-budget-and-measured-band-costs.md)).

**Two ways the A/B itself can lie.** A bench that times a call as the
minimum of several back-to-back repeats cannot measure a design that caches
state across calls, because the repeats hit the cache; time that design in
production instead
([kblob hot-loading kernels on the device](../07-our-work/kblob-hot-loading-kernels-on-the-device.md)).
And a comparison across two runs is only valid when the runtime settings
that change the rate were equal in both, because a stored setting can
silently override a compile-time constant
([the frame budget and measured band costs](../07-our-work/frame-budget-and-measured-band-costs.md)).

**Price the outcome honestly before you start.** In the one recorded pass,
of the kernels given a published ratio against their own twin, four were
wins, two were parity or better and kept, and two lost and one of those was
switched off at the flag while its code stayed in the file
([the animation kernel pass](../07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md)).
Hand-written assembly does not beat the compiler on principle
([myths about optimizing for this core](../09-adversarial/myths-about-optimizing-for-this-core.md)).

**Stop and keep the compiler's code if the device says the kernel is
slower.** Keep the kernel in the file behind a flag that defaults off, so
the next attempt starts from something already bit-exact and fuzzed rather
than from nothing
([the animation kernel pass](../07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md)).
**Keep a tie.** A tie that is bit-exact, fuzzed and understood is a known
starting point; a loss is not
([the animation kernel pass](../07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md)).

**Expect the ground to move.** The toolchain a kernel was tuned against is
one version of one compiler: ESP-IDF 5.5.1 ships GCC 14.2 and later
releases ship 15.2 and 16.1
([GCC Xtensa lineage](../08-frontiers/gcc-xtensa-lineage-and-what-newer-releases-bring.md)).
Espressif's Clang fork already carries the full vector instruction set and
269 builtins, and nobody has measured it against GCC 14 on a kernel
([LLVM and Clang for Xtensa](../08-frontiers/llvm-clang-for-xtensa-status-and-what-it-changes.md)).
A hand kernel is a bet that the compiler will not catch up, and the bet has
to be re-measured when the compiler changes.

## Step 6. What a kernel must never do

These are not tradeoffs. Each one is a defect that can pass every earlier
gate.

**Never write `CPENABLE`.** The register is 8 bits at special register 224
and ESP-IDF saves coprocessor state lazily on the coprocessor-disabled
exception, tracking one owner per coprocessor per core. A kernel that
enables a unit itself skips that bookkeeping, so the owner array disagrees
with the hardware and another task's floating-point or vector state can be
corrupted
([coprocessors, CPENABLE and lazy context](../00-foundations/coprocessors-cpenable-and-lazy-context.md)).
Only a bare-metal harness sets it, once, in its own entry point
([the animation kernel pass](../07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md)).
Coprocessor instructions are thread-only unless the FPU-in-ISR option is
set, which is off by default and endorsed only for the FPU
([coprocessors, CPENABLE and lazy context](../00-foundations/coprocessors-cpenable-and-lazy-context.md)).

**Never trust a pointer's alignment.** A vector access masks the low
address bits and does not trap, so the failure is wrong pixels rather than
an exception. Align with a prefix, or with an aligned allocation, and never
by assumption
([PIE load, store and alignment](../02-pie-vector/pie-load-store-and-alignment.md),
[code and data placement in ESP-IDF](../04-toolchain-and-codegen/code-and-data-placement-in-esp-idf.md)).
The related scalar trap is that an 8- or 16-bit access to instruction-bus
memory raises a fault, so byte access to IRAM is not a supported path on
this part
([the memory map and address spaces](../00-foundations/esp32s3-memory-map-and-address-spaces.md)).

**Never let output depend on how the caller grouped the work.** Where a
kernel is called with a variable number of rows, a row's pixels must depend
only on its own absolute coordinate and the frame state, never on which
other rows happen to share the call. Three of four redesigns in one day
copied from a neighbour inside the call's own buffer or picked a source row
from a call-local offset. Every one passed the golden image diff and would
have painted wrong rows once the real call shapes engaged
([the band contract](../07-our-work/band-contract-interlace-and-row-independence.md),
[the animation kernel pass](../07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md)).
The check that catches it renders one frame in every call shape a real call
site issues and requires identical output row for row
([the verification ladder](../07-our-work/verification-ladder-host-asm-qemu-device.md)).

**Never read past a lookup table's pad, and never fuzz without
sanitizers.** AddressSanitizer and UndefinedBehaviorSanitizer are required
for a fuzz run to be worth anything: without them an unpadded table overrun
reads a plausible neighbouring byte and the test passes
([bit-exact reference tests and fuzzing](../05-measurement/bit-exact-reference-tests-and-fuzzing.md)).
That is how one kernel shipped a palette pad sized from a guess against a
dither amplitude the code itself caps wider. The rule the defect left: size
a pad from the producer's own cap, and re-run the fuzz across every kernel
when a table's layout changes
([fuzzing the fleet and the palette pad](../07-our-work/fuzzing-the-fleet-and-the-silk-palette-pad.md)).

**Never leave an asm block's side effects undeclared.** `SAR`, `SAR_BYTE`,
the loop registers and the vector registers have no clobber name, so a
block that touches them owns them silently. A walking pointer needs a
read-write constraint, a temporary written before the last input is read
needs an early-clobber, and a store needs the memory clobber
([GCC extended inline asm on Xtensa](../04-toolchain-and-codegen/gcc-extended-inline-asm-on-xtensa.md)).
A hand-written hardware loop whose body exceeds 256 bytes is silently
relaxed into a sequence that destroys the count register
([zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md)).

**Never let a kernel leak a resource the bench reuses.** One candidate that
skipped a release pinned the hot-memory count above zero, and every later
measurement on that boot silently ran with its tables in the slow memory,
with no symptom other than a number that was 1.7 times worse
([the hot slab and table placement](../07-our-work/hot-slab-table-placement-and-the-dram-budget.md),
[kblob hot-loading kernels on the device](../07-our-work/kblob-hot-loading-kernels-on-the-device.md)).

## The checklist

Run top to bottom. Any unchecked box is a stop.

**Before writing anything**

- [ ] The time is measured, not assumed, with the cycle counter and a
      stated statistic
      ([CCOUNT and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md)).
- [ ] Per-call setup and the row-state builder are measured separately from
      the inner loop
      ([loop shapes and per-call setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md)).
- [ ] Every table's placement is a decision in source, ranked by reads per
      output element, inside a fixed budget
      ([the hot slab and table placement](../07-our-work/hot-slab-table-placement-and-the-dram-budget.md),
      [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)).
- [ ] The assembly was produced with the build's own compiler and flags, at
      the build's real optimisation level
      ([GCC 14 Xtensa flags](../04-toolchain-and-codegen/gcc14-xtensa-flags-and-what-they-cost.md)).
- [ ] The assembly is free of spills, a lost hardware loop, a libcall in
      the body and an unfilled load-use pair
      ([register pressure and reading the assembly](../04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md)).
- [ ] The source-level interventions were tried first
      ([register pressure and reading the assembly](../04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md)).
- [ ] The edge you intend to take is structural, not a transcription
      ([register pressure and reading the assembly](../04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md)).
- [ ] If vector: the idea needs no multi-lane gather, no 8- or 16-bit lane
      shift, no select and no float lanes
      ([PIE arithmetic, multiply, saturate and shuffle](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md),
      [PIE load, store and alignment](../02-pie-vector/pie-load-store-and-alignment.md)).
- [ ] If vector: the alignment prefix, the `SAR` ownership and the stage-2
      producers are all in the design
      ([PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md),
      [the PIE register file, SAR and context](../02-pie-vector/pie-register-file-sar-and-context.md)).

**Before it ships**

- [ ] A portable twin exists and is treated as the specification
      ([bit-exact reference tests and fuzzing](../05-measurement/bit-exact-reference-tests-and-fuzzing.md)).
- [ ] Host goldens pass, and the call-shape invariant is checked in every
      shape a real caller issues
      ([the verification ladder](../07-our-work/verification-ladder-host-asm-qemu-device.md)).
- [ ] The fuzz run has AddressSanitizer and UndefinedBehaviorSanitizer on,
      and every gather's pad is sized from its producer's cap
      ([fuzzing the fleet and the palette pad](../07-our-work/fuzzing-the-fleet-and-the-silk-palette-pad.md)).
- [ ] The emulator run is bit-exact against the twin, and any instruction
      on the divergence list is treated as unproven until the device says
      otherwise
      ([QEMU versus silicon](../09-adversarial/qemu-versus-silicon-known-and-suspected-divergences.md)).
- [ ] The device equivalence test reports zero mismatched output
      ([the verification ladder](../07-our-work/verification-ladder-host-asm-qemu-device.md)).
- [ ] The speed claim is a device A/B against the twin under production
      conditions, with the settings that affect the rate equal in both runs
      ([the verification ladder](../07-our-work/verification-ladder-host-asm-qemu-device.md),
      [the frame budget and measured band costs](../07-our-work/frame-budget-and-measured-band-costs.md)).
- [ ] The kernel writes no `CPENABLE`, assumes no alignment, depends on no
      call grouping, and releases everything it takes
      ([coprocessors, CPENABLE and lazy context](../00-foundations/coprocessors-cpenable-and-lazy-context.md),
      [the band contract](../07-our-work/band-contract-interlace-and-row-independence.md)).
- [ ] If the device says it is slower, the compiler's code ships and the
      kernel stays behind a flag that defaults off
      ([the animation kernel pass](../07-our-work/animation-kernel-pass-2026-09-what-the-device-decided.md)).

## Where this flow is weakest

The flow leans on measurement at every gate because the corpus cannot
supply the numbers from documents. Cadence does not publish per-instruction
latencies for this core, the vector chapter gives a stage table and no
cycle counts at all, and no cache-miss cycle cost is published for the
part. The uncertainty register groups all 146 open questions and names the
experiment that would settle each
([the LX7 pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md),
[PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md),
[the uncertainty register](../09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md)).
Until those are measured, every step above that sounds like arithmetic is
really a rule for deciding what to measure next. `[experience]`
