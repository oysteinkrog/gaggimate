---
title: Host versus device reversals measured here
id: 07-our-work/host-versus-device-reversals-measured-here
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, gaggimate, host-vs-device, animbench, reversal, measurement]
confidence: high
---

# Host versus device reversals measured here

`05-measurement/host-benchmarks-versus-the-device.md` explains *why* a host
benchmark and a real ESP32-S3 disagree: the host is wide, out of order, and
has a large renamed register file, so it hides load-use stalls, register
spills and cache misses that an in-order LX7 pays for in full. This page is
the record, not the theory: every specific case in this repo's animation
work (`tools/animbench`, the hand-written kernels in
`src/display/ui/default/bganim/`) where a host number and a device number
pointed different ways, with the measured numbers and the commit each one
came from.

The rule this repo settled on states the conclusion plainly: "When the host
bench and the device disagree, the device wins"[^optimize-rule]. Everything
below is a specific instance of that rule being tested against a real
change, not a restatement of it.

## 1. The four reversals from the first optimization pass

`tools/animbench/OPTIMIZE.md` names four changes where the host bench and
the device timing pointed opposite ways, found while multiple workers
tuned the 13 animations in parallel[^optimize-rule]:

| Change | Animation | Host result | Device result | Kept or reverted |
|---|---|---|---|---|
| Manual 4x loop unroll | silk | Faster: 0.337 to 0.291 ms | Worse: 158 to 430 static instructions | Reverted |
| Named locals instead of a pointer array | aurora | Faster | Worse: 883 to 949 instructions, one of two hardware loops lost | Reverted |
| Pack two `uint8` LUTs into one `uint16` LUT | mandala | About 9% slower | Better: freed a base-pointer register, halved per-pixel spills | Kept |
| Count a loop down to zero instead of up | lava | Marginally slower | Better: GCC emits the zero-overhead `LOOP` instruction and drops the per-iteration branch | Kept |

The silk unroll and the aurora locals both did what a host bench predicts
they should: give the compiler more independent, live work per iteration.
On the LX7 that backfired, because the windowed ABI leaves only about 13 to
14 usable registers inside one call; the extra live state spilled to memory
and, for silk, cost the hardware loop as well[^optimize-rule]. The mandala
and lava changes ran the argument the other way: each looked like a
pessimization on x86 and was a real win once measured in the real
compiler's assembly, because each removed a resource the device is short
of (a register, or a taken branch) rather than adding host-side
parallelism the device cannot use[^optimize-rule].

## 2. Two kernels that batched work into PIE and lost to a register index

The 2026-09-04 hand-written-kernel pass names "PIE decode into scratch
loses to keeping the index in a register" as one of two lessons the whole
13-animation pass kept[^round1-commit]. Two animations tried this shape in
their first round and both lost to the plain compiler-generated code that
kept the index in a scalar register the whole time instead of writing it
out to memory and reading it back.

**Plasma**, round 1: a PIE vector kernel computed the per-pixel palette
index (`(ct[i]+rt)>>4 & 255`) for a whole row into a scratch buffer, then a
separate scalar kernel gathered the palette entries from that buffer. It
assembled cleanly and was bit-exact, but on the device it only reached
0.98x of `bandRef()`'s own time in SRAM, i.e. slightly slower than the
plain C++ path it was meant to beat[^plasma-comment]. The reason is in
`bandRef()`'s own disassembly: GCC already folds the shift-and-mask into
one `EXTUI` and schedules the pairwise loop into a hardware `LOOP` with
only one unhidden load-use stall, so PIE's real saving over that single
scalar instruction was small, about a quarter of a vector instruction per
pixel, and the scratch buffer's own store-then-reload traffic, plus two
extra windowed-ABI calls per row for the split kernels, spent more than
that saving back[^plasma-comment]. The round that followed fused the index
math and the gather into one hand-scheduled scalar kernel and used no PIE
at all.

**Mandala**, round 1: `band()` decoded its `polarMap` table eight entries
at a time into a scratch buffer with PIE, then drained the scratch with a
separate hand-scheduled scalar pass, on the theory that batching one
multiply across eight pixels would win. Measured on the device with every
table pinned to internal SRAM (so the comparison was kernel against
kernel, not table placement against table placement), it ran at 48.1 ms
per frame against `bandRef()`'s 41.9 ms, a clear loss[^mandala-round1].
Reading the device compiler's own disassembly for `bandRef()` found why:
GCC pays exactly one load-use stall per pixel, the final
palette-load-into-store that has nothing independent left to hide it
behind, while the hand kernel paid four, because it separately masked out
two fields the compiler's schedule left combined until the very last step,
and because one shift instruction sat in the wrong slot in the hand
schedule[^mandala-round1]. The PIE amortization was real but small, and it
was bought with a full round trip through a scratch buffer, an extra
function call pair per row, and an alignment-driven table layout change
that a scalar design never needed[^mandala-round1]. The round that
followed removed the scratch buffers and the decode step entirely and
replaced them with one hand-scheduled scalar pass that copies GCC's own
proven schedule instruction for instruction, five static instructions
lighter per call than the retired PIE design[^mandala-round1].

## 3. Hand kernels that won on paper and still lost on the chip

A later synthesis in this repo's own `CLAUDE.md` states the general form of
both cases above: "kernels that won on static instruction count lost on
the chip (mandala 0.85x, silk 0.73x of the compiler's own `bandRef`)
because the device pays for load-use stalls and cache misses, not
instructions"[^claude-md-kernels]. Those two ratios are the fifth pass's
hand-written 8-pixel-grid kernels for mandala and silk: each had fewer
static instructions than the compiler's own generated loop, and each still
ran slower once timed on real silicon, so both were retired rather than
shipped[^claude-md-kernels].

Silk's kernels came back a third time, written to transcribe GCC 14's own
compiled loop mnemonic for mnemonic rather than to out-clever it, alongside
new kernels for lava and a second silk-family animation called Silk 2, all
three left untimed on the device because the bench board was offline when
they were written[^lava-silk-untimed]. The first production A/B settled
their defaults[^silk-ab-result]:

| Kernel | Device result | Default |
|---|---|---|
| Silk 2 pixel-pair loop | 1.16x of `bandRef` (win) | On |
| Lava finalize and gather loops | 0.99x of `bandRef` (tie) | On |
| Silk cell kernels (16-pixel grid) | `band()` 13.1 ms against `bandRef()` 11.4 ms, about 0.87x, 0.86x in the animtest bracket (loss) | Off |

Even a kernel built by copying the compiler's own schedule, with every
load-use gap already filled by construction, still lost to the compiler's
own generated code on real silicon for silk specifically: "the third silk
kernel GCC 14's compile of `bandRef` has beaten on the chip"[^silk-ab-result].
Parity by construction on paper, in QEMU, and in the assembly reading is
not a device result; only the on-device A/B was.

## 4. A host check that caught what the golden-frame diff missed

Not every reversal here is about speed. Three of four row-pair redesigns
made on 2026-09-05 (aurora, lava and mandala on that date, with ember
following the same day) cut band time by rendering one row of each
vertical pair and copying or deriving the other, to halve the per-pixel
field work[^aurora-rowpair][^lava-rowpair][^mandala-rowpair][^ember-rowpair].
Aurora's and lava's first attempts each passed the ordinary host golden-
frame comparison (`bench.cpp`'s pixel diff against the reference PPMs) and
still painted the wrong pixels once flashed, because that comparison only
ever renders a whole contiguous band, while production's interlaced path
calls `band()` one row at a time in a parity-skipping order that never
visits a row's own partner first[^aurora-rowpair][^lava-rowpair]. A design
that copied from "the neighbouring row in this call's own buffer," or that
picked "the pair's source row" from an offset local to one call, is
correct under the whole-band shape and wrong under the interlaced one,
because the neighbour or the offset it relies on is not always in the same
call[^lava-rowpair].

What caught this before either animation reached the board was not the
golden-frame diff but a separate, stricter host tool built earlier for
exactly this invariant: `tools/animbench/interlace_check.cpp` renders one
already-advanced frame in every call shape production actually uses,
including single-row calls in the parity-skipping sequence, and requires
every shape to produce identical pixels[^interlace-check]. Ember's
redesign, written after aurora's and lava's had already hit this, was built
from the start to derive every row's state from its pair row's absolute
position (`y & ~1`) rather than from anything local to one call, and
copies a partner's result only when that partner is actually present in
the same call[^ember-rowpair]. Mandala's redesign, a half-resolution sample
grid rather than a row-pair cache, did not hit this particular bug, which
is consistent with it not sharing the cross-call caching shape the other
three did[^mandala-rowpair].

This is a different kind of host-versus-device gap from the others on this
page: not a case where the device disagreed with a host *speed* estimate,
but a case where one host check (a pixel diff against a whole-band render)
was not a strong enough model of the device's actual calling pattern, and a
second host check that models that pattern directly was what closed the
gap before a flash was needed.

## 5. The calibration itself needed correcting

`tools/animbench/BASELINE.md` set an explicit scaling rule from the one
animation the whole fleet agreed was safely at budget: plasma cost 0.122 ms
of host band time, ran with zero libm calls, and was known to sustain
30 fps on the device with headroom, so the rule adopted was "host band_ms x
~80 ≈ device ms/frame" for pure-ALU code[^baseline-calibration]. Measured
against the fleet on the actual bench device on 2026-09-04, that estimate
was wrong in the pessimistic direction: "the device is 2 to 3x worse than
the host-scaled estimate for most of the fleet"[^asmbrief-device]. The
gap is attributed to costs the x80 scaling factor, derived from one
animation's host-to-device ratio, could not carry to every other kernel:
load-use stalls, spills out of the 16-register window, PSRAM cache misses
on any table over 8 KB, and 240 `band()` calls per frame each paying its
own per-call setup, since the device's band height is 2 rows against the
bench's 8[^asmbrief-device]. A single calibration constant measured on one
animation is not a substitute for measuring the rest of the fleet.

## 6. Instruction count and cycle count are not the same currency

Even for a kernel the device agrees is fast, an instruction count from the
compiler's disassembly is not a cycle count. Plasma's compiled inner loop
was measured at 15 instructions per pixel pair inside a hardware loop, and
the same kernel cost 14 cycles per pixel on the device, not per pixel
pair[^asmbrief-planacount]. `ASM_BRIEF.md` draws the conclusion directly:
"instruction count is roughly half the story and scheduling and memory are
the other half"[^asmbrief-planacount]. A static instruction count is
evidence about code shape; only a device or cycle-accurate-emulator
measurement says how many cycles that shape actually costs, which is the
same conclusion `05-measurement/host-benchmarks-versus-the-device.md`
reaches from the ISA manual's own description of the pipeline, reached
here independently from a kernel this repo shipped.

## 7. A device probe that found the real cost inside a "tight" loop

Silk's per-pixel body was already minimal, one add, one shift, one gather
and one store, and still cost 13.4 ms per frame on the rig. Five probe
blobs run against the real board in about twenty minutes, each disabling
or changing one part of the per-cell work, showed the per-cell node work
(three sine gathers, a contrast gather, a vignette product, a curvature
probe) was a third of that frame, not the pixel loop the static count
suggested was the whole story[^silk-probe]. Widening the sampling grid
from 8 to 16 pixels and writing each pixel pair as one 32-bit store, with
the field still sampled every 16 pixels, cut the frame to 8.2 ms, with the
goldens moving by a mean of 2.0 out of 255, all of it the dither grain
going from one pixel wide to two, invisible at native resolution and kept
as the shipped design[^silk-probe]. No amount of reading the compiled
assembly for the one-add-one-shift-one-gather-one-store body would have
found that the per-cell setup, not the body, was the frame's real
cost; only timing the device with the setup varied did.

## Footnotes

[^optimize-rule]: This repository, `tools/animbench/OPTIMIZE.md`, section
    "When the host bench and the device disagree, the device wins," which
    lists the silk unroll, aurora locals, mandala LUT pack and lava
    counted-down loop cases with the quoted numbers.

[^round1-commit]: This repository, commit `8fc84e7a` ("bganim: hand-written
    Xtensa kernels for the 13 animations, hot tables in the slab"), commit
    message: "PIE decode into scratch loses to keeping the index in a
    register."

[^plasma-comment]: This repository, `src/display/ui/default/bganim/AnimPlasma.cpp`,
    the comment beginning "Round 1 of this pass tried PIE," introduced in
    commit `8fc84e7a`.

[^mandala-round1]: This repository, `src/display/ui/default/bganim/AnimMandala.cpp`,
    the comment beginning "Assembly pass, round 2 (2026-09-04, same day,
    after device numbers came back)," introduced in commit `8fc84e7a`.

[^claude-md-kernels]: This repository, `CLAUDE.md`, "Animation kernels"
    section: "Kernels that won on static instruction count lost on the chip
    (mandala 0.85x, silk 0.73x of the compiler's own bandRef)."

[^lava-silk-untimed]: This repository, commit `564f64ca` ("bganim: run the
    lava, silk and Silk 2 kernels by default, glue proven on the host"),
    commit message.

[^silk-ab-result]: This repository, commit `817f7608` ("bganim: turn
    silk's kernels off after the device A/B"), commit message: "Silk 2
    wins at 1.16x and lava ties at 0.99x... Silk loses: band() 13.1 ms
    against bandRef() 11.4 ms in the production A/B and 0.86x in the
    animtest bracket."

[^aurora-rowpair]: This repository, commit `b9ac0e42` ("bganim: aurora
    renders one row per pair and keeps its row LUT across bands"), commit
    message: "the first integration, which copied inside the call's
    buffer, failed the interlace check on exactly those shapes."

[^lava-rowpair]: This repository, commit `49050d67` ("bganim: lava renders
    one row per pair"), commit message: "The interlace check caught the
    first cut, which fell back to the row's own phase when the partner was
    absent."

[^mandala-rowpair]: This repository, commit `24d5cd76` ("bganim: mandala
    interpolates a half-resolution sample grid"), commit message and
    accompanying comment in `AnimMandala.cpp`.

[^ember-rowpair]: This repository, commit `7ae4dc2a` ("bganim: ember
    computes the field per row pair and dithers every row on the PIE"),
    commit message and comment in `AnimEmber.cpp` referencing
    `AnimLava.cpp`'s "same integration bug."

[^interlace-check]: This repository, `tools/animbench/interlace_check.cpp`,
    file header comment: "Checks the band() call-shape invariant that
    SleepAnimation's interlaced half-res path depends on... An optimization
    that caches derived rows across band() calls can break one of those
    shapes while leaving the ordinary full-band render... perfectly
    bit-exact."

[^baseline-calibration]: This repository, `tools/animbench/BASELINE.md`,
    "Calibration" section.

[^asmbrief-device]: This repository, `tools/animbench/ASM_BRIEF.md`,
    section "Where the fleet stands on the device (2026-09-04, production
    build)."

[^asmbrief-planacount]: This repository, `tools/animbench/ASM_BRIEF.md`,
    same section as above: "Plasma's compiled inner loop is 15 instructions
    per pixel pair inside a hardware loop and still costs 14 cycles per
    pixel on the device, so instruction count is roughly half the story
    and scheduling and memory are the other half."

[^silk-probe]: This repository, commit `2d314fdb` ("bganim: silk on a
    16-pixel grid with paired stores, palette pad 16"), commit message.
