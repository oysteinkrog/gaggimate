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
//   - rowAux[].dx2 stores the vignette column term as vignK*dx*dx*256 (Q8,
//     always >= 0).
//   - envRowBase is computed once per ROW (not per pixel) as
//     round((1 - vignK*dy*dy) * 256) — a float->int cast here costs
//     nothing, it happens `rows` times per band() call, not w*rows times.
//   - band()'s tail is then: env_q8 = envRowBase_q8 - dx2_q8 (env is
//     PROVABLY non-negative for any square panel — see below — so the old
//     "if (env<0) env=0" branch is dropped, not just made branchless);
//     idxq = nc_q8 * env_q8 + dith_q16 (one 32x32 mull, Q8*Q8=Q16); idx =
//     idxq >> 16 (one arithmetic shift, replacing trunc.s + min + max).
//   - ra[x].dith is now stored pre-scaled to Q16 (dith*65536) rather than
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
// Design: anim-fluid (Fable), 2026-08-15. Optimized: anim-fluid, 2026-08-15;
// opt-silk2 (fixed-point tail), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

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
// Dither LUT folds the BAYER4 "/16, -0.5 center, *255/160 rescale" chain
// into a single lookup so band() spends one array read instead of two
// subtracts and two multiplies per pixel. Built as float here (init-time
// only, once per process) then re-quantized to Q16 into rowAux[].dith.
float ditherLUT[16];
// rowAux[phase][x] merges what used to be two separate per-pixel arrays:
//   - dx2LUT[x]        = (x - cx)^2 * g_vignK, row-invariant vignette term
//   - ditherRow[y&3][x] = ditherLUT[(y&3)*4 + (x&3)] expanded to full width
// into ONE array-of-structs, so band()'s hot loop walks a SINGLE
// incrementing pointer that yields both terms via fixed 0/4-byte offsets,
// instead of two independent walking pointers. dx2 doesn't actually vary
// by phase, so it's wastefully duplicated 4x here (4*w floats instead of
// w — ~7.5KB extra SRAM at w=480), but that's the price of collapsing two
// loop-carried induction pointers into one. Confirmed via xtensa-asm.sh
// that having dx2LUT and ditherRow as two SEPARATE walking pointers (on
// top of the out-pointer) pushed band()'s loop over whatever
// induction-variable-count threshold the Xtensa "zero-overhead LOOP"
// selection pass uses — even though total instruction count was already
// below baseline, the loop fell back to a plain compare-and-branch. Merging
// them back down to two walking pointers (out + rowAux) restores the
// hardware LOOP instruction. See band()'s comment for the measured effect.
struct RowAux {
    int32_t dx2;  // Q8: g_vignK*dx*dx * 256, always >= 0
    int32_t dith; // Q16: dither value * 65536 (see file header for why Q16, not Q8)
};
RowAux *rowAux[4] = {nullptr, nullptr, nullptr, nullptr};
uint32_t lastThemeGen = 0xFFFFFFFF;
int lastGlow = -1;
float g_invR2 = 1.0f;
float g_vignK = 0.32f; // 0.32f * g_invR2, folded so band() does one multiply instead of two
int32_t g_step[3];    // per-pixel x-phase step, Q32 turns/px
int32_t g_rowStep[3]; // per-row y-phase step, Q32 turns/row
uint32_t g_wtTurn[3]; // temporal phase at y=0, Q32 turns (already mod 2*pi via wraparound)
// sinLut() lives in BgAnimCommon.cpp (a different translation unit — this
// build has no LTO), so calling it from the pixel loop is a real, un-inlined
// function call with a lazy-init branch, 3x/pixel = 691200 calls/frame. That
// call overhead was the actual dominant cost of this animation, not the
// fmodf (removing fmodf alone barely moved host time). Cache the pointer
// once at init and index it directly in band() instead.
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

bool init(int w, int h) {
    g_sinLut = sinLut();
    if (g_sinLut == nullptr) {
        return false;
    }
    if (g_lut == nullptr) {
        g_lut = static_cast<uint16_t *>(alloc(LUT_N * sizeof(uint16_t)));
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
        for (int k = 0; k < 16; k++) {
            ditherLUT[k] = (BAYER4[k] / 16.0f - 0.5f) * (255.0f / 160.0f);
        }
    }

    // Build the merged vignette+dither table (see rowAux comment above): one
    // array per y&3 phase, each spanning the full panel width. Both ditherLUT
    // and g_vignK/cx are constant for the process lifetime at this point, so
    // each phase is still built at most once.
    //
    // This deliberately sits OUTSIDE the lastGlow guard above, and each phase
    // carries its own null check. Previously the whole thing lived inside that
    // guard, which set lastGlow before allocating: if any phase failed, init()
    // returned false but lastGlow stayed set, so the next activation skipped
    // the block entirely -- retry and check alike -- and the trailing test
    // only looked at rowAux[0]. A single transient allocation failure on any
    // of phases 1-3 therefore became a permanent null dereference in band(),
    // which indexes rowAux[y & 3] with no check of its own.
    {
        const float cx = w * 0.5f;
        for (int ph = 0; ph < 4; ph++) {
            if (rowAux[ph] != nullptr) {
                continue;
            }
            rowAux[ph] = static_cast<RowAux *>(alloc(w * sizeof(RowAux)));
            if (rowAux[ph] == nullptr) {
                continue; // retried on the next init()
            }
            for (int x = 0; x < w; x++) {
                const float dx = x - cx;
                // Pre-scaled by g_vignK and quantized to Q8 so band()'s
                // per-pixel vignette work is a single integer subtract —
                // env_q8 = envRowBase_q8 - ra[x].dx2 — instead of a load +
                // add(dy2) + multiply(vignK) + subtract, all in float.
                rowAux[ph][x].dx2 = static_cast<int32_t>(lroundf(g_vignK * dx * dx * 256.0f));
                // Q16 (not Q8): dith's real magnitude is < 1, so rounding it
                // to a plain integer here would collapse all 16 dither
                // levels into 2-3 buckets — see file header.
                rowAux[ph][x].dith = static_cast<int32_t>(lroundf(ditherLUT[ph * 4 + (x & 3)] * 65536.0f));
            }
        }
    }
    // All four phases, unconditionally: band() dereferences whichever phase
    // the row lands on, so any missing one must fail init().
    for (int ph = 0; ph < 4; ph++) {
        if (rowAux[ph] == nullptr) {
            return false;
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

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
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
    // insns/pixel of spill/reload traffic). Kept as a single-pixel loop.
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const float dy = y - cy;
        // envRowBase is computed once per ROW, not per pixel, so a float
        // here costs nothing (rows times per band() call, not w*rows) — see
        // file header for the Q8 scheme and the non-negative-env proof.
        // rowAux[].dx2 is pre-scaled by g_vignK (also Q8), so folding the
        // row's dy contribution in here collapses the per-pixel vignette
        // math down to a single integer subtract (see rowAux comment).
        // Manual "+0.5 then truncate" round instead of lroundf(): this runs
        // once per row (every band() call touches `rows` rows), and
        // envRowBase is provably always in [0.68, 1.0] (never negative), so
        // round-half-away-from-zero collapses to round-half-up — no libm
        // call needed, keeping band() itself at zero libm calls.
        const int32_t envRowBase_q8 = static_cast<int32_t>((1.0f - g_vignK * dy * dy) * 256.0f + 0.5f);
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        uint32_t a = base[0], b = base[1], c = base[2];
        // Pick this row's merged vignette+dither table (see rowAux
        // comment) — a single per-row pointer select, zero per-pixel cost.
        // band()'s hot loop then walks ONE incrementing pointer (ra) to
        // get BOTH per-pixel terms (ra[x].dx2, ra[x].dith) instead of two
        // separate walking pointers — that's what keeps the loop simple
        // enough to earn the hardware zero-overhead LOOP instruction
        // (confirmed via xtensa-asm.sh: two independent per-pixel array
        // pointers here, on top of the out-pointer, was enough loop-carried
        // induction traffic to make the compiler fall back to a plain
        // compare-and-branch, even though raw instruction count was already
        // below baseline — merging back to two walking pointers total
        // restores the `loop` instruction).
        const RowAux *const ra = rowAux[y & 3];
        for (int x = 0; x < w; x++) {
            const int32_t s = sinFromTurn(a) + sinFromTurn(b) + sinFromTurn(c); // -1536..1536
            a += g_step[0];
            b += g_step[1];
            c += g_step[2];
            // g_lut[s+1536] (contrast curve) and g_lut[PALETTE_REAL_OFF+idx]
            // (palette) are ONE walking pointer with two compile-time-
            // constant offsets, not two separate pointers — see the
            // g_lut/contrastLUT/paletteExt comment above for why that
            // matters (frees the AR register the second pointer needed,
            // which otherwise had to spill/reload every pixel).
            const int32_t nc_q8 = g_lut[s + 1536]; // direct index, Q8 (nc*256)
            // env_q8 in [0,256] always (see file header proof); no clamp
            // needed for any square panel (all current display drivers are
            // square), which is what turns the old float subtract + compare
            // + branch + multiply-add + trunc.s chain into one mull + one
            // shift below.
            const int32_t env_q8 = envRowBase_q8 - ra[x].dx2;
            // nc_q8 (Q8) * env_q8 (Q8) = Q16 (nc*env*65536); ra[x].dith is
            // pre-scaled to the same Q16 units (see file header for why
            // dither needs the extra fractional bits), so one add combines
            // them and one arithmetic shift recovers the plain index.
            const int32_t idxq = nc_q8 * env_q8 + ra[x].dith;
            const int idx = static_cast<int>(idxq >> 16);
            out[x] = g_lut[PALETTE_REAL_OFF + idx];
        }
        base[0] += static_cast<uint32_t>(g_rowStep[0]);
        base[1] += static_cast<uint32_t>(g_rowStep[1]);
        base[2] += static_cast<uint32_t>(g_rowStep[2]);
    }
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
};

#endif // GAGGIMATE_SIM
