#ifndef GAGGIMATE_SIM

// "Refraction": soft, rounded channels seen through moving water. This is
// entry 20 in tools/animbench/web/anim_bench.html, including its three slow
// bends and the broad brightness wave travelling down the panel. Four closed
// harmonics make one 512-entry texture; each absolute row translates it by
// rowOff[y]. Channel width favours the low harmonics, Contrast scales their
// swing, and the palette's cubic shoulder keeps most of the face dark.
// Glow wave, Darkness, Ripple and Flow (gm-3vj.23) act in the same table
// builds: rowAdd's amplitude, the palette map's linear/cubic balance, the
// fifth harmonic's weight and the scroll rate. No pixel loop reads them.
//
// The page deliberately keeps the vertical wave and Bayer dither in Q4:
//   v = (texQ4[(x + rowOff[y]) & 511] + rowAdd[y] + dith[y&7][x&7]) >> 4;
//   pixel = palette[clamp(v, 0, 255)];
// Rounding rowAdd to whole indices before the sum would draw horizontal
// contours across the channels. Dither belongs to screen x/y, never to the
// translated texture position. There is no row duplication or band cache.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

// The portable reference remains available for the device parity and timing
// checks. GM_ANIM_IRAM comes from BgAnimCommon.h and normally leaves kernels
// in flash, preserving internal DRAM for the web UI.
#ifndef GM_BGANIM_REFRACTION_ASM
#define GM_BGANIM_REFRACTION_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int TEX = 512;
constexpr int TEX_MASK = TEX - 1;
// One periodic texture plus a duplicate and eight guard entries. The extra
// storage carries identical values, not extra phases. It lets an unaligned
// PIE stream walk up to 512 pixels from any row offset without a wrap branch,
// including the final speculative eight-lane load. bandRef uses only TEX.
constexpr int TEX_STORAGE = 2 * TEX + 8;
constexpr int Q4_CAP = 255 * 16;

uint16_t *texQ4 = nullptr;
uint16_t *rowOff = nullptr;
int16_t *rowAdd = nullptr;
int16_t *dith = nullptr;
uint16_t *palette = nullptr;
int16_t *capQ4 = nullptr;
uint16_t *ramp = nullptr;
const int16_t *sl = nullptr; // borrowed boot-lifetime shared sine table

int allocH = 0;
uint8_t lastP[BG_ANIM_PARAMS];
uint32_t lastThemeGen = 0xFFFFFFFF;
bool tablesValid = false;

// At h=480, every table read per pixel or per row fits in the 9,216 B slab:
//   texQ4     2,064 B: 1,024 B useful + 1,024 B duplicate + 16 B guard
//   rowOff      960 B: one unsigned texture offset per absolute row
//   rowAdd      960 B: one signed Q4 brightness term per absolute row
//   dith        128 B: 8x8 signed Q4 Bayer offsets
//   palette     512 B: 256 RGB565 colours after the cubic remap
//   capQ4        16 B: eight copies of 4080 for the PIE clamp
// Total       4,640 B, including 16-byte slab alignment at this height.
//   ramp        512 B: PSRAM, used only when rebuilding the palette
// The shared sine table uses the slab's separately reserved shared region.
void release();

bool init(int, int h) {
    if (h <= 0) {
        release();
        return false;
    }
    if (allocH != 0 && allocH != h) {
        release();
    }
    sl = sinLut();
    if (sl == nullptr) {
        release();
        return false;
    }
    if (texQ4 != nullptr) {
        return true; // successful init is idempotent, including frame state
    }
    allocH = h; // release needs the size even after a partial allocation
    texQ4 = static_cast<uint16_t *>(allocHot(TEX_STORAGE * sizeof(uint16_t)));
    rowOff = static_cast<uint16_t *>(allocHot(static_cast<size_t>(h) * sizeof(uint16_t)));
    rowAdd = static_cast<int16_t *>(allocHot(static_cast<size_t>(h) * sizeof(int16_t)));
    dith = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    capQ4 = static_cast<int16_t *>(allocHot(8 * sizeof(int16_t)));
    ramp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    if (!texQ4 || !rowOff || !rowAdd || !dith || !palette || !capQ4 || !ramp) {
        release(); // no partial hot set or heap allocation survives a retry
        return false;
    }
    for (int i = 0; i < 8; i++) {
        capQ4[i] = Q4_CAP;
    }
    return true;
}

void rebuild(const uint8_t *p) {
    buildThemeRamp(ramp, 256);
    // Darkness (p[5]) moves weight between the terms of the palette map,
    // keeping their sum at 226: linear, square and cube are 26/54/146 at 50
    // (the old map), 146/54/26 at 0 (an open, bright face) and 0/0/226 at
    // 100 (a dark face, light only in the channel crests).
    const int dk = p[5];
    const int cLin = dk <= 50 ? 26 + (50 - dk) * 120 / 50 : 26 - (dk - 50) * 26 / 50;
    const int cSq = dk <= 50 ? 54 : 54 - (dk - 50) * 54 / 50;
    const int cCube = 226 - cLin - cSq;
    for (int i = 0; i < 256; i++) {
        const int i2 = (i * i) >> 8;
        const int i3 = (i2 * i) >> 8;
        // Page's exact cubic map: positions 4..226 of the theme ramp at the
        // default, never past 227 at any Darkness.
        const int q = 4 + ((i * cLin) >> 8) + ((i2 * cSq) >> 8) + ((i3 * cCube) >> 8);
        palette[i] = ramp[q];
    }
    const int wLow = 90 + static_cast<int>(p[2]) * 190 / 100;   // 90..280
    const int wHigh = 150 - static_cast<int>(p[2]) * 110 / 100; // 150..40
    // Ripple (p[6]) weights the fifth harmonic, the fine ripple along each
    // channel: none at 0, wHigh / 3 at 50 (wHigh * 50 / 150 is the same
    // floor for wHigh >= 0), and wHigh, as strong as the third, at 100.
    const int rp = p[6];
    const int w5 = rp <= 50 ? wHigh * rp / 150 : wHigh / 3 + (rp - 50) * wHigh * 2 / 150;
    const int amp = 280 + static_cast<int>(p[3]) * 380 / 100;   // 280..660
    for (int tx = 0; tx < TEX; tx++) {
        // Shared SIN has 1024 entries and amplitude 512. Steps 2,4,6,10
        // give harmonics 1,2,3,5, all closing exactly at 512. The phase
        // offsets 300,700,150 keep their peaks from lining up.
        const int h1 = sl[(tx * 2) & (SIN_N - 1)];
        const int h2 = sl[(tx * 4 + 300) & (SIN_N - 1)];
        const int h3 = sl[(tx * 6 + 700) & (SIN_N - 1)];
        const int h5 = sl[(tx * 10 + 150) & (SIN_N - 1)];
        // Signed division truncates toward zero, matching Math.trunc.
        // The high fifth harmonic has one third weight at the default
        // Ripple, not one half.
        const int v = (h1 * wLow + h2 * (wLow / 2) + h3 * wHigh + h5 * w5) / (3 * 512);
        const int idx = 128 + (v * amp) / (256 * 2);
        texQ4[tx] = static_cast<uint16_t>((idx < 0 ? 0 : (idx > 255 ? 255 : idx)) * 16);
    }
    for (int tx = TEX; tx < TEX_STORAGE; tx++) {
        texQ4[tx] = texQ4[tx & TEX_MASK];
    }
    // Page's bayerOffsets(..., ditherAmpJS(pal)*0.8, 16). The shared
    // amplitude is capped at 16 indices, so these offsets are within +-205
    // Q4 units. Only this table build uses floats; all frame motion and pixel
    // arithmetic are integer. lroundf matches the page's signed lround.
    const float damp = ditherAmp(palette, 256) * 0.8f;
    for (int k = 0; k < 64; k++) {
        dith[k] = static_cast<int16_t>(lroundf((static_cast<float>(BAYER8[k]) - 31.5f) * (damp * 16.0f / 31.5f)));
    }
}

void frame(uint32_t tMs, int, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    // The tables read p[2], p[3], p[5] and p[6]; comparing every slot only
    // rebuilds a little more often than it has to.
    if (!tablesValid || memcmp(p, lastP, BG_ANIM_PARAMS) != 0 || gen != lastThemeGen) {
        rebuild(p);
        memcpy(lastP, p, BG_ANIM_PARAMS);
        lastThemeGen = gen;
        tablesValid = true;
    }
    // Speed follows the fleet's curve, speedMul(): 0.15x at 0, 1x at 50 and
    // 6.7x at 100 of the rate Speed 50 has always had (bead gm-kh2s). The
    // multiplier is Q6 and 1664 at 50, the old sp of 26 with six fraction
    // bits, and the five shifts below take them back, so Speed 50 is the
    // same picture. The old affine law read 4 + p[0] * 44 / 100 and covered
    // 0.15x to 1.85x; the flat top half of its slider was the law, not a
    // saturating term. Both products wrap BEFORE shifting, exactly like
    // JavaScript's >>> 0, and the phases read bits 14..28 of them at most,
    // so the wrap never shows. Rounded, not truncated: the nearest .5
    // boundary over Speed 0..100 is 19 float ulps away, so the host, the
    // device and the page agree.
    const uint32_t speedQ6 = static_cast<uint32_t>(lroundf(1664.0f * speedMul(p[0])));
    const uint32_t base = tMs * speedQ6;
    // Flow (p[7]) scales the sideways scroll alone, in sixteenths: still at
    // 0, 16/16 at 50 and twice as fast at 100. The product wraps before the
    // shift, and >> 19 of base * 16 reads bits 15..27 of base, the same bits
    // base >> 15 gives below bit 28; rowOff keeps only nine of them.
    const uint32_t flowQ4 = static_cast<uint32_t>(p[7]) * 32 / 100;
    const uint32_t scroll = (base * flowQ4) >> 19;
    const uint32_t bendPh1 = base >> 15;
    const uint32_t bendPh2 = (base * 3u) >> 17;
    const uint32_t bendPh3 = base >> 18;
    const uint32_t glowPh = base >> 14;
    // At default Speed, scroll is 21.484375 px/s. The bend periods are
    // 47.6625, 63.55 and 381.3 seconds; glow's period is 23.8313 seconds.
    // fgQ4=27 gives a 606.815-pixel glow wavelength and 25.463 px/s down.
    const int amp1 = static_cast<int>(p[1]) * 90 / 100;
    const int amp2 = static_cast<int>(p[1]) * 210 / 100;
    const int amp3 = static_cast<int>(p[1]) * 150 / 100;
    const int denom = h > 0 ? h : 1;
    const int f1Q4 = 1536 * 16 / denom; // nominally 1.5 bend cycles down h
    const int f2Q4 = 560 * 16 / denom;  // nominally 0.546875 cycles
    const int f3Q4 = 240 * 16 / denom;  // nominally 0.234375 cycles
    const int fgQ4 = 16 * 1024 / 600;   // fixed pixel wavelength, not scaled by h
    // Glow (p[4]) scales the travelling brightness wave: flat at 0, 704 Q4
    // units (44 palette indices) at 50, 1408 at 100.
    const int glowA = 704 * static_cast<int>(p[4]) / 50;
    for (int y = 0; y < h; y++) {
        const int b1 = sl[(((y * f1Q4) >> 4) + bendPh1) & (SIN_N - 1)] * amp1 / 256;
        const int b2 = sl[(((y * f2Q4) >> 4) + bendPh2) & (SIN_N - 1)] * amp2 / 256;
        const int b3 = sl[(((y * f3Q4) >> 4) - bendPh3) & (SIN_N - 1)] * amp3 / 256;
        // The three displacements sum to at most +-900 pixels. Four texture
        // lengths keep the signed sum positive before the uint32 phase wrap.
        rowOff[y] = static_cast<uint16_t>((static_cast<uint32_t>(b1 + b2 + b3 + 4 * TEX) + scroll) & TEX_MASK);
        // Arithmetic shift, not truncating division: JavaScript >> floors
        // negative products too. GCC on both host and Xtensa does the same.
        // 704 Q4 units is exactly 44 palette indices of brightness swing at
        // the default Glow, and |rowAdd| <= 1408 at any Glow.
        rowAdd[y] = static_cast<int16_t>((sl[(((y * fgQ4) >> 4) - glowPh) & (SIN_N - 1)] * glowA) >> 9);
    }
}

void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint16_t *tex = texQ4;
    const uint16_t *pal = palette;
    for (int y = y0; y < y0 + rows; y++) {
        const int off = rowOff[y];
        const int add = rowAdd[y];
        const int16_t *d = dith + (y & 7) * 8;
        int x = 0;
        for (; x + 1 < w; x += 2) {
            int v0 = (tex[(x + off) & TEX_MASK] + add + d[x & 7]) >> 4;
            int v1 = (tex[(x + 1 + off) & TEX_MASK] + add + d[(x + 1) & 7]) >> 4;
            v0 = v0 < 0 ? 0 : (v0 > 255 ? 255 : v0);
            v1 = v1 < 0 ? 0 : (v1 > 255 ? 255 : v1);
            // BgAnim.h guarantees 4-byte dst alignment and even multi-row w.
            *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(pal[v0]) | (static_cast<uint32_t>(pal[v1]) << 16);
            dst += 2;
        }
        for (; x < w; x++) {
            int v = (tex[(x + off) & TEX_MASK] + add + d[x & 7]) >> 4;
            v = v < 0 ? 0 : (v > 255 ? 255 : v);
            *dst++ = pal[v];
        }
    }
}

#if GM_BGANIM_REFRACTION_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14.2's original bandRef .L4 loop is 37 instructions per pair with no
// immediate load consumers. Its verbatim scalar transcription is retained
// in tests/anim_refraction/main.c. The edge here is a contiguous PIE texture
// stream and eight-lane add/clamp, eliminating the texture address/mask and
// dither read per pixel. The only gather left is the 256-colour palette.
//
// tex points to TEX_STORAGE entries, with its base 16-byte aligned by
// allocHot. d and caps are aligned eight-lane slab vectors. off is 0..511,
// add is -1408..1408 (Glow at 100), and each d lane is -205..205. No signed
// saturation can occur in the sums: tex+d+add is -1613..5693. Clamping that sum to 0..4080
// before extracting bits 4..11 gives exactly bandRef's shift then clamp.
//
// EE.LD.128.USAR.IP captures the source's byte offset while loading the
// aligned block below it; SRC.Q combines it with the following block. The
// duplicate texture makes the wrap seamless; its eight guard entries cover
// the last prefetch. No vector load reaches before tex or past its guard.
// Output uses scalar pair stores, requiring only BgAnim.h's 4-byte
// alignment, including buffers at 4, 8 or 12 mod 16.
//
// The hardware LOOP body is 47 instructions for eight pixels, 5.875/pixel.
// MOVI.32.A brings each pair into ARs; two EXTUIs both unpack and divide Q4
// by 16, so there is no index scratch buffer or vector multiply. Palette
// loads are interleaved with the next lane move or the next texture load.
// Every load has an independent instruction before use, including across
// the loop back edge. This is a warm-memory scheduling estimate, not device
// timing. Host x86 cannot execute PIE and times bandRef instead.
//
// GCC never allocates q0..q7, so they need no compiler clobber syntax. The
// block uses q0..q6 and SAR_BYTE, never writes CPENABLE, and leaves FreeRTOS
// to save PIE through its lazy coprocessor context mechanism.
GM_ANIM_IRAM __attribute__((noinline)) void refractionRowAsm(uint16_t *out, const uint16_t *tex,
        const int16_t *d, const uint16_t *pal, const int16_t *caps, int off, int add, int w) {
    const uint32_t bias = (uint32_t)(uint16_t)add * 65537u; // two copies of signed add
    int x = 0;
    while (w - x >= 8) {
        // A 512-pixel chunk can start at any off without leaving the duplicate.
        // 512 is divisible by eight, so a subsequent chunk keeps Bayer phase.
        const int n = (w - x < 512 ? w - x : 512) >> 3;
        const uint16_t *src = tex + ((off + x) & 511);
        const int16_t *dp = d, *cp = caps;
        uint32_t t0, t1, pair;
        asm volatile("ee.ld.128.usar.ip q0, %[src], 16\n"
                     "ee.vld.128.ip q3, %[d], 0\n"
                     "ee.ld.128.usar.ip q1, %[src], 16\n"
                     "ee.vld.128.ip q5, %[cap], 0\n"
                     "ee.movi.32.q q4, %[bias], 0\n"
                     "ee.movi.32.q q4, %[bias], 1\n"
                     "ee.movi.32.q q4, %[bias], 2\n"
                     "ee.movi.32.q q4, %[bias], 3\n"
                     "ee.zero.q q6\n"
                     "ee.vadds.s16 q4, q4, q3\n"
                     "loopnez %[n], 1f\n"
                     "ee.src.q q2, q0, q1\n"
                     "ee.orq q0, q1, q1\n"
                     "ee.vadds.s16 q2, q2, q4\n"
                     "ee.vmax.s16 q2, q2, q6\n"
                     "ee.vmin.s16 q2, q2, q5\n"
                     "ee.movi.32.a q2, %[pair], 0\n"
                     "extui %[t0], %[pair], 4, 8\n"
                     "extui %[t1], %[pair], 20, 8\n"
                     "addx2 %[t0], %[t0], %[pal]\n"
                     "addx2 %[t1], %[t1], %[pal]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "ee.movi.32.a q2, %[pair], 1\n"
                     "slli %[t1], %[t1], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "s32i %[t0], %[out], 0\n"
                     "extui %[t0], %[pair], 4, 8\n"
                     "extui %[t1], %[pair], 20, 8\n"
                     "addx2 %[t0], %[t0], %[pal]\n"
                     "addx2 %[t1], %[t1], %[pal]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "ee.movi.32.a q2, %[pair], 2\n"
                     "slli %[t1], %[t1], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "s32i %[t0], %[out], 4\n"
                     "extui %[t0], %[pair], 4, 8\n"
                     "extui %[t1], %[pair], 20, 8\n"
                     "addx2 %[t0], %[t0], %[pal]\n"
                     "addx2 %[t1], %[t1], %[pal]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "ee.movi.32.a q2, %[pair], 3\n"
                     "slli %[t1], %[t1], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "s32i %[t0], %[out], 8\n"
                     "extui %[t0], %[pair], 4, 8\n"
                     "extui %[t1], %[pair], 20, 8\n"
                     "addx2 %[t0], %[t0], %[pal]\n"
                     "addx2 %[t1], %[t1], %[pal]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "ee.ld.128.usar.ip q1, %[src], 16\n"
                     "slli %[t1], %[t1], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "s32i %[t0], %[out], 12\n"
                     "addi %[out], %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out), [src] "+&r"(src), [d] "+r"(dp), [cap] "+r"(cp),
                       [t0] "=&r"(t0), [t1] "=&r"(t1), [pair] "=&r"(pair)
                     : [bias] "r"(bias), [n] "r"(n), [pal] "r"(pal)
                     : "memory");
        x += n * 8;
    }
    // At most seven pixels, also covers widths below eight and odd widths.
    for (; x < w; x++) {
        int v = (tex[(off + x) & 511] + add + d[x & 7]) >> 4;
        v = v < 0 ? 0 : (v > 255 ? 255 : v);
        *out++ = pal[v];
    }
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        refractionRowAsm(dst, texQ4, dith + (y & 7) * 8, palette, capQ4, rowOff[y], rowAdd[y], w);
        dst += w;
    }
}
#else
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif

void release() {
    releaseTable(ramp, 256 * sizeof(uint16_t));
    releaseTable(capQ4, 8 * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(rowAdd, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(rowOff, static_cast<size_t>(allocH) * sizeof(uint16_t));
    releaseTable(texQ4, TEX_STORAGE * sizeof(uint16_t));
    sl = nullptr;
    allocH = 0;
    tablesValid = false;
    lastThemeGen = 0xFFFFFFFF;
    memset(lastP, 255, sizeof(lastP));
}

} // namespace

extern const BgAnimation bg_anim_refraction;
const BgAnimation bg_anim_refraction = {
    "refraction",
    "Refraction",
    {{"speed", "Speed", 50},
     {"bend", "Bend", 35},
     {"width", "Channel width", 65},
     {"contrast", "Contrast", 30},
     {"glow", "Glow wave", 50},
     {"darkness", "Darkness", 50},
     {"ripple", "Ripple", 50},
     {"flow", "Flow", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
