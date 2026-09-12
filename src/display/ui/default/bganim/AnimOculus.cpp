#ifndef GAGGIMATE_SIM

// "Oculus" - a softly lit circular opening, a faint outer halo, and a low
// ripple crawling outward while the opening and its light breathe. This is
// entry 16 of tools/animbench/web/anim_bench.html, including its redesigned
// palette. Geometry is separable squared radius, not distance: the column
// and row terms add in Q4 palette-index units, then one RGB565 gather gives
// the pixel. No square root, float, or moving state lives in the pixel loop.
//
// The page changes all 256 palette entries every frame. Speed follows the
// fleet's curve (bead gm-kh2s): at the default Speed 50 the main breath and
// ripple period is 5.04123 s, the second breath is 8.06557 s and the gain
// breath is 6.72164 s, and the whole set scales by speedMul(). Ripple phase
// rises by 15 sine-table units per radial index and retreats with time, so
// the wave travels outward. Pixel speed varies with radius because the
// coordinate is squared radius. Keep these integer clocks, including each
// uint32 multiplication's wrap.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

#ifndef GM_BGANIM_OCULUS_ASM
#define GM_BGANIM_OCULUS_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int HALF_SPAN = 1904; // Q4 radius-squared contribution at either axis edge
constexpr int DISC_IDX = 119;  // 1904 / 16: palette coordinate of the panel circle
constexpr int INDEX_MAX = 4095; // largest Q4 sum that still gathers index 255
constexpr int PHASES = 8;       // the page's eight Bayer row phases
static_assert(SIN_N == 1024 && SIN_AMP == 512, "Oculus uses the page's 1024-entry, amplitude-512 sine table");
static_assert(INDEX_MAX == 4095, "Oculus assembly extracts eight palette-index bits above four Q4 bits");

int16_t *colTerm = nullptr;    // raw squared-x terms, frame() only, PSRAM
int16_t *colTermPh = nullptr;  // eight dithered column copies, hot slab
int16_t *rowTerm = nullptr;    // squared-y terms, hot slab
uint16_t *themeRamp = nullptr; // the active theme, frame() only, PSRAM
uint16_t *palette = nullptr;   // radial light profile, hot slab
int16_t *dithOff = nullptr;    // 64 Q4 Bayer offsets, frame() only, PSRAM
const int16_t *sine = nullptr; // borrowed shared sine table, never released here

uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFFu;
bool geomValid = false;
int allocW = 0, allocH = 0, colStride = 0;

// At 480x480 the 9,216 B per-animation hot slab holds:
//   colTermPh   8 * 480 * 2 = 7,680 B, read every pixel
//   rowTerm         480 * 2 =   960 B, read every row
//   palette         256 * 2 =   512 B, gathered every pixel
//                             9,152 B total, 64 B spare
// PSRAM holds colTerm 960 B, themeRamp 512 B, and dithOff 128 B: 1,600 B.
// The column stride rounds w up to eight halfwords so every phase starts
// on 16 bytes, also at widths 466/233. Padding is allocation-only and never
// changes an x coordinate or the dither. allocHot aligns every table base;
// the 480-wide budget above includes all alignment. Shared sine storage is
// already reserved outside these 9,216 B. No private static lookup tables.
void release();

bool init(int w, int h) {
    if (w <= 0 || h <= 0) {
        release();
        return false;
    }
    if (allocW != w || allocH != h) {
        release();
    }
    allocW = w;
    allocH = h;
    colStride = (w + 7) & ~7;
    sine = sinLut();
    if (sine == nullptr) {
        release();
        return false;
    }
    if (colTerm == nullptr) colTerm = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
    if (colTermPh == nullptr)
        colTermPh = static_cast<int16_t *>(allocHot(PHASES * colStride * sizeof(int16_t)));
    if (rowTerm == nullptr) rowTerm = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
    if (themeRamp == nullptr) themeRamp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    if (palette == nullptr) palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    if (dithOff == nullptr) dithOff = static_cast<int16_t *>(alloc(64 * sizeof(int16_t)));
    if (!colTerm || !colTermPh || !rowTerm || !themeRamp || !palette || !dithOff) {
        release(); // OOM unwinds every successful allocation before the retry.
        return false;
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    const bool changed = memcmp(p, lastP, 4) != 0 || gen != lastThemeGen;
    if (changed || !geomValid) {
        buildThemeRamp(themeRamp, 256);
        lastThemeGen = gen;
        // The page measures the straight theme ramp, not the moving radial
        // palette. 0.45 of its step amplitude, in 16 Q4 units per index.
        // lroundf matches the page's lround (half away from zero). Float is
        // confined to table construction; bandRef and the kernel are integer.
        const float amp = ditherAmp(themeRamp, 256) * 0.45f;
        for (int k = 0; k < 64; k++) {
            dithOff[k] = static_cast<int16_t>(
                lroundf((BAYER8[k] - 31.5f) * (amp * 16.0f / 31.5f)));
        }
        const int cx = w / 2, cy = h / 2;
        const int sx = cx > 0 ? cx : 1, sy = cy > 0 ? cy : 1;
        // Same Q16 reciprocal truncation as the page, then >>16 after the
        // square. At panel sizes all products are <=1904*65536=124,780,544.
        const int kx = HALF_SPAN * 65536 / (sx * sx);
        const int ky = HALF_SPAN * 65536 / (sy * sy);
        for (int x = 0; x < w; x++) {
            const int dx = x - cx;
            const int v = (dx * dx * kx) >> 16;
            colTerm[x] = static_cast<int16_t>(v > HALF_SPAN ? HALF_SPAN : v);
        }
        for (int y = 0; y < h; y++) {
            const int dy = y - cy;
            const int v = (dy * dy * ky) >> 16;
            rowTerm[y] = static_cast<int16_t>(v > HALF_SPAN ? HALF_SPAN : v);
        }
        for (int ph = 0; ph < PHASES; ph++) {
            int16_t *ct = colTermPh + ph * colStride;
            for (int x = 0; x < w; x++) {
                int v = colTerm[x] + dithOff[ph * 8 + (x & 7)];
                v = v < 0 ? 0 : (v > INDEX_MAX - HALF_SPAN ? INDEX_MAX - HALF_SPAN : v);
                ct[x] = static_cast<int16_t>(v);
            }
        }
        geomValid = true;
    }
    memcpy(lastP, p, 4);

    // JavaScript >>>0 truncates EACH product, before the subsequent >>>.
    // Unsigned C++ products preserve those wraps, even near millis() rollover.
    //
    // Speed follows the fleet's curve, speedMul(): 0.15x at 0, 1x at 50 and
    // 6.7x at 100 of the rate Speed 50 has always had (bead gm-kh2s). The
    // old private law, 4 + p[0] * 44 / 100, ran 4 to 48 and so covered 12x
    // where the fleet covers 45x, which left the whole slider compressed
    // toward the middle. The multiplier is Q6 and 1664 at 50, the old 26
    // with six more fraction bits, so the shifts below are the old 7, 10, 7
    // and 9 plus six and every clock at Speed 50 is unchanged. The largest
    // shift is 16, so a uint32 wrap of any of these products moves the sine
    // index by a whole number of 1024-entry cycles and never shows.
    // Rounded, not truncated: the nearest .5 boundary over Speed 0 to 100 is
    // 19 float ulps away, so exp2f on the host, exp2f on the device and
    // Math.pow on the page land on the same integer. The breath and ripple
    // period over the slider is 33.86, 13.06, 5.04, 1.95 and 0.75 s at
    // Speed 0, 25, 50, 75 and 100.
    //
    // Half change time at those five settings reads 5542, 3050, 1090, 484
    // and 82 ms against fleet targets of 8050, 3120, 1200, 463 and 179. The
    // first four are inside the accept band and Speed 100 is not, but the
    // reading there is the measurement, not the picture: motion.js estimates
    // its "unrelated" level from the top third of thirty sampled pairs, and
    // for Oculus at Speed 100 that estimate is 40.1 against 57 to 59 at every
    // other setting, which drops the half threshold with it. Speed 95 reads
    // 173 ms, and the same measurement with 216 pairs reads 123 ms at
    // Speed 100. Do not slow the animation to chase the 82.
    const uint32_t speedQ6 = static_cast<uint32_t>(lroundf(1664.0f * speedMul(p[0])));
    const uint32_t base = tMs * speedQ6;
    const uint32_t phB = base >> 13, phB2 = (base * 5u) >> 16;
    const uint32_t phRip = base >> 13, phGain = (base * 3u) >> 15;
    const int edge = 12 + static_cast<int>(p[3]) * 30 / 100;  // 12..42 radial indices
    const int breath = 6 + static_cast<int>(p[2]) * 24 / 100; // 6..30 radial indices
    const int lo = 14 + edge + breath, hi = DISC_IDX - 10 - edge - breath;
    const int mean = lo + static_cast<int>(p[1]) * (hi > lo ? hi - lo : 0) / 100;
    // Signed >> matches JS's arithmetic shift on both supported compilers.
    // Divisions elsewhere truncate toward zero, exactly like Math.trunc.
    int a = mean + ((sine[phB & 1023] * breath) >> 9) + ((sine[phB2 & 1023] * breath) >> 10);
    a = a < 8 ? 8 : (a > DISC_IDX - 6 ? DISC_IDX - 6 : a);
    const int halo = a + 34 + (edge >> 1); // halo offset grows with edge softness
    const int gain = 232 + ((sine[phGain & 1023] * 22) >> 9); // Q8 gain 210..254
    const int inner = a - edge > 1 ? a - edge : 1;
    const int outerStart = a + edge;
    const int outerSpan = DISC_IDX - outerStart > 1 ? DISC_IDX - outerStart : 1;
    for (int i = 0; i < 256; i++) {
        int v;
        if (i <= inner) {
            v = 4 + i * 17 / inner; // dark interior rises from theme index 4 to 21
        } else if (i >= outerStart) {
            int j = i - outerStart;
            if (j > outerSpan) j = outerSpan;
            v = 74 - j * 58 / outerSpan; // outside falls from 74 to 16
            if (i > DISC_IDX) {
                int k2 = i - DISC_IDX;
                if (k2 > 120) k2 = 120;
                v -= k2 * 12 / 120; // another 12 indices of falloff past the panel circle
            }
        } else {
            const int u = ((i - inner) << 8) / (outerStart - inner);
            int tt = (u * u * (768 - 2 * u)) >> 16; // Q8 smoothstep u*u*(3-2*u)
            tt = tt < 0 ? 0 : (tt > 256 ? 256 : tt);
            v = 21 + (((74 - 21) * tt) >> 8);
        }
        int d = i - a, ad = d < 0 ? -d : d;
        if (ad < edge) {
            const int u = 256 - ad * 256 / edge;
            const int sm = (u * u * (768 - 2 * u)) >> 16;
            v += ((132 - v) * (sm < 0 ? 0 : (sm > 256 ? 256 : sm))) >> 8;
        }
        d = i - halo;
        ad = d < 0 ? -d : d;
        const int hw = edge + 8; // halo is wider, with only 14 indices of light
        if (ad < hw) {
            const int u = 256 - ad * 256 / hw, kk = (u * u) >> 8;
            v += (kk * 14) >> 8;
        }
        // 15 sine units per radial index, amplitude 15; the page's "3.5
        // cycles" describes the field rather than a linear pixel frequency.
        v += (sine[(static_cast<uint32_t>(i * 15) - phRip) & 1023] * 15) >> 9;
        v = (v * gain) >> 8;
        palette[i] = themeRamp[v < 0 ? 0 : (v > 255 ? 255 : v)];
    }
}

// Portable spec: ct[x] in 0..2191 plus rt in 0..1904 stays in 0..4095,
// so >>4 is already 0..255 and the page's final &255 is redundant. No row
// pairing or call-local phase: y&7 always selects the absolute Bayer row.
// The page and host PPM can differ by one RGB888 unit because the page
// replicates RGB565 channel bits and the bench scales them by 255/31 or
// 255/63. Their RGB565 pixels are identical at the three golden times.
GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint16_t *__restrict pal = palette;
    for (int y = y0; y < y0 + rows; y++) {
        const int rt = rowTerm[y];
        const int16_t *__restrict ct = colTermPh + (y & 7) * colStride;
        int x = 0;
        for (; x + 1 < w; x += 2) {
            const uint16_t c0 = pal[(ct[x] + rt) >> 4];
            const uint16_t c1 = pal[(ct[x + 1] + rt) >> 4];
            *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(c0) | (static_cast<uint32_t>(c1) << 16);
            dst += 2;
        }
        for (; x < w; x++) *dst++ = pal[(ct[x] + rt) >> 4];
    }
}

#if GM_BGANIM_OCULUS_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14.2's bandRef .L4 loop is 15 instructions per pair, already with
// every scalar load-use gap filled. The tail below transcribes that exact
// load/add/shift/gather/pack/store schedule. The main loop's edge is PIE:
// one aligned vector load and one VADDS replace eight column loads and
// eight scalar adds. MOVI.32.A extracts pairs directly into ARs, avoiding
// the scratch-buffer traffic that lost to GCC in earlier fleet kernels.
//
// Main body: VLD + column advance + VADDS, four 10-instruction pair gathers,
// output advance = 44 instructions / 8 pixels = 5.5 per pixel (GCC: 7.5).
// The column advance fills VLD's stage-2 to stage-1 dependency slot. Each
// scalar load has an independent instruction before its use; VADDS and
// MOVI.32.A define at stage 1, so their consumers need no gap. This is an
// issue-count lower bound with hot data, not a measured device cycle time.
// The 15-instruction scalar pair body is 7.5 per pixel, also without an
// exposed load-use gap. Both loops are under 256 bytes and never nested.
//
// ct is 16-byte aligned by allocHot and colStride, and only full eight-
// pixel blocks are loaded. out needs only BgAnim.h's four-byte alignment;
// all stores are scalar pairs, so 466/233 widths and interlaced rows are
// safe. No padded column is read and no pixel outside n is stored.
// ct=0..2191 and rt=0..1904 give sums 0..4095, so signed VADDS cannot
// saturate and EXTUI bits 4..11 implements >>4 exactly in each halfword.
//
// GCC never allocates q registers and has no q-register clobber syntax.
// q0/q1 are private to this block. SAR is untouched. Never write CPENABLE:
// FreeRTOS's lazy CP3 exception owns enabling and saving the task's state.
// MOVI.32.A selectors 0..3 were executed separately in the QEMU probe
// tests/anim_oculus/probe_movi before using them here. QEMU proves pixels;
// device animtest parity and production band/bandRef timing remain rung 4.
GM_ANIM_IRAM __attribute__((noinline)) void oculusRowAsm(uint16_t *out, const int16_t *ct,
                                                       const uint16_t *pal, int rt, int n) {
    if (n <= 0) return;
    const int blocks = n >> 3;
    if (blocks != 0) {
        const uint32_t packedRow = (uint32_t)rt * 0x00010001u;
        uint32_t lo, hi;
        asm volatile("ee.movi.32.q q1, %[rt], 0\n"
                     "ee.movi.32.q q1, %[rt], 1\n"
                     "ee.movi.32.q q1, %[rt], 2\n"
                     "ee.movi.32.q q1, %[rt], 3\n"
                     "loopnez %[n], 1f\n"
                     "ee.vld.128.ip q0, %[ct], 0\n"
                     "addi    %[ct], %[ct], 16\n"
                     "ee.vadds.s16 q0, q0, q1\n"
                     "ee.movi.32.a q0, %[hi], 0\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 0\n"
                     "ee.movi.32.a q0, %[hi], 1\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 4\n"
                     "ee.movi.32.a q0, %[hi], 2\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 8\n"
                     "ee.movi.32.a q0, %[hi], 3\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 12\n"
                     "addi    %[out], %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out), [ct] "+&r"(ct), [lo] "=&r"(lo), [hi] "=&r"(hi)
                     : [n] "r"(blocks), [rt] "r"(packedRow), [pal] "r"(pal)
                     : "memory");
    }
    const int pairs = (n & 7) >> 1;
    int lo, hi;
    asm volatile("loopnez %[n], 1f\n"
                 "l16si   %[hi], %[ct], 2\n"
                 "l16si   %[lo], %[ct], 0\n"
                 "add     %[hi], %[hi], %[rt]\n"
                 "srai    %[hi], %[hi], 4\n"
                 "add     %[lo], %[lo], %[rt]\n"
                 "addx2   %[hi], %[hi], %[pal]\n"
                 "srai    %[lo], %[lo], 4\n"
                 "l16ui   %[hi], %[hi], 0\n"
                 "addx2   %[lo], %[lo], %[pal]\n"
                 "l16ui   %[lo], %[lo], 0\n"
                 "slli    %[hi], %[hi], 16\n"
                 "or      %[hi], %[hi], %[lo]\n"
                 "s32i    %[hi], %[out], 0\n"
                 "addi    %[ct], %[ct], 4\n"
                 "addi    %[out], %[out], 4\n"
                 "1:\n"
                 : [out] "+&r"(out), [ct] "+&r"(ct), [lo] "=&r"(lo), [hi] "=&r"(hi)
                 : [n] "r"(pairs), [rt] "r"(rt), [pal] "r"(pal)
                 : "memory");
    if (n & 1) *out = pal[(*ct + rt) >> 4];
}
#endif

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_OCULUS_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    for (int y = y0; y < y0 + rows; y++) {
        oculusRowAsm(dst, colTermPh + (y & 7) * colStride, palette, rowTerm[y], w);
        dst += w;
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(colTermPh, static_cast<size_t>(PHASES * colStride) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(themeRamp, 256 * sizeof(uint16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(dithOff, 64 * sizeof(int16_t));
    sine = nullptr;
    allocW = allocH = colStride = 0;
    lastThemeGen = 0xFFFFFFFFu;
    geomValid = false;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

extern const BgAnimation bg_anim_oculus;
const BgAnimation bg_anim_oculus = {
    "oculus",
    "Oculus",
    {{"speed", "Speed", 50}, {"diameter", "Diameter", 65}, {"breath", "Breath", 20}, {"edge", "Edge softness", 55}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
