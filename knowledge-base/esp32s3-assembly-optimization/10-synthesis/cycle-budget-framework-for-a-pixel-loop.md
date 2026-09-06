---
title: Cycle budget framework for a pixel loop
id: 10-synthesis/cycle-budget-framework-for-a-pixel-loop
schema_version: 1
doc_type: how-to
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, cycle-budget, cost-model, pixel-loop, measurement, synthesis]
confidence: medium
sources:
  - 00-foundations/lx7-core-pipeline-and-cost-model
  - 01-scalar-isa/branches-jumps-and-control-flow-costs
  - 01-scalar-isa/zero-overhead-loops
  - 02-pie-vector/pie-hazards-latencies-and-issue-rules
  - 03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus
  - 05-measurement/ccount-cycle-counter-and-timing-a-kernel
  - 05-measurement/host-benchmarks-versus-the-device
  - 06-kernel-patterns/lut-gathers-palettes-and-rgb565
  - 06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup
  - 08-frontiers/rtos-and-runtime-effects-on-kernel-timing
  - 09-adversarial/uncertainty-register-what-the-corpus-does-not-know
---

# Cycle budget framework for a pixel loop

This page turns a frame-rate target into a per-pixel cycle budget on the
Xtensa LX7 core in the ESP32-S3, and gives a way to account for where those
cycles go. It introduces no new facts. Every cost below is either a claim
already held by a leaf in this corpus, linked at the point of use, or is
marked `[uncertain]` because that leaf could not confirm it. The arithmetic
that connects them is `[obvious]`.

The framework has four parts: the budget arithmetic, a cost model to spend
it against, a worked example on one kernel shape, and the rules for reading
a measured number back against the budget.

## 1. The budget arithmetic

The core runs at up to 240 MHz, so one core delivers 240,000,000 cycles a
second when it is held at that
clock ([core as configured](../00-foundations/lx7-core-pipeline-and-cost-model.md)).
Two divisions turn that into a per-pixel figure. `[obvious]`

```
cycles per frame = clock / target frame rate
cycles per pixel = cycles per frame / pixels per frame
```

At 240 MHz:

| Pixels per frame | 30 fps | 60 fps |
|---|---|---|
| 76,800 (320 x 240) | 104.2 cycles/px | 52.1 cycles/px |
| 153,600 (480 x 320) | 52.1 cycles/px | 26.0 cycles/px |
| 307,200 (640 x 480) | 26.0 cycles/px | 13.0 cycles/px |

Three things this arithmetic does not say, each of which changes the number
a kernel may actually spend.

**The budget belongs to the whole frame, not to one kernel.** Anything else
that runs once per frame comes out of the same figure: a composite or blend
pass over the same pixels, the transfer that gets the frame to the display,
and the per-call setup of whatever interface hands the kernel its work. A
kernel that exactly meets the per-pixel budget has left nothing for the rest.
A worked instance of that split, on one board with its own measured blend
and transfer costs, is
in [the frame budget one display pipeline measured](../07-our-work/frame-budget-and-measured-band-costs.md).

**Refreshing alternate rows on alternate frames doubles the budget and
halves the per-row rate.** If the pipeline renders only the rows it will
push this frame, pixels per frame halves and the per-pixel budget doubles,
while any one row is refreshed at half the loop's rate. That trade is only
available to a kernel whose output for a row depends on the absolute row
index and the frame state, and not on which other rows share the call. The
contract that makes it safe, and what breaks when a kernel derives a row
from its neighbour inside the call buffer, is
in [the band contract for an interlaced renderer](../07-our-work/band-contract-interlace-and-row-independence.md);
the same page records that an interface handing the kernel two rows per call
pays the kernel's per-call setup once per band, so a row-state builder is as
hot as the pixel loop it feeds. The generic form of that rule, that per-call
setup runs at the frequency of the outer level of the pixel loop rather than
once per frame, is
in [loop shapes and per-call setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md).

**A frame flip that waits on a display's scan-out quantises the achieved
rate.** When the pipeline blocks on the display period rather than on its own
work, render time does not translate into frame rate smoothly: overrunning
one period by any amount costs a whole second period. The budget above is
therefore a deadline, not a rate dial, and the effect is worked through with
measured periods
in [the frame budget one display pipeline measured](../07-our-work/frame-budget-and-measured-band-costs.md).

## 2. The cost model to spend the budget against

The corpus's own framing is that the Xtensa ISA manual gives the pipeline
structure and the interlock model but no per-instruction cycle counts, and
defers timing to a processor data book that is not openly published for the
LX7 ([LX7 pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md)).
So the table below splits cleanly: rows that are structural carry a link to
the leaf that cites them, and rows that are not are marked `[uncertain]`
with the settling method
that [the uncertainty register](../09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md)
names for them.

### 2.1 Issue and scalar scheduling

| Cost | Value | Status |
|---|---|---|
| Issue width | At most one instruction per cycle, in order | Cited, [pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) |
| ALU result used by the next instruction | No extra cycle | Cited, [pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) |
| Load result used by the next instruction | One stall cycle, removed by placing one independent instruction between them | Cited, [pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) |
| Store when the write buffer is full | Interlocks in the decode stage, duration set by the buffer draining | Cited, [pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) |
| Branch not taken | No extra cycle | Cited, [branches and control flow](../01-scalar-isa/branches-jumps-and-control-flow-costs.md) |
| Branch taken, aligned target | 2 cycles, the instructions in decode and execute are removed | Cited, [branches and control flow](../01-scalar-isa/branches-jumps-and-control-flow-costs.md) |
| Branch taken, target crosses a fetch boundary | One more cycle | Cited, [pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) |
| Hardware loop back edge | No branch, so none of the taken-branch cost | Cited, [zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md) |
| The hardware loop's own back-edge cost | `[uncertain]`; the manual warns of an extra clock on the first loop back of certain loops, and gives no count | [zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md), settled by a CCOUNT loop |

There is no branch predictor on this core, so the taken-branch cost is fixed
rather than something a hint can improve, and a clamp or a per-element pick
written as a conditional move avoids it
entirely ([branches and control flow](../01-scalar-isa/branches-jumps-and-control-flow-costs.md),
[loop shapes](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md)).

### 2.2 Arithmetic that has no published latency

| Operation | Value | Status |
|---|---|---|
| `MULL`, `MULSH`, `MULUH` result latency | `[uncertain]` | [pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md), settled by a CCOUNT loop |
| `QUOS`, `QUOU`, `REMS`, `REMU` | `[uncertain]`, iterative and multi-cycle; hardware divide is not the same as fast divide | [pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md), settled by a CCOUNT loop |
| Float add, multiply, multiply-add | `[uncertain]`; the instructions exist and the compiler inlines them | [pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md), settled by a CCOUNT loop |
| Float divide | A windowed call into a routine of 30 instructions; the total is `[uncertain]` | [pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) |
| `sqrtf` | A C library call; the total is `[uncertain]` | [pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) |
| Any `double` operation | A library call, because the FPU is single precision only | Cited, [pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) |

These rows are the largest single group in the uncertainty register, and
scalar multiply and divide latency is ranked first there, because it is what
every fixed-point format decision in the corpus is currently compared
against without a
number ([uncertainty register](../09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md)).
The budget consequence is directional rather than numeric: a divide or a
square root that repeats every iteration is worth hoisting, tabulating, or
turning into a multiply by a value computed
once ([pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md)),
and a stray `double` from a missing `f` suffix drops into library calls that
cost nothing extra on a host
build ([host benchmarks versus the device](../05-measurement/host-benchmarks-versus-the-device.md)).

### 2.3 The vector unit

The only published timing model for the PIE extension is a table of the
stage at which each instruction defines and uses each operand, with no cycle
count and no throughput figure
anywhere ([PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md)).
Within that model:

| Cost | Value | Status |
|---|---|---|
| Vector load, broadcast or indexed load, result used by a stage 1 consumer | Defines at stage 2, so one independent instruction is needed between them | Cited, [PIE hazards](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) |
| Vector multiply, result used by a stage 1 consumer | Defines at stage 2 as well, so a multiply is load-shaped for scheduling | Cited, [PIE hazards](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) |
| Adds, min, max, compares, bitwise ops, shifts, zips and unzips | Define at stage 1 and chain with no gap | Cited, [PIE hazards](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) |
| A chain of multiply-accumulates into the same accumulator | Reads and writes the accumulator at stage 2, so one per cycle with no gap | Cited, [PIE hazards](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) |
| Reading an accumulator out | Uses it at stage 1, so one independent instruction after the last accumulate | Cited, [PIE hazards](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) |
| Post-increment pointer forms | Use and define the address register at stage 1, so a walking pointer costs no interlock | Cited, [PIE hazards](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) |
| Back-to-back throughput of two vector multiplies | `[uncertain]`; the core has eight 16-bit multipliers and the manual never says which instruction reserves which unit for how long | [PIE hazards](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md), settled by a CCOUNT loop comparing a dense chain against one with fillers |

This is a schedule model, not a cost model: applying it removes stalls a
kernel would otherwise pay, but it cannot say what the kernel costs. The
uncertainty register ranks the multiply throughput question second overall,
because it decides whether a vectorised kernel is bound by lane count or by
a hidden
reservation ([uncertainty register](../09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md)).

### 2.4 Where the operands live

A cycle cost is meaningless without saying which memory the operand came
from, and this is the one part of the model where the multipliers are large
enough to dominate everything above.

| Class | Value | Status |
|---|---|---|
| Register | No memory access | Cited, [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) |
| Internal SRAM | One access; the load-use rule above is the whole cost | Cited, [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) |
| Cached flash or cached PSRAM, hit | As far as the pipeline is concerned, the same as an SRAM access | Cited, [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) |
| Cached flash or cached PSRAM, miss | Full cost `[uncertain]`; the floor is the line fill's data phase, about 48 core cycles for a 32-byte line on octal DDR PSRAM at 80 MHz | [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md), settled by a CCOUNT loop read against the external-memory miss counters |
| The same miss while the other core or a DMA engine is busy | Larger by an unstated amount; flash and PSRAM share one external bus and both caches serve both cores through an arbiter | Cited, [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) |

Two budget rules follow from the line-fill floor and the cache geometry.
A random gather into a table larger than the data cache cannot beat the
line-fill floor per access, whatever the loop body does, which sets a hard
ceiling on any table-driven kernel that misses per pixel. And a sequential
sweep amortises one fill over the whole line, so large sequential data
belongs in external memory while small tables read once per output element
belong in internal
SRAM ([caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)).
The two miss rows are ranked fourth in the uncertainty register, on the
grounds that they are the floor every other latency in the corpus is
compared
against ([uncertainty register](../09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md)).

### 2.5 Costs that do not belong to the loop

These are paid around the kernel rather than inside it, and they are the
usual reason a measured number exceeds a budget the loop body seems to meet.

| Cost | Value | Status |
|---|---|---|
| Bulk copy through `memcpy` | A call into a boot ROM routine that aligns the destination, then moves 16 bytes per hardware-loop iteration with word-sized stores | Cited, [loop shapes and per-call setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md) |
| Register window overflow or underflow | `[uncertain]`; a full exception plus a handler that moves 4, 8 or 12 registers, materially more than a register move | [register windows and the windowed ABI](../00-foundations/register-windows-and-windowed-abi.md), settled by triggering one in isolation under CCOUNT |
| First vector or floating-point instruction after a contended task handoff | One coprocessor-disabled exception, whose handler saves and restores 208 bytes of vector state or 72 bytes of floating-point state; the cycle cost is `[uncertain]` | [coprocessors, CPENABLE and lazy context](../00-foundations/coprocessors-cpenable-and-lazy-context.md), [RTOS effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md) |
| The RTOS tick | A level-1 interrupt, 100 times a second by default, staggered between the two cores | Cited, [RTOS effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md) |
| A flash write anywhere in the system | Both caches go down and the other core is parked, so code and data outside internal SRAM are unreachable for the duration | Cited, [RTOS effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md) |

The coprocessor exception is paid at most once per contended handoff, not
once per call, so a benchmark loop on an idle core stops seeing it after the
first iteration while a kernel sharing a core with other vector users pays
it every
time ([RTOS effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md)).
Window exceptions are an ISA property of windowed calls rather than an RTOS
service, so keeping the loop body call-free removes them from the inner loop
whatever the runtime
is ([loop shapes and per-call setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md)).

## 3. A worked example: a palette lookup over RGB565

This is the kernel shape the corpus carries measured instruction counts for,
so the budget can be spent against a real count rather than a guess. The
kernel reads a byte index per pixel, looks it up in a 256-entry 16-bit
palette, and writes an RGB565
pixel ([table lookups, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md)).

**Budget.** Take 480 x 320 at 60 fps from the table in section 1: 153,600
pixels a frame and 26.0 cycles a pixel for everything the frame does.
Suppose a composite pass and a transfer take half of it, leaving the kernel
13.0 cycles a pixel. `[obvious]`

**Count.** Three loop shapes, all of which keep the hardware
loop ([table lookups, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md)):

| Shape | Instructions | Per pixel |
|---|---|---|
| Indexed, one lookup, one halfword store | 6 per pixel | 6 |
| Two lookups through walked pointers, packed into one word store | 11 per two pixels | 5.5 |
| Four lookups through walked pointers, two word stores | 20 per four pixels | 5 |

The first shape has the load-use defect the cost model predicts: the table
load writes a register the store reads immediately, so the pipeline stalls
rather than
retiring ([table lookups, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md)).
The same source, written with an index variable rather than walked pointers,
costs 13 instructions for two pixels because the compiler rebuilds the
address every
iteration ([table lookups, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md)).
Walking pointers is therefore worth about 1.5 instructions a pixel here
before any scheduling argument. `[obvious]`

**The gap.** At one issue slot per instruction and no stalls, the four-wide
shape has an issue-bound floor of 5 cycles a pixel, against a 13.0 cycle
budget. `[obvious]` The remaining 8 cycles are what the memory system may
take. Two placements bound it:

- The palette is 512 bytes, small enough for internal SRAM and to stay
  resident in the data
  cache ([table lookups, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md)),
  so its reads cost the load-use rule and nothing else.
- If the index stream comes from external memory, one 32-byte line covers 32
  consecutive one-byte indices, so the line-fill floor of about 48 cycles
  amortises to about 1.5 cycles a pixel on a sequential
  sweep ([caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)).
  `[obvious]` for the division.

So the accounting is roughly 5 cycles of issue plus 1.5 cycles of amortised
fill plus whatever stalls the schedule leaves, against 13.0. The shape fits
with room. That is a prediction, not a result: section 4 is how it gets
checked.

**Where the next cycle would come from, in order.** Interleaving lookups so
every table load has two or more instructions before its first use, which is
what the four-wide shape already
buys ([table lookups, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md)).
Packing two pixels into one 32-bit store, which halves the store count and
costs one shift and one or, with a 4-byte row alignment and a scalar tail
for an odd
width ([table lookups, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md)).
Interleaving two independent elements so each one's load-use gap is filled
by the
other's ([loop shapes and per-call setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md)).
Two limits stop the unroll: the hardware loop's body cap of 256 bytes, past
which the loop is lost or its entry is quietly relaxed into a longer
sequence ([zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md)),
and the 14 address registers a leaf function has, past which the extra
copies spill and the reload reintroduces the stall the interleave
removed ([loop shapes and per-call setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md)).

Note what the vector unit does not offer this kernel. There is no full
gather: the one indexed vector load fills a single 32-bit lane, from a
32-bit table, with both lane selectors as immediates, so four separate
instructions fill one
register ([table lookups, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md)).
A host build that auto-vectorises the same lookup into a gather instruction
is measuring a strategy this core cannot
run ([host benchmarks versus the device](../05-measurement/host-benchmarks-versus-the-device.md)).

## 4. Reading a measured number against the budget

**Measure cycles, and divide by elements.** The core has a cycle counter
that increments once per processor clock, wrapped by the platform as a
one-instruction read; hold the clock fixed, pin the measuring task to one
core, and report cycles rather than
microseconds ([CCOUNT and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md)).

**Take the minimum of n, except when misses are the work.** Nothing an
interrupt or a context switch does makes a kernel finish sooner, so the
minimum is the least disturbed run. For code dominated by cache misses the
misses are part of the work rather than noise, and the minimum reports an
iteration the kernel will never deliver; report the mean and the spread
there, and say which one you
reported ([CCOUNT and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md)).

**Compare cycles per element against the inner loop's instruction count.**
Close to the count means the loop is issue-bound and the way forward is
fewer instructions. Well above it means the loop is stalling, and fewer
instructions will not help; look at load-use distance, alignment, and where
the operands
live ([CCOUNT and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md)).
In the worked example above, a measured 5 to 7 cycles a pixel would say the
kernel is issue-bound, and a measured 20 would say the index stream is
missing rather than the loop being long. `[obvious]`

**A rate is not a time when the flip waits.** If the pipeline blocks on a
display period, an achieved frame rate is a step function of render time and
cannot be inverted back into a per-pixel cost. Measure the kernel's own
cycles, and treat the frame rate as the deadline check;
[the frame budget one display pipeline measured](../07-our-work/frame-budget-and-measured-band-costs.md)
works this through on real periods.

**State the conditions, or the number is not comparable.** Report the value
of n and the cold first run alongside the minimum, where the operands,
tables and code live, the toolchain and flags, the cache configuration, the
clock and whether power management was on, and the core, priority and
interrupt state of the measuring
task ([CCOUNT and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md)).
Two numbers taken under different entries in that list are not two
measurements of the same thing.

**A host number locates cost and cannot size it.** The host core is
out-of-order and superscalar, so it fills the load-use gaps this core stalls
on; it renames onto a large physical register file, so it hides a spill
cliff that is real here; it has hardware divide, square root and double
precision where this core has library calls; and its caches are orders of
magnitude larger than the path to external memory
here ([host benchmarks versus the device](../05-measurement/host-benchmarks-versus-the-device.md)).
That leaf also lists the reversal patterns to expect, including unrolling
that helps on the host and spills here, and a smaller table that loses on
the host and wins
here ([host benchmarks versus the device](../05-measurement/host-benchmarks-versus-the-device.md)).
Between the host and the device sit two cheaper checks: the cross-compiler's
own assembly, which is a fact about the code the device will run, and an
emulator, which answers correctness rather than speed unless it models
timing ([host benchmarks versus the device](../05-measurement/host-benchmarks-versus-the-device.md)).
A worked instance of a fleet of kernels where the device reversed the host's
ranking is
in [host versus device reversals measured on one board](../07-our-work/host-versus-device-reversals-measured-here.md).

## 5. What this framework cannot tell you

The budget arithmetic in section 1 is exact. The accounting in section 2 is
complete only for the structural rows. Everything in sections 2.2 and 2.3,
plus the two miss rows in 2.4 and the two exception rows in 2.5, is
`[uncertain]`, which means a predicted cycle count assembled from this table
has an unbounded error term wherever those rows carry weight.

The practical consequence is an ordering, not a caveat. Use the table to
choose between two shapes of the same kernel, where the uncertain rows
appear on both sides and cancel. Do not use it to predict an absolute cycle
count and then treat a measurement that disagrees as a measurement error.
[experience]

The uncertainty register ranks which of these gaps would change the most
kernel decisions if one measurement campaign were run: scalar multiply and
divide latency first, vector multiply throughput second, whether the vector
unit shares the scalar load-store port on a miss third, and the flash and
PSRAM miss cost
fourth ([uncertainty register](../09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md)).
Filling those four rows would turn this framework from a way of ranking
shapes into a way of predicting times.
