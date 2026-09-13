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
int16_t *ozHalf = nullptr, *osHalf = nullptr;
int16_t *czLoRow = nullptr, *czHiRow = nullptr, *csLoRow = nullptr, *csHiRow = nullptr;
uint32_t *paletteStorage = nullptr, *palette = nullptr;
uint16_t *smooth = nullptr, *weights = nullptr;
const int16_t *sine = nullptr; // borrowed shared table
int allocW = 0, allocH = 0;
int discR = 0, invK = 1, icx16 = 0, icy16 = 0, bodyBase = 0;
int qaThr = 0, qzThr = 0;
bool tablesValid = false, laneSafe = false;
uint32_t lastThemeGen = 0xFFFFFFFF;
uint8_t lastP[4] = {255, 255, 255, 255};

// At 480x480 the per-pixel tables take 8,784 B of the resident 9,216 B slab
// (8,770 B of payload, the rest alignment): radCol 1,920 B, radRow 1,920 B,
// bgRow 960 B, spanPx 960 B, dith 128 B, paletteStorage 1,408 B, smooth 514 B
// (528 aligned), weights 960 B. Read back as hot_used on /api/debug/heap with
// this animation resident rather than added up here.
// paletteStorage is 32-bit because EE.LDXQ.32 scales its index by four and so
// can only gather from a 32-bit table. That costs 704 B more than the 16-bit
// version would.
// Six row tables are in PSRAM, 960 B each, 5,760 B in all: ozHalf and osHalf,
// the outer disc's chord half widths, and czLoRow, czHiRow, csLoRow and
// csHiRow, the cut's per-row coverage boundaries. Each is read once per row
// rather than per pixel, which is the rule for what may leave the slab.
// The borrowed sine is 2048 B in the existing shared reservation. The palette
// has 256 real theme entries plus clamped ends for the device's plain gather.
// Exact integer roots. The float root is within one of the true value over
// this range (under 6e7), and the two loops settle it.
inline int isqrtFloor(int v) {
    if (v <= 0) return 0;
    int r = static_cast<int>(sqrtf(static_cast<float>(v)));
    while (r > 0 && r * r > v) --r;
    while ((r + 1) * (r + 1) <= v) ++r;
    return r;
}
inline int isqrtCeil(int v) {
    const int r = isqrtFloor(v);
    return r * r == v ? r : r + 1;
}
inline int floorDiv16(int v) { return v >= 0 ? (v >> 4) : -((-v + 15) >> 4); }
inline int ceilDiv16(int v) { return -floorDiv16(-v); }

// The cut circle's two soft-edge memberships, as predicates on a whole pixel.
// X is the Q4 distance from the cut centre along the row.
inline bool cutZeroAt(int x, int c16, int t) {
    const int X = x * 16 - c16;
    return X * X <= t;
}
inline bool cutSatRightAt(int x, int c16, int t) {
    const int X = x * 16 - c16;
    return X >= 0 && X * X >= t;
}
inline bool cutSatLeftAt(int x, int c16, int t) {
    const int X = x * 16 - c16;
    return X <= 0 && X * X >= t;
}

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
    paletteStorage = static_cast<uint32_t *>(allocHot(PAL_N * sizeof(uint32_t)));
    smooth = static_cast<uint16_t *>(allocHot(257 * sizeof(uint16_t)));
    weights = static_cast<uint16_t *>(allocHot(w * sizeof(uint16_t)));
    // Read once a row, so PSRAM. Both depend only on the disc radius, which
    // moves with the theme and the parameters, not with time.
    ozHalf = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
    osHalf = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
    czLoRow = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
    czHiRow = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
    csLoRow = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
    csHiRow = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
    if (!radCol || !radRow || !bgRow || !spanPx || !dith || !paletteStorage || !smooth || !weights ||
        !ozHalf || !osHalf || !czLoRow || !czHiRow || !csLoRow || !csHiRow) {
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
        uint16_t ramp[256];
        buildThemeRamp(ramp, 256);
        // Page: 0.6*ditherAmp, rounded in Q4, then *4 in squared radius.
        // Moving this to the final palette index would change the picture.
        const float amp = ditherAmp(ramp, 256) * 0.6f;
        for (int i = 0; i < 256; ++i) palette[i] = ramp[i];
        for (int k = 0; k < 64; ++k)
            dith[k] = static_cast<int16_t>(lroundf((BAYER8[k] - 31.5f) * (amp * 16.0f / 31.5f)));
        for (int i = -PAL_PAD; i < FLOOR; ++i) palette[i] = palette[FLOOR];
        for (int i = 256; i < 320; ++i) palette[i] = palette[255];
        const int rMax = cx < cy ? cx : cy;
        // Page size: 65..97 percent of half-panel radius, minimum 16 px.
        discR = rMax * (65 + static_cast<int>(p[1]) * 32 / 100) / 100;
        if (discR < 16) discR = 16;
        invK = 65536 * 256 / (2 * discR * SOFT_PX);
        // Exact thresholds on the coverage numerators. The kernel computes
        // u = 128 + ((q * invK) >> 16) and clamps it to [0,256], so u is 256
        // exactly when q >= ceil(8388608/invK) and 0 exactly when
        // q <= floor(-8323073/invK). Both ends are constant over a run, which
        // is what lets band() fill instead of compute.
        qaThr = (8388608 + invK - 1) / invK;
        qzThr = -((8323073 + invK - 1) / invK);
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
            // uo > 0 while dx*dx < sNz, uo == 256 while dx*dx <= s256.
            const int r2d = discR * discR;
            const int sNz = r2d - dy * dy - qzThr, s256 = r2d - dy * dy - qaThr;
            ozHalf[y] = static_cast<int16_t>(sNz > 0 ? isqrtCeil(sNz) - 1 : -1);
            osHalf[y] = static_cast<int16_t>(s256 >= 0 ? isqrtFloor(s256) : -1);
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
    // BOTH turn and swell use base>>7, which at the Speed 50 rate below is a
    // 6721.6 ms cycle. Body breathing is base*3>>9: 8962.2 ms. The base*3
    // product still wraps as uint32, matching the page's >>>0, even near
    // millis() rollover.
    // Speed follows the fleet's curve, speedMul(): 0.15x at 0, 1x at 50 and
    // 6.7x at 100 of the rate Speed 50 has since gm-33fm (bead gm-kh2s).
    // The multiplier is Q6 and 1248 at 50, which is the 19.5 units per
    // millisecond the quarter-rate law gave at Speed 50, so the shifts below
    // are unchanged and Speed 50 is the same picture. The old law read
    // 3 * (4 + p[0] * 44 / 100) quarters and covered 0.15x to 1.85x.
    // Rounded, not truncated: the nearest .5 boundary over Speed 0..100 is
    // 20 float ulps away, so the host, the device and the page agree.
    // The multiply widens to 64 bits first, as the quarter-rate law did: a
    // uint32 product would throw away the top bits of the rate before the
    // shift could use them. The page does the same arithmetic.
    const uint32_t speedQ6 = static_cast<uint32_t>(lroundf(1248.0f * speedMul(p[0])));
    const uint32_t base = static_cast<uint32_t>((static_cast<uint64_t>(tMs) * speedQ6) >> 6);
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
    // Where the cut circle's soft edge falls on each row, in whole pixels.
    // band() reads four numbers a row instead of taking two square roots, and
    // sqrtf is a library call from flash that cost 156 cycles each. Walking
    // from the previous row costs a comparison or two, because the edge moves
    // by about a pixel a row, and each walk is bounded by the centre pixel on
    // one side and by the ray it is looking for on the other, so a jump in the
    // centre costs steps and never correctness.
    {
        const int rr2 = discR * discR;
        const int zBase = 256 * (rr2 + qzThr) + 255, aBase = 256 * (rr2 + qaThr);
        const int xc = (icx16 + 8) >> 4, xr = ceilDiv16(icx16), xl = floorDiv16(icx16);
        int lo = xc, hi = xc, sLo = xl, sHi = xr;
        for (int y = 0; y < h; ++y) {
            const int dyi = y * 16 - icy16, dyi2 = dyi * dyi;
            const int t1 = zBase - dyi2, t2 = aBase - dyi2;
            if (t1 < 0 || !cutZeroAt(xc, icx16, t1)) {
                czLoRow[y] = 1;
                czHiRow[y] = 0;
            } else {
                if (hi < xc) hi = xc;
                while (cutZeroAt(hi + 1, icx16, t1)) ++hi;
                while (hi > xc && !cutZeroAt(hi, icx16, t1)) --hi;
                if (lo > xc) lo = xc;
                while (cutZeroAt(lo - 1, icx16, t1)) --lo;
                while (lo < xc && !cutZeroAt(lo, icx16, t1)) ++lo;
                czLoRow[y] = static_cast<int16_t>(lo);
                czHiRow[y] = static_cast<int16_t>(hi);
            }
            if (sHi < xr) sHi = xr;
            while (sHi > xr && cutSatRightAt(sHi - 1, icx16, t2)) --sHi;
            while (!cutSatRightAt(sHi, icx16, t2)) ++sHi;
            if (sLo > xl) sLo = xl;
            while (sLo < xl && cutSatLeftAt(sLo + 1, icx16, t2)) ++sLo;
            while (!cutSatLeftAt(sLo, icx16, t2)) --sLo;
            csLoRow[y] = static_cast<int16_t>(sLo);
            csHiRow[y] = static_cast<int16_t>(sHi);
        }
    }
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

// Exact integer roots. The float root is within one of the true value over
// this range (under 6e7), and the two loops settle it.

// One coverage factor at a time. Where the other factor is saturated at 256
// the product (a*256)>>8 is a, so a run that crosses only one edge costs one
// gather instead of two.
GM_ANIM_IRAM __attribute__((noinline)) void crescentOuterRef(uint16_t *out, int qOuter, int dOuter,
                                                             int inv, const uint16_t *sm, int n) {
    for (int x = 0; x < n; ++x) {
        int uo = 128 + ((qOuter * inv) >> 16);
        uo = uo < 0 ? 0 : (uo > 256 ? 256 : uo);
        out[x] = sm[uo];
        qOuter += dOuter; dOuter -= 2;
    }
}

GM_ANIM_IRAM __attribute__((noinline)) void crescentCutRef(uint16_t *out, int qCut, int dCut, int r2,
                                                           int inv, const uint16_t *sm, int n) {
    for (int x = 0; x < n; ++x) {
        int uc = 128 + ((((qCut >> 8) - r2) * inv) >> 16);
        uc = uc < 0 ? 0 : (uc > 256 ? 256 : uc);
        out[x] = sm[uc];
        qCut += dCut; dCut += 512;
    }
}

GM_ANIM_IRAM __attribute__((noinline)) void crescentFillRef(uint16_t *out, uint16_t v, int n) {
    for (int x = 0; x < n; ++x) out[x] = v;
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
            dst[row * w + x] = static_cast<uint16_t>(palette[idx]);
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
                                                             const uint32_t *pal, int bgBase, int body,
                                                             int n) {
    if (((uintptr_t)out & 15u) == 0) {
        // EE.LDXQ.32 reads eight palette entries with eight indexed loads and
        // one unzip, where the scalar gather below needs eleven instructions
        // for two pixels. The palette is 32-bit because the instruction scales
        // its index by four. Biasing both blend constants by 512 turns the
        // arithmetic shift by four into the padded palette's index directly:
        // floor((v+512)/16) is floor(v/16)+32, and 32 is PAL_PAD.
        const uint32_t coeffs[5] = {20u, 78u, (uint32_t)(bgBase + 512), (uint32_t)(body + 512), 1u};
        const uint32_t *coeff = coeffs;
        // The +512 bias makes the index count from the padded start, so the
        // gather's base is the storage base, not the zero entry.
        const uint32_t *palBase = pal - PAL_PAD;
        const int vblocks = n >> 3;
        uint16_t *vout = out;
        const int32_t *vcol = col;
        const uint16_t *vweight = weight;
        asm volatile("ee.vldbc.16.ip q3, %[coeff], 4\n"
                     "ee.vldbc.16.ip q4, %[coeff], 4\n"
                     "ee.vldbc.16.ip q5, %[coeff], 4\n"
                     "ee.vldbc.16.ip q6, %[coeff], 4\n"
                     "ee.vldbc.16.ip q2, %[coeff], 0\n"
                     "loopnez %[n], 1f\n"
                     "ssai 8\n"
                     "ee.vld.128.ip q0, %[col], 16\n"
                     "ee.vld.128.ip q1, %[col], 16\n"
                     "ee.vld.128.ip q7, %[weight], 16\n"
                     "ee.vunzip.16 q0, q1\n"
                     "ee.vld.128.ip q1, %[rr], 0\n"
                     "ee.vadds.s16 q0, q0, q1\n"
                     "ee.vmul.s16 q1, q0, q3\n"
                     "ee.vmul.s16 q0, q0, q4\n"
                     "ee.vsubs.s16 q1, q5, q1\n"
                     "ee.vadds.s16 q0, q0, q6\n"
                     "ee.vsubs.s16 q0, q0, q1\n"
                     "ee.vmul.s16 q0, q0, q7\n"
                     "ee.vadds.s16 q0, q0, q1\n"
                     "ssai 4\n"
                     "ee.vmul.s16 q0, q0, q2\n"
                     "ee.ldxq.32 q1, q0, %[pal], 0, 0\n"
                     "ee.ldxq.32 q1, q0, %[pal], 1, 1\n"
                     "ee.ldxq.32 q1, q0, %[pal], 2, 2\n"
                     "ee.ldxq.32 q1, q0, %[pal], 3, 3\n"
                     "ee.ldxq.32 q7, q0, %[pal], 0, 4\n"
                     "ee.ldxq.32 q7, q0, %[pal], 1, 5\n"
                     "ee.ldxq.32 q7, q0, %[pal], 2, 6\n"
                     "ee.ldxq.32 q7, q0, %[pal], 3, 7\n"
                     "ee.vunzip.16 q1, q7\n"
                     "ee.vst.128.ip q1, %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(vout), [col] "+&r"(vcol), [weight] "+&r"(vweight),
                       [coeff] "+&r"(coeff)
                     : [rr] "r"(rowRad), [pal] "r"(palBase), [n] "r"(vblocks)
                     : "memory");
        const int done = vblocks * 8;
        for (int x = done; x < n; ++x) {
            const int rad = col[x] + rowRad[x & 7];
            const int bg = bgBase - ((rad * 20) >> 8);
            const int lit = body + ((rad * 78) >> 8);
            out[x] = (uint16_t)pal[(bg + (((lit - bg) * weight[x]) >> 8)) >> 4];
        }
        return;
    }
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
                 "addx4 %[high], %[high], %[pal]\n"
                 "addx4 %[low], %[low], %[pal]\n"
                 "l32i %[high], %[high], 0\n"
                 "l32i %[low], %[low], 0\n"
                 "slli %[high], %[high], 16\n"
                 "or %[high], %[high], %[low]\n"
                 "s32i %[high], %[out], 0\n"
                 "ee.movi.32.a q0, %[high], 1\n"
                 "sext %[low], %[high], 15\n"
                 "srai %[high], %[high], 20\n"
                 "srai %[low], %[low], 4\n"
                 "addx4 %[high], %[high], %[pal]\n"
                 "addx4 %[low], %[low], %[pal]\n"
                 "l32i %[high], %[high], 0\n"
                 "l32i %[low], %[low], 0\n"
                 "slli %[high], %[high], 16\n"
                 "or %[high], %[high], %[low]\n"
                 "s32i %[high], %[out], 4\n"
                 "ee.movi.32.a q0, %[high], 2\n"
                 "sext %[low], %[high], 15\n"
                 "srai %[high], %[high], 20\n"
                 "srai %[low], %[low], 4\n"
                 "addx4 %[high], %[high], %[pal]\n"
                 "addx4 %[low], %[low], %[pal]\n"
                 "l32i %[high], %[high], 0\n"
                 "l32i %[low], %[low], 0\n"
                 "slli %[high], %[high], 16\n"
                 "or %[high], %[high], %[low]\n"
                 "s32i %[high], %[out], 8\n"
                 "ee.movi.32.a q0, %[high], 3\n"
                 "sext %[low], %[high], 15\n"
                 "srai %[high], %[high], 20\n"
                 "srai %[low], %[low], 4\n"
                 "addx4 %[high], %[high], %[pal]\n"
                 "addx4 %[low], %[low], %[pal]\n"
                 "l32i %[high], %[high], 0\n"
                 "l32i %[low], %[low], 0\n"
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
        out[x] = (uint16_t)pal[(bg + (((lit - bg) * weight[x]) >> 8)) >> 4];
    }
}

// Ten instructions a pixel against the two-factor loop's twenty-two. Same
// bias-by-128 trick: the table pointer carries the +128 and the clamp is to
// [-128,128], which removes two adds.
GM_ANIM_IRAM __attribute__((noinline)) void crescentOuterAsm(uint16_t *out, int qOuter, int dOuter,
                                                             int inv, const uint16_t *sm, int n) {
    sm += 128;
    int uo, lo, hi;
    asm volatile("movi %[lo], -128\n"
                 "movi %[hi], 128\n"
                 "loopnez %[n], 1f\n"
                 "mull %[uo], %[qo], %[inv]\n"
                 "srai %[uo], %[uo], 16\n"
                 "min %[uo], %[uo], %[hi]\n"
                 "max %[uo], %[uo], %[lo]\n"
                 "addx2 %[uo], %[uo], %[sm]\n"
                 "add %[qo], %[qo], %[dqo]\n"
                 "l16ui %[uo], %[uo], 0\n"
                 "addi %[dqo], %[dqo], -2\n"
                 "s16i %[uo], %[out], 0\n"
                 "addi %[out], %[out], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [qo] "+&r"(qOuter), [dqo] "+&r"(dOuter),
                   [uo] "=&r"(uo), [lo] "=&r"(lo), [hi] "=&r"(hi)
                 : [inv] "r"(inv), [sm] "r"(sm), [n] "r"(n)
                 : "memory");
}

GM_ANIM_IRAM __attribute__((noinline)) void crescentCutAsm(uint16_t *out, int qCut, int dCut, int r2,
                                                           int inv, const uint16_t *sm, int n) {
    sm += 128;
    int uc, lo, hi;
    asm volatile("movi %[lo], -128\n"
                 "movi %[hi], 128\n"
                 "loopnez %[n], 1f\n"
                 "srai %[uc], %[qc], 8\n"
                 "sub %[uc], %[uc], %[r2]\n"
                 "mull %[uc], %[uc], %[inv]\n"
                 "srai %[uc], %[uc], 16\n"
                 "min %[uc], %[uc], %[hi]\n"
                 "max %[uc], %[uc], %[lo]\n"
                 "addx2 %[uc], %[uc], %[sm]\n"
                 "add %[qc], %[qc], %[dqc]\n"
                 "l16ui %[uc], %[uc], 0\n"
                 "addmi %[dqc], %[dqc], 512\n"
                 "s16i %[uc], %[out], 0\n"
                 "addi %[out], %[out], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [qc] "+&r"(qCut), [dqc] "+&r"(dCut),
                   [uc] "=&r"(uc), [lo] "=&r"(lo), [hi] "=&r"(hi)
                 : [r2] "r"(r2), [inv] "r"(inv), [sm] "r"(sm), [n] "r"(n)
                 : "memory");
}

// Two pixels a store. The run lengths here are whole constant stretches of a
// row, so this is where most of the old kernel's pixels went.
GM_ANIM_IRAM __attribute__((noinline)) void crescentFillAsm(uint16_t *out, uint16_t v, int n) {
    if (n <= 0) return;
    // Eight pixels a store once the cursor is aligned. EE.VST.128.IP clears
    // the low four address bits, so the prefix is walked one pixel at a time.
    while (n > 0 && (((uintptr_t)out & 15u) != 0)) { *out++ = v; --n; }
    const int blocks = n >> 3;
    if (blocks > 0) {
        const uint16_t vv = v;
        const uint16_t *vp = &vv;
        uint16_t *q = out;
        asm volatile("ee.vldbc.16.ip q0, %[vp], 0\n"
                     "loopnez %[n], 1f\n"
                     "ee.vst.128.ip q0, %[q], 16\n"
                     "1:\n"
                     : [q] "+&r"(q), [vp] "+&r"(vp)
                     : [n] "r"(blocks)
                     : "memory");
        out += blocks * 8;
        n -= blocks * 8;
    }
    while (n-- > 0) *out++ = v;
}
#endif

// A row's coverage is mostly constant. uo is 256 across the body of the disc
// and 0 outside its soft edge; uc is 0 inside the cut disc and 256 outside its
// soft edge. Both edges are 26 pixels wide, so on a full row only about a
// third of the pixels sit on an edge and the rest are a fill of 0 or 256.
// The thresholds above turn "is this run constant" into exact integer
// comparisons on the same numerators the kernel uses, and the run ends come
// from exact integer roots, so the picture does not change.
//
// Where exactly one factor is on its edge the other is 256, and (a*256)>>8 is
// a, so those runs take a one-gather kernel. Only where the two edges overlap
// does the two-gather kernel run.
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM) && GM_BGANIM_CRESCENT_ASM
    if (!laneSafe) { bandRef(dst, y0, rows, w, tMs, p); return; }
#define CR_OUTER crescentOuterAsm
#define CR_CUT crescentCutAsm
#define CR_BOTH crescentWeightsAsm
#define CR_FILL crescentFillAsm
#else
#define CR_OUTER crescentOuterRef
#define CR_CUT crescentCutRef
#define CR_BOTH crescentWeightsRef
#define CR_FILL crescentFillRef
#endif
    const int cx = allocW / 2, cy = allocH / 2, r2 = discR * discR;
    for (int row = 0; row < rows; ++row) {
        const int y = y0 + row, span = spanPx[y];
        int a = 0, b = 0;
        if (span >= 0) {
            a = cx - span; b = cx + span + 1;
            if (a < 0) a = 0;
            if (b > w) b = w;
        }
        if (a > 0) CR_FILL(weights, 0, a);
        if (b < w) CR_FILL(weights + b, 0, w - b);
        const int dy = y - cy, dy2 = dy * dy;
        const int dyi = y * 16 - icy16, dyi2 = dyi * dyi;
        const int nzR = ozHalf[y], satR = osHalf[y];
        const int oz0 = cx - nzR, oz1 = cx + nzR;   // empty when nzR < 0
        const int os0 = cx - satR, os1 = cx + satR; // empty when satR < 0
        const int cz0 = czLoRow[y], cz1 = czHiRow[y];
        const int csLo = csLoRow[y], csHi = csHiRow[y];
        int bp[8], nb = 0;
        const int cand[8] = {oz0, oz1 + 1, os0, os1 + 1, cz0, cz1 + 1, csLo + 1, csHi};
        for (int i = 0; i < 8; ++i) {
            const int v = cand[i];
            if (v > a && v < b) bp[nb++] = v;
        }
        for (int i = 1; i < nb; ++i) { // insertion sort, at most eight entries
            const int v = bp[i];
            int j = i - 1;
            while (j >= 0 && bp[j] > v) { bp[j + 1] = bp[j]; --j; }
            bp[j + 1] = v;
        }
        int l = a;
        for (int i = 0; i <= nb; ++i) {
            const int r = i < nb ? bp[i] : b;
            if (r <= l) continue;
            const int len = r - l;
            const bool uoPos = l >= oz0 && l <= oz1;
            const bool ucZero = l >= cz0 && l <= cz1;
            const bool uoSat = l >= os0 && l <= os1;
            const bool ucSat = l <= csLo || l >= csHi;
            if (!uoPos || ucZero) {
                CR_FILL(weights + l, 0, len);
            } else if (uoSat && ucSat) {
                CR_FILL(weights + l, 256, len);
            } else {
                const int dx = l - cx, xi = l * 16 - icx16;
                if (uoSat) {
                    CR_CUT(weights + l, xi * xi + dyi2, 32 * xi + 256, r2, invK, smooth, len);
                } else if (ucSat) {
                    CR_OUTER(weights + l, r2 - dx * dx - dy2, -2 * dx - 1, invK, smooth, len);
                } else {
                    CR_BOTH(weights + l, r2 - dx * dx - dy2, -2 * dx - 1, xi * xi + dyi2,
                            32 * xi + 256, r2, invK, smooth, len);
                }
            }
            l = r;
        }
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM) && GM_BGANIM_CRESCENT_ASM
        alignas(16) int16_t rowRad[8];
        for (int k = 0; k < 8; ++k)
            rowRad[k] = static_cast<int16_t>(radRow[y] + dith[(y & 7) * 8 + k] * 4);
        crescentShadeAsm(dst + row * w, radCol, weights, rowRad, palette,
                         BG_HI * 16 + bgRow[y], bodyBase, w);
#else
        const int16_t *off = dith + (y & 7) * 8;
        const int rr = radRow[y], bgBase = BG_HI * 16 + bgRow[y], litBase = bodyBase;
        for (int x = 0; x < w; ++x) {
            const int rad = radCol[x] + off[x & 7] * 4 + rr;
            const int bg = bgBase - (static_cast<int32_t>(static_cast<uint32_t>(rad) * BGF) >> 8);
            const int body = litBase + (static_cast<int32_t>(static_cast<uint32_t>(rad) * LIMB) >> 8);
            int idx = (bg + (static_cast<int32_t>(static_cast<uint32_t>(body - bg) * weights[x]) >> 8)) >> 4;
            idx = idx < FLOOR ? FLOOR : (idx > 255 ? 255 : idx);
            dst[row * w + x] = static_cast<uint16_t>(palette[idx]);
        }
#endif
    }
#undef CR_OUTER
#undef CR_CUT
#undef CR_BOTH
#undef CR_FILL
}

void release() {
    releaseTable(radCol, static_cast<size_t>(allocW) * sizeof(int32_t));
    releaseTable(radRow, static_cast<size_t>(allocH) * sizeof(int32_t));
    releaseTable(bgRow, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(spanPx, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(paletteStorage, PAL_N * sizeof(uint32_t));
    releaseTable(smooth, 257 * sizeof(uint16_t));
    releaseTable(weights, static_cast<size_t>(allocW) * sizeof(uint16_t));
    releaseTable(ozHalf, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(osHalf, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(czLoRow, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(czHiRow, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(csLoRow, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(csHiRow, static_cast<size_t>(allocH) * sizeof(int16_t));
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
