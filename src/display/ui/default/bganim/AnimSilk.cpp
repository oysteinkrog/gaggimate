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
// [0, 255<<16] exactly (65280*256 == 255*65536). The dither amplitude comes
// from ditherAmp(): half the palette's RGB565 step spacing, capped at 16.0
// index units for a palette flat enough to have only 8 distinct steps, so
// dith_q16 is in [-16, +16] << 16 at the widest (the old fixed 255/160
// amplitude that gave the [-1, 255] bound quoted in earlier revisions of
// this comment is gone since the dither was derived from the palette).
// Summing and shifting right 16 (floor) gives idxq>>16 in [-16, 271], so
// PAD below is 16: with PAD=4 the fuzz harness under ASan read one entry
// past g_lut at a parameter set that produced a coarse palette (2026-09-04),
// which on the device is a wrong colour from whatever follows the table in
// the slab. band()'s final lookup stays a single unclamped, unbranched
// paletteExt[PAD+idx] read.
//
// Third pass (this one): one exact per-pixel constant-fold-out, plus a
// coarse-grid interpolation of the wave field.
//   - the dither table carried PALETTE_REAL_OFF<<16 baked in at build time
//     from this pass to the fifth, so band()'s final `idxq >> 16` was
//     already g_lut's absolute palette index — no per-pixel `addmi
//     PALETTE_REAL_OFF`. The folding is algebraically exact (arithmetic
//     right shift distributes over adding an exact multiple of the shift
//     base). The sixth pass moved the offset into the palette base pointer
//     instead; see SILK_GRID_SHIFT for why.
//   - band() now evaluates the exact 3-LUT-read sine sum only once every
//     SILK_GRID pixels (8 then, 16 since the sixth pass) and linearly
//     interpolates the contrastLUT index in between via a Q8 fixed-point
//     ramp (one add + one shift per pixel)
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
//     products, both in [0, 255<<16], so idx stays in [-16, 271] exactly as
//     proven above (dither unchanged). With the sixth pass's shift of 4:
//     PQ = P<<4 <= 255<<20 < 2^28, |dq| = |dith|<<4 <= 16<<20 = 2^24, so
//     |PQ + dq| < 2^29 — no int32 overflow anywhere in the fast path. (The
//     fourth pass's bound also had the palette offset inside dq, 3328<<19;
//     one more bit and that term alone passes 2^31, which is why the offset
//     now lives in the palette pointer, see SILK_GRID_SHIFT.)
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
// Sixth pass: the grid is 16 pixels and pixels are written in pairs. After
// the fifth pass the per-pixel body was already one add, one shift, one
// gather and one store, and the device measured 13.4 ms per frame; the
// kblob rig (tools/kblob) then put numbers on where the rest went. Doubling
// the grid alone gave 10.2 ms with the picture unchanged (goldens moved a
// mean of 0.14 of 255, compiler-noise territory), because the node work
// (three sine gathers, a contrast gather, the vignette product, the
// curvature probe) had been a third of the frame. Writing each pixel pair
// as one 32-bit store of one sample gave 8.3 ms; the field is sampled every
// 16 pixels anyway, so the only visible change is a dither grain two pixels
// wide (goldens moved a mean of 2.0, all of it that grain). A 32-pixel grid
// was slower (9.8 ms), the wider chord failing the curvature probe far more
// often, and the exact fallback that remains at 16 costs 0.7 ms of the 8.3.
// The palette offset left the dither constants for the palette pointer
// because the Q20 ramp cannot hold it (see SILK_GRID_SHIFT). The fifth
// pass's Xtensa kernels were left unported and gated off by a static_assert
// until the port below (2026-09-05), which is still off by default.
//
// Design: anim-fluid (Fable), 2026-08-15. Optimized: anim-fluid, 2026-08-15;
// opt-silk2 (fixed-point tail), 2026-08-15; opt-silk3 (constant folding +
// coarse-grid interpolation), 2026-08-17; opt-silk4 (per-cell product ramp +
// IRAM pin), 2026-08-31; opt-silk5 (Xtensa kernels, allocHot placement,
// mull scheduling), 2026-09-04; sixth pass (16-pixel grid, paired stores),
// 2026-09-04.

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

// Master switch for the hand-written Xtensa kernels below (silkFastCell16Asm,
// silkExactCell16Asm, and band()'s dispatch to them). OFF by default: on the
// device the fifth pass's kernels lost to GCC 14's compile of bandRef() in
// all three passes that tried them (2026-09-04, production band time per
// full-res frame: round 2 asm 22.5 ms vs ref 17.3, round 3 asm 25.4 vs ref
// 18.6, with the same tables in the same places), while bandRef() itself
// with the per-pixel tables in the hot slab is the fastest silk has
// measured (HEAD needed 23.8 KB of SRAM for 18.2 ms). The sixth pass moved
// bandRef() to a 16-pixel grid with paired stores, which those kernels did
// not follow, so a static_assert stopped this flag from building at all
// until the port was real. The port is done now (see the kernels' own
// comments below): they are bit-exact against the current bandRef() under
// QEMU (tools/qemubench/tests/anim_silk), but not yet measured on real
// hardware, since the bench board was offline when this port was written.
// -DGM_BGANIM_SILK_ASM=1 re-enables them for that measurement. The flag
// stays off until a production A/B (camshots/anim_devbench.py) shows a
// win on the device; only the device settles it, and it has said no twice
// already for the retired kernels this replaces.
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
// entries on each side of the real 256-entry ramp so an out-of-range
// fixed-point index lands on a valid clamped entry with no branch. The
// dither reaches +-16 index units at its ditherAmp() cap, so PAD is 16 (see
// the file-header bound; 4 was one short of the cap and overran under ASan).
constexpr int PAD = 16;
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
//     Indexed (y&3)*4 + ((x>>1)&3) since the sixth pass (the Bayer cell is
//     two pixels wide, see the header), Q16, see below.
// The two walking-pointer constraint that justified the AoS shape in the
// first place no longer applies: band()'s asm kernels take explicit
// pointer/register arguments instead of asking GCC to keep an
// induction-variable count under some threshold, so there is nothing left
// for a merged struct to buy. g_ditherQ is small enough (64 B, 16 entries)
// that the per-cell/per-row code below loads its 4 relevant phase values
// into registers once rather than walking it at all, see band()'s dqArr
// and silkExactCell16Asm's ditherRow comment.
uint8_t *g_dx2Row = nullptr;
int g_dx2RowW = 0; // width g_dx2Row was sized for
// Q16: dither value * 65536 (see file header for why Q16, not Q8). From the
// third pass to the fifth this table also carried PALETTE_REAL_OFF<<16 so
// the final `idxq >> 16` was g_lut's absolute index; the sixth pass's Q20
// ramp cannot hold that bias (see SILK_GRID_SHIFT), so the offset moved into
// the palette base pointer the pixel loops index (pal = g_lut +
// PALETTE_REAL_OFF), which costs one loop-invariant register and no
// per-pixel instruction. Entries are in [-16, 16] << 16.
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
// 16 since the sixth pass (was 8): the node work (three sine gathers, a
// contrast gather, the vignette product and the curvature probe) is paid
// once per cell, and at 8 it was about a third of the frame. 32 was measured
// too and lost (9.8 ms against 8.3 at 16 on the device) because the wider
// chord sends far more cells down the exact fallback; at 16 the fallback
// costs 0.7 ms of the 8.3 (7.6 with the probe forced off).
constexpr int SILK_GRID = 16;      // coarse-grid cell width in pixels
constexpr int SILK_GRID_SHIFT = 4; // log2(SILK_GRID)
constexpr int SILK_Q_BITS = 8;     // interpolation fixed-point fractional bits
// The fast ramp carries the Q16 product shifted up by SILK_GRID_SHIFT (Q20),
// so per-pixel work is one add and one shift. The palette offset used to
// ride in the dither constants (PALETTE_REAL_OFF<<16, 3328<<16) and at Q19
// still fit; at Q20 it is 3328<<20 > 2^31. It lives in the palette base
// pointer instead (g_lut + PALETTE_REAL_OFF, one register, loop-invariant),
// and g_ditherQ is the plain Q16 dither again.
static_assert(SILK_GRID_SHIFT <= 4, "the Q(16+SHIFT) ramp bound below assumes at most 4 extra bits");
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
// proof that idx only ever reaches [-16, 271] here).
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
        g_ditherQ[k] = static_cast<int32_t>(lroundf(ditherLUT[k] * 65536.0f));
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
            g_ditherQ[k] = static_cast<int32_t>(lroundf(ditherLUT[k] * 65536.0f));
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
// Hand-written Xtensa kernels for band()'s per-cell inner loops.
//
// Ported for the sixth pass's 16-pixel grid and paired 32-bit stores (see
// the file header). The fifth pass's kernels below used to be written for
// the 8-pixel grid, wrote one pixel at a time, and read a dither table
// that carried PALETTE_REAL_OFF baked in, none of which bandRef() does any
// more, so a static_assert stopped this flag from building until the port
// was real.
//
// Method: read GCC 14's own compile of the current bandRef()
// (tools/animbench/xtensa-asm14.sh AnimSilk, then read
// xtensa-asm14/AnimSilk.S) and transcribe its FAST cell (fully unrolled,
// no loop instruction: GCC chose not to loop over just 8 independent
// stores) and EXACT cell (a genuine hardware zero-overhead LOOP over a
// compact per-pixel-pair body) instruction for instruction, then kept two
// scheduling ideas from the retired kernels that still apply without
// touching the arithmetic:
//   - an independent instruction between MULL and the add that consumes
//     it (the sq ramp step, in the EXACT kernel), matching the fifth
//     pass's round 3 kernel;
//   - a fresh 0-based counter standing in for the pixel-pair's phase
//     (kctr, ANDed with 3) instead of tracking x itself, valid because
//     every call site's x is a multiple of SILK_GRID (16, itself a
//     multiple of 8), so pixel pair k's true (x>>1)&3 equals a fresh
//     counter's k&3 exactly.
// The FAST kernel keeps GCC's own trick for the ramp step too: bandRef()
// computes dP2 = 2*(Pnext-Pcur) once and adds it each store, but GCC's
// compiled loop never materializes dP2 as its own value. It passes the
// undoubled half-step to ADDX2, which multiplies its first operand by 2
// as part of the add, so "PQ += dP2" becomes one ADDX2 instead of a
// separate multiply and add. That is exact (2*half is dP2 bit for bit,
// multiplying by a power of two loses nothing), so silkFastCell16Asm
// below takes dPhalf = Pnext - Pcur and steps with ADDX2, not dP2 with a
// plain add.
//
// One thing this port does NOT copy from GCC: GCC's FAST cell computes
// all 8 pre-shift index values before gathering any of them, which needs
// 8 live registers simultaneously and only works there because the
// surrounding function had already spilled most other state to the
// stack. A standalone leaf function does not have that headroom (this
// file's own ~13-14 AR register budget, see the file header), so
// silkFastCell16Asm below computes and gathers one store at a time
// instead, the same granularity the fifth pass's kernels used. The
// arithmetic is identical either way; only the instruction interleaving
// differs from GCC's literal schedule.
//
// Verified bit-exact against portable references transcribed from the
// current bandRef() under QEMU (tools/qemubench/tests/anim_silk). Not
// verified on real hardware: the bench board was offline when this port
// was written, so there is no device timing and no
// SleepAnimation::runAnimTest comparison for it yet. That measurement is
// the next step before this flag means anything beyond a QEMU-verified
// candidate, see the master switch comment above.
//
// PIE (the S3's 128-bit vector coprocessor) does not help either loop
// below: both are dominated by a LUT GATHER (an index computed per pixel
// or per pixel pair), and there is no vector gather instruction on this
// hardware, see tools/animbench/ASM_BRIEF.md's PIE facts.
//
// Both are noinline and take plain pointers and ints so the exact same
// bodies drop into the QEMU test at tools/qemubench/tests/anim_silk/
// unchanged, transcribed there by hand, not regenerated, so a QEMU PASS
// is evidence about this literal sequence.
//
// IRAM-pinned like band() itself: band() runs from IRAM specifically so
// LVGL's icache churn on core 1 can't stall it behind an MSPI refill (see
// the GM_ANIM_IRAM comment above), and calling out to a flash-resident
// helper from an IRAM function would reopen exactly that stall on every
// cell, so these two callees must stay in IRAM too.
//
// Neither kernel writes CPENABLE. FreeRTOS enables the FPU and PIE
// coprocessors lazily per task through the coprocessor-disabled
// exception; a kernel that set CPENABLE itself would skip that and could
// corrupt another task's coprocessor state (Controller::loopLogic's float
// state, in this codebase). Nothing here touches a coprocessor register,
// so there is nothing to enable, this note exists only so the next kernel
// added here does not have to rediscover the rule.

// silkFastCell16Asm: the dominant per-cell path (see band()'s "FAST cell"
// call below). Ramps the precomputed Q16 nc*env product P linearly across
// the cell's 16 pixels, written as 8 paired 32-bit stores (sixth pass):
// each store gathers ONE palette entry and duplicates it into both halves
// of the word, since the field is sampled once every 2 pixels now, not
// once per pixel. That is 8 gathers for 16 pixels, half the rate a
// hypothetical unpaired 16-pixel kernel would need, matching bandRef()
// exactly and matching the reason the sixth pass adopted pairing in the
// first place.
//
//   out:    16 consecutive uint16_t outputs to fill (8 uint32_t stores),
//           == out+x in band(); 4-byte aligned, since x is always a
//           multiple of SILK_GRID at every call site.
//   PQ0:    Pcur << SILK_GRID_SHIFT (Q20), the ramp's value for store 0.
//   dPhalf: Pnext - Pcur (Q16). See the block comment above for why this
//           is undoubled and stepped with ADDX2 rather than passed as
//           bandRef()'s dP2.
//   dq:     4-entry table, dq[k % 4] applies to store k (already
//           <<SILK_GRID_SHIFT, see g_ditherQ's declaration and dqArr's
//           construction in band()), matching bandRef()'s dq0..dq3
//           cycling across o[0]..o[7].
//   pal:    g_lut + PALETTE_REAL_OFF, the same pointer bandRef() reads
//           through for this path. NOT g_lut itself: the sixth pass moved
//           the palette offset out of the dither constants and into this
//           pointer instead (see PALETTE_REAL_OFF's comment), so a bare
//           g_lut base here would read the wrong table.
GM_ANIM_IRAM __attribute__((noinline)) void silkFastCell16Asm(uint16_t *out, int32_t PQ0, int32_t dPhalf,
                                                                const int32_t *dq, const uint16_t *pal) {
    int32_t pq = PQ0;
    const int32_t dq0v = dq[0];
    const int32_t dq1v = dq[1];
    const int32_t dq2v = dq[2];
    const int32_t dq3v = dq[3];
    int32_t t0, t1;
    asm volatile("add    %[t0], %[pq], %[dq0]\n" // store 0
                 "srai   %[t0], %[t0], 20\n"
                 "addx2  %[pq], %[dph], %[pq]\n" // PQ1 = PQ0 + dP2 (2*dPhalf via ADDX2)
                 "addx2  %[t0], %[t0], %[pal]\n"
                 "l16ui  %[t0], %[t0], 0\n"
                 "slli   %[t1], %[t0], 16\n"
                 "add    %[t0], %[t0], %[t1]\n"
                 "s32i   %[t0], %[out], 0\n"
                 "add    %[t0], %[pq], %[dq1]\n" // store 1
                 "srai   %[t0], %[t0], 20\n"
                 "addx2  %[pq], %[dph], %[pq]\n" // PQ2
                 "addx2  %[t0], %[t0], %[pal]\n"
                 "l16ui  %[t0], %[t0], 0\n"
                 "slli   %[t1], %[t0], 16\n"
                 "add    %[t0], %[t0], %[t1]\n"
                 "s32i   %[t0], %[out], 4\n"
                 "add    %[t0], %[pq], %[dq2]\n" // store 2
                 "srai   %[t0], %[t0], 20\n"
                 "addx2  %[pq], %[dph], %[pq]\n" // PQ3
                 "addx2  %[t0], %[t0], %[pal]\n"
                 "l16ui  %[t0], %[t0], 0\n"
                 "slli   %[t1], %[t0], 16\n"
                 "add    %[t0], %[t0], %[t1]\n"
                 "s32i   %[t0], %[out], 8\n"
                 "add    %[t0], %[pq], %[dq3]\n" // store 3
                 "srai   %[t0], %[t0], 20\n"
                 "addx2  %[pq], %[dph], %[pq]\n" // PQ4
                 "addx2  %[t0], %[t0], %[pal]\n"
                 "l16ui  %[t0], %[t0], 0\n"
                 "slli   %[t1], %[t0], 16\n"
                 "add    %[t0], %[t0], %[t1]\n"
                 "s32i   %[t0], %[out], 12\n"
                 "add    %[t0], %[pq], %[dq0]\n" // store 4
                 "srai   %[t0], %[t0], 20\n"
                 "addx2  %[pq], %[dph], %[pq]\n" // PQ5
                 "addx2  %[t0], %[t0], %[pal]\n"
                 "l16ui  %[t0], %[t0], 0\n"
                 "slli   %[t1], %[t0], 16\n"
                 "add    %[t0], %[t0], %[t1]\n"
                 "s32i   %[t0], %[out], 16\n"
                 "add    %[t0], %[pq], %[dq1]\n" // store 5
                 "srai   %[t0], %[t0], 20\n"
                 "addx2  %[pq], %[dph], %[pq]\n" // PQ6
                 "addx2  %[t0], %[t0], %[pal]\n"
                 "l16ui  %[t0], %[t0], 0\n"
                 "slli   %[t1], %[t0], 16\n"
                 "add    %[t0], %[t0], %[t1]\n"
                 "s32i   %[t0], %[out], 20\n"
                 "add    %[t0], %[pq], %[dq2]\n" // store 6
                 "srai   %[t0], %[t0], 20\n"
                 "addx2  %[pq], %[dph], %[pq]\n" // PQ7
                 "addx2  %[t0], %[t0], %[pal]\n"
                 "l16ui  %[t0], %[t0], 0\n"
                 "slli   %[t1], %[t0], 16\n"
                 "add    %[t0], %[t0], %[t1]\n"
                 "s32i   %[t0], %[out], 24\n"
                 "add    %[t0], %[pq], %[dq3]\n" // store 7 (no further PQ step: bandRef() steps PQ
                                                  // seven times for eight stores, and this is the eighth)
                 "srai   %[t0], %[t0], 20\n"
                 "addx2  %[t0], %[t0], %[pal]\n"
                 "l16ui  %[t0], %[t0], 0\n"
                 "slli   %[t1], %[t0], 16\n"
                 "add    %[t0], %[t0], %[t1]\n"
                 "s32i   %[t0], %[out], 28\n"
                 : [pq] "+r"(pq), [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [dph] "r"(dPhalf), [dq0] "r"(dq0v), [dq1] "r"(dq1v), [dq2] "r"(dq2v), [dq3] "r"(dq3v),
                   [pal] "r"(pal), [out] "r"(out)
                 : "memory");
}

// silkExactCell16Asm: the curvature-fallback path (see band()'s "EXACT
// cell" call below). Transcribed instruction for instruction from GCC's
// own compiled EXACT-cell loop for the current bandRef() (see the block
// comment above), which is the same shape the fifth pass's round 3
// rewrite already established for this cell: a hardware zero-overhead
// LOOP over a compact body, not an unrolled one, because IRAM has no
// instruction cache and a loop's ~20 bytes of body are fetched once per
// call where an unrolled body's several hundred bytes are fetched fresh
// every time (see that history below the file header). The sixth pass
// changed what the loop produces per iteration (one pixel pair instead of
// one pixel) and where the palette offset lives (its own pointer, not
// baked into the dither table); this kernel follows both.
//
//   out:       16 consecutive uint16_t outputs to fill (8 uint32_t
//              stores, one pixel pair each).
//   sQ0:       sCur << SILK_Q_BITS (Q8), the s-ramp's value at pixel 0.
//   stepQ2:    the PAIRED s-ramp step, bandRef()'s stepQ2. This is
//              already doubled for the sixth pass's pixel-pair stride; it
//              is not the fifth pass's per-pixel step.
//   envBase:   envRowBase_q8 for this row (Q8).
//   dx2AtX:    &g_dx2Row[x], one uint8_t PER PAIR, walked forward two
//              bytes per iteration (only the pair's first pixel's dx2 is
//              read, matching bandRef()'s g_dx2Row[x] with x advancing by
//              2 each loop pass).
//   ditherRow: &g_ditherQ[yph*4]. Indexed by the pair's phase (0..3,
//              cycling) via a fresh 0-based counter, the same idea
//              silkFastCell16Asm's dq[] cycling relies on: every call
//              site's x is a multiple of SILK_GRID (16), so pair k's true
//              (x>>1)&3 equals a fresh counter's k&3 exactly, with no
//              need to carry the real x into this kernel at all.
//   lut:       g_lut base, for the contrast gather. Index s is already in
//              [0, CONTRAST_N-1], no offset needed, same as bandRef().
//   pal:       g_lut + PALETTE_REAL_OFF, for the palette gather. A
//              SEPARATE pointer from lut, since the sixth pass moved the
//              offset out of the dither table and into this pointer (see
//              PALETTE_REAL_OFF's comment); the fifth pass's retired
//              kernel used one shared "lut" pointer for both gathers
//              because its dither table carried the bias, which is no
//              longer true.
//
// mull's result has one independent instruction (the sq ramp step) before
// the add that consumes it, matching GCC's own schedule for this data
// layout, the same idea the fifth pass's round 3 kernel already used for
// this cell.
GM_ANIM_IRAM __attribute__((noinline)) void silkExactCell16Asm(uint16_t *out, int32_t sQ0, int32_t stepQ2,
                                                                 int32_t envBase, const uint8_t *dx2AtX,
                                                                 const int32_t *ditherRow, const uint16_t *lut,
                                                                 const uint16_t *pal) {
    int32_t sq = sQ0;
    const uint8_t *dx2p = dx2AtX;
    uint16_t *outp = out;
    int32_t kctr = 0;
    int32_t u, v1, ph;
    asm volatile("movi   %[u], 8\n"
                 "loop   %[u], 1f\n"
                 "l8ui   %[v1], %[dx2p], 0\n"          // dx2
                 "addi   %[dx2p], %[dx2p], 2\n"        // dx2p += 2 (next pair, done early: not needed again)
                 "srai   %[u], %[sq], 8\n"             // s = sq >> 8
                 "addx2  %[u], %[u], %[lut]\n"         // &lut[s]
                 "extui  %[ph], %[kctr], 0, 2\n"       // k & 3
                 "l16ui  %[u], %[u], 0\n"              // nc_q8 = lut[s]
                 "sub    %[v1], %[envb], %[v1]\n"      // env_q8 = envBase - dx2
                 "addx4  %[ph], %[ph], %[ditherrow]\n" // &ditherRow[k&3]
                 "l32i   %[ph], %[ph], 0\n"            // dith = ditherRow[k&3]
                 "mull   %[u], %[u], %[v1]\n"          // nc_q8 * env_q8
                 "add    %[sq], %[sq], %[stepq]\n"     // sq += stepQ2 (mull-gap filler)
                 "add    %[u], %[u], %[ph]\n"          // idxq = mull_result + dith
                 "srai   %[u], %[u], 16\n"             // idx
                 "addx2  %[u], %[u], %[pal]\n"         // &pal[idx]
                 "l16ui  %[u], %[u], 0\n"              // v = pal[idx]
                 "addi   %[kctr], %[kctr], 1\n"        // kctr++
                 "slli   %[v1], %[u], 16\n"            // v << 16
                 "add    %[u], %[u], %[v1]\n"          // v | (v << 16)
                 "s32i   %[u], %[outp], 0\n"           // out[pair] = v duplicated into both halves
                 "addi   %[outp], %[outp], 4\n"        // outp += 4 (one pixel pair)
                 "1:\n"
                 : [sq] "+r"(sq), [dx2p] "+r"(dx2p), [outp] "+r"(outp), [kctr] "+r"(kctr), [u] "=&r"(u),
                   [v1] "=&r"(v1), [ph] "=&r"(ph)
                 : [stepq] "r"(stepQ2), [envb] "r"(envBase), [lut] "r"(lut), [pal] "r"(pal),
                   [ditherrow] "r"(ditherRow)
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
    // this loop. (The asm pass's silkFastCell16Asm/silkExactCell16Asm above
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
        // Dither y-phase for this row -- g_ditherQ is indexed
        // (y&3)*4 + ((x>>1)&3), g_dx2Row by x alone (dx2 never varied by
        // phase, see its declaration), so there is no per-row table SELECT
        // left to do, only this one phase index to carry into the loop below.
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
        // supported panel sizes; 466 % 16 == 2), so the trailing <SILK_GRID
        // pixels at the row's right edge fall back to the exact per-pixel
        // path below — at most 15 pixels/row, not a per-band()-call cost.
        uint32_t a = base[0], b = base[1], c = base[2];
        // Biased once per node, not per pixel (see SIN_SUM_BIAS).
        int32_t sCur = SIN_SUM_BIAS + sinFromTurn(a) + sinFromTurn(b) + sinFromTurn(c); // exact node @ x=0
        int32_t ncCur = g_lut[sCur];                            // Q8 contrast at the node
        int32_t Pcur = ncCur * (envRowBase_q8 - g_dx2Node[0]);  // Q16 nc*env product at the node
        // --- Fourth pass: per-cell PRODUCT ramp (see file header) ---
        // Pixels are written in pairs (sixth pass), and the Bayer cell is a
        // pixel pair wide: pair k of a row takes dither phase k & 3. Every
        // cell starts at x % 16 == 0, so within ANY cell the eight pairs
        // cycle phases 0,1,2,3,0,1,2,3 from the cell start. g_ditherQ[yph*4 +
        // 0..3] is exactly those four values; shift them into the ramp's Q20
        // once per ROW and they live in registers for the whole row: the
        // fast path reads no per-pixel tables at all except the palette.
        // Multiplies, not shifts: the dither is signed now that the palette
        // bias is out of it, and a left shift of a negative value is UB.
        const int32_t dq0 = g_ditherQ[yph * 4 + 0] * (1 << SILK_GRID_SHIFT);
        const int32_t dq1 = g_ditherQ[yph * 4 + 1] * (1 << SILK_GRID_SHIFT);
        const int32_t dq2 = g_ditherQ[yph * 4 + 2] * (1 << SILK_GRID_SHIFT);
        const int32_t dq3 = g_ditherQ[yph * 4 + 3] * (1 << SILK_GRID_SHIFT);
        // Palette base: g_lut + PALETTE_REAL_OFF, indexed by the signed
        // palette index in [-16, 271] (see the file-header bound), which
        // lands inside paletteExt's padding at both ends.
        const uint16_t *const pal = g_lut + PALETTE_REAL_OFF;
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
                // Q20 (<< SILK_GRID_SHIFT), two pixels per step. Exact
                // closure, same argument as the old s-ramp: 8 steps of
                // 2*(Pnext-Pcur) from Pcur<<4 land on Pnext<<4 exactly.
                // Pixel 0 telescopes to the slow path's node value
                // bit-for-bit: ((P + d) << 4) >> 20 == (P + d) >> 16 (the
                // <<4 is exact, see the header overflow bound, and
                // arithmetic shifts compose), so fast and slow cells never
                // seam.
                //
                // Both pixels of a pair get the pair's first sample and one
                // 32-bit store (AnimPlasma.cpp's precedent: bands start on a
                // row boundary and w is even, so out + x is 4-byte aligned
                // whenever x is). Halving the gathers and stores is what
                // took the fast path from 10.2 to 8.3 ms on the device; the
                // field itself is sampled on a 16-pixel grid, so the only
                // thing the pairing changes is that the dither grain is two
                // pixels wide (goldens moved by a mean of 2.0 of 255 from
                // that alone; the grid change by itself moved them 0.14).
                //
                // This IS an 8x unroll, but not the one band()'s NOTE above
                // warns about: that flat unroll replicated the whole 3-LUT
                // pixel body (~20+ simultaneous live values, spilled, lost
                // the hardware LOOP). This body keeps ~10 values live (PQ,
                // dP2, dq0-3, o, pal, v) -- inside the ~13-14 AR budget.
                int32_t PQ = Pcur << SILK_GRID_SHIFT;
                const int32_t dP2 = (Pnext - Pcur) * 2;
                uint32_t *const o = reinterpret_cast<uint32_t *>(out + x);
                uint32_t v;
                v = pal[(PQ + dq0) >> (16 + SILK_GRID_SHIFT)];
                o[0] = v | (v << 16);
                PQ += dP2;
                v = pal[(PQ + dq1) >> (16 + SILK_GRID_SHIFT)];
                o[1] = v | (v << 16);
                PQ += dP2;
                v = pal[(PQ + dq2) >> (16 + SILK_GRID_SHIFT)];
                o[2] = v | (v << 16);
                PQ += dP2;
                v = pal[(PQ + dq3) >> (16 + SILK_GRID_SHIFT)];
                o[3] = v | (v << 16);
                PQ += dP2;
                v = pal[(PQ + dq0) >> (16 + SILK_GRID_SHIFT)];
                o[4] = v | (v << 16);
                PQ += dP2;
                v = pal[(PQ + dq1) >> (16 + SILK_GRID_SHIFT)];
                o[5] = v | (v << 16);
                PQ += dP2;
                v = pal[(PQ + dq2) >> (16 + SILK_GRID_SHIFT)];
                o[6] = v | (v << 16);
                PQ += dP2;
                v = pal[(PQ + dq3) >> (16 + SILK_GRID_SHIFT)];
                o[7] = v | (v << 16);
                x += SILK_GRID;
            } else {
#ifdef GM_SILK_HOST_DIFF
                g_silkSlowCells++;
#endif
                // EXACT cell (the pre-pass-4 body, per pixel pair since the
                // sixth pass): the contrast chord would deviate visibly
                // here, so apply the curve along the interpolated s at every
                // pair. Exact closure as before: 8 steps of 2*(sNext-sCur)
                // << (Q_BITS-GRID_SHIFT) land on sNext<<Q_BITS.
                const int32_t stepQ2 = (sNext - sCur) * (2 << (SILK_Q_BITS - SILK_GRID_SHIFT)); // a multiply, not a shift: the difference can be negative
                int32_t sQ = sCur << SILK_Q_BITS;
                for (int i = 0; i < SILK_GRID; i += 2) {
                    const int32_t s = sQ >> SILK_Q_BITS;
                    // g_lut[s] (contrast curve) and pal[idx] (palette) are
                    // ONE table with two base offsets, not two tables -- see
                    // the g_lut/contrastLUT/paletteExt comment above.
                    const int32_t nc_q8 = g_lut[s]; // already the biased index, Q8 (nc*256)
                    // env_q8 in [0,256] always (see file header proof); no
                    // clamp needed for any square panel (all current
                    // display drivers are square).
                    const int32_t env_q8 = envRowBase_q8 - g_dx2Row[x];
                    // nc_q8 (Q8) * env_q8 (Q8) = Q16; g_ditherQ[] is Q16, so
                    // one add combines them and one arithmetic shift gives
                    // the signed palette index.
                    const int32_t idxq = nc_q8 * env_q8 + g_ditherQ[yph * 4 + ((x >> 1) & 3)];
                    const uint32_t v = pal[static_cast<int>(idxq >> 16)];
                    *reinterpret_cast<uint32_t *>(out + x) = v | (v << 16);
                    sQ += stepQ2;
                    x += 2;
                }
            }
            sCur = sNext;
            ncCur = ncNext;
            Pcur = Pnext;
        }
        // Exact tail for the row's non-grid-aligned remainder (0..15 pixels):
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
            const int32_t idxq = nc_q8 * env_q8 + g_ditherQ[yph * 4 + ((x >> 1) & 3)];
            out[x] = pal[static_cast<int>(idxq >> 16)];
        }
        base[0] += static_cast<uint32_t>(g_rowStep[0]);
        base[1] += static_cast<uint32_t>(g_rowStep[1]);
        base[2] += static_cast<uint32_t>(g_rowStep[2]);
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM) && GM_BGANIM_SILK_ASM
// On-device band(): identical algorithm to bandRef() above (the coarse-grid
// node computation and curvature probe are unchanged C++), but the two
// per-cell inner bodies, the 16-pixel product ramp and the 16-pixel exact
// fallback, dispatch to the hand-written Xtensa kernels above instead of
// the scalar C++ loops. See silkFastCell16Asm/silkExactCell16Asm's own
// comments for why PIE doesn't apply here (gather-dominated, no vector
// gather on this hardware) and for the scheduling. This dispatch is kept
// pixel-exact with the current bandRef() by construction (every value it
// hands the kernels is computed the same way bandRef() computes its own
// C++ path, and the kernels are transcribed from bandRef()'s own compiled
// form), and checked against portable references transcribed from
// bandRef() under QEMU (tools/qemubench/tests/anim_silk/). Not yet
// checked by SleepAnimation::runAnimTest (/api/debug/animtest) against
// bandRef() on the real chip: the bench board was offline when this port
// was written.
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
        // keeps as separate scalars (dq0..dq3): the asm kernel needs them
        // as an array to pass a single pointer argument (see
        // silkFastCell16Asm's signature). Multiplied, not shifted, for the
        // same reason bandRef() switched from a shift: the dither is
        // signed now that the palette bias is out of it, and left-shifting
        // a negative value is undefined behavior. Built once per row, like
        // the scalar dq0..dq3 it mirrors.
        const int32_t dqArr[4] = {
            g_ditherQ[yph * 4 + 0] * (1 << SILK_GRID_SHIFT),
            g_ditherQ[yph * 4 + 1] * (1 << SILK_GRID_SHIFT),
            g_ditherQ[yph * 4 + 2] * (1 << SILK_GRID_SHIFT),
            g_ditherQ[yph * 4 + 3] * (1 << SILK_GRID_SHIFT),
        };
        // Palette base for both the fast and exact kernels below, same
        // pointer and same reasoning as bandRef()'s own pal: the sixth
        // pass moved the PALETTE_REAL_OFF bias out of the dither table and
        // into this pointer, so the kernels index it directly instead of
        // adding the bias per pixel.
        const uint16_t *const pal = g_lut + PALETTE_REAL_OFF;
        // silkExactCell16Asm's per-row dither operand (see its comment):
        // the row's y-phase offset into g_ditherQ, folded into the pointer
        // once here rather than into 4 preloaded scalars. The kernel
        // indexes it by the pixel pair's phase (0..3) at runtime inside
        // its loop via a fresh counter, matching GCC's own compiled
        // schedule, so it takes the base pointer, not unshifted copies of
        // its 4 entries.
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
                // FAST cell: see silkFastCell16Asm's comment above. pal, not
                // g_lut: the ramp's indices are relative to the palette base,
                // not the combined contrast+palette table.
                silkFastCell16Asm(out + x, Pcur << SILK_GRID_SHIFT, Pnext - Pcur, dqArr, pal);
            } else {
                // EXACT cell: see silkExactCell16Asm's comment above. stepQ2
                // is bandRef()'s paired step (the *2 accounts for the sixth
                // pass's pixel-pair stride, matching bandRef()'s own
                // stepQ2 exactly, not the pre-sixth-pass per-pixel step).
                // a multiply, not a shift: the difference can be negative
                const int32_t stepQ2 = (sNext - sCur) * (2 << (SILK_Q_BITS - SILK_GRID_SHIFT));
                silkExactCell16Asm(out + x, sCur << SILK_Q_BITS, stepQ2, envRowBase_q8, g_dx2Row + x, ditherRow,
                                   g_lut, pal);
            }
            x += SILK_GRID;
            sCur = sNext;
            ncCur = ncNext;
            Pcur = Pnext;
        }
        // Exact tail for the row's non-grid-aligned remainder (0..15
        // pixels; 0 at this panel's 480/240 widths, up to 2 at 466, see
        // bandRef()'s comment). Small and rare enough that hand-written
        // asm is not worth it here; this is bandRef()'s tail, verbatim,
        // including reading the output through pal (not g_lut: idxq's
        // index is relative to the palette base since the sixth pass, see
        // PALETTE_REAL_OFF's comment) and the pair-phase dither index.
        for (; x < w; x++) {
            const int32_t s = SIN_SUM_BIAS + sinFromTurn(a) + sinFromTurn(b) + sinFromTurn(c);
            a += g_step[0];
            b += g_step[1];
            c += g_step[2];
            const int32_t nc_q8 = g_lut[s];
            const int32_t env_q8 = envRowBase_q8 - g_dx2Row[x];
            const int32_t idxq = nc_q8 * env_q8 + g_ditherQ[yph * 4 + ((x >> 1) & 3)];
            const int idx = static_cast<int>(idxq >> 16);
            out[x] = pal[idx];
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
