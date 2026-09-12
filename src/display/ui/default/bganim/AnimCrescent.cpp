#ifndef GAGGIMATE_SIM

// "Crescent": a broad, softly shaded lune, turning and swelling over a dark
// vignette. Port of entry 22 in tools/animbench/web/anim_bench.html. Two equal
// circles contribute smoothstep coverage fields whose product has no seam
// where a scanline crosses both tips. The body brightens toward the limb.
//
// Positions and palette values use Q4, coverage Q8, and invK = 2^24/(2*R*26).
// Keep every truncation, especially the cut's squared Q4 distance >>8 BEFORE
// multiplying by invK. Exact integer second differences walk both squares.
// The 257-entry smoothstep table evaluates the page's cubic at every possible
// clamped input, with no extra quantization. Factor radColPh into radCol plus
// dith[phase*8+(x&7)]*4 to avoid eight copies of a 32-bit column. Every row,
// including its Bayer phase, depends only on absolute y and frame state.
// RGB565 maps directly to the page's theme ramp. Its RGB888 preview replicates
// channel bits; the host PPM writer uses channel*255/max, which can differ by
// one RGB888 count even when the stored RGB565 pixels agree exactly.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

#ifndef GM_BGANIM_CRESCENT_ASM
#define GM_BGANIM_CRESCENT_ASM 1
#endif

namespace {
using namespace bganim;
constexpr int BODY_LO = 74;
constexpr int LIMB = 78; // radial body gain /256
constexpr int BG_HI = 26; // background theme position before vignette
constexpr int BGF = 20; // radial background loss /256
constexpr int FLOOR = 3; // darkest theme position
constexpr int SOFT_PX = 26; // squared-distance edge width = 2*R*SOFT_PX
constexpr int RAD_SCALE = 4080; // 255 palette positions in Q4
constexpr int PAL_PAD = 32, PAL_N = PAL_PAD + 320;
int32_t *radCol = nullptr, *radRow = nullptr;
int16_t *bgRow = nullptr, *spanPx = nullptr, *dith = nullptr;
uint16_t *paletteStorage = nullptr, *palette = nullptr;
uint16_t *smooth = nullptr, *weights = nullptr;
const int16_t *sine = nullptr; // borrowed shared table
int allocW = 0, allocH = 0;
int discR = 0, invK = 1, icx16 = 0, icy16 = 0, bodyBase = 0;
bool tablesValid = false, laneSafe = false;
uint32_t lastThemeGen = 0xFFFFFFFF;
uint8_t lastP[4] = {255, 255, 255, 255};

// At 480x480, all tables are allocHot: radCol 1920, radRow 1920, bgRow 960,
// spanPx 960, dith 128, paletteStorage 704, smooth 514 (528 aligned), weights
// 960. Payload 8066 B, slab 8080 B of 9216 B. No private PSRAM tables.
// The borrowed sine is 2048 B in the existing shared reservation. The palette
// has 256 real theme entries plus clamped ends for the device's plain gather.
void release();
bool init(int w, int h) {
    // The largest supported panel bounds the slab allocation below.
    if (w <= 0 || h <= 0 || w > 480 || h > 480) {
        release();
        return false;
    }
    if (allocW != w || allocH != h) release();
    sine = sinLut();
    if (!sine) { release(); return false; }
    if (radCol) return true;
    // Save sizes first: every failed allocation releases the complete partial
    // set before returning false, so an immediate retry starts clean.
    allocW = w; allocH = h;
    radCol = static_cast<int32_t *>(allocHot(w * sizeof(int32_t)));
    radRow = static_cast<int32_t *>(allocHot(h * sizeof(int32_t)));
    bgRow = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
    spanPx = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
    dith = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    paletteStorage = static_cast<uint16_t *>(allocHot(PAL_N * sizeof(uint16_t)));
    smooth = static_cast<uint16_t *>(allocHot(257 * sizeof(uint16_t)));
    weights = static_cast<uint16_t *>(allocHot(w * sizeof(uint16_t)));
    if (!radCol || !radRow || !bgRow || !spanPx || !dith || !paletteStorage || !smooth || !weights) {
        release();
        return false;
    }
    palette = paletteStorage + PAL_PAD;
    // 768 = 3*256: Q8 cubic smoothstep, 3*t^2 - 2*t^3.
    for (int u = 0; u <= 256; ++u) smooth[u] = static_cast<uint16_t>((u * u * (768 - 2 * u)) >> 16);
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const int cx = w / 2, cy = h / 2;
    const uint32_t gen = themeGen();
    if (!tablesValid || gen != lastThemeGen || memcmp(p, lastP, 4) != 0) {
        buildThemeRamp(palette, 256);
        // Page: 0.6*ditherAmp, rounded in Q4, then *4 in squared radius.
        // Moving this to the final palette index would change the picture.
        const float amp = ditherAmp(palette, 256) * 0.6f;
        for (int k = 0; k < 64; ++k)
            dith[k] = static_cast<int16_t>(lroundf((BAYER8[k] - 31.5f) * (amp * 16.0f / 31.5f)));
        for (int i = -PAL_PAD; i < FLOOR; ++i) palette[i] = palette[FLOOR];
        for (int i = 256; i < 320; ++i) palette[i] = palette[255];
        const int rMax = cx < cy ? cx : cy;
        // Page size: 65..97 percent of half-panel radius, minimum 16 px.
        discR = rMax * (65 + static_cast<int>(p[1]) * 32 / 100) / 100;
        if (discR < 16) discR = 16;
        invK = 65536 * 256 / (2 * discR * SOFT_PX);
        const int outer = discR + SOFT_PX;
        // Vignette radius is one pixel inside half the panel, as on the page.
        const int R = rMax - 1, R2 = R * R > 0 ? R * R : 1;
        int maxCol = 0, maxRow = 0;
        for (int x = 0; x < w; ++x) {
            const int dx = x - cx;
            radCol[x] = dx * dx * RAD_SCALE / R2;
            if (radCol[x] > maxCol) maxCol = radCol[x];
        }
        for (int y = 0; y < h; ++y) {
            const int dy = y - cy;
            radRow[y] = dy * dy * RAD_SCALE / R2;
            if (radRow[y] > maxRow) maxRow = radRow[y];
            // Nine palette positions of vertical lift, carried in Q4.
            bgRow[y] = static_cast<int16_t>(y * 9 * 16 / (h > 1 ? h - 1 : 1));
            const int inside = outer * outer - dy * dy;
            spanPx[y] = static_cast<int16_t>(inside > 0 ? static_cast<int>(sqrtf(static_cast<float>(inside))) : -1);
        }
        // All supported square panels (233/240/466/480) satisfy this bound.
        // Unusual aspect ratios use the portable path if signed PIE lanes or
        // the padded palette would not suffice. Max dither is round(16*.6*16)*4=616.
        laneSafe = maxCol + maxRow + 616 <= 9216;
        lastThemeGen = gen;
        memcpy(lastP, p, 4);
        tablesValid = true;
    }
    // Use the actual page clocks. Its prose says about 13/16.5 seconds, but
    // BOTH turn and swell use base>>7: a nominal 13107.2 ms cycle at default
    // p0=15, sp=10, which the speed calibration below stretches to 17476.3 ms.
    // Body breathing is base*3>>9: 17476.2667 ms, stretched to 23301.7 ms.
    // The base*3 product still wraps as uint32, matching the page's >>>0, even
    // near millis() rollover.
    // Speed calibration (gm-33fm): the clock runs at 0.75x the original rate,
    // so Speed 50 gives about the same visible movement here as on every other
    // animation. The Speed parameter, its label and its default of 50 are
    // unchanged. Three quarters of a whole-number rate is not a whole number,
    // so the rate is carried as quarters (sp4 = 3 * the old rate) and the
    // product is shifted back down. The multiply widens to 64 bits first: the
    // old uint32 wrap would have thrown away the top two bits of the rate
    // before the shift could use them. The page does the same arithmetic.
    const uint32_t sp4 = 3u * (4u + static_cast<uint32_t>(p[0]) * 44 / 100);
    const uint32_t base = static_cast<uint32_t>((static_cast<uint64_t>(tMs) * sp4) >> 2);
    const uint32_t angIdx = (base >> 7) & (SIN_N - 1);
    const uint32_t swellIdx = (base >> 7) & (SIN_N - 1);
    const uint32_t phBr = (base * 3u) >> 9;
    // +768 is negative cosine; +256 below is cosine in the 1024-entry sine.
    const int swell = 512 - sine[(swellIdx + 768) & (SIN_N - 1)];
    // Offset never below 28% of R; the phase knob sets the maximum to 45..100%.
    const int dMin = discR * 28 / 100;
    const int dMax = discR * (45 + static_cast<int>(p[2]) * 55 / 100) / 100;
    const int d = dMin + (((dMax - dMin) * swell) >> 10);
    icx16 = cx * 16 + ((d * sine[(angIdx + 256) & (SIN_N - 1)]) >> 5);
    icy16 = cy * 16 + ((d * sine[angIdx]) >> 5);
    // Contrast sets 74..120 palette positions, with a +-150 Q4 pulse.
    bodyBase = (BODY_LO + static_cast<int>(p[3]) * 46 / 100) * 16 +
               ((sine[phBr & (SIN_N - 1)] * 150) >> 9);
}

// Exact square recurrences: qOuter=R^2-dx^2-dy^2, dOuter=-(2*dx+1);
// qCut=dxi^2+dyi^2 in Q8, dCut=32*dxi+256. Second differences -2 and +512.
GM_ANIM_IRAM __attribute__((noinline)) void crescentWeightsRef(uint16_t *out, int qOuter, int dOuter,
                                                               int qCut, int dCut, int r2, int inv,
                                                               const uint16_t *sm, int n) {
    for (int x = 0; x < n; ++x) {
        int uo = 128 + ((qOuter * inv) >> 16);
        int uc = 128 + ((((qCut >> 8) - r2) * inv) >> 16);
        uo = uo < 0 ? 0 : (uo > 256 ? 256 : uo);
        uc = uc < 0 ? 0 : (uc > 256 ? 256 : uc);
        out[x] = static_cast<uint16_t>((sm[uo] * sm[uc]) >> 8);
        qOuter += dOuter; dOuter -= 2;
        qCut += dCut; dCut += 512;
    }
}

GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int cx = allocW / 2, cy = allocH / 2, r2 = discR * discR;
    for (int row = 0; row < rows; ++row) {
        const int y = y0 + row, span = spanPx[y];
        int a = 0, b = 0;
        if (span >= 0) {
            a = cx - span; b = cx + span + 1;
            if (a < 0) a = 0;
            if (b > w) b = w;
        }
        for (int x = 0; x < a; ++x) weights[x] = 0;
        for (int x = b; x < w; ++x) weights[x] = 0;
        const int dx = a - cx, dy = y - cy;
        const int dxi = a * 16 - icx16, dyi = y * 16 - icy16;
        crescentWeightsRef(weights + a, r2 - dx * dx - dy * dy, -2 * dx - 1,
                           dxi * dxi + dyi * dyi, 32 * dxi + 256, r2, invK, smooth, b - a);
        const int16_t *off = dith + (y & 7) * 8;
        const int rr = radRow[y], bgBase = BG_HI * 16 + bgRow[y], litBase = bodyBase;
        for (int x = 0; x < w; ++x) {
            const int rad = radCol[x] + off[x & 7] * 4 + rr;
            // Unsigned products preserve JS ToInt32 even on tiny rectangular
            // host test surfaces where the radial normalization can be huge.
            // All four device sizes fit signed 32 bits without wrapping.
            const int bg = bgBase - (static_cast<int32_t>(static_cast<uint32_t>(rad) * BGF) >> 8);
            const int body = litBase + (static_cast<int32_t>(static_cast<uint32_t>(rad) * LIMB) >> 8);
            int idx = (bg + (static_cast<int32_t>(static_cast<uint32_t>(body - bg) * weights[x]) >> 8)) >> 4;
            idx = idx < FLOOR ? FLOOR : (idx > 255 ? 255 : idx);
            dst[row * w + x] = palette[idx];
        }
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM) && GM_BGANIM_CRESCENT_ASM
// GCC 14's original 27-instruction coverage loop is preserved and executed
// in tests/anim_crescent/gcc_baseline.S. It reconstructs dOuter from a spilled
// pointer bias every pixel. Keep the actual recurrence in an AR instead.
// Bias sm by 128 and clamp to [-128,128], removing the two adds and two
// redundant EXTUI instructions. Thirteen ARs, no spills inside the loop.
// The loop is 22 instructions/pixel versus GCC's 27. With two MULLs at the
// brief's nominal two cycles, estimate 24 cycles/pixel plus memory and fetch.
// Both smoothstep gathers have independent work before their consumers.
// Full panel/parameter bounds: R=75..232, invK=1390..4301 on the four panel
// sizes; cut squares and reciprocal products stay in signed 32-bit range.
GM_ANIM_IRAM __attribute__((noinline)) void crescentWeightsAsm(uint16_t *out, int qOuter, int dOuter,
                                                               int qCut, int dCut, int r2, int inv,
                                                               const uint16_t *sm, int n) {
    sm += 128;
    int uo, uc, lo, hi;
    asm volatile("movi %[lo], -128\n"
                 "movi %[hi], 128\n"
                 "loopnez %[n], 1f\n"
                 "srai %[uc], %[qc], 8\n"
                 "sub %[uc], %[uc], %[r2]\n"
                 "mull %[uo], %[qo], %[inv]\n"
                 "mull %[uc], %[uc], %[inv]\n"
                 "srai %[uo], %[uo], 16\n"
                 "srai %[uc], %[uc], 16\n"
                 "min %[uo], %[uo], %[hi]\n"
                 "min %[uc], %[uc], %[hi]\n"
                 "max %[uo], %[uo], %[lo]\n"
                 "max %[uc], %[uc], %[lo]\n"
                 "addx2 %[uo], %[uo], %[sm]\n"
                 "addx2 %[uc], %[uc], %[sm]\n"
                 "l16ui %[uo], %[uo], 0\n"
                 "l16ui %[uc], %[uc], 0\n"
                 "add %[qo], %[qo], %[dqo]\n"
                 "mul16u %[uo], %[uo], %[uc]\n"
                 "addi %[dqo], %[dqo], -2\n"
                 "srli %[uo], %[uo], 8\n"
                 "s16i %[uo], %[out], 0\n"
                 "add %[qc], %[qc], %[dqc]\n"
                 "addmi %[dqc], %[dqc], 512\n"
                 "addi %[out], %[out], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [qo] "+&r"(qOuter), [dqo] "+&r"(dOuter),
                   [qc] "+&r"(qCut), [dqc] "+&r"(dCut), [uo] "=&r"(uo), [uc] "=&r"(uc),
                   [lo] "=&r"(lo), [hi] "=&r"(hi)
                 : [r2] "r"(r2), [inv] "r"(inv), [sm] "r"(sm), [n] "r"(n)
                 : "memory");
}

// Eight pixels: narrow the exact 32-bit radCol values, add the factored
// dither/row vector, compute vignette, limb and coverage blend in signed Q4.
// q2..q6 hold row constants, q0/q1 arithmetic and q7 weights. GCC never
// allocates q0..q7, so there is no q-register clobber syntax. FreeRTOS owns
// CPENABLE and lazily saves CP3; this kernel never writes CPENABLE.
//
// col, weight and rowRad are 16-byte aligned by allocHot or aligned stack
// construction. Each iteration reads exactly eight columns and weights.
// Output uses only S32I, so every permitted 4-byte output alignment works,
// including odd-width single rows. A scalar tail handles the last 0..7.
// The output increment fills the last VMUL's use gap: subtract 16 only
// inside asm before LOOPNEZ and restore after it, never access that address.
// All vector loads/multiplies and scalar gathers have independent work in
// their use gaps. Direct MOVI.32.A extraction avoids a scratch-index spill.
// The loop is 57 instructions/eight pixels, 7.125 instructions/pixel, with
// no unfilled dependency gaps. PIE multiplier resource timing and PSRAM
// output traffic still need device timing; host speed is not that evidence.
//
// rad in [-616,9216], bgBase in [416,560], bodyBase in [1034,2070], weight
// in [0,256]: bg in [-304,609], body in [846,4878], the Q4 blend in [-304,4878].
// Thus idx in [-19,304], covered by palette[-32..319]. No signed saturation
// occurs. Padding repeats palette[3] below 3 and palette[255] above 255.
GM_ANIM_IRAM __attribute__((noinline)) void crescentShadeAsm(uint16_t *out, const int32_t *col,
                                                             const uint16_t *weight, const int16_t *rowRad,
                                                             const uint16_t *pal, int bgBase, int body,
                                                             int n) {
    const uint32_t coeffs[4] = {20u, 78u, (uint32_t)bgBase, (uint32_t)body};
    const uint32_t *coeff = coeffs;
    const int blocks = n >> 3;
    int low, high;
    asm volatile("ee.vldbc.16.ip q3, %[coeff], 4\n"
                 "ee.vldbc.16.ip q4, %[coeff], 4\n"
                 "ee.vldbc.16.ip q5, %[coeff], 4\n"
                 "ee.vldbc.16.ip q6, %[coeff], 0\n"
                 "ee.vld.128.ip q2, %[rr], 0\n"
                 "ssai 8\n"
                 "addi %[out], %[out], -16\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[col], 16\n"
                 "ee.vld.128.ip q1, %[col], 16\n"
                 "ee.vld.128.ip q7, %[weight], 16\n"
                 "ee.vunzip.16 q0, q1\n"
                 "ee.vadds.s16 q0, q0, q2\n"
                 "ee.vmul.s16 q1, q0, q3\n"
                 "ee.vmul.s16 q0, q0, q4\n"
                 "ee.vsubs.s16 q1, q5, q1\n"
                 "ee.vadds.s16 q0, q0, q6\n"
                 "ee.vsubs.s16 q0, q0, q1\n"
                 "ee.vmul.s16 q0, q0, q7\n"
                 "addi %[out], %[out], 16\n"
                 "ee.vadds.s16 q0, q0, q1\n"
                 "ee.movi.32.a q0, %[high], 0\n"
                 "sext %[low], %[high], 15\n"
                 "srai %[high], %[high], 20\n"
                 "srai %[low], %[low], 4\n"
                 "addx2 %[high], %[high], %[pal]\n"
                 "addx2 %[low], %[low], %[pal]\n"
                 "l16ui %[high], %[high], 0\n"
                 "l16ui %[low], %[low], 0\n"
                 "slli %[high], %[high], 16\n"
                 "or %[high], %[high], %[low]\n"
                 "s32i %[high], %[out], 0\n"
                 "ee.movi.32.a q0, %[high], 1\n"
                 "sext %[low], %[high], 15\n"
                 "srai %[high], %[high], 20\n"
                 "srai %[low], %[low], 4\n"
                 "addx2 %[high], %[high], %[pal]\n"
                 "addx2 %[low], %[low], %[pal]\n"
                 "l16ui %[high], %[high], 0\n"
                 "l16ui %[low], %[low], 0\n"
                 "slli %[high], %[high], 16\n"
                 "or %[high], %[high], %[low]\n"
                 "s32i %[high], %[out], 4\n"
                 "ee.movi.32.a q0, %[high], 2\n"
                 "sext %[low], %[high], 15\n"
                 "srai %[high], %[high], 20\n"
                 "srai %[low], %[low], 4\n"
                 "addx2 %[high], %[high], %[pal]\n"
                 "addx2 %[low], %[low], %[pal]\n"
                 "l16ui %[high], %[high], 0\n"
                 "l16ui %[low], %[low], 0\n"
                 "slli %[high], %[high], 16\n"
                 "or %[high], %[high], %[low]\n"
                 "s32i %[high], %[out], 8\n"
                 "ee.movi.32.a q0, %[high], 3\n"
                 "sext %[low], %[high], 15\n"
                 "srai %[high], %[high], 20\n"
                 "srai %[low], %[low], 4\n"
                 "addx2 %[high], %[high], %[pal]\n"
                 "addx2 %[low], %[low], %[pal]\n"
                 "l16ui %[high], %[high], 0\n"
                 "l16ui %[low], %[low], 0\n"
                 "slli %[high], %[high], 16\n"
                 "or %[high], %[high], %[low]\n"
                 "s32i %[high], %[out], 12\n"
                 "1:\n"
                 "addi %[out], %[out], 16\n"
                 : [out] "+&r"(out), [col] "+&r"(col), [weight] "+&r"(weight),
                   [coeff] "+&r"(coeff), [low] "=&r"(low), [high] "=&r"(high)
                 : [rr] "r"(rowRad), [pal] "r"(pal), [n] "r"(blocks)
                 : "memory");
    for (int x = 0; x < (n & 7); ++x) {
        const int rad = col[x] + rowRad[x];
        const int bg = bgBase - ((rad * 20) >> 8);
        const int lit = body + ((rad * 78) >> 8);
        out[x] = pal[(bg + (((lit - bg) * weight[x]) >> 8)) >> 4];
    }
}
#endif

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM) && GM_BGANIM_CRESCENT_ASM
    if (!laneSafe) { bandRef(dst, y0, rows, w, tMs, p); return; }
    const int cx = allocW / 2, cy = allocH / 2, r2 = discR * discR;
    for (int row = 0; row < rows; ++row) {
        const int y = y0 + row, span = spanPx[y];
        int a = 0, b = 0;
        if (span >= 0) {
            a = cx - span; b = cx + span + 1;
            if (a < 0) a = 0;
            if (b > w) b = w;
        }
        for (int x = 0; x < a; ++x) weights[x] = 0;
        for (int x = b; x < w; ++x) weights[x] = 0;
        const int dx = a - cx, dy = y - cy;
        const int dxi = a * 16 - icx16, dyi = y * 16 - icy16;
        crescentWeightsAsm(weights + a, r2 - dx * dx - dy * dy, -2 * dx - 1,
                           dxi * dxi + dyi * dyi, 32 * dxi + 256, r2, invK, smooth, b - a);
        alignas(16) int16_t rowRad[8];
        for (int k = 0; k < 8; ++k)
            rowRad[k] = static_cast<int16_t>(radRow[y] + dith[(y & 7) * 8 + k] * 4);
        crescentShadeAsm(dst + row * w, radCol, weights, rowRad, palette,
                         BG_HI * 16 + bgRow[y], bodyBase, w);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(radCol, static_cast<size_t>(allocW) * sizeof(int32_t));
    releaseTable(radRow, static_cast<size_t>(allocH) * sizeof(int32_t));
    releaseTable(bgRow, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(spanPx, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(paletteStorage, PAL_N * sizeof(uint16_t));
    releaseTable(smooth, 257 * sizeof(uint16_t));
    releaseTable(weights, static_cast<size_t>(allocW) * sizeof(uint16_t));
    palette = nullptr; sine = nullptr;
    allocW = allocH = discR = icx16 = icy16 = bodyBase = 0;
    invK = 1;
    tablesValid = laneSafe = false;
    lastThemeGen = 0xFFFFFFFF;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}
} // namespace

extern const BgAnimation bg_anim_crescent;
const BgAnimation bg_anim_crescent = {
    "crescent", "Crescent",
    {{"speed", "Speed", 50}, {"size", "Size", 70},
     {"phase", "Phase range", 40}, {"contrast", "Contrast", 40}},
    init, frame, band, release, bandRef,
};
#endif // GAGGIMATE_SIM
