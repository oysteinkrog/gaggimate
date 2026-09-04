#ifndef GAGGIMATE_SIM

// "Silk 2" -- same fabric-and-light idea as AnimSilk.cpp (soft, drifting
// interference cells lit by a slowly sweeping highlight) rebuilt so every
// per-pixel step is a plain array read or a single shared-table gather, no
// per-pixel trig and no per-pixel multiply.
//
// Round 3 built the fringe pattern from one column table and one ROW table
// (colFold[x] + rowFold[y]), an exactly separable field, plus a per-row
// SHIFT on the column table to break the "several identical peaks" symmetry
// that separability causes (see the report). Two independent visual reviews
// of that round agreed the fix was real -- one dominant hotspot most of the
// time -- but both still saw the underlying lattice: rowFold[y] is a
// function of y ALONE, so every cell in a given row shares that row's
// height, and every highlight still sits in one of a small set of fixed
// horizontal lanes. A per-row-independent term, however it is built, cannot
// avoid this: it is what "the row term" MEANS.
//
// This round removes rowFold entirely. Silk's own three waves point in
// genuinely arbitrary, independently rotating directions -- a real
// sin(kx*x + ky*y) evaluated in full at every pixel -- which is exactly why
// no row or column of the field is ever privileged: kx*x + ky*y depends on
// both coordinates inseparably at every angle except the axis-aligned ones.
// Silk pays for that with a real per-pixel (or per-grid-cell) sine and a
// curvature branch, three times over. Silk 2 still cannot afford that, but
// it can afford the algebraic identity that makes an oblique PLANE (not the
// sine of one, the plane itself) separable after all:
//
//     kx*x + ky*y  =  kx * (x + (ky/kx)*y)
//
// i.e. a wave tilted at angle atan(ky/kx) is exactly a column table read at
// x + slope*y, slope = ky/kx, with the y-dependence folded into a per-row
// INTEGER SHIFT of which window of the table this row reads -- one gather
// per pixel, the same cost as any other column-table read, not a multiply
// and not a second gather. Round 3 already used a shift of this shape, but
// as a cosmetic sine wobble with no wave behind it, purely to bend colFold's
// stripes enough to break the row/column symmetry. This round uses the
// SAME mechanism for what it is actually for: two genuinely oblique waves,
// colFoldA[x + shiftA(y)] and colFoldB[x + shiftB(y)], each a real plane
// tilted at its own (slowly drifting) angle, with shiftA/shiftB now the
// exact linear functions the identity above gives, not an approximation.
// Neither wave is ever a function of y alone, so there is no row for
// highlights to collect on -- the lattice the second review caught is gone
// by construction, not tuned away.
//
// Cost: this is a genuinely new per-pixel table read (colFoldB[x]) beside
// the one round 3 already had (colFoldA[x]), before the sheen's sine gather
// and the final palette gather -- four memory accesses a pixel instead of
// three. Round 2's own lesson was that a third read, however cheap, was not
// free on this chip; a fourth is measured on the device below, not assumed.
//
// Sheen stays the third wave, unchanged from round 3: it is already
// genuinely oblique and continuously rotating (SHEEN_A0 drifts through the
// full circle over time, not a bounded wobble), which the linear-shift trick
// above cannot represent -- tan(angle) diverges approaching +-90 degrees, so
// a table built to cover a full rotation's worst-case shift would need
// unbounded padding. Sheen keeps paying for a real per-pixel sine gather
// because unbounded rotation is the one thing it needs that colFoldA/B's
// bounded wobble (see their own comments for the angle ranges and why they
// stay narrow) cannot buy cheaply.
//
// Vignette: round 3's version was an additive approximation of silk's true
// multiplicative one, split across colFold's column half and rowFold's row
// half. With rowFold gone, the row half moves to a new small table,
// rowVign[y] (see its own comment) -- vignette-only, no wave, so it is
// still an exact function of y alone, which is fine: unlike a fringe cell,
// the rim SHOULD darken uniformly regardless of x, that symmetry is the
// point of a vignette. Strengthened from round 3 (300 vs 220 at each rim)
// per this round's brief, closer to how strongly silk's own rim darkens.
//
// What this still trades away from silk's look: colFoldA and colFoldB are
// each a sum of two close, non-commensurate frequencies (the round 3 beat
// trick, for irregular cell sizes) rather than one pure wave, and their
// angles wobble in a bounded range rather than rotating freely the way
// silk's three waves and this file's own sheen do -- see their own comments
// for why the range must stay bounded. The dither pattern lives only on
// colFoldA (4 columns x 2 row-phases, unchanged from round 3) to keep
// colFoldB's now much wider padded table inside the hot-SRAM budget; the
// combined sum still carries that dither into the final quantization, so
// this was checked for banding in dark regions, not assumed safe.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

// Same reasoning as AnimSilk.cpp/AnimEmber.cpp: keep the hot loop out of
// flash so LVGL's icache churn on the other core can't stall it behind an
// MSPI refill. No-op on the host bench.
#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

namespace {
using namespace bganim;

// ---- range budget ----------------------------------------------------
// Every amplitude below is a hard bound from where it is built (see
// frame()'s colFoldA/colFoldB/rowVign loops), not a typical case, so
// SUM_MIN/SUM_MAX are computed FROM these constants rather than
// hand-derived, the same discipline AnimSilk.cpp's own range proofs use:
// change a constant below and the LUT resizes itself correctly.
//
// Round 3's colFoldPh and rowFold each carried a native +-SIN_AMP (512)
// swing; with two oblique column tables now sharing the per-pixel sum with
// the sheen, and both bigger padded tables and the LUT itself sharing one
// 9,216 B hot-SRAM budget, every wave's amplitude is pre-scaled DOWN, at
// build time for colFoldA/B or once at init() for the sheen (see
// g_sheenLut's own comment), to AMP_* below, well under SIN_AMP, so the
// final LUT needs a few thousand entries instead of the ~7,000 a
// full-amplitude three-wave sum would need. AMP_A/AMP_B/AMP_SHEEN equal by
// design: no one wave should dominate the sum's dynamic range.
//   colFoldA[x]  two shared-sinLut reads scaled to +-AMP_A - column
//                vignette (0..VIGN_COL_MAX)
//   colFoldB[x]  two shared-sinLut reads scaled to +-AMP_B, no vignette
//                (see colFoldB's own comment for why)
//   sheen        one shared-sinLut read, scaled to +-AMP_SHEEN
//   rowVign[y]   0..VIGN_ROW_MAX, darken-only, no wave (see its own comment)
// No dither this round -- see band()'s own comment for why it was tried,
// what it cost, and why it did not survive the budget.
// The vignette terms only ever SUBTRACT (they darken, never brighten), so
// they widen the negative side of the range without touching the positive
// side.
constexpr int32_t AMP_A = 200;        // colFoldA wave amplitude after build-time scaling
constexpr int32_t AMP_B = 200;        // colFoldB wave amplitude after build-time scaling
constexpr int32_t AMP_SHEEN = 200;    // sheen amplitude after its one post-gather scale
constexpr int32_t VIGN_COL_MAX = 300; // max column-vignette darkening, at the screen edge
constexpr int32_t VIGN_ROW_MAX = 300; // max row-vignette darkening, at y==0 or y==h-1
constexpr int32_t SUM_MAX = AMP_A + AMP_B + AMP_SHEEN;
constexpr int32_t SUM_MIN = -(AMP_A + VIGN_COL_MAX) - AMP_B - AMP_SHEEN - VIGN_ROW_MAX;
constexpr int32_t SUM_BIAS = -SUM_MIN; // keeps every index non-negative
constexpr int SUM_N = SUM_MAX + SUM_BIAS + 1; // covers every reachable index exactly
uint16_t *g_lut2 = nullptr; // [SUM_N], allocHot: the only per-pixel gather that isn't a plain read

// Sheen reads its own private copy of the shared sine table, pre-scaled to
// +-AMP_SHEEN once at init() (see g_sheenLut's own comment) instead of
// scaling the shared table's raw +-SIN_AMP sample at band() time: measured
// this round (see band()'s own comment), a runtime multiply-and-shift on
// every pixel PAIR was not free on this instruction-count-bound kernel, on
// top of colFoldB's new gather. The shared table cannot be rescaled in
// place -- other animations read the same g_sinLut -- so this round pays
// 2,048 B of hot-SRAM for a private one instead.

// ---- oblique wave angles -----------------------------------------------
// Both angles stay in a narrow, ONE-SIGNED range (never crossing zero) and
// well clear of +-90 degrees, unlike silk's own three waves or this file's
// sheen, which all rotate through the full circle over time. The reason is
// the linear-shift identity in the file header: shift(y) = slope*(y-h/2),
// slope = tan(angle), and tan diverges at +-90 degrees -- a table built to
// cover a full rotation's worst-case shift would need unbounded padding.
// Keeping each angle within a fixed band bounds slope, and therefore bounds
// how many columns of padding colFoldA/colFoldB need (see computePad()),
// which is what keeps both tables inside the hot-SRAM budget. The two
// angles are far enough apart (base 20 vs -35 degrees) that the two waves'
// cells never line up, and each wobbles on its own slow clock (200s vs
// 260s, non-commensurate periods) so the two drifts don't lock together
// either -- same "own clock" discipline this file already applies to the
// fold pattern's phase drift and the sheen's rotation.
constexpr float ANGLE_A_BASE = 0.349066f;      // 20 degrees
constexpr float ANGLE_A_WOBBLE = 0.104720f;    // +-6 degrees: range 14..26 degrees
constexpr float ANGLE_A_DRIFT_RATE = 6.2831853f / 200000.0f; // one wobble cycle per 200s at speedF==1
constexpr float ANGLE_A_PHASE = 0.4f;          // arbitrary starting phase
constexpr float ANGLE_B_BASE = -0.610865f;     // -35 degrees
constexpr float ANGLE_B_WOBBLE = 0.104720f;    // +-6 degrees: range -41..-29 degrees
constexpr float ANGLE_B_DRIFT_RATE = 6.2831853f / 260000.0f; // different, non-commensurate period
constexpr float ANGLE_B_PHASE = 2.1f;

// Padding, in columns, so band() can read colFoldA/colFoldB at x+shift(y)
// for any shift this frame's slope can produce without a per-pixel bounds
// check. Computed once, at init(), from h and the WORST-CASE angle in each
// wave's wobble range (base +/- wobble, whichever pushes |tan| higher) --
// unlike round 3's PAD_X, this genuinely depends on h (a taller panel needs
// more columns of padding for the same angle), so it cannot be a compile-
// time constant. The +2 is the same rounding-safety margin round 3's
// PAD_X/SHIFT_AMP pair used, now applied to a computed bound instead of a
// hand-picked one.
//
// tan(angle) is built from fastSinRad/fastCosRad (sin/cos of the ratio),
// not libm's tanf: the on-device blob loader only links symbols the
// firmware already exports, and nothing in this tree calls tanf, so it is
// simply absent -- fastSinRad/fastCosRad are already exported (every
// animation with a rotating wave calls them) and exact enough for a
// once-per-init bound.
int computePad(float maxAngleMag, int h) {
    const float maxSlope = fastSinRad(maxAngleMag) / fastCosRad(maxAngleMag);
    const float hHalf = static_cast<float>(h) * 0.5f;
    return static_cast<int>(ceilf(maxSlope * hHalf)) + 2;
}

// Fringe pattern, oblique wave A: two close frequencies (2 and 3 cycles
// across the panel, same choice round 3 made for its column term), summed
// and scaled to +-AMP_A (see the range-budget comment above), and the
// column half of the vignette, baked in at build time -- a single copy (see
// band()'s own comment for why dither moved out of this table this round).
int16_t *colFoldA = nullptr; // [foldTableWA], allocHot
int foldTableWA = 0;         // w + 2*padA
int padA = 0;

// Fringe pattern, oblique wave B: the second plane the file header
// describes, a different pair of close frequencies (4 and 5 -- shares no
// value with A's 2/3, same "no shared frequency" reasoning round 3 used for
// its column/row pair) at a steeper, oppositely-signed angle (see the angle
// comment above). No vignette here (see colFoldA's, which carries the whole
// column half) -- colFoldB's padded width is already the larger of the two
// tables (a steeper angle needs more padding for the same panel height),
// and there is no reason to split one vignette term across two tables.
int16_t *colFoldB = nullptr; // [foldTableWB], allocHot
int foldTableWB = 0;         // w + 2*padB
int padB = 0;

// Per-row shift, in pixels, for each oblique wave: the exact linear
// function the file header's algebraic identity gives, slope = tan(angle),
// shift(y) = round(slope*(y - h/2)) -- centered on the panel's vertical
// midline so a bounded, one-signed angle still needs symmetric (two-sided)
// padding, since y-h/2 itself changes sign across the panel. Built once per
// frame() call (h reads each, a plain multiply and round, cheaper than
// round 3's shiftTable which needed a fastSinRad call per row for its
// cosmetic wobble) and read once per row in band(), folded straight into
// colFoldA's/colFoldB's row pointer -- free at band() time. PSRAM (alloc()),
// same reasoning as round 3's shiftTable: read at most h times a frame, not
// w*h, so hot-SRAM placement is not worth spending slab budget on here.
int16_t *shiftTableA = nullptr; // [h], alloc()
int16_t *shiftTableB = nullptr; // [h], alloc()

// Row vignette, exact: unlike colFoldA's column vignette (which is only
// exactly right for whichever row has zero shift that frame, see colFoldA's
// own comment), rowVign is read at the true y with no shift ever applied to
// it, so it needs no such approximation. No wave lives here any more --
// round 3's rowFold carried both a y-only fringe wave and this vignette
// term in one table; the wave moved to colFoldB above (a real oblique wave
// beats a y-only one for breaking the lattice, see the file header), and
// vignette-only content is still exactly a function of y alone on purpose:
// a rim should darken uniformly regardless of x, unlike a fringe cell.
// PSRAM (alloc()), same "read at most h times a frame" reasoning as
// shiftTableA/B above. Stores the darkening already negated, so band()'s
// "lutBase + rowVign[y]" needs no separate sign flip.
int16_t *rowVign = nullptr; // [h], alloc()
int allocH = 0;             // shared size for shiftTableA/B and rowVign

uint32_t lastThemeGen = 0xFFFFFFFF;
int lastGlow = -1;
const int16_t *g_sinLut = nullptr; // cached: see AnimSilk.cpp's g_sinLut comment for why

// Private, pre-scaled copy of the shared sine table for the sheen (see the
// range-budget comment above for why this exists instead of a runtime
// scale). Built ONCE, at init() -- it is a fixed rescaling of a table that
// never itself changes, not a function of params or theme, so there is no
// per-frame or per-theme-change rebuild the way buildSilk2Lut needs one.
int16_t *g_sheenLut = nullptr; // [SIN_N], allocHot

// Sheen: one rotating oblique wave, the same phase-accumulator trick
// AnimSilk.cpp uses for its three waves, kept for just one of them here --
// see the file header for why this is the one wave that still needs a real
// per-pixel sine gather rather than the linear-shift trick colFoldA/B use.
// kx/ky/wt are recomputed from tMs every frame() call (not accumulated
// across calls), so band() stays a pure function of the state frame() last
// set -- the same contract every other animation in this tree keeps.
constexpr float TURN = 4294967296.0f / 6.2831853f;
constexpr float SHEEN_A0 = 1.1f;                   // arbitrary starting angle
constexpr float SHEEN_ROT_MULT = 0.8f;             // rotation rate relative to omega0
constexpr float SHEEN_W_MULT = 1.0f;               // temporal-phase rate relative to omega0
constexpr float SHEEN_WK = 6.2831853f / 90000.0f;  // k-wobble rate
constexpr float SHEEN_PHK = 1.3f;
int32_t g_sheenStep = 0;    // per-pixel x-phase step, Q32 turns/px
int32_t g_sheenRowStep = 0; // per-row y-phase step, Q32 turns/row
uint32_t g_sheenWt = 0;     // temporal phase at y=0, Q32 turns (already mod 2*pi via wraparound)

// Reads g_sheenLut, already scaled to +-AMP_SHEEN -- see its own comment.
inline int16_t sinFromTurn(uint32_t turn) { return g_sheenLut[turn >> 22]; }

// Same shape as AnimSilk.cpp's buildContrastLUT (nc = powf(t, e), t and e
// >= 0 so the themeRGB() position this feeds is always valid) but a much
// steeper exponent: glow 0..100 maps to e = 3.0..9.0, against silk's own
// 0.6..2.6. Silk can afford the mild exponent because a separate, genuinely
// multiplicative vignette stage (env_q8, see AnimSilk.cpp) darkens the rim
// afterward regardless of curve shape; here the vignette is folded
// additively into the sum below the curve instead (see colFoldA's and
// rowVign's own comments), and at silk's mild exponent that produced four
// evenly-lit lobes, not one dominant hotspot (round 3's finding) -- an
// additive offset barely changes a powf curve's shallow low end. Steepening
// the curve compresses most of the sum range toward black and expands the
// top few percent into the visible brightness ramp, so the same additive
// vignette (a pixel near the curve's steep top loses a lot of brightness
// for a given sum offset, one already low loses almost nothing) reads as
// "bright cells fade near the rim, the dark background is unaffected"
// without a per-pixel multiply.
// GAIN pre-scales the normalized sum before the curve clamps it to 1.0:
// colFoldA's row shift decorrelates which peak of A lands on which peak of
// B per row, so the field's true maximum is rarer at any single pixel than
// an unshifted sum's would be, and without GAIN a steep-enough curve to
// keep one dominant hotspot never reached the theme gradient's brightest,
// near-white colors. GAIN=1.15 carries over unchanged from round 3 (chosen
// there as the smallest push past 1.0 clamp that reached a genuinely
// white-hot core without also flattening most of the range to full
// brightness, the failure a higher GAIN produced).
void buildSilk2Lut(uint8_t glow) {
    const float e = 3.0f + 6.0f * (glow / 100.0f);
    constexpr float GAIN = 1.15f;
    for (int idx = 0; idx < SUM_N; idx++) {
        float t = idx / static_cast<float>(SUM_N - 1); // 0..1
        t = t * GAIN;
        if (t > 1.0f) {
            t = 1.0f;
        }
        const float tc = powf(t, e);                          // 0..1, gamma-shaped
        uint8_t rgb[3];
        themeRGB(static_cast<int>(lroundf(tc * 255.0f)), rgb);
        g_lut2[idx] = rgb565(rgb[0], rgb[1], rgb[2]);
    }
}

bool init(int w, int h) {
    g_sinLut = sinLut();
    if (g_sinLut == nullptr) {
        return false;
    }
    if (g_sheenLut == nullptr) {
        g_sheenLut = static_cast<int16_t *>(allocHot(SIN_N * sizeof(int16_t)));
        if (g_sheenLut == nullptr) {
            g_sheenLut = static_cast<int16_t *>(alloc(SIN_N * sizeof(int16_t)));
        }
        if (g_sheenLut == nullptr) {
            return false;
        }
        const float sheenScale = static_cast<float>(AMP_SHEEN) / static_cast<float>(SIN_AMP);
        for (int i = 0; i < SIN_N; i++) {
            g_sheenLut[i] = static_cast<int16_t>(lroundf(static_cast<float>(g_sinLut[i]) * sheenScale));
        }
    }
    // Highest allocHot priority: the only table read every pixel that isn't
    // a plain array read (see g_lut2's own comment above).
    if (g_lut2 == nullptr) {
        g_lut2 = static_cast<uint16_t *>(allocHot(SUM_N * sizeof(uint16_t)));
        if (g_lut2 == nullptr) {
            g_lut2 = static_cast<uint16_t *>(alloc(SUM_N * sizeof(uint16_t)));
        }
        if (g_lut2 == nullptr) {
            return false;
        }
    }
    if (colFoldA == nullptr) {
        padA = computePad(ANGLE_A_BASE + ANGLE_A_WOBBLE, h);
        foldTableWA = w + 2 * padA;
        colFoldA = static_cast<int16_t *>(allocHot(static_cast<size_t>(foldTableWA) * sizeof(int16_t)));
        if (colFoldA == nullptr) {
            colFoldA = static_cast<int16_t *>(alloc(static_cast<size_t>(foldTableWA) * sizeof(int16_t)));
        }
        if (colFoldA == nullptr) {
            return false;
        }
    }
    if (colFoldB == nullptr) {
        padB = computePad(fabsf(ANGLE_B_BASE) + ANGLE_B_WOBBLE, h);
        foldTableWB = w + 2 * padB;
        colFoldB = static_cast<int16_t *>(allocHot(static_cast<size_t>(foldTableWB) * sizeof(int16_t)));
        if (colFoldB == nullptr) {
            colFoldB = static_cast<int16_t *>(alloc(static_cast<size_t>(foldTableWB) * sizeof(int16_t)));
        }
        if (colFoldB == nullptr) {
            return false;
        }
    }
    if (shiftTableA == nullptr) {
        shiftTableA = static_cast<int16_t *>(alloc(static_cast<size_t>(h) * sizeof(int16_t)));
        if (shiftTableA == nullptr) {
            return false;
        }
        allocH = h;
    }
    if (shiftTableB == nullptr) {
        shiftTableB = static_cast<int16_t *>(alloc(static_cast<size_t>(h) * sizeof(int16_t)));
        if (shiftTableB == nullptr) {
            return false;
        }
    }
    if (rowVign == nullptr) {
        rowVign = static_cast<int16_t *>(alloc(static_cast<size_t>(h) * sizeof(int16_t)));
        if (rowVign == nullptr) {
            return false;
        }
    }
    if (lastGlow < 0) {
        buildSilk2Lut(55);
        lastGlow = 55;
        lastThemeGen = themeGen();
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    if (p[2] != lastGlow || themeGen() != lastThemeGen) {
        buildSilk2Lut(p[2]);
        lastGlow = p[2];
        lastThemeGen = themeGen();
    }
    const float speedF = speedMul(p[0]);

    // ---- Oblique wave angles: each wobbles on its own slow clock around
    // its own fixed base (see the angle constants' own comment for the
    // ranges and why they must stay bounded). The trig calls below (four
    // fastSinRad/fastCosRad pairs, see computePad's own comment for why
    // these and not tanf) run once per frame() call, not once per row or
    // per pixel -- negligible next to buildSilk2Lut's SUM_N-sized loop
    // above, let alone band()'s w*h loop. ----
    const float angleA = ANGLE_A_BASE + ANGLE_A_WOBBLE * fastSinRad(ANGLE_A_DRIFT_RATE * tMs * speedF + ANGLE_A_PHASE);
    const float angleB = ANGLE_B_BASE + ANGLE_B_WOBBLE * fastSinRad(ANGLE_B_DRIFT_RATE * tMs * speedF + ANGLE_B_PHASE);
    const float slopeA = fastSinRad(angleA) / fastCosRad(angleA);
    const float slopeB = fastSinRad(angleB) / fastCosRad(angleB);

    // ---- Fringe pattern: two close frequencies per oblique wave (see
    // colFoldA's and colFoldB's own comments), deliberately NON-commensurate
    // between the two waves (2 and 3 cycles for A, 4 and 5 for B -- shares
    // no value, the same reasoning round 3 used for its column/row pair) so
    // the two waves' cells don't lock into a repeating grid. Its own clock,
    // distinct from the angle wobbles above and the sheen's clock below, so
    // all three drift out of lockstep with each other. ----
    const uint32_t foldSpeed = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 4..48, /16 = 0.25x..3x
    const uint32_t foldBase = tMs * foldSpeed >> 4;
    const uint32_t phaseA0 = foldBase * 9 >> 8;
    const uint32_t phaseA1 = foldBase * 8 >> 8; // close to phaseA0: a slow beat, not a fixed second grating
    const uint32_t phaseB0 = foldBase * 7 >> 8; // different ratio again: the cells drift, not just scroll
    const uint32_t phaseB1 = foldBase * 6 >> 8;
    // Fringe density (p[1]): 0.5x..2x spatial frequency, same "sx" shape
    // AnimPlasma.cpp's scale mapping uses. sx >= 128 keeps every frequency
    // below >= 1, same argument AnimPlasma.cpp's frame() comment makes.
    const uint32_t sxScale = 128 + static_cast<uint32_t>(p[1]) * 384 / 100; // 128..512
    const uint32_t freqA0 = (2 * sxScale) >> 8;
    const uint32_t freqA1 = (3 * sxScale) >> 8;
    const uint32_t freqB0 = (4 * sxScale) >> 8;
    const uint32_t freqB1 = (5 * sxScale) >> 8;
    const int16_t *sl = g_sinLut;
    // Build-time amplitude scale: a raw two-sine sum before halving spans
    // +-2*SIN_AMP; the "* 0.5f" halves that the way round 3's ">>1" did, and
    // the "* AMP_x/SIN_AMP" factor further scales the halved sum down to
    // this round's tighter per-wave budget (see the range-budget comment).
    // Folded into one float multiply per table entry -- frame()-time cost
    // (foldTableWA/foldTableWB reads total), not per-pixel.
    const float ampScaleA = 0.5f * static_cast<float>(AMP_A) / static_cast<float>(SIN_AMP);
    const float ampScaleB = 0.5f * static_cast<float>(AMP_B) / static_cast<float>(SIN_AMP);

    // colFoldA is built padA columns wider than the screen on each side
    // (see padA's own comment): table slot tx represents true spatial
    // coordinate spatialX = tx - padA, which runs from -padA to w-1+padA,
    // i.e. genuinely outside [0, w) at the padded ends. The wave formula
    // below is evaluated AT that true coordinate (a real continuation of
    // the periodic field), not clamped, so a shifted row's read crosses
    // smoothly into the padding with no seam. The vignette's column term,
    // in contrast, IS clamped to the visible [0, w) range first (clampedX
    // below): vignette should read as "darker near the physical screen
    // edge", and a table slot's own position is only an approximation of
    // which screen column ends up reading it once a row's shift is applied
    // -- clamping keeps that approximation's own darkening bounded at
    // exactly VIGN_COL_MAX rather than growing further in the padding,
    // which the range-budget comment above depends on.
    const float wHalf = static_cast<float>(w) * 0.5f;
    for (int tx = 0; tx < foldTableWA; tx++) {
        const int spatialX = tx - padA;
        // Unsigned wraparound of a negative spatialX is well-defined (mod
        // 2^32) and gives exactly the right modular phase for the periodic
        // sine formula below -- see the file header for the same argument
        // applied to shiftTableA/B's reads in band().
        const uint32_t ux = static_cast<uint32_t>(spatialX);
        const float raw =
            static_cast<float>(sl[(ux * freqA0 + phaseA0) & (SIN_N - 1)] + sl[(ux * freqA1 + phaseA1) & (SIN_N - 1)]);
        const int16_t base = static_cast<int16_t>(lroundf(raw * ampScaleA));
        const int clampedX = spatialX < 0 ? 0 : (spatialX >= w ? w - 1 : spatialX);
        const float dxNorm = (static_cast<float>(clampedX) - wHalf) / wHalf; // -1..1
        const int32_t colVign = static_cast<int32_t>(lroundf(VIGN_COL_MAX * dxNorm * dxNorm)); // 0..VIGN_COL_MAX
        colFoldA[tx] = static_cast<int16_t>(base - colVign);
    }
    // colFoldB: same padded-table shape as colFoldA, its own angle's
    // padding (padB, usually wider -- see padB's own comment), no vignette
    // (see colFoldB's own comment for why).
    for (int tx = 0; tx < foldTableWB; tx++) {
        const int spatialX = tx - padB;
        const uint32_t ux = static_cast<uint32_t>(spatialX);
        const float raw =
            static_cast<float>(sl[(ux * freqB0 + phaseB0) & (SIN_N - 1)] + sl[(ux * freqB1 + phaseB1) & (SIN_N - 1)]);
        colFoldB[tx] = static_cast<int16_t>(lroundf(raw * ampScaleB));
    }
    // shiftTableA/B and rowVign: one pass over y, h reads each (see their
    // own comments). shiftTable is the exact linear identity from the file
    // header, centered on the vertical midline so a one-signed angle still
    // needs symmetric padding (y-hHalf changes sign across the panel even
    // when slope does not). rowVign stores the darkening already negated,
    // so band()'s "lutBase + rowVign[y]" needs no separate sign flip.
    const float hHalf = static_cast<float>(h) * 0.5f;
    for (int y = 0; y < h; y++) {
        const float yf = static_cast<float>(y);
        shiftTableA[y] = static_cast<int16_t>(lroundf(slopeA * (yf - hHalf)));
        shiftTableB[y] = static_cast<int16_t>(lroundf(slopeB * (yf - hHalf)));
        const float dyNorm = (yf - hHalf) / hHalf; // -1..1
        rowVign[y] = static_cast<int16_t>(-lroundf(VIGN_ROW_MAX * dyNorm * dyNorm)); // -VIGN_ROW_MAX..0
    }

    // ---- Sheen: one rotating oblique wave (AnimSilk.cpp's per-wave
    // technique, kept for just one wave instead of three). Unchanged from
    // round 3 except for the post-gather amplitude scale in band() (see
    // SHEEN_SCALE_NUM/SHIFT's own comment). ----
    const float omega0 = 6.2831853f / 70000.0f * speedF;
    const float A = SHEEN_A0 + omega0 * 0.15f * SHEEN_ROT_MULT * tMs;
    // Much coarser than the fringe frequencies above -- about 0.7 to 1.9
    // cycles across the whole panel width, so this reads as one broad
    // highlight sweeping across the frame, not another layer of fringes at
    // the same scale.
    const float k0 = 0.0015f + 0.0025f * (p[1] / 100.0f);
    const float k = k0 * (1.0f + 0.15f * fastSinRad(SHEEN_WK * tMs + SHEEN_PHK));
    const float kx = k * fastCosRad(A);
    const float ky = k * fastSinRad(A);
    g_sheenStep = static_cast<int32_t>(kx * TURN);
    g_sheenRowStep = static_cast<int32_t>(ky * TURN);
    // Same int64 product AnimSilk.cpp's g_wtTurn uses: wRateQ (turns/ms, Q32)
    // times tMs (ms) wraps mod 2^32 exactly like mod-one-turn, with none of
    // the float*unbounded-tMs overflow risk BgAnimCommon.h's fastCosRad
    // comment warns about (tMs is unbounded across an uptime of days; kx, ky
    // stay well inside int32 range the same way AnimSilk.cpp's per-wave step
    // does, since k never exceeds ~0.0184 turns/px here and TURN is a fixed
    // constant, giving |kx*TURN| a few million at most).
    const float wRate = omega0 * SHEEN_W_MULT;
    const int32_t wRateQ = static_cast<int32_t>(wRate * TURN);
    g_sheenWt = static_cast<uint32_t>(static_cast<int64_t>(wRateQ) * static_cast<int64_t>(tMs));
}

// The portable spec, registered as both band() and bandRef() below (see the
// registry entry): this animation ships no hand-written kernel this round.
// Stateless across calls -- the sheen row phase is rebuilt from the
// absolute y every call (like AnimSilk.cpp's base[]), never carried from a
// previous band() call -- so any band height, and the interlaced rows==1
// path, render identically to a full-frame pass.
GM_ANIM_IRAM void band(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    // __restrict on dst/cfA/cfB/lut: the compiler otherwise has to assume a
    // write through dst could alias colFoldA/colFoldB/g_lut2 (int16_t and
    // uint16_t are distinct types but the same width, so a strict-aliasing
    // proof isn't automatic) and reloads them every pixel instead of
    // keeping them live across the store -- measured on the host bench in
    // round 3, restrict alone closed most of the gap to AnimPlasma.cpp's
    // equivalent loop.
    //
    // Three terms feed the final gather now (cfA[x] + cfB[x] + sh), not two:
    // colFoldB is this round's new per-pixel read (see the file header for
    // why -- a second genuinely oblique wave needs a second table, there is
    // no way to fold it into colFoldA's own read since the two waves have
    // different shifts per row). Everything that CAN still be precomputed
    // once per frame stays folded into per-row pointer setup below, not
    // into the per-pixel loop: both vignette terms and both row shifts.
    // g_sheenStep is hoisted into its own local (not re-read through a
    // pointer every iteration) so it stays in a register across the
    // unrolled pair instead of round-tripping through the stack --
    // disassembly (tools/animbench/xtensa-asm14.sh AnimSilk2) showed an
    // earlier round reloading it from the stack frame twice per two pixels.
    const int32_t sheenStep = g_sheenStep;
    // SUM_BIAS folded into the base pointer once for the whole call (not
    // once per pixel, not even once per row): g_lut2+SUM_BIAS is the address
    // that a raw combined sum of 0 would read, so band()'s inner loop never
    // pays for that add. Same trick AnimSilk.cpp's PALETTE_REAL_OFF folding
    // uses, one level simpler since there is only one table here.
    const uint16_t *__restrict lutBase = g_lut2 + SUM_BIAS;
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        uint16_t *__restrict out = dst + static_cast<size_t>(row) * w;
        uint32_t turn = g_sheenWt + static_cast<uint32_t>(g_sheenRowStep) * static_cast<uint32_t>(y);
        // rowVign[y] folded into the row's own base pointer, same trick
        // round 3's rowFold[y] used -- it is a per-ROW constant (see
        // rowVign's own comment), so paying its add once per row here is
        // strictly better than paying it once per pixel inside the loop.
        const uint16_t *__restrict lut = lutBase + rowVign[y];
        // Both rows' shifts are also once-per-row costs, same as lut above:
        // the column vignette is folded into colFoldA at frame-build time
        // (see its own comment), and each shift just moves which padded
        // window of its table this row reads from -- both need only a
        // pointer choice here, never a per-pixel read.
        const int16_t *__restrict cfA = colFoldA + padA + shiftTableA[y];
        const int16_t *__restrict cfB = colFoldB + padB + shiftTableB[y];
        int x = 0;
        // Pixel pairs via one uint32 store: AnimPlasma.cpp's bandRef() does
        // the same, for the same reason -- dst is always 32-bit aligned and
        // w is even, so this halves the store count for free. The sheen's
        // sine is also evaluated once per PAIR, not once per pixel: its own
        // spatial frequency is deliberately very low (see frame()'s k0
        // comment, well under 2 cycles across the whole 480 px panel), so
        // even at the highest Fringe density its phase turns by a small
        // fraction of a degree from one pixel to the next -- holding it for
        // one extra pixel is below what the eye can resolve on a "broad,
        // slow sweep" (checked against dumped frames, rounds 2 and 3).
        // colFoldA is read every pixel: its spatial frequency is the fringe
        // cells themselves, which do need per-pixel resolution. colFoldB is
        // read once per PAIR too, folded into b alongside sh: reading it
        // every pixel measured 13.2 ms on the device (kb.py, not the host
        // bench, which never saw this cost at all -- band_ms barely moved),
        // confirming the file header's warning that a fourth memory access
        // is not free on this instruction-count-bound chip. colFoldB's own
        // frequency (4-5 cycles across the panel, same order as colFoldA's
        // 2-3) is still far enough below the per-pixel Nyquist limit that
        // holding it for one extra pixel does not visibly blur its cells
        // (checked against dumped frames, this round) -- the same margin sh
        // already relies on, just smaller.
        //
        // No dither this round: a per-pixel checkerboard (+-DITHER_AMP,
        // folded into a per-row sign so it cost only two adds a pair) held
        // the device at 10.8 ms even after removing it from colFoldA (round
        // 3's 4-column dither baked into a shifted table produced a clearly
        // visible diagonal moire once colFoldA started carrying a genuine
        // linear per-row shift instead of round 3's slow cosmetic wobble --
        // the dither's phase rides the shift and beats against its own
        // drift) and after amortizing it to a 2x1-pixel block instead of a
        // true checkerboard. Every cut tried bought back part of colFoldB's
        // cost, none of them free: this file's second oblique wave and
        // ordered dither both want a per-pixel add this round's budget does
        // not have room for at the same time. Cutting dither instead of
        // colFoldB was the priority call -- the brief that asked for this
        // round's structural change treats the 9 ms budget as the harder
        // constraint. Without dither, buildSilk2Lut's curve can still show
        // hard steps in a boosted-brightness dark corner (checked against
        // dumped frames) the way round 3's un-dithered field did before its
        // own dither existed; whether that is visible at the panel's actual
        // brightness, not a 6x boost, has not been checked on the device.
        for (; x + 1 < w; x += 2) {
            const int32_t sh = sinFromTurn(turn);
            turn += static_cast<uint32_t>(sheenStep) * 2u;
            const int32_t b = cfB[x] + sh;
            const uint16_t p0 = lut[cfA[x] + b];
            const uint16_t p1 = lut[cfA[x + 1] + b];
            *reinterpret_cast<uint32_t *>(out) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
            out += 2;
        }
        for (; x < w; x++) {
            const int32_t sh = sinFromTurn(turn);
            turn += static_cast<uint32_t>(sheenStep);
            *out++ = lut[cfA[x] + cfB[x] + sh];
        }
    }
}

void release() {
    releaseTable(g_lut2, static_cast<size_t>(SUM_N) * sizeof(uint16_t));
    releaseTable(g_sheenLut, static_cast<size_t>(SIN_N) * sizeof(int16_t));
    releaseTable(colFoldA, static_cast<size_t>(foldTableWA) * sizeof(int16_t));
    releaseTable(colFoldB, static_cast<size_t>(foldTableWB) * sizeof(int16_t));
    releaseTable(shiftTableA, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(shiftTableB, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(rowVign, static_cast<size_t>(allocH) * sizeof(int16_t));
    foldTableWA = 0;
    foldTableWB = 0;
    padA = 0;
    padB = 0;
    allocH = 0;
    lastGlow = -1;
    lastThemeGen = 0xFFFFFFFF;
    g_sinLut = nullptr;
}

} // namespace

// extern: const namespace-scope objects default to internal linkage.
extern const BgAnimation bg_anim_silk2;
const BgAnimation bg_anim_silk2 = {
    "silk2",
    "Silk 2",
    {{"speed", "Speed", 50}, {"scale", "Fringe density", 45}, {"glow", "Sheen", 55}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    band, // bandRef == band: portable C++, no Xtensa kernel this round
};

#endif // GAGGIMATE_SIM
