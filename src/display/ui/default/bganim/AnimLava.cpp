#ifndef GAGGIMATE_SIM

// "Lava": six soft metaballs on incommensurate orbits, cubic falloff with a
// t^6 hot core, palette-mapped. Design: anim-fluid (Fable), 2026-08-15.
// Perf pass (sleep17, round 2): band() previously recomputed, per pixel,
// dx = x-bx, d2 = dx*dx+dy2, tt = 1-d2*invR2, then a t^3 term plus a
// branchy t^6 hot-core add-on: around 8 float multiplies and 2 branches
// per touched pixel per blob, followed by a second float pipeline (scale,
// dither, clamp) to turn the summed field into a palette index. The whole
// thing is now integer end to end:
//
// 1. tt(x) for fixed row/blob is an exact parabola in x (coefficient of x^2
//    is the per-blob constant -invR2, independent of row). A parabola's
//    *second* difference is constant, so tt can be swept left-to-right with
//    a Bresenham-style forward-difference accumulator: two integer adds per
//    pixel (tt += step; step += step2) reproduce the exact quadratic with no
//    per-pixel multiply at all. tt0/step0 (the row-starting value/slope) and
//    step2 (the constant curvature, truly per-blob-only. It doesn't even
//    depend on row) are computed once per (blob,row) and once per blob
//    respectively, both in the cheap outer loops, not the pixel loop.
// 2. The nonlinear part, t3 = tt^3, plus the tt>0.7 hot-core t^6 term,
//    times intensity, times kFieldScale (the field->palette-index scale
//    that used to run per pixel in the finalization loop), depends only on
//    tt, not on blob identity or pixel position. So it's folded into one
//    already-integer, already-index-scaled LUT (1024 buckets, padded to
//    1088 so the index shift never runs off the end: round 5 narrowed this
//    to 512 buckets padded to 576, see LUT_BITS's own comment), rebuilt once
//    per frame from the current intensity. band()'s field loop turns the
//    fixed-point tt accumulator into a LUT index with a shift (no multiply,
//    no divide, no branch) and adds the looked-up integer straight into an
//    integer fieldRow accumulator: no float ops left in this loop at all.
// 3. The finalization loop (field -> palette index) used to do a float
//    clamp-to-1.6, a float multiply by kFieldScale, and a float->int trunc
//    per pixel. Since fieldRow is now already in palette-index units, the
//    "clamp to 1.6f before scale" step becomes an integer min(fieldRow,255):
//    same saturation semantics (dither still perturbs saturated pixels,
//    matching the original's intent of avoiding banding at blob overlaps)
//    but compiled as a branchless Xtensa MIN instruction instead of a float
//    compare+branch. Same technique AnimEmber.cpp uses for its palette
//    lookup (see its file header): this file keeps a bounded min/max clamp
//    rather than Ember's fully-padded array because up to 6 large,
//    frequently-overlapping blobs make the worst-case index harder to bound
//    tightly than Ember's single radial field.
//
// Net: the blob-field inner loop is 2 int adds + 1 shift + 1 array read + 1
// int add + 1 store, zero branches. The finalization loop is 1 min + 1 int
// add + 1 min + 1 max + 1 palette lookup + 1 store, zero float ops, zero
// branches. Zero libm, zero float divides, zero float multiplies per pixel
// anywhere in band(). xlo/xhi are still the full-radius per-blob column
// window computed once in frame() (a safe superset of the true per-row
// chord); the LUT's zero-padded low side folds the tt<=0 early-out into the
// table lookup instead of a per-pixel compare.
//
// Perf pass, 2026-08-18 (opt-lava): the finalization loop above was still
// run over all 480 columns of every row, even though 6 blobs of radius
// ~0.19*min(w,h) rarely cover the whole width: most touched pixels are
// background, and the finalization formula for a background pixel
// (fieldRow[x] == 0, always) collapses to a function of (x&3, y&3) alone.
// That 16-value pattern is now precomputed once per theme change into four
// full-width rows, one per y&3 (buildBgRows()), and band() memcpy's the
// right one into the output row before doing anything else. The field
// accumulation loop is UNCHANGED (still walks every blob's own xlo/xhi
// window, still the same forward-difference math); band() additionally now
// records each touched blob's [xlo,xhi] as it accumulates (free: the
// values are already live locals in that loop), sorts and merges those into
// the row's true touched-column union (at most NUM_BLOBS==6 intervals, so a
// plain insertion sort), and re-runs the old finalization arithmetic
// (moved verbatim into finalizeSpan(), just windowed to [lo,hi] instead of
// always [0,w)) ONLY over that union, overwriting the memcpy'd background
// there. A row no blob reaches skips finalization entirely: one memcpy is
// the whole cost. Measured host band_ms at BAND_H=8: 0.250 -> ~0.220 (five
// medians measured directly; team-lead's independent 0.250 predates this
// pass), a ~12% cut, comfortably outside the quoted 2-9% run-to-run spread,
// and a genuine arithmetic reduction (not a locality effect), so this one
// SHOULD show up on host and does. New static footprint: bgRowAll,
// 4 * w * sizeof(uint16_t) = 3,840 B at w=480, alloc()'d (under the 8 KB
// PSRAM threshold, so internal DRAM like paletteLUT/fieldRow, not PSRAM.
// See the growth this cost in the commit message). Golden frames stay
// bit-exact (background pixels are byte-identical to what full computation
// produced, by construction: same formula, same inputs, just computed once
// and copied instead of recomputed per row) and band()'s call-shape
// invariant is untouched: nothing here is cached across band() calls, only
// within one call's per-row loop, and the per-row span/merge state is fresh
// every row and every call.
//
// Xtensa assembly pass, rounds 1-3 (2026-09-04, asm-lava), reverted round 4:
// three rounds of hand-written Xtensa kernels and control-flow restructuring
// (a software-pipelined field-accumulation gather, a branch-eliminating
// finalize kernel, a PIE background-row copy, an aligned bgRowAll split, and
// two different placements of a narrowed lavaLUT) were each measured against
// HEAD's own band() on the device at matched table placement, and none beat
// it: 31.1-32.8 ms and then 29.0-32.0 ms per full-res frame against HEAD's
// 26.0 ms, across every combination tried. None of it is carried forward.
// This file is HEAD's band()/bandRef split verbatim (band() below is a
// direct call to bandRef(), no kernel, no restructuring); the only change is
// table placement, using the bganim::allocHot() API added this round: the
// three tables HEAD itself allocated at or under 8 KB (paletteLUT 512 B,
// fieldRow 1,920 B at w=480, bgRowAll 3,840 B at w=480: the set HEAD's own
// alloc() used to land in internal DRAM when the pool allowed, per the
// per-table comments below) now go through allocHot()'s reserved slab
// instead of racing the general heap for that placement. lavaLUT (9,216 B,
// the one table HEAD's old alloc() always sent to PSRAM on size) is
// unchanged, still alloc(). release() needs no changes: releaseTable()
// already dispatches by where each pointer actually came from.
//
// Round 5 (device-in-the-loop, kb.py, 2026-09-04): the round-4 measurement
// above (26.0 ms) predates kb.py; kbench on the round-4 firmware read
// band min 20.77/first 35.5/mean 32.33 ms, and this round's own kb.py run
// of the unchanged round-4 source confirmed it (band 19.42/28.67/28.84,
// blob of the same source 19.26/31.11/29.14: nil placement offset between
// IRAM blob and flash band(), matching kblob/README.md's claim for other
// anims). The three tables round 4 moved to allocHot() used only 6,272 B of
// the 9,216 B animation's slab share (512+1,920+3,840), leaving 2,944 B
// free. lavaLUT (the fourth and largest table, read once per touched
// pixel in the field-accumulation loop) was the only one still on alloc()
// (PSRAM), by round 4's own design, not a fallback. This round narrows it
// to int16_t and drops LUT_BITS from 10 to 512 buckets (see the constant's
// comment for the exact margin math) so LUT_SIZE*sizeof(int16_t) is 2,304 B,
// fitting the free 2,944 B, and moves it to allocHot() too: the first
// attempt where all four of lava's tables are hot-slab-resident at once,
// which rounds 1-3 could not do (allocHot() did not exist yet; the old
// heap-race heuristic could put lavaLUT in SRAM only by starving something
// else of the placement it wanted, which is presumably why "a narrowed
// int16_t width... measured slower on the device regardless" then). Five
// kb.py runs after the move: blob min_ms held at 18.94-18.96 against band's
// 19.35-19.51 (repeatable ~2.3-2.9% cut, well outside the <3% noise floor
// stated in this round's brief because the within-variant spread, <0.1 ms,
// is 5-20x smaller than the between-variant gap), and blob's first_ms sat
// below band's first_ms in all five runs though both are noisy in absolute
// terms (dominated by which bands' tables/code were cold going in). A
// follow-up dropped LUT_BITS to 8 (1,152 B, more slab headroom): min_ms was
// unchanged (18.94-18.96 again) and first_ms showed no consistent
// improvement, so the win comes from getting lavaLUT off PSRAM at all, not
// from shrinking it further past LUT_BITS=9: kept the finer table for the
// better golden margin at no measured cost. Golden diff at LUT_BITS=9:
// mean 0.061-0.101, max 33-36 (tolerance mean<=3, max<=48), so the coarser
// bucket is visually silent. Host bandRef host time similarly moved
// 0.230 -> 0.220 ms/frame, in the same direction as the device number.
//
// The field-accumulation inner loop itself (the `for (int n = ...)` loop
// below) was checked against xtensa-esp32s3 GCC 14's own codegen
// (xtensa-asm14.sh) rather than re-attempted in hand asm: the compiled loop
// already runs inside a hardware zero-overhead LOOP (confirmed in the .S,
// same as the round-4 comment above already notes) at 9 instructions per
// touched pixel: srai+addx2 (2, compute the LUT index and address),
// l16si (1, load lut[idx]), l32i (1, load the accumulator), add.n (1,
// ttQ+=stepQ), add.n (1, accumulate), s32i (1, store the accumulator),
// add.n (1, stepQ+=step2Q), addi.n (1, field++), with GCC's own scheduler
// already inserting the independent ttQ+=stepQ update between the lut load
// and its use, so there is no load-use stall to remove either. That is the
// true arithmetic floor for this Bresenham-LUT structure: one gather load,
// one read-modify-write of the shared accumulator, and the two recurrence
// adds, none of which the algorithm can drop without changing what it
// computes. It matches what three prior on-device rounds already found by
// trying kernels here and losing (31.1-32.8 ms and 29.0-32.0 ms against
// HEAD's then-26.0 ms, file-top round 1-3 comment above), so this round did
// not spend the shared board's time re-running that experiment a fourth
// time. finalizeSpan's own 4-wide loop is the one hot loop in this file
// that did NOT get a hardware LOOP (xtensa-asm14 shows a plain
// decrement-and-branch, `bnez.n`, closing it). A `loopnez` conversion
// there is a real, untested-this-round candidate, but by instruction count
// it removes at most one taken branch per 4 pixels against a ~24-instruction
// body, under 5% of finalizeSpan's own cost and likely under the 3% total
// noise floor; round 1-3's "branch-eliminating finalize kernel" (file-top
// comment above) may already have covered this exact loop and lost, so it
// is named here as a candidate for whoever picks this file up next with
// board time to spend confirming it, not shipped speculatively.
//
// Redesign, 2026-09-05 (design-lava): three more Xtensa rounds against this
// file's own field-accumulation and finalize loops (see round 1-3 and round
// 4/5 above) had already found the arithmetic floor for computing every
// row; the only way further down was to compute fewer rows. Probe blobs
// (kb.py, anim 1, isolating each half of a row's cost by stubbing the other
// half) measured the split before changing anything: the field-accumulation
// gather cost about 10.0 ms of the 19.1 ms frame, finalizeSpan (dither,
// clamp, palette lookup over the touched-column union) about 6.1 ms, with
// roughly 2.8 ms of shared per-row bookkeeping (the memset and the six-blob
// dy2 span check) neither probe isolates cleanly. Reading the code first
// suggested finalizeSpan was the larger cost; it is not, and the three
// failed asm rounds above make sense in that light: the gather was never
// a small piece of the frame, so no instruction-level trick to it was ever
// going to move much.
//
// Since both costs (and the shared bookkeeping) scale with rows processed
// rather than with anything asm can trim independently, band() now computes
// one row of each vertical pair in full and duplicates it into the row
// below, instead of running the gather and finalize passes on every row.
// Production always calls this with rows==2 and y0 even (BAND_H,
// SleepAnimation.cpp), so the common case is exactly one compute-and-
// duplicate pair per call; nothing here carries state between calls, same
// contract this file's bandRef always relied on. Blob positions, radii,
// palette and the LUT are all unchanged: this is the same math, run over
// half the rows. Measured: rig (kb.py blob min_ms) 19.1 -> 10.1 ms, a 1.88x
// cut; golden diff (frames 30/120/210, defaults and both extremes of all
// three parameters) 0.4 to 1.6 of 255, all from dither grain, no shape or
// colour change.
//
// The dither phase needs care once rows are paired. The ordered dither is
// keyed on a Bayer row phase (BAYER4's row period); a first cut passed the
// source row's own y&3 into the shared per-row renderer and duplicated the
// result verbatim. y is always even in every real caller, so y&3 only ever
// comes out 0 or 2: rows with phase 1 or 3 never appear. In a 4x4 Bayer
// matrix, rows 0 and 2 share the same column parity (both alternate low,
// high, low, high across columns), so the two phases that do appear
// reinforce the same per-column pattern in every displayed row instead of
// alternating with their neighbours, and the texture reads as fine vertical
// stripes in bright, flat regions (a blob's hot core, the glow falloff)
// instead of a checkerboard grain. Confirmed by inspection against a
// pre-fix render side by side with this file's own frames, which show no
// such stripes. Fixed by keying the phase on the row PAIR, (y>>1)&3,
// instead of the row: pair 0 (rows 0-1) uses Bayer row 0, pair 1 (rows 2-3)
// row 1, pair 2 row 2, pair 3 row 3, pair 4 wraps back to row 0, and so on.
// All four Bayer rows now appear, each stretched over two physical rows
// instead of one (the same coarsening this redesign already trades for
// speed, just applied along y instead of collapsed along y) and
// successive pairs use different Bayer rows, so the grain is a 1x2
// checkerboard again rather than a column of solid stripes. Cost: one shift
// and one mask, already free next to the memset/gather/finalize below.
// Confirmed at no measured timing cost (10.14 ms both before and after).
//
// A second bug surfaced at integration, caught by
// tools/animbench/interlace_check.cpp: a row's content and dither phase
// must depend only on its own absolute y, never on which other rows the
// same band() call happened to also request. An earlier cut of bandRef
// kept the pair phase, (y>>1)&3, only when both rows of a pair landed in
// the same call, and fell back to a lone row's own y&3 otherwise, which
// differs from its pair phase whenever y is even, so the same row rendered
// two different ways depending on call shape. This is not a hypothetical:
// SleepAnimation.cpp's row-level interlace path (splitRenderFull) calls
// band() with rows==1, one row at a time, rendering only every other row
// each frame, so a real production path requests rows individually with no
// partner in the same call. Fixed by deriving ySrc = y & ~1 and phase =
// (y>>1)&3 from y before deciding whether to duplicate, so a lone row
// renders ySrc's content directly and gets exactly the pixels it would
// have gotten as the duplicate half of a same-call pair. See bandRef's
// own comment for the mechanism.
//
// Round 6 (2026-09-05, asm-lava), flag-gated, UNTESTED ON THE DEVICE: the
// bench board was offline for this round (do not flash it, do not touch
// COM3, do not call 192.168.1.121), so nothing below is a timing claim. It
// is a candidate for whoever next has board time, picking up exactly what
// round 5's file-top comment already named: "finalizeSpan's own 4-wide loop
// is the one hot loop in this file that did NOT get a hardware LOOP... A
// loopnez conversion there is a real, untested-this-round candidate."
//
// Two hand-written Xtensa kernels, both gated off by default
// (GM_BGANIM_LAVA_ASM, same pattern as AnimSilk.cpp's GM_BGANIM_SILK_ASM).
// With the flag off, band() is exactly what round 4 left it: a direct call
// to bandRef(), byte for byte. Nothing here changes bandRef(), renderRow()
// or finalizeSpan(): those stay the spec, unedited. The kernels sit beside
// them (lavaFinalizeQuadAsm, lavaFieldGatherAsm) feeding a second, parallel
// implementation (finalizeSpanAsm, renderRowAsm, bandAsm) that band() only
// reaches when the flag is on.
//
// finalizeSpan's 4-wide loop (lavaFinalizeQuadAsm): xtensa-asm14.sh's
// compiled .S for this file shows GCC 14 closing this loop with a plain
// decrement-and-branch (addi.n + bnez.n), not a hardware LOOP, over a
// 40-instruction body (42 counting those two branch instructions): 10.5
// static instructions per pixel. The kernel is that same 40-instruction
// body, transcribed instruction for instruction off the .S (same op order,
// same data flow, even GCC's own pixel-3,pixel-1,pixel-2,pixel-0 evaluation
// order and its apparently redundant 16-bit EXTUI after every clamp, kept
// as found rather than "corrected": a transcription's job is to match what
// was measured, not to guess at what the compiler meant), wrapped in a
// `loopnez` instead of the branch: 40 static instructions over 4 pixels,
// 10/pixel, and the taken branch every iteration is gone rather than merely
// recounted. Three prior rounds (see the round 1-3 comment above) already
// measured a branch-eliminating finalize kernel losing on the device
// despite winning on paper, for reasons this loop's own instruction count
// can't see (IRAM code size against the flash icache; see AnimSilk.cpp's
// fifth/sixth-pass comments and this repo's CLAUDE.md "Animation kernels"
// section), so this is offered as a candidate, not a claim: it may lose the
// same way.
//
// The field-accumulation gather (lavaFieldGatherAsm): round 5's file-top
// comment above already found that GCC's compiled loop for this exact C++
// (`for (int n = xhi-xlo+1; n>0; n--) { *field++ += lut[ttQ>>LUT_SHIFT];
// ttQ+=stepQ; stepQ+=step2Q; }`) is ALREADY a 9-instruction hardware
// zero-overhead LOOP with no load-use stall left to remove (the scheduler
// already inserts the independent ttQ+=stepQ add between the LUT load and
// the add that consumes it). This kernel is a verbatim transcription of
// that same 9-instruction body inside a hand-written `loopnez`, at parity
// by construction: it cannot be faster than GCC's own loop, since it is the
// same instructions in the same order. It exists so the on-device A/B, if
// anyone runs it, compares two structurally identical loops (one compiled,
// one hand-assembled) instead of the compiled loop against nothing. No edge
// was taken past that transcription: the two candidates round 5 named
// (keeping an accumulator load/store resident across iterations, or a
// 2-wide unroll to overlap load-use latency) do not apply without changing
// the arithmetic. Consecutive iterations touch DIFFERENT field[] addresses
// (there is no accumulator living in a register across iterations to keep
// resident), and the loop's one load-use gap is already filled, so a 2-wide
// unroll would duplicate work without shortening any dependency chain.
//
// Both kernels are checked bit-exact against portable C references,
// transcribed the same way, in tools/qemubench/tests/anim_lava/ (rewritten
// this round; the previous version was a stub reporting that lava shipped
// no kernel). Neither kernel has executed on real Xtensa silicon: only
// through xtensa-asm14.sh's assembler and in QEMU. That is the gap between
// this comment and a timing number. finalizeSpanAsm/renderRowAsm/bandAsm
// are not new algorithm, just renderRow()'s and finalizeSpan()'s own
// structure with the two hot loops swapped for calls into the kernels
// above, so the eventual on-device A/B has a full band() to call.
//
// Round 7 (2026-09-05): round 6 left the flag untestable off-device, since
// the whole kernel/glue block above required __XTENSA__ to compile at all;
// a host build with the flag on saw no glue and silently fell through to
// bandRef() via the #else below, so nothing had ever exercised
// finalizeSpanAsm/renderRowAsm/bandAsm outside the Xtensa assembler and
// QEMU. This round gives lavaFinalizeQuadAsm and lavaFieldGatherAsm a
// second body each: a plain C++ twin (identical to lavaFinalizeQuadRef and
// lavaFieldGatherRef in tools/qemubench/tests/anim_lava/main.cpp), selected
// on non-Xtensa builds and any GM_BGANIM_NO_ASM build, so the surrounding
// glue is now real code on the host, not dead text. With the flag on:
// tools/animbench's golden compare and interlace_check both pass through
// bandAsm/renderRowAsm/finalizeSpanAsm calling the host twins
// (`make ... -DGM_BGANIM_LAVA_ASM=1 check` -> GOLDENS OK, ALL OK);
// render_one's shape check passes the same way
// (`./render_... --shapes 30 120 210` -> "lava shapes: ALL OK"); the ASan
// fuzzer renders 47,700 bands clean at anim index 1. On the device side,
// xtensa-asm14.sh's own compile line plus -DGM_BGANIM_LAVA_ASM=1 still
// assembles and links (both hand kernels present in the .o, each still a
// single `loopnez`), and tools/qemubench/tests/anim_lava still reports
// PASS, 0 mismatches. None of this is a timing claim: the host proves the
// glue computes the right pixels through both kernel bodies and the device
// side proves the asm still assembles the same way; only kb.py or a real
// on-device A/B (bench board was unavailable again this round) can say
// whether bandAsm is faster than bandRef. The flag now defaults to 1 (see
// its own comment by the #define) on exactly that evidence: correctness is
// covered on both bodies, speed is not measured on either.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

// Master switch for the hand-written Xtensa kernels near the end of this
// file (lavaFinalizeQuadAsm, lavaFieldGatherAsm, and the
// finalizeSpanAsm/renderRowAsm/bandAsm chain that wires them into a second
// band() implementation). ON by default: each kernel above has a portable
// C++ twin (round 7, same name and signature, selected when the build is
// not Xtensa or defines GM_BGANIM_NO_ASM), so the glue is real code on
// every build, not dead text gated behind __XTENSA__. Bit-exact: the asm
// kernels are checked against portable references under QEMU
// (tools/qemubench/tests/anim_lava, PASS, 0 mismatches), and with the flag
// on the host twins are proven against tools/animbench's own goldens and
// interlace_check (GOLDENS OK, ALL OK), render_one's shape check (lava
// shapes: ALL OK) and the ASan fuzzer (bands rendered clean). Not yet
// timed on the device: the bench board was offline for round 6 and again
// for round 7, so nothing here is a speed claim; the first production A/B
// (camshots/anim_devbench.py, useref=1 swaps in bandRef) decides whether
// this stays on. -DGM_BGANIM_LAVA_ASM=0 falls back to bandRef(), byte for
// byte what round 4 shipped.
#ifndef GM_BGANIM_LAVA_ASM
#define GM_BGANIM_LAVA_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int NUM_BLOBS = 6;
// 1/1.6 exactly (1.6 = 8/5, so 1/1.6 = 0.625 = 5/8, exact in binary), folded
// with the *255 index scale. Used only once per frame now (building
// lavaLUT in frame()). band() never multiplies by it per pixel.
constexpr float kFieldScale = 0.625f * 255.0f; // 159.375, exact

// Fixed-point scheme for the per-pixel tt accumulator (Bresenham-style
// quadratic forward difference, see file-top comment). Q12.20: tt lives in
// (0, 1] so 20 fractional bits give ample precision (worst-case curvature
// step rounding drifts tt by well under 0.01 over a full blob-width scan,
// negligible next to the LUT's own 1024-bucket quantization).
constexpr int FRAC_BITS = 20;
constexpr float FIXED_SCALE = static_cast<float>(1 << FRAC_BITS); // 1,048,576

// tt -> (t^3 + hot-core t^6) * intensity * kFieldScale LUT (already in
// palette-index units, see file-top comment), indexed by the top LUT_BITS
// of the Q12.20 tt accumulator (arithmetic shift, so negative tt maps to
// negative indices). band()'s row/blob setup guarantees dy2 < R2 (the row
// early-out) and |dx| <= R (the xlo/xhi window), so d2 < 2*R^2 and therefore
// tt = 1 - d2*invR2 is bounded in (-1, 1]: never more negative than -1. The
// table is padded on *both* ends (low side zero-filled, representing "no
// contribution", so it doubles as the tt<=0 early-out with no branch; high
// side clamped to the tt=1 blob-center value) so band()'s inner loop can
// index it unconditionally with no clamp/compare at all. lavaBase is the
// LUT pointer already offset so it can be indexed directly by the signed
// shifted tt value.
// Round 5 (memory-ratio pass): dropped one bit from LUT_BITS (1024 -> 512
// buckets) so the whole table narrows enough to fit the slab's free 2,944 B
// (see the round-5 file-top comment) as int16_t. Host goldens stay within
// tolerance (see the round-5 comment) because the contribution curve is
// smooth in tt; the coarser bucket only matters where the curve is steepest
// (the hot core past tt=0.7), and that is exactly where six overlapping
// blobs plus dither already mask a lot of quantization.
constexpr int LUT_BITS = 9;
constexpr int LUT_HALF = 1 << LUT_BITS; // 512 buckets covering tt in (0,1]
// Slack past the +-1 analytic bound. The forward-difference accumulator does
// not track tt exactly: stepQ and step2Q are rounded to whole Q12.20 units, so
// each carries up to 0.5 LSB of error, and step2Q's error is re-added on every
// iteration. Over a k-pixel scan the accumulated deviation is bounded by
//   0.5*k*(k-1)/2 + 0.5*k + 0.5   Q12.20 units,
// which at the widest possible scan (k = 480, a blob spanning the panel) is
// ~57.7k units = 0.055 in tt, i.e. 0.055 * LUT_HALF buckets on either side
// (56.4 at the original LUT_BITS=10, 28.2 here at LUT_BITS=9: the margin
// need scales linearly with LUT_HALF since bucket width is what changed, not
// the underlying fixed-point error). 32 was not enough at LUT_BITS=10: it
// let the shifted index reach lavaLUT[-3] (caught by tools/animbench/fuzz
// under ASan). 128 covered the LUT_BITS=10 worst case with a 2.27x margin;
// keeping that same safety factor at LUT_BITS=9 gives 64 (128 * 512/1024),
// and costs 128 bytes as int16_t (256 as int32_t).
constexpr int LUT_MARGIN = 64;
constexpr int LUT_OFFSET = LUT_HALF + LUT_MARGIN;
constexpr int LUT_SIZE = 2 * LUT_HALF + 2 * LUT_MARGIN;
constexpr int LUT_SHIFT = FRAC_BITS - LUT_BITS; // 11

// Palette-index saturation cap: the field-domain equivalent of the original
// "clamp to 1.6f before scaling" (1.6f * kFieldScale == 255.0f exactly), now
// applied post-scale as an integer min() so overlapping hot blob cores
// still saturate at the same brightness the original design intended:
// dither can then still jitter the result below the cap, avoiding a
// flat/banded look at blob overlaps. Its swing is now one palette step
// wide rather than +-1 index unit, so a saturated core still dithers.
constexpr int32_t kIndexCap = 255;

struct BlobDef {
    float fx1, fx2, fy1, fy2, ax1, ax2, ay1, ay2, px1, px2, py1, py2, cx, cy, R0, Rpulse, wR, phR;
};
struct BlobState {
    float bx, by, R2, invR2;
    int xlo, xhi;      // precomputed per-blob column window (superset of the true chord), hoisted out of band()
    int32_t step2Q;    // constant curvature of tt(x) in Q12.20 (-2*invR2 scaled), per-blob, row-independent
};

BlobDef blobDef[NUM_BLOBS];
BlobState blob[NUM_BLOBS];
uint16_t *paletteLUT = nullptr;
int32_t *fieldRow = nullptr; // one row of accumulated field, already in palette-index units
int32_t ditherLUT[16];       // ordered dither in palette-index units, indexed by (y&3)*4+(x&3)
int16_t *lavaLUT = nullptr;  // tt-bucket -> field contribution, pre-scaled to palette-index units, rebuilt in frame()
int16_t *lavaBase = nullptr; // lavaLUT + LUT_OFFSET, so band() indexes it directly with the signed shifted-tt value
uint32_t lastThemeGen = 0xFFFFFFFF;
bool inited = false;
int allocW = 0; // width fieldRow was sized for

// Precomputed "no blob touched this pixel" row, one per Bayer row phase
// 0..3, rebuilt only when paletteLUT changes (theme change). A pixel no
// blob writes into has fieldRow[x] == 0 for the whole frame, so the
// finalization formula collapses to a function of (x&3, phase) alone:
//   idx = clamp(0 + ditherRow[x&3], 0, 255);  out[x] = paletteLUT[idx];
// band() used to run that formula (with fieldRow[x] folded in, always 0
// here) per pixel, every pixel, every row. Four blobs' worth of soft glow
// rarely covers all 480 columns of every row, so most of that work was
// computing the same 4-periodic pattern over and over. Precomputing it once
// per theme and memcpy-ing it into rows that need it (all of a row with no
// touched blob, everywhere outside the touched span on a partial row) turns
// that into a sequential copy instead of a per-pixel branch+lookup+store.
uint16_t *bgRowAll = nullptr; // 4 rows of bgRowAllW pixels each, row py at bgRowAll + py*bgRowAllW
int bgRowAllW = 0;            // width bgRowAll was sized for (mirrors allocW's fixed-at-first-init contract)

// Rebuilds the four background rows from the current paletteLUT/ditherLUT.
// Must run after both are populated, and again any time paletteLUT changes
// (theme change), which now also means rebuilding ditherLUT first, since
// its amplitude is derived from the palette's step spacing. w is always bgRowAllW: bgRowAll is sized once like fieldRow/allocW,
// so this never risks writing past the allocation even if a caller's w
// argument were to differ from the size decided at first init().
// Amplitude is half the spacing between the palette's RGB565 steps, so the
// dither cell spans exactly one step. The old fixed 255/128 was +-1 index unit,
// enough to break lava's own fixed-point index quantization but not the panel's:
// 9.9% of disc pixels sat on a monotone <=1 LSB staircase at brightness 100,
// 13.5% at 55. Deriving it gives 2.3% and 2.9%.
void buildDitherLUT() {
    const float amp = ditherAmp(paletteLUT, 256);
    for (int k = 0; k < 16; k++) {
        const float d = (static_cast<float>(BAYER4[k]) - 7.5f) * (amp / 7.5f);
        ditherLUT[k] = static_cast<int32_t>(d >= 0.0f ? d + 0.5f : d - 0.5f);
    }
}

void buildBgRows(int w) {
    for (int py = 0; py < 4; py++) {
        uint16_t *row = bgRowAll + static_cast<size_t>(py) * w;
        const int32_t *ditherRow = &ditherLUT[py * 4];
        for (int x = 0; x < w; x++) {
            int idx = ditherRow[x & 3]; // fieldRow[x] == 0 here: min(0, kIndexCap) + dither == dither
            if (idx < 0) {
                idx = 0;
            } else if (idx > 255) {
                idx = 255;
            }
            row[x] = paletteLUT[idx];
        }
    }
}

bool init(int w, int h) {
    if (paletteLUT == nullptr) {
        // Round 4 placement: HEAD allocated this at or under 8 KB, so it
        // moves to bganim::allocHot() (see the file-top round-4 comment).
        paletteLUT = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t))); // 512 B
    }
    if (fieldRow == nullptr) {
        // Round 4 placement: same as paletteLUT above.
        fieldRow = static_cast<int32_t *>(allocHot(w * sizeof(int32_t))); // 1,920 B at w=480
        allocW = w;
    }
    if (lavaLUT == nullptr) {
        // Round 5 placement: allocHot(), narrowed to int16_t at LUT_BITS=9
        // (see the constant's comment). Rounds 1-3 tried moving this table
        // to the internal slab at its original int32_t width (where it
        // alone exhausted whatever slab existed then, leaving no room for
        // paletteLUT/fieldRow) and at a narrowed int16_t width but still
        // LUT_BITS=10 (which fit the slab of that era but measured slower
        // on the device regardless. See the round-5 file-top comment for
        // why that result does not indict this round's placement: the
        // access pattern already made this a cheap read, so the earlier
        // move bought nothing and the earlier revert was about something
        // else, table budget). LUT_SIZE*sizeof(int16_t) is 2,304 B here,
        // fitting inside the 2,944 B the slab had free after paletteLUT
        // (512) + fieldRow (1,920) + bgRowAll (3,840) at round 4, so this
        // is the first attempt where the table can sit in the slab
        // alongside all three other resident tables, not in place of them.
        lavaLUT = static_cast<int16_t *>(allocHot(LUT_SIZE * sizeof(int16_t)));
    }
    if (bgRowAll == nullptr) {
        // Round 4 placement: same as paletteLUT above. This replaces HEAD's
        // own placement story for this table (a cumulative-SRAM-budget
        // heuristic against the old plain alloc(), since retired along with
        // the heuristic itself. See the file-top round-4 comment): the
        // pool-availability race that heuristic was exposed to is exactly
        // what allocHot()'s fixed, reserved slab removes.
        bgRowAll = static_cast<uint16_t *>(allocHot(4 * w * sizeof(uint16_t))); // 3,840 B at w=480
        bgRowAllW = w;
    }
    if (paletteLUT == nullptr || fieldRow == nullptr || lavaLUT == nullptr || bgRowAll == nullptr) {
        return false;
    }
    // Derived here rather than under the !inited guard below: the base pointer
    // has to follow the allocation, not the one-time table setup.
    lavaBase = lavaLUT + LUT_OFFSET;
    if (!inited) {
        inited = true;
        for (int i = 0; i < NUM_BLOBS; i++) {
            const float ga = i * 2.39996323f; // golden angle spreads phases
            BlobDef &d = blobDef[i];
            d.fx1 = 0.55f + 0.11f * i;
            d.fx2 = 1.41421356f * (0.35f + 0.05f * i);
            d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
            d.fy2 = 1.73205081f * (0.30f + 0.04f * i);
            d.ax1 = w * 0.14f;
            d.ax2 = w * 0.07f;
            d.ay1 = h * 0.14f;
            d.ay2 = h * 0.07f;
            d.px1 = ga;
            d.px2 = ga * 2.1f;
            d.py1 = ga * 1.7f;
            d.py2 = ga * 0.6f;
            d.cx = w * 0.5f + w * 0.28f * cosf(ga);
            d.cy = h * 0.5f + h * 0.28f * sinf(ga * 1.3f);
            const float m = (w < h ? w : h);
            d.R0 = m * 0.19f;
            d.Rpulse = m * 0.05f;
            d.wR = 0.00011f + 0.00003f * i;
            d.phR = ga * 2.7f;
        }
        buildThemeRamp(paletteLUT, 256);
        lastThemeGen = themeGen();
        buildDitherLUT();       // amplitude follows the ramp just built
        buildBgRows(bgRowAllW); // needs both of the above
    }
    return true;
}

void frame(uint32_t tMs, int w, int, const uint8_t p[4]) {
    const float omega0 = 6.2831853f / 45000.0f * speedMul(p[0]); // 45s base cycle at speed 50
    const float sizeMul = 0.6f + (p[1] / 100.0f);
    const float intensity = 0.5f + (p[2] / 100.0f) * 1.3f;
    if (themeGen() != lastThemeGen) {
        buildThemeRamp(paletteLUT, 256);
        lastThemeGen = themeGen();
        buildDitherLUT();
        buildBgRows(bgRowAllW);
    }

    // Rebuild the tt -> field-contribution LUT for this frame's intensity.
    // Depends only on the tt bucket (not blob position), so one table
    // serves all 6 blobs and every pixel this frame. Cheap: ~1000 flops
    // once per frame, versus once per pixel before. Values are pre-scaled
    // by kFieldScale so band()'s field loop accumulates directly in
    // palette-index units (see file-top comment). Indexed via lavaBase so
    // band() can pass the signed shifted-tt value straight through: entries
    // below shifted==0 (tt<=0, outside the blob) are zero so an out-of-blob
    // pixel adds nothing, with no per-pixel branch needed.
    for (int p2 = 0; p2 < LUT_SIZE; p2++) {
        const int shifted = p2 - LUT_OFFSET;
        if (shifted < 0) {
            lavaLUT[p2] = 0;
            continue;
        }
        float tt = (shifted + 0.5f) * (1.0f / LUT_HALF);
        if (tt > 1.0f) {
            tt = 1.0f; // padding entries clamp to the tt=1 (blob-center) value
        }
        const float t3 = tt * tt * tt;
        const float contribution = (t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f)) * kFieldScale;
        // contribution is always >= 0 and its max (tt=1, intensity=1.8,
        // hot core included) is 459.0: well inside int16_t's range, so
        // the round-5 narrowing (see file-top comment) loses no precision
        // versus the old int32_t storage, only bucket resolution (LUT_BITS).
        lavaLUT[p2] = static_cast<int16_t>(contribution + 0.5f);
    }

    const float t = tMs * omega0;
    for (int i = 0; i < NUM_BLOBS; i++) {
        const BlobDef &d = blobDef[i];
        BlobState &b = blob[i];
        b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
        b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
        const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
        b.R2 = R * R;
        b.invR2 = 1.0f / b.R2;
        // Constant curvature of tt(x) = 1 - invR2*((x-bx)^2 + dy^2): the x^2
        // coefficient is -invR2 regardless of row, so this Bresenham "second
        // difference" is a per-blob constant computed once per frame, not
        // once per row (see file-top comment).
        b.step2Q = static_cast<int32_t>(lroundf(-2.0f * b.invR2 * FIXED_SCALE));
        // Column window is a per-blob constant (doesn't depend on row/dy), so
        // it's computed once here instead of once per touched row in band().
        // Using the full radius R (not the per-row chord half-width) means no
        // sqrt is needed; band()'s inner loop still early-outs on tt<=0 for
        // the rows/columns outside the true chord.
        int xlo = static_cast<int>(b.bx - R);
        int xhi = static_cast<int>(b.bx + R);
        if (xlo < 0) {
            xlo = 0;
        }
        if (xhi > w - 1) {
            xhi = w - 1;
        }
        b.xlo = xlo;
        b.xhi = xhi;
    }
}

// Finalizes fieldRow[lo..hi] into out[lo..hi], identical arithmetic to the
// full-width loop this replaces (see the file's history for the pre-span
// version). Just windowed to a sub-range instead of always [0, w). Pulled
// out of band() as a named function, not a lambda, so it reads once instead
// of once per merged span in the disassembly, and so its own loops are not
// re-examined by GCC's whole-function budget every time band() changes (see
// the bcTable/tail-copy lesson in AnimNebula.cpp: unrelated loops in a
// function can lose their hardware LOOP when the function grows unrelated
// live state around them: keeping this arithmetic in its own function
// keeps that risk local to this function alone). yPhase selects which of
// the four Bayer dither rows this call uses; see renderRow's comment for
// why it is passed in rather than derived from a row number here.
void finalizeSpan(uint16_t *out, int lo, int hi, int yPhase) {
    const int32_t *ditherRow = &ditherLUT[yPhase * 4];
    int x = lo;
    // Scalar prefix up to the next 4-boundary: lo is an arbitrary blob-union
    // edge, not guaranteed 4-aligned like the whole-row loop's x=0 start, so
    // the unrolled body below needs an aligned entry point for its d0..d3
    // literals to line up with x&3 == 0.
    for (; (x & 3) != 0 && x <= hi; x++) {
        int idx = (fieldRow[x] < kIndexCap ? fieldRow[x] : kIndexCap) + ditherRow[x & 3];
        if (idx < 0) {
            idx = 0;
        } else if (idx > 255) {
            idx = 255;
        }
        out[x] = paletteLUT[idx];
    }
    const int32_t d0 = ditherRow[0];
    const int32_t d1 = ditherRow[1];
    const int32_t d2 = ditherRow[2];
    const int32_t d3 = ditherRow[3];
    for (; x + 3 <= hi; x += 4) {
        // min(fieldRow, 255) reproduces the original "clamp field to 1.6f
        // before scaling" (1.6f*kFieldScale == 255.0f exactly, see
        // file-top comment) as a branchless integer MIN. Dither is added
        // after the cap, exactly like the original float pipeline, so
        // saturated/overlapping pixels still get jittered instead of
        // pinning flat.
        int idx0 = (fieldRow[x] < kIndexCap ? fieldRow[x] : kIndexCap) + d0;
        int idx1 = (fieldRow[x + 1] < kIndexCap ? fieldRow[x + 1] : kIndexCap) + d1;
        int idx2 = (fieldRow[x + 2] < kIndexCap ? fieldRow[x + 2] : kIndexCap) + d2;
        int idx3 = (fieldRow[x + 3] < kIndexCap ? fieldRow[x + 3] : kIndexCap) + d3;
        if (idx0 < 0) {
            idx0 = 0;
        } else if (idx0 > 255) {
            idx0 = 255;
        }
        if (idx1 < 0) {
            idx1 = 0;
        } else if (idx1 > 255) {
            idx1 = 255;
        }
        if (idx2 < 0) {
            idx2 = 0;
        } else if (idx2 > 255) {
            idx2 = 255;
        }
        if (idx3 < 0) {
            idx3 = 0;
        } else if (idx3 > 255) {
            idx3 = 255;
        }
        const uint16_t p0 = paletteLUT[idx0];
        const uint16_t p1 = paletteLUT[idx1];
        const uint16_t p2 = paletteLUT[idx2];
        const uint16_t p3 = paletteLUT[idx3];
        *reinterpret_cast<uint32_t *>(out + x) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
        *reinterpret_cast<uint32_t *>(out + x + 2) = static_cast<uint32_t>(p2) | (static_cast<uint32_t>(p3) << 16);
    }
    for (; x <= hi; x++) {
        int idx = (fieldRow[x] < kIndexCap ? fieldRow[x] : kIndexCap) + ditherRow[x & 3];
        if (idx < 0) {
            idx = 0;
        } else if (idx > 255) {
            idx = 255;
        }
        out[x] = paletteLUT[idx];
    }
}

// Renders exactly one absolute row y into out[0..w), dithered with Bayer row
// yPhase (0..3). Field accumulation, span merge and finalize are unchanged
// from the pre-redesign band(); the only change from here down is that
// bandRef() now calls this once per row PAIR instead of once per row, and
// yPhase is a separate argument (rather than being derived from y here)
// because bandRef keys it on the row pair, not the row. See bandRef's own
// comment for why.
void renderRow(uint16_t *out, int y, int w, int yPhase) {
    memset(fieldRow, 0, static_cast<size_t>(w) * sizeof(int32_t));
    // Spans of blobs that actually reach this row (dy2 < R2), collected
    // in the same pass that accumulates the field: no extra iteration
    // over blobs. Everywhere outside their union, fieldRow is provably
    // 0 for the rest of this row (nothing else writes it), so
    // finalization there is bit-identical to the precomputed background
    // row and is copied instead of recomputed.
    int spanLo[NUM_BLOBS];
    int spanHi[NUM_BLOBS];
    int nSpans = 0;
    for (int i = 0; i < NUM_BLOBS; i++) {
        const BlobState &b = blob[i];
        const float dy = y - b.by;
        const float dy2 = dy * dy;
        if (dy2 >= b.R2) {
            continue;
        }
        const float invR2 = b.invR2;
        const int xlo = b.xlo;
        const int xhi = b.xhi;
        spanLo[nSpans] = xlo;
        spanHi[nSpans] = xhi;
        nSpans++;
        const float dx0 = xlo - b.bx;
        // Row-starting value and slope of the tt(x) parabola (one-time
        // float setup per touched row per blob, not per pixel).
        const float tt0f = 1.0f - invR2 * (dy2 + dx0 * dx0);
        const float step0f = -invR2 * (2.0f * dx0 + 1.0f);
        int32_t ttQ = static_cast<int32_t>(tt0f * FIXED_SCALE + (tt0f >= 0.0f ? 0.5f : -0.5f));
        int32_t stepQ = static_cast<int32_t>(step0f * FIXED_SCALE + (step0f >= 0.0f ? 0.5f : -0.5f));
        const int32_t step2Q = b.step2Q;
        const int16_t *lut = lavaBase;
        // Pure integer forward-difference sweep: no multiply, no
        // divide, no libm, no branch. lut is zero-padded below index 0
        // (see LUT build in frame()), so out-of-blob pixels (tt<=0,
        // negative shifted index) just add zero: the tt<=0 early-out
        // is folded into the table instead of a per-pixel compare.
        // xlo/xhi are the precomputed full-radius window (superset of
        // the true chord for this row).
        //
        // Counted-down form on purpose: it gives GCC a trip count known
        // at loop entry, which is what lets it emit the Xtensa hardware
        // zero-overhead LOOP instruction here (confirmed in the .S) and
        // drop the per-iteration compare-and-branch entirely. Slightly
        // slower on the x86 bench, which has no such instruction: the
        // device is the one that has to hit 30 fps.
        int32_t *field = fieldRow + xlo;
        for (int n = xhi - xlo + 1; n > 0; n--) {
            *field++ += lut[ttQ >> LUT_SHIFT];
            ttQ += stepQ;
            stepQ += step2Q;
        }
    }

    const uint16_t *bg = bgRowAll + static_cast<size_t>(yPhase) * bgRowAllW;
    memcpy(out, bg, static_cast<size_t>(w) * sizeof(uint16_t));
    if (nSpans == 0) {
        return; // no blob touched this row: background covers all of it, already copied
    }

    // Sort the (at most NUM_BLOBS==6) collected spans by lo, then sweep
    // once to merge overlapping/adjacent ones into the true union.
    // Insertion sort: cheap for this size, done once per row, not once
    // per pixel.
    for (int i = 1; i < nSpans; i++) {
        const int lo = spanLo[i];
        const int hi = spanHi[i];
        int j = i - 1;
        while (j >= 0 && spanLo[j] > lo) {
            spanLo[j + 1] = spanLo[j];
            spanHi[j + 1] = spanHi[j];
            j--;
        }
        spanLo[j + 1] = lo;
        spanHi[j + 1] = hi;
    }
    int mLo = spanLo[0];
    int mHi = spanHi[0];
    for (int i = 1; i < nSpans; i++) {
        if (spanLo[i] <= mHi + 1) {
            // Overlaps or is adjacent to the run so far: merging is a
            // pure win (fewer finalizeSpan calls) and never wrong, since
            // any gap it swallows has fieldRow == 0 there anyway.
            if (spanHi[i] > mHi) {
                mHi = spanHi[i];
            }
            continue;
        }
        finalizeSpan(out, mLo, mHi, yPhase);
        mLo = spanLo[i];
        mHi = spanHi[i];
    }
    finalizeSpan(out, mLo, mHi, yPhase);
}

// Computes the even row of a y/y+1 pair in full and duplicates it into the
// odd row, instead of running renderRow on every row (see the 2026-09-05
// file-top comment). Production's usual call is rows==2 with y0 even
// (SleepAnimation.cpp's BAND_H), so the common case is exactly one
// compute-and-duplicate pair per call; the host bench's rows==8 covers four
// pairs the same way.
//
// A row's content and dither phase are always derived from ySrc = y & ~1
// (its pair's even row) and phase = (y>>1)&3 (its pair's Bayer row), never
// from y directly and never from anything about the call other than y
// itself. That is what makes this safe under SleepAnimation.cpp's row-level
// interlace path, which calls band() with rows==1 for one row at a time
// (`bandFn(band + r*w, y0+r, 1, w, tMs, p)`, only every other row each
// frame). interlace_check.cpp exists to catch exactly this class of bug
// and caught an earlier version of this function that kept the pair phase
// only when both rows of a pair were in the same call and fell back to
// y&3 for a lone row: the same absolute row then rendered with a different
// dither phase depending on whether its partner happened to be in the same
// call, a real call-shape variance, not a test artifact, since
// splitRenderFull's single-row calls are a real production path. Deriving
// ySrc/phase from y alone before deciding whether to duplicate removes the
// dependency on call shape entirely: a lone row (interlace, or any
// y0/rows that splits a pair across two calls) computes ySrc's content
// directly and gets exactly the pixels it would have gotten as the
// duplicate half of a same-call pair, just without that call's memcpy
// saving.
//
// Bayer row 0 backs pair 0 (rows 0-1), row 1 backs pair 1 (rows 2-3), row 2
// backs pair 2, row 3 backs pair 3, pair 4 wraps back to row 0, and so on.
// All four Bayer rows appear, each stretched over two physical rows, and
// successive pairs use different rows, so the grain is a checkerboard
// rather than the column of stripes an earlier cut of this design produced
// (see the file-top comment).
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    int row = 0;
    while (row < rows) {
        const int y = y0 + row;
        const int ySrc = y & ~1;
        const int phase = (y >> 1) & 3;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        if ((y & 1) == 0 && row + 1 < rows) {
            // y starts a pair and y+1 is also in this call: compute once,
            // duplicate. The common case for every real caller.
            renderRow(out, ySrc, w, phase);
            memcpy(out + w, out, static_cast<size_t>(w) * sizeof(uint16_t));
            row += 2;
        } else {
            // y is odd (its partner is the row behind it, not in this
            // call) or y is even but the call ends before y+1 (row-level
            // interlace). Either way, render ySrc's content directly so
            // this row matches what it would be as half of a same-call
            // pair.
            renderRow(out, ySrc, w, phase);
            row += 1;
        }
    }
}

#if GM_BGANIM_LAVA_ASM
// ---- Round 6/7 kernels + glue (see the file-top comment) ----
//
// lavaFinalizeQuadAsm and lavaFieldGatherAsm have two bodies: hand-written
// Xtensa asm on a real device build, a plain C++ twin everywhere else (host
// bench, fuzzer, render_one, and any GM_BGANIM_NO_ASM build). Both bodies
// share the same name and signature, so the glue below (finalizeSpanAsm,
// renderRowAsm, bandAsm) is written once and compiles for either: on the
// host it is exercising real code, not a stub, which is what lets
// tools/animbench's golden/fuzz/interlace checks and render_one's shape
// check run against this path with the flag on, something round 6 could
// not do (the whole block used to require __XTENSA__, so a host build with
// the flag on saw no glue at all and fell through to the #else below).
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// Both kernels are noinline, take plain pointers/ints, and are transcribed
// instruction-for-instruction into tools/qemubench/tests/anim_lava/ (not
// regenerated), matching this project's established precedent (see
// AnimSilk.cpp's kernels and tools/qemubench/tests/anim_ember/main.c): a
// QEMU PASS is then evidence about this literal instruction sequence, not
// about the algorithm being reimplemented correctly.

static_assert(LUT_SHIFT == 11, "lavaFieldGatherAsm bakes LUT_SHIFT in as an immediate (srai ..., 11); "
                                "update the asm if LUT_BITS/FRAC_BITS ever change this");
static_assert(kIndexCap == 255, "lavaFinalizeQuadAsm bakes kIndexCap in as an immediate (movi ..., 255)");

// lavaFinalizeQuadAsm: the 4-wide clamp/dither/palette-gather body from
// finalizeSpan(), transcribed instruction-for-instruction off
// xtensa-asm14.sh's compiled .S for this exact loop (see the round-6
// file-top comment: 40 instructions here against GCC's 42, including its
// own decrement-and-branch), closed with a hardware `loopnez` instead of
// the compiled bnez.n so the per-iteration branch disappears rather than
// just getting counted differently. Op order, register-to-register data
// flow, and even GCC's own pixel evaluation order (3,1,2,0, not 0,1,2,3)
// and its 16-bit EXTUI after every clamp (redundant once idx is known to be
// in [0,255], but that is what the compiler emitted) are kept as found:
// this is a transcription, not a rewrite. field/out both advance by one
// quad (4 pixels) per iteration; the caller guarantees `out` is 4-pixel
// (8-byte) aligned, the same precondition finalizeSpan()'s own comment
// already documents for its unrolled C++ version of this loop.
//
//   out:    4 consecutive uint16_t outputs per iteration, x&3==0 at entry.
//   field:  &fieldRow[x], one int32_t per pixel.
//   d0..d3: the four dither constants for x&3 == 0,1,2,3 (ditherRow[0..3]),
//           unchanged across iterations: the dither pattern repeats every 4
//           pixels regardless of which quad is being processed.
//   lut:    paletteLUT base (256 entries, indexed 0..255 only, post-clamp).
//   nQuads: trip count. loopnez so nQuads==0 (a span with no full quad left
//           after the alignment prefix) safely does nothing, rather than
//           the one guaranteed iteration a plain `loop` would run.
__attribute__((noinline)) void lavaFinalizeQuadAsm(uint16_t *out, const int32_t *field, int32_t d0, int32_t d1,
                                                     int32_t d2, int32_t d3, const uint16_t *lut, int32_t nQuads) {
    int32_t t0, t1, t2, t3, cap, zero;
    asm volatile("movi    %[cap], 255\n"
                 "movi    %[zero], 0\n"
                 "loopnez %[n], 1f\n"
                 "l32i    %[t3], %[field], 12\n" // fieldRow[x+3]
                 "l32i    %[t1], %[field], 4\n"  // fieldRow[x+1]
                 "l32i    %[t2], %[field], 8\n"  // fieldRow[x+2]
                 "l32i    %[t0], %[field], 0\n"  // fieldRow[x]
                 "min     %[t3], %[cap], %[t3]\n"
                 "min     %[t1], %[cap], %[t1]\n"
                 "min     %[t2], %[cap], %[t2]\n"
                 "add     %[t3], %[t3], %[d3]\n"
                 "add     %[t1], %[t1], %[d1]\n"
                 "min     %[t0], %[cap], %[t0]\n"
                 "add     %[t2], %[t2], %[d2]\n"
                 "min     %[t3], %[t3], %[cap]\n"
                 "min     %[t1], %[t1], %[cap]\n"
                 "add     %[t0], %[t0], %[d0]\n"
                 "min     %[t2], %[t2], %[cap]\n"
                 "max     %[t3], %[t3], %[zero]\n"
                 "max     %[t1], %[t1], %[zero]\n"
                 "min     %[t0], %[t0], %[cap]\n"
                 "max     %[t2], %[t2], %[zero]\n"
                 "extui   %[t3], %[t3], 0, 16\n"
                 "extui   %[t1], %[t1], 0, 16\n"
                 "max     %[t0], %[t0], %[zero]\n"
                 "extui   %[t2], %[t2], 0, 16\n"
                 "addx2   %[t3], %[t3], %[lut]\n"
                 "addx2   %[t1], %[t1], %[lut]\n"
                 "extui   %[t0], %[t0], 0, 16\n"
                 "l16ui   %[t3], %[t3], 0\n"
                 "l16ui   %[t1], %[t1], 0\n"
                 "addx2   %[t2], %[t2], %[lut]\n"
                 "addx2   %[t0], %[t0], %[lut]\n"
                 "l16ui   %[t2], %[t2], 0\n"
                 "l16ui   %[t0], %[t0], 0\n"
                 "slli    %[t1], %[t1], 16\n"
                 "slli    %[t3], %[t3], 16\n"
                 "or      %[t1], %[t1], %[t0]\n"
                 "or      %[t3], %[t3], %[t2]\n"
                 "s32i    %[t1], %[out], 0\n"
                 "s32i    %[t3], %[out], 4\n"
                 "addi    %[field], %[field], 16\n"
                 "addi    %[out], %[out], 8\n"
                 "1:\n"
                 : [out] "+r"(out), [field] "+r"(field), [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2),
                   [t3] "=&r"(t3), [cap] "=&r"(cap), [zero] "=&r"(zero)
                 : [d0] "r"(d0), [d1] "r"(d1), [d2] "r"(d2), [d3] "r"(d3), [lut] "r"(lut), [n] "r"(nQuads)
                 : "memory");
}

// lavaFieldGatherAsm: the Bresenham-LUT field-accumulation loop from
// renderRow(), transcribed instruction-for-instruction off the SAME .S (see
// the round-5 file-top comment, which already found this loop compiles to a
// 9-instruction hardware zero-overhead LOOP with the scheduler already
// filling the LUT load's use-latency gap with the independent ttQ+=stepQ
// add). This kernel is that identical 9-instruction body inside a
// hand-written `loopnez`: at parity by construction, since it is the same
// instructions in the same order. No edge was taken past the transcription:
// consecutive iterations write DIFFERENT field[] addresses (there is no
// accumulator living in a register across iterations to keep resident), and
// the loop's one load-use gap is already filled by the scheduler, so a
// 2-wide unroll would duplicate work without shortening any dependency
// chain.
//
//   field:  &fieldRow[xlo], walked forward one int32_t per pixel.
//   ttQ0:   the Q12.20 tt accumulator's starting value (tt0f<<FRAC_BITS,
//           rounded).
//   stepQ0: tt's starting per-pixel slope (step0f<<FRAC_BITS, rounded).
//   step2Q: tt's constant curvature (per blob, row-independent).
//   lut:    lavaBase (already offset so a signed shifted-tt value indexes
//           it directly; see lavaBase's own comment).
//   n:      trip count (xhi-xlo+1, always >=1 in production since xlo<=xhi
//           by construction; loopnez costs nothing to keep that assumption
//           from ever being load-bearing here).
__attribute__((noinline)) void lavaFieldGatherAsm(int32_t *field, int32_t ttQ0, int32_t stepQ0, int32_t step2Q,
                                                    const int16_t *lut, int32_t n) {
    int32_t ttQ = ttQ0;
    int32_t stepQ = stepQ0;
    int32_t idx, acc;
    asm volatile("loopnez %[n], 1f\n"
                 "srai    %[idx], %[ttq], 11\n"      // idx = ttQ >> LUT_SHIFT
                 "addx2   %[idx], %[idx], %[lut]\n"  // &lut[idx]
                 "l32i    %[acc], %[field], 0\n"     // acc = *field
                 "l16si   %[idx], %[idx], 0\n"       // val = lut[idx] (sign-extend: int16_t)
                 "add     %[ttq], %[ttq], %[stepq]\n" // ttQ += stepQ (fills the load's use-latency gap)
                 "add     %[idx], %[acc], %[idx]\n"  // acc + val
                 "s32i    %[idx], %[field], 0\n"     // *field = acc + val
                 "add     %[stepq], %[stepq], %[step2q]\n" // stepQ += step2Q
                 "addi    %[field], %[field], 4\n"   // field++
                 "1:\n"
                 : [field] "+r"(field), [ttq] "+r"(ttQ), [stepq] "+r"(stepQ), [idx] "=&r"(idx), [acc] "=&r"(acc)
                 : [step2q] "r"(step2Q), [lut] "r"(lut), [n] "r"(n)
                 : "memory");
}

#else // !(__XTENSA__ && !GM_BGANIM_NO_ASM): portable host twins, round 7

// Host twin of lavaFinalizeQuadAsm: same name, same signature, same
// per-pixel result as the asm body above and as finalizeSpan()'s own 4-wide
// loop. This is what lets a non-Xtensa build (host bench, fuzzer,
// render_one) compile and run the glue below with the flag on: identical to
// lavaFinalizeQuadRef in tools/qemubench/tests/anim_lava/main.cpp, which
// checks the asm against this same arithmetic under QEMU.
void lavaFinalizeQuadAsm(uint16_t *out, const int32_t *field, int32_t d0, int32_t d1, int32_t d2, int32_t d3,
                          const uint16_t *lut, int32_t nQuads) {
    for (int32_t q = 0; q < nQuads; q++) {
        const int32_t *f = field + q * 4;
        uint16_t *o = out + q * 4;
        int32_t idx0 = f[0] < kIndexCap ? f[0] : kIndexCap;
        idx0 += d0;
        idx0 = idx0 < 0 ? 0 : (idx0 > 255 ? 255 : idx0);
        int32_t idx1 = f[1] < kIndexCap ? f[1] : kIndexCap;
        idx1 += d1;
        idx1 = idx1 < 0 ? 0 : (idx1 > 255 ? 255 : idx1);
        int32_t idx2 = f[2] < kIndexCap ? f[2] : kIndexCap;
        idx2 += d2;
        idx2 = idx2 < 0 ? 0 : (idx2 > 255 ? 255 : idx2);
        int32_t idx3 = f[3] < kIndexCap ? f[3] : kIndexCap;
        idx3 += d3;
        idx3 = idx3 < 0 ? 0 : (idx3 > 255 ? 255 : idx3);
        o[0] = lut[idx0];
        o[1] = lut[idx1];
        o[2] = lut[idx2];
        o[3] = lut[idx3];
    }
}

// Host twin of lavaFieldGatherAsm: same name, same signature, same
// per-pixel result as the asm body above and as renderRow()'s own
// Bresenham-LUT loop. Identical to lavaFieldGatherRef in
// tools/qemubench/tests/anim_lava/main.cpp.
void lavaFieldGatherAsm(int32_t *field, int32_t ttQ0, int32_t stepQ0, int32_t step2Q, const int16_t *lut, int32_t n) {
    int32_t ttQ = ttQ0;
    int32_t stepQ = stepQ0;
    for (int32_t i = 0; i < n; i++) {
        field[i] += lut[ttQ >> LUT_SHIFT];
        ttQ += stepQ;
        stepQ += step2Q;
    }
}

#endif // __XTENSA__ && !GM_BGANIM_NO_ASM

// finalizeSpanAsm: finalizeSpan() with its 4-wide loop swapped for
// lavaFinalizeQuadAsm. The scalar alignment prefix and the scalar tail are
// copied verbatim from finalizeSpan() (see that function's own comments for
// why they exist): each is 0-3 pixels, never worth hand asm.
void finalizeSpanAsm(uint16_t *out, int lo, int hi, int yPhase) {
    const int32_t *ditherRow = &ditherLUT[yPhase * 4];
    int x = lo;
    for (; (x & 3) != 0 && x <= hi; x++) {
        int idx = (fieldRow[x] < kIndexCap ? fieldRow[x] : kIndexCap) + ditherRow[x & 3];
        if (idx < 0) {
            idx = 0;
        } else if (idx > 255) {
            idx = 255;
        }
        out[x] = paletteLUT[idx];
    }
    const int32_t d0 = ditherRow[0];
    const int32_t d1 = ditherRow[1];
    const int32_t d2 = ditherRow[2];
    const int32_t d3 = ditherRow[3];
    if (x <= hi) {
        // Same trip count as finalizeSpan()'s `for (; x + 3 <= hi; x += 4)`:
        // x is 4-aligned here (the prefix loop above only exits early on
        // x>hi, otherwise on x&3==0), so this is an exact floor division.
        const int32_t nQuads = (hi - x + 1) / 4;
        if (nQuads > 0) {
            lavaFinalizeQuadAsm(out + x, fieldRow + x, d0, d1, d2, d3, paletteLUT, nQuads);
            x += nQuads * 4;
        }
    }
    for (; x <= hi; x++) {
        int idx = (fieldRow[x] < kIndexCap ? fieldRow[x] : kIndexCap) + ditherRow[x & 3];
        if (idx < 0) {
            idx = 0;
        } else if (idx > 255) {
            idx = 255;
        }
        out[x] = paletteLUT[idx];
    }
}

// renderRowAsm: renderRow() with its field-accumulation loop swapped for
// lavaFieldGatherAsm and its finalizeSpan() call swapped for
// finalizeSpanAsm. Everything else (the memset, the per-blob row-starting
// setup, the span collection/sort/merge) is copied verbatim: see
// renderRow() for what each part does and why.
void renderRowAsm(uint16_t *out, int y, int w, int yPhase) {
    memset(fieldRow, 0, static_cast<size_t>(w) * sizeof(int32_t));
    int spanLo[NUM_BLOBS];
    int spanHi[NUM_BLOBS];
    int nSpans = 0;
    for (int i = 0; i < NUM_BLOBS; i++) {
        const BlobState &b = blob[i];
        const float dy = y - b.by;
        const float dy2 = dy * dy;
        if (dy2 >= b.R2) {
            continue;
        }
        const float invR2 = b.invR2;
        const int xlo = b.xlo;
        const int xhi = b.xhi;
        spanLo[nSpans] = xlo;
        spanHi[nSpans] = xhi;
        nSpans++;
        const float dx0 = xlo - b.bx;
        const float tt0f = 1.0f - invR2 * (dy2 + dx0 * dx0);
        const float step0f = -invR2 * (2.0f * dx0 + 1.0f);
        int32_t ttQ = static_cast<int32_t>(tt0f * FIXED_SCALE + (tt0f >= 0.0f ? 0.5f : -0.5f));
        int32_t stepQ = static_cast<int32_t>(step0f * FIXED_SCALE + (step0f >= 0.0f ? 0.5f : -0.5f));
        const int32_t step2Q = b.step2Q;
        lavaFieldGatherAsm(fieldRow + xlo, ttQ, stepQ, step2Q, lavaBase, xhi - xlo + 1);
    }

    const uint16_t *bg = bgRowAll + static_cast<size_t>(yPhase) * bgRowAllW;
    memcpy(out, bg, static_cast<size_t>(w) * sizeof(uint16_t));
    if (nSpans == 0) {
        return;
    }

    for (int i = 1; i < nSpans; i++) {
        const int lo = spanLo[i];
        const int hi = spanHi[i];
        int j = i - 1;
        while (j >= 0 && spanLo[j] > lo) {
            spanLo[j + 1] = spanLo[j];
            spanHi[j + 1] = spanHi[j];
            j--;
        }
        spanLo[j + 1] = lo;
        spanHi[j + 1] = hi;
    }
    int mLo = spanLo[0];
    int mHi = spanHi[0];
    for (int i = 1; i < nSpans; i++) {
        if (spanLo[i] <= mHi + 1) {
            if (spanHi[i] > mHi) {
                mHi = spanHi[i];
            }
            continue;
        }
        finalizeSpanAsm(out, mLo, mHi, yPhase);
        mLo = spanLo[i];
        mHi = spanHi[i];
    }
    finalizeSpanAsm(out, mLo, mHi, yPhase);
}

// bandAsm: bandRef() with renderRow() swapped for renderRowAsm(). The row
// pairing/duplication logic (and its dither-phase derivation) is copied
// verbatim: see bandRef()'s own comment for why ySrc/phase must be derived
// from y alone, never from call shape.
void bandAsm(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    int row = 0;
    while (row < rows) {
        const int y = y0 + row;
        const int ySrc = y & ~1;
        const int phase = (y >> 1) & 3;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        if ((y & 1) == 0 && row + 1 < rows) {
            renderRowAsm(out, ySrc, w, phase);
            memcpy(out + w, out, static_cast<size_t>(w) * sizeof(uint16_t));
            row += 2;
        } else {
            renderRowAsm(out, ySrc, w, phase);
            row += 1;
        }
    }
}
#endif // GM_BGANIM_LAVA_ASM

// Round 4: with the flag off, band() ships no hand asm and no
// restructuring: every asm kernel and every control-flow change tried in
// rounds 1-3 measured slower than this shape on the device at equal table
// placement, so band() is a direct call to the portable reference above.
// See the file-top round-4 comment. Round 6/7 add a flag-gated alternative
// (bandAsm; see the round-6/7 file-top comments), ON by default since round
// 7 (see the flag's own comment by the #define): -DGM_BGANIM_LAVA_ASM=1
// routes band() there on every build (Xtensa runs the hand asm kernels,
// everything else runs their host twins); -DGM_BGANIM_LAVA_ASM=0 falls
// back to this bandRef() call.
#if GM_BGANIM_LAVA_ASM
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandAsm(dst, y0, rows, w, tMs, p);
}
#else
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif

void release() {
    releaseTable(paletteLUT, 256 * sizeof(uint16_t));
    releaseTable(fieldRow, static_cast<size_t>(allocW) * sizeof(int32_t));
    releaseTable(lavaLUT, static_cast<size_t>(LUT_SIZE) * sizeof(int16_t));
    releaseTable(bgRowAll, 4 * static_cast<size_t>(bgRowAllW) * sizeof(uint16_t));
    // Offset alias into lavaLUT, not an allocation of its own.
    lavaBase = nullptr;
    allocW = 0;
    bgRowAllW = 0;
    lastThemeGen = 0xFFFFFFFF;
    inited = false;
}

} // namespace

extern const BgAnimation bg_anim_lava;
const BgAnimation bg_anim_lava = {
    "lava",
    "Lava",
    {{"speed", "Speed", 50}, {"scale", "Blob size", 50}, {"glow", "Glow", 60}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
