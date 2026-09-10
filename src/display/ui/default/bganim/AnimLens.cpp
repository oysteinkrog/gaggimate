#ifndef GAGGIMATE_SIM

// "Lens": a round magnifying glass wandering across a dim, scrolling mottle.
// This is entry 'lens' in tools/animbench/web/anim_bench.html, including its
// softened five-run edge: ground, feather, lens, feather, ground. The 64x64
// tile has one broad cycle and two crossed third harmonics at quarter weight.
// The executable preview uses >>2 and 84 + round(v*0.0410); its header's
// eighth-weight/78-step description predates that arithmetic.
//
// Coordinates match the page's 480-pixel design space, including its fixed
// centre (240,240). Width clips runs and height selects rows, as on the page.
// Unsigned Q16.16 texture cursors wrap exactly like JavaScript's |0 cursors:
// 32768 per ground pixel, 16384 per lens pixel, hence 2x magnification.
// The 12 px feather uses squared radius, advanced by 2*dx+1, and a Q24
// reciprocal of R*R-RI*RI to produce Q8 alpha. Its quartic darkening is a
// multiplier, not a flat outline. There is no dither in this page entry.
//
// All per-pixel and per-row tables request the fixed hot slab. Texture
// levels and the theme ramp are only swept at init/theme/brightness changes,
// in PSRAM. Byte chords retain the page's full 160 entries: radii never
// exceed 130, so narrowing loses no precision and fits the slab budget.
//
// Parameter pass (2026-09-10, gm-3vj.31): five more sliders, taking this
// animation from 3 to 8. None of them touches a pixel loop. Contrast and rim
// darkness change the bytes in two tables that were built once and are now
// rebuilt when their slider moves; edge width joins lens size in the block
// that rebuilds the chords; lens travel and ground drift are per-frame
// constants. So bandRef and the hand-written Xtensa kernel below are byte for
// byte what they were, and paramSpan() returns its middle argument as a
// literal at 50, which is what makes the default picture the old picture
// rather than a hope about float rounding.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

// On by default, with a portable reference for the device parity/timing A/B.
// Host and QEMU checks establish correctness; speed still needs the device.
#ifndef GM_BGANIM_LENS_ASM
#define GM_BGANIM_LENS_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int TILE_N = 64;
constexpr int TILE_PIXELS = TILE_N * TILE_N;
constexpr int CHORD_N = 160;
constexpr int FEATHER = 12; // the edge-width slider's midpoint, in pixels
constexpr uint32_t GROUND_STEP = 32768;
constexpr uint32_t LENS_STEP = 16384;

uint8_t *levels = nullptr;     // 4096 B, PSRAM, read on palette rebuild only
uint16_t *ramp = nullptr;      // 512 B, PSRAM, read on palette rebuild only
uint16_t *texture = nullptr;   // 8192 B, slab, RGB565 texture gathers
uint16_t *rimMul = nullptr;    // 514 B (528 aligned), slab, Q8 darkening
uint8_t *chordO = nullptr;     // 160 B, slab, outer half chords
uint8_t *chordI = nullptr;     // 160 B, slab, inner half chords
uint16_t *pieConst = nullptr;  // 112 B, slab, seven broadcast constants
uint16_t *stage8 = nullptr;    // 64 B, slab, current feather's eight pixels

// Slab: 8192 + align16(514) + 160 + 160 + 112 + 64 = 9216 of 9216 B.
// PSRAM: 4096 + 512 = 4608 B. The shared sine table uses the separate
// 3072 B shared reserve. stage8 is overwritten completely before each use;
// keeping it here avoids traffic to the render task's PSRAM stack. No cache
// of pixels or row state survives a band call, and there are no static tables.
static_assert(8192 + 528 + 160 + 160 + 112 + 64 <= HOT_SLAB_BYTES - HOT_SHARED_RESERVE,
              "Lens tables must fit the resident animation's hot slab");

int lastBrightness = -1;
// lastSize keys the chord block on lens size and edge width together.
int lastSize = -1;
// The two table sliders start where init() builds their table, so a device
// left at the defaults never pays for a rebuild.
int lastContrast = 50;
int lastRim = 50;
uint32_t lastThemeGen = 0xFFFFFFFF;
int feather = FEATHER;
int radius = 0, innerRadius = 0, inner2 = 0, span = 1, invSpan = 0;
int lensX = 240, lensY = 240, scrollX = 0, scrollY = 0;

void release();

// Math.round ties toward +infinity; lroundf would move negative ties away
// from zero. No per-pixel float operations use this helper.
int roundJS(float v) { return static_cast<int>(floorf(v + 0.5f)); }

// A 0-100 slider as a piecewise linear value with an exact midpoint: lo at
// 0, mid at 50, hi at 100. At 50 it returns the mid literal itself rather
// than an expression that happens to round to it, so a default slider folds
// away to the constant this file hard-coded before. Called a few times per
// frame at most, never per row and never per pixel.
inline float paramSpan(uint8_t v, float lo, float mid, float hi) {
    const int d = static_cast<int>(v) - 50;
    if (d == 0) {
        return mid;
    }
    const float f = d / 50.0f;
    return d < 0 ? mid + (mid - lo) * f : mid + (hi - mid) * f;
}

// The same curve as a whole number, for the sliders that set a pixel count
// or a Q8 amount. Rounded the way the page rounds, through roundJS.
inline int paramSpanI(uint8_t v, int lo, int mid, int hi) {
    if (static_cast<int>(v) == 50) {
        return mid;
    }
    return roundJS(paramSpan(v, static_cast<float>(lo), static_cast<float>(mid), static_cast<float>(hi)));
}

// The mottle tile. Contrast is a gain on the summed waves before the level
// offset, so at slider 50 it is a multiply by a literal 1.0f and the bytes
// are the bytes this loop wrote before. Levels feed the texture, so a
// rebuild here has to invalidate the texture as well; frame() does that.
void buildLevels(uint8_t contrastParam) {
    const int16_t *sl = sinLut();
    if (sl == nullptr || levels == nullptr) {
        return;
    }
    const float contrast = paramSpan(contrastParam, 0.25f, 1.0f, 2.4f);
    for (int j = 0; j < TILE_N; j++) {
        for (int i = 0; i < TILE_N; i++) {
            // 16 LUT entries is one cycle per tile, 48 is three. The phase
            // offsets 180, 300 and 700 keep the crossed waves asymmetric.
            const int v = sl[(i * 16) & 1023] + sl[(j * 16 + 180) & 1023] +
                          (sl[(i * 48 + j * 16 + 300) & 1023] >> 2) +
                          (sl[(i * 16 - j * 48 + 700) & 1023] >> 2);
            const int idx = 84 + roundJS(static_cast<float>(v) * 0.0410f * contrast);
            levels[j * TILE_N + i] = static_cast<uint8_t>(idx < 0 ? 0 : (idx > 255 ? 255 : idx));
        }
    }
}

// The rim, as a darkening across the feather rather than a run of one dark
// colour. Depth is the Q8 amount taken off at the middle of the feather: 70
// at slider 50, which is the number this loop hard-coded. Nothing reads
// rimMul but the blend, so this table stands alone.
void buildRim(uint8_t rimParam) {
    if (rimMul == nullptr) {
        return;
    }
    const float depth = static_cast<float>(paramSpanI(rimParam, 0, 70, 180));
    for (int k = 0; k <= 256; k++) {
        const float g = (k - 128) * (1.0f / 128.0f);
        const float b = 1.0f - g * g;
        rimMul[k] = static_cast<uint16_t>(256 - roundJS(depth * b * b));
    }
}

bool init(int, int) {
    if (texture != nullptr) {
        return true;
    }
    if (sinLut() == nullptr) {
        release();
        return false;
    }
    levels = static_cast<uint8_t *>(alloc(TILE_PIXELS));
    ramp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    texture = static_cast<uint16_t *>(allocHot(TILE_PIXELS * sizeof(uint16_t)));
    rimMul = static_cast<uint16_t *>(allocHot(257 * sizeof(uint16_t)));
    chordO = static_cast<uint8_t *>(allocHot(CHORD_N));
    chordI = static_cast<uint8_t *>(allocHot(CHORD_N));
    pieConst = static_cast<uint16_t *>(allocHot(112));
    stage8 = static_cast<uint16_t *>(allocHot(64));
    if (!levels || !ramp || !texture || !rimMul || !chordO || !chordI || !pieConst || !stage8) {
        // A retry cannot mistake a partial texture for a complete set or
        // retain any allocation that would prevent the slab from resetting.
        release();
        return false;
    }
    // Built at the two sliders' defaults. frame() rebuilds either one the
    // moment its slider reads anything else, so init() is complete on its
    // own and a band call cannot meet a half-built table.
    buildLevels(50);
    buildRim(50);
    lastContrast = 50;
    lastRim = 50;
    for (int lane = 0; lane < 8; lane++) {
        // Mask, expand factor, repack factor for R and G; B needs no
        // repack. floor(r5*33/4) and floor(g6*65/16) replicate the low
        // bits exactly, without a vector lane shift or a temporary lane.
        pieConst[lane] = 0xF800;
        pieConst[8 + lane] = 33;
        pieConst[16 + lane] = 2048;
        pieConst[24 + lane] = 0x07E0;
        pieConst[32 + lane] = 65;
        pieConst[40 + lane] = 32;
        pieConst[48 + lane] = 31;
    }
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[BG_ANIM_PARAMS]) {
    if (lastContrast != p[3]) {
        buildLevels(p[3]);
        lastContrast = p[3];
        lastBrightness = -1; // texture is the ramp read through levels
    }
    if (lastRim != p[5]) {
        buildRim(p[5]);
        lastRim = p[5];
    }
    const uint32_t gen = themeGen();
    if (lastBrightness != p[2] || gen != lastThemeGen) {
        // pa_bright(): Q8 80..256, rounding before the channel scale. This
        // is the animation's existing brightness knob, in addition to the
        // firmware's global theme tone applied by buildThemeRamp().
        const int bright = 80 + (static_cast<int>(p[2]) * 176 + 50) / 100;
        buildThemeRamp(ramp, static_cast<uint16_t>(bright));
        for (int i = 0; i < TILE_PIXELS; i++) {
            texture[i] = ramp[levels[i]];
        }
        lastBrightness = p[2];
        lastThemeGen = gen;
    }
    // Lens size and edge width share the chord block: both move the radii.
    const int sizeKey = static_cast<int>(p[1]) | (static_cast<int>(p[4]) << 8);
    if (lastSize != sizeKey) {
        radius = 80 + (static_cast<int>(p[1]) * 50 + 50) / 100;
        feather = paramSpanI(p[4], 3, FEATHER, 30);
        innerRadius = radius - feather;
        inner2 = innerRadius * innerRadius;
        span = radius * radius - inner2;
        invSpan = 16777216 / span; // floor(2^24/span), exactly the page
        for (int k = 0; k < radius; k++) {
            chordO[k] = static_cast<uint8_t>(roundJS(sqrtf(static_cast<float>(radius * radius - k * k))));
        }
        for (int k = 0; k < innerRadius; k++) {
            chordI[k] = static_cast<uint8_t>(roundJS(sqrtf(static_cast<float>(inner2 - k * k))));
        }
        lastSize = sizeKey;
    }
    // Derived from wall time, not from which frames or bands were requested.
    // True sinf calls once per frame avoid the coarse fastSinRad table's
    // visible position steps. Single precision can differ from JS double
    // precision after long uptimes; subsequent pixel math is exact integer.
    // Even UINT32_MAX at speed 100 keeps scrolls inside int32. Texture
    // cursors deliberately wrap uint32, avoiding signed left-shift overflow.
    const float t = static_cast<float>(tMs) * speedMul(p[0]);
    constexpr float TAU = 6.2831853071795864769f;
    // Travel scales how far the lens roams about the centre; at 0 it parks
    // at (240, 240) and only the ground moves under it. Drift scales the
    // ground's scroll on both axes together, so its direction is fixed and
    // only its rate changes. Both are a literal 1.0f at slider 50.
    const float travel = paramSpan(p[6], 0.0f, 1.0f, 2.0f);
    const float drift = paramSpan(p[7], 0.0f, 1.0f, 3.0f);
    const float ampX = 70.0f * travel;
    const float ampY = 90.0f * travel;
    lensX = roundJS(240.0f + ampX * sinf(t * (TAU / 37000.0f) + 0.7f));
    lensY = roundJS(240.0f + ampY * sinf(t * (TAU / 53000.0f) + 2.3f));
    scrollX = roundJS(-t * 0.006f * drift);  // -6 ground pixels/s at the defaults
    scrollY = roundJS(-t * 0.0034f * drift); // -3.4 ground pixels/s at the defaults
}

// The preview blends expanded RGB888 samples of an RGB565 palette, then dims
// and requantizes. Blending raw 5/6-bit channels first would round twice too
// early and darken the rim. Expand by bit replication to match pa_blendDim.
BGANIM_INLINE uint16_t blendDim(uint16_t bg, uint16_t fg, int a, int m) {
    const int br5 = bg >> 11, bg6 = (bg >> 5) & 63, bb5 = bg & 31;
    const int fr5 = fg >> 11, fg6 = (fg >> 5) & 63, fb5 = fg & 31;
    const int br = (br5 << 3) | (br5 >> 2), gr = (bg6 << 2) | (bg6 >> 4), bl = (bb5 << 3) | (bb5 >> 2);
    const int fr = (fr5 << 3) | (fr5 >> 2), fg8 = (fg6 << 2) | (fg6 >> 4), fb = (fb5 << 3) | (fb5 >> 2);
    const int r = ((br + (((fr - br) * a) >> 8)) * m) >> 8;
    const int g = ((gr + (((fg8 - gr) * a) >> 8)) * m) >> 8;
    const int b = ((bl + (((fb - bl) * a) >> 8)) * m) >> 8;
    return rgb565(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
}

GM_ANIM_IRAM void runRef(uint16_t *out, const uint16_t *tex, uint32_t u, uint32_t step, int n) {
    for (int i = 0; i < n; i++) {
        out[i] = tex[(u >> 16) & 63];
        u += step;
    }
}

GM_ANIM_IRAM void featherRef(uint16_t *out, const uint16_t *bg, const uint16_t *fg, uint32_t uB,
                            uint32_t uL, int dx, int dy2, int n) {
    int rr2 = dx * dx + dy2;
    for (int i = 0; i < n; i++) {
        int e = rr2 - inner2;
        e = e < 0 ? 0 : (e > span ? span : e);
        // e*invSpan <= 2^24. The outermost alpha can be 1 because the
        // reciprocal is floored; do not replace this with division.
        const int a = 256 - ((e * invSpan) >> 16);
        out[i] = blendDim(bg[(uB >> 16) & 63], fg[(uL >> 16) & 63], a, rimMul[a]);
        uB += GROUND_STEP;
        uL += LENS_STEP;
        rr2 += 2 * dx + 1;
        dx++;
    }
}

BGANIM_INLINE int clipX(int x, int w) { return x < 0 ? 0 : (x > w ? w : x); }

// Every cursor, chord and radial recurrence starts from this row's absolute
// y. No cross-call caches or neighbouring-row copies: rows==1 and parity
// skipping produce exactly the same pixels as a full-frame call.
GM_ANIM_IRAM void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        const int dy = y - lensY;
        const int ady = dy < 0 ? -dy : dy;
        const uint16_t *bg = texture + (((y + scrollY) >> 1) & 63) * TILE_N;
        const uint32_t uB = static_cast<uint32_t>(scrollX) << 15;
        if (ady >= radius) {
            runRef(out, bg, uB, GROUND_STEP, w);
            continue;
        }
        const int halfO = chordO[ady];
        const int halfI = ady < innerRadius ? chordI[ady] : 0;
        const int xA = lensX - halfO;
        const int eA = clipX(xA, w), eB = clipX(lensX - halfI, w);
        const int eC = clipX(lensX + halfI, w), eD = clipX(lensX + halfO, w);
        const uint16_t *fg = texture + (((scrollY + lensY + (dy >> 1)) >> 1) & 63) * TILE_N;
        // Preserve floor(-halfO/2) before the Q16.16 shift. Re-centring
        // the cursor would lose a quarter-texel on odd chords.
        const uint32_t uL = (static_cast<uint32_t>(scrollX + lensX + ((xA - lensX) >> 1)) << 15) +
                            LENS_STEP * static_cast<uint32_t>(eA - xA);
        runRef(out, bg, uB, GROUND_STEP, eA);
        featherRef(out + eA, bg, fg, uB + GROUND_STEP * eA, uL, eA - lensX, dy * dy, eB - eA);
        runRef(out + eB, fg, uL + LENS_STEP * (eB - eA), LENS_STEP, eC - eB);
        featherRef(out + eC, bg, fg, uB + GROUND_STEP * eC, uL + LENS_STEP * (eC - eA),
                   eC - lensX, dy * dy, eD - eC);
        runRef(out + eD, bg, uB + GROUND_STEP * eD, GROUND_STEP, w - eD);
    }
}

#if GM_BGANIM_LENS_ASM
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14.2's original runRef loop was extui/addx2/l16ui/add/s16i/addi,
// six instructions per pixel with the cursor add filling the load-use gap.
// The general pair loop below transcribes that dependency chain twice,
// interleaves the gathers and packs one s32i. Ground runs take a further
// edge: half-texel steps need only one new texel per pair. At integral phase
// both pixels repeat it; at half phase the high pixel becomes the next low.
// These bodies cost 8, 9 and 12 instructions per pair, respectively, with
// no immediate load consumers. The generic body also handles cursor wrap.
// out is 4-byte aligned by runAsm's scalar prefix; no PIE alignment needed.
// Estimated warm-SRAM issue floors are 4, 4.5 and 6 cycles/pixel, excluding
// calls, loop setup and memory contention. Only the device A/B can time this.
GM_ANIM_IRAM __attribute__((noinline)) uint32_t lensRunPairsAsm(uint16_t *out, const uint16_t *tex,
                                                             uint32_t u, uint32_t step, int nPairs) {
    uint32_t t0, t1, packed;
    if (nPairs <= 0) {
        return u;
    }
    if (step == 32768 && (u & 32768) == 0) {
        const uint32_t pairStep = 65536;
        asm volatile("loopnez %[n], 1f\n"
                     "extui %[t0], %[u], 16, 6\n"
                     "addx2 %[t0], %[t0], %[tex]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "add %[u], %[u], %[step]\n"
                     "slli %[t1], %[t0], 16\n"
                     "or %[t1], %[t1], %[t0]\n"
                     "s32i %[t1], %[out], 0\n"
                     "addi %[out], %[out], 4\n"
                     "1:\n"
                     : [out] "+&r"(out), [u] "+&r"(u), [t0] "=&r"(t0), [t1] "=&r"(t1)
                     : [tex] "r"(tex), [step] "r"(pairStep), [n] "r"(nPairs)
                     : "memory");
    } else if (step == 32768) {
        t0 = tex[(u >> 16) & 63];
        u += step;
        const uint32_t pairStep = 65536;
        asm volatile("loopnez %[n], 1f\n"
                     "extui %[t1], %[u], 16, 6\n"
                     "addx2 %[t1], %[t1], %[tex]\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "add %[u], %[u], %[step]\n"
                     "slli %[pack], %[t1], 16\n"
                     "or %[pack], %[pack], %[t0]\n"
                     "s32i %[pack], %[out], 0\n"
                     "mov %[t0], %[t1]\n"
                     "addi %[out], %[out], 4\n"
                     "1:\n"
                     : [out] "+&r"(out), [u] "+&r"(u), [t0] "+&r"(t0), [t1] "=&r"(t1),
                       [pack] "=&r"(packed)
                     : [tex] "r"(tex), [step] "r"(pairStep), [n] "r"(nPairs)
                     : "memory");
        u -= step;
    } else {
        asm volatile("loopnez %[n], 1f\n"
                     "extui %[t0], %[u], 16, 6\n"
                     "add %[u], %[u], %[step]\n"
                     "extui %[t1], %[u], 16, 6\n"
                     "addx2 %[t0], %[t0], %[tex]\n"
                     "addx2 %[t1], %[t1], %[tex]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "add %[u], %[u], %[step]\n"
                     "slli %[t1], %[t1], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "s32i %[t0], %[out], 0\n"
                     "addi %[out], %[out], 4\n"
                     "1:\n"
                     : [out] "+&r"(out), [u] "+&r"(u), [t0] "=&r"(t0), [t1] "=&r"(t1)
                     : [tex] "r"(tex), [step] "r"(step), [n] "r"(nPairs)
                     : "memory");
    }
    return u;
}

// Gather the feather's three data-dependent reads in a hardware loop. The
// GCC staging loop reloaded its radial constants from the stack per pixel;
// this 14-register block retains them, schedules every load-use gap, and
// closes 24 instructions/pixel with LOOP. e is rr2-inner2, de is 2*dx+1.
// Both are fresh at each block. Clamping precedes the Q24 reciprocal multiply
// so its product is at most 2^24 for radii 80..130 and edge widths 3..30
// (span f*(2R-f), 471..6900); invSpan is floor(2^24/span) for any of them.
// The temp holding LOOP's count is free to reuse after the instruction has
// copied it to LCOUNT. There are no nested hardware loops or calls here.
GM_ANIM_IRAM __attribute__((noinline)) void lensStage8Asm(uint16_t *stage, const uint16_t *bg,
                                                       const uint16_t *fg, const uint16_t *rim,
                                                       uint32_t uB, uint32_t uL, int e, int de,
                                                       int cap, int inv) {
    int t0, t1, t2;
    const uint32_t stepB = 32768;
    asm volatile("movi %[t2], 8\n"
                 "loop %[t2], 1f\n"
                 "min %[t0], %[e], %[cap]\n"
                 "movi %[t1], 0\n"
                 "max %[t0], %[t0], %[t1]\n"
                 "mull %[t0], %[t0], %[inv]\n"
                 "movi %[t1], 256\n"
                 "srai %[t0], %[t0], 16\n"
                 "sub %[t0], %[t1], %[t0]\n"
                 "extui %[t1], %[ub], 16, 6\n"
                 "s16i %[t0], %[out], 32\n"
                 "addx2 %[t0], %[t0], %[rim]\n"
                 "extui %[t2], %[ul], 16, 6\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "addx2 %[t1], %[t1], %[bg]\n"
                 "addx2 %[t2], %[t2], %[fg]\n"
                 "s16i %[t0], %[out], 48\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t2], %[t2], 0\n"
                 "add %[e], %[e], %[de]\n"
                 "s16i %[t1], %[out], 0\n"
                 "s16i %[t2], %[out], 16\n"
                 "addi %[de], %[de], 2\n"
                 "add %[ub], %[ub], %[step]\n"
                 "addmi %[ul], %[ul], 16384\n"
                 "addi %[out], %[out], 2\n"
                 "1:\n"
                 : [out] "+&r"(stage), [ub] "+&r"(uB), [ul] "+&r"(uL), [e] "+&r"(e),
                   [de] "+&r"(de), [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2)
                 : [bg] "r"(bg), [fg] "r"(fg), [rim] "r"(rim), [cap] "r"(cap), [inv] "r"(inv),
                   [step] "r"(stepB)
                 : "memory");
}

// Eight independent feather pixels. stage holds bg[8], fg[8], alpha[8],
// dim[8], all uint16. The wrapper checks stage and constant alignment, then
// aligns out with a scalar prefix before calling this kernel.
// q0/q1 retain packed RGB565, q2/q3 retain Q8 alpha/dim, q4/q5 are channel
// temporaries, q6 assembles RGB565 and q7 streams the broadcast constants.
// GCC does not allocate q registers; no q clobber syntax exists. SAR is
// set before every multiply mode. Never writes CPENABLE: FreeRTOS owns it.
//
// Unsigned multiply expands masked R/G/B directly: (Rbits*33)>>13,
// (Gbits*65)>>9, (Bbits*33)>>2. Signed multiply preserves the page's
// floor of a negative channel delta. Delta and channel sums stay within
// [-255,255], so signed saturating add/sub never saturate. The last dim
// multiply folds in quantization: >>11 for R/B, >>10 for G, then repack.
// This preserves both page rounding stages exactly for all 65536 colours,
// alpha 0..256 and dim 0..256, including the actual rim's 186..256 range.
// 52 instructions for eight pixels, including all loads/stores (6.5/pixel),
// plus two exposed one-cycle dependencies: the G mask load into AND, and
// the final B multiply into OR. The stage model's warm-SRAM issue floor is
// therefore 6.75 cycles/pixel, excluding scalar gathering and call setup.
// This is an estimate, not a device measurement. The frame tables and
// staging slab matter more than a lower host time, as the other ports found.
// EE.VSUBS.S16 and signed multiply also have execution precedent in
// tools/qemubench/tests/anim_nebula/main.c; this kernel is checked anew.
GM_ANIM_IRAM __attribute__((noinline)) void lensBlend8Asm(uint16_t *out, const uint16_t *stage,
                                                       const uint16_t *ct) {
    asm volatile("ee.vld.128.ip q0, %[in], 16\n"
                 "ee.vld.128.ip q1, %[in], 16\n"
                 "ee.vld.128.ip q2, %[in], 16\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // R mask
                 "ee.vld.128.ip q3, %[in], 16\n"
                 "ee.andq q4, q0, q7\n"
                 "ee.andq q5, q1, q7\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // 33
                 "ssai 13\n"
                 "ee.vmul.u16 q4, q4, q7\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ssai 8\n"
                 "ee.vsubs.s16 q5, q5, q4\n"
                 "ee.vmul.s16 q5, q5, q2\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // R repack 2048
                 "ee.vadds.s16 q5, q5, q4\n"
                 "ssai 11\n"
                 "ee.vmul.u16 q5, q5, q3\n"
                 "ssai 0\n"
                 "ee.vmul.u16 q6, q5, q7\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // G mask
                 "ee.andq q4, q0, q7\n"
                 "ee.andq q5, q1, q7\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // 65
                 "ssai 9\n"
                 "ee.vmul.u16 q4, q4, q7\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ssai 8\n"
                 "ee.vsubs.s16 q5, q5, q4\n"
                 "ee.vmul.s16 q5, q5, q2\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // G repack 32
                 "ee.vadds.s16 q5, q5, q4\n"
                 "ssai 10\n"
                 "ee.vmul.u16 q5, q5, q3\n"
                 "ssai 0\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ee.vld.128.ip q7, %[ct], -80\n" // B mask at 96, rewind to shared 33 at 16
                 "ee.orq q6, q6, q5\n"
                 "ee.andq q4, q0, q7\n"
                 "ee.andq q5, q1, q7\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // 33
                 "ssai 2\n"
                 "ee.vmul.u16 q4, q4, q7\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ssai 8\n"
                 "ee.vsubs.s16 q5, q5, q4\n"
                 "ee.vmul.s16 q5, q5, q2\n"
                 "ssai 11\n"
                 "ee.vadds.s16 q5, q5, q4\n"
                 "ee.vmul.u16 q5, q5, q3\n"
                 "ee.orq q6, q6, q5\n"
                 "ee.vst.128.ip q6, %[out], 16\n"
                 : [out] "+&r"(out), [in] "+&r"(stage), [ct] "+&r"(ct)
                 :
                 : "memory");
}
#else
// Portable twins make the prefixes, tails and eight-lane staging available
// to host parity checks of bandAsm. The registered host entry calls bandRef.
uint32_t lensRunPairsAsm(uint16_t *out, const uint16_t *tex, uint32_t u, uint32_t step, int nPairs) {
    runRef(out, tex, u, step, nPairs * 2);
    return u + step * static_cast<uint32_t>(nPairs * 2);
}

void lensStage8Asm(uint16_t *stage, const uint16_t *bg, const uint16_t *fg, const uint16_t *rim,
                   uint32_t uB, uint32_t uL, int e, int de, int cap, int inv) {
    for (int i = 0; i < 8; i++) {
        const int clipped = e < 0 ? 0 : (e > cap ? cap : e);
        const int a = 256 - ((clipped * inv) >> 16);
        stage[i] = bg[(uB >> 16) & 63];
        stage[8 + i] = fg[(uL >> 16) & 63];
        stage[16 + i] = static_cast<uint16_t>(a);
        stage[24 + i] = rim[a];
        uB += GROUND_STEP;
        uL += LENS_STEP;
        e += de;
        de += 2;
    }
}

void lensBlend8Asm(uint16_t *out, const uint16_t *stage, const uint16_t *) {
    for (int i = 0; i < 8; i++) {
        out[i] = blendDim(stage[i], stage[8 + i], stage[16 + i], stage[24 + i]);
    }
}
#endif

GM_ANIM_IRAM void runAsm(uint16_t *out, const uint16_t *tex, uint32_t u, uint32_t step, int n) {
    if (n > 0 && (reinterpret_cast<uintptr_t>(out) & 3)) {
        *out++ = tex[(u >> 16) & 63];
        u += step;
        n--;
    }
    const int pairs = n / 2;
    u = lensRunPairsAsm(out, tex, u, step, pairs);
    if (n & 1) {
        out[pairs * 2] = tex[(u >> 16) & 63];
    }
}

GM_ANIM_IRAM void featherAsm(uint16_t *out, const uint16_t *bg, const uint16_t *fg, uint32_t uB,
                            uint32_t uL, int dx, int dy2, int n) {
    // allocHot can fall back to PSRAM, whose allocator does not guarantee
    // 16-byte alignment. Slab residue can trigger that even though our own
    // tables fit. PIE masks low address bits instead of trapping, so prove
    // BOTH table addresses before staging or any vector load/store. A
    // misaligned table sends the whole span through the scalar reference.
    if ((reinterpret_cast<uintptr_t>(stage8) | reinterpret_cast<uintptr_t>(pieConst)) & 15) {
        featherRef(out, bg, fg, uB, uL, dx, dy2, n);
        return;
    }
    // Only real, fully aligned 8-pixel spans reach ee.vst.128.ip. A caller
    // may begin a feather at any x, including a 4-byte-aligned band buffer
    // that is not itself 16-byte aligned. Prefix and tail never overread.
    int prefix = static_cast<int>((16 - (reinterpret_cast<uintptr_t>(out) & 15)) & 15) / 2;
    if (prefix > n) {
        prefix = n;
    }
    featherRef(out, bg, fg, uB, uL, dx, dy2, prefix);
    out += prefix;
    uB += GROUND_STEP * prefix;
    uL += LENS_STEP * prefix;
    dx += prefix;
    n -= prefix;
    int rr2 = dx * dx + dy2;
    uint16_t *stage = stage8; // All four vectors follow the checked base by 16 B.
    while (n >= 8) {
        lensStage8Asm(stage, bg, fg, rimMul, uB, uL, rr2 - inner2, 2 * dx + 1, span, invSpan);
        lensBlend8Asm(out, stage, pieConst);
        uB += 8 * GROUND_STEP;
        uL += 8 * LENS_STEP;
        rr2 += 16 * dx + 64; // (dx+8)^2 - dx^2, exactly eight DDA steps
        dx += 8;
        out += 8;
        n -= 8;
    }
    featherRef(out, bg, fg, uB, uL, dx, dy2, n);
}
#endif // GM_BGANIM_LENS_ASM

GM_ANIM_IRAM void bandAsm(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_LENS_ASM
    (void)tMs;
    (void)p;
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        const int dy = y - lensY;
        const int ady = dy < 0 ? -dy : dy;
        const uint16_t *bg = texture + (((y + scrollY) >> 1) & 63) * TILE_N;
        const uint32_t uB = static_cast<uint32_t>(scrollX) << 15;
        if (ady >= radius) {
            runAsm(out, bg, uB, GROUND_STEP, w);
            continue;
        }
        const int halfO = chordO[ady];
        const int halfI = ady < innerRadius ? chordI[ady] : 0;
        const int xA = lensX - halfO;
        const int eA = clipX(xA, w), eB = clipX(lensX - halfI, w);
        const int eC = clipX(lensX + halfI, w), eD = clipX(lensX + halfO, w);
        const uint16_t *fg = texture + (((scrollY + lensY + (dy >> 1)) >> 1) & 63) * TILE_N;
        const uint32_t uL = (static_cast<uint32_t>(scrollX + lensX + ((xA - lensX) >> 1)) << 15) +
                            LENS_STEP * static_cast<uint32_t>(eA - xA);
        runAsm(out, bg, uB, GROUND_STEP, eA);
        featherAsm(out + eA, bg, fg, uB + GROUND_STEP * eA, uL, eA - lensX, dy * dy, eB - eA);
        runAsm(out + eB, fg, uL + LENS_STEP * (eB - eA), LENS_STEP, eC - eB);
        featherAsm(out + eC, bg, fg, uB + GROUND_STEP * eC, uL + LENS_STEP * (eC - eA),
                   eC - lensX, dy * dy, eD - eC);
        runAsm(out + eD, bg, uB + GROUND_STEP * eD, GROUND_STEP, w - eD);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_LENS_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    bandAsm(dst, y0, rows, w, tMs, p);
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(levels, TILE_PIXELS);
    releaseTable(ramp, 256 * sizeof(uint16_t));
    releaseTable(texture, TILE_PIXELS * sizeof(uint16_t));
    releaseTable(rimMul, 257 * sizeof(uint16_t));
    releaseTable(chordO, CHORD_N);
    releaseTable(chordI, CHORD_N);
    releaseTable(pieConst, 112);
    releaseTable(stage8, 64);
    lastBrightness = lastSize = -1;
    lastContrast = 50;
    lastRim = 50;
    feather = FEATHER;
    lastThemeGen = 0xFFFFFFFF;
    radius = innerRadius = inner2 = invSpan = 0;
    span = 1;
    lensX = lensY = 240;
    scrollX = scrollY = 0;
}

} // namespace

extern const BgAnimation bg_anim_lens;
const BgAnimation bg_anim_lens = {
    "lens",
    "Lens",
    {{"speed", "Speed", 50},
     {"size", "Lens size", 55},
     {"brightness", "Brightness", 62},
     {"contrast", "Contrast", 50},
     {"edge", "Edge width", 50},
     {"rim", "Rim darkness", 50},
     {"travel", "Lens travel", 50},
     {"drift", "Ground drift", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
