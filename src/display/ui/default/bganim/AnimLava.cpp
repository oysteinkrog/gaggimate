#ifndef GAGGIMATE_SIM

// "Lava" — six soft metaballs on incommensurate orbits, cubic falloff with a
// t^6 hot core, palette-mapped. Design: anim-fluid (Fable), 2026-08-15.
// Perf pass (sleep17, round 2): band() previously recomputed, per pixel,
// dx = x-bx, d2 = dx*dx+dy2, tt = 1-d2*invR2, then a t^3 term plus a
// branchy t^6 hot-core add-on — around 8 float multiplies and 2 branches
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
//    step2 (the constant curvature, truly per-blob-only — it doesn't even
//    depend on row) are computed once per (blob,row) and once per blob
//    respectively, both in the cheap outer loops, not the pixel loop.
// 2. The nonlinear part — t3 = tt^3, plus the tt>0.7 hot-core t^6 term,
//    times intensity, times kFieldScale (the field->palette-index scale
//    that used to run per pixel in the finalization loop) — depends only on
//    tt, not on blob identity or pixel position. So it's folded into one
//    already-integer, already-index-scaled LUT (1024 buckets, padded to
//    1088 so the index shift never runs off the end), rebuilt once per
//    frame from the current intensity. band()'s field loop turns the
//    fixed-point tt accumulator into a LUT index with a shift (no multiply,
//    no divide, no branch) and adds the looked-up integer straight into an
//    integer fieldRow accumulator — no float ops left in this loop at all.
// 3. The finalization loop (field -> palette index) used to do a float
//    clamp-to-1.6, a float multiply by kFieldScale, and a float->int trunc
//    per pixel. Since fieldRow is now already in palette-index units, the
//    "clamp to 1.6f before scale" step becomes an integer min(fieldRow,255)
//    — same saturation semantics (dither still perturbs saturated pixels,
//    matching the original's intent of avoiding banding at blob overlaps)
//    but compiled as a branchless Xtensa MIN instruction instead of a float
//    compare+branch. Same technique AnimEmber.cpp uses for its palette
//    lookup (see its file header) — this file keeps a bounded min/max clamp
//    rather than Ember's fully-padded array because up to 6 large,
//    frequently-overlapping blobs make the worst-case index harder to bound
//    tightly than Ember's single radial field.
//
// Net: the blob-field inner loop is 2 int adds + 1 shift + 1 array read + 1
// int add + 1 store, zero branches. The finalization loop is 1 min + 1 int
// add + 1 min + 1 max + 1 palette lookup + 1 store, zero float ops, zero
// branches. Zero libm, zero float divides, zero float multiplies per pixel
// anywhere in band(). xlo/xhi are still the full-radius per-blob column
// window computed once in frame() — a safe superset of the true per-row
// chord; the LUT's zero-padded low side folds the tt<=0 early-out into the
// table lookup instead of a per-pixel compare.
//
// Perf pass, 2026-08-18 (opt-lava): the finalization loop above was still
// run over all 480 columns of every row, even though 6 blobs of radius
// ~0.19*min(w,h) rarely cover the whole width — most touched pixels are
// background, and the finalization formula for a background pixel
// (fieldRow[x] == 0, always) collapses to a function of (x&3, y&3) alone.
// That 16-value pattern is now precomputed once per theme change into four
// full-width rows, one per y&3 (buildBgRows()), and band() memcpy's the
// right one into the output row before doing anything else. The field
// accumulation loop is UNCHANGED (still walks every blob's own xlo/xhi
// window, still the same forward-difference math); band() additionally now
// records each touched blob's [xlo,xhi] as it accumulates (free — the
// values are already live locals in that loop), sorts and merges those into
// the row's true touched-column union (at most NUM_BLOBS==6 intervals, so a
// plain insertion sort), and re-runs the old finalization arithmetic
// (moved verbatim into finalizeSpan(), just windowed to [lo,hi] instead of
// always [0,w)) ONLY over that union, overwriting the memcpy'd background
// there. A row no blob reaches skips finalization entirely -- one memcpy is
// the whole cost. Measured host band_ms at BAND_H=8: 0.250 -> ~0.220 (five
// medians measured directly; team-lead's independent 0.250 predates this
// pass), a ~12% cut, comfortably outside the quoted 2-9% run-to-run spread,
// and a genuine arithmetic reduction (not a locality effect), so this one
// SHOULD show up on host and does. New static footprint: bgRowAll,
// 4 * w * sizeof(uint16_t) = 3,840 B at w=480, alloc()'d (under the 8 KB
// PSRAM threshold, so internal DRAM like paletteLUT/fieldRow, not PSRAM —
// see the growth this cost in the commit message). Golden frames stay
// bit-exact (background pixels are byte-identical to what full computation
// produced, by construction: same formula, same inputs, just computed once
// and copied instead of recomputed per row) and band()'s call-shape
// invariant is untouched — nothing here is cached across band() calls, only
// within one call's per-row loop, and the per-row span/merge state is fresh
// every row and every call.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

constexpr int NUM_BLOBS = 6;
// 1/1.6 exactly (1.6 = 8/5, so 1/1.6 = 0.625 = 5/8, exact in binary), folded
// with the *255 index scale. Used only once per frame now (building
// lavaLUT in frame()) — band() never multiplies by it per pixel.
constexpr float kFieldScale = 0.625f * 255.0f; // 159.375, exact

// Fixed-point scheme for the per-pixel tt accumulator (Bresenham-style
// quadratic forward difference — see file-top comment). Q12.20: tt lives in
// (0, 1] so 20 fractional bits give ample precision (worst-case curvature
// step rounding drifts tt by well under 0.01 over a full blob-width scan,
// negligible next to the LUT's own 1024-bucket quantization).
constexpr int FRAC_BITS = 20;
constexpr float FIXED_SCALE = static_cast<float>(1 << FRAC_BITS); // 1,048,576

// tt -> (t^3 + hot-core t^6) * intensity * kFieldScale LUT (already in
// palette-index units — see file-top comment), indexed by the top LUT_BITS
// of the Q12.20 tt accumulator (arithmetic shift, so negative tt maps to
// negative indices). band()'s row/blob setup guarantees dy2 < R2 (the row
// early-out) and |dx| <= R (the xlo/xhi window), so d2 < 2*R^2 and therefore
// tt = 1 - d2*invR2 is bounded in (-1, 1] — never more negative than -1. The
// table is padded on *both* ends (low side zero-filled, representing "no
// contribution", so it doubles as the tt<=0 early-out with no branch; high
// side clamped to the tt=1 blob-center value) so band()'s inner loop can
// index it unconditionally with no clamp/compare at all. lavaBase is the
// LUT pointer already offset so it can be indexed directly by the signed
// shifted tt value.
constexpr int LUT_BITS = 10;
constexpr int LUT_HALF = 1 << LUT_BITS; // 1024 buckets covering tt in (0,1]
// Slack past the +-1 analytic bound. The forward-difference accumulator does
// not track tt exactly: stepQ and step2Q are rounded to whole Q12.20 units, so
// each carries up to 0.5 LSB of error, and step2Q's error is re-added on every
// iteration. Over a k-pixel scan the accumulated deviation is bounded by
//   0.5*k*(k-1)/2 + 0.5*k + 0.5   Q12.20 units,
// which at the widest possible scan (k = 480, a blob spanning the panel) is
// ~57.7k units = 0.055 in tt = 57 buckets, on either side. 32 was not enough:
// it let the shifted index reach lavaLUT[-3] (caught by tools/animbench/fuzz
// under ASan). 128 covers the worst case with room and costs 768 bytes.
constexpr int LUT_MARGIN = 128;
constexpr int LUT_OFFSET = LUT_HALF + LUT_MARGIN;
constexpr int LUT_SIZE = 2 * LUT_HALF + 2 * LUT_MARGIN;
constexpr int LUT_SHIFT = FRAC_BITS - LUT_BITS; // 10

// Palette-index saturation cap: the field-domain equivalent of the original
// "clamp to 1.6f before scaling" (1.6f * kFieldScale == 255.0f exactly), now
// applied post-scale as an integer min() so overlapping hot blob cores
// still saturate at the same brightness the original design intended —
// dither can then still jitter the result by +-1, avoiding a flat/banded
// look at blob overlaps.
constexpr int32_t kIndexCap = 255;

struct BlobDef {
    float fx1, fx2, fy1, fy2, ax1, ax2, ay1, ay2, px1, px2, py1, py2, cx, cy, R0, Rpulse, wR, phR;
};
struct BlobState {
    float bx, by, R2, invR2;
    int xlo, xhi;      // precomputed per-blob column window (superset of the true chord), hoisted out of band()
    int32_t step2Q;    // constant curvature of tt(x) in Q12.20 (-2*invR2 scaled) — per-blob, row-independent
};

BlobDef blobDef[NUM_BLOBS];
BlobState blob[NUM_BLOBS];
uint16_t *paletteLUT = nullptr;
int32_t *fieldRow = nullptr; // one row of accumulated field, already in palette-index units
int32_t ditherLUT[16];       // precomputed (BAYER4[k]/16 - 0.5) / 128 * 255, indexed by (y&3)*4+(x&3)
int32_t *lavaLUT = nullptr;  // tt-bucket -> field contribution, pre-scaled to palette-index units, rebuilt in frame()
int32_t *lavaBase = nullptr; // lavaLUT + LUT_OFFSET, so band() indexes it directly with the signed shifted-tt value
uint32_t lastThemeGen = 0xFFFFFFFF;
bool inited = false;
int allocW = 0; // width fieldRow was sized for

// Precomputed "no blob touched this pixel" row, one per y&3 (Bayer4's row
// period), rebuilt only when paletteLUT changes (theme change). A pixel no
// blob writes into has fieldRow[x] == 0 for the whole frame, so band()'s
// finalization formula collapses to a function of (x&3, y&3) alone:
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
// (theme change) -- ditherLUT itself never changes after the one-time init
// below. w is always bgRowAllW: bgRowAll is sized once like fieldRow/allocW,
// so this never risks writing past the allocation even if a caller's w
// argument were to differ from the size decided at first init().
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
        paletteLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    }
    if (fieldRow == nullptr) {
        fieldRow = static_cast<int32_t *>(alloc(w * sizeof(int32_t)));
        allocW = w;
    }
    if (lavaLUT == nullptr) {
        // 9,216 B. As a static array this was the largest single object in
        // internal DRAM in the whole firmware, and it was resident for every
        // animation, not just this one -- which is what left AsyncTCP unable to
        // allocate the few dozen bytes it needs per ACK to keep a large
        // response moving. alloc() sends anything over 8 KB to PSRAM, and this
        // table suits that: band() sweeps it monotonically through a forward
        // difference on ttQ, so the reads are sequential rather than random,
        // and 9 KB stays largely cache-resident anyway.
        lavaLUT = static_cast<int32_t *>(alloc(LUT_SIZE * sizeof(int32_t)));
    }
    if (bgRowAll == nullptr) {
        // 4 * w * 2 B (3,840 B at w=480). Under the 8 KB per-allocation
        // threshold, so it is not sent to PSRAM on size -- but alloc()'s budget
        // is CUMULATIVE, and this is the last table lava asks for, which makes
        // it the one that spills if the budget is short. It lands in internal
        // DRAM today, and the reason is an invariant held elsewhere: only one
        // animation's tables are live at a time, because SleepAnimation calls
        // prev.release() on switch and all 13 animations have a release entry,
        // and release() refunds g_allocSram from the pool the pointer actually
        // came from. Live SRAM here is therefore the shared borrowed terms
        // (sinLut 2,048 B + cosTableF 1,024 B; noiseTex256's 64 KB is over the
        // limit and in PSRAM) plus lava's own 512 + 1,920 + 3,840, about 9.3 KB
        // against SRAM_TOTAL_BUDGET's 28,672.
        //
        // The margin is thinner than that sounds. Silk asks 22,536 B, so a
        // silk-then-lava sequence WITHOUT the release in between reaches 24,968
        // and this request crosses the ceiling by 136 bytes. Adding an animation
        // that omits release(), or growing any table by ~136 B, therefore
        // degrades these rows to PSRAM -- read on every row of every band()
        // call, which is exactly the large-table-with-per-pixel-index pattern
        // alloc()'s own CAVEAT warns about and that cost aurora 21%. The spill
        // is silent in the placement path but alloc() now logs the crossing.
        bgRowAll = static_cast<uint16_t *>(alloc(4 * w * sizeof(uint16_t)));
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
        for (int k = 0; k < 16; k++) {
            const float d = (BAYER4[k] / 16.0f - 0.5f) * (1.0f / 128.0f) * 255.0f;
            ditherLUT[k] = static_cast<int32_t>(d >= 0.0f ? d + 0.5f : d - 0.5f);
        }
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
        buildBgRows(bgRowAllW); // needs both ditherLUT (just above) and paletteLUT (just above)
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
        lavaLUT[p2] = static_cast<int32_t>(contribution + 0.5f); // contribution is always >= 0
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
// version) — just windowed to a sub-range instead of always [0, w). Pulled
// out of band() as a named function, not a lambda, so it reads once instead
// of once per merged span in the disassembly, and so its own loops are not
// re-examined by GCC's whole-function budget every time band() changes (see
// the bcTable/tail-copy lesson in AnimNebula.cpp: unrelated loops in a
// function can lose their hardware LOOP when the function grows unrelated
// live state around them — keeping this arithmetic in its own function
// keeps that risk local to this function alone).
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
        // before scaling" (1.6f*kFieldScale == 255.0f exactly) — see
        // file-top comment — as a branchless integer MIN. Dither is added
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

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        memset(fieldRow, 0, w * sizeof(int32_t));
        // Spans of blobs that actually reach this row (dy2 < R2), collected
        // in the same pass that accumulates the field -- no extra iteration
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
            // float setup per touched row per blob — not per pixel).
            const float tt0f = 1.0f - invR2 * (dy2 + dx0 * dx0);
            const float step0f = -invR2 * (2.0f * dx0 + 1.0f);
            int32_t ttQ = static_cast<int32_t>(tt0f * FIXED_SCALE + (tt0f >= 0.0f ? 0.5f : -0.5f));
            int32_t stepQ = static_cast<int32_t>(step0f * FIXED_SCALE + (step0f >= 0.0f ? 0.5f : -0.5f));
            const int32_t step2Q = b.step2Q;
            const int32_t *lut = lavaBase;
            // Pure integer forward-difference sweep: no multiply, no
            // divide, no libm, no branch. lut is zero-padded below index 0
            // (see LUT build in frame()), so out-of-blob pixels (tt<=0,
            // negative shifted index) just add zero — the tt<=0 early-out
            // is folded into the table instead of a per-pixel compare.
            // xlo/xhi are the precomputed full-radius window (superset of
            // the true chord for this row).
            //
            // Counted-down form on purpose: it gives GCC a trip count known
            // at loop entry, which is what lets it emit the Xtensa hardware
            // zero-overhead LOOP instruction here (confirmed in the .S) and
            // drop the per-iteration compare-and-branch entirely. Slightly
            // slower on the x86 bench, which has no such instruction — the
            // device is the one that has to hit 30 fps.
            int32_t *field = fieldRow + xlo;
            for (int n = xhi - xlo + 1; n > 0; n--) {
                *field++ += lut[ttQ >> LUT_SHIFT];
                ttQ += stepQ;
                stepQ += step2Q;
            }
        }

        uint16_t *out = dst + static_cast<size_t>(row) * w;
        const uint16_t *bg = bgRowAll + static_cast<size_t>(y & 3) * bgRowAllW;
        memcpy(out, bg, static_cast<size_t>(w) * sizeof(uint16_t));
        if (nSpans == 0) {
            continue; // no blob touched this row -- background covers all of it, already copied
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
            finalizeSpan(out, mLo, mHi, y & 3);
            mLo = spanLo[i];
            mHi = spanHi[i];
        }
        finalizeSpan(out, mLo, mHi, y & 3);
    }
}

void release() {
    releaseTable(paletteLUT, 256 * sizeof(uint16_t));
    releaseTable(fieldRow, static_cast<size_t>(allocW) * sizeof(int32_t));
    releaseTable(lavaLUT, static_cast<size_t>(LUT_SIZE) * sizeof(int32_t));
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
};

#endif // GAGGIMATE_SIM
