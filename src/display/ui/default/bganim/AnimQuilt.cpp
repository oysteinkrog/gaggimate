#ifndef GAGGIMATE_SIM

// "Quilt": soft square pillows with highlights and shadows walking around
// them as the light turns. The grid drifts upward. This is the approved
// id 'quilt' entry, including its deviations, in anim_bench.html: a 36 s
// light revolution, 8 px/s drift, and ambient 1560 in Q4 palette units.
//
// A cosine height and its negative-sine slope on each axis make a separable
// pillow. Keeping the height terms at 35% of the slope amplitude preserves
// the pillows when the light crosses an axis. frame() multiplies the slopes
// by the Q9 light direction, adds the heights, and folds Q4 Bayer dither
// into eight column phases. bandRef() is the page's sepBand verbatim in
// integer arithmetic: pal[((colTermPh[(y&7)*w+x] + rowTerm[y]) >> 4) & 255].
// No row depends on a preceding call or on which rows share its band.

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

#ifndef GM_BGANIM_QUILT_ASM
#define GM_BGANIM_QUILT_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int AMBIENT = 1560; // Q4 palette units, the page's readability adjustment
constexpr int PHASES = 8;     // absolute y's Bayer8 phase, never the band-local row
constexpr int PALETTE_N = 256;

int16_t *dHx = nullptr;
int16_t *dHy = nullptr;
int16_t *hX = nullptr;
int16_t *hY = nullptr;
int16_t *colTermPh = nullptr;
int16_t *rowTerm = nullptr;
int16_t *dith = nullptr;
uint16_t *pal = nullptr;
const int16_t *sl = nullptr; // borrowed boot-lifetime shared sine table
int allocW = 0, allocH = 0;
int lastPitch = -1, lastRelief = -1;
uint32_t lastThemeGen = 0;
bool paletteValid = false;

// At 480x480, including allocHot's 16-byte rounding:
//   colTermPh  8*480*2 = 7680 B  slab, read every pixel
//   rowTerm      480*2 =  960 B  slab, read every row
//   pal          256*2 =  512 B  slab, gathered every pixel
//                         9152 B of the animation's 9216 B share
//   dHx, dHy, hX, hY     960 B each, PSRAM, frame() reads only
//   dith                  128 B, PSRAM, frame() reads only
// The shared sine table is already charged to the shared 3072 B reserve.
// Nothing allocates per frame, and no table hides in permanent BSS.
void release();

bool init(int w, int h) {
    if (w != allocW || h != allocH) {
        release();
    }
    allocW = w;
    allocH = h;
    sl = sinLut();
    if (sl == nullptr) {
        release();
        return false;
    }
    if (colTermPh == nullptr) colTermPh = static_cast<int16_t *>(allocHot(PHASES * w * sizeof(int16_t)));
    if (rowTerm == nullptr) rowTerm = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
    if (pal == nullptr) pal = static_cast<uint16_t *>(allocHot(PALETTE_N * sizeof(uint16_t)));
    if (dHx == nullptr) dHx = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
    if (dHy == nullptr) dHy = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
    if (hX == nullptr) hX = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
    if (hY == nullptr) hY = static_cast<int16_t *>(alloc(h * sizeof(int16_t)));
    if (dith == nullptr) dith = static_cast<int16_t *>(alloc(64 * sizeof(int16_t)));
    if (!colTermPh || !rowTerm || !pal || !dHx || !dHy || !hX || !hY || !dith) {
        // Includes partial slab allocations: a retry must start with no live
        // tables and the slab must reset when the final table is released.
        release();
        return false;
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    const uint32_t gen = themeGen();
    if (!paletteValid || gen != lastThemeGen) {
        buildThemeRamp(pal, 256);
        const float amp = ditherAmp(pal, 256);
        for (int k = 0; k < 64; k++) {
            // Page bayerOffsets(..., amp, 16): whole Q4 units, rounded
            // half away from zero. ditherAmp caps at 16, so |dith| <= 256.
            dith[k] = static_cast<int16_t>(lroundf((BAYER8[k] - 31.5f) * (amp * 16.0f / 31.5f)));
        }
        lastThemeGen = gen;
        paletteValid = true;
    }
    if (lastPitch != p[1] || lastRelief != p[2]) {
        // All six pitches divide 480. Keep the page's pixel units even
        // at other sizes: rescaling here would change its design. At a
        // height not divisible by the chosen pitch, its modulo-h drift
        // has the same wrap seam as rendering the page at that height.
        const int pitches[] = {48, 60, 80, 96, 120, 160};
        const int bucket = static_cast<int>(p[1]) * 6 / 100;
        const int pitch = pitches[bucket < 6 ? bucket : 5];
        const int amp = 200 + static_cast<int>(lroundf(p[2] * 3.4f));
        const int hAmp = static_cast<int>(lroundf(amp * 0.35f));
        const float angleStep = 2.0f * static_cast<float>(M_PI) / pitch;
        for (int x = 0; x < w; x++) {
            dHx[x] = static_cast<int16_t>(lroundf(-sinf(angleStep * x) * amp));
            hX[x] = static_cast<int16_t>(lroundf(cosf(angleStep * x) * hAmp));
        }
        for (int y = 0; y < h; y++) {
            dHy[y] = static_cast<int16_t>(lroundf(-sinf(angleStep * y) * amp));
            hY[y] = static_cast<int16_t>(lroundf(cosf(angleStep * y) * hAmp));
        }
        lastPitch = p[1];
        lastRelief = p[2];
    }

    // Absolute time, as on the page, so parameter changes immediately
    // select that speed's phase. Use uint64_t before masking: at maximum
    // uptime and speed, the Q8 sine cursor exceeds uint32_t. Float frame
    // math can round a cursor/drift boundary differently from JS doubles;
    // it never changes the periods, Q8 interpolation, or integer pixel math.
    const float t = static_cast<float>(tMs) * speedMul(p[0]);
    const uint64_t lQ8 = static_cast<uint64_t>(t * (1024.0f * 256.0f / 36000.0f)) + (128u << 8);
    const int li = static_cast<int>((lQ8 >> 8) & 1023);
    const int lf = static_cast<int>(lQ8 & 255);
    const int lx0 = sl[(li + 256) & 1023];
    const int ly0 = sl[li];
    // The shared sine's amplitude 512 is Q9. Interpolate its entries in
    // Q8 with arithmetic right shifts, matching JS >> on negative deltas.
    const int lx = lx0 + (((sl[(li + 257) & 1023] - lx0) * lf) >> 8);
    const int ly = ly0 + (((sl[(li + 1) & 1023] - ly0) * lf) >> 8);
    const int drift = static_cast<int>(static_cast<uint64_t>(t * 8.0f / 1000.0f) % static_cast<unsigned>(h));
    for (int ph = 0; ph < PHASES; ph++) {
        int16_t *ct = colTermPh + static_cast<size_t>(ph) * w;
        for (int x = 0; x < w; x++) {
            ct[x] = static_cast<int16_t>(((lx * dHx[x]) >> 9) + hX[x] + dith[ph * 8 + (x & 7)]);
        }
    }
    for (int y = 0; y < h; y++) {
        int yy = y + drift;
        if (yy >= h) yy -= h;
        rowTerm[y] = static_cast<int16_t>(((ly * dHy[yy]) >> 9) + hY[yy] + AMBIENT);
    }
}

// Portable spec. Unsigned extraction of bits 4..11 equals the page's
// (signed sum >> 4) & 255, including negative sums, without depending on
// C++17's implementation-defined negative right shift in the pixel loop.
GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        const int16_t *__restrict ct = colTermPh + static_cast<size_t>(y & 7) * w;
        const int rt = rowTerm[y];
        uint16_t *__restrict out = dst + static_cast<size_t>(r) * w;
        for (int x = 0; x < w; x++) {
            out[x] = pal[(static_cast<uint32_t>(ct[x] + rt) >> 4) & 255];
        }
    }
}

#if GM_BGANIM_QUILT_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14's original bandRef .L6 loop, transcribed before the vector pass:
// L16SI, ADDI ct, ADD rt, EXTUI, ADDX2, L16UI, S16I, ADDI dst. Eight
// instructions/pixel, one exposed palette-load/store interlock. Kept for
// the alignment prefix, tail, and rows whose input/output halfword parity
// differs. This body is the compiler baseline, not a predicted speedup.
GM_ANIM_IRAM __attribute__((noinline)) void quiltScalarAsm(uint16_t *dst, const int16_t *ct, int rt,
                                                         const uint16_t *palette, int n) {
    int v;
    asm volatile("loopnez %[n], 1f\n"
                 "l16si   %[v], %[ct], 0\n"
                 "addi    %[ct], %[ct], 2\n"
                 "add     %[v], %[v], %[rt]\n"
                 "extui   %[v], %[v], 4, 8\n"
                 "addx2   %[v], %[v], %[pal]\n"
                 "l16ui   %[v], %[v], 0\n"
                 "s16i    %[v], %[dst], 0\n"
                 "addi    %[dst], %[dst], 2\n"
                 "1:\n"
                 : [ct] "+&r"(ct), [dst] "+&r"(dst), [v] "=&r"(v)
                 : [rt] "r"(rt), [pal] "r"(palette), [n] "r"(n)
                 : "memory");
}

// The compiler cannot use PIE or move q-register words directly into its
// scalar gather schedule. Eight sums use one VADDS.S16; MOVI.32.A extracts
// four pairs straight to ARs, with no index scratch table or PSRAM traffic.
// EXTUI selects bits 4..11 and 20..27 of each packed pair, preserving the
// page's signed-shift-and-mask even for a negative sum. Palette reads are
// genuine gathers and remain scalar. Load the high pixel first, low second,
// then shift high and OR low: neither load has an immediate consumer.
//
// Bounds at relief 0..100: |dH| <= 540, |h| <= 189, |light| <= 512,
// |dith| <= 256. Thus ct is in [-985,985], rt in [831,2289], and their
// sum in [-154,3274]. VADDS.S16 cannot saturate anywhere in that superset.
//
// Main body: 43 instructions per eight pixels (5.375/pixel), 129 bytes
// before density relaxation, with no exposed load-use interlocks under the
// documented pipeline model. About 5.375 cycles/pixel is only an ideal
// issue estimate; vector memory throughput, stores and task preemption
// still need device timing. Compared with GCC's eight-instruction loop,
// this also halves output stores. Host timings do not measure this edge.
//
// MOVI.32.A selectors 0..3 were separately probed in QEMU before use.
// q0 and q1 are free: GCC never allocates q registers and has no q clobber
// syntax. The block changes neither SAR nor CPENABLE. FreeRTOS owns the
// lazy CP3 enable/context save; production must never enable it directly.
//
// Input needs only halfword alignment at entry. A scalar prefix aligns ct
// to 16 bytes before any VLD. Output needs four-byte alignment for S32I:
// if aligning ct would leave dst at 2 mod 4, use the scalar loop for this
// row. This covers odd-width Bayer phases without touching a neighbour.
// Full vectors never read beyond n. There is no speculative next load.
GM_ANIM_IRAM __attribute__((noinline)) void quiltRowAsm(uint16_t *dst, const int16_t *ct, int rt,
                                                      const uint16_t *palette, int n) {
    if (n <= 0) return;
    if ((((uintptr_t)ct ^ (uintptr_t)dst) & 2u) != 0) {
        quiltScalarAsm(dst, ct, rt, palette, n);
        return;
    }
    int prefix = (int)((16u - ((uintptr_t)ct & 15u)) & 15u) / 2;
    if (prefix > n) prefix = n;
    if (prefix != 0) {
        quiltScalarAsm(dst, ct, rt, palette, prefix);
        dst += prefix;
        ct += prefix;
        n -= prefix;
    }
    const int blocks = n >> 3;
    if (blocks != 0) {
        uint32_t packed = (uint16_t)rt;
        packed |= packed << 16; // the same Q4 row term in both halfwords
        uint32_t hi, lo;
        // Predecrement only inside asm, never form a C pointer before the
        // array. The per-block ADDI restores the current output address
        // while covering the vector-load latency. The final ADDI leaves
        // dst pointing just past the vectors for the scalar tail.
        asm volatile("ee.movi.32.q q1, %[rt], 0\n"
                     "ee.movi.32.q q1, %[rt], 1\n"
                     "ee.movi.32.q q1, %[rt], 2\n"
                     "ee.movi.32.q q1, %[rt], 3\n"
                     "addi    %[dst], %[dst], -16\n"
                     "loopnez %[n], 1f\n"
                     "ee.vld.128.ip q0, %[ct], 16\n"
                     "addi    %[dst], %[dst], 16\n"
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
                     "s32i    %[hi], %[dst], 0\n"
                     "ee.movi.32.a q0, %[hi], 1\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[dst], 4\n"
                     "ee.movi.32.a q0, %[hi], 2\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[dst], 8\n"
                     "ee.movi.32.a q0, %[hi], 3\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[dst], 12\n"
                     "1:\n"
                     "addi    %[dst], %[dst], 16\n"
                     : [ct] "+&r"(ct), [dst] "+&r"(dst), [hi] "=&r"(hi), [lo] "=&r"(lo)
                     : [rt] "r"(packed), [pal] "r"(palette), [n] "r"(blocks)
                     : "memory");
    }
    const int tail = n & 7;
    if (tail != 0) quiltScalarAsm(dst, ct, rt, palette, tail);
}
#endif

// IRAM keeps the device's hot loop away from flash instruction-cache
// refills while LVGL runs on the other core. The switch defaults to 1;
// -DGM_BGANIM_QUILT_ASM=0 selects bandRef for the device A/B measurement.
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_QUILT_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        quiltRowAsm(dst + static_cast<size_t>(r) * w, colTermPh + static_cast<size_t>(y & 7) * w,
                    rowTerm[y], pal, w);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(colTermPh, static_cast<size_t>(PHASES * allocW) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(pal, PALETTE_N * sizeof(uint16_t));
    releaseTable(dHx, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(dHy, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(hX, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(hY, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    sl = nullptr;
    allocW = allocH = 0;
    lastPitch = lastRelief = -1;
    lastThemeGen = 0;
    paletteValid = false;
}

} // namespace

extern const BgAnimation bg_anim_quilt;
const BgAnimation bg_anim_quilt = {
    "quilt",
    "Quilt",
    {{"speed", "Speed", 50},
     {"pitch", "Pillow size", 75},
     {"relief", "Relief", 55},
     {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
