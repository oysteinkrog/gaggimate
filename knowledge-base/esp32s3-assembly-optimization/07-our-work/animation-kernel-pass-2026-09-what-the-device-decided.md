---
title: The animation kernel pass of September 2026, and what the device decided
id: 07-our-work/animation-kernel-pass-2026-09-what-the-device-decided
schema_version: 1
doc_type: explanation
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, pie, kernels, measurement, gcc14, our-work]
confidence: high
---

# The animation kernel pass of September 2026, and what the device decided

Over two days in September 2026 the hot path of every background
animation in this repo was rewritten as hand-written Xtensa LX7 assembly,
measured on the board, and then partly reverted. The pass is worth
recording because the reverts are the finding. Of the kernels that were
written, some beat the compiler by a factor of two, some lost by a
quarter, and the ones that lost had all looked better on paper first.

The animations are 14 registered entries in
`src/display/ui/default/bganim/`, each with an `init`, `frame`, `band`
and `release`.[^1] `band()` fills a horizontal strip of the 480x480
panel. On the device it is called 240 times per frame with two rows each,
so per-call setup is paid 240 times and the per-pixel loop runs 230,400
times.[^2] That is the whole subject of the pass.

## The shape of the deliverable

Every animation keeps its portable C++ implementation as `bandRef`, a
trailing field on the `BgAnimation` registry struct.[^1] `bandRef` is the
spec. The host bench runs it against stored golden frames, and the
on-device equivalence test runs the assembly `band()` against it pixel for
pixel.[^3] Assembly lives inside the animation's own file under
`#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)`, in `noinline`
functions taking plain pointers and integers so the same function can be
dropped into a QEMU test unchanged.[^4]

Three animations carry a build flag that selects the kernel or `bandRef`
at compile time: `GM_BGANIM_LAVA_ASM`, `GM_BGANIM_SILK_ASM` and
`GM_BGANIM_SILK2_ASM`. The other eleven are gated only on the target.[^5]
The flags exist because those three kernels were written while the bench
board was offline, so their defaults had to be decided later by
measurement rather than at the time of writing.[^6]

## The verification ladder

Four rungs, in order, and nothing below rung four is evidence about
speed.[^7]

1. **Host goldens.** `tools/animbench`, `make check`. The portable path
   must reproduce the stored frames.
2. **The real device compiler.** `tools/animbench/xtensa-asm14.sh <name>`
   compiles the file with the firmware's own GCC 14.2 and flags. The
   object proves every `ee.*` mnemonic and operand form assembles; the
   listing shows what GCC did around the block. An older script using a
   GCC 8.4 toolchain the firmware does not use was retired for this
   reason.
3. **Bit-exact execution in QEMU.** `tools/qemubench/build.sh
   tests/anim_<name>` then `run.sh`, which greps for
   `GM_QEMUBENCH_PIE: PASS`. The kernel and a plain C reference run over
   synthetic inputs covering the operand ranges.
4. **The device.** `/api/debug/animtest?anim=N` renders 8 frames across 3
   parameter sets through `band()` and `bandRef()` back to back with
   alternating order and reports `mismatch_px`, which must be zero.
   `/api/debug/anim?useref=1` swaps the render loop to `bandRef` so the
   speed claim is measured under production conditions instead of
   estimated.

The generic reasoning behind rungs 1 and 3 is in
[bit-exact reference tests and fuzzing](../05-measurement/bit-exact-reference-tests-and-fuzzing.md)
and
[QEMU for the ESP32-S3](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md).

## Where the fleet started

The first round's baseline, taken on the bench board at full resolution
with interlace pinned off, is the reason the pass happened at all. The
device ran 2 to 3 times worse than a host measurement scaled by clock
ratio predicted, across almost the whole fleet.[^8] Plasma's compiled
inner loop was 15 instructions per pixel pair inside a hardware loop and
still cost about 14 cycles per pixel on silicon.[^8] The gap is load-use
stalls, spills out of a 16-register window, and cache misses on tables
larger than the 32 KB external-memory cache.

## The outcome, animation by animation

`band` time is milliseconds for one full 480x480 frame, interlace off,
default parameters. The "old" column carries two numbers because the same
code measured differently depending on whether its tables happened to land
in internal SRAM or in PSRAM, which is the placement finding below. All
numbers `[measured]` on the bench board, commit `8fc84e7a`, 2026-09-04.[^9]
Kernel shapes are as they stood at that landing. Aurora, lava, ember and
mandala were redesigned the following day, and that table is further down.

| id | animation | kernel shape | old (SRAM / PSRAM) | after round 4 |
|---|---|---|---|---|
| 0 | plasma | scalar row kernel, 4 pixels per iteration, hardware loop, index kept in a register | 9.1 / 17.8 | 8.7 |
| 1 | lava | none shipped in round 4 | 26 to 36 either way | unchanged |
| 2 | silk | per-cell kernels, written then compiled out | 18.2 / 27.1 | 17.7 |
| 3 | starfield | scalar vignette gather, 4 pixels per iteration, MIN instead of branches, paired stores | 15.6 / 27.8 | 14.8 |
| 4 | aurora | hand-scheduled scalar over the coarse column grid, no PIE | 46.7 / 67.6 | 35.9 |
| 5 | ripples | PIE tile fill plus a scalar band accumulator | 13.2 / 13.3 | 3.2 |
| 6 | caustics | scalar span kernel, register allocation fixed | 22.6 / 37.5 | 15.2 |
| 7 | mandala | scalar interpolation kernel | 44.4 / 50.1 | 42.7 |
| 8 | orbits | PIE flat fill, 8 RGB565 pixels per 128-bit store | 6.1 / 7.0 | 3.5 |
| 9 | fireflies | PIE broadcast fill plus a scalar glow span | 10.9 / 10.0 | 7.4 |
| 10 | steam | PIE row fill, 8 pixels per store in a hardware loop | 8.4 / 10.8 | 6.9 |
| 11 | ember | scalar field and flicker kernels plus a scalar gather | 35.0 / 41.6 | 33.5 |
| 12 | nebula | PIE row interpolation with byte widen and narrow, scalar gather | 40.2 / 39.6 | 30.7 |

Those are before-and-after times for the whole pass, which mixes the
kernel with table placement and algorithm changes. The ratio that isolates
the kernel is `band()` against the same animation's own `bandRef` at the
same table placement, measured through the production A/B. Where that
ratio was published it reads:[^10]

| animation | band() vs bandRef | default | flag |
|---|---|---|---|
| ripples | 3x to 5x, depending on ring state | on | none |
| orbits | 2.0x | on | none |
| caustics | 1.5x | on | none |
| nebula | 1.3x | on | none |
| Silk 2 | 1.16x | on | `GM_BGANIM_SILK2_ASM`, default 1 |
| lava | 0.99x | on | `GM_BGANIM_LAVA_ASM`, default 1 |
| mandala | 0.85x | on | none |
| silk | 0.73x | off | `GM_BGANIM_SILK_ASM`, default 0 |

The remaining animations were not given a separately reported ratio. The
lava and silk ratios were settled a day later than the rest, in commit
`817f7608`, after the first silicon run of the three kernels written with
the board offline.[^11]

## The wins, and what they had in common

The four clear wins are not the four cleverest kernels. They are the four
places where the compiler was doing something a human could remove
outright.

**Ripples and orbits win on stores, not arithmetic.** Orbits writes a flat
background across the whole frame, 230,400 identical values, and has
almost no per-pixel work otherwise: the paths and bodies are a few hundred
scattered writes.[^12] Replacing the compiler's store loop with
`ee.vst.128.ip`, one 128-bit store per eight RGB565 pixels, removes most of
the store instructions and every loop branch with them. Ripples is the same
story with a four-pixel tile pattern instead of one colour, plus a scalar
prefix and suffix because a span's start is an arbitrary edge and the PIE
store masks its low four address bits silently instead of trapping.[^13]

**Caustics wins on register pressure.** The compiler's own listing showed
the span loop spilling six loop-invariant values to the stack and
reloading them on every four-pixel span.[^14] The kernel is the same
algorithm with the invariants held in registers. Nothing was vectorised:
the two per-pixel lookups are data-dependent gathers, and this core's PIE
extension has no multi-lane gather.

**Nebula wins by attacking the right loop.** The two dominant-octave row
tables run 255 and 256 iterations per row to feed a pixel loop that is
only 240 wide at the half resolution this panel renders at. Deleting
them outright moved the device from 33.6 to 44.8 frames per second, which said the row
builders and not the per-pixel arithmetic were the cost.[^15] Earlier
passes had gone at the pixel loop.

The common shape: each win removed work the compiler could not remove,
rather than issuing the same work in a better order.

## The losses, and why they lost

**Mandala at 0.85x and silk at 0.73x** both won on static instruction
count and lost on silicon.[^10] The reason is that the device pays for
load-use stalls and cache misses, and an instruction count measures
neither. Separately, both of the pass's designs that decoded indices into
a PIE scratch buffer lost to simply keeping the index in a register: the scratch
buffer costs a store per pixel out of the vector kernel and a load per
pixel back into the gather, memory traffic the compiler's version never
pays.[^16]

Plasma reached the same conclusion one round earlier and kept the result.
Its round-one PIE kernel was bit-exact and assembled clean and still only
matched `bandRef` at 0.98x, because GCC had already compiled the
shift-and-mask into a single `extui` and the pairwise loop into a hardware
zero-overhead loop with one unhidden load-use stall.[^17] The saving
available to PIE over one `extui` per pixel is a quarter of a vector
instruction, and the scratch buffer spent more than that.

**Silk lost three times.** Two kernels written for the eight-pixel grid
lost on the device in the first rounds. A third pair, ported to the
sixteen-pixel grid with paired stores, was 63 instructions against GCC's
73 for the fast cell and 20 per pixel pair against 21 for the exact
cell.[^18] On the board it measured 13.1 ms for `band()` against 11.4 ms
for `bandRef` in the production A/B, and 0.86x in the equivalence-test
bracket.[^11] `GM_BGANIM_SILK_ASM` was set to 0 and `bandRef` renders. The
kernels stay in the file, bit-exact and fuzzed, behind
`-DGM_BGANIM_SILK_ASM=1` for a future attempt.

Getting that third silk pair to compile was not wasted, though. Matching
`band()` to `bandRef` exposed four real bugs in dispatch code that had
never compiled since the grid changed: a left shift of a possibly negative
value, the fast cell called with the wrong table pointer, a missing
pairing factor in the exact cell's step, and a tail loop indexing two
things wrongly at once.[^18]

## The ties, and what a tie is worth

Lava at 0.99x and Silk 2 at 1.16x are both straight transcriptions of
GCC's own loop, with one edge added that the compiler could not take: a
hardware zero-overhead loop closing the body instead of an
increment-and-branch pair, or a walking pointer instead of a base plus
index.[^19] Silk 2's kernel is 20 instructions per pixel pair, the same
count the compiler already reached, because its two palette gathers are
data dependent and nothing upstream of them can be hoisted.[^20] Parity
was the honest expectation and parity is roughly what happened.

Both stayed on. A tie that is bit-exact, fuzzed and understood is worth
keeping, because it is a known starting point for the next attempt. A loss
is not, which is why silk's flag went to zero rather than the kernels being
deleted.

## Table placement beats instruction count

The single largest effect in the pass was not in any kernel. The same
kernel ran 1.3 to 2 times slower with its per-pixel tables in PSRAM than
in internal SRAM: plasma 9.1 against 17.8 ms, caustics 22.6 against 37.5,
lava 26.0 against 47.7, aurora 46.7 against 67.6.[^9]

Worse, before the fix, placement was not a decision at all. Tables were
allocated at `init()` against whatever the free internal pool happened to
be, and the pool idled within a few kilobytes of its reserve, so the same
table landed in SRAM on one boot and PSRAM on the next. Band time swung by
a factor of two per boot, and how much internal memory was left for the
network stack depended on which animation was running.[^21]

The fix makes placement a decision in source. A fixed 12 KB slab is
reserved statically; `bganim::allocHot` serves from it and `bganim::alloc`
always returns PSRAM. Of the slab, 3,072 bytes hold shared trigonometric
tables for the whole boot and 9,216 bytes belong to whichever animation is
resident. A table that does not fit falls back to PSRAM and increments a
counter visible on a debug endpoint. An animation that needs more than its
share shrinks a table; it does not get more slab. Static tables are not a
way around this, because they come out of the same internal pool.[^21]

The rule this leaves: `allocHot` for tables read per pixel or per row,
ranked by reads per frame within the 9,216 bytes, and `alloc` for bulk
sequential sweeps. Lava's round-five change is the rule applied: narrowing
its lookup table to 16-bit entries with 9 index bits made it fit the slab,
and production time fell from 35.6 to 25.0 ms because the table had been
in PSRAM.[^22] The general memory background is in
[caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md).

## When the answer was the algorithm, not the kernel

By 2026-09-05 several animations were at the floor of what their kernel
could give, and the next gains came from computing fewer pixels. Four
redesigns landed, each measured rather than argued.[^23]

| animation | change | measured |
|---|---|---|
| aurora | row lookup table kept across bands and rebuilt only on background change, then one row computed per vertical pair | 25.5 to 9.5 ms on the rig |
| lava | one row computed per vertical pair, dither phase indexed by row pair | 19.1 to 10.1 ms on the rig |
| ember | field computed once per row pair, per-row dither moved onto the PIE with `ee.vadds.s8` | 29.6 to 19.4 ms in production, which moves a 58 ms frame to 39 ms |
| mandala | half-resolution sample grid with linear fill, an exact 64x64 patch at the centre, and a full-resolution guard when the ring wavelength approaches the sampling pitch | 42.9 to 21 to 25 ms in production |

Each of these trades picture for time, so each carries a measured golden
difference: aurora 0.65 to 0.87 of 255, lava 0.9 to 1.3, ember 0.13 to
0.23, mandala 1.30 to 1.46, all of it grain.[^23] Mandala's commit also
records two cheaper variants that were measured and rejected: horizontal
interpolation with duplicated rows at 21.3 ms showed visible row steps,
and plain duplication at 17.1 ms showed a comb pattern.[^24] Ember
similarly measured whole-row doubling at 18.4 ms and rejected it for its
1x2 grain.[^25]

## Four traps this pass paid for

**QEMU agreement is not silicon agreement.** Ember's PIE dither uses
`ee.vadds.s8`, an 8-bit signed saturating add. This QEMU fork's model of
that instruction floors at -127. The reference implementation was first
written to the emulator, and then the on-device test, sweeping the
parameter sets, found two pixels at the all-100 set where the silicon had
floored at -128. The reference is now written to the silicon, and the QEMU
test carries both floors so the emulator rung still passes and reports
which lanes it could not judge.[^25] A single-instruction probe,
`tools/qemubench/tests/probe_vadds_s8`, is what isolated it.

**A row's pixels must depend on its absolute y, never on which other rows
share the call.** Production's interlaced path calls `band()` with one row
and parity-skipping sequences, and the half-resolution path hands it
240-wide rows. Three of the four redesigns above first produced a version
that copied from a neighbour inside the call's own buffer, or picked the
source row from a call-local offset. Every one passed the golden diff and
would have painted wrong rows on the device. None reached the board: the
interlace check caught the first at integration. The shape that survives
derives everything from the pair row `y & ~1` and copies only when the
partner is in the same call.[^26] The check that catches it is the
interlace check in `tools/animbench`.

**A fuzzer without sanitizers is not a fuzzer.** Silk shipped a palette
pad of 4 against a dither amplitude the code caps at 16, so at default
parameters it read past its lookup table. Without ASan the overrun reads
the neighbouring byte and the test passes.[^27] Any animation with an
unclamped padded gather sizes its pad from that cap, and a change to a
table's layout re-runs the fuzz for the whole fleet.

**The bench rig cannot time a design that caches a row across calls.** The
on-device kernel bench times each band as the minimum of several
back-to-back calls, so repeats hit the cache and sample nothing, or miss
twice per call. Mandala's row-caching design was therefore timed by the
production A/B instead.[^24] The rig taught another lesson the same
evening: never reflash the board while a round is running, or the
comparison rows become a snapshot of whatever the tree held at build
time.[^28]

## What the repo carries forward

Three rules came out of the pass and now sit in the repo's own agent
notes, which is where a future kernel writer meets them first.[^29]

- **Table placement beats instruction count.** Decide it in source with
  `allocHot` and `alloc`, ranked by reads per frame.
- **GCC 14's schedule is the baseline, not the target.** Transcribe the
  compiler's loop first, then look for an edge it cannot take. The wins
  came from a closed hardware loop, a walking pointer, or a store the
  compiler issued one pixel at a time.
- **A production kernel never writes CPENABLE.** FreeRTOS enables the
  floating-point and vector units lazily per task through the
  coprocessor-disabled exception, which is also how another task's
  coprocessor state gets saved. A kernel that sets the register itself
  skips that and can corrupt another task's floating-point state. The
  bare-metal QEMU harness sets it once in its own `main`.[^30] See
  [coprocessors, CPENABLE and lazy context](../00-foundations/coprocessors-cpenable-and-lazy-context.md).

The blunt version, and the reason this leaf exists: instruction counts and
host timings predicted wins that the device reversed in 4 of 13
animations.[^29]

[^1]: `src/display/ui/default/bganim/BgAnimRegistry.cpp` (registration order is the persisted animation id, 14 entries as of 2026-09-06) and `BgAnim.h`, where `bandRef` is a field of `struct BgAnimation` with the same signature as `band`.
[^2]: `BAND_H` is 2 on the device, so a 480-row frame is 240 `band()` calls. Stated in `tools/animbench/ASM_BRIEF.md` (2026-09-04) and used throughout the animation sources.
[^3]: `tools/animbench` (host goldens) and `SleepAnimation::runAnimTest`, reachable at `/api/debug/animtest`. Described in `tools/animbench/ASM_BRIEF.md`, "Verification ladder", and in the repo root `CLAUDE.md`, "Animation kernels".
[^4]: `tools/animbench/ASM_BRIEF.md`, "Deliverable shape", item 3. Confirmed in the sources: every `Anim*.cpp` gates its kernels on `#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)`.
[^5]: `src/display/ui/default/bganim/AnimLava.cpp` line 354, `AnimSilk.cpp` line 248, `AnimSilk2.cpp` line 134. Each is an `#ifndef` guarded `#define` with the default in the file; none appears in `platformio.ini`.
[^6]: Commits `05b9a460` (2026-09-05, lava), `c73d80f3` (2026-09-05, silk), `fdac2de7` (2026-09-05, Silk 2). Each commit body states the bench board was offline and that nothing in it is timed.
[^7]: Repo root `CLAUDE.md`, "Animation kernels", and `tools/animbench/ASM_BRIEF.md`, "Verification ladder (all four rungs, in order, evidence in the report)".
[^8]: `[measured]` `tools/animbench/ASM_BRIEF.md`, "Where the fleet stands on the device (2026-09-04, production build)", taken with `anim_devbench.py` on the bench board at full resolution with interlace pinned off. The same section states the device is 2 to 3 times worse than the host-scaled estimate and gives plasma's 15 instructions per pixel pair against roughly 14 cycles per pixel.
[^9]: `[measured]` commit `8fc84e7a` (2026-09-04), commit body, "Production band time, ms per full-res frame with interlace off, old code with tables in SRAM by luck / in PSRAM, then now".
[^10]: `[measured]` repo root `CLAUDE.md`, "Animation kernels", second bullet, added in commit `189c59a6` (2026-09-04) and amended by `817f7608` (2026-09-05). Ratios are `band()` against the same animation's `bandRef` on the device.
[^11]: `[measured]` commit `817f7608` (2026-09-05), commit body: Silk 2 at 1.16x and lava at 0.99x stay on; silk measures 13.1 ms for `band()` against 11.4 ms for `bandRef` in the production A/B and 0.86x in the animtest bracket, so `GM_BGANIM_SILK_ASM` defaults to 0. The fleet equivalence run reported 14 of 14 at zero mismatched pixels.
[^12]: `src/display/ui/default/bganim/AnimOrbits.cpp` line 350 and following, the comment above `fillBgPie`: 480x480 at 25 frames per second is 230,400 background writes a frame, all the same constant, while path points and orbit bodies are a few hundred scattered writes.
[^13]: `src/display/ui/default/bganim/AnimRipples.cpp` line 574 and following, "Kernel A". The address masking is documented in the ESP32-S3 Technical Reference Manual; see [PIE load, store and alignment](../02-pie-vector/pie-load-store-and-alignment.md).
[^14]: `src/display/ui/default/bganim/AnimCaustics.cpp` line 470 and following: the compiled listing showed the span loop spilling six loop-invariant values and reloading them per four-pixel span. The same comment states the kernel stays scalar because the two per-pixel lookups are data-dependent gathers.
[^15]: `[measured]` `src/display/ui/default/bganim/AnimNebula.cpp` line 292 and following: removing the two dominant-octave row tables outright measured 33.6 to 44.8 frames per second on the device.
[^16]: Repo root `CLAUDE.md`, "Animation kernels", second bullet: both PIE-decode-into-scratch designs lost to keeping the index in a register.
[^17]: `[measured]` `src/display/ui/default/bganim/AnimPlasma.cpp` line 198 and following: the round-one PIE kernel measured 0.98x of `bandRef` with tables in SRAM, and the compiled `bandRef` already used a single `extui` and a hardware zero-overhead loop.
[^18]: Commit `c73d80f3` (2026-09-05), commit body: 63 instructions against GCC 14's 73 for the fast cell, 20 per pixel pair against 21 for the exact cell, and the four dispatch bugs the port exposed.
[^19]: Commit `05b9a460` (2026-09-05) for lava's two kernels, and commit `564f64ca` (2026-09-05), which turned all three flags on after proving the dispatch glue on the host through portable twins.
[^20]: Commit `fdac2de7` (2026-09-05), commit body: `silk2PairRowAsm` is a mnemonic-for-mnemonic transcription at 20 instructions per pair, and the two palette gathers are data dependent, so a walking pointer removes nothing upstream.
[^21]: Repo root `CLAUDE.md`, "Internal DRAM budget", the `bganim::allocHot` bullet, added in commit `189c59a6` (2026-09-04). The slab itself landed in commit `78a13bc5`.
[^22]: `[measured]` commit `b81633e3` (2026-09-04), round 5: lava's lookup table as 16-bit entries with 9 index bits took the rig from 20.77 to 19.08 ms and production from 35.6 to 25.0 ms, because the table had been in PSRAM.
[^23]: `[measured]` commits `b9ac0e42` (aurora), `49050d67` (lava), `7ae4dc2a` (ember) and `24d5cd76` (mandala), all 2026-09-05. Golden differences are quoted from the same commit bodies; each commit regenerated its animation's goldens deliberately.
[^24]: `[measured]` commit `24d5cd76` (2026-09-05), commit body, including the note that the rig's per-band minimum cannot time a design that caches a row across calls, so the number is a production measurement.
[^25]: `[measured]` commit `7ae4dc2a` (2026-09-05), commit body: whole-row doubling measured 18.4 ms in production and was rejected for its 1x2 grain; the `ee.vadds.s8` saturation floor differed between this QEMU fork and silicon, and `tools/qemubench/tests/probe_vadds_s8` is the probe that found it.
[^26]: Commit `b9ac0e42` and commit `49050d67` (both 2026-09-05) each record an integration that failed the interlace check on exactly the single-row and parity-skipping call shapes. The rule is stated in the repo root `CLAUDE.md`, "Animation kernels", final bullet.
[^27]: Repo root `CLAUDE.md`, "Animation kernels": without ASan a one-entry table overrun reads the neighbouring byte and passes, which is how silk shipped a palette pad of 4 against a dither amplitude capped at 16. The fuzz harness is `tools/animbench/Makefile.fuzz`.
[^28]: Repo root `CLAUDE.md`, "Animation kernels", the `display-kdev` bullet, and `tools/kblob/README.md`.
[^29]: Repo root `CLAUDE.md`, "Animation kernels", introduced in commit `189c59a6` (2026-09-04) and amended in `564f64ca` and `817f7608` (2026-09-05). The count of reversals is stated there.
[^30]: Commit `8fc84e7a` (2026-09-04), commit body, and the repo root `CLAUDE.md` bullet of the same wording. The mechanism is the coprocessor-disabled exception described in the Xtensa ISA reference manual and implemented by FreeRTOS in ESP-IDF.
