#ifndef GAGGIMATE_SIM

// "Aurora" — two domain-warped sine curtains over a dark sky. The warp terms
// depend only on y and t (per-row constants); per pixel is two DDS phase
// accumulators indexing pre-weighted sine LUTs, a squared-intensity LUT, a
// row-constant scale, ordered dither, and one final LUT read that already
// has the sky color baked in. Everything per-pixel is integer table lookups
// and adds; the only per-pixel multiply left is the row-varying intensity
// scale. Design: anim-celestial (Fable), 2026-08-15. Perf pass: opt-aurora,
// 2026-08-15 (see report for the "sinLut() called twice per pixel is really
// two uninlinable function calls" finding — that was the bulk of the cost).

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
uint16_t *sqLUTStorage = nullptr; // [2*V_MAX+1], indexed via sqLUT = storage + V_MAX
uint16_t *sqLUT = nullptr;        // sqLUT[v] valid for v in [-V_MAX, V_MAX]

float g_t = 0, g_A1 = 0, g_A2 = 0;
int32_t g_inten14 = 0; // intensity * 1.4 in Q8

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
    if (sqLUTStorage == nullptr) {
        sqLUTStorage = static_cast<uint16_t *>(alloc((2 * V_MAX + 1) * sizeof(uint16_t)));
        sqLUT = sqLUTStorage + V_MAX;
    }
    if (glowLUT == nullptr || wLut1 == nullptr || wLut2 == nullptr || sqLUTStorage == nullptr) {
        return false;
    }
    for (int i = 0; i < SIN_N; i++) {
        wLut1[i] = static_cast<int32_t>(lut[i]) * W1;
        wLut2[i] = static_cast<int32_t>(lut[i]) * W2;
    }
    for (int32_t v = -V_MAX; v <= V_MAX; v++) {
        sqLUT[v] = v < 0 ? 0 : static_cast<uint16_t>((v * v) >> 12);
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
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const float t = g_t;
    // Cache the pre-weighted sine LUTs locally (they never change after
    // init()). Reading wLut1/wLut2 directly folds the old per-pixel
    // "sinLut()[..] * 635 / * 393" (a non-inlinable external call, called
    // twice per pixel, plus two multiplies) down to two plain array loads.
    const int32_t *__restrict w1 = wLut1;
    const int32_t *__restrict w2 = wLut2;
    const uint16_t *__restrict sq = sqLUT; // sq[v] valid for v in [-V_MAX, V_MAX], 0 for v<0

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
    // -- is baked into a 256-entry rowLUT[intensity] whenever that index
    // changes (<=10 rebuilds/frame), turning the whole per-pixel color step
    // into a single LUT read.
    uint16_t rowLUT[256];
    int lastBgIdx = -1;

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
            for (int i = 0; i < 256; i++) {
                const uint16_t glow = glowLUT[i];
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

        // Row phase starts (rad -> Q8 ticks); negatives wrap via uint32.
        uint32_t ph1 = static_cast<uint32_t>(static_cast<int64_t>((warp1 + t * 0.12f) * TICKS));
        uint32_t ph2 = static_cast<uint32_t>(static_cast<int64_t>((warp2 + t * 0.07f) * TICKS));
        const int dbase = (y & 7) * 8; // row-constant Bayer row offset

        const uint8_t *__restrict bayerRow = &BAYER8[dbase];
        uint16_t *__restrict row = dst + static_cast<size_t>(ry) * w;

        // One pixel's worth of work, `bit` is the Bayer column (0-7). Taking
        // it as a compile-time constant in the unrolled path below turns
        // "bayerRow[x & 7]" into a plain constant-offset load and removes
        // the per-pixel AND; it also amortizes the loop-control (increment +
        // compare + branch) across 8 pixels instead of paying it every pixel.
        auto emit = [&](int xi, int bit) {
            const int32_t v = (w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023]) >> 7; // ~±4112
            ph1 += STEP1;
            ph2 += STEP2;
            int32_t inten = (sq[v] * rowScale) >> 12; // branchless clip+square via signed-indexed LUT
            inten += (bayerRow[bit] - 32) >> 2;        // ordered dither
            if (inten < 0) {
                inten = 0;
            } else if (inten > 255) {
                inten = 255;
            }
            row[xi] = rowLUT[inten];
        };

        int x = 0;
        for (; x + 8 <= w; x += 8) {
            emit(x + 0, 0);
            emit(x + 1, 1);
            emit(x + 2, 2);
            emit(x + 3, 3);
            emit(x + 4, 4);
            emit(x + 5, 5);
            emit(x + 6, 6);
            emit(x + 7, 7);
        }
        for (; x < w; x++) {
            emit(x, x & 7);
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
