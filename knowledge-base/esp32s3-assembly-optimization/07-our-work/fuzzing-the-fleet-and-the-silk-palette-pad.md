---
title: "Fuzzing the animation fleet, and the silk palette-pad defect"
id: 07-our-work/fuzzing-the-fleet-and-the-silk-palette-pad
schema_version: 1
doc_type: explanation
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, our-work, fuzzing, sanitizers, memory-safety]
confidence: high
sources:
  - "gaggimate repo, tools/animbench/fuzz.cpp (HEAD)"
  - "gaggimate repo, tools/animbench/Makefile.fuzz (HEAD)"
  - "gaggimate repo, tools/animbench/Makefile (HEAD, `check` target)"
  - "gaggimate repo, tools/animbench/check-params.py (HEAD)"
  - "gaggimate repo, src/display/ui/default/bganim/AnimSilk.cpp:67-83,286-320 (HEAD)"
  - "gaggimate repo, src/display/ui/default/bganim/BgAnimCommon.cpp:128-150 (allocHot), :230-260 (ditherAmp)"
  - "gaggimate repo, commit e2f15ae357187ed7802a5596dd1e84483f73d5d6 (fuzz.cpp created, lava LUT margin fix, 2026-08-15)"
  - "gaggimate repo, commit 2d314fdb (silk palette pad 4 -> 16, 2026-09-04)"
  - "gaggimate repo CLAUDE.md, 'Animation kernels' section"
---

# Fuzzing the bganim fleet, and the silk palette-pad defect

GaggiMate's display firmware runs fourteen background animations
(`src/display/ui/default/bganim/Anim*.cpp`) on an ESP32-S3. Each animation
renders a 480x480 frame in bands (two rows per call on the device) through a `band()` function tuned by
hand for the chip's Xtensa LX7 core. The project fuzzes this code with a
dedicated harness, `tools/animbench/fuzz.cpp`, because the ordinary
performance benchmark cannot see the two bug classes that hand-tuned,
fixed-point, pointer-heavy kernels actually risk. This document explains what
the fuzzer drives, the defect it caught in the "silk" animation's palette
lookup, and how the fuzzer sits alongside the project's two other automated
checks. See
[bit-exact reference tests and fuzzing](../05-measurement/bit-exact-reference-tests-and-fuzzing.md)
for the general verification method this is one instance of.

## What the fuzzer drives

`tools/animbench/fuzz.cpp` links the same animation sources the firmware
ships (`$(wildcard $(ANIM_DIR)/*.cpp)` in `tools/animbench/Makefile.fuzz`)
into a small host program. For every registered animation it renders through
three sweeps at once:

- **Parameters.** Every animation exposes up to four 0-100 knobs. The
  harness renders the defaults, both all-extreme corners, every single
  parameter pushed to 0 and to 100 on its own, and 24 pseudorandom
  combinations. The header comment states the reason plainly: "every
  parameter is 0-100 and every combination is reachable from the web UI, so
  works at the defaults proves nothing."
- **Themes.** Each animation's palette is rebuilt from a colour theme with a
  variable stop count (2 to 8). The fuzzer rotates every parameter set across
  the built-in themes plus two synthetic ones it builds itself: a 2-stop
  theme and an 8-stop theme, specifically to stress a palette walker sized
  around the 6-stop built-ins at both ends.
- **Time.** `tMs` is `millis()`, which grows to about 4.2 billion before it
  wraps a `uint32_t`. The harness probes six values: 0, 33 ms, 100 s, 5000 s,
  1 billion ms, and one step short of the wrap, so fixed-point time math that
  is exact at small `t` but overflows at large `t` gets exercised.

Each 16-row band is rendered into its own exactly-sized heap block
(`std::vector<uint16_t> dst(W * BAND_H)`), not one shared full-screen
framebuffer. That is the mechanism that makes the fuzzer useful: a `band()`
that writes outside its assigned rows or past its row width corrupts heap
metadata immediately next to a real allocation, which AddressSanitizer traps
as an overflow. Rendered into one big framebuffer, as the plain performance
benchmark does, the same write would land on an unrelated but valid pixel and
never be noticed.

## How the sanitizers are wired in

`Makefile.fuzz` builds with
`-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g`. The Makefile's
own comment names the reason for existing at all: "the sanitizers are the
point of this harness... without them a one-entry table overrun reads a
neighbouring byte and passes." On WSL1 the binary must run with
`ASAN_OPTIONS=verify_asan_link_order=0`, because libasan is not first in the
process's initial library list there and AddressSanitizer's own startup
check would otherwise abort before the animations run; the Makefile marks
that link-order check as advisory on this platform, not a real safety
guarantee being waived.

## The silk defect: a palette pad sized from a guess, not the caller's cap

`AnimSilk.cpp` builds its output colour by summing a contrast term and a
vignette term into a fixed-point index, adding a per-pixel dither offset,
and reading `paletteExt[PAD + idx]` unclamped and unbranched: the same
padded-gather pattern `AnimEmber.cpp` uses. The dither offset comes from
`ditherAmp()` (`BgAnimCommon.cpp:230`), which measures how many distinct
RGB565 steps the current 256-entry palette actually has (a flat, few-step
palette: exactly what a 2-stop custom theme produces) and returns half the
palette's step spacing, capped at 16.0 index units. So `idx` can legally
land at plus or minus 16 outside the raw 0-255 range whenever a coarse
enough palette drives the dither term to its cap.

Silk shipped with `PAD` set to 4. As the source comment at
`AnimSilk.cpp:79` records: "with PAD=4 the fuzz harness under ASan read one
entry past `g_lut` at a parameter set that produced a coarse palette
(2026-09-04)." Four entries of headroom covered ordinary use but not the
16-unit cap `ditherAmp()` can actually return, and nothing at build time
connected the two numbers: `PAD` was a constant picked once, `ditherAmp()`'s
cap was changed independently in an earlier dither pass. Without
AddressSanitizer this reads the byte or entries adjacent to the palette
table in the shared LUT slab and returns a plausible-looking but wrong
`uint16_t` colour: not a crash, not a visible artifact reliable enough to
notice by eye, which is exactly the failure mode the project's
CLAUDE.md calls out: "without ASan a one-entry table overrun reads the
neighbouring byte and passes."

The fix landed in commit `2d314fdb` ("silk on a 16-pixel grid with paired
stores, palette pad 16", 2026-09-04), bundled with an unrelated performance
change from the same optimization pass (widening silk's sampling grid from
8 to 16 pixels and pairing pixel stores). The commit message states the fix
plainly: "the palette pad goes from 4 to 16: `ditherAmp()` caps the dither
at 16 index units, and under ASan the fuzz read one entry past the LUT at a
parameter set that produced a coarse palette." `PAD` now equals the caller's
actual worst case instead of an arbitrary small number.

## The rule this defect established

Root `CLAUDE.md`'s "Animation kernels" section states the generalization
directly: "any animation with an unclamped, padded gather sizes its pad from
that cap, and a change to a table's layout re-runs the fuzz for the fleet."
Two things follow from the silk case:

- **Size the pad from the producer, not from habit.** A padded, unclamped
  lookup is only as safe as the widest value its own index expression can
  produce. That bound has to be read out of the function that supplies the
  index (here, `ditherAmp()`'s documented 16.0 cap) and asserted or commented
  at the point the pad constant is declared, the way `AnimSilk.cpp:79`
  now documents the full derivation. Guessing a small pad and hoping is what
  produced the bug.
- **A table-layout change re-runs the fuzz for the whole fleet, not just the
  animation being edited.** The fuzzer isn't scoped per-animation in
  practice: `fuzz.cpp` iterates `bg_animation_count()` and runs every
  registered animation by default. `./build/fuzz --anim N` narrows to one
  animation only when a specific suspect is already known.

The silk pad is not the fuzzer's only catch. Its creation commit
(`e2f15ae3`, 2026-08-15) found a bug in `AnimLava` on its first run: the
forward-difference accumulator's rounding error in a Q12.20 step could
reach 57 LUT buckets past the table's 32-bucket margin at a full 480-pixel
scan, so the fix raised that table's margin to 128 entries. Two different
animations, two different arithmetic mistakes, the same fuzz harness: that
is the argument for keeping it wired into the fleet's checks rather than
treating either fix as a one-off.

## Where this sits next to the other two checks

The fleet runs three automated checks, and none of them subsumes another:

- **The golden compare** (`tools/animbench/Makefile`'s `check` target, and
  `OPTIMIZE.md`'s workflow instructions) renders 240 frames and diffs
  against pre-optimization reference frames in `golden/`, reporting mean and
  max RGB888 error per animation ("OK" is roughly mean <= 3.0, max <= 48).
  This catches a kernel that changed the *picture*: a rewrite that drifts
  the visible output. It has no way to catch an out-of-bounds read that
  happens to return a colour close enough to the correct one, or a heap
  write that lands somewhere the golden comparison never samples.
- **`check-params.py`** diffs the firmware's parameter registry
  (`BgAnimRegistry.cpp` and the `Anim*.cpp` structs) against the web UI's
  mirror (`web/src/config/bgAnimations.js`), because a silently dropped
  parameter key makes `bg_parse_params()` feed a zero default with no build
  warning. This is a table-*consistency* check between two independent
  copies of the same data; it has nothing to say about memory safety inside
  a single animation's `band()`.
- **The fuzzer** is the only one of the three that exercises out-of-range
  memory access directly, by construction (the per-band exact-sized heap
  block), across the full parameter/theme/time space rather than the
  defaults the other two checks implicitly run at.

A change to any one of these does not stand in for the other two; the
silk defect specifically is a case a golden-frame diff alone would very
likely have missed, since a coarse-palette-triggered one-entry overrun does
not reliably move a frame's mean or max error past the golden tolerance.

## What the fuzz cannot see

The fuzzer runs on the host, compiling the same `Anim*.cpp` sources the
firmware ships, but two device-only behaviours are outside what it
exercises:

- **Alignment masking in the hand-written Xtensa kernels.** The project's
  PIE (128-bit SIMD) assembly kernels for silk, lava, and Silk 2 are gated
  behind `GM_BGANIM_SILK_ASM`, `GM_BGANIM_LAVA_ASM`, and
  `GM_BGANIM_SILK2_ASM`, each defaulting to 0 (`AnimSilk.cpp:248-249`).
  `Makefile.fuzz` does not define any of these, so the fuzzer compiles and
  runs `bandRef()`, the portable C++ path: the same path production runs
  today, since silk's kernel currently loses to GCC's own compile and stays
  off by default. The `ee.vld.128.ip`/`ee.vst.128.ip` instructions those
  kernels use silently mask the low four address bits, an alignment
  assumption the fuzzer's portable-path runs never touch.
- **PSRAM-versus-SRAM table placement.** `allocHot()` (`BgAnimCommon.cpp:128`)
  runs the identical slab-fitting logic on host and device, so the fuzzer
  does check whether a table falls back to `alloc()` (PSRAM) when the hot
  slab is full. What it cannot check is the *consequence* of that fallback:
  on the host, a PSRAM-backed and an SRAM-backed table are read at the same
  speed, so a placement decision that is correctness-neutral but
  performance-critical on the real chip (root `CLAUDE.md`'s "table placement
  beats instruction count": the same kernel measured 1.3x to 2x slower with
  its tables in PSRAM instead of SRAM) produces no observable difference in
  the fuzz run. That gap is why the device-only `display-kdev` bench exists
  separately, timing kernels with the hot slab in its real, boot-time state
  rather than asserting only that the bytes are correct.
