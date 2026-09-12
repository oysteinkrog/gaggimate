#ifndef GAGGIMATE_SIM

// "Brushed" - a broad travelling reflection over fixed horizontal grain,
// with a shorter swell moving the other way and a slow vertical tilt. This
// is entry 14 in tools/animbench/web/anim_bench.html, including its metal
// palette: a dark base, a broad highlight shoulder and a small second one.
// The grain holds for 4..12 rows and is smoothed once at init, so the surface
// stays still while the light moves across it.
//
// The page's separable field is kept exactly: eight Bayer phase copies of
// colTerm, plus one rowTerm at the absolute y, in sixteenths of a palette
// index. bandRef gathers palette[(colTermPh[x] + rowTerm[y]) >> 4]. Sine
// samples have amplitude 512, spatial steps are Q4 sine-table units per
// pixel, and phase products wrap at 32 bits exactly as JS >>> 0. There is
// no per-pixel float or dependency on band height. Each physical row has
// its own grain and y & 7 dither phase.
// The host PPM writer expands RGB565 with channel * 255 / (31 or 63),
// while the page replicates high bits. Their RGB888 values can differ by
// one with identical RGB565 pixels; this is export rounding, not the look.

#include "BgAnim.h"
#include "BgAnimCommon.h"

#ifndef GM_BGANIM_BRUSHED_ASM
#define GM_BGANIM_BRUSHED_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int INDEX_MAX = 4095; // 256 palette entries, 16 field units each
constexpr int MID = 2048;       // centre of the page's 12-bit field

int16_t *colTerm = nullptr;   // page's un-dithered column table, frame only
int16_t *colTermPh = nullptr; // eight Bayer phases, one read per pixel
int16_t *rowTerm = nullptr;   // one read per absolute row
int16_t *grainBase = nullptr; // fixed, smoothed grain, frame only
int16_t *dithOff = nullptr;   // 64 offsets in sixteenths of a palette index
uint16_t *ramp = nullptr;    // 256-entry theme ramp, palette rebuild only
uint16_t *palette = nullptr; // shaped metal curve, one gather per pixel
const int16_t *sl = nullptr; // borrowed shared SIN_N-entry sine table

uint32_t lastThemeGen = 0xFFFFFFFF;
bool paletteValid = false;
int allocW = 0, allocH = 0, colStride = 0;

// Table budget at 480x480, against the 9,216 B animation slab:
//   colTermPh  7,680 B  slab, 8 * round_up(w, 8) * sizeof(int16_t)
//   rowTerm      960 B  slab, h * sizeof(int16_t)
//   palette      512 B  slab, 256 * sizeof(uint16_t)
//              9,152 B  total, 64 B spare
//   colTerm      960 B  PSRAM, w * sizeof(int16_t)
//   grainBase    960 B  PSRAM, h * sizeof(int16_t)
//   dithOff      128 B  PSRAM, 64 * sizeof(int16_t)
//   ramp         512 B  PSRAM, 256 * sizeof(uint16_t)
//              2,560 B  total PSRAM
// rowTerm temporarily holds the page's raw grain during init, before frame
// builds row terms. No extra raw allocation or permanent BSS table is needed.
// Every phase starts on a 16-byte boundary, even at widths 466 and 233.
// Padding is never sampled; rounding the stride does not rescale the image.
void release();

bool init(int w, int h) {
    if (w != allocW || h != allocH) {
        release();
    }
    if (palette != nullptr) {
        return true; // all allocations and the grain build already succeeded
    }
    // Supported panel dimensions also bound the stride and slab footprint.
    if (w <= 0 || h <= 0 || w > 480 || h > 480) {
        release();
        return false;
    }
    allocW = w;
    allocH = h;
    colStride = (w + 7) & ~7;
    sl = sinLut();
    if (sl == nullptr) {
        release();
        return false;
    }
    colTerm = static_cast<int16_t *>(alloc(static_cast<size_t>(w) * sizeof(int16_t)));
    colTermPh = static_cast<int16_t *>(allocHot(static_cast<size_t>(8 * colStride) * sizeof(int16_t)));
    rowTerm = static_cast<int16_t *>(allocHot(static_cast<size_t>(h) * sizeof(int16_t)));
    grainBase = static_cast<int16_t *>(alloc(static_cast<size_t>(h) * sizeof(int16_t)));
    dithOff = static_cast<int16_t *>(alloc(64 * sizeof(int16_t)));
    ramp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    if (!colTerm || !colTermPh || !rowTerm || !grainBase || !dithOff || !ramp || !palette) {
        release(); // no partial set may survive a failed init or its retry
        return false;
    }

    // Same xorshift seed, draw order, hold lengths and 7:1 coarse/fine mix
    // as the page. Signed right shifts on GCC round down, like JS >>.
    uint32_t seed = 0x9E3779B9u;
    int coarse = 0, hold = 0;
    for (int y = 0; y < h; y++) {
        if (hold == 0) {
            coarse = static_cast<int>(nextRand(seed) & 1023u) - 512;
            hold = 4 + static_cast<int>(nextRand(seed) % 9u);
        }
        hold--;
        const int fine = static_cast<int>(nextRand(seed) & 1023u) - 512;
        rowTerm[y] = static_cast<int16_t>((coarse * 7 + fine) >> 3);
    }
    // One 1-2-1 pass with replicated endpoints. Every neighbor is from the
    // original raw grain, not from an in-place smoothing recurrence.
    for (int y = 0; y < h; y++) {
        const int a = rowTerm[y > 0 ? y - 1 : 0];
        const int b = rowTerm[y];
        const int c = rowTerm[y < h - 1 ? y + 1 : h - 1];
        grainBase[y] = static_cast<int16_t>((a + 2 * b + c) >> 2);
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (!paletteValid || gen != lastThemeGen) {
        buildThemeRamp(ramp, 256);
        for (int i = 0; i < 256; i++) {
            // Page's metal curve in theme-ramp positions: a 7..46 base and
            // squared triangular shoulders, centre/radius/gain 182/56/132
            // and 238/18/60. Divisions truncate before squaring.
            int v = 7 + ((i * 40) >> 8);
            int d = i - 182;
            int ad = d < 0 ? -d : d;
            if (ad < 56) {
                const int k = 256 - ad * 256 / 56;
                v += (((k * k) >> 8) * 132) >> 8;
            }
            d = i - 238;
            ad = d < 0 ? -d : d;
            if (ad < 18) {
                const int k = 256 - ad * 256 / 18;
                v += (((k * k) >> 8) * 60) >> 8;
            }
            v = v < 0 ? 0 : (v > 255 ? 255 : v);
            palette[i] = ramp[v];
        }
        // Measured on the shaped RGB565 palette, then scaled by the page's
        // 0.75 and 16 pre-shift units. lroundf matches its lround (ties away
        // from zero). Float versus JS double rounding may move an offset by
        // one field unit at a tie, never change the palette mapping.
        const float amp = ditherAmp(palette, 256) * 0.75f;
        for (int k = 0; k < 64; k++) {
            const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 16.0f / 31.5f);
            dithOff[k] = static_cast<int16_t>(lroundf(d));
        }
        lastThemeGen = gen;
        paletteValid = true;
    }
    // The page also keys on params, but palette/dither use no parameter.
    // Rebuilding only on theme changes therefore produces the same tables.
    // Speed follows the fleet's curve, speedMul(): 0.15x at 0, 1x at 50 and
    // 6.7x at 100 of the rate Speed 50 has since gm-33fm (bead gm-kh2s).
    // The multiplier is Q6 and 1664 at 50, the old rate of 26 with six
    // fraction bits, and the three shifts below take those bits back, so
    // Speed 50 is the same picture. The old affine law read
    // 4 + p[0] * 44 / 100 and covered 0.15x to 1.85x, so the whole top half
    // of the slider bought less than a doubling. Every phase below reads at
    // most bit 20 of its product, and Q6 keeps bits 0..25, so the uint32 wrap
    // never shows and the Speed 50 output is bit identical to the old law's.
    // Rounded, not truncated: the nearest .5 boundary over Speed 0..100 is
    // 19 float ulps away, so the host, the device and the page agree.
    const uint32_t speedQ6 = static_cast<uint32_t>(lroundf(1664.0f * speedMul(p[0])));
    const uint32_t base = tMs * speedQ6; // no old placeholder >> 4 here
    const uint32_t ph1 = base >> 14;
    const uint32_t ph2 = (base * 3u) >> 16;
    const uint32_t ph3 = base >> 17;
    // Sine periods at Speed 50: 10.082462 s, 13.443282 s and 80.659692 s.
    // The older figures of 21.845333, 29.127111 and 174.762667 s were the
    // periods at the Speed default of 12 this entry had before gm-33fm.
    // Unsigned products preserve JS wrap at long uptime; computing from
    // tMs also matches the page when the speed parameter changes.
    const int grainQ8 = 16 + static_cast<int>(p[1]) * 68 / 100; // 16..84
    const int tiltA = 120 + static_cast<int>(p[1]) * 150 / 100; // 120..270
    int rowMax = 0;
    for (int y = 0; y < h; y++) {
        const int g = (grainBase[y] * grainQ8) >> 8;
        const int v = g + ((sl[((static_cast<uint32_t>(y) * 9u >> 4) + ph3) & (SIN_N - 1)] * tiltA) >> 9);
        rowTerm[y] = static_cast<int16_t>(v);
        const int a = v < 0 ? -v : v;
        if (a > rowMax) {
            rowMax = a;
        }
    }
    // Wavelengths 380..900 px and 55% of that, quantized to Q4 sine-table
    // steps as on the page. Default steps 26 and 48 give wavelengths
    // 630.153846 and 341.333333 px, moving right at 62.5 px/s and left at
    // 25.390625 px/s at Speed 50, and scaling with speedMul() from there.
    // Width never rescales these physical-pixel speeds.
    const int px = 380 + static_cast<int>(p[2]) * 520 / 100;
    const uint32_t f1Q4 = 16 * 1024 / px;
    const uint32_t f2Q4 = 16 * 1024 / (px * 55 / 100);
    const int amp1 = 420 + static_cast<int>(p[3]) * 560 / 100; // 420..980
    const int amp2 = amp1 >> 2;                             // 105..245
    for (int x = 0; x < w; x++) {
        const uint32_t i1 = ((static_cast<uint32_t>(x) * f1Q4) >> 4) - ph1;
        const uint32_t i2 = ((static_cast<uint32_t>(x) * f2Q4) >> 4) + ph2;
        colTerm[x] = static_cast<int16_t>(MID + ((sl[i1 & (SIN_N - 1)] * amp1) >> 9) +
                                        ((sl[i2 & (SIN_N - 1)] * amp2) >> 9));
    }
    // |grain| <= 512 and |rowTerm| <= 168 + 270 = 438 at every slider
    // extreme. Clamp the dithered columns to [rowMax, 4095-rowMax], proving
    // the final sum is 0..4095, including ditherAmp's maximum of 16.
    const int lo = rowMax;
    const int hi = INDEX_MAX - rowMax;
    for (int ph = 0; ph < 8; ph++) {
        int16_t *ct = colTermPh + static_cast<size_t>(ph) * colStride;
        const int16_t *off = dithOff + ph * 8;
        for (int x = 0; x < w; x++) {
            int v = colTerm[x] + off[x & 7];
            ct[x] = static_cast<int16_t>(v < lo ? lo : (v > hi ? hi : v));
        }
    }
}

#if GM_BGANIM_BRUSHED_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14.2, xtensa-asm14.sh, before adding this kernel: bandRef's .L4 is
// already a hardware loop with 15 instructions per pair, no spills inside
// the loop and no adjacent scalar load/use. The short scalar loop below
// transcribes its schedule verbatim. The edge for full blocks is PIE:
// load eight columns and add the signed row term in eight s16 lanes. All
// sums are 0..4095, so VADDS never saturates. MOVI.32.A keeps the result
// in registers, and EXTUI extracts bits 4..11 and 20..27 as two indices.
// There is no vector gather for a 16-bit palette; four interleaved scalar
// pairs do the gathers and four S32I stores emit the eight RGB565 pixels.
// No index scratch buffer, extra palette or PSRAM reads enter this loop.
//
// Full block: 44 instructions / 8 pixels = 5.5 instructions per pixel,
// versus GCC's 60 / 8 = 7.5. ADDI between VLD and VADDS hides the vector
// load/use slot; the independent high-half shift hides the low palette
// load/use slot. No adjacent load/use remains. This is a schedule claim,
// not a measured device speedup: flash fetch, PSRAM output and row-call
// setup still need the production A/B. Host timings cannot settle that.
//
// ct is 16-byte aligned by allocHot and the padded colStride, out is only
// required to be 4-byte aligned. Only complete eight-pixel spans use VLD;
// the 0..7-pixel tail never reads the padding. The assembled 128-byte vector
// body and 40-byte scalar body fit LOOPNEZ's 256-byte reach and never nest.
// GCC does not allocate q registers and has no q-register clobber syntax.
// q0/q1 are private to this asm; FreeRTOS saves them lazily. CPENABLE and
// SAR are never written here. The MOVI.32.A selectors were executed first
// in tools/qemubench/tests/anim_brushed/probe_movi.
GM_ANIM_IRAM __attribute__((noinline)) void brushedRowAsm(uint16_t *out, const int16_t *ct,
                                                         const uint16_t *pal, int rt, int n) {
    const int blocks = n >> 3;
    int32_t lo, hi;
    if (blocks != 0) {
        uint32_t rowPair = (uint16_t)rt;
        rowPair |= rowPair << 16;
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
                     : [rt] "r"(rowPair), [pal] "r"(pal), [n] "r"(blocks)
                     : "memory");
    }
    const int pairs = (n & 7) >> 1;
    asm volatile("loopnez %[n], 2f\n"
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
                 "2:\n"
                 : [out] "+&r"(out), [ct] "+&r"(ct), [lo] "=&r"(lo), [hi] "=&r"(hi)
                 : [rt] "r"(rt), [pal] "r"(pal), [n] "r"(pairs)
                 : "memory");
    if (n & 1) {
        *out = pal[(*ct + rt) >> 4];
    }
}
#endif

GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        const int rt = rowTerm[y];
        const int16_t *__restrict ct = colTermPh + static_cast<size_t>(y & 7) * colStride;
        int x = 0;
        // BgAnim.h guarantees 4-byte dst alignment and even widths for
        // multi-row calls. Odd widths arrive one row at a time.
        for (; x + 1 < w; x += 2) {
            const uint16_t c0 = palette[(ct[x] + rt) >> 4];
            const uint16_t c1 = palette[(ct[x + 1] + rt) >> 4];
            *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(c0) | (static_cast<uint32_t>(c1) << 16);
            dst += 2;
        }
        for (; x < w; x++) {
            *dst++ = palette[(ct[x] + rt) >> 4];
        }
    }
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_BRUSHED_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    for (int y = y0; y < y0 + rows; y++) {
        const int16_t *ct = colTermPh + static_cast<size_t>(y & 7) * colStride;
        brushedRowAsm(dst, ct, palette, rowTerm[y], w);
        dst += w;
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(colTermPh, static_cast<size_t>(8 * colStride) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(grainBase, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(dithOff, 64 * sizeof(int16_t));
    releaseTable(ramp, 256 * sizeof(uint16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    sl = nullptr; // borrowed, never release the shared sine table
    allocW = allocH = colStride = 0;
    lastThemeGen = 0xFFFFFFFF;
    paletteValid = false;
}

} // namespace

extern const BgAnimation bg_anim_brushed;
const BgAnimation bg_anim_brushed = {
    "brushed",
    "Brushed",
    {{"speed", "Speed", 50}, {"grain", "Grain", 35}, {"reflection", "Reflection", 45}, {"contrast", "Contrast", 30}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
