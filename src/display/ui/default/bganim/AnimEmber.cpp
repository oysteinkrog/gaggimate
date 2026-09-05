#ifndef GAGGIMATE_SIM

// "Ember": a warm glow breathing from below screen center, like coals in a
// hearth. Three incommensurate breathing periods (11.3s/17.7s/6.1s) so the
// pattern never visibly repeats, plus optional edge-of-perception flicker
// from the shared tileable noise texture. The radial field is an incremental
// r^2 walk (two adds per pixel) into a radius LUT, so there is no sqrt
// anywhere in the per-pixel path.
//
// Palette is stored padded: paletteExt has PAD clamp entries on each side of
// the real 256-entry ramp, so the per-pixel combined index (radius plus
// dither/breathe plus flicker) can be used to index paletteExt directly with
// no clamp branch; the pad entries already hold the clamped edge colors.
// Range proof (worst-case params, p[1..3]=100): breathe in [-35,+35], dither
// in [-6,+6], flicker term in [-10,+9], radius in [72,255] (CORE_FLOOR=72 is
// a true floor: clamp8f(72 + r*scale) with r*scale always non-negative, for
// every glow/w/h this animation targets), so the combined index lands in
// [21,305], 285 distinct values. PAD=64 covers that with margin.
//
// radiusLUT is padded the same way on the high side (RPAD entries repeating
// radiusLUT[255]) so radiusLUT[ridx] needs no >255 clamp either; safe for
// this animation's fixed 480x480 target (the farthest corner from center
// gives r^2>>RSHIFT of about 244, inside 255+RPAD with margin to spare).
//
// Breathe is frame-constant, so instead of subtracting it from every pixel
// it is folded once per row into a small per-column tile alongside the
// dither term. Flicker is a per-pixel int multiply and shift; since
// flickerAmp is also frame-constant, it is folded into a 256-entry LUT
// (flickerLUT), rebuilt only when the flicker parameter changes, turning
// the multiply into a lookup. Whether flicker is on at all is decided once
// per band() call rather than once per pixel: the caller picks which source
// buffer feeds the combine stage instead of branching inside the loop.
//
// Design: anim-atmosphere (Fable), 2026-08-15. Optimized: opt-ember,
// 2026-08-15; row-precomputed flicker/dither term, 2026-08-30; hand-written
// Xtensa scalar kernels plus hot-slab table placement, 2026-09-04, five
// rounds ending at 20.45 ms kbench (git history carries the per-round
// measurements; this file no longer documents those kernels individually,
// since round 4 below replaces every one of them).
//
// Round 4 (PIE for the per-row half), 2026-09-05, design-worker (ember
// lane). The field term (radiusLUT[r2>>RSHIFT]) and the flicker term
// (flickerLUT[noise]) both depend only on a row pair's shared even row, so
// they are now computed once per pair, into fieldRow and flickerRow, by two
// small scalar kernels (emberFieldRow, emberFlickerFieldRow). Dither is not
// shared this way: BAYER8[y&7] genuinely differs for every real row, and an
// earlier row-doubling redesign that did duplicate dither across a pair read
// as coarser, directionally striped grain (duplicating only even rows shows
// only BAYER8 rows 0/2/4/6, which in this table all share the same column
// parity, reinforcing one vertical phase instead of the usual checkerboard),
// so dither is always rebuilt fresh per real row, into perColTile.
//
// A probe on the rig (kb.py, isolating each half of a row's cost) found the
// per-row half, the dither/flicker combine plus the final palette gather,
// cost about 12.2 of a 19.5 ms frame; that half is two small saturating
// adds and one gather per pixel, which is exactly the shape this chip's PIE
// vector unit does sixteen lanes at a time (the gather itself stays scalar:
// PIE has no vector gather on this chip). The combine became a PIE kernel,
// emberIdxRowPie, because it is exactly two saturating byte adds with no
// gather in it, the one piece of the old fused kernel that vectorizes
// cleanly; the gather that used to be fused with it moved into
// emberFinalizeRow, which is now gather-only. Four kernels do the whole
// per-row-pair-and-per-row job: emberFieldRow and emberFlickerFieldRow
// (scalar, once per pair), emberIdxRowPie (vector, once per real row), and
// emberFinalizeRow (scalar, once per real row).
//
// The combined index needs 285 distinct values, and a saturating byte add
// (ee.vadds.s8) has 256 representable outputs. The vector stage biases the
// field term (FIELD_BIAS) to center it near zero, adds flicker and then
// breathe/dither in that order (the order that keeps the one real clamp
// identical to clamping the true three-way sum once, proven at
// FIELD_BIAS's own comment), and XORs the sign bit to recover an unsigned
// palette offset. This costs 29 of the 285 true values (14 at the coolest
// corner, 15 at the hottest core, reachable only when glow, flicker and
// pulse are all near their limits at once); full derivation is at
// FIELD_BIAS and satAddS8, below.
//
// The negative floor is the textbook -128 on silicon, but this QEMU fork's
// model of ee.vadds.s8 floors at -127 (tools/qemubench/tests/probe_vadds_s8
// found it), and the first bandRef was written to the model. kbench at
// default parameters never drives the combined index into the clamp
// region, so the rig's bit-exact match against blobref did not exercise the
// edge; the production gate's animtest, which sweeps the parameter sets on
// the board, did: at the all-100 set two pixels in 5760 bands landed on the
// floor and read one palette entry apart (2026-09-05). satAddS8 below is
// written to the silicon; the QEMU ember test carries both floors so the
// emulator rung still passes and says which lanes took the model's.
//
// Call-shape rule: a row's field and flicker must depend only on its own
// absolute y, never on which other rows the same band() call happened to
// also request. Production's row-level interlace path (SleepAnimation.cpp)
// calls band() with rows==1, one row at a time, and also renders every
// other row in a parity-skipping pass without ever visiting the skipped row
// first, so a design that picked the pair's source row from the call's
// local offset would give one row a different field or flicker depending on
// whether it happened to be called with its partner or alone. Fixed by
// deriving fieldP = y & ~1 from each row's own y before looking at anything
// about the call, so a solitary row always gets exactly what a same-call
// pair would have given it.
//
// Measured on the device: kbench (kb.py, an isolated blob run) reads 13.60
// ms; the production A/B (useblob against the real running firmware, which
// also includes blending and pushing the frame to the panel, not just
// band()) reads 19.2 ms band time and 39 ms whole frame time, down from
// 29.6 ms and 58 ms before this round. An earlier, simpler redesign along
// the way, row-doubling (compute one row of a pair and duplicate it, the
// same technique also used in this codebase's lava kernel), measured 18.4
// ms in the same production A/B, comparable to this round's number, and was
// not shipped: it is the coarser, directionally striped dither described
// above, a real fidelity cost that the field-per-pair-plus-PIE design here
// avoids by keeping every row's own dither, for a similar production number.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

namespace {
using namespace bganim;

// DDS phase steps: full circle = 2^32, periods 11.3s / 17.7s / 6.1s. The
// breathing envelope is part of this animation's identity and is not touched
// by this round's design change.
constexpr uint32_t STEP1 = static_cast<uint32_t>(4294967296.0 / 11300.0);
constexpr uint32_t STEP2 = static_cast<uint32_t>(4294967296.0 / 17700.0);
constexpr uint32_t STEP3 = static_cast<uint32_t>(4294967296.0 / 6100.0);
constexpr uint32_t PHOFF2 = static_cast<uint32_t>(1.7 / 6.2831853 * 4294967296.0);
constexpr uint32_t PHOFF3 = static_cast<uint32_t>(4.2 / 6.2831853 * 4294967296.0);
constexpr int RSHIFT = 9; // r^2 -> radiusLUT bucket

constexpr int PAD = 64;
constexpr int PAL_EXT_N = 256 + 2 * PAD;
constexpr int RPAD = 64;
constexpr int RLUT_N = 256 + RPAD;

uint16_t *paletteExt = nullptr; // [PAL_EXT_N]; real ramp lives at paletteExt+PAD
uint16_t *palette = nullptr;    // = paletteExt + PAD, 256 entries, reversed theme ramp
int8_t *radiusLUT = nullptr;   // [RLUT_N]; r^2>>RSHIFT -> FIELD_BIAS-centered radius byte (see FIELD_BIAS)
int16_t *flickerLUT = nullptr;  // [256]; noise byte -> signed flicker contribution
const uint8_t *noise = nullptr;
int8_t *fieldRow = nullptr;    // [allocW]; radiusLUT[ridx] for the current row pair, shared by both rows, still FIELD_BIAS-centered
int8_t *flickerRow = nullptr;  // [allocW]; flickerLUT[noise] truncated to a byte (proven to fit, see file header), current row pair
int8_t *zeroFlickerRow = nullptr; // [allocW]; all zero, stands in for flickerRow when flicker is off (see band())
int8_t *perColTile = nullptr;  // [16]; this row's 8 dither/breathe values replicated twice for the PIE stage
uint8_t *idxRow = nullptr;     // [allocW]; emberIdxRowPie's output: the unsigned, ready-to-gather palette offset
int allocW = 0;
uint32_t lastThemeGen = 0xFFFFFFFF;
uint8_t lastGlow = 255;
uint8_t lastFlickerParam = 255;
int g_cx = 240, g_cy = 260;
float g_maxR = 353.7f;
int g_breathe = 0, g_flickerAmp = 0, g_sx = 0, g_sy = 0;

// Ramp is reversed so index 0 (brightest) lands at the glow's core, which
// sits at screen center where the UI puts its readouts; starting the ramp
// part way in (CORE_FLOOR) takes the peak off the text without changing the
// falloff shape.
constexpr int CORE_FLOOR = 72;

// Round 4's bias scheme. The true combined palette index (radiusLUT gather
// + perCol dither/breathe + flicker) lands in [21,305], 285 distinct
// values (see the file header's range proof), and ee.vadds.s8 has 256
// representable outputs, [-128,127]. A single byte-wide vector add of all
// three terms in one shot would wrap for the outermost values, so
// FIELD_BIAS re-centers radiusLUT's own [72,255] range on zero (163 is
// [21,305]'s own midpoint, which is what keeps the clamp region nearly
// symmetric too, see below) so it fits with room to spare ([-91,92]); the
// vector stage then adds the two small per-row/per-pair terms (flicker,
// then perCol) on top via ee.vadds.s8, which saturates instead of
// wrapping, and INDEX_UNBIAS undoes the same 163-centering (minus the 128
// that XORing the sign bit adds back, so 163-128=35) at the scalar
// gather's base pointer: palOffBiased = palOff + INDEX_UNBIAS reads the
// same palette entry palOff[trueIndex] would have, for every trueIndex the
// vector stage can represent losslessly. idx=0 is the coolest clamp
// (true sum at or below -128) and reads palOffBiased[0] = palOff[35], a
// valid entry.
//
// Order matters and is NOT swappable: the vector stage adds flicker first,
// then perCol, specifically because flicker is small enough that
// radiusLUT_biased + flicker spans [-101,101], strictly inside [-128,127]
// (checked, not assumed, same standard as the range proof itself), so
// that first add never saturates. Adding perCol first instead
// (radiusLUT_biased + perCol spans [-132,133], which DOES exceed that
// range) would let that first add clip some values the true three-way sum
// does not actually need clipped, and a second add afterward cannot undo a
// clip the first one already made: e.g. radiusLUT_biased=-91, perCol=-41,
// flicker=+9 sums to -123 (inside range, no clipping needed at all), but
// perCol-first computes sat(-91-41)=-128 then sat(-128+9)=-119, four away
// from the correct -123: wrong despite the true sum never leaving the
// representable range. Flicker-first avoids this because its own
// intermediate step provably never saturates, so the single saturating
// clip that remains (on the second add) is applied to the exact true sum,
// identical to clamping the sum once. That is what keeps the 29-value
// clamp region (14 values at the coolest corner where trueIndex <= 34, 15
// at the hottest core where trueIndex >= 291) the ONLY approximation in
// this scheme, not an additional, order-dependent one stacked on top of
// it: 285 true values minus 29 clamped is exactly 256, the vector stage's
// actual capacity, which is what makes this accounting self-checking
// rather than assumed. Those 29 values are only reachable when glow,
// flicker and pulse are all simultaneously at or near their extremes (the
// range proof's own worst case), and even there each clamp is a one-step
// ramp flattening, not a color jump.
constexpr int FIELD_BIAS = 163;  // subtracted from radiusLUT's raw [72,255] at build time
constexpr int INDEX_UNBIAS = 35; // FIELD_BIAS - 128; added to palOff to read the biased index back

// Sixteen copies of 0x80 for the PIE stage's sign<->unsigned conversion:
// XORing a saturated ee.vadds.s8 result's sign bit is bit-identical to
// adding 128 for any value already representable as a signed byte
// (two's-complement offset-binary), converting the signed saturated sum
// directly into the unsigned offset palOffBiased indexes with no scalar
// work.
alignas(16) constexpr uint8_t kIdxUnsignBias[16] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                                                     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};

void buildRadiusLut(uint8_t glow) {
    const float glowGain = 0.55f + 0.014f * glow;
    const float scale = 255.0f / (g_maxR * glowGain);
    for (int i = 0; i < 256; i++) {
        const float r = sqrtf(static_cast<float>(i << RSHIFT));
        radiusLUT[i] = static_cast<int8_t>(clamp8f(CORE_FLOOR + r * scale) - FIELD_BIAS);
    }
    for (int i = 256; i < RLUT_N; i++) {
        radiusLUT[i] = radiusLUT[255];
    }
}

void extendPalette() {
    const uint16_t lo = palette[0];
    const uint16_t hi = palette[255];
    for (int i = 0; i < PAD; i++) {
        paletteExt[i] = lo;
        paletteExt[PAD + 256 + i] = hi;
    }
}

void buildFlickerLut(int flickerAmp) {
    for (int i = 0; i < 256; i++) {
        // int16_t so the gather-side kernel can read it with a sign-
        // extending 16-bit load; see emberFlickerFieldRow's own header.
        flickerLUT[i] = static_cast<int16_t>(((i - 128) * flickerAmp) >> 7);
    }
}

void *allocHotOrPsram(size_t size) {
    void *p = allocHot(size);
    if (p == nullptr) {
        p = alloc(size);
    }
    return p;
}

bool init(int w, int h) {
    if (paletteExt == nullptr) {
        // Nine tables total: the four kept from before (paletteExt,
        // radiusLUT, flickerLUT, the borrowed noise texture) plus five new
        // ones for the round-4 split (fieldRow, flickerRow, idxRow: up to
        // 480 B each; perColTile: 16 B; zeroFlickerRow: up to 480 B), total
        // up to 3,536 B. Smaller than the previous round despite adding
        // buffers, since flickerRow and fieldRow both went from
        // wider/duplicate storage to packed bytes and the old combined-
        // index buffer disappeared entirely (see the round-4 header note).
        // Comfortably inside the 9,216 B hot slab this animation gets while
        // resident.
        paletteExt = static_cast<uint16_t *>(allocHotOrPsram(PAL_EXT_N * sizeof(uint16_t)));
        radiusLUT = static_cast<int8_t *>(allocHotOrPsram(RLUT_N));
        flickerLUT = static_cast<int16_t *>(allocHotOrPsram(256 * sizeof(int16_t)));
        noise = noiseTex256();
        fieldRow = static_cast<int8_t *>(allocHotOrPsram(static_cast<size_t>(w)));
        flickerRow = static_cast<int8_t *>(allocHotOrPsram(static_cast<size_t>(w)));
        zeroFlickerRow = static_cast<int8_t *>(allocHotOrPsram(static_cast<size_t>(w)));
        perColTile = static_cast<int8_t *>(allocHotOrPsram(16));
        idxRow = static_cast<uint8_t *>(allocHotOrPsram(static_cast<size_t>(w)));
        allocW = w;
        if (paletteExt == nullptr || radiusLUT == nullptr || flickerLUT == nullptr || noise == nullptr ||
            fieldRow == nullptr || flickerRow == nullptr || zeroFlickerRow == nullptr || perColTile == nullptr ||
            idxRow == nullptr) {
            return false;
        }
        palette = paletteExt + PAD;
        // zeroFlickerRow never changes after this: it stands in for
        // flickerRow whenever flicker is off, so emberIdxRowPie's vector
        // stage needs no flicker-off branch (see band()'s own comment).
        memset(zeroFlickerRow, 0, static_cast<size_t>(w));
    }
    g_cx = w / 2;
    g_cy = h / 2 + (20 * h) / 480;
    const float dx = static_cast<float>(g_cx);
    const float dy = static_cast<float>(g_cy > h - g_cy ? g_cy : h - g_cy);
    g_maxR = sqrtf(dx * dx + dy * dy);
    lastThemeGen = 0xFFFFFFFF;
    lastGlow = 255;
    lastFlickerParam = 255;
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildThemeRamp(palette, 256, /*reversed=*/true);
        extendPalette();
        lastThemeGen = themeGen();
    }
    if (p[1] != lastGlow) {
        buildRadiusLut(p[1]);
        lastGlow = p[1];
    }
    const float spd = speedMul(p[0]);
    const uint32_t vt = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(tMs) * spd));
    const float pulseGain = p[3] / 100.0f;
    const float s1 = sin1024((vt * STEP1) >> 22) * (1.0f / SIN_AMP);
    const float s2 = sin1024(((vt * STEP2) + PHOFF2) >> 22) * (1.0f / SIN_AMP);
    const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
    g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
    g_flickerAmp = (10 * p[2]) / 100;
    if (p[2] != lastFlickerParam) {
        buildFlickerLut(g_flickerAmp);
        lastFlickerParam = p[2];
    }
    g_sx = static_cast<int>((vt * 6u) >> 10) & 255;
    g_sy = static_cast<int>((vt * 4u) >> 10) & 255;
}

// Portable reference for the field/finalize design: the spec this file's own
// band() (device path) is checked against.
//
// A row's field and flicker must depend only on its own absolute y, never on
// which other rows the same band() call happened to also request
// (interlace_check.cpp; AnimLava.cpp hit the same integration bug and its
// own bandRef comment has the full story). Production's row-level interlace
// path (SleepAnimation.cpp) calls band() with rows==1, one row at a time,
// and a parity-skipping sequence that renders every other row in one pass
// without ever visiting the skipped row first: a design that decides "which
// row is the pair's source" from the call's local offset (r==0 within this
// call) gives row y a different field or flicker depending on whether it
// happened to be called alongside its partner or alone, which is exactly the
// bug this shape check exists to catch.
//
// Fixed by deriving the pair's source row, fieldP = y & ~1, from each row's
// own y before looking at anything about the call: fieldP is always the even
// row of y's pair, regardless of y0, rows, or what other rows this call also
// covers. cachedFieldP tracks which row fieldRow/flickerRow currently hold
// and is recomputed whenever a row's own fieldP differs from it, which means
// a solitary rows==1 call always recomputes (cachedFieldP starts unset every
// call) and gets exactly what a same-call pair would have given that row,
// while an ordinary ascending multi-row call still recomputes only once per
// pair, since consecutive rows share the same fieldP until it advances.
// Dither (BAYER8[y&7], all 8 rows in rotation, exactly as before this round,
// since every row still gets its own real dither) depends only on the row's
// own y already, so it needs no equivalent fix, and is deliberately NOT
// shared across the pair - see the file header for why the dither term is
// the one round 1 proved cannot be shared without banding.
// Reference for ee.vadds.s8: an 8-bit signed saturating add with the
// textbook [-128,127] range, which is what the silicon does. The board is
// the authority here, not the emulator: this QEMU fork's model of the
// instruction floors at -127 (tools/qemubench/tests/probe_vadds_s8, a
// 16-case boundary probe: every true sum <= -128 came back -127, the
// positive side and every in-range case exact), and the first version of
// this function was written to the model. Nothing on the rig could tell
// the two apart, because kbench at default parameters never drives the
// combined index into the clamp region; the production gate's animtest,
// which sweeps the parameter sets on the board, found two pixels at the
// all-100 set where band() (the PIE) read palette entry 35 and this
// function read 36 (2026-09-05). The QEMU ember test keeps both floors so
// the emulator rung still passes and reports which lanes took the model's.
inline int8_t satAddS8(int a, int b) {
    int s = a + b;
    if (s > 127) {
        s = 127;
    }
    if (s < -128) {
        s = -128;
    }
    return static_cast<int8_t>(s);
}

void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const bool doFlicker = g_flickerAmp != 0;
    const uint16_t *palOffBiased = paletteExt + PAD + INDEX_UNBIAS;
    int cachedFieldP = INT32_MIN; // nothing cached; forces the first row to compute
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        const int fieldP = y & ~1;
        if (fieldP != cachedFieldP) {
            const int dyP = fieldP - g_cy;
            const int dyP2 = dyP * dyP;
            for (int x = 0; x < w; x++) {
                const int dx = x - g_cx;
                fieldRow[x] = radiusLUT[(dx * dx + dyP2) >> RSHIFT];
            }
            if (doFlicker) {
                const uint8_t *noiseRow = noise + ((fieldP + g_sy) & 255) * 256;
                for (int x = 0; x < w; x++) {
                    flickerRow[x] = static_cast<int8_t>(flickerLUT[noiseRow[(x + g_sx) & 255]]);
                }
            }
            cachedFieldP = fieldP;
        }
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
        int8_t perCol[8];
        for (int j = 0; j < 8; j++) {
            perCol[j] = static_cast<int8_t>((static_cast<int>(bayerRow[j]) - 31) / 5 - g_breathe);
        }
        const int8_t *flickerSrc = doFlicker ? flickerRow : zeroFlickerRow;
        // Same two-step saturating order the PIE kernel uses (flicker
        // first, then perCol) and for the same reason: FIELD_BIAS's own
        // comment proves the first step never actually saturates, so the
        // one real clamp that remains lands on exactly the true three-way
        // sum, matching the device bit for bit.
        for (int x = 0; x < w; x++) {
            const int8_t step1 = satAddS8(fieldRow[x], flickerSrc[x]);
            const int8_t step2 = satAddS8(step1, perCol[x & 7]);
            const uint8_t idx = static_cast<uint8_t>(step2) ^ 0x80;
            row[x] = palOffBiased[idx];
        }
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)

// New this round: single-level scalar gather, fieldRow[x] =
// radiusLUT[r2>>RSHIFT], two pixels per iteration. Same incremental r^2 walk
// as the portable version (two adds per pixel, exact integer identity for
// (x-cx)^2, no per-pixel multiply) and the same two-pixel interleave shape
// already proven on this chip (see the round-4 header paragraph above),
// but only one gather level: no combined-index add, no second address
// computation, no second-level load. That is what
// buys this kernel its shorter per-pixel schedule (14 instructions / 2px =
// 7 cycles/pixel here, against emberGatherRow's 23 / 2px = 11.5) - it does
// half the memory work of the kernel it is modeled on.
//
// Schedule (one iteration, 2 pixels, %[ridx0]/%[ridx1] free after use):
//   1 srai ridx0 <- r2>>RSHIFT              (r2 for pixel 0)
//   2 add  r2 += ddx                        3 addi ddx += 2
//   4 srai ridx1 <- r2>>RSHIFT              (r2 for pixel 1)
//   5 add  r2 += ddx                        6 addi ddx += 2
//   7 add  ridx0 = &radiusLUT[ridx0]
//   8 l8ui ridx0 = radiusLUT[ridx0]         <- load
//   9 add  ridx1 = &radiusLUT[ridx1]         (gap for #8; independent)
//  10 l8ui ridx1 = radiusLUT[ridx1]         <- load
//  11 addi ddx += 0                          (gap for #10; harmless filler -
//                                             S16I's offset field is unsigned
//                                             [0,510] scaled by 2 on this
//                                             ISA, so the store below cannot
//                                             use a "wr-2" trick the way a
//                                             signed-offset ISA could; wr is
//                                             advanced only after its store,
//                                             so this slot needs a real,
//                                             independent filler instead)
//  12 slli ridx1 <<= 8                       (ridx1 loaded at #10, one-instruction
//                                             gap via #11: safe, same minimum
//                                             gap this file's other kernels use)
//  13 or   ridx0 |= ridx1                    (ridx0 loaded at #8, four-instruction
//                                             gap: safe; ALU-to-ALU with ridx1)
//  14 s16i [wr] = ridx0                      (pack two bytes into one halfword
//                                             store, the same store-pairing
//                                             trick this file's other gather
//                                             kernels use)
//  15 addi wr += 2
// No alignment requirement beyond the store's own element size (a 16-bit
// store wants 2-byte alignment): wr starts at a hot-slab allocation and
// advances by exactly 2 bytes per iteration, so it stays 2-byte aligned as
// long as the allocator itself returns an aligned pointer, the same
// assumption every other kernel in this file already makes for its own
// output buffer.
__attribute__((noinline)) static void GM_ANIM_IRAM emberFieldRow(int8_t *__restrict out,
                                                                   const int8_t *__restrict radiusLUTIn, int r2_0,
                                                                   int ddx_0, int wPairs) {
    int r2 = r2_0;
    int ddx = ddx_0;
    int8_t *wr = out;
    int ridx0, ridx1;
    asm volatile("loopnez %[n], 2f\n"
                 "srai   %[ridx0], %[r2], %[rshift]\n"
                 "add    %[r2], %[r2], %[ddx]\n"
                 "addi   %[ddx], %[ddx], 2\n"
                 "srai   %[ridx1], %[r2], %[rshift]\n"
                 "add    %[r2], %[r2], %[ddx]\n"
                 "addi   %[ddx], %[ddx], 2\n"
                 "add    %[ridx0], %[rlut], %[ridx0]\n"
                 "l8ui   %[ridx0], %[ridx0], 0\n"
                 "add    %[ridx1], %[rlut], %[ridx1]\n"
                 "l8ui   %[ridx1], %[ridx1], 0\n"
                 "addi   %[ddx], %[ddx], 0\n"
                 "slli   %[ridx1], %[ridx1], 8\n"
                 "or     %[ridx0], %[ridx0], %[ridx1]\n"
                 "s16i   %[ridx0], %[wr], 0\n"
                 "addi   %[wr], %[wr], 2\n"
                 "2:\n"
                 : [r2] "+r"(r2), [ddx] "+r"(ddx), [wr] "+r"(wr), [ridx0] "=&r"(ridx0), [ridx1] "=&r"(ridx1)
                 : [n] "r"(wPairs), [rlut] "r"(radiusLUTIn), [rshift] "i"(RSHIFT)
                 : "memory");
}

// New this round: single-level scalar gather for the flicker term alone,
// flickerRow[x] = flickerLUT[noiseRow[(x+gsx)&255]], two pixels per
// iteration - the noise-gather half of the old fused emberFlickerRow, with
// the perCol/dither add removed (that add is what round 1's fix proved must
// stay per real row; see the file header). Removing it does not just delete
// two instructions, it removes the natural filler those instructions
// supplied: the old kernel's "add idx0 = idx0+pc0" (an ALU op reading idx0
// after its flickerLUT load) doubled as the load-use gap before idx1's
// flickerLUT result was itself read. Without it the two stores below - not
// two adds - take over that role: storing idx0 first is what gives idx1's
// load its required one-instruction gap before it is stored second, so the
// store order here is load-bearing, not incidental.
//
// This kernel runs once per row PAIR instead of once per real row, which is
// the flicker half of round 3's optional lever: flicker draws from the
// shared noise texture, a temporal source with no fixed 8-row phase, so
// sharing its draw 1x2 does not reproduce round 1's Bayer-phase stripe
// (BAYER8[y&7] is deliberately NOT sourced from here - dither stays a
// per-real-row term built in band() below, exactly as before this round).
//
// Round 4: the output store truncates to a byte instead of a halfword
// (s8i, not s16i). Safe by the same modular-arithmetic-truncation
// principle this file's l16ui-then-truncate reads already relied on: the
// true flicker value is proven to fit in int8 ([-10,+9], see the file
// header's range proof), so keeping only its low 8 bits after the l16si
// load and truncating store is numerically exact, not an approximation.
// The store order (idx0 then idx1) is unchanged from round 3: see the
// original comment above for why it, not an extra filler, supplies idx1's
// load-to-use gap.
__attribute__((noinline)) static void GM_ANIM_IRAM emberFlickerFieldRow(int8_t *__restrict flickerRowOut,
                                                                          const uint8_t *__restrict noiseRowIn,
                                                                          const int16_t *__restrict flickerLUTIn,
                                                                          int gsxIn, int wPairs) {
    int ni = gsxIn;
    int8_t *cr = flickerRowOut;
    int idx0, idx1;
    asm volatile("loopnez %[n], 2f\n"
                 "extui  %[idx0], %[ni], 0, 8\n"
                 "addi   %[ni], %[ni], 1\n"
                 "extui  %[idx1], %[ni], 0, 8\n"
                 "addi   %[ni], %[ni], 1\n"
                 "add    %[idx0], %[noiseRow], %[idx0]\n"
                 "l8ui   %[idx0], %[idx0], 0\n"
                 "add    %[idx1], %[noiseRow], %[idx1]\n"
                 "l8ui   %[idx1], %[idx1], 0\n"
                 "addx2  %[idx0], %[idx0], %[flk]\n"
                 "l16si  %[idx0], %[idx0], 0\n"
                 "addx2  %[idx1], %[idx1], %[flk]\n"
                 "l16si  %[idx1], %[idx1], 0\n"
                 "s8i    %[idx0], %[cr], 0\n"
                 "s8i    %[idx1], %[cr], 1\n"
                 "addi   %[cr], %[cr], 2\n"
                 "2:\n"
                 : [ni] "+r"(ni), [cr] "+r"(cr), [idx0] "=&r"(idx0), [idx1] "=&r"(idx1)
                 : [n] "r"(wPairs), [noiseRow] "r"(noiseRowIn), [flk] "r"(flickerLUTIn)
                 : "memory");
}

// New this round: the PIE stage. idxRow[x] = unsign(sat(sat(fieldRow[x] +
// flickerSrc[x]) + perColTile[x&15])), sixteen pixels per group on the
// ESP32-S3's PIE vector unit; see FIELD_BIAS's own comment for the bias
// derivation and for why the add order (flicker first, then perCol) is the
// one that keeps the single remaining saturation identical to clamping the
// true three-way sum once. This replaces BOTH of round 3's per-real-row
// costs (the emberCombineRow scalar-ish combine and the add half of
// emberFinalizeRow's gather) with one pass: there is no vector gather on
// this chip (ASM_BRIEF.md is explicit, and this file's own field/flicker
// kernels above already work around the same fact), so the palette lookup
// itself stays a separate scalar tail (the new, address-only
// emberFinalizeRow below), but everything upstream of that lookup, both
// adds per pixel, is now vector work.
//
// flickerSrc is fieldRow's per-pair partner when flicker is on, or
// zeroFlickerRow (all zero, filled once at init and never touched again)
// when it is off: adding zero is exact (never saturates, see FIELD_BIAS's
// comment), so this kernel needs no flicker-off branch of its own: the
// caller in band() picks the source pointer, not the kernel.
//
// perColTile is the row's own 8 dither/breathe values replicated twice
// (period 8 into a 16-lane group), rebuilt fresh per real row; see
// band()'s own comment for why dither cannot be shared across a row pair
// the way field and flicker are (round 1's fix).
//
// Register map: q7 = the resident 0x80 bias broadcast (loaded once,
// constant for the animation's whole lifetime, see kIdxUnsignBias), q6 =
// the resident perCol tile (loaded once per real row, constant for that
// row's whole width), q0/q1 = the two per-group loads. Four of eight q
// registers used, no spills; PIE is coprocessor CP3, thread context only,
// and the compiler never allocates q registers, so this needs no clobber
// list for them (same fact AnimNebula.cpp's own PIE kernels rely on).
//
// Manual counted loop (addi/bnez), not loopnez: every existing PIE kernel
// in this codebase (SleepAnimation.cpp's scale565Oct, AnimNebula.cpp's
// lerpRowPie/lerpShiftRowPie/nebulaFieldPie) uses this form and none uses
// loopnez with EE.* instructions in the body, so this kernel matches that
// precedent rather than assuming an untested combination works; loopnez
// stays for this file's own pure-scalar kernels above and below, which
// already run this way on the device.
//
// fieldIn, flickerSrcIn, idxOut must be 16-byte aligned (ee.vld.128.ip and
// ee.vst.128.ip mask, not trap on, the low four address bits); band()
// checks this once per call, the same defensive pattern AnimNebula.cpp
// uses for its own hot tables. perColTileIn and biasTileIn are always
// allocHot/static-const and 16-byte aligned by construction.
__attribute__((noinline)) static void GM_ANIM_IRAM emberIdxRowPie(uint8_t *__restrict idxOut,
                                                                     const int8_t *__restrict fieldIn,
                                                                     const int8_t *__restrict flickerSrcIn,
                                                                     const int8_t *__restrict perColTileIn,
                                                                     const uint8_t *__restrict biasTileIn,
                                                                     int wSixteens) {
    const int8_t *fr = fieldIn;
    const int8_t *lr = flickerSrcIn;
    uint8_t *wr = idxOut;
    int n = wSixteens;
    asm volatile("ee.vld.128.ip q7, %[bias], 0\n" // q7 = 0x80 x16, resident whole animation
                 "ee.vld.128.ip q6, %[pct], 0\n"  // q6 = perCol tile x16, resident this row
                 "1:\n"
                 "ee.vld.128.ip q0, %[fr], 16\n" // fieldRow[0..15], FIELD_BIAS-centered
                 "ee.vld.128.ip q1, %[lr], 16\n" // flicker source[0..15]
                 "ee.vadds.s8 q0, q0, q1\n"      // step1 = field + flicker (exact, never saturates)
                 "ee.vadds.s8 q0, q0, q6\n"      // step2 = step1 + perCol (the one real clamp)
                 "ee.xorq q0, q0, q7\n"          // unsigned offset = step2 + 128
                 "ee.vst.128.ip q0, %[wr], 16\n"
                 "addi %[n], %[n], -1\n"
                 "bnez %[n], 1b\n"
                 : [fr] "+r"(fr), [lr] "+r"(lr), [wr] "+r"(wr), [n] "+r"(n), [bias] "+r"(biasTileIn),
                   [pct] "+r"(perColTileIn)
                 :
                 : "memory");
}

// Simplified this round: the palette gather is now the ONLY thing this
// kernel does: emberIdxRowPie above already computed the ready-to-use,
// unsigned offset, so there is no add left here, unlike round 3's fused
// add-then-gather. fieldRow[x] and combRow[x] (round 3's two inputs) are
// gone; idxRow[x] alone drives the lookup.
//
// Schedule (one iteration, 2 pixels):
//   1 l8ui  i0 <- idxRow[0]
//   2 l8ui  i1 <- idxRow[1]                   (independent of #1's result)
//   3 addx2 a0 = &palOffBiased[i0]            (i0 from #1: 1-instr gap, safe)
//   4 l16ui i0 = palOffBiased[i0]             <- load (color0), overwrites i0
//   5 addx2 a1 = &palOffBiased[i1]            (i1 from #2: 2-instr gap, safe)
//   6 l16ui i1 = palOffBiased[i1]             <- load (color1), overwrites i1
//   7 addi  ir += 2                           (gap for #6; independent)
//   8 slli  i1 <<= 16                         (i1/color1 from #6: 1-instr gap
//                                               via #7: safe)
//   9 or    i0 |= i1                          (i0/color0 from #4: 4-instr gap
//                                               via #5,#6,#7,#8: safe;
//                                               ALU-to-ALU with i1)
//  10 s32i  [wr] = i0
//  11 addi  wr += 4
// 11 instructions / 2px = 5.5 cycles/pixel, zero stalls, down from round
// 3's 16 instructions / 2px (8 cycles/pixel) now that the add and its
// address arithmetic moved into the PIE stage. w is always a multiple of
// 16 here (band()'s own guard below, tighter than round 3's %8 since the
// PIE stage needs full 16-lane groups), so wPairs has no remainder.
__attribute__((noinline)) static void GM_ANIM_IRAM emberFinalizeRow(uint16_t *__restrict row,
                                                                      const uint16_t *__restrict palOffBiased,
                                                                      const uint8_t *__restrict idxRowIn,
                                                                      int wPairs) {
    const uint8_t *ir = idxRowIn;
    uint16_t *wr = row;
    int i0, i1, a0, a1;
    asm volatile("loopnez %[n], 2f\n"
                 "l8ui   %[i0], %[ir], 0\n"
                 "l8ui   %[i1], %[ir], 1\n"
                 "addx2  %[a0], %[i0], %[pal]\n"
                 "l16ui  %[i0], %[a0], 0\n"
                 "addx2  %[a1], %[i1], %[pal]\n"
                 "l16ui  %[i1], %[a1], 0\n"
                 "addi   %[ir], %[ir], 2\n"
                 "slli   %[i1], %[i1], 16\n"
                 "or     %[i0], %[i0], %[i1]\n"
                 "s32i   %[i0], %[wr], 0\n"
                 "addi   %[wr], %[wr], 4\n"
                 "2:\n"
                 : [i0] "=&r"(i0), [i1] "=&r"(i1), [a0] "=&r"(a0), [a1] "=&r"(a1), [ir] "+r"(ir), [wr] "+r"(wr)
                 : [n] "r"(wPairs), [pal] "r"(palOffBiased)
                 : "memory");
}

#endif // __XTENSA__ && !GM_BGANIM_NO_ASM

// band(): on real hardware, computes fieldRow and (when flicker is on)
// flickerRow once per row pair (both asm, both keyed by the same fieldP so
// the call-shape fix covers both together), then per real row builds that
// row's own dither/breathe pattern (perColTile, replicated to 16 lanes),
// runs the PIE stage (emberIdxRowPie: both adds, sixteen pixels at a time
// (see its own header for the bias derivation) and the now-gather-only
// finalize kernel. Off-Xtensa (host bench, sim, this file's own
// render/golden path) falls straight to bandRef, which implements the
// same split with plain per-pixel loops and the identical saturating-clamp
// order.
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    if (w % 16 != 0) {
        // Never happens on the real device (w is always 240 or 480, both
        // multiples of 16). Defensive fallback only, tighter than round
        // 3's %8 guard: the PIE stage now needs full 16-lane groups, not
        // just 8-wide scalar unrolls.
        bandRef(dst, y0, rows, w, tMs, p);
        return;
    }
    // allocHot()'s bump allocator only guarantees 16-byte alignment for
    // pointers actually carved from the slab; a request that overflows the
    // slab falls back to alloc() (PSRAM), which does not promise 16-byte
    // alignment. ee.vld.128.ip/ee.vst.128.ip mask (not trap on) a
    // misaligned address, so a silent fallback would corrupt output rather
    // than crash. Checked once per band() call, same pattern
    // AnimNebula.cpp uses for its own hot tables.
    const bool hotAligned = (reinterpret_cast<uintptr_t>(fieldRow) & 15) == 0 &&
                             (reinterpret_cast<uintptr_t>(flickerRow) & 15) == 0 &&
                             (reinterpret_cast<uintptr_t>(zeroFlickerRow) & 15) == 0 &&
                             (reinterpret_cast<uintptr_t>(perColTile) & 15) == 0 &&
                             (reinterpret_cast<uintptr_t>(idxRow) & 15) == 0;
    if (!hotAligned) {
        bandRef(dst, y0, rows, w, tMs, p);
        return;
    }
    const bool doFlicker = g_flickerAmp != 0;
    const uint16_t *palOffBiased = paletteExt + PAD + INDEX_UNBIAS;
    const int wPairs = w >> 1;
    const int wSixteens = w >> 4;

    // Shape-invariant field+flicker: see bandRef's header. fieldP = y & ~1
    // depends only on this row's own absolute y, never on r or y0, so a
    // rows==1 call (production's row-level interlace path) or a parity-
    // skipping sequence gets exactly what a same-call pair would have given
    // that row. cachedFieldP resets every band() call, so a solitary row
    // always recomputes; an ordinary ascending multi-row call still
    // recomputes only once per pair, since consecutive rows share fieldP
    // until it advances.
    int cachedFieldP = INT32_MIN;
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        const int fieldP = y & ~1;
        if (fieldP != cachedFieldP) {
            const int dxTop0 = -g_cx;
            const int dyP = fieldP - g_cy;
            const int r2_0 = dxTop0 * dxTop0 + dyP * dyP;
            const int ddx_0 = 2 * dxTop0 + 1;
            emberFieldRow(fieldRow, radiusLUT, r2_0, ddx_0, wPairs);
            if (doFlicker) {
                const uint8_t *noiseRow = noise + ((fieldP + g_sy) & 255) * 256;
                emberFlickerFieldRow(flickerRow, noiseRow, flickerLUT, g_sx, wPairs);
            }
            cachedFieldP = fieldP;
        }
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
        for (int j = 0; j < 8; j++) {
            const int8_t v = static_cast<int8_t>((static_cast<int>(bayerRow[j]) - 31) / 5 - g_breathe);
            perColTile[j] = v;
            perColTile[j + 8] = v;
        }
        // PIE stage: both adds, per pixel, sixteen at a time; see
        // emberIdxRowPie's own header for the bias/order derivation. Picks
        // flickerRow when flicker is on, zeroFlickerRow (all zero, exact)
        // when it is off, so the kernel itself needs no flicker-off branch.
        emberIdxRowPie(idxRow, fieldRow, doFlicker ? flickerRow : zeroFlickerRow, perColTile, kIdxUnsignBias,
                       wSixteens);
        emberFinalizeRow(row, palOffBiased, idxRow, wPairs);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(paletteExt, PAL_EXT_N * sizeof(uint16_t));
    palette = nullptr;
    releaseTable(radiusLUT, RLUT_N);
    releaseTable(flickerLUT, 256 * sizeof(int16_t));
    releaseTable(fieldRow, static_cast<size_t>(allocW));
    releaseTable(flickerRow, static_cast<size_t>(allocW));
    releaseTable(zeroFlickerRow, static_cast<size_t>(allocW));
    releaseTable(perColTile, 16);
    releaseTable(idxRow, static_cast<size_t>(allocW));
    noise = nullptr;
    lastThemeGen = 0xFFFFFFFF;
    lastGlow = 255;
    lastFlickerParam = 255;
}

} // namespace

extern const BgAnimation bg_anim_ember;
const BgAnimation bg_anim_ember = {
    "ember",
    "Ember",
    {{"speed", "Speed", 50}, {"glow", "Glow size", 45}, {"flicker", "Flicker", 20}, {"pulse", "Pulse", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
