#ifndef GAGGIMATE_SIM

// "Ripples" — rain drops on dark water: up to 4 expanding rings with signed
// height fields that genuinely interfere where they cross. Design: anim-water
// (Fable), 2026-08-15; annulus-span + clamp-LUT restructure, 2026-08-15.
//
// Distance field, sqrt-free and integer: along a scan row, the true Euclidean
// distance r(x) = sqrt((x-cx)^2+dy^2) is 1-Lipschitz in x (|dr/dx| <= 1), so
// as integer x steps by exactly 1 the integer floor(r) changes by at most 1.
// Ripple centers are rounded to the nearest pixel once per frame (g_icx/
// g_icy) so the whole per-pixel tracker runs in int32: per ring per row we
// seed integer r once (one fastSqrt call, at the crossing's left edge x0),
// then carry (r, r2=r*r, dist2) forward per pixel with add/shift only —
// dist2 += 2*dx+1, and r/r2 rebracketed via `while (dist2 >= r2+2r+1) {
// r2+=2r+1; r++; }` (and the mirror decrement) — no multiply, no libm,
// amortized O(1). Rounding the ripple center to the nearest pixel and r to
// an integer (vs. continuous float distance) costs at most ~1px of phase
// error against a 27px wavelength (~13 degrees) — invisible under the
// existing dither.
//
// That integer distance is why this animation does not draw its page design
// pixel for pixel, and it is recorded debt against gm-pciz rather than a bug
// to fix. Measured 2026-09-13 by building a variant that takes the page's
// arithmetic per pixel and nothing else: the exact float centre instead of
// the floored g_icx/g_icy, sqrt of the exact distance instead of the
// tracker's floor(r), the page's envelope 1 - n*n instead of envLUT, the
// page's cos(delta * 2*PI/27) instead of the 256-entry truncated cosine
// table, and the page's own double row expression for the swell instead of
// the same table. That variant is exact: 0 differing pixels at all three
// golden frames, against 17,441, 18,369 and 15,358 here. Taken apart, the
// distance and the two per-pixel tables are 97% of it and the row swell the
// other 3% (593, 649 and 311 pixels left when only the distance and the
// tables were fixed, and the largest deviation anywhere fell from 46 to 8,
// one palette step).
//
// That partial variant is not a cheaper option, which is the number that
// decides this. Three binaries built from the same tree and timed
// interleaved, seven runs each, minimum of 240-frame host bench band times:
// this file 0.263 ms a frame, the distance and tables fixed with the row
// swell left on its table 0.562, everything exact 0.546. So the middle
// option buys 97% of the gap for 100% of the cost, and the row swell is
// free: it is one sine per row against about 100,000 ring pixels a frame.
// The two alternatives are exactness at about 2.1x, or this file.
//
// (An earlier note here said 0.118 to 0.541, about 4.6x. The 0.118 was a
// stale baseline read off a fleet run from 2026-09-10, before something
// outside this file changed what ripples does per frame; the same binary
// measures 0.263 today, and the mathcount column reads 5 libm calls a frame
// where the old runs read 1. Time a baseline in the same session as the
// variant it is being compared with.)
//
// It is not shipped because the page's model is per-pixel libm, and the
// device pays worse than the host does: band_us is already 14,004 for a
// full frame, this is about 100,000 ring pixels a frame, and every one of
// them would need a square root, a cosine and a divide where it now does
// integer adds and two table reads. A float
// version with a double fallback near the rounding boundaries, the scheme
// AnimFireflies.cpp uses, would cut that but not to nothing, and it would
// have to be transcribed bit for bit into the two hand-written Xtensa
// kernels below, which have no build flag to fall back to the portable path.
// Anyone taking that on should measure the device band first: the wave is
// 27 px long, so what the error buys the panel is a ring up to 2 px off its
// design radius, which is a fine-detail difference and not a defect of the
// kind Mandala's wrong shape or Ember's darkness were.
//
// Row-window fix (this pass): a ring is a thin annulus (radial thickness
// 2*HALFW), not a filled disk. The previous version bounded a ring-row's
// x-range using only the OUTER edge (r+HALFW), which for a mature ring
// (radius routinely exceeds the 480px screen width before its amplitude
// decays away) produces an x-window spanning nearly the entire row even
// though only the two ~26px-wide crossings where the annulus actually
// intersects the row can contribute anything (the ring's hollow interior is
// plain water). That's why band_ms stayed ~10x over the ~0.20ms host target
// even after removing the per-pixel libm calls: the per-pixel tracker/
// envelope loop was still walking O(radius) pixels per row, most of which
// always evaluate to "no contribution". Instead we now solve for BOTH the
// outer edge (r+HALFW) and inner edge (r-HALFW) circles' intersection with
// the row, giving up to two disjoint thin crossings (left/right) per ring
// per row — width ~2*HALFW regardless of radius. When the ring hasn't
// grown past HALFW yet, or the row cuts through the annulus's vertical cap
// (inner circle doesn't reach this row), it falls back to one wide-ish
// window (rare: only the ~2*HALFW rows nearest a mature ring's top/bottom).
// A small WIN_MARGIN pads every window because the window is computed from
// the exact float radius while the per-pixel envelope test runs on the
// rounded-integer tracker (~1px slack, see above) — padding a few extra
// pixels that will simply fail the envelope test is far cheaper than
// clipping a real contribution.
//
// Per ring-row, each 1-2 crossings becomes a RowBand entry (own tracker
// seed). RowBand x-ranges are sorted and merged into disjoint spans; pixels
// outside every span never touch the ring math at all. For those pixels
// (and whole rings-free rows) we exploit that the background dither
// (BAYER4[(y&3)*4+(x&3)]) only takes 4 distinct values per row, so a 4-entry
// "tile" of pre-clamped, pre-packed rgb565 values is computed once per row
// and copied for every non-ring pixel — replacing a per-pixel float clamp +
// rgb565 pack with a single array read.
//
// Device codegen (xtensa-asm.sh, real ESP32-S3 GCC): float division has no
// FPU instruction here (compiles to a __divsf3 libcall, ~30-50 cy),
// fmaxf/fminf are libcalls too (not inlined), and bganim::fastCosRad/
// fastSinRad each call cosTableF() (call8 + lazy-init check) on every use.
// Any call inside a loop body blocks GCC's zero-overhead LOOP codegen
// entirely. So band() caches the cosine table pointer once (g_cosTable,
// fetched in init()) and indexes it directly via local cosRadLocal/
// sinRadLocal helpers; the row-level `y/479.0f` divide is a reciprocal
// multiply (INV_ROWMAX); x0/x1 clamps use ternaries instead of fmaxf/fminf;
// and clamp8f's branchy `v<0?0:(v>255?255:cast(v))` chain (3 branches per
// channel, every pixel) is replaced by a single padded uint8 LUT (clampU8),
// indexed by `(int)v + CLAMP_PAD` — bit-for-bit equivalent to clamp8f (see
// the derivation in init()) but branch-free. The crossing half-widths and
// integer-tracker seeds (per-ring-per-row, not per-pixel) used to call
// sqrtf; they now call fastSqrt (Quake-style rsqrt-and-multiply, see its
// definition below) instead — same call site, zero libm calls, since the
// window math only needs ~1px accuracy (WIN_MARGIN already budgets that)
// and the tracker seed is corrected exactly by seedBand's rebracket loop
// regardless of the seed's precision.

// Assembly pass (2026-09-04): band() rewritten to dispatch two hand-written
// Xtensa kernels; bandRef() below is now the spec they must match pixel-for-
// pixel (SleepAnimation::runAnimTest / /api/debug/animtest compares them on
// device). Device baseline: band_us 14,004 for the full 480x480 frame (240
// calls of rows=2), host band_ms 0.118 (see tools/animbench/ASM_BRIEF.md).
//
// Restructure (applies to BOTH bandRef and band(), so it cannot itself be a
// source of drift between them): the original per-pixel loop was
//   for (x in span) for (i in nrb) if (x in rb[i].range) { accumulate; step; }
// i.e. pixel-outer, band-inner, with a per-pixel-per-band range check. That
// shape is hostile to hand-scheduling (a data-dependent branch inside the
// innermost loop, no fixed trip count) and gets in the way of GCC's own
// codegen too. It is restructured to band-outer, pixel-inner:
//   zero hAccBuf[0..w)
//   for (i in nrb): for (x in rb[i].x0..rb[i].x1): hAccBuf[x] += contribution; step tracker
//   for (x in span): read hAccBuf[x], blend into color, pack, store
// This is PROVABLY bit-exact with the original, not just visually
// equivalent: for any pixel x touched by bands i0 < i1 < ..., the original
// added their contributions to hAcc in that same order (i ascending, once
// per x, guarded by the range check) because the outer x-loop revisits every
// band index for every x. The restructured form processes band i0's entire
// x-range first (writing into hAccBuf), then i1's, etc. -- for a specific x,
// the sequence of float additions landing in hAccBuf[x] is still
// ((0 + contrib_i0) + contrib_i1) + ... in the same i-ascending order, just
// computed in a different outer sweep. Floating-point addition order is what
// determines the rounding, and that order is unchanged, so the sum is
// bit-identical. The per-band tracker (curR/curR2/curDx/curDist2) is
// likewise stepped exactly once per x in [rb[i].x0, rb[i].x1], in ascending
// x order, in both forms -- the original's range-check `continue` already
// meant band i's tracker was ONLY stepped while x was inside its own range,
// so restructuring to "loop directly over that range" changes nothing about
// which states it passes through or in what order.
//
// This turns the ring math into two clean, fixed-trip-count kernels with no
// per-pixel branch on band membership:
//   Kernel A (fillTileSpanPie): the background tile[x&3] fill, now a PIE
//     vector store of a resident 8-lane pattern instead of a scalar
//     load+AND+store per pixel. This is the highest-value kernel: a mature
//     ring's radius exceeds the screen diagonal quickly (speed up to 220px/s,
//     life up to 7s -> radius up to ~1540px vs. a 679px screen diagonal), and
//     once it does, EVERY row it still intersects contributes only two thin
//     ~2*(HALFW+WIN_MARGIN)=30px annulus crossings -- the other 450+ columns
//     of that row are background tile fill. Across a frame with 1-4 rings
//     active, background fill is the majority of the 230,400 pixels/frame
//     even while rings are on screen, so this is where OPTIMIZE.md's "span/
//     tile skipping" bullet pays off most.
//   Kernel B (accumulateBandAsm): one band's contribution over its own
//     x-range, hand-scheduled scalar (envLUT/cosTable are gathers -- no PIE
//     vector gather instruction exists, per ASM_BRIEF.md -- so this stays
//     scalar FPU code, not vectorised). Verified real Xtensa hardware FPU
//     is usable from bare-metal QEMU (this repo's tools/qemubench harness)
//     PROVIDED CPENABLE is set first: a probe using bare add.s/mul.s/
//     trunc.s double-faulted (repeating jumps to the DoubleException vector)
//     until `movi a4,0xff; wsr.cpenable a4; rsync` ran before any FP
//     instruction, then passed bit-exact (add.s 3.5+2=5.5, mul.s 3.5*2=7.0,
//     trunc.s 7.0->7, all exact). An earlier version of this kernel carried
//     that same CPENABLE write itself, reasoning it as "redundant on device,
//     required under the harness" -- that reasoning was wrong and the write
//     was removed: on real hardware CPENABLE is not just "already set", it
//     is the OS's bookkeeping for who owns the live FPU register file, and
//     self-enabling bypasses the save/restore that keeps a preempted task's
//     float state intact (full mechanism in the comment on accumulateBandAsm
//     itself, below). QEMU has no OS and nothing else to corrupt, so the
//     bring-up belongs there instead, once, in tests/anim_ripples/main.cpp.
//     This is why the fixed-point-LUT redesign that was the first plan here
//     was abandoned in favour of literal FPU asm: once the FPU was confirmed
//     to work and be bit-exact, transliterating the existing float algorithm
//     directly is strictly lower-risk than re-deriving new Q-format
//     constants and LUT contents that would also have needed bandRef itself
//     restructured to match.
// The final blend+pack (baseR/G/B + hAcc*g_glow*crest/trough, dither,
// clampU8 gather, rgb565 pack) stays portable C++ (blendPackSpan below),
// shared verbatim by both band() and bandRef() -- it touches a gather table
// (clampU8) GCC already compiles reasonably at -O2, unlike the tile-fill and
// ring-accumulate loops which specifically hit patterns GCC compiles badly
// (a per-pixel branch-and-load memset-alike, and a per-pixel-per-band range
// check); left as a candidate for a future pass, not attempted here.

// Parameter pass (2026-09-10, gm-3vj.8): four more sliders, taking this
// animation from 4 to 8. Every one of them acts in frame() or in a table
// frame() rebuilds -- drop spread moves the landing point, ring width
// rewrites envLUT, water tone scales two per-row constants, trough dip
// scales a colour factor blendPackSpan reads -- so neither pixel loop
// changed and the two hand-written Xtensa kernels below are untouched.
// Each new slider defaults to 50, and at 50 the arithmetic collapses to the
// constant this file hard-coded before: paramScale returns a literal 1.0f,
// the spread term is 1.0f + 0.0f, and rebuildEnvLUT takes a branch that
// writes the original expression. So the default output is the old output
// bit for bit, which is what the golden frames check.
#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

constexpr int MAX_RIPPLES = 4;
constexpr int MAX_BANDS = MAX_RIPPLES * 2; // a ring can split into a left+right annulus crossing per row
constexpr float HALFW = 13.0f;
constexpr float WAVEFREQ = 6.2831853f / 27.0f;
constexpr float ENV_SCALE = 255.0f / HALFW; // replaces a per-pixel divide by HALFW
constexpr float INV_ROWMAX = 1.0f / 479.0f;  // replaces a per-row divide by 479.0f
constexpr float RAD_TO_TABLE = 256.0f / 6.2831853f;
constexpr float WIN_MARGIN = 2.0f; // px slack on every crossing window (see file header)

// Speed law (2026-09-12, gm-kh2s). Before this pass the Speed slider set
// the ring travel speed linearly, 25 px/s at 0 to 220 px/s at 100, a ring
// lived a fixed 7 s at the default Fade, and its amplitude decayed with
// age. The fleet's movement metric (half change time, target 1200 ms at
// Speed 50) read 118 ms at 50, and slower again above 50, because a fast
// ring outran the panel and left it empty. The target needs a ring to move
// about 6 px in 1.2 s, so the travel speed at 50 is now RING_SPEED_50 and
// follows the universal curve, and the other quantities are defined so the
// pond keeps rings on it at every speed:
//   speed  = RING_SPEED_50 * speedMul(p[0])          px/s
//   travel = FADE_REF_SPEED * lifeS(p[2])            px, amplitude decay length
//   life   = LIFE_MUL * lifeS(p[2]) / speedMul(p[0]) s
//   drops  = interval(p[1]) / speedMul(p[0])         ms between deadlines
//   amp    = rise * exp(-radius / travel) * tail(age / life)
// where lifeS is the old 7 s to 2.2 s Fade span and FADE_REF_SPEED is the
// old speed at slider 50, so the brightness a ring has at a given radius is
// the one this file shipped with. The life is a time, so at a slow speed a
// ring fades out while still on the panel, which the old hard cut at
// amp 0.37 could not do without a visible pop: tail() takes the amplitude
// to zero over the last TAIL_FRAC of the life. With four slots and a drop
// every 9 s at the default Drop rate, a 37 s life keeps three to four rings
// on the pond; a life tied to the travel distance instead was tried first
// and at slow speeds it held every slot for minutes, so the pond emptied
// for a quarter of the time and then took four drops in a burst. The
// swell of the water surface follows the same speed multiplier so the
// whole picture obeys the slider.
constexpr float RING_SPEED_50 = 4.0f;
constexpr float FADE_REF_SPEED = 122.5f;
constexpr float LIFE_MUL = 8.0f;
constexpr float TAIL_FRAC = 0.4f;

// Padded clamp-to-uint8 LUT sizing. Worst case for cr/cg/cb (see clamp8f
// call sites below): baseR/baseG/baseB in [0,255] (themeRGB output, whatever
// the water tone parameter does to the position it asks for); ring
// term g*crestF/troughF where |g| = |hAcc * g_glow| and hAcc sums up to
// MAX_RIPPLES per-ring contributions, each bounded by amp*cos*env <=
// amp_max * env_max ~= 0.98 * 1.6 = 1.57 (rise in [0,1], the tail in [0,1],
// expf(-radius/travel) < 1 with its peak at ageS=0.18s, a radius under 6 px
// at Speed 100 against a travel of at least 270 px -> ~0.98; envLUT holds
// a*(1-n*n)^e with the base in [0,1] and a <= 1.6 at the
// fattest ring width, p[5]=0), so |hAcc| <= 4*1.56 = 6.24; g_glow in
// [0.35, 1.5] (p[3] 0..100) so |g| <= 9.36. crestF[ch] <= 255*0.65 = 165.75
// (crest branch, g>0): cr_max ~= 255 + 9.36*165.75 + dith(4.125) ~= 1810.8.
// troughF[ch] <= crestF*0.72 <= 119.34 (trough branch, g<0, at the deepest
// trough dip p[7]=100): cr_min ~= 0 - 9.36*119.34 - 4.125 ~= -1121.2. So the
// true range is about [-1122, 1811]; CLAMP_PAD/SIZE below add a comfortable
// margin on both ends. Both grew for the two new parameters that widen this
// bound (p[5] lifts env above 1, p[7] takes the trough factor to 6x its old
// fixed 0.12), which costs 1,600 B more of the hot slab: 4,544 B of 9,216
// before, 6,144 after, so nothing here had to be shrunk to fit either.
constexpr int CLAMP_PAD = 1200;
constexpr int CLAMP_SIZE = 3200; // covers b = (int)v in [-1200, 1999]

// Local equivalents of bganim::fastCosRad/fastSinRad that take an already-
// fetched table pointer, so callers don't pay a cosTableF() call8 per use
// (see file header). Same indexing formula as BgAnimCommon.h.
inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
inline float sinRadLocal(const float *ct, float rad) { return cosRadLocal(ct, rad - 1.5707963f); }

// A 0-100 parameter as a multiplier on a tuned constant: exactly 1.0 at 50,
// 1/base at 0, base at 100, geometric in between. Every parameter this file
// gained on 2026-09-10 defaults to 50 and scales a constant that was
// hard-coded before, so the default multiplies by a literal 1.0f and
// reproduces the old constant bit for bit -- the `d == 0` branch is what
// makes that a guarantee rather than a hope about powf's rounding. Called
// from frame() and the table rebuilds, never per row and never per pixel.
inline float paramScale(uint8_t v, float base) {
    const int d = static_cast<int>(v) - 50;
    return d == 0 ? 1.0f : powf(base, d / 50.0f);
}

// Quake-style fast approximate sqrt: rsqrt via the bit-hack magic constant,
// refined by two Newton iterations, then sqrt(x) = x * rsqrt(x). Device has
// no HW sqrt (sqrtf compiles to a ~90cy libcall) but DOES have a HW FPU for
// mul/add/madd (1-2cy each) and no divide is used here at all, so this is
// ~8 float mul/add ops instead of a libcall -- an order of magnitude
// cheaper, and fully inlinable (keeps GCC's zero-overhead-loop eligibility
// for anything this gets hoisted into). Two iterations bring relative error
// well under 0.01%, negligible next to the ~2px WIN_MARGIN slack the window
// math already carries, and the seedBand() integer correction loop below
// exactly fixes up any residual error in its use of this for r0 regardless.
// Only used for window half-widths and the seed radius -- never per-pixel.
inline float fastSqrt(float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    const float xhalf = 0.5f * x;
    int32_t i;
    __builtin_memcpy(&i, &x, sizeof(i));
    i = 0x5f3759df - (i >> 1);
    float y;
    __builtin_memcpy(&y, &i, sizeof(y));
    y = y * (1.5f - xhalf * y * y);
    y = y * (1.5f - xhalf * y * y);
    return x * y;
}

// One annulus crossing's window plus its incremental sqrt-free integer
// distance tracker, seeded once at x0 (see file header, "Distance field"
// section) and stepped per pixel while x sweeps its own [x0, x1]. Hoisted to
// namespace scope (was a local struct inside band()) because accumulateBandRef
// and accumulateBandAsm below now take one as a parameter -- see the
// 2026-09-04 header section for why the per-pixel walk was restructured to
// band-outer, pixel-inner.
struct RowBand {
    float r, amp;
    int x0, x1;
    int curDx, curDist2, curR, curR2;
};

// Everything buildRowState() computes once per scanline: shared, byte-for-
// byte, by band() and bandRef(), so the two can never disagree about WHERE a
// ring contributes or what the background looks like -- only about how the
// two hot per-pixel loops (fill, accumulate) are coded.
struct RowState {
    float baseR, baseG, baseB;
    uint16_t tile[4];  // background pixel for x&3 == 0..3 (dither applied)
    float dith4[4];    // same dither term, unpacked (x&3 == 0..3), for ring pixels
    RowBand rb[MAX_BANDS];
    int nrb;
    int spanX0[MAX_BANDS], spanX1[MAX_BANDS]; // disjoint, x1 inclusive
    int nSpans;
};

struct Ripple {
    float cx, cy;
    int32_t birthMs; // signed so ages and deadlines survive the millis wrap
    bool active;
};
Ripple ripples[MAX_RIPPLES];
int32_t nextDropMs = 0;
bool primed = false; // the first frame after init warms the pond, see frame()
uint32_t rng = 0xC0FFEE;
float *envLUT = nullptr;     // 256: 1-(i/255)^2
uint8_t *clampU8 = nullptr;  // CLAMP_SIZE: clamp(idx-CLAMP_PAD, 0, 255) — see derivation above
float *g_hAccBuf = nullptr;  // 480 floats: one row's pre-summed ring height, see full comment at its use site below
const float *g_cosTable = nullptr; // cached once so band()/frame() never call cosTableF()
bool inited = false;
uint32_t lastThemeGen = 0xFFFFFFFF;
float crestF[3] = {62, 98, 127}, troughF[3] = {7, 12, 15};
// Parameter values the two rebuilt tables were last built for, so frame()
// rebuilds only when the user moves that slider (envLUT is 256 powf calls,
// the crest/trough colours three multiplies).
uint8_t lastWidthP = 50;
uint8_t lastTroughP = 50;
// Water tone (p[6]): the vertical span and the offset of the background's
// gradient position. 20.0f/3.0f are the values every build before
// 2026-09-10 hard-coded, and paramScale(50, ...) puts them back exactly.
float g_toneSpan = 20.0f;
float g_toneOff = 3.0f;

int g_n = 0;
float g_cx[MAX_RIPPLES], g_cy[MAX_RIPPLES], g_r[MAX_RIPPLES], g_amp[MAX_RIPPLES];
int g_icx[MAX_RIPPLES], g_icy[MAX_RIPPLES]; // centers rounded to nearest pixel, for the integer tracker
float g_glow = 1.0f;
float g_swellT = 0.0f; // tMs scaled by the speed multiplier, for the swell

// Ring width (p[5]): the radial envelope is a gather table, so the whole
// parameter lives in this rebuild and neither pixel loop -- the portable one
// or the hand-written Xtensa kernel, which reads envLUT by index and knows
// nothing about its contents -- changes at all. n = ad/HALFW, and the
// envelope is 1 - n*n at the default, written as the original expression on
// purpose (powf(x, 1.0f) is very probably x, but "very probably" is not the
// golden-frame contract).
//
// The two directions are shaped differently, and the reason is the fixed
// cosine the envelope multiplies. cos(ad*WAVEFREQ) crosses zero at
// ad = 6.75 px, so the annulus is a lit crest inside that radius and a
// shadow outside it, and the envelope is already near 1 across the crest.
// Bending the curve alone therefore moves only the shadow, which is a few
// levels on near-black water: the first version of this parameter measured
// 11 of 255 at one end and 5 at the other, which is not a parameter a
// person would notice. So:
//   thinner (above 50): squeeze the envelope's support, n' = min(1, n*c)
//     with c up to 4, which takes it to zero at ad = 3.25 px instead of 13
//     and narrows the lit band itself.
//   fatter (below 50): flatten the curve AND lift it, a * (1 - n*n)^e with
//     a up to 1.6 and e down to 0.2, so the crest reaches further out
//     before it falls away and the shadow behind it deepens.
// Both meet the default at 1 - n*n exactly (a and e are 1.0f there, and
// multiplying by 1.0f is exact). The base stays in [0, 1] so there is no
// negative base and no NaN, but the lift means the table now reaches 1.6
// rather than 1, which the clamp table above is sized for.
void rebuildEnvLUT(uint8_t widthP) {
    if (envLUT == nullptr) {
        return;
    }
    const int d = static_cast<int>(widthP) - 50;
    if (d == 0) {
        for (int i = 0; i < 256; i++) {
            const float n = i / 255.0f;
            envLUT[i] = 1.0f - n * n;
        }
        return;
    }
    if (d > 0) {
        const float c = 1.0f + (d / 50.0f) * 3.0f; // 1 .. 4
        for (int i = 0; i < 256; i++) {
            float n = (i / 255.0f) * c;
            if (n > 1.0f) {
                n = 1.0f;
            }
            envLUT[i] = 1.0f - n * n;
        }
        return;
    }
    const float e = 1.0f + (d / 50.0f) * 0.8f;  // 1 .. 0.2
    const float a = 1.0f - (d / 50.0f) * 0.6f;  // 1 .. 1.6
    for (int i = 0; i < 256; i++) {
        const float n = i / 255.0f;
        envLUT[i] = a * powf(1.0f - n * n, e);
    }
}

bool init(int, int) {
    // envLUT, clampU8 and g_hAccBuf are all read (and, for g_hAccBuf,
    // written) once or more per pixel -- allocHot() puts them in the fixed
    // internal-SRAM slab instead of PSRAM (BgAnimCommon.h's GM_BGANIM_HOT_SLAB
    // comment: placement decides more of band() time than the kernel does).
    // Total ask is 1,024 + 3,200 + 1,920 = 6,144 B, comfortably inside the
    // 9,216 B this animation gets after the shared sinLut/cosTableF term, so
    // nothing here was shrunk to fit. allocHot() falls back to alloc()
    // (PSRAM) on its own if the slab is ever full when this runs -- same
    // nullptr contract as alloc(), checked the same way below.
    if (envLUT == nullptr) {
        envLUT = static_cast<float *>(allocHot(256 * sizeof(float)));
        if (envLUT == nullptr) {
            return false;
        }
        // init() gets no parameters, so the table starts at the default
        // width and frame() rebuilds it on the first pass that sees a
        // different p[5]. lastWidthP is set here, next to the build it
        // describes, so a release()/init() cycle can never leave the
        // sentinel claiming a width the fresh table does not have.
        lastWidthP = 50;
        rebuildEnvLUT(lastWidthP);
    }
    if (clampU8 == nullptr) {
        clampU8 = static_cast<uint8_t *>(allocHot(CLAMP_SIZE));
        if (clampU8 == nullptr) {
            return false;
        }
        for (int i = 0; i < CLAMP_SIZE; i++) {
            const int b = i - CLAMP_PAD;
            clampU8[i] = b < 0 ? 0 : (b > 255 ? 255 : static_cast<uint8_t>(b));
        }
    }
    if (g_hAccBuf == nullptr) {
        g_hAccBuf = static_cast<float *>(allocHot(480 * sizeof(float)));
        if (g_hAccBuf == nullptr) {
            return false;
        }
        // No init loop needed: every band()/bandRef() call memsets its own
        // working width before accumulating into it (see below), so content
        // left behind by a previous animation's use of this slab memory is
        // always overwritten before it is read.
    }
    if (g_cosTable == nullptr) {
        // Fetched once here (same lazy-build semantics as fastCosRad's own
        // first call) so band()/frame() can index it directly with zero
        // per-use call overhead.
        g_cosTable = cosTableF();
    }
    if (!inited) {
        inited = true;
        for (auto &r : ripples) {
            r.active = false;
        }
        primed = false;
    }
    return true;
}

// Water surface sits in the theme's darkest ~10%; ring crests borrow the
// brightest stop, troughs a dimmed version of it. The trough dip (p[7])
// scales that dimming: 0.12 of the crest colour at the default, a sixth of
// that at 0 (a bright arc on flat water, no dark ring beside it at all) and
// six times it at 100 (a dark ring that reaches black where the wave is
// strongest). The water itself is only 10 to 25 levels above black, so this
// is a small number of levels either way; it is what makes the shadow
// beside a crest read as a trough rather than as nothing. Both factors are
// read only by blendPackSpan, which is portable C++ shared by band() and
// bandRef(), so this parameter does not reach the Xtensa kernel either.
void rebuildThemeAssets(uint8_t troughP) {
    uint8_t c[3];
    themeRGB(255, c);
    const float troughMul = 0.12f * paramScale(troughP, 6.0f);
    for (int ch = 0; ch < 3; ch++) {
        crestF[ch] = c[ch] * 0.65f;
        troughF[ch] = crestF[ch] * troughMul;
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    if (themeGen() != lastThemeGen || p[7] != lastTroughP) {
        lastTroughP = p[7];
        rebuildThemeAssets(lastTroughP);
        lastThemeGen = themeGen();
    }
    if (p[5] != lastWidthP) {
        lastWidthP = p[5];
        rebuildEnvLUT(lastWidthP);
    }
    // Water tone (p[6]): scales both terms of the background gradient
    // position, so the water goes from near-flat black at 0 to a clearly
    // graded, lighter surface at 100.
    {
        const float toneMul = paramScale(p[6], 3.0f);
        g_toneSpan = 20.0f * toneMul;
        g_toneOff = 3.0f * toneMul;
    }
    // Drop spread (p[4]): the random landing point pulled toward the centre
    // (0.30 at slider 0, so every drop lands inside the middle 30 percent of
    // the panel) or pushed out past the rim (1.70 at slider 100, so some
    // drops land off-panel and their rings sweep in as arcs). Written as rand*s + centre*(1-s)
    // because at s == 1.0f exactly that is rand + 0.0f, the old expression
    // bit for bit, and the random stream is untouched at every setting: the
    // same drops at the same times, moved.
    const float spread = 1.0f + static_cast<float>(static_cast<int>(p[4]) - 50) * 0.014f;
    const float spd = speedMul(p[0]);
    // Speed divides every time constant and multiplies the travel speed, so
    // the fast end is the Speed 50 pond played faster rather than a different
    // pond (gm-kh2s). Until 2026-09-12 only the travel speed scaled: a ring
    // then reached the rim in a fraction of the time while drops still landed
    // every nine seconds and a ring still took 37 s to fade, so the top of the
    // slider showed less movement than three quarters of it. The fleet sweep
    // caught it as a dip in the one-step change fraction, 6.04 percent at
    // Speed 75 against 3.93 at Speed 100, which is the slider running
    // backwards as far as the eye is concerned.
    //
    // travel is a distance, not a time, and stays where it is: a ring's
    // brightness at a given radius is the one this file shipped with, at every
    // speed. The catch-up bound below is unchanged, because life and interval
    // are divided by the same number and only their ratio enters it.
    const float interval = lerpf(14000.0f, 1500.0f, p[1] / 100.0f) / spd;
    const float speed = RING_SPEED_50 * spd;
    const float lifeS = lerpf(7.0f, 2.2f, p[2] / 100.0f);
    const float travel = FADE_REF_SPEED * lifeS;
    const float life = LIFE_MUL * lifeS / spd;
    g_swellT = static_cast<float>(tMs) * spd;
    g_glow = 0.35f + 1.15f * (p[3] / 100.0f);

    // Drops land on their deadlines, not on the frame that first passes one,
    // and every deadline up to now is played out in turn. So the first frame
    // after init, and the first frame after a gap longer than a ring's life,
    // start from a pond that has been raining for one lifetime rather than
    // from flat water: at the default Fade a ring takes 37 s to fade, and without
    // this the panel would show empty water and then one small ring for the
    // first half minute. The catch-up starts on the life boundary before
    // the one that precedes now (between one and two lives back, so it is
    // bounded at 2 * life / (0.55 * interval) deadlines, under a hundred at
    // the fastest drop rate), and it starts there rather than at now minus
    // one life so that the host bench, whose first frame is at 0, and the
    // web page, whose first render is at 1990 ms, play the same deadlines
    // from the same seed and draw the same rings. A deadline further back
    // than one life restarts the same way. A ring that would have died
    // before a deadline frees its slot before that deadline's drop, the
    // same as it would have frame by frame.
    const int32_t now = static_cast<int32_t>(tMs);
    const int32_t lifeMs = static_cast<int32_t>(life * 1000.0f);
    if (!primed || now - nextDropMs > lifeMs) {
        primed = true;
        int32_t q = now / lifeMs; // floor division for a negative now (after a wrap)
        if (now % lifeMs < 0) {
            q--;
        }
        nextDropMs = q * lifeMs - lifeMs;
    }
    while (now - nextDropMs >= 0) {
        const int32_t at = nextDropMs;
        for (auto &r : ripples) {
            if (r.active && (at - r.birthMs) * 0.001f >= life) {
                r.active = false;
            }
        }
        for (auto &r : ripples) {
            if (!r.active) {
                const float rx = nextRandf(rng) * w;
                const float ry = nextRandf(rng) * h;
                const float invS = 1.0f - spread;
                r = {rx * spread + (w * 0.5f) * invS, ry * spread + (h * 0.5f) * invS, at, true};
                break;
            }
        }
        nextDropMs = at + static_cast<int32_t>(interval * (0.55f + 0.9f * nextRandf(rng)));
    }

    g_n = 0;
    for (auto &r : ripples) {
        if (!r.active) {
            continue;
        }
        const float ageS = (now - r.birthMs) * 0.001f;
        if (ageS >= life) {
            r.active = false;
            continue;
        }
        const float radius = speed * ageS;
        const float rise = ageS < 0.18f ? ageS / 0.18f : 1.0f;
        // Smoothstep from 1 at (1 - TAIL_FRAC) of the life to 0 at its end.
        float tail = (1.0f - ageS / life) * (1.0f / TAIL_FRAC);
        if (tail > 1.0f) {
            tail = 1.0f;
        }
        tail = tail * tail * (3.0f - 2.0f * tail);
        const float amp = rise * expf(-radius / travel) * tail;
        if (radius <= 0 || amp < 0.008f) {
            continue;
        }
        g_cx[g_n] = r.cx;
        g_cy[g_n] = r.cy;
        // Round once per frame (not per row/pixel) for the integer tracker.
        // Truncation-after-offset is a valid round for cx/cy >= 0, which is
        // every drop the spread parameter has not pushed off the panel; for
        // a negative centre the extra decrement makes it a floor instead of
        // a round toward zero. It lands one pixel low at an exact negative
        // integer, which is the same ~1 px of phase error the rounding
        // itself already costs and which WIN_MARGIN already budgets.
        const float rcx = r.cx + 0.5f;
        const float rcy = r.cy + 0.5f;
        g_icx[g_n] = static_cast<int>(rcx) - (rcx < 0.0f ? 1 : 0);
        g_icy[g_n] = static_cast<int>(rcy) - (rcy < 0.0f ? 1 : 0);
        g_r[g_n] = radius;
        g_amp[g_n] = amp;
        g_n++;
    }
}

// Builds tile[]/dith4[]/rb[]/spans for row y: everything about WHERE a ring
// contributes and what the background looks like there. Single
// implementation shared by band() and bandRef() (see file header) -- the two
// can only differ in HOW each span's pixels get filled/accumulated, never in
// which pixels that is.
void buildRowState(int y, int w, RowState &rs) {
    const float wMinus1 = static_cast<float>(w - 1);
    {
        const float vt = y * INV_ROWMAX;
        const float swell = sinRadLocal(g_cosTable, g_swellT * 0.00014f + y * 0.014f) * 2.5f;
        // g_toneSpan/g_toneOff are 20.0f/3.0f at the default water tone, so
        // this is the original expression there. The upper clamp is 127
        // rather than 31 because the highest tone (span 60, offset 9) asks
        // for positions up to about 72; the old 31 was never reached at the
        // default (max 25.5), so raising it changes nothing there, and
        // themeRGB clamps to 255 on its own anyway.
        int basePos = static_cast<int>(vt * g_toneSpan + swell + g_toneOff);
        if (basePos < 0) {
            basePos = 0;
        } else if (basePos > 127) {
            basePos = 127;
        }
        uint8_t baseC[3];
        themeRGB(basePos, baseC);
        rs.baseR = baseC[0];
        rs.baseG = baseC[1];
        rs.baseB = baseC[2];

        // The background dither only takes 4 distinct values per row
        // (BAYER4 indexes x&3): tile[] packs them to rgb565 for the
        // ring-free majority of the row, dith4[] keeps the raw term for
        // ring pixels (blendPackSpan below), same formula, computed once
        // here instead of once per pixel either way.
        for (int k = 0; k < 4; k++) {
            const float dith = (BAYER4[(y & 3) * 4 + k] - 7.5f) * 0.55f;
            rs.dith4[k] = dith;
            rs.tile[k] = rgb565(clampU8[static_cast<int>(rs.baseR + dith) + CLAMP_PAD],
                                 clampU8[static_cast<int>(rs.baseG + dith) + CLAMP_PAD],
                                 clampU8[static_cast<int>(rs.baseB + dith) + CLAMP_PAD]);
        }
    }

    rs.nrb = 0;
    RowBand *rb = rs.rb;
    int &nrb = rs.nrb;
    {
        auto seedBand = [&](int ringIdx, int bx0, int bx1) {
            if (bx1 < bx0) {
                return;
            }
            // Seed the integer distance at bx0 (one fastSqrt per crossing
            // per row — not per pixel, and libm-free; see fastSqrt above).
            const int idy = y - g_icy[ringIdx];
            const int idx0 = bx0 - g_icx[ringIdx];
            const int dist2 = idx0 * idx0 + idy * idy;
            int r0 = static_cast<int>(fastSqrt(static_cast<float>(dist2)));
            while ((r0 + 1) * (r0 + 1) <= dist2) {
                r0++;
            }
            while (r0 * r0 > dist2) {
                r0--;
            }
            rb[nrb++] = {g_r[ringIdx], g_amp[ringIdx], bx0, bx1, idx0, dist2, r0, r0 * r0};
        };

        for (int i = 0; i < g_n; i++) {
            const float dy = y - g_cy[i];
            const float dy2 = dy * dy;
            const float outerR = g_r[i] + HALFW + WIN_MARGIN;
            const float outer2 = outerR * outerR;
            if (dy2 > outer2) {
                continue;
            }
            const float halfOuter = fastSqrt(outer2 - dy2);
            const float innerR = g_r[i] - HALFW - WIN_MARGIN;
            bool split = false;
            if (innerR > 0.0f) {
                const float inner2 = innerR * innerR;
                if (dy2 < inner2) {
                    // Row cuts through the annulus proper (not just its cap):
                    // two thin crossings, ~2*(HALFW+WIN_MARGIN) wide each,
                    // instead of one wide disk-width window. This is the fix
                    // — previously the window always spanned the full outer
                    // disk (leftF..rightF below), which for a mature ring
                    // (radius >> HALFW) meant walking almost the entire row
                    // width per ring per row even though the ring's hollow
                    // interior never contributes.
                    split = true;
                    const float halfInner = fastSqrt(inner2 - dy2);
                    // The two crossings touch when halfInner approaches 0,
                    // which every mature ring does on the rows where |dy|
                    // nears innerR: (int)(cx - eps) and (int)(cx + eps)
                    // truncate to the SAME column, and nothing downstream
                    // dedupes rings, so that column would accumulate this
                    // ring's contribution twice. That is both a one-pixel
                    // brightness artifact and an unmodelled term in the
                    // clampU8 index bound derived above. Track where the left
                    // crossing actually ended and start the right one after
                    // it. -1 means the left crossing seeded nothing (bx0 is
                    // always >= 0, so it can never collide with the sentinel).
                    int leftEnd = -1;
                    {
                        const float leftF = g_cx[i] - halfOuter;
                        const float rightF = g_cx[i] - halfInner;
                        const int bx0 = static_cast<int>(leftF > 0.0f ? leftF : 0.0f);
                        const int bx1 = static_cast<int>(rightF < wMinus1 ? rightF : wMinus1);
                        if (bx1 >= bx0) {
                            leftEnd = bx1;
                        }
                        seedBand(i, bx0, bx1);
                    }
                    {
                        const float leftF = g_cx[i] + halfInner;
                        const float rightF = g_cx[i] + halfOuter;
                        int bx0 = static_cast<int>(leftF > 0.0f ? leftF : 0.0f);
                        if (bx0 <= leftEnd) {
                            bx0 = leftEnd + 1;
                        }
                        const int bx1 = static_cast<int>(rightF < wMinus1 ? rightF : wMinus1);
                        seedBand(i, bx0, bx1);
                    }
                }
            }
            if (!split) {
                // Ring hasn't grown past HALFW yet, or this row only clips
                // the annulus's vertical cap (rare — a ~2*(HALFW+margin)-row
                // band near a mature ring's top/bottom): fall back to the
                // single outer-disk window, same as before this pass.
                const float leftF = g_cx[i] - halfOuter;
                const float rightF = g_cx[i] + halfOuter;
                const int bx0 = static_cast<int>(leftF > 0.0f ? leftF : 0.0f);
                const int bx1 = static_cast<int>(rightF < wMinus1 ? rightF : wMinus1);
                seedBand(i, bx0, bx1);
            }
        }

    }

    // Merge the (up to MAX_BANDS) crossing windows into disjoint spans so
    // every pixel outside all of them can skip straight to the tile lookup
    // instead of paying nrb bound-checks. nrb is tiny (<= 8), so an
    // insertion sort is cheap and branch-predictable.
    for (int a = 1; a < nrb; a++) {
        RowBand key = rb[a];
        int b = a - 1;
        while (b >= 0 && rb[b].x0 > key.x0) {
            rb[b + 1] = rb[b];
            b--;
        }
        rb[b + 1] = key;
    }
    rs.nSpans = 0;
    for (int a = 0; a < nrb; a++) {
        if (rs.nSpans > 0 && rb[a].x0 <= rs.spanX1[rs.nSpans - 1] + 1) {
            if (rb[a].x1 > rs.spanX1[rs.nSpans - 1]) {
                rs.spanX1[rs.nSpans - 1] = rb[a].x1;
            }
        } else {
            rs.spanX0[rs.nSpans] = rb[a].x0;
            rs.spanX1[rs.nSpans] = rb[a].x1;
            rs.nSpans++;
        }
    }
}

// Accumulates one ring-crossing band's contribution into hAccBuf[b.x0..b.x1]
// and advances its tracker across the same range. Portable reference: same
// per-pixel formula the original single-pass loop used, just pulled out of
// the x-outer/band-inner double loop into a band-outer/x-inner call (see
// file header for the proof that this reordering is bit-exact with the
// pre-restructure code). Used directly by bandRef(), and by band() itself
// wherever accumulateBandAsm below is unavailable (non-Xtensa, or
// GM_BGANIM_NO_ASM).
void accumulateBandRef(float *hAccBuf, const RowBand &b, const float *cosTable, const float *envLUT) {
    int curDx = b.curDx, curDist2 = b.curDist2, curR = b.curR, curR2 = b.curR2;
    for (int x = b.x0; x <= b.x1; x++) {
        // curR is already the tracked integer distance for this x (seeded
        // at x0, stepped at the bottom of this loop).
        const float delta = static_cast<float>(curR) - b.r;
        const float ad = fabsf(delta);
        if (ad <= HALFW) {
            const float env = envLUT[static_cast<int>(ad * ENV_SCALE)];
            hAccBuf[x] += b.amp * cosRadLocal(cosTable, delta * WAVEFREQ) * env;
        }
        // Step the sqrt-free tracker to x+1: dist2 grows by 2*dx+1 as dx
        // increments by exactly 1 (int add), then rebracket curR/curR2 with
        // add/shift only (no multiply, no sqrt), amortized O(1), at most
        // one nudge either way.
        curDist2 += 2 * curDx + 1;
        curDx += 1;
        while (curDist2 >= curR2 + 2 * curR + 1) {
            curR2 += 2 * curR + 1;
            curR++;
        }
        while (curDist2 < curR2) {
            curR2 -= 2 * curR - 1;
            curR--;
        }
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)

// Kernel A: row[x0..x1) = tile[x&3]. PIE vector store of a resident 8-lane
// pattern for the aligned bulk, scalar for the unaligned edges.
//
// Alignment: EE.VLD.128.IP/EE.VST.128.IP mask the low four address bits
// silently instead of trapping (SleepAnimation.cpp's scale565Oct comment
// on kPieMasks), so a misaligned store corrupts the pixels either side of
// it rather than failing loudly. Every row here is 16-byte aligned
// (960-byte stride off a 64-byte-aligned band buffer, see this repo's
// CLAUDE.md), but a span's x0 is an arbitrary crossing-window edge, not
// generally a multiple of 8 pixels (16 bytes = 8 x uint16_t). So this
// kernel walks a scalar prefix up to the next 8-pixel boundary, vector-
// stores the aligned middle, then a scalar suffix for what is left
// (< 8 pixels). Because 8 is a multiple of the tile's period (4), that
// aligned boundary is ALSO always a multiple of 4, so the 8-lane pattern
// loaded once before the loop, [t0,t1,t2,t3,t0,t1,t2,t3], is valid at any
// 8-pixel-aligned start regardless of where x0 itself fell in the x&3
// cycle -- the scalar prefix (one pixel at a time from x0) keeps the
// tile[x&3] indexing correct up to that point by construction.
//
// PIE is coprocessor CP3, thread-context only; band() runs on the SleepAnim
// task, never an ISR, so q0 needs no clobber list (the compiler never
// allocates q registers -- same citation as scale565Oct).
__attribute__((noinline)) static void fillTileSpanPie(uint16_t *row, int x0, int x1, const uint16_t tile[4]) {
    int x = x0;
    const int alignedStart = (x0 + 7) & ~7;
    const int prefixEnd = alignedStart < x1 ? alignedStart : x1;
    for (; x < prefixEnd; x++) {
        row[x] = tile[x & 3];
    }
    const int n8 = (x1 - x) >> 3;
    if (n8 > 0) {
        uint16_t *wr = row + x;
        alignas(16) uint16_t pat[8] = {tile[0], tile[1], tile[2], tile[3], tile[0], tile[1], tile[2], tile[3]};
        const uint16_t *patPtr = pat;
        int n = n8;
        asm volatile("ee.vld.128.ip q0, %[pp], 0\n" // q0 = the 8-lane pattern, resident for the loop
                     "1:\n"
                     "ee.vst.128.ip q0, %[wr], 16\n"
                     "addi %[n], %[n], -1\n"
                     "bnez %[n], 1b\n"
                     : [wr] "+r"(wr), [n] "+r"(n)
                     : [pp] "r"(patPtr)
                     : "memory");
        x += n8 << 3;
    }
    for (; x < x1; x++) {
        row[x] = tile[x & 3];
    }
}

// Kernel B: same math as accumulateBandRef above, hand-scheduled scalar
// Xtensa FPU asm. envLUT/cosTable are gather tables (index computed
// per-pixel, no fixed stride) so this cannot vectorise on PIE -- there is
// no vector gather instruction (ASM_BRIEF.md). Constants (HALFW, ENV_SCALE,
// WAVEFREQ, RAD_TO_TABLE) are the exact float32 bit patterns of this file's
// own constexpr values -- computed with a throwaway C program, not by hand,
// specifically to avoid a silent single-bit rounding mismatch; see the
// derivation note kept in the report for this pass.
//
// CPENABLE: this kernel must NEVER write CPENABLE itself. ESP-IDF's Xtensa
// port clears CPENABLE on every context switch and re-enables a coprocessor
// lazily through the CP-disabled exception -- and that exception handler is
// also where the port saves the outgoing owner's f0-f15 into its TCB and
// records the new task as owner, same mechanism SleepAnimation.cpp's
// scale565Oct notes for PIE/CP3. A task that sets CPENABLE itself (as an
// earlier version of this kernel did, "movi a4,0xff; wsr.cpenable a4") skips
// that handler entirely: the FPU register file may still hold a DIFFERENT
// task's live state (e.g. Controller::loopLogic's PID math on core 0, which
// also uses float), and the self-enable neither saves that state out nor
// records itself as the new owner. This kernel then computes into registers
// that still belong to the other task, and when that task resumes, the port
// -- believing nothing changed -- restores nothing, so it silently reads
// this kernel's leftover float garbage instead of its own state. No crash,
// no fault, just corrupted PID math. The correct, only-safe path is to let
// the first f-register instruction below (wfr f0, ...) fault naturally and
// let the port's lazy mechanism do the save/restore/ownership bookkeeping,
// exactly as scale565Oct already does for PIE. tools/qemubench's bare-metal
// harness has no such port (no OS at all, so no lazy enable and no other
// task to corrupt) -- that harness sets CPENABLE once itself, in its own
// main(), outside any kernel body; see tests/anim_ripples/main.cpp.
__attribute__((noinline)) static void accumulateBandAsm(float *hAccBuf, const RowBand &b, const float *cosTable,
                                                         const float *envLUT) {
    float *bufPtr = hAccBuf + b.x0;
    int n = b.x1 - b.x0 + 1;
    if (n <= 0) {
        return;
    }
    int curDx = b.curDx, curDist2 = b.curDist2, curR = b.curR, curR2 = b.curR2;
    uint32_t ampBits, rBits;
    __builtin_memcpy(&ampBits, &b.amp, sizeof(ampBits));
    __builtin_memcpy(&rBits, &b.r, sizeof(rBits));
    int s0 = 0, s1 = 0;
    asm volatile(
        // No CPENABLE write here -- see the comment above this function:
        // this instruction is deliberately the first FPU use, so it faults
        // and the OS's lazy coprocessor-enable path (save/restore/ownership)
        // runs as it must.
        "wfr f0, %[amp]\n" // f0 = amp (constant for the whole call)
        "wfr f1, %[rr]\n"  // f1 = r   (constant for the whole call)
        // f2 = HALFW (13.0f), f3 = ENV_SCALE (255.0f/13.0f), f4 = WAVEFREQ
        // (6.2831853f/27.0f), f5 = RAD_TO_TABLE (256.0f/6.2831853f) --
        // materialised once, not per pixel.
        "movi %[s0], 0x41500000\n"
        "wfr f2, %[s0]\n"
        "movi %[s0], 0x419cec4f\n"
        "wfr f3, %[s0]\n"
        "movi %[s0], 0x3e6e4bae\n"
        "wfr f4, %[s0]\n"
        "movi %[s0], 0x4222f983\n"
        "wfr f5, %[s0]\n"
        "1:\n"
        // hAccBuf[x] is loaded first and used last (in add.s, at the very
        // bottom of this block): lsi has multi-cycle load latency, and
        // hoisting the load here gives it the whole envLUT/cosTable gather
        // chain below to land in before anything touches f12 again. Moved
        // here from right before add.s (its natural, unscheduled spot) --
        // that placement stalled every pixel on the load with nothing
        // independent left to hide it behind. bufPtr (%[buf]) is not
        // written until after the store below, so reading it this early
        // changes nothing about the value seen.
        "lsi f12, %[buf], 0\n"     // f12 = hAccBuf[x] (current)
        "float.s f6, %[curR], 0\n" // f6 = (float)curR
        "sub.s f7, f6, f1\n"       // f7 = delta = curR - r
        "abs.s f8, f7\n"           // f8 = ad = fabsf(delta)
        "ole.s b0, f8, f2\n"       // b0 = (ad <= HALFW)
        "bf b0, 2f\n"              // skip the contribution when not
        "mul.s f9, f8, f3\n"       // f9 = ad * ENV_SCALE
        "trunc.s %[s0], f9, 0\n"   // s0 = (int)(ad*ENV_SCALE); ad>=0 so trunc==floor
        "addx4 %[s1], %[s0], %[envp]\n"
        "lsi f10, %[s1], 0\n"      // f10 = envLUT[idxEnv]
        "mul.s f9, f7, f4\n"       // f9 = rad = delta * WAVEFREQ (reuses f9)
        "mul.s f9, f9, f5\n"       // f9 = rad * RAD_TO_TABLE
        "trunc.s %[s0], f9, 0\n"   // s0 = (int)(rad*RAD_TO_TABLE), truncated toward zero exactly as
                                    // the C (int) cast this replaces
        "extui %[s0], %[s0], 0, 8\n" // s0 &= 255 -- extui takes the low 8 bits of the two's-complement
                                      // value, bit-identical to C's `& 255` for a negative int too
        "addx4 %[s1], %[s0], %[cosp]\n"
        "lsi f11, %[s1], 0\n"      // f11 = cosTable[idxCos]
        "mul.s f9, f0, f11\n"      // f9 = amp * cosv (reuses f9 again)
        "mul.s f9, f9, f10\n"      // f9 = amp * cosv * env
        "add.s f12, f12, f9\n"     // f12 (loaded above) + contribution
        "ssi f12, %[buf], 0\n"     // hAccBuf[x] = old + contribution
        "2:\n"
        // Tracker step: dist2 += 2*dx+1, dx += 1 (pure int, no libm, no
        // PIE -- see file header, "Distance field" section).
        "slli %[s0], %[curDx], 1\n"
        "addi %[s0], %[s0], 1\n"
        "add %[curDist2], %[curDist2], %[s0]\n"
        "addi %[curDx], %[curDx], 1\n"
        "3:\n" // while (curDist2 >= curR2 + 2*curR + 1) { curR2 += ...; curR++; }
        "slli %[s0], %[curR], 1\n"
        "addi %[s0], %[s0], 1\n"
        "add %[s1], %[curR2], %[s0]\n"
        "blt %[curDist2], %[s1], 4f\n"
        "mov %[curR2], %[s1]\n"
        "addi %[curR], %[curR], 1\n"
        "j 3b\n"
        "4:\n" // while (curDist2 < curR2) { curR2 -= 2*curR-1; curR--; }
        "bge %[curDist2], %[curR2], 5f\n"
        "slli %[s0], %[curR], 1\n"
        "addi %[s0], %[s0], -1\n"
        "sub %[curR2], %[curR2], %[s0]\n"
        "addi %[curR], %[curR], -1\n"
        "j 4b\n"
        "5:\n"
        "addi %[buf], %[buf], 4\n" // advance hAccBuf pointer by one float
        "addi %[n], %[n], -1\n"
        "bnez %[n], 1b\n"
        : [buf] "+r"(bufPtr), [n] "+r"(n), [curDx] "+r"(curDx), [curDist2] "+r"(curDist2), [curR] "+r"(curR),
          [curR2] "+r"(curR2), [s0] "+r"(s0), [s1] "+r"(s1)
        : [amp] "r"(ampBits), [rr] "r"(rBits), [envp] "r"(envLUT), [cosp] "r"(cosTable)
        : "memory", "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12", "b0");
}

#endif // __XTENSA__ && !GM_BGANIM_NO_ASM

// Portable row[x0..x1) = tile[x&3] fill: bandRef()'s version of Kernel A,
// and band()'s fallback wherever fillTileSpanPie is unavailable.
void fillTileSpanRef(uint16_t *row, int x0, int x1, const uint16_t tile[4]) {
    for (int x = x0; x < x1; x++) {
        row[x] = tile[x & 3];
    }
}

// g_hAccBuf (declared above, near envLUT/clampU8): one row's worth of
// pre-summed ring height, shared scratch for both band() and bandRef()
// (they are never called concurrently -- one render task, one call at a
// time). Sized to the largest w this fleet renders (480, full resolution;
// half resolution uses the first 240 entries). Read once per pixel by
// blendPackSpan and written once per accumulated pixel by
// accumulateBandRef/Asm, so it is exactly the kind of per-pixel traffic the
// hot slab exists for (BgAnimCommon.h's GM_BGANIM_HOT_SLAB comment) --
// allocHot()'d from init(), not a plain static array as an earlier version
// of this file had it: that static array cost 1,920 B of permanent .bss
// outside the fleet's tracked SRAM budget regardless of whether Ripples was
// even the resident animation, where the slab gives the same SRAM
// placement back to the pool via release() the moment another animation
// takes over.

// Shared final pass: blend the accumulated ring height into the base color
// and pack to RGB565. Portable, identical code for band() and bandRef() --
// leaves the two unable to disagree about how a filled hAccBuf gets turned
// into pixels, only about how hAccBuf got filled. Not hand-written asm (see
// file header for why this one was left as compiler-optimized C++).
void blendPackSpan(uint16_t *row, int x0, int x1, float baseR, float baseG, float baseB, const float *hAccBuf,
                   const float dith4[4]) {
    for (int x = x0; x < x1; x++) {
        float cr = baseR, cg = baseG, cb = baseB;
        const float hAcc = hAccBuf[x];
        if (hAcc != 0) {
            const float g = hAcc * g_glow;
            if (g > 0) {
                cr += g * crestF[0];
                cg += g * crestF[1];
                cb += g * crestF[2];
            } else {
                cr += g * troughF[0];
                cg += g * troughF[1];
                cb += g * troughF[2];
            }
        }
        const float dith = dith4[x & 3];
        row[x] = rgb565(clampU8[static_cast<int>(cr + dith) + CLAMP_PAD], clampU8[static_cast<int>(cg + dith) + CLAMP_PAD],
                         clampU8[static_cast<int>(cb + dith) + CLAMP_PAD]);
    }
}

// The portable spec: bandRef() is what the host bench runs against golden
// frames, and what SleepAnimation::runAnimTest (/api/debug/animtest)
// compares band()'s asm kernels against on the real device. See file header
// for why this restructuring (band-outer accumulate into g_hAccBuf, then a
// single blend/pack pass) is bit-exact with the pre-2026-09-04 single-pass
// version, not merely equivalent.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int yy = 0; yy < rows; yy++) {
        const int y = y0 + yy;
        RowState rs;
        buildRowState(y, w, rs);
        uint16_t *row = dst + static_cast<size_t>(yy) * w;
        if (rs.nrb == 0) {
            fillTileSpanRef(row, 0, w, rs.tile);
            continue;
        }
        memset(g_hAccBuf, 0, sizeof(float) * static_cast<size_t>(w));
        for (int i = 0; i < rs.nrb; i++) {
            accumulateBandRef(g_hAccBuf, rs.rb[i], g_cosTable, envLUT);
        }
        int x = 0;
        for (int s = 0; s < rs.nSpans; s++) {
            fillTileSpanRef(row, x, rs.spanX0[s], rs.tile);
            blendPackSpan(row, rs.spanX0[s], rs.spanX1[s] + 1, rs.baseR, rs.baseG, rs.baseB, g_hAccBuf, rs.dith4);
            x = rs.spanX1[s] + 1;
        }
        fillTileSpanRef(row, x, w, rs.tile);
    }
}

// Dispatches to the hand-written Xtensa kernels on device; falls back to
// bandRef() verbatim everywhere else (host bench, GM_BGANIM_NO_ASM builds).
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    for (int yy = 0; yy < rows; yy++) {
        const int y = y0 + yy;
        RowState rs;
        buildRowState(y, w, rs);
        uint16_t *row = dst + static_cast<size_t>(yy) * w;
        if (rs.nrb == 0) {
            fillTileSpanPie(row, 0, w, rs.tile);
            continue;
        }
        memset(g_hAccBuf, 0, sizeof(float) * static_cast<size_t>(w));
        for (int i = 0; i < rs.nrb; i++) {
            accumulateBandAsm(g_hAccBuf, rs.rb[i], g_cosTable, envLUT);
        }
        int x = 0;
        for (int s = 0; s < rs.nSpans; s++) {
            fillTileSpanPie(row, x, rs.spanX0[s], rs.tile);
            blendPackSpan(row, rs.spanX0[s], rs.spanX1[s] + 1, rs.baseR, rs.baseG, rs.baseB, g_hAccBuf, rs.dith4);
            x = rs.spanX1[s] + 1;
        }
        fillTileSpanPie(row, x, w, rs.tile);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(envLUT, 256 * sizeof(float));
    releaseTable(clampU8, CLAMP_SIZE);
    releaseTable(g_hAccBuf, 480 * sizeof(float));
    // Borrowed: cosTableF() is owned and shared by BgAnimCommon.
    g_cosTable = nullptr;
    // Forces frame() to rebuild the crest/trough colors on the next init.
    // They live in plain statics rather than an allocation, so this is
    // belt-and-braces rather than load-bearing — but a stale sentinel next to
    // a freed table set is the bug class this entry point exists to prevent.
    lastThemeGen = 0xFFFFFFFF;
    // `inited` is deliberately NOT reset. It gates the ripple SIMULATION
    // state (active flags, nextDropMs and primed), not any table's content:
    // envLUT and clampU8 are pure functions of compile-time constants, so
    // init() refills them identically whatever `inited` says. Leaving the
    // simulation intact across a release/init cycle means a short absence
    // resumes the same pond, and a long one is caught up by frame() the
    // same way the first frame is.
}

} // namespace

extern const BgAnimation bg_anim_ripples;
const BgAnimation bg_anim_ripples = {
    "ripples",
    "Ripples",
    {{"speed", "Ring speed", 50},
     {"rate", "Drop rate", 40},
     {"decay", "Fade", 50},
     {"glow", "Glow", 50},
     {"spread", "Drop spread", 50},
     {"width", "Ring width", 50},
     {"tone", "Water tone", 50},
     {"trough", "Trough dip", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
