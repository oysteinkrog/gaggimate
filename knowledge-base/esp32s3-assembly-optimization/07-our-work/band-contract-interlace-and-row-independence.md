---
title: "The band() contract: interlace, half resolution, and row independence"
id: 07-our-work/band-contract-interlace-and-row-independence
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, gaggimate, bganim, band, interlace, row-independence, testing, alignment]
confidence: high
---

# The band() contract: interlace, half resolution, and row independence

Every background animation in GaggiMate's display firmware renders through
one function, `band()`, and the render task calls it in more shapes than a
naive reading of "fill a horizontal strip" suggests. This page states the
contract a `band()` implementation must satisfy to render correctly under
every shape the firmware actually uses, why the contract exists, and how a
kernel proves it holds before it reaches the device.

## 1. The animation interface

An animation is a `BgAnimation` record of five function pointers: `init`
(lazy buffer and LUT allocation, sized from the render width and height),
`frame` (once per frame: advance particle state, rebuild per-row or
per-column terms, rotate palettes), `band` (fill a run of rows), an
optional `bandRef` (a portable C++ twin of a hand-written `band()`, used
only for on-device equivalence testing), and an optional `release` (frees
what `init` allocated)[^bganimh]. `band()`'s own signature is
`band(dst, y0, rows, w, tMs, p)`: fill `rows` rows starting at absolute row
`y0` into `dst`, `w` pixels wide, given the frame timestamp and up to four
0-100 user parameters[^bganimh]. Nothing below changes that signature; it
changes what a caller is allowed to assume about `y0` and `rows` across
successive calls.

## 2. BAND_H is 2 in production, and per-call setup is paid 240 times a frame

The render task calls `band()` once per band, `BAND_H` rows at a time, and
`BAND_H` is 2 on the device[^bandh]. At a 480-row panel that is 240
`band()` calls a frame, so whatever a kernel does once per call, before its
pixel loop starts, is paid 240 times a frame: a row-state builder is exactly
as hot as the pixel loop it feeds, not a fixed overhead that amortizes away.

`BAND_H` did not start at 2. It moved 16 to 12 to 8 (commit `7fede9c9`,
"perf(sleep-anim): BAND_H 16 -> 8") to 2 (commit `dc4fd6d3`, "bganim: make
the render path answer for itself under a camera"), each step halving the
band buffers to give the web server's WiFi frame copies more internal DRAM;
a later attempt at `BAND_H = 4` was benched on-device and reverted the same
day, since the band DMA is bandwidth-bound (a bigger transfer buys back no
per-transfer overhead) and the doubled internal slots pushed the
DMA-capable pool low enough to start timing out the web server[^bandh].

Two of this repo's own measurement tools still assume an older value, and
the mismatch is recorded rather than silently absorbed. `tools/animbench/bench.cpp`
benchmarks with `BAND_H = 8` by default (`GM_BENCH_BAND_H`), because its own
comment shows the choice of band height shifts individual animations'
relative host time by up to 9% in either direction, and `BASELINE.md`'s
numbers were taken at the value production used before `7fede9c9`, which
was 16[^benchbandh][^baseline]. Neither number is wrong for what it
measures; neither is directly comparable to a number taken at the device's
actual `BAND_H = 2` without saying so.

## 3. The call shapes production actually issues

`SleepAnimation.cpp`'s render loop does not always call `band()` once per
band with a contiguous row range. Two features change the shape of the
calls it issues, and a `band()` implementation is exercised by both.

**Row-level interlace at full resolution.** When a band is interlaced this
frame (`bandInterlaced`) and the render-half knob is set, `renderSkip` is
true and the frame renders only the rows it will actually push. At full
resolution (`!pairMode`) that becomes `splitRenderFull`: instead of one
`band()` call for the whole `BAND_H`-row band, the loop calls `band()`
once per row, with `rows == 1`, skipping every row whose parity does not
match this frame's parity[^splitrender]:

```cpp
if (splitRenderFull) {
    for (int r = 0; r < rows; r++) {
        if ((((y0 + r) ^ parityNow) & 1) != 0) {
            continue;
        }
        bandFn(band + static_cast<size_t>(r) * w, y0 + r, 1, w, tMs, p);
    }
} else {
    bandFn(band, y0, rows, w, tMs, p);
}
```

A full-resolution interlaced frame therefore issues a sequence of
solitary, `rows == 1` calls at every other absolute row, not one call per
band. This is production traffic, not a hypothetical the tests invented:
half the rows of an interlaced frame are never asked for at all, and the
half that are asked for arrive one at a time.

**Half resolution.** At half resolution the same `renderSkip` gate becomes
`splitRender`, and the loop calls `band()` once per half-width source row,
each `rows == 1`, again skipping rows of the wrong parity, into a half-width
scratch buffer that is expanded 2x in both axes afterward[^splitrender]:

```cpp
if (splitRender) {
    for (int sr = 0; sr < hrows; sr++) {
        if (((srcBase + sr) & 1) != parityNow) {
            continue;
        }
        bandFn(halfBuf + static_cast<size_t>(sr) * rw, srcBase + sr, 1, rw, tMs, p);
    }
}
```

`rw` there is `w / 2`, so a `band()` implementation must also render
correctly when handed 240-wide rows rather than 480-wide ones, since half
resolution renders at half the linear dimension in both axes, not at half
the row count alone.

## 4. The rule: a row's pixels depend only on its absolute y and the frame state

The comment threaded through every animation touched by the 2026-09-05
redesign pass states the rule the two call shapes above impose, and it is
the load-bearing sentence of this whole page: **a row's pixels, and
anything about it that varies row to row (a dither phase, a cached LUT
index), must be a pure function of that row's own absolute `y` and the
state `frame()` set up, and never of which other rows happen to share the
same `band()` call**[^aurora]. `BAND_H` rounds up (`(h + BAND_H - 1) /
BAND_H`), `SleepAnimation.cpp`'s row-level interlace path calls `band()`
with `rows == 1` for one row at a time, rendering only every other row each
frame, so a lone-row call is not a hypothetical shape to design around, it
is what the render task actually issues whenever interlacing is
active[^aurora].

## 5. The row-doubling bug three of four 2026-09-05 redesigns had

The 2026-09-05 redesign pass moved four animations (aurora, lava, mandala,
ember) to compute one row of a vertical pair and reuse it for the other,
trading some vertical smoothness for roughly half the per-pixel work. Three
of those four redesigns failed the interlace check before they were
integrated[^toolscommit].

**Aurora's first cut** computed the pair's row state from whichever row
happened to be first in a given `band()` call, and fell back to a lone
row's own state when there was no partner in the call. `tools/animbench/interlace_check.cpp`
caught it: a row's content and dither phase depended on which other rows
the same call happened to also request, which is exactly the rule in
section 4 being violated[^aurora].

**Lava's first cut** had the mirror-image bug: a lone row (the shape the
row-level interlace path actually calls) fell back to rendering its own
phase directly instead of the phase its pair partner would have used, so a
solitary call and a same-call pair produced different pixels for the same
row[^lava].

Both bugs share one symptom: they pass the ordinary golden-frame diff,
because `bench.cpp` and the golden PPMs only ever render full, contiguous
bands. Nothing about a whole-frame or whole-band render exercises a
solitary `rows == 1` call or a parity-skipping sequence, so a kernel that
special-cases "am I the first row this call saw" or "is my partner also
here" can be pixel-perfect against every golden image the fleet already
has and still paint the wrong rows on the device the moment interlacing
engages[^toolscommit].

Mandala's redesign (a different technique: a half-resolution sample grid
with linear interpolation, not row-pair-and-copy) and ember's redesign were
built call-shape-independent from the start and are the ones the tooling
confirmed without a fix cycle; mandala's one-row sample cache is
specifically documented as depending only on the row it was asked for,
verified against every shape `interlace_check` exercises before
shipping[^mandala].

## 6. The shape that passes

The fix, applied identically to aurora and lava, is to derive everything
from the pair's even row and treat the memcpy duplication as a pure
optimization that never changes output:

```cpp
const int ySrc = y & ~1;
uint16_t *out = dst + static_cast<size_t>(row) * w;
if ((y & 1) == 0 && row + 1 < rows) {
    // y starts a pair and y+1 is also in this call: compute once, duplicate.
    renderRow(out, ySrc, ...);
    memcpy(out + w, out, static_cast<size_t>(w) * sizeof(uint16_t));
    row += 2;
} else {
    // y is odd (its partner is the row behind it, not in this call) or
    // y is even but the call ends before y+1 (row-level interlace).
    // Either way, render ySrc's content directly.
    renderRow(out, ySrc, ...);
    row += 1;
}
```

The `memcpy` fires only when both rows of a pair land in the same call
(production's ordinary `rows == 2`, `y0` even, is exactly that case); every
other shape, including a solitary `rows == 1` call and a parity-skipping
sequence, renders `ySrc`'s content directly and gets the same pixels a
same-call pair would have produced for that row, just without the memcpy's
saving[^aurora][^lava]. A dither phase keyed on the row pair (`y >> 1`)
rather than on `y` itself is part of the same fix: keying it on `y` only
ever reaches the row-pair's even member at the point the row renderer sees
it, which shows four of an 8x8 Bayer matrix's eight row phases and produces
visible vertical stripes through a gradient instead of a checkerboard,
independent of the call-shape bug[^aurora].

## 7. Verification: the whole fleet, and one candidate before it touches src/

Two host tools check the rule in section 4, at two different points in a
kernel's life.

`tools/animbench/interlace_check.cpp` runs against every registered
animation. For each of several advanced frame states, it renders the same
state as a contiguous 8-row band (the reference, rendered twice as its own
purity control), as 16-, 4-, and 1-row contiguous bands, and as the two
parity-skipping `rows == 1` sequences production's interlaced path uses,
then reports the first pixel that differs from the reference and which
shape produced it. Every band height it tests divides 16, because some
animations (orbits, which bins its path points into fixed 16-row bins) read
exactly one bin per call and a band straddling a bin boundary loses path
pixels; a whole-frame single call is deliberately not one of the tested
shapes, since nothing in production ever calls `band()` that way, and using
it as the reference for orbits reports every legitimate shape as a
mismatch[^interlacecheck].

`tools/animbench/render_one.cpp`'s `--shapes` mode is the same check for
one animation descriptor named at compile time, registered in the fleet or
not, so a redesign can be checked before it is wired into
`BgAnimRegistry.cpp` at all. It runs the same shape set (8-, 16-, 4-, and
2-row bands, solitary rows, and both parity sequences) at both `480x480`
and `240x240`, the second because the half-resolution path hands `band()`
half-width rows and a design that is call-shape-safe at one width is not
automatically safe at the other[^renderone]. This is the tool that caught
aurora's and lava's first-cut bugs: three of the four 2026-09-05 redesigns
failed `--shapes` before integration, and the fix in every case was the
`y & ~1` derivation in section 6[^toolscommit].

## 8. Buffer alignment: 64-byte bands, 960-byte rows

The production band buffer is allocated 64-byte aligned
(`heap_caps_aligned_alloc(64, bandBytes, ...)`), and at the panel's 480
columns a row of `uint16_t` RGB565 pixels is 960 bytes, so every row inside
the buffer starts on a 64-byte, and so also a 16-byte, boundary regardless
of which row of the band it is[^alignment]. That matters specifically for
a hand-written PIE kernel: `ee.vld.128.ip` and `ee.vst.128.ip` load and
store 16 bytes and mask the low four address bits of the pointer silently
instead of trapping on a misaligned one, so a kernel that assumes `dst`
rows are 16-byte aligned is relying on this allocation choice, not on
anything the `band()` signature itself guarantees[^alignment]. A kernel's
own tables need the same alignment made explicit (`bganim::alloc` returns
only ordinary heap alignment; a table read with `ee.vld.128.ip` must be
over-allocated and aligned by hand, or declared `alignas(16)` if it is a
small constant)[^alignment].

## 9. What frame() may do that band() may not

The two functions carry different budgets because of how often each runs.
`frame()` runs once per rendered frame, so a libm call or a float divide
there is affordable, and per-frame phases, reciprocals, and palette rebuilds
belong there[^optimize]. `band()` runs 240 times a frame at `BAND_H = 2`,
against a total per-pixel budget of about 34 CPU cycles measured against a
480x480 panel at 30 fps, so the rules for its own body are stricter: zero
libm calls per pixel (one per-pixel `sinf` alone is roughly 4x the whole
budget), zero float divides per pixel (a divide is about 30 cycles against
the LX7's 1-2 cycle float add/multiply, since there is no hardware `fdiv`;
precompute a reciprocal in `frame()` or at the top of the row instead), and
never allocate, in `band()` or in `frame()` -- every table a kernel needs
is sized and allocated once in `init()`[^optimize].

## 10. Theme reactivity

Every animation draws its colors from one active gradient, and a change to
that gradient (a settings edit, a schedule-driven theme switch) is signaled
by a monotonically increasing generation counter, `bganim::themeGen()`, not
by a callback. An animation is expected to poll `themeGen()` in its
`frame()` and rebuild whatever palette or ramp it derived from the gradient
when the value changes; `band()` itself never reads theme state directly
and never rebuilds anything, keeping the same per-call budget from section
9 whether or not the theme just changed[^themegen].

[^bganimh]: `src/display/ui/default/bganim/BgAnim.h`, the `BgAnimation`
    struct and its preceding contract comment (init/frame/band/release,
    the band() signature, and the `bandRef` field used only for on-device
    equivalence testing), read 2026-09-06.
[^bandh]: `src/display/ui/default/SleepAnimation.cpp`, the `BAND_H`
    constant and its surrounding comment (current value 2, the BAND_H=4
    on-device revert). History: commit `7fede9c9` "perf(sleep-anim):
    BAND_H 16 -> 8" (2026-08-16), commit `dc4fd6d3` "bganim: make the
    render path answer for itself under a camera" (2026-08-28, BAND_H
    8 -> 2), commit `5a02a4f0` "display: record the BAND_H=4 experiment
    outcome".
[^benchbandh]: `tools/animbench/bench.cpp`, the `GM_BENCH_BAND_H` comment
    block: default 8, the measured 8-vs-16 host deltas per animation
    (-8% to +9%), and the note that this constant drifted out of sync with
    the device's `BAND_H` once already, at commit `7fede9c9`.
[^baseline]: `tools/animbench/BASELINE.md`, lines 9-11: numbers were taken
    at band height 16, before `bench.cpp`'s default moved to 8.
[^splitrender]: `src/display/ui/default/SleepAnimation.cpp`, the
    `renderFrame` band loop: `renderSkip`/`splitRender` (half resolution,
    around the `hrows`/`srcBase` loop) and `splitRenderFull` (full
    resolution, the `((y0 + r) ^ parityNow) & 1` loop), read 2026-09-06.
[^aurora]: `src/display/ui/default/bganim/AnimAurora.cpp`, file-header
    comment for the 2026-09-05 redesign (design worker "aurora2") and the
    `bandRef`-preceding comment on `ySrc = y & ~1`. Commit `b9ac0e42`
    "bganim: aurora renders one row per pair and keeps its row LUT across
    bands" (2026-09-05).
[^lava]: `src/display/ui/default/bganim/AnimLava.cpp`, the comment
    preceding the row-pair `band()`/`bandRef()` implementations ("A row's
    content and dither phase are always derived from ySrc = y & ~1").
    Commit `49050d67` "bganim: lava renders one row per pair"
    (2026-09-05).
[^mandala]: `src/display/ui/default/bganim/AnimMandala.cpp`, file-header
    comment on the one-row sample cache (`g_sampleCache`/`fetchRow`) and
    its verification against `interlace_check`'s call shapes. Commit
    `24d5cd76` "bganim: mandala interpolates a half-resolution sample
    grid" (2026-09-05).
[^toolscommit]: Commit `48e61200` "tools: host renderer and shape check
    for one candidate, rig notes" (2026-09-05): introduces
    `render_one.cpp` and its `--shapes` mode, and states "three of the
    four redesigns failed it before integration."
[^interlacecheck]: `tools/animbench/interlace_check.cpp`, header comment
    and `shapes[]` table (8-row reference and its purity control, 16-, 4-,
    and 1-row contiguous bands, `parity0`/`parity1`), read 2026-09-06.
[^renderone]: `tools/animbench/render_one.cpp`, header comment and
    `kShapes[]` table (adds a 2-row shape to interlace_check's set, and
    runs the whole set at both 480x480 and 240x240 via `checkShapes`),
    read 2026-09-06.
[^alignment]: Allocation: `src/display/ui/default/SleepAnimation.cpp`,
    `heap_caps_aligned_alloc(64, bandBytes, ...)` for `bandBuf`. PIE
    load/store masking and the alignment consequence for kernel tables:
    `tools/animbench/ASM_BRIEF.md`, the `ee.vld.128.ip`/`ee.vst.128.ip`
    bullet ("Band rows are 960 bytes and the band buffer is 64-byte
    aligned, so dst rows are aligned").
[^optimize]: `tools/animbench/OPTIMIZE.md`, the device cost model table
    (float divide ~30 cycles, sinf/cosf/exp2f ~150, no hardware fdiv) and
    the numbered rules following it: rule 1 (zero libm calls per pixel),
    rule 2 (zero float divides per pixel, precompute reciprocals), rule 7
    (frame() runs once per frame; libm/float is fine there), rule 8 (never
    allocate per frame).
[^themegen]: `src/display/ui/default/bganim/BgAnimCommon.h`,
    `themeGen()`/`setThemeStops`/`setThemeStopsPos` and their preceding
    comments. Contract statement: `tools/animbench/ASM_BRIEF.md`, "Theme
    reactivity preserved: poll bganim::themeGen() in frame() and rebuild
    palettes on change, exactly like the current code."
