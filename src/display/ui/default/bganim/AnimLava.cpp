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

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

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

// Round 4: band() ships no hand asm and no restructuring: every asm
// kernel and every control-flow change tried in rounds 1-3 measured
// slower than this shape on the device at equal table placement, so
// band() is a direct call to the portable reference above. See the
// file-top round-4 comment.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}

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
