#ifndef GAGGIMATE_SIM

// "Silk" — three slowly rotating plane waves interfere into a moiré sheen,
// contrast-curved and vignetted. Phase kept as a wrapping uint32 turn
// accumulator (DDS style): per pixel = 3 LUT reads + 3 adds.
//
// Row and temporal phase are ALSO plain Q32 turn accumulators now (same
// trick AnimPlasma.cpp uses for its "phase"/"cycle" fields): a uint32_t
// wraps mod 2^32 for free on every add/multiply, and one turn == 2^32, so
// wraparound IS the mod-2*pi reduction. That replaces the previous
// per-row `fmodf(ky*y + wt, 2*pi)` (1440 fmodf/frame, the whole cost of
// this animation) with a per-row integer add and a per-band-call integer
// multiply — zero libm in band(). The only float->int casts left are of
// small, frame-scoped magnitudes (kx/ky/wRate * TURN, all comfortably
// inside int32 range — see report for the bound), never of an unbounded
// growing phase, so there's no float->uint32 UB risk.
//
// The per-pixel TAIL (contrast curve -> vignette -> dither -> palette) is
// now integer fixed-point too, after two prior passes got band() to
// ~0.31ms host / ~41 xtensa insns/pixel but stalled there: real-hardware
// disassembly showed the tail's ~6 float ops (contrastLUT load, subtract,
// clamp compare, multiply-add, trunc.s) forced 3 stack spill/reloads per
// pixel (contrastLUT ptr, g_step[2], paletteLUT ptr) even though only a
// single pixel was live per iteration — the float file plus the turn
// accumulators (a/b/c) plus their steps overflowed the ~13-14 AR registers
// the windowed call ABI leaves available, and a 4x manual unroll to hide
// that latency was tried and measured WORSE (see band()'s comment) because
// it needed even more simultaneous live values and killed the hardware
// zero-overhead LOOP entirely.
//
// Fix: move contrast and vignette to Q8 fixed point (value*256) so the tail
// runs entirely in the AR (integer) register file, which is both larger
// pressure-wise here (no float file contention) and cheaper per op (mull is
// 2 cycles vs multiple for madd.s+trunc.s):
//   - contrastLUT stores nc*256 (nc is always in [0,255], so this fits
//     uint16_t, 0..65280 — built once per frame in buildContrastLUT()).
//   - the vignette column term is stored as vignK*dx*dx*256 (Q8, always
//     >= 0), g_dx2Row[] as of the fifth pass, see its declaration below.
//   - envRowBase is computed once per ROW (not per pixel) as
//     round((1 - vignK*dy*dy) * 256) — a float->int cast here costs
//     nothing, it happens `rows` times per band() call, not w*rows times.
//   - band()'s tail is then: env_q8 = envRowBase_q8 - dx2_q8 (env is
//     PROVABLY non-negative for any square panel — see below — so the old
//     "if (env<0) env=0" branch is dropped, not just made branchless);
//     idxq = nc_q8 * env_q8 + dith_q16 (one 32x32 mull, Q8*Q8=Q16); idx =
//     idxq >> 16 (one arithmetic shift, replacing trunc.s + min + max).
//   - the dither term is stored pre-scaled to Q16 (dith*65536) rather than
//     Q8, so it can be added directly to the Q16 product before the single
//     final shift — this keeps sub-integer dither precision (dith's real
//     range is only ~[-0.8,+0.7], so rounding it to a plain integer first,
//     before combining with the product, would collapse all 16 BAYER4
//     dither levels down to just 2-3 buckets and flatten the banding
//     texture the dither exists to hide).
//
// Non-negative env proof (why the clamp branch is gone): for ANY square
// panel (w==h — true of every display driver in this tree: 480x480 and
// 466x466), R = w/2 and vignK = 0.32/R^2, so vignK*dx^2 and vignK*dy^2 are
// each in [0, 0.32] regardless of resolution (dx,dy max out at R, so
// vignK*R^2 == 0.32 exactly). envRowBase = 1 - vignK*dy^2 is therefore in
// [0.68, 1.0], and env = envRowBase - vignK*dx^2 is in [0.68-0.32, 1.0] =
// [0.36, 1.0] — always positive. In Q8 that's env_q8 in [~92, 256], with
// zero real-world path to negative. (If a non-square panel is ever added,
// this bound breaks; the fix would be to reinstate a one-instruction
// `if (env_q8 < 0) env_q8 = 0;` guard, matching AnimEmber.cpp's precedent
// of trusting an analytic proof scoped to the supported panel shapes.)
//
// Final index range and the padded-palette trick (same pattern as
// AnimEmber.cpp's paletteExt): nc_q8 in [0, 65280] (nc in [0,255] exactly,
// since powf(x,e) for x in [0,1] never leaves [0,1]) and env_q8 in [0, 256]
// (envRowBase <= 1 always, dx2 >= 0 always, so env <= 1 always, clamped to
// >= 0 as the compile-time-safe floor). Their product is therefore in
// [0, 255<<16] exactly (65280*256 == 255*65536). dith_q16's real range is
// [-0.796875, +0.697265625] (from BAYER4 in [(0,0.5*)) -> Q16 [-52224,
// +45696]). Summing and shifting right 16 (floor) gives idxq>>16 in
// [floor(-52224/65536), floor((255*65536+45696)/65536)] = [-1, 255] — a
// provably tiny 1-step underflow and zero overflow. PAD=4 below covers that
// with comfortable margin for the rounding choices made when building the
// Q8/Q16 tables (round-to-nearest, not truncation), so band()'s final
// lookup is a single unclamped, unbranched paletteExt[PAD+idx] read.
//
// Third pass (this one): one exact per-pixel constant-fold-out, plus a
// coarse-grid interpolation of the wave field.
//   - the dither table now has PALETTE_REAL_OFF<<16 baked in at build time, so
//     band()'s final `idxq >> 16` is already g_lut's absolute palette
//     index — no per-pixel `addmi PALETTE_REAL_OFF`. The folding is
//     algebraically exact (arithmetic right shift distributes over adding an
//     exact multiple of the shift base), not an approximation.
//   - band() now evaluates the exact 3-LUT-read sine sum only once every
//     SILK_GRID (8) pixels and linearly interpolates the contrastLUT index
//     in between via a Q8 fixed-point ramp (one add + one shift per pixel)
//     — the field is spatially smooth enough at this animation's fringe
//     densities that the interpolation error is far below what
//     contrastLUT's 3073-point resolution or the golden-frame comparison
//     can distinguish. See band()'s comment for the exactness-at-grid-nodes
//     argument and the overflow bound (checked across the full parameter
//     range, not just defaults). Vignette and dither stay full-resolution
//     (read per pixel exactly as before), only the slowly-varying
//     interference term is coarsened.
//
// Tried and reverted in the same pass: a PRIVATE, pre-biased copy of the
// shared sine table (each entry +SIN_AMP) so the 3-wave sum would already be
// the contrastLUT index, saving the `+1536` addmi. Reverted because the
// coarse grid above already cut that addmi's frequency by 8x (once per
// SILK_GRID pixels, not every pixel), so the saving was marginal against a
// real, permanent SRAM cost, doubly true now that every internal-SRAM byte
// is an explicit, budgeted allocHot() request rather than a hope against a
// shared pool (see the fifth-pass note above and the g_dx2Row/g_ditherQ
// declarations below for the current numbers). Borrowing the shared table is
// strictly better here.
// Fourth pass: per-cell PRODUCT ramp. The third pass still paid, per pixel,
// a contrast-LUT gather, two per-pixel loads (dx2, dith), a subtract and a
// 32x32 mull. This pass moves ALL of that to the grid nodes: each cell
// computes the full Q16 product P = nc_q8 * env_q8 at its two endpoints and
// linearly ramps P across the cell with the same exact-closure trick the
// s-ramp already used (seed Pcur<<3, step Pnext-Pcur, 8 steps land exactly
// on Pnext<<3). The 4-periodic Bayer dither (the only remaining per-pixel
// table term) becomes four per-row register constants pre-shifted into the
// ramp's Q19, so the pixel body collapses to add + shift + palette gather +
// store + ramp add — no loads except the palette read, no multiply.
//   - Exact at every node column: ((P + d)<<3)>>19 == (P + d)>>16 for the
//     in-range values here, so pixel 0 of every cell is bit-identical to
//     the third-pass (slow-path) value — fast and slow cells never seam.
//   - The contrast curve is now applied at nodes only and CHORDED between
//     them, where the third pass applied it at full resolution along the
//     interpolated s. Cells where that chord would visibly deviate fall
//     back to the exact per-pixel path, gated by a direct curvature probe:
//     one extra contrast gather at the cell's s-midpoint, and if
//     |2*lut(mid) - lut(a) - lut(b)| > SILK_BOW_TOL the cell goes exact.
//     Probing in OUTPUT space is what makes this safe across the whole
//     glow range — the curve's exponent spans [0.6, 2.6], so it has both a
//     steep convex bright end (glow high) and an infinite-second-derivative
//     concave dark end (glow low, near s=0); an input-space |ds| threshold
//     can't see the second kind, a chord probe catches both.
//   - The vignette's contribution to the endpoint products comes from
//     g_dx2Node (the dx2 column term at node columns only, incl. x==w which
//     g_dx2Row can't supply, being sized to w not w+1), quantized identically
//     to g_dx2Row[].
//     Linearizing env across a cell adds at most vignK*4096 =~ 0.024 of a
//     Q8 env unit of interior error (env is quadratic in x; max lerp error
//     over h=8 is f''*h^2/8 = 2*vignK*256*64/8) — three orders of magnitude
//     under one palette step.
//   - Ranges/overflow: interior P is a convex combination of two node
//     products, both in [0, 255<<16], so idx stays in [-1, 255] exactly as
//     proven above (dither unchanged). PQ = P<<3 <= 255<<19 < 2^27.
//     dq = dith<<3 <= (3328*65536 + 45696)*8 < 1.75e9, PQ + dq < 1.88e9 <
//     2^31 — no int32 overflow anywhere in the fast path.
//   - band() is now also IRAM-pinned (GM_ANIM_IRAM), AnimEmber.cpp's
//     precedent: the S3 has one 16 KB flash icache shared by both cores,
//     LVGL churns it from core 1, and refills queue on the MSPI bus behind
//     the panel's PSRAM scan-out stream.
//
// Fifth pass: hand-written Xtensa kernels for the two per-cell inner bodies
// (see the __XTENSA__ block below), plus two changes forced by the device
// numbers rather than the host bench, which said the fourth pass was already
// a win:
//   - Table placement moved from "whatever alloc() finds free in the internal
//     pool at init() time" (nondeterministic boot to boot once the pool idles
//     within a few kB of its floor) to bganim::allocHot(), a fixed 9,216 B
//     slab budgeted at build time. rowAux's old per-phase array-of-structs
//     (4 copies x w entries x 8 B = 15,360 B at w=480) wasted 3/4 of its
//     bytes: dx2 never varied by phase, only dith did, and dith only ever
//     takes 16 distinct values. It is now g_dx2Row[w] (uint8_t, ~480 B) plus
//     g_ditherQ[16] (int32_t, 64 B) -- see their declarations below for the
//     range arguments that make the narrower types exact, not approximate.
//   - bandRef() is IRAM-pinned again. It was dropped in the fourth pass on
//     the assumption that bandRef() only ran from an occasional debug call;
//     the device numbers instead measured it as a sustained render-loop
//     candidate (SleepAnimation's /api/debug/anim?useref=1), where flash
//     residency matters exactly as much as it does for band() -- see
//     bandRef()'s own comment.
//   - silkExactCell8Asm's per-pixel schedule now leaves two independent
//     instructions between `mull` and the add that consumes its result,
//     matching what GCC 14 itself does in the equivalent compiled loop
//     (bandRef's EXACT-cell body); the fourth-pass kernel left zero, which
//     the device numbers show costing more than the kernel's lower
//     instruction count bought back. See the kernel's own comment for the
//     schedule and the report for the GCC comparison that found it.
//
// Design: anim-fluid (Fable), 2026-08-15. Optimized: anim-fluid, 2026-08-15;
// opt-silk2 (fixed-point tail), 2026-08-15; opt-silk3 (constant folding +
// coarse-grid interpolation), 2026-08-17; opt-silk4 (per-cell product ramp +
// IRAM pin), 2026-08-31; opt-silk5 (Xtensa kernels, allocHot placement,
// mull scheduling), 2026-09-04.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

// Same pattern as AnimEmber.cpp: keep band() out of flash so LVGL's icache
// churn on the other core can't stall it behind MSPI refills (see file
// header). No-op on the host bench.
#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

// Master switch for the hand-written Xtensa kernels below (silkFastCell8Asm,
// silkExactCell8Asm, and band()'s dispatch to them). OFF by default: on the
// device the kernels lost to GCC 14's compile of bandRef() in all three
// passes that tried them (2026-09-04, production band time per full-res
// frame: round 2 asm 22.5 ms vs ref 17.3, round 3 asm 25.4 vs ref 18.6,
// with the same tables in the same places), while bandRef() itself with the
// per-pixel tables in the hot slab is the fastest silk has measured (HEAD
// needed 23.8 KB of SRAM for 18.2 ms). The kernels stay in the file, bit-exact
// (QEMU test tools/qemubench/tests/anim_silk), for whoever finds the stall;
// -DGM_BGANIM_SILK_ASM=1 re-enables them. Only the device settles it.
#ifndef GM_BGANIM_SILK_ASM
#define GM_BGANIM_SILK_ASM 0
#endif

#ifdef GM_SILK_HOST_DIFF
// Test hooks for the host differ (tools/animbench silk_diff builds this file
// with -DGM_SILK_HOST_DIFF; neither the firmware, the bench timing build,
// nor xtensa-asm.sh define it, so none of them carry the per-cell check or
// the counters below). g_silkForceExact forces every grid cell down the
// exact per-pixel path, which is bit-identical to the pre-pass-4 algorithm;
// the differ renders with and without it to bound the fast path's error
// across the full parameter space, not just the goldens' default params.
// External linkage on purpose.
bool g_silkForceExact = false;
unsigned long long g_silkFastCells = 0;
unsigned long long g_silkSlowCells = 0;
#endif

namespace {
using namespace bganim;

struct SilkWave {
    float A0, rotMult, wMult, wk, phk, kx, ky;
};
SilkWave wave[3] = {
    {0.20f, 0.6f, 1.00f, 6.2831853f / 71000.0f, 0.4f, 0, 0},
    {2.15f, -1.0f, 1.37f, 6.2831853f / 95000.0f, 2.1f, 0, 0},
    {4.35f, 1.4f, 0.71f, 6.2831853f / 123000.0f, 4.0f, 0, 0},
};

// contrastLUT is indexed DIRECTLY by (s + 1536), s being the raw 3-wave sine
// sum (range -1536..1536, 3073 values) — no more scaling s down to a 0..255
// index first. That skips both the "*255/3072" reduction (which the
// compiler was already turning into a 64-bit magic-number multiply, several
// instructions in the hot loop) and the int->float conversion of the old
// uint8_t table. Entries are stored as Q8 fixed point (nc*256, nc always in
// [0,255] so this fits uint16_t) rather than float — see file header.
constexpr int CONTRAST_N = 3073; // 2*1536 + 1
// Palette is stored "padded" like AnimEmber.cpp's paletteExt: PAD clamp
// entries on each side of the real 256-entry ramp so the (rare, 1-step)
// out-of-range fixed-point index lands on a valid clamped entry with no
// branch — see the file-header proof for the exact [-1,255] bound.
constexpr int PAD = 4;
constexpr int PAL_EXT_N = 256 + 2 * PAD;
// contrastLUT and the padded palette are ONE allocation (contrast curve
// first, palette immediately after) so band()'s hot loop only ever needs a
// SINGLE loop-invariant base pointer (g_lut) instead of two. Both offsets
// into it (s+1536 for the contrast curve, PALETTE_REAL_OFF+idx for the
// palette) are compile-time constants, so the compiler folds them into the
// existing immediate-add addressing rather than needing a second live
// pointer register — freeing up exactly the register that was forcing
// contrastLUT and paletteExt to each spill/reload every pixel.
//
// PALETTE_REAL_OFF is deliberately rounded UP to a multiple of 256, not just
// placed right after the contrast curve. Xtensa's `addmi` instruction adds
// an immediate that's a multiple of 256 in a single cycle with no spare
// register (that's exactly how "s + 1536" above compiles — 1536 = 6*256).
// A non-256-aligned offset like 3077 can't be folded that way, so the
// compiler was materializing it into an extra always-live register instead
// — which simply moved the spill from the old two-pointers case onto
// g_step[1]/g_step[2] rather than eliminating it (confirmed via
// xtensa-asm.sh: same 2 stack reloads per pixel, just a different victim).
// Rounding up wastes a few hundred bytes of unused gap between the contrast
// curve and the palette (never read, since the two index ranges don't
// overlap) in exchange for a single-instruction, zero-register palette
// offset — see band()'s use of PALETTE_REAL_OFF for the payoff.
constexpr int PALETTE_REAL_OFF = ((CONTRAST_N + PAD + 255) / 256) * 256; // palette[0], 256-aligned
constexpr int PALETTE_OFF = PALETTE_REAL_OFF - PAD; // paletteExt[0] lives here in g_lut
constexpr int LUT_N = PALETTE_OFF + PAL_EXT_N;
uint16_t *g_lut = nullptr;      // [LUT_N]: contrast curve, then padded palette
uint16_t *contrastLUT = nullptr; // = g_lut, alias for readability outside band()
uint16_t *paletteExt = nullptr;  // = g_lut + PALETTE_OFF, alias for readability
uint16_t *palette = nullptr;     // = g_lut + PALETTE_REAL_OFF, 256 entries
// Dither LUT folds the BAYER4 centering and rescale into a single lookup so
// band() spends one array read instead of two subtracts and two multiplies
// per pixel. Built as float here, then re-quantized to Q16 into g_ditherQ[].
// Rebuilt whenever the palette is (theme, brightness, knee), because the
// amplitude is derived from the palette's step spacing.
float ditherLUT[16];
// Fifth pass (allocHot budget, see file header): the old rowAux[phase][x]
// array-of-structs merged two per-pixel terms behind one walking pointer -
//   - dx2LUT[x]        = (x - cx)^2 * g_vignK, row-invariant vignette term
//   - ditherRow[y&3][x] = ditherLUT[(y&3)*4 + (x&3)] expanded to full width
//, duplicated 4x (once per y&3 phase) purely so both terms hung off a
// single incrementing pointer instead of two independent ones (see
// xtensa-asm.sh history below for why that pointer-count constraint existed
// at all). That cost 15,360 B at w=480, 3/4 of it reproducing a term
// (dx2) that never varied by phase in the first place, against a slab that
// now only has 9,216 B total. Split back into what each term actually is:
//   - g_dx2Row[x]: ONE copy, not four. dx2 depends only on x, so there is
//     nothing to duplicate. Q8, range [0, ~82] for any x on a panel this
//     size (see the file header's env proof: vignK*dx^2 <= 0.32*256 ==
//     81.92 for |dx| up to the panel radius), so uint8_t holds it exactly -
//     not a precision cut, a range fit. ~480 B at w=480, versus 4*480*4 =
//     7,680 B for the dx2 half of the old table alone.
//   - g_ditherQ[16]: the dither term only ever takes 16 distinct values
//     (4 y-phases x 4 x-phases, from BAYER4), so storing it once per pixel
//     column was always 30x more entries than the value actually has.
//     Indexed (y&3)*4+(x&3), same Q16-plus-PALETTE_REAL_OFF-bias encoding
//     RowAux.dith used, see below for why Q16 and why the bias lives here.
// The two walking-pointer constraint that justified the AoS shape in the
// first place no longer applies: band()'s asm kernels take explicit
// pointer/register arguments instead of asking GCC to keep an
// induction-variable count under some threshold, so there is nothing left
// for a merged struct to buy. g_ditherQ is small enough (64 B, 16 entries)
// that the per-cell/per-row code below loads its 4 relevant phase values
// into registers once rather than walking it at all, see band()'s dqArr
// and silkExactCell8Asm's d0..d3 comment.
uint8_t *g_dx2Row = nullptr;
int g_dx2RowW = 0; // width g_dx2Row was sized for
// Q16: dither value * 65536, PLUS PALETTE_REAL_OFF<<16 baked in (see file
// header for why Q16, not Q8, and band()'s comment for why the offset lives
// here). Folding the constant palette-array offset into this table (built
// once, not per pixel) means band()'s final `idxq >> 16` is already
// g_lut's absolute index, dropping the `addmi PALETTE_REAL_OFF` that would
// otherwise run on every pixel. This is exact, not approximate: for any
// integer X and multiple-of-2^16 bias k*65536, (X + k*65536) >> 16 ==
// (X >> 16) + k under arithmetic right shift (what C++ does for signed idxq
// here, same as the pre-existing reliance on arithmetic shift for negative
// dith), so this changes nothing about which palette entry is picked, in
// any case including the underflow corner (old idx==-1 -> new
// idx==PALETTE_REAL_OFF-1, still inside paletteExt's padding).
//
// Heap-allocated (allocHot(), not a plain static array) even though its
// size never changes: BgAnimCommon.h is explicit that a table forced
// internal by being `static` bypasses the slab's budget entirely --
// "Static BSS is not yours to spend" -- so this goes through the same
// allocHot()-with-alloc()-fallback path as g_dx2Row and g_dx2Node below,
// not a compile-time array, even though 64 B would never itself be the
// difference between fitting the slab or not.
int32_t *g_ditherQ = nullptr;
uint32_t lastThemeGen = 0xFFFFFFFF;
int lastGlow = -1;
// Vignette column term (same value and quantization as g_dx2Row[]) at the
// GRID-NODE columns only: x = 0, SILK_GRID, ..., cellsFull*SILK_GRID — note
// the last entry can be x == w, one past what g_dx2Row holds, because it is
// the ramp target of the final full cell. (w>>SILK_GRID_SHIFT)+1 uint8_t
// entries, ~61 B at w=480 (was int32_t, ~244 B, same range argument as
// g_dx2Row above applies here unchanged). Matching g_dx2Row's quantization
// exactly is what makes the fast path's node pixels bit-identical to the
// slow path's.
uint8_t *g_dx2Node = nullptr;
int g_dx2NodeN = 0;
float g_invR2 = 1.0f;
float g_vignK = 0.32f; // 0.32f * g_invR2, folded so band() does one multiply instead of two
int32_t g_step[3];    // per-pixel x-phase step, Q32 turns/px
int32_t g_bigStep[3]; // = g_step * SILK_GRID: per-cell x-phase step for the
                       // coarse-grid interpolation in band() (see there);
                       // computed once per frame() call alongside g_step,
                       // not per band()/row/pixel.
int32_t g_rowStep[3]; // per-row y-phase step, Q32 turns/row
uint32_t g_wtTurn[3]; // temporal phase at y=0, Q32 turns (already mod 2*pi via wraparound)
// Coarse-grid interpolation constants for band()'s wave-interference field
// (see band()'s comment for the full rationale and the overflow/precision
// bound). SILK_GRID must be a power of two so both the per-cell step (divide
// by SILK_GRID) and the tail-loop bound (mod SILK_GRID) reduce to shifts —
// no runtime divide is introduced.
constexpr int SILK_GRID = 8;       // coarse-grid cell width in pixels
constexpr int SILK_GRID_SHIFT = 3; // log2(SILK_GRID)
constexpr int SILK_Q_BITS = 8;     // interpolation fixed-point fractional bits
// The 3-wave sine sum is in [-1536,1536]; contrastLUT is indexed 0..3072. The
// bias is added at the GRID NODES (once per SILK_GRID pixels) rather than at
// every pixel: it cancels out of the ramp's delta, and seeding sQ from the
// already-biased node means the per-pixel `sQ >> SILK_Q_BITS` yields the
// contrastLUT index directly. That is where the pre-biased private sine table
// the header describes would have saved an instruction, and why it isn't
// needed.
constexpr int32_t SIN_SUM_BIAS = 3 * SIN_AMP; // 1536
// Fast-path gate for the fourth pass's per-cell product ramp (see band()):
// |2*lut(mid) - lut(a) - lut(b)| in Q8 contrast-output units == twice the
// chord's deviation from the curve at the cell midpoint. 512 caps the
// interior error at ~1 palette index (one index == 256 Q8 units, and env
// <= 1 only shrinks it) before a cell falls back to the exact path.
constexpr int32_t SILK_BOW_TOL = 512;
// sinLut() lives in BgAnimCommon.cpp (a different translation unit — this
// build has no LTO), so calling it from the pixel loop is a real, un-inlined
// function call with a lazy-init branch, 3x/pixel = 691200 calls/frame. That
// call overhead was the actual dominant cost of this animation, not the
// fmodf (removing fmodf alone barely moved host time). Cache the pointer
// once at init and index it directly in band() instead.
//
// Borrowed, not owned: BgAnimCommon holds the one shared copy and every other
// animation reads the same unbiased table. Pre-biasing a private copy was
// tried and reverted — see the file header for why (2 KB over the SRAM
// ceiling, saving an addmi the coarse grid already made 8x rarer). Entries
// are in [-512,512] (SIN_AMP), so the sum of three reads is in [-1536,1536]
// and `s + 1536` covers contrastLUT's [0, CONTRAST_N-1] index domain exactly.
const int16_t *g_sinLut = nullptr;

// idx spans 0..CONTRAST_N-1 (== s+1536, s being the raw sine sum): this is
// the same gamma curve as before (powf(n255/255,e)*255 where n255 was s
// rescaled to 0..255) but applied directly at 3073-point resolution instead
// of quantizing to 256 levels first, so it's at least as accurate, not less.
void buildContrastLUT(uint8_t glow) {
    const float e = 0.6f + 2.0f * (glow / 100.0f);
    for (int idx = 0; idx < CONTRAST_N; idx++) {
        const float nc = powf(idx / static_cast<float>(CONTRAST_N - 1), e) * 255.0f;
        // Q8: nc is always in [0,255] (powf(x,e) never leaves [0,1] for
        // x,e >= 0), so nc*256 always fits uint16_t (max 65280).
        contrastLUT[idx] = static_cast<uint16_t>(lroundf(nc * 256.0f));
    }
}

// Fills the clamp padding around the freshly-rebuilt 256-entry ramp so
// paletteExt[PAD + idx] is valid for idx in [-PAD, 255+PAD] with no branch
// (same trick as AnimEmber.cpp's extendPalette — see file header for the
// proof that idx only ever reaches [-1, 255] here).
void extendPalette() {
    const uint16_t lo = palette[0];
    const uint16_t hi = palette[255];
    for (int i = 0; i < PAD; i++) {
        paletteExt[i] = lo;
        paletteExt[PAD + 256 + i] = hi;
    }
}

// Amplitude comes from the palette, not a constant: it is half the spacing
// between the ramp's RGB565 steps, so the dither cell spans exactly one step.
// The old fixed 255/160 was +-0.8 index units, tuned against silk's own
// fixed-point index quantization rather than the panel's, which is why the
// ramp still showed hard contours -- 54.7% of disc pixels sat on a monotone
// <=1 LSB staircase at brightness 100. Deriving it drops that to 3.4%.
void buildDitherLUT() {
    const float amp = ditherAmp(palette, 256);
    for (int k = 0; k < 16; k++) {
        ditherLUT[k] = (static_cast<float>(BAYER4[k]) - 7.5f) * (amp / 7.5f);
    }
}

// Re-quantize g_ditherQ[]. Only reached on a theme/tone change, which
// already rebuilds a 256-entry ramp and the contrast LUT, so 16 stores on
// top of that are not worth optimizing. This is also the ONLY place a hot
// table's *contents* change after its first build, see BgAnimCommon.h's
// release() comment on why that has to be an in-place rebuild rather than a
// release+reallocate (releasing anything but the most-recently-allocated hot
// table doesn't reclaim the slab until every hot table is released, so a
// free+realloc here would either leak slab space or require releasing and
// rebuilding every hot table on every theme change). g_ditherQ already
// satisfied this before the allocHot move (it was always rebuilt in place),
// so no behavior changes.
void refreshDither() {
    buildDitherLUT();
    for (int k = 0; k < 16; k++) {
        g_ditherQ[k] = static_cast<int32_t>(lroundf(ditherLUT[k] * 65536.0f)) + (PALETTE_REAL_OFF << 16);
    }
}

bool init(int w, int h) {
    g_sinLut = sinLut();
    if (g_sinLut == nullptr) {
        return false;
    }
    if (g_lut == nullptr) {
        // Highest allocHot priority: up to 3 gathers per pixel on the
        // exact/tail paths (contrast, midpoint probe, palette) and 1-2 per
        // CELL on the fast path (node contrast, node/mid probes), by far
        // the most-read table here. allocHot() falls back to nullptr when
        // the 9,216 B slab is full (not expected at this table's size, see
        // the report), so alloc() (PSRAM) is the explicit fallback rather
        // than a failed init().
        g_lut = static_cast<uint16_t *>(allocHot(LUT_N * sizeof(uint16_t)));
        if (g_lut == nullptr) {
            g_lut = static_cast<uint16_t *>(alloc(LUT_N * sizeof(uint16_t)));
        }
        if (g_lut != nullptr) {
            contrastLUT = g_lut;
            paletteExt = g_lut + PALETTE_OFF;
            palette = g_lut + PALETTE_REAL_OFF;
        }
    }
    if (g_lut == nullptr) {
        return false;
    }
    const float R = (w < h ? w : h) * 0.5f;
    g_invR2 = 1.0f / (R * R);
    g_vignK = 0.32f * g_invR2;
    if (lastGlow < 0) {
        buildThemeRamp(palette, 256);
        extendPalette();
        lastThemeGen = themeGen();
        buildContrastLUT(55);
        lastGlow = 55;
        buildDitherLUT();
    }

    // g_dx2Row: one allocation, not four, dx2 never varied by phase, so the
    // old per-phase retry loop (and its four independent null checks) is
    // gone along with RowAux.
    if (g_dx2Row == nullptr) {
        const float cx = w * 0.5f;
        g_dx2Row = static_cast<uint8_t *>(allocHot(static_cast<size_t>(w) * sizeof(uint8_t)));
        if (g_dx2Row == nullptr) {
            g_dx2Row = static_cast<uint8_t *>(alloc(static_cast<size_t>(w) * sizeof(uint8_t)));
        }
        if (g_dx2Row == nullptr) {
            return false;
        }
        g_dx2RowW = w;
        for (int x = 0; x < w; x++) {
            const float dx = x - cx;
            // Pre-scaled by g_vignK and quantized to Q8 so band()'s
            // per-pixel vignette work is a single integer subtract -
            // env_q8 = envRowBase_q8 - g_dx2Row[x], instead of a load +
            // add(dy2) + multiply(vignK) + subtract, all in float. Range
            // [0, ~82] (see g_dx2Row's declaration) fits uint8_t exactly.
            g_dx2Row[x] = static_cast<uint8_t>(lroundf(g_vignK * dx * dx * 256.0f));
        }
    }
    // g_ditherQ: independent allocation and null check from g_dx2Row above
    // (not bundled into that guard), a transient allocHot()/alloc() failure
    // on one must not permanently skip retrying the other on the next
    // init(), same reasoning the original RowAux per-phase loop documented.
    // Built here exactly where the old rowAux[ph].dith fill ran on first
    // allocation; refreshDither() (called from frame() on a later theme
    // change) rebuilds it again in place when the palette moves.
    if (g_ditherQ == nullptr) {
        g_ditherQ = static_cast<int32_t *>(allocHot(16 * sizeof(int32_t)));
        if (g_ditherQ == nullptr) {
            g_ditherQ = static_cast<int32_t *>(alloc(16 * sizeof(int32_t)));
        }
        if (g_ditherQ == nullptr) {
            return false;
        }
        buildDitherLUT();
        for (int k = 0; k < 16; k++) {
            // Q16 (not Q8): dith's real magnitude is < 1, so rounding it to
            // a plain integer here would collapse all 16 dither levels into
            // 2-3 buckets, see file header.
            g_ditherQ[k] = static_cast<int32_t>(lroundf(ditherLUT[k] * 65536.0f)) + (PALETTE_REAL_OFF << 16);
        }
    }
    // Node-column vignette table for the fourth pass's product ramp (see
    // declaration). Null-checked like g_dx2Row so a transient allocation
    // failure is retried on the next init() rather than latched.
    if (g_dx2Node == nullptr) {
        const float cx = w * 0.5f;
        const int n = (w >> SILK_GRID_SHIFT) + 1;
        g_dx2Node = static_cast<uint8_t *>(allocHot(static_cast<size_t>(n) * sizeof(uint8_t)));
        if (g_dx2Node == nullptr) {
            g_dx2Node = static_cast<uint8_t *>(alloc(static_cast<size_t>(n) * sizeof(uint8_t)));
        }
        if (g_dx2Node == nullptr) {
            return false;
        }
        g_dx2NodeN = n;
        for (int j = 0; j < n; j++) {
            const float dx = j * SILK_GRID - cx;
            // Identical rounding to g_dx2Row[] above, required for the
            // node-column bit-exactness argument in the file header.
            g_dx2Node[j] = static_cast<uint8_t>(lroundf(g_vignK * dx * dx * 256.0f));
        }
    }
    return true;
}

// Phase accumulator: full uint32 range = one turn; the shared 1024-entry sine
// LUT is indexed by the top 10 bits. Indexes g_sinLut directly (see comment
// above) rather than calling sinLut() per pixel.
inline int16_t sinFromTurn(uint32_t turn) { return g_sinLut[turn >> 22]; }
constexpr float TURN = 4294967296.0f / 6.2831853f;

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    const float omega0 = 6.2831853f / 55000.0f * speedMul(p[0]); // 55s base drift at speed 50
    const float k0 = 0.008f + 0.022f * (p[1] / 100.0f);
    if (themeGen() != lastThemeGen) {
        buildThemeRamp(palette, 256);
        extendPalette();
        lastThemeGen = themeGen();
        refreshDither(); // step spacing moved with the new palette
    }
    if (p[2] != lastGlow) {
        buildContrastLUT(p[2]);
        lastGlow = p[2];
    }
    for (int i = 0; i < 3; i++) {
        SilkWave &wv = wave[i];
        const float A = wv.A0 + (omega0 * 0.15f * wv.rotMult) * tMs;
        const float k = k0 * (1.0f + 0.15f * fastSinRad(wv.wk * tMs + wv.phk));
        wv.kx = k * fastCosRad(A);
        wv.ky = k * fastSinRad(A);
        g_step[i] = static_cast<int32_t>(wv.kx * TURN);
        g_bigStep[i] = g_step[i] * SILK_GRID; // see g_bigStep declaration
        g_rowStep[i] = static_cast<int32_t>(wv.ky * TURN);
        // Temporal phase as a 64-bit Q32 product: wRateQ (turns/ms) times
        // tMs (ms) wraps mod 2^32 exactly like mod-one-turn, so the result
        // is already phase-mod-2*pi with no fmodf and no risk of the old
        // float->uint32 overflow (wRate*tMs as a float would blow way past
        // uint32 range once tMs runs into the minutes/hours).
        const float wRate = omega0 * wv.wMult; // rad/ms
        const int32_t wRateQ = static_cast<int32_t>(wRate * TURN); // turns/ms, Q32
        g_wtTurn[i] = static_cast<uint32_t>(static_cast<int64_t>(wRateQ) * static_cast<int64_t>(tMs));
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM) && GM_BGANIM_SILK_ASM
// ---- Hand-written Xtensa kernels for band()'s per-cell inner loops -------
//
// PIE (the S3's 128-bit vector coprocessor) does not help either loop below:
// both are dominated by a LUT GATHER (g_lut[idx], idx data-dependent per
// pixel), and there is no vector gather instruction on this hardware -- see
// tools/animbench/ASM_BRIEF.md's PIE facts. So both kernels are
// hand-scheduled SCALAR Xtensa, fully unrolled (SILK_GRID == 8 is a
// compile-time constant here, not a runtime loop bound), which sidesteps
// the register-pressure problem that sank a compiler-driven 4x unroll of
// this same loop (see bandRef()'s "NOTE" comment below) -- an asm block
// controls its own register allocation instead of asking GCC's windowed-ABI
// allocator to find ~20 simultaneous live values in a ~13-14-register
// budget. Every load's result here is consumed at least one instruction
// after the load, so neither kernel pays the 1-cycle load-use stall
// ASM_BRIEF.md's scalar facts describe.
//
// Both are noinline and take plain pointers/ints (the brief's requirement)
// so the exact same bodies drop into the QEMU test at
// tools/qemubench/tests/anim_silk/ unchanged -- transcribed there by hand,
// not regenerated, so a QEMU PASS is evidence about this literal sequence.
//
// IRAM-pinned like band() itself: band() runs from IRAM specifically so
// LVGL's icache churn on core 1 can't stall it behind an MSPI refill (see
// the GM_ANIM_IRAM comment above), and calling out to a flash-resident
// helper from an IRAM function would reopen exactly that stall on every
// cell -- so these two callees must stay in IRAM too.

// silkFastCell8Asm: the dominant per-cell path (see band()'s "FAST cell"
// call below) -- ramps the precomputed Q16 nc*env product P linearly across
// 8 pixels and gathers ONE palette entry per pixel. No contrast gather here;
// that is the entire point of the pass-4 product ramp this mirrors.
//
//   out:  8 consecutive uint16_t outputs to fill (== out+x in band())
//   PQ0:  Pcur << SILK_GRID_SHIFT (Q19) -- the ramp's value at pixel 0
//   dP:   Pnext - Pcur (Q16) -- the ramp's per-pixel step
//   dq:   4-entry table, dq[k] applies to pixel k%4 (already
//         <<SILK_GRID_SHIFT and PALETTE_REAL_OFF-biased -- see g_ditherQ's
//         declaration above), so (PQ + dq[k%4]) >> 19 is g_lut's absolute
//         index with no separate offset add (matches the scalar reference's
//         `g_lut[(PQ+dqK)>>19]` exactly).
//   lut:  g_lut base.
//
// Pixel pairs are packed into one 32-bit store (SleepAnimation.cpp's
// scale565Oct precedent): `out` is the band row buffer, 4-byte aligned, and
// x is always a multiple of SILK_GRID (8) here, so every pair starts on an
// even pixel -- one aligned s32i instead of two s16i.
//
// PQ advances by dP seven times total (matching the scalar reference's
// seven "PQ += dP" -- there is no increment after pixel 7); the four dq
// values are each used twice (period 4 over 8 pixels).
GM_ANIM_IRAM __attribute__((noinline)) void silkFastCell8Asm(uint16_t *out, int32_t PQ0, int32_t dP,
                                                               const int32_t *dq, const uint16_t *lut) {
    int32_t pq = PQ0;
    const int32_t dq0v = dq[0];
    const int32_t dq1v = dq[1];
    const int32_t dq2v = dq[2];
    const int32_t dq3v = dq[3];
    int32_t t0, t1;
    asm volatile("add    %[t0], %[pq], %[dq0]\n" // pixels 0,1
                 "srai   %[t0], %[t0], 19\n"
                 "add    %[pq], %[pq], %[dp]\n"
                 "add    %[t1], %[pq], %[dq1]\n"
                 "srai   %[t1], %[t1], 19\n"
                 "addx2  %[t0], %[t0], %[lut]\n"
                 "addx2  %[t1], %[t1], %[lut]\n"
                 "l16ui  %[t0], %[t0], 0\n"
                 "l16ui  %[t1], %[t1], 0\n"
                 "add    %[pq], %[pq], %[dp]\n"
                 "slli   %[t1], %[t1], 16\n"
                 "or     %[t0], %[t0], %[t1]\n"
                 "s32i   %[t0], %[out], 0\n"
                 "add    %[t0], %[pq], %[dq2]\n" // pixels 2,3
                 "srai   %[t0], %[t0], 19\n"
                 "add    %[pq], %[pq], %[dp]\n"
                 "add    %[t1], %[pq], %[dq3]\n"
                 "srai   %[t1], %[t1], 19\n"
                 "addx2  %[t0], %[t0], %[lut]\n"
                 "addx2  %[t1], %[t1], %[lut]\n"
                 "l16ui  %[t0], %[t0], 0\n"
                 "l16ui  %[t1], %[t1], 0\n"
                 "add    %[pq], %[pq], %[dp]\n"
                 "slli   %[t1], %[t1], 16\n"
                 "or     %[t0], %[t0], %[t1]\n"
                 "s32i   %[t0], %[out], 4\n"
                 "add    %[t0], %[pq], %[dq0]\n" // pixels 4,5
                 "srai   %[t0], %[t0], 19\n"
                 "add    %[pq], %[pq], %[dp]\n"
                 "add    %[t1], %[pq], %[dq1]\n"
                 "srai   %[t1], %[t1], 19\n"
                 "addx2  %[t0], %[t0], %[lut]\n"
                 "addx2  %[t1], %[t1], %[lut]\n"
                 "l16ui  %[t0], %[t0], 0\n"
                 "l16ui  %[t1], %[t1], 0\n"
                 "add    %[pq], %[pq], %[dp]\n"
                 "slli   %[t1], %[t1], 16\n"
                 "or     %[t0], %[t0], %[t1]\n"
                 "s32i   %[t0], %[out], 8\n"
                 "add    %[t0], %[pq], %[dq2]\n" // pixels 6,7
                 "srai   %[t0], %[t0], 19\n"
                 "add    %[pq], %[pq], %[dp]\n"
                 "add    %[t1], %[pq], %[dq3]\n"
                 "srai   %[t1], %[t1], 19\n"
                 "addx2  %[t0], %[t0], %[lut]\n"
                 "addx2  %[t1], %[t1], %[lut]\n"
                 "l16ui  %[t0], %[t0], 0\n"
                 "l16ui  %[t1], %[t1], 0\n"
                 "slli   %[t1], %[t1], 16\n"
                 "or     %[t0], %[t0], %[t1]\n"
                 "s32i   %[t0], %[out], 12\n"
                 : [pq] "+r"(pq), [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [dp] "r"(dP), [dq0] "r"(dq0v), [dq1] "r"(dq1v), [dq2] "r"(dq2v), [dq3] "r"(dq3v),
                   [lut] "r"(lut), [out] "r"(out)
                 : "memory");
}

// silkExactCell8Asm: the curvature-fallback path (see band()'s "EXACT cell"
// call below) -- ramps the interpolated contrast index s (Q8) across 8
// pixels and evaluates the full per-pixel expression: TWO gathers (contrast,
// palette) plus a 32x32 multiply, against the fast kernel's one gather. This
// path is rare (only cells where the curvature probe trips) but not
// provably negligible at parameter extremes (glow 0 or 100 sharpen the
// contrast curve enough that more cells fail the probe), so it gets the
// same scheduling care, not a leftover scalar loop.
//
//   out:       8 consecutive uint16_t outputs to fill.
//   sQ0:       sCur << SILK_Q_BITS (Q8) -- the s-ramp's value at pixel 0.
//   stepQ:     the s-ramp's per-pixel step.
//   envBase:   envRowBase_q8 for this row (Q8).
//   dx2AtX:    &g_dx2Row[x] -- one uint8_t per pixel, walked forward one
//              byte per iteration.
//   ditherRow: &g_ditherQ[yph*4] -- the caller folds the row's y-phase
//              offset into the pointer once, so this kernel only ever
//              indexes it by the pixel's x-phase (0..3, cycling); Q16,
//              PALETTE_REAL_OFF-biased, unshifted (unlike the fast kernel's
//              dq[], this feeds a Q16 mull result directly, not a Q19
//              ramp).
//   lut:       g_lut base.
//
// Round 3 rewrite: rounds 1 and 2 both fully unrolled this into 8 flat
// copies of the pixel body (110-113 static instructions) on the theory that
// an asm block controls its own register allocation, so unrolling can't
// spill the way it did when band()'s NOTE above describes GCC's compiler
// -driven 4x unroll losing the hardware LOOP. Round 2's device numbers
// showed that theory was incomplete: the unrolled kernel still lost to
// GCC's OWN compile of the identical algorithm by about a quarter, despite
// matching or beating it on instruction count and matching its mull
// -to-consumer gap. The missed variable was never register pressure -- it
// was code size. Comparing this function's disassembly against GCC's
// compiled bandRef() (tools/animbench/xtensa-asm14.sh AnimSilk, read the
// .S) found that GCC does NOT unroll the EXACT cell's `for (i=0;i<8;i++)`
// loop at all: it keeps it as a genuine hardware zero-overhead LOOP over an
// 18-instruction body, the same body executed 8 times, roughly 60 bytes of
// code. Round 2's unrolled kernel put 303 bytes of straight-line code in
// IRAM for the same work. IRAM has no instruction cache: every one of those
// 303 bytes is fetched fresh on every single call, where a flash-resident
// loop that fits the 16 KB icache only pays that cost on a miss. A LOOP
// -bodied kernel that stays IRAM-resident (see GM_ANIM_IRAM above -- IRAM
// still buys freedom from LVGL's icache churn, this isn't reverting that)
// gets the code-size benefit of the loop AND keeps the flash-icache
// -contention immunity, instead of paying for neither: this is the
// structural fix, not a scheduling tweak. Rewritten to match GCC's shape --
// a real `loop` instruction over a compact per-pixel body, transcribed
// instruction-for-instruction from GCC's own compiled EXACT-cell loop, not
// re-derived by hand -- rather than attempt a third round of manual
// unrolled scheduling.
//
// kctr (0..7, only the low 2 bits used) stands in for GCC's own `x & 3`
// phase computation: x is always a multiple of SILK_GRID (8, itself a
// multiple of 4) at every call site, so pixel k's true x&3 == k&3, and a
// fresh 0-based counter's low 2 bits equal the real x's low 2 bits exactly
// -- same argument silkFastCell8Asm's dq[] cycling already relies on, just
// computed at runtime here (via EXTUI, matching GCC's own instruction
// choice) instead of unrolled at compile time, since there is no unrolled
// compile time left to fold it into.
//
// mull's result has ONE independent instruction before the add that
// consumes it (the sq ramp-step add) -- not two. Round 2 targeted a
// two-instruction gap based on a comparison against GCC's PRE-round-2
// compiled output (the old RowAux table layout); with the current
// g_dx2Row/g_ditherQ layout, GCC's OWN fresh compile of the same algorithm
// only manages a one-instruction gap (the extra addx4/l32i work to walk
// g_ditherQ leaves less independent work available to fill the gap with),
// confirmed by direct inspection this round. Matching GCC's actual, current
// schedule -- not a target inferred from a stale comparison -- is the
// entire point of transcribing it directly instead of re-deriving it.
GM_ANIM_IRAM __attribute__((noinline)) void silkExactCell8Asm(uint16_t *out, int32_t sQ0, int32_t stepQ,
                                                                int32_t envBase, const uint8_t *dx2AtX,
                                                                const int32_t *ditherRow, const uint16_t *lut) {
    int32_t sq = sQ0;
    const uint8_t *dx2p = dx2AtX;
    uint16_t *outp = out;
    int32_t kctr = 0;
    int32_t u, v1, ph;
    asm volatile("movi   %[u], 8\n"
                 "loop   %[u], 1f\n"
                 "l8ui   %[v1], %[dx2p], 0\n"   // dx2
                 "srai   %[u], %[sq], 8\n"      // s = sq >> 8
                 "addx2  %[u], %[u], %[lut]\n"  // &lut[s]
                 "extui  %[ph], %[kctr], 0, 2\n" // k & 3
                 "l16ui  %[u], %[u], 0\n"       // nc_q8 = lut[s]
                 "addx4  %[ph], %[ph], %[ditherrow]\n" // &ditherRow[k&3]
                 "sub    %[v1], %[envb], %[v1]\n" // env_q8 = envBase - dx2
                 "l32i   %[ph], %[ph], 0\n"     // dith = ditherRow[k&3]
                 "mull   %[u], %[u], %[v1]\n"   // nc_q8 * env_q8
                 "add    %[sq], %[sq], %[stepq]\n" // sq += stepQ (mull-gap filler)
                 "add    %[u], %[u], %[ph]\n"   // idxq = mull_result + dith
                 "srai   %[u], %[u], 16\n"      // idx
                 "addx2  %[u], %[u], %[lut]\n"  // &lut[idx]
                 "addi   %[dx2p], %[dx2p], 1\n" // dx2p++
                 "l16ui  %[u], %[u], 0\n"       // palette = lut[idx]
                 "addi   %[kctr], %[kctr], 1\n" // kctr++
                 "s16i   %[u], %[outp], 0\n"    // out[x] = palette
                 "addi   %[outp], %[outp], 2\n" // outp++
                 "1:\n"
                 : [sq] "+r"(sq), [dx2p] "+r"(dx2p), [outp] "+r"(outp), [kctr] "+r"(kctr), [u] "=&r"(u),
                   [v1] "=&r"(v1), [ph] "=&r"(ph)
                 : [stepq] "r"(stepQ), [envb] "r"(envBase), [lut] "r"(lut), [ditherrow] "r"(ditherRow)
                 : "memory");
}
#endif // __XTENSA__ && !GM_BGANIM_NO_ASM && GM_BGANIM_SILK_ASM

// bandRef(): the portable, pixel-exact reference implementation (this
// file's spec -- see BgAnim.h's bandRef field comment). Used directly as
// band() on non-Xtensa builds (host bench, the GM_SILK_HOST_DIFF differ)
// and always as bandRef() so SleepAnimation::runAnimTest
// (/api/debug/animtest) can compare it against the hand-written Xtensa
// kernels in band() below, on the real chip.
//
// IRAM-pinned (fifth pass): dropped in the fourth pass on the assumption
// that this only ran from an occasional debug call. The device numbers
// disproved that -- SleepAnimation's /api/debug/anim?useref=1 measures
// bandRef() as a sustained, live render-loop candidate, not an occasional
// probe, so it pays the same flash-icache-contention cost band() does (see
// the GM_ANIM_IRAM comment above) whenever it is the active path. Measured
// 15% slower than HEAD with this pin missing despite being otherwise
// byte-identical to HEAD's algorithm -- see the report.
GM_ANIM_IRAM void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const float cy = w * 0.5f;
    // Seed each wave's row-phase at y0, then step by g_rowStep per row
    // (plain uint32 add — wraps mod 2*pi for free, replacing the old
    // per-row fmodf).
    uint32_t base[3];
    for (int i = 0; i < 3; i++) {
        base[i] = g_wtTurn[i] + static_cast<uint32_t>(g_rowStep[i]) * static_cast<uint32_t>(y0);
    }
    // NOTE: a real 4x manual unroll (computing s0..s3 / e0..e3 / i0..i3
    // before consuming any of them, to make the dither term and the store
    // a compile-time-constant lane) was tried and measured WORSE on real
    // Xtensa despite looking better on the host x86 bench: only ~13-14 AR
    // registers are live-in-window here (windowed ABI), and that unroll
    // needed ~20+ simultaneous live values (4x each of s/e/i plus
    // a/b/c/steps/4 LUT ptrs), so GCC spilled almost everything to the
    // stack every iteration and the loop lost its zero-overhead LOOP
    // instruction entirely (confirmed via xtensa-asm.sh: went from a
    // `loop` + ~48 insns/pixel to a plain `bne`-branch loop with ~75
    // insns/pixel of spill/reload traffic). That was a manual unroll of the
    // FLAT per-pixel loop (4 copies of a/b/c/LUT-reads live at once, more
    // registers needed, same total work). The SILK_GRID=8 inner loop added
    // below is a DIFFERENT kind of change — it does LESS total work per
    // pixel (one add + one shift, replacing three LUT reads), not the same
    // work four times over — so it doesn't reintroduce the register
    // pressure that sank the unroll. Don't conflate the two if revisiting
    // this loop. (The asm pass's silkFastCell8Asm/silkExactCell8Asm above
    // sidestep this whole problem by controlling register allocation
    // directly instead of asking GCC to find it.)
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const float dy = y - cy;
        // envRowBase is computed once per ROW, not per pixel, so a float
        // here costs nothing (rows times per band() call, not w*rows) — see
        // file header for the Q8 scheme and the non-negative-env proof.
        // g_dx2Row[] is pre-scaled by g_vignK (also Q8), so folding the
        // row's dy contribution in here collapses the per-pixel vignette
        // math down to a single integer subtract.
        // Manual "+0.5 then truncate" round instead of lroundf(): this runs
        // once per row (every band() call touches `rows` rows), and
        // envRowBase is provably always in [0.68, 1.0] (never negative), so
        // round-half-away-from-zero collapses to round-half-up — no libm
        // call needed, keeping band() itself at zero libm calls.
        const int32_t envRowBase_q8 = static_cast<int32_t>((1.0f - g_vignK * dy * dy) * 256.0f + 0.5f);
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        // Dither y-phase for this row -- g_ditherQ is indexed (y&3)*4+(x&3),
        // g_dx2Row by x alone (dx2 never varied by phase, see its
        // declaration), so there is no per-row table SELECT left to do, only
        // this one phase index to carry into the loop below.
        const int yph = y & 3;
        // --- Coarse-grid interpolation of the wave-interference field ---
        // `s` (the 3-wave sine sum that indexes contrastLUT) is spatially
        // SMOOTH at the fringe densities this animation uses (k0 in
        // [0.008,0.03] turns/px-equivalent, see frame()): its curvature over
        // a handful of pixels is far below what the eye or the golden-frame
        // comparison can resolve. So instead of the exact 3-LUT-read/2-add
        // evaluation at EVERY pixel, evaluate it exactly once per SILK_GRID
        // pixels ("grid nodes") and fill the pixels in between with a Q8
        // linear ramp, stepped with one add + one shift per pixel — the
        // brief's "coarse-grid + integer bilinear upsample" technique. Grid
        // nodes are exact (no drift accumulates node-to-node, since each
        // cell's ramp is reseeded from the next exact node, not from the
        // previous cell's interpolated end). Only vignette/dither stay
        // full-resolution (g_dx2Row[x]/g_ditherQ[], read per pixel as
        // before), only the slowly-varying sine-interference term is
        // coarsened.
        //
        // Correctness of the ramp: over SILK_GRID=8 steps of size stepQ,
        // sQ advances from sCur<<Q_BITS to exactly sCur<<Q_BITS +
        // GRID*stepQ = sCur<<Q_BITS + (sNext-sCur)<<Q_BITS = sNext<<Q_BITS
        // (GRID << (Q_BITS-GRID_SHIFT) == 1 << Q_BITS for any power-of-two
        // GRID) — i.e. the ramp closes EXACTLY on the next node with no
        // rounding drift, and (being a monotonic linear ramp between two
        // in-range endpoints) every intermediate sample stays inside
        // [sCur,sNext] (or [sNext,sCur]), so it can never index contrastLUT
        // outside [0, CONTRAST_N-1] — verified, not assumed, since sCur/sNext
        // are provably in [0,3072] at every node (the sine sum is in
        // [-1536,1536] and SIN_SUM_BIAS is added at the node).
        //
        // Overflow bound (checked for the full p[0]/p[1] range, not just
        // defaults): the biased node value is in [0,3072] always, so
        // |sNext-sCur| <= 3072 in the most adversarial case; stepQ = delta <<
        // (Q_BITS-GRID_SHIFT) then maxes out around 3072*32 ~= 98304, and sQ
        // across a whole cell tops out around 1.6M — both trivially inside
        // int32 range regardless of speed/scale or how long the device has
        // been up.
        //
        // Width isn't always a multiple of SILK_GRID (466 and 480 are both
        // supported panel sizes; 466 % 8 == 2), so the trailing <SILK_GRID
        // pixels at the row's right edge fall back to the exact per-pixel
        // path below — at most 7 pixels/row, not a per-band()-call cost.
        uint32_t a = base[0], b = base[1], c = base[2];
        // Biased once per node, not per pixel (see SIN_SUM_BIAS).
        int32_t sCur = SIN_SUM_BIAS + sinFromTurn(a) + sinFromTurn(b) + sinFromTurn(c); // exact node @ x=0
        int32_t ncCur = g_lut[sCur];                            // Q8 contrast at the node
        int32_t Pcur = ncCur * (envRowBase_q8 - g_dx2Node[0]);  // Q16 nc*env product at the node
        // --- Fourth pass: per-cell PRODUCT ramp (see file header) ---
        // The Bayer dither has period 4 in x and every cell starts at
        // x % 8 == 0, so within ANY cell the dither values cycle phases
        // 0,1,2,3,0,1,2,3 from the cell start. g_ditherQ[yph*4+0..3] is
        // exactly those four values, pre-biased by PALETTE_REAL_OFF<<16.
        // Shift them into the ramp's Q19 once per ROW and they live in
        // registers for the whole row: the fast path reads no per-pixel
        // tables at all except the palette itself.
        const int32_t dq0 = g_ditherQ[yph * 4 + 0] << SILK_GRID_SHIFT;
        const int32_t dq1 = g_ditherQ[yph * 4 + 1] << SILK_GRID_SHIFT;
        const int32_t dq2 = g_ditherQ[yph * 4 + 2] << SILK_GRID_SHIFT;
        const int32_t dq3 = g_ditherQ[yph * 4 + 3] << SILK_GRID_SHIFT;
        const int cellsFull = w >> SILK_GRID_SHIFT;
        int x = 0;
        for (int cell = 0; cell < cellsFull; cell++) {
            a += g_bigStep[0];
            b += g_bigStep[1];
            c += g_bigStep[2];
            const int32_t sNext = SIN_SUM_BIAS + sinFromTurn(a) + sinFromTurn(b) + sinFromTurn(c); // exact next node
            const int32_t ncNext = g_lut[sNext];
            const int32_t Pnext = ncNext * (envRowBase_q8 - g_dx2Node[cell + 1]);
            // Curvature probe: one extra contrast gather at the cell's
            // s-midpoint. bow == twice the chord-vs-curve deviation there,
            // in Q8 output units. Probing OUTPUT space (not |ds|) is what
            // keeps this valid at both exponent extremes of the contrast
            // curve — see the file-header discussion of glow 0 vs 100.
            const int32_t ncMid = g_lut[(sCur + sNext) >> 1];
            int32_t bow = 2 * ncMid - (ncCur + ncNext);
            bow = bow < 0 ? -bow : bow;
#ifdef GM_SILK_HOST_DIFF
            if (g_silkForceExact) {
                bow = SILK_BOW_TOL + 1; // force the exact path (differ hook)
            }
#endif
            if (bow <= SILK_BOW_TOL) {
#ifdef GM_SILK_HOST_DIFF
                g_silkFastCells++;
#endif
                // FAST cell: ramp the Q16 product P from Pcur to Pnext in
                // Q19 (<< SILK_GRID_SHIFT). Exact closure, same argument as
                // the old s-ramp: 8 steps of (Pnext-Pcur) from Pcur<<3 land
                // on Pnext<<3 exactly. Pixel 0 telescopes to the slow
                // path's node value bit-for-bit: ((P + d) << 3) >> 19 ==
                // (P + d) >> 16 (the <<3 is exact — see the header overflow
                // bound — and arithmetic shifts compose), so fast and slow
                // cells never seam.
                //
                // This IS an 8x unroll, but not the one band()'s NOTE above
                // warns about: that flat unroll replicated the whole 3-LUT
                // pixel body (~20+ simultaneous live values, spilled, lost
                // the hardware LOOP). This body keeps ~10 values live (PQ,
                // dP, dq0-3, out, g_lut, temps) — inside the ~13-14 AR
                // budget. Verified via xtensa-asm.sh; re-verify if touched.
                int32_t PQ = Pcur << SILK_GRID_SHIFT;
                const int32_t dP = Pnext - Pcur;
                uint16_t *const o = out + x;
                o[0] = g_lut[(PQ + dq0) >> (16 + SILK_GRID_SHIFT)];
                PQ += dP;
                o[1] = g_lut[(PQ + dq1) >> (16 + SILK_GRID_SHIFT)];
                PQ += dP;
                o[2] = g_lut[(PQ + dq2) >> (16 + SILK_GRID_SHIFT)];
                PQ += dP;
                o[3] = g_lut[(PQ + dq3) >> (16 + SILK_GRID_SHIFT)];
                PQ += dP;
                o[4] = g_lut[(PQ + dq0) >> (16 + SILK_GRID_SHIFT)];
                PQ += dP;
                o[5] = g_lut[(PQ + dq1) >> (16 + SILK_GRID_SHIFT)];
                PQ += dP;
                o[6] = g_lut[(PQ + dq2) >> (16 + SILK_GRID_SHIFT)];
                PQ += dP;
                o[7] = g_lut[(PQ + dq3) >> (16 + SILK_GRID_SHIFT)];
                x += SILK_GRID;
            } else {
#ifdef GM_SILK_HOST_DIFF
                g_silkSlowCells++;
#endif
                // EXACT cell (the pre-pass-4 body, verbatim): the contrast
                // chord would deviate visibly here, so apply the curve at
                // full resolution along the interpolated s.
                const int32_t stepQ = (sNext - sCur) << (SILK_Q_BITS - SILK_GRID_SHIFT);
                int32_t sQ = sCur << SILK_Q_BITS;
                for (int i = 0; i < SILK_GRID; i++) {
                    const int32_t s = sQ >> SILK_Q_BITS;
                    // g_lut[s] (contrast curve) and g_lut[idx] (palette,
                    // offset folded into g_ditherQ[], see its declaration)
                    // are ONE walking pointer with two precomputed offsets,
                    // not two separate pointers — see the
                    // g_lut/contrastLUT/paletteExt comment above.
                    const int32_t nc_q8 = g_lut[s]; // already the biased index, Q8 (nc*256)
                    // env_q8 in [0,256] always (see file header proof); no
                    // clamp needed for any square panel (all current
                    // display drivers are square).
                    const int32_t env_q8 = envRowBase_q8 - g_dx2Row[x];
                    // nc_q8 (Q8) * env_q8 (Q8) = Q16; g_ditherQ[] is Q16 AND
                    // pre-biased by PALETTE_REAL_OFF<<16 (see its
                    // declaration), so one add combines them and one
                    // arithmetic shift recovers g_lut's ABSOLUTE palette
                    // index.
                    const int32_t idxq = nc_q8 * env_q8 + g_ditherQ[yph * 4 + (x & 3)];
                    const int idx = static_cast<int>(idxq >> 16);
                    out[x] = g_lut[idx];
                    sQ += stepQ;
                    x++;
                }
            }
            sCur = sNext;
            ncCur = ncNext;
            Pcur = Pnext;
        }
        // Exact tail for the row's non-grid-aligned remainder (0..7 pixels):
        // same per-pixel math band() used everywhere before interpolation
        // existed. `a,b,c` already sit at the last grid node's phase (== x
        // here), so this just resumes the ORIGINAL per-pixel g_step.
        for (; x < w; x++) {
            const int32_t s = SIN_SUM_BIAS + sinFromTurn(a) + sinFromTurn(b) + sinFromTurn(c);
            a += g_step[0];
            b += g_step[1];
            c += g_step[2];
            const int32_t nc_q8 = g_lut[s];
            const int32_t env_q8 = envRowBase_q8 - g_dx2Row[x];
            const int32_t idxq = nc_q8 * env_q8 + g_ditherQ[yph * 4 + (x & 3)];
            const int idx = static_cast<int>(idxq >> 16);
            out[x] = g_lut[idx];
        }
        base[0] += static_cast<uint32_t>(g_rowStep[0]);
        base[1] += static_cast<uint32_t>(g_rowStep[1]);
        base[2] += static_cast<uint32_t>(g_rowStep[2]);
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM) && GM_BGANIM_SILK_ASM
// On-device band(): identical algorithm to bandRef() above (the coarse-grid
// node computation and curvature probe are unchanged C++), but the two
// per-cell inner bodies -- the 8-pixel product ramp and the 8-pixel exact
// fallback -- dispatch to the hand-written Xtensa kernels above instead of
// the scalar C++ loops. See silkFastCell8Asm/silkExactCell8Asm's own
// comments for why PIE doesn't apply here (gather-dominated, no vector
// gather on this hardware) and for the scheduling. Kept pixel-exact with
// bandRef() -- verified by tools/qemubench/tests/anim_silk/ against the
// same scalar arithmetic transcribed by hand, and by
// SleepAnimation::runAnimTest (/api/debug/animtest) against bandRef()
// itself on the real chip.
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const float cy = w * 0.5f;
    uint32_t base[3];
    for (int i = 0; i < 3; i++) {
        base[i] = g_wtTurn[i] + static_cast<uint32_t>(g_rowStep[i]) * static_cast<uint32_t>(y0);
    }
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const float dy = y - cy;
        const int32_t envRowBase_q8 = static_cast<int32_t>((1.0f - g_vignK * dy * dy) * 256.0f + 0.5f);
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        // Dither y-phase for this row -- see bandRef()'s yph comment; the
        // per-row RowAux table select this used to do is gone with it.
        const int yph = y & 3;
        uint32_t a = base[0], b = base[1], c = base[2];
        int32_t sCur = SIN_SUM_BIAS + sinFromTurn(a) + sinFromTurn(b) + sinFromTurn(c);
        int32_t ncCur = g_lut[sCur];
        int32_t Pcur = ncCur * (envRowBase_q8 - g_dx2Node[0]);
        // dqArr holds the same four per-row dither constants bandRef()
        // keeps as separate scalars -- the asm kernel needs them as an
        // array to pass a single pointer argument (see
        // silkFastCell8Asm's signature). Built once per row, like the
        // scalar dq0..dq3 it replaces.
        const int32_t dqArr[4] = {
            g_ditherQ[yph * 4 + 0] << SILK_GRID_SHIFT,
            g_ditherQ[yph * 4 + 1] << SILK_GRID_SHIFT,
            g_ditherQ[yph * 4 + 2] << SILK_GRID_SHIFT,
            g_ditherQ[yph * 4 + 3] << SILK_GRID_SHIFT,
        };
        // silkExactCell8Asm's per-row dither operand (see its comment): the
        // row's y-phase offset into g_ditherQ, folded into the pointer once
        // here rather than into 4 preloaded scalars -- the round-3 kernel
        // indexes it by the pixel's x-phase (0..3) at runtime inside its
        // loop, matching GCC's own compiled schedule, so it takes the base
        // pointer, not unshifted copies of its 4 entries.
        const int32_t *ditherRow = g_ditherQ + yph * 4;
        const int cellsFull = w >> SILK_GRID_SHIFT;
        int x = 0;
        for (int cell = 0; cell < cellsFull; cell++) {
            a += g_bigStep[0];
            b += g_bigStep[1];
            c += g_bigStep[2];
            const int32_t sNext = SIN_SUM_BIAS + sinFromTurn(a) + sinFromTurn(b) + sinFromTurn(c);
            const int32_t ncNext = g_lut[sNext];
            const int32_t Pnext = ncNext * (envRowBase_q8 - g_dx2Node[cell + 1]);
            const int32_t ncMid = g_lut[(sCur + sNext) >> 1];
            int32_t bow = 2 * ncMid - (ncCur + ncNext);
            bow = bow < 0 ? -bow : bow;
            if (bow <= SILK_BOW_TOL) {
                // FAST cell: see silkFastCell8Asm's comment above.
                silkFastCell8Asm(out + x, Pcur << SILK_GRID_SHIFT, Pnext - Pcur, dqArr, g_lut);
            } else {
                // EXACT cell: see silkExactCell8Asm's comment above.
                const int32_t stepQ = (sNext - sCur) << (SILK_Q_BITS - SILK_GRID_SHIFT);
                silkExactCell8Asm(out + x, sCur << SILK_Q_BITS, stepQ, envRowBase_q8, g_dx2Row + x, ditherRow,
                                   g_lut);
            }
            x += SILK_GRID;
            sCur = sNext;
            ncCur = ncNext;
            Pcur = Pnext;
        }
        // Exact tail for the row's non-grid-aligned remainder (0..7
        // pixels; 0 at this panel's 480/240 widths, up to 2 at 466 -- see
        // bandRef()'s comment). Small and rare enough that hand-written
        // asm is not worth it here; this is bandRef()'s tail, verbatim.
        for (; x < w; x++) {
            const int32_t s = SIN_SUM_BIAS + sinFromTurn(a) + sinFromTurn(b) + sinFromTurn(c);
            a += g_step[0];
            b += g_step[1];
            c += g_step[2];
            const int32_t nc_q8 = g_lut[s];
            const int32_t env_q8 = envRowBase_q8 - g_dx2Row[x];
            const int32_t idxq = nc_q8 * env_q8 + g_ditherQ[yph * 4 + (x & 3)];
            const int idx = static_cast<int>(idxq >> 16);
            out[x] = g_lut[idx];
        }
        base[0] += static_cast<uint32_t>(g_rowStep[0]);
        base[1] += static_cast<uint32_t>(g_rowStep[1]);
        base[2] += static_cast<uint32_t>(g_rowStep[2]);
    }
}
#else
// No Xtensa kernel to dispatch to on this build (host bench, the
// GM_SILK_HOST_DIFF differ): band() is bandRef() byte for byte, so the
// host golden comparison and the differ's fast/slow-cell counters exercise
// the same code either way.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif // __XTENSA__ && !GM_BGANIM_NO_ASM && GM_BGANIM_SILK_ASM

void release() {
    // releaseTable() works the same whether the pointer came from allocHot()
    // or alloc() (see BgAnimCommon.h) -- no bookkeeping here needs to track
    // which pool each table landed in. All four hot tables must be released
    // for the slab's bottom region to reset (see BgAnimCommon.h's release()
    // comment); release() always runs all four below unconditionally.
    releaseTable(g_lut, static_cast<size_t>(LUT_N) * sizeof(uint16_t));
    // All three point into g_lut; freeing any of them would be a double free.
    contrastLUT = nullptr;
    paletteExt = nullptr;
    palette = nullptr;
    releaseTable(g_dx2Row, static_cast<size_t>(g_dx2RowW) * sizeof(uint8_t));
    g_dx2RowW = 0;
    releaseTable(g_ditherQ, 16 * sizeof(int32_t));
    releaseTable(g_dx2Node, static_cast<size_t>(g_dx2NodeN) * sizeof(uint8_t));
    g_dx2NodeN = 0;
    // Borrowed from BgAnimCommon, which owns it and shares it fleet-wide.
    g_sinLut = nullptr;
    lastThemeGen = 0xFFFFFFFF;
    lastGlow = -1;
}

} // namespace

extern const BgAnimation bg_anim_silk;
const BgAnimation bg_anim_silk = {
    "silk",
    "Silk",
    {{"speed", "Speed", 50}, {"scale", "Fringe density", 45}, {"glow", "Sheen", 55}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
