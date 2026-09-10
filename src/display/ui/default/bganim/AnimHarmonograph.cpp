#ifndef GAGGIMATE_SIM

// "Harmonograph": one luminous Lissajous thread over a drifting, vignetted
// wash, entry 35 of tools/animbench/web/anim_bench.html. Every frame walks
// 2,048 samples. Four Q6 sine phases and the rotation interpolate the shared
// 1,024-entry, +/-512 sine table. Q2 positions select one of sixteen quarter
// pixel 11x11 stamps: radius 1.8 px flat core, cosine skirt to radius 5.4 px,
// raised to 1.5. Max coverage preserves every crossing. A smooth, nonzero
// comet envelope completes one lap per 8 seconds at speed 50.
//
// The page's wash is deliberately asymmetric here: rowTerm is multiplied
// by 32, colTerm is not. Bayer offsets are multiplied by 32 before the sum
// is shifted by 10. Making the axes symmetric changes the approved look.
// The page fixes its geometry at 480px, centred on (240,240), even for
// smaller dimensions. Keep those literal coordinates, including clipping.

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

#ifndef GM_BGANIM_HARMONOGRAPH_ASM
#define GM_BGANIM_HARMONOGRAPH_ASM 1
#endif

namespace {
using namespace bganim;
constexpr int NS = 2048;
constexpr int SW = 11, SH = 11;
constexpr int STAMP_BYTES = 16 * SW * SH;
constexpr float PI = 3.14159265358979323846f;

// At 480x480: ctPh 7,680 + rowTerm 960 + palette 512 + scratch 32 +
// ones 16 = 9,200 B of the 9,216 B slab. ctPh rows are padded to eight int16s,
// including at 466/233 wide, so vector loads start on 16-byte boundaries.
// The coverage image is a bulk sequential sweep in PSRAM, with 16-byte
// padded rows and an aligned alias. bufStorage owns that allocation.
// Frame-only PSRAM tables: colTerm 960, tail 2,048, dith 128, stamp 1,936,
// bufStorage 230,415 B at 480x480 (including 15 B alignment reserve).
int16_t *ctPh = nullptr, *rowTerm = nullptr, *colTerm = nullptr;
uint16_t *palette = nullptr, *scratch = nullptr, *ones = nullptr;
uint8_t *bufStorage = nullptr, *buf = nullptr, *tail = nullptr, *stamp = nullptr;
int16_t *dith = nullptr;
const int16_t *sl = nullptr; // borrowed shared slab table, never released here
int allocW = 0, allocH = 0, ctStride = 0, bufStride = 0;
uint32_t lastThemeGen = 0xFFFFFFFF;
int lastGlow = -1, lastBright = -1, top = 0;

void release();

bool init(int w, int h) {
    // Reject dimensions above the firmware's largest surface before a hot
    // allocation could silently spill out of the reserved slab.
    if (w < 1 || h < 1 || w > 480 || h > 480) {
        release();
        return false;
    }
    if (palette != nullptr && allocW == w && allocH == h) return true;
    release();
    allocW = w;
    allocH = h;
    ctStride = (w + 7) & ~7;
    bufStride = (w + 15) & ~15;
    sl = sinLut();
    if (sl == nullptr) {
        release();
        return false;
    }
    ctPh = static_cast<int16_t *>(allocHot(8 * ctStride * sizeof(int16_t)));
    rowTerm = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    scratch = static_cast<uint16_t *>(allocHot(16 * sizeof(uint16_t)));
    ones = static_cast<uint16_t *>(allocHot(8 * sizeof(uint16_t)));
    colTerm = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
    tail = static_cast<uint8_t *>(alloc(NS));
    dith = static_cast<int16_t *>(alloc(64 * sizeof(int16_t)));
    stamp = static_cast<uint8_t *>(alloc(STAMP_BYTES));
    bufStorage = static_cast<uint8_t *>(alloc(static_cast<size_t>(bufStride) * h + 15));
    if (!ctPh || !rowTerm || !palette || !scratch || !ones || !colTerm || !tail || !dith || !stamp || !bufStorage) {
        // Every successful allocation is released, including those after
        // an earlier failure. A retry sees exactly the initial state.
        release();
        return false;
    }
    buf = reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(bufStorage) + 15) & ~uintptr_t(15));
    for (int k = 0; k < 8; ++k) ones[k] = 1;
    for (int fy = 0; fy < 4; ++fy) {
        for (int fx = 0; fx < 4; ++fx) {
            const int o = (fy * 4 + fx) * SW * SH;
            for (int j = 0; j < SH; ++j) {
                for (int i = 0; i < SW; ++i) {
                    const float dx = i - 5 - fx * 0.25f, dy = j - 5 - fy * 0.25f;
                    const float r = sqrtf(dx * dx + dy * dy);
                    const float prof = r <= 1.8f ? 1.0f : r >= 5.4f ? 0.0f :
                        powf(0.5f * (1.0f + cosf(PI * (r - 1.8f) / (5.4f - 1.8f))), 1.5f);
                    stamp[o + j * SW + i] = static_cast<uint8_t>(lroundf(255.0f * prof));
                }
            }
        }
    }
    // pcDither(..., 2.6), whole palette indices. No half-integer ties occur,
    // so lroundf agrees with JavaScript Math.round for negative values too.
    for (int k = 0; k < 64; ++k)
        dith[k] = static_cast<int16_t>(lroundf((BAYER8[k] - 31.5f) / 31.5f * 2.6f));
    return true;
}

// Positive float | 0 in JS truncates then wraps modulo 2^32. A uint64_t
// bridge avoids undefined conversions at the largest tMs and speed 100.
// All phase lookups mask away the high bits, including the sign bit.
// frame uses float on both firmware and host, while JS uses double. At
// long uptimes float loses fine phase bits; it still uses the page's direct
// tMs clock, with no extra accumulator drift or different cycle period.
uint32_t phase(float tt, float rate) {
    return static_cast<uint32_t>(static_cast<uint64_t>(tt * rate));
}

BGANIM_INLINE int sineQ6(uint32_t q) {
    const int k = (q >> 6) & 1023;
    return sl[k] + (((sl[(k + 1) & 1023] - sl[k]) * static_cast<int>(q & 63)) >> 6);
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (lastBright != p[3] || lastThemeGen != gen) {
        // The page applies 176..256 Q8 brightness after themeRGB, using
        // round(p[3]*0.8). Keep this explicit animation parameter mapping.
        buildThemeRamp(palette, 176 + (static_cast<int>(p[3]) * 8 + 5) / 10);
        lastBright = p[3];
        lastThemeGen = gen;
    }
    if (lastGlow != p[2]) {
        const float floorLvl = 78.0f + p[2] * 0.35f;
        for (int k = 0; k < NS; ++k) {
            const float u = 0.5f * (1.0f + cosf(2.0f * PI * k / NS));
            tail[k] = static_cast<uint8_t>(lroundf(floorLvl + (255.0f - floorLvl) * powf(u, 2.6f)));
        }
        top = 196 + (static_cast<int>(p[2]) * 55 + 50) / 100;
        lastGlow = p[2];
    }
    const float tt = static_cast<float>(tMs) * speedMul(p[0]);
    const uint32_t g1 = phase(tt, 0.0170f), g2 = phase(tt, 0.0119f);
    const uint32_t g3 = phase(tt, 0.0098f), g4 = phase(tt, 0.0145f);
    for (int x = 0; x < w; ++x) {
        const float q = (x - 240) / 240.0f;
        colTerm[x] = ((sl[(x * 3 + g1) & 1023] * 135) >> 9) +
                     ((sl[(x * 7 - g2) & 1023] * 75) >> 9) - static_cast<int>(1560.0f * q * q);
    }
    for (int y = 0; y < h; ++y) {
        const float q = (y - 240) / 240.0f;
        rowTerm[y] = ((sl[(y * 4 + g3) & 1023] * 135) >> 9) +
                     ((sl[(y * 5 - g4) & 1023] * 75) >> 9) - static_cast<int>(1560.0f * q * q);
    }
    for (int ph = 0; ph < 8; ++ph) {
        for (int x = 0; x < w; ++x)
            ctPh[ph * ctStride + x] = colTerm[x] + dith[ph * 8 + (x & 7)] * 32;
    }
    memset(buf, 0, static_cast<size_t>(bufStride) * h);
    const float at = 60.0f + p[1] * 0.95f;
    const int a1 = static_cast<int>(at * 0.69f), a2 = static_cast<int>(at * 0.31f);
    const uint32_t q1 = phase(tt, 0.704f), q2 = phase(tt, 0.4544f);
    const uint32_t q3 = phase(tt, 0.3136f), q4 = phase(tt, 0.5952f);
    const uint32_t rot = phase(tt, 0.18176f); // Q6, 360 degrees per ~360.56 s
    const int ca = sineQ6(rot + 256 * 64), sa = sineQ6(rot);
    const int head = phase(tt, NS / 8000.0f) & (NS - 1);
    for (int s = 0; s < NS; ++s) {
        // Q6 strides 192/320/128/256 are exactly 3/5/2/4 table entries.
        // The weighted coordinates use Q9 sine, rotation shifts by 7 to Q2.
        const int ux = (a1 * sineQ6(s * 192u + q1) + a2 * sineQ6(s * 320u + q2)) >> 9;
        const int uy = (a1 * sineQ6(s * 128u + q3) + a2 * sineQ6(s * 256u + q4)) >> 9;
        const int pxq = 240 * 4 + ((ux * ca - uy * sa) >> 7);
        const int pyq = 240 * 4 + ((ux * sa + uy * ca) >> 7);
        const int x0 = (pxq >> 2) - 5, y0 = (pyq >> 2) - 5;
        if (x0 < 0 || y0 < 0 || x0 + SW > w || y0 + SH > h) continue;
        const int lvl = tail[(s - head) & (NS - 1)];
        const uint8_t *sb = stamp + (((pyq & 3) * 4) + (pxq & 3)) * SW * SH;
        // An 11-byte stamp row cannot contain an aligned 16-byte PIE span.
        // Keep these short, arbitrarily aligned max stores scalar; widening
        // their footprints would touch unrelated coverage beside the stamp.
        for (int j = 0; j < SH; ++j) {
            uint8_t *out = buf + (y0 + j) * bufStride + x0;
            for (int i = 0; i < SW; ++i) {
                const int g = (sb[j * SW + i] * lvl) >> 8;
                if (g > out[i]) out[i] = static_cast<uint8_t>(g);
            }
        }
    }
}

GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; ++r) {
        const int y = y0 + r;
        const int rt = rowTerm[y] * 32; // shifting a negative signed value would be UB
        const int16_t *__restrict ct = ctPh + (y & 7) * ctStride;
        const uint8_t *__restrict coverage = buf + y * bufStride;
        uint16_t *__restrict out = dst + static_cast<size_t>(r) * w;
        for (int x = 0; x < w; ++x) {
            int i = 90 + ((ct[x] + rt) >> 10);
            const int g = coverage[x];
            if (g != 0) i += ((top - i) * g) >> 8;
            out[x] = palette[i < 0 ? 0 : i > 255 ? 255 : i];
        }
    }
}

#if GM_BGANIM_HARMONOGRAPH_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14's original .L8 hardware loop is 20 instructions/pixel when g!=0,
// 18 when g==0, including a branch and a multiply even for zero coverage.
// Its schedule is l16si/l8ui/add/srai/addi/sub/mull/mov/beqz/[srai/add]/
// min/extui/addx2/movltz/l16ui/addi/s16i/addi/addi. The loads already have
// independent instructions before use. The edge here is lane arithmetic,
// removal of the redundant branch/clamp, and paired stores, not unrolling
// that already sound scalar schedule into a larger register window.
//
// Split rt=32*rowTerm into 1024*(rowTerm>>5) + 32*(rowTerm&31).
// Thus i=(90+(rowTerm>>5)) + ((ct+32*(rowTerm&31))>>10), exactly,
// including negative rows. rowTerm is -1770..210; ct is -1866..306.
// The lane sum is -1866..1298, i is 32..96, top is 196..251, and
// coverage is 0..254 (QEMU additionally tests 255). The composite lies
// in 32..250, so signed lane adds/subtracts never saturate and the palette
// clamp cannot fire. Unsigned coverage widened against zero stays positive.
//
// Row constants are broadcast from scalars, without a stack table. one is
// eight uint16 ones in the hot slab: SAR=10 turns its multiply into a shift.
// ct and coverage are aligned by padded allocation, indices is 32 bytes
// in the hot slab, and output uses only scalar word stores. No vector load
// or store is ever given the caller's merely 4-byte-aligned destination.
// All named register results are early clobbers. GCC does not allocate q
// registers and provides no q clobber syntax; this block owns q0..q7.
// SAR and PIE context belong to FreeRTOS. Never write CPENABLE here.
//
// Two eight-lane groups share one 16-byte coverage load. The 11-instruction
// scalar pair loop hides every load-use gap and writes one s32i per pair.
// The surrounding 16-pixel loop uses a branch so hardware loops never nest.
// The slab scratch keeps the vector-to-gather handoff off the PSRAM stack.
// Its body executes 114 instructions per 16 pixels (7.125/pixel), including
// the eight scalar pairs and loop control. At one issue per cycle plus the
// outer taken branch's two cycles, the hot-cache estimate is 7.25 cycles
// per pixel, excluding row setup, stores waiting on the bus, and preemption.
// Device timing still decides whether this beats GCC under panel traffic;
// host timing cannot measure PIE, load interlocks, or PSRAM contention.
GM_ANIM_IRAM __attribute__((noinline)) void harmonographRowAsm(
    uint16_t *out, const int16_t *ct, const uint8_t *coverage, const uint16_t *pal,
    uint16_t *indices, const uint16_t *one, int rowValue, int topValue, int n16) {
    // Multiplication by 65537 replicates a nonnegative halfword into both
    // halves of a scalar word. Four moves then broadcast all eight lanes.
    const uint32_t rem = (uint32_t)((rowValue & 31) * 32) * 65537u;
    const uint32_t base = (uint32_t)(90 + (rowValue >> 5)) * 65537u;
    const uint32_t crest = (uint32_t)topValue * 65537u;
    asm volatile("ee.movi.32.q q4, %[rem], 0\n"
                 "ee.movi.32.q q4, %[rem], 1\n"
                 "ee.movi.32.q q4, %[rem], 2\n"
                 "ee.movi.32.q q4, %[rem], 3\n"
                 "ee.movi.32.q q5, %[base], 0\n"
                 "ee.movi.32.q q5, %[base], 1\n"
                 "ee.movi.32.q q5, %[base], 2\n"
                 "ee.movi.32.q q5, %[base], 3\n"
                 "ee.movi.32.q q7, %[crest], 0\n"
                 "ee.movi.32.q q7, %[crest], 1\n"
                 "ee.movi.32.q q7, %[crest], 2\n"
                 "ee.movi.32.q q7, %[crest], 3\n"
                 : : [rem] "r"(rem), [base] "r"(base), [crest] "r"(crest) : "memory");
    // Both blocks are in one noinline leaf. GCC cannot insert PIE work or
    // calls between them; its scalar register setup leaves q4/q5/q7 intact.
    uint32_t t0, t1;
    uint16_t *walk;
    const int pairs = 8;
    asm volatile("beqz %[n], 3f\n"
                 "1:\n"
                 "ee.vld.128.ip q0, %[g], 16\n"
                 "ee.vld.128.ip q2, %[ct], 16\n"
                 "ee.vld.128.ip q3, %[ct], 16\n"
                 "ee.zero.q q1\n"
                 "ee.vld.128.ip q6, %[one], 0\n"
                 "ee.vzip.8 q0, q1\n"
                 "ee.vadds.s16 q2, q2, q4\n"
                 "ee.vadds.s16 q3, q3, q4\n"
                 "ssai 10\n"
                 "ee.vmul.s16 q2, q2, q6\n"
                 "ee.vmul.s16 q3, q3, q6\n"
                 "ee.vadds.s16 q2, q2, q5\n"
                 "ee.vadds.s16 q3, q3, q5\n"
                 "ssai 8\n"
                 "ee.vsubs.s16 q6, q7, q2\n"
                 "ee.vmul.s16 q0, q6, q0\n"
                 "ee.vsubs.s16 q6, q7, q3\n"
                 "ee.vmul.s16 q1, q6, q1\n"
                 "ee.vadds.s16 q2, q2, q0\n"
                 "ee.vadds.s16 q3, q3, q1\n"
                 "mov %[walk], %[indices]\n"
                 "ee.vst.128.ip q2, %[walk], 16\n"
                 "ee.vst.128.ip q3, %[walk], -16\n"
                 "loop %[pairs], 2f\n"
                 "l16ui %[t0], %[walk], 0\n"
                 "l16ui %[t1], %[walk], 2\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "addi %[walk], %[walk], 4\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addi %[out], %[out], 4\n"
                 "2:\n"
                 "addi %[n], %[n], -1\n"
                 "bnez %[n], 1b\n"
                 "3:\n"
                 : [out] "+&r"(out), [ct] "+&r"(ct), [g] "+&r"(coverage),
                   [n] "+&r"(n16), [walk] "=&r"(walk), [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [pal] "r"(pal), [indices] "r"(indices), [one] "r"(one), [pairs] "r"(pairs)
                 : "memory");
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; ++r) {
        const int y = y0 + r;
        const int rt = rowTerm[y];
        const int16_t *ct = ctPh + (y & 7) * ctStride;
        const uint8_t *coverage = buf + y * bufStride;
        uint16_t *out = dst + static_cast<size_t>(r) * w;
        harmonographRowAsm(out, ct, coverage, palette, scratch, ones, rt, top, w >> 4);
        // Scalar tail for 466/233 and other widths. Never read padded
        // columns or depend on another row being in this band call.
        for (int x = w & ~15; x < w; ++x) {
            int i = 90 + ((ct[x] + rt * 32) >> 10);
            i += ((top - i) * coverage[x]) >> 8;
            out[x] = palette[i];
        }
    }
}
#else
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif

void release() {
    releaseTable(ctPh, 8 * static_cast<size_t>(ctStride) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(scratch, 16 * sizeof(uint16_t));
    releaseTable(ones, 8 * sizeof(uint16_t));
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(tail, NS);
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(stamp, STAMP_BYTES);
    releaseTable(bufStorage, static_cast<size_t>(bufStride) * allocH + 15);
    buf = nullptr;
    sl = nullptr;
    allocW = allocH = ctStride = bufStride = 0;
    lastThemeGen = 0xFFFFFFFF;
    lastGlow = lastBright = -1;
    top = 0;
}

} // namespace

extern const BgAnimation bg_anim_harmonograph;
const BgAnimation bg_anim_harmonograph = {
    "harmonograph",
    "Harmonograph",
    {{"speed", "Speed", 50},
     {"size", "Figure size", 68},
     {"glow", "Thread glow", 60},
     {"bright", "Brightness", 60}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
