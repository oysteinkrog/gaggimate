#ifndef GAGGIMATE_SIM

// "Aurora" — two domain-warped sine curtains over a dark sky. The warp terms
// depend only on y and t (per-row constants); per pixel is two DDS phase
// accumulators indexing pre-weighted sine LUTs, a squared-intensity LUT, a
// row-constant scale, ordered dither, and one final LUT read that already
// has the sky color baked in. Everything per-pixel is integer table lookups
// and one multiply (the row-varying intensity scale) and adds -- no clip
// branches (rowLUT is padded to absorb the analytically bounded pre-clip
// range) and no per-pixel libcalls (the wraparound-safe phase-base cast to
// int64 runs once/frame in frame(), not 960x/frame inside band()). Design:
// anim-celestial (Fable), 2026-08-15. Perf pass 1: opt-aurora, 2026-08-15
// (host band_ms 0.936 -> 0.277; sinLut() called twice per pixel turned out
// to be two uninlinable function calls -- that was the bulk of the cost).
// Perf pass 2: opt-aurora, 2026-08-15 (host band_ms 0.277 -> ~0.19; hoisted
// the per-row __fixsfdi wraparound cast to frame(), replaced the clip-then-
// index color step with a padded rowLUT, and folded the ordered-dither delta
// into 8 row-constant pointers so the x loop is a single LUT read per pixel
// plus one paired 32-bit store per two pixels).

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

uint16_t *glowLUT = nullptr; // [256 intensity] -> RGB565 glow color (theme-baked)
uint32_t lastThemeGen = 0xFFFFFFFF;

// f1 = 0.026, f1b = 0.017 rad/px -> phase steps in Q8 ticks of the 1024-LUT.
constexpr float TICKS = 1024.0f * 256.0f / 6.2831853f; // rad -> Q8 LUT ticks
constexpr uint32_t STEP1 = static_cast<uint32_t>(0.026f * TICKS);
constexpr uint32_t STEP2 = static_cast<uint32_t>(0.017f * TICKS);

// Curtain weights (0.62/0.38 in Q7) are compile-time constants, so pre-scaling
// the shared sine LUT by them once (at init) turns the per-pixel "v1*635"/
// "v2*393" multiplies into plain array reads.
constexpr int32_t W1 = 635, W2 = 393;
int32_t *wLut1 = nullptr; // [1024] sin1024(i) * W1
int32_t *wLut2 = nullptr; // [1024] sin1024(i) * W2

// v = (wLut1+wLut2)>>7 ranges about +-4112 (512*(W1+W2)>>7); sqLUT covers the
// full signed range so the per-pixel "clip negative to 0, then square>>12"
// collapses to one branchless offset array read (negative entries are 0).
constexpr int32_t V_MAX = (512 * (W1 + W2)) >> 7;

float g_t = 0, g_A1 = 0, g_A2 = 0;
int32_t g_inten14 = 0; // intensity * 1.4 in Q8

// Per-row phase = TICKS*(warp(y) + t*coeff). t*coeff is frame-constant (same
// for all 480 rows), but t itself is proportional to uptime and unbounded, so
// TICKS*t*coeff can exceed int32 range after long enough uptime. The old code
// cast the *whole* per-row sum through int64 to get correct uint32 wraparound
// (see boot-loop history in other anims' OTA notes) -- but doing that 64-bit
// libcall (__fixsfdi) 2x/row * 480 rows = 960x/frame was the single biggest
// remaining per-frame cost. Splitting the sum algebraically fixes this: the
// int64-safe wraparound conversion happens ONCE per frame (here) for the
// t*coeff term only; per-row, warp(y)*TICKS is bounded (|warp|<=3, so
// |warp*TICKS|<~125000, well inside int32) and needs only a plain trunc-to-
// int32 (one hardware instruction, no libcall). uint32 addition of the two
// wraps identically to converting the combined sum, so long-uptime behavior
// is unchanged.
uint32_t g_phBase1 = 0, g_phBase2 = 0;

// Curtain color rides the theme's mid-to-bright range; the fade ramp keeps
// low intensities near-black so the additive blend stays subtle.
void buildGlowLUT() {
    for (int i = 0; i < 256; i++) {
        uint8_t c[3];
        themeRGB(40 + ((i * 215) >> 8), c);
        const float scale = fminf(1.0f, (i / 255.0f) * 2.2f);
        glowLUT[i] = rgb565(clamp8f(c[0] * scale), clamp8f(c[1] * scale), clamp8f(c[2] * scale));
    }
}

bool init(int, int) {
    const int16_t *lut = sinLut();
    if (lut == nullptr) {
        return false;
    }
    if (glowLUT == nullptr) {
        glowLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    }
    if (wLut1 == nullptr) {
        wLut1 = static_cast<int32_t *>(alloc(SIN_N * sizeof(int32_t)));
    }
    if (wLut2 == nullptr) {
        wLut2 = static_cast<int32_t *>(alloc(SIN_N * sizeof(int32_t)));
    }
    if (glowLUT == nullptr || wLut1 == nullptr || wLut2 == nullptr) {
        return false;
    }
    for (int i = 0; i < SIN_N; i++) {
        wLut1[i] = static_cast<int32_t>(lut[i]) * W1;
        wLut2[i] = static_cast<int32_t>(lut[i]) * W2;
    }
    buildGlowLUT();
    lastThemeGen = themeGen();
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildGlowLUT();
        lastThemeGen = themeGen();
    }
    g_t = (tMs * 0.001f) * 0.45f * speedMul(p[0]);
    g_A1 = 0.6f + (p[2] / 100.0f) * 2.4f;
    g_A2 = 0.4f + (p[2] / 100.0f) * 1.6f;
    g_inten14 = static_cast<int32_t>((p[1] / 100.0f) * 1.4f * 256.0f);
    // Wraparound-safe once/frame (see note by g_phBase1/2 above); replaces the
    // 960x/frame int64 conversion that used to run per-row inside band().
    g_phBase1 = static_cast<uint32_t>(static_cast<int64_t>(g_t * 0.12f * TICKS));
    g_phBase2 = static_cast<uint32_t>(static_cast<int64_t>(g_t * 0.07f * TICKS));
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const float t = g_t;
    // Cache the pre-weighted sine LUTs locally (they never change after
    // init()). Reading wLut1/wLut2 directly folds the old per-pixel
    // "sinLut()[..] * 635 / * 393" (a non-inlinable external call, called
    // twice per pixel, plus two multiplies) down to two plain array loads.
    const int32_t *__restrict w1 = wLut1;
    const int32_t *__restrict w2 = wLut2;

    // Cache cosTableF() once too: fastSinRad()/fastCosRad() each call it
    // internally (another non-inlinable external call), and row-setup below
    // calls the sin helper twice per row (960x/frame). Reading the table
    // through this pointer keeps row-setup call-free as well.
    const float *__restrict ct = cosTableF();
    auto rowCos = [ct](float rad) { return ct[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; };
    auto rowSin = [&rowCos](float rad) { return rowCos(rad - 1.5707963f); };

    // bg (sky color) only takes ~10 distinct values across the whole 480-row
    // frame (themeRGB is sampled at yn*10, truncated to an int 0..9), so the
    // glow+sky blend -- previously unpacked/added/clamped/repacked per pixel
    // -- is baked into a rowLUT[intensity] whenever that index changes (<=10
    // rebuilds/frame), turning the whole per-pixel color step into a single
    // LUT read.
    //
    // rowLUT is padded so the per-pixel clip branches (inten<0 / inten>255)
    // disappear entirely: pre-dither scaled intensity (sq[v]*rowScale>>12) is
    // analytically bounded by [0, SQ_MAX*INTEN14_MAX>>12] (both factors are
    // compile-time-known worst cases: v's square-LUT tops out at v=V_MAX, and
    // g_inten14 tops out at p[1]=100), and the ordered-dither delta is
    // bounded by BAYER8's [0,63] range folded through (val-32)>>2, i.e.
    // [-8,7]. ROWLUT_PAD absorbs the low excursion so the combined index is
    // never negative; ROWLUT_SIZE covers the full span (+8 extra headroom).
    constexpr int32_t SQ_MAX = (V_MAX * V_MAX) >> 12;      // sqLUT[V_MAX], the largest table entry
    constexpr int32_t INTEN14_MAX = 358;                   // p[1]=100 -> (100/100.0f)*1.4f*256.0f, truncated
    constexpr int32_t ROWLUT_PAD = 8;                      // covers dither's -8 low excursion
    constexpr int32_t ROWLUT_SIZE = ((SQ_MAX * INTEN14_MAX) >> 12) + 7 + ROWLUT_PAD + 1 + 8;
    uint16_t rowLUT[ROWLUT_SIZE];
    int lastBgIdx = -1;

    // Ordered-dither delta, pre-folded with rowLUT's low-pad offset:
    // ditherFold[(y&7)*8 + bit] == ROWLUT_PAD + ((BAYER8[row][bit]-32)>>2).
    // Precomputed once per band() call (64 entries) instead of recomputing
    // bayerRow[bit]-32>>2 on every one of the 230400 pixels/frame -- BAYER8
    // is row-constant (only 8 distinct rows, cycling with y&7).
    int32_t ditherFold[64];
    for (int i = 0; i < 64; i++) {
        ditherFold[i] = ROWLUT_PAD + ((static_cast<int32_t>(BAYER8[i]) - 32) >> 2);
    }

    for (int ry = 0; ry < rows; ry++) {
        const int y = y0 + ry;
        const float warp1 = rowSin(y * 0.021f + t * 0.5f) * g_A1;
        const float warp2 = rowSin(y * 0.013f - t * 0.44f + 1.7f) * g_A2;
        const float yn = y * (1.0f / 480.0f); // was a divide (__divsf3 libcall on device)
        float env = 1.0f - fabsf(yn - 0.32f) * (1.0f / 0.85f);
        env = env < 0 ? 0 : env * env;
        const int32_t envQ12 = static_cast<int32_t>(env * 4096.0f);
        // Fold env and intensity into one row-constant scale so the x loop
        // does a single multiply+shift instead of two.
        const int32_t rowScale = (envQ12 * g_inten14) >> 12;

        const int bgIdx = static_cast<int>(yn * 10.0f); // sky sits in the darkest ~4% of the theme
        if (bgIdx != lastBgIdx) {
            uint8_t bg[3];
            themeRGB(bgIdx, bg);
            const int bgR5 = bg[0] >> 3, bgG6 = bg[1] >> 2, bgB5 = bg[2] >> 3;
            for (int i = 0; i < ROWLUT_SIZE; i++) {
                // Undo the pad, then clamp to the real [0,255] intensity
                // range -- entries outside it just replicate the black or
                // full-glow endpoint, which is exactly what the old
                // clip-then-index sequence produced per pixel.
                int inten = i - ROWLUT_PAD;
                if (inten < 0) {
                    inten = 0;
                } else if (inten > 255) {
                    inten = 255;
                }
                const uint16_t glow = glowLUT[inten];
                int r = bgR5 + ((glow >> 11) & 0x1F);
                int g = bgG6 + ((glow >> 5) & 0x3F);
                int b = bgB5 + (glow & 0x1F);
                if (r > 0x1F) {
                    r = 0x1F;
                }
                if (g > 0x3F) {
                    g = 0x3F;
                }
                if (b > 0x1F) {
                    b = 0x1F;
                }
                rowLUT[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
            }
            lastBgIdx = bgIdx;
        }

        // Row phase starts (rad -> Q8 ticks). warp*TICKS is bounded (plain
        // int32 trunc, no libcall); g_phBase1/2 carries the frame-constant,
        // wraparound-safe time term computed once in frame() (see note there
        // and by g_phBase1/2's declaration) -- uint32 addition of the two
        // wraps identically to the old single int64-cast-then-truncate.
        uint32_t ph1 = g_phBase1 + static_cast<uint32_t>(static_cast<int32_t>(warp1 * TICKS));
        uint32_t ph2 = g_phBase2 + static_cast<uint32_t>(static_cast<int32_t>(warp2 * TICKS));
        const int dbase = (y & 7) * 8; // row-constant Bayer row offset

        uint16_t *__restrict row = dst + static_cast<size_t>(ry) * w;

        // 8 row-constant pointers, one per Bayer column, each pre-offset into
        // rowLUT by that column's dither delta. This folds the per-pixel
        // "scaledSq + ditherRow[bit]" add into the pointer itself (computed
        // 8x/row instead of 2x/pixel), so the x loop's only remaining work
        // per pixel is the LUT index -- no add left before the final read.
        const uint16_t *rowLUTAtBit[8];
        for (int b = 0; b < 8; b++) {
            rowLUTAtBit[b] = rowLUT + ditherFold[dbase + b];
        }

        // One pixel's worth of work, `bit` is the Bayer column (0-7). Taking
        // it as a compile-time constant in the unrolled path below turns
        // "rowLUTAtBit[bit]" into a loop-invariant pointer load and removes
        // the per-pixel AND; it also amortizes the loop-control (increment +
        // compare + branch) across 8 pixels instead of paying it every pixel.
        // No clip left at all: rowLUT is padded to cover the analytically
        // bounded pre-clip range, so the index is always valid.
        // always_inline is load-bearing, not a hint. At -O2 GCC declined to
        // inline these two and emitted them as real functions, so the
        // "unrolled, branch-free" loop below actually compiled to four
        // indirect callx8 per 8 pixels -- a windowed-ABI register rotation
        // plus caller-saved spills to stack plus a recomputed frame address
        // to pass the by-reference captures, on every second pixel. That is
        // where aurora's ~82 cycles/pixel went. Verified by disassembly, not
        // assumed: before this attribute, band() contained 8 callx8 and the
        // object exported two lambda operator() symbols.
        auto pixel = [&](int bit) __attribute__((always_inline)) -> uint16_t {
            const int32_t v = (w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023]) >> 7; // ~±4112
            ph1 += STEP1;
            ph2 += STEP2;
            // Was a table read: sqLUT[v] for v in [-V_MAX, V_MAX], which at
            // V_MAX=4112 is 16,450 bytes -- over alloc()'s 8 KB threshold, so
            // it lived in PSRAM, and v is computed per pixel, so the access
            // was random rather than the sequential sweep that threshold
            // assumes. Every pixel paid a PSRAM round trip to look up one
            // multiply and one shift. Values are identical, so the rowLUT
            // padding bounds derived from SQ_MAX still hold exactly.
            const int32_t vc = v > 0 ? v : 0;
            const int32_t scaledSq = (((vc * vc) >> 12) * rowScale) >> 12;
            return rowLUTAtBit[bit][scaledSq];
        };
        // Two pixels' worth of work packed into one 32-bit store (dst is
        // 4-byte aligned and w is even -- see xtensa-asm addendum). Halves
        // the store traffic vs. one s16i per pixel.
        auto emitPair = [&](int xi, int bit0) __attribute__((always_inline)) {
            const uint16_t p0 = pixel(bit0);
            const uint16_t p1 = pixel(bit0 + 1);
            *reinterpret_cast<uint32_t *>(row + xi) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
        };

        int x = 0;
        for (; x + 8 <= w; x += 8) {
            emitPair(x + 0, 0);
            emitPair(x + 2, 2);
            emitPair(x + 4, 4);
            emitPair(x + 6, 6);
        }
        for (; x + 2 <= w; x += 2) {
            emitPair(x, x & 7);
        }
        for (; x < w; x++) {
            row[x] = pixel(x & 7);
        }
    }
}

} // namespace

extern const BgAnimation bg_anim_aurora;
const BgAnimation bg_anim_aurora = {
    "aurora",
    "Aurora",
    {{"speed", "Speed", 50}, {"intensity", "Intensity", 55}, {"waviness", "Waviness", 50}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
