#ifndef GAGGIMATE_SIM

// "Nebula" — deep-space clouds from three samples of the shared tileable
// noise texture at 1x/2x/4x scale with independent drift directions (cheap
// multi-octave turbulence from one 64KB asset). The dominant 1x octave is
// bilinear-sampled to kill banding; 2x/4x are nearest. Scroll state lives in
// persistent Q8.8 accumulators whose overflow is texel-aligned, so the drift
// never jumps. Design: anim-atmosphere (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"

namespace {
using namespace bganim;

uint16_t *palette = nullptr;
const uint8_t *noise = nullptr;
uint32_t lastThemeGen = 0xFFFFFFFF;
// Q8.8 scroll accumulators — texel-aligned wraparound (65536 = 256 texels).
uint16_t sAx = 0, sAy = 0, sBx = 0, sBy = 0, sCx = 0, sCy = 0;
int g_wA = 32, g_wB = 20, g_wC = 12, g_densOff = 0;
int g_axI = 0, g_axF = 0, g_ayI = 0, g_ayF = 0, g_bx = 0, g_by = 0, g_cx = 0, g_cy = 0;

bool init(int, int) {
    if (palette == nullptr) {
        palette = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
        noise = noiseTex256();
        if (palette == nullptr || noise == nullptr) {
            return false;
        }
    }
    lastThemeGen = 0xFFFFFFFF;
    return true;
}

void frame(uint32_t, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildThemeRamp(palette, 256);
        lastThemeGen = themeGen();
    }
    // Per-frame deltas matched to the web preview at ~30fps: px/frame * 256.
    const float g = 1.2f * speedMul(p[0]);
    sAx += static_cast<uint16_t>(85.0f * g);
    sAy += static_cast<uint16_t>(51.0f * g);
    sBx -= static_cast<uint16_t>(145.0f * g);
    sBy += static_cast<uint16_t>(111.0f * g);
    sCx += static_cast<uint16_t>(222.0f * g);
    sCy -= static_cast<uint16_t>(179.0f * g);
    const float turb = p[2] / 100.0f;
    g_wA = static_cast<int>((0.60f - 0.15f * turb) * 64.0f);
    g_wB = static_cast<int>((0.25f + 0.05f * turb) * 64.0f);
    g_wC = 64 - g_wA - g_wB;
    g_densOff = static_cast<int>((p[1] - 50) * 1.1f);
    g_axI = sAx >> 8;
    g_axF = sAx & 0xFF;
    g_ayI = sAy >> 8;
    g_ayF = sAy & 0xFF;
    g_bx = sBx >> 8;
    g_by = sBy >> 8;
    g_cx = sCx >> 8;
    g_cy = sCy >> 8;
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
        int8_t dith[8];
        for (int k = 0; k < 8; k++) {
            dith[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 4);
        }
        const uint8_t *rowA0 = noise + ((y + g_ayI) & 255) * 256;
        const uint8_t *rowA1 = noise + ((y + g_ayI + 1) & 255) * 256;
        const uint8_t *rowB = noise + ((y * 2 + g_by) & 255) * 256;
        const uint8_t *rowC = noise + ((y * 4 + g_cy) & 255) * 256;
        for (int x = 0; x < w; x++) {
            // Octave A: bilinear (dominant low-frequency layer, banding-prone).
            const int x0 = (x + g_axI) & 255;
            const int x1 = (x0 + 1) & 255;
            const int va = (rowA0[x0] * (256 - g_axF) + rowA0[x1] * g_axF) >> 8;
            const int vb = (rowA1[x0] * (256 - g_axF) + rowA1[x1] * g_axF) >> 8;
            const int a = (va * (256 - g_ayF) + vb * g_ayF) >> 8;
            const int b = rowB[(x * 2 + g_bx) & 255];
            const int c = rowC[(x * 4 + g_cx) & 255];
            int v = ((a * g_wA + b * g_wB + c * g_wC) >> 6) + g_densOff + dith[x & 7];
            if (v < 0) {
                v = 0;
            } else if (v > 255) {
                v = 255;
            }
            row[x] = palette[v];
        }
    }
}

} // namespace

extern const BgAnimation bg_anim_nebula;
const BgAnimation bg_anim_nebula = {
    "nebula",
    "Nebula",
    {{"speed", "Drift speed", 50}, {"density", "Density", 50}, {"turbulence", "Turbulence", 40}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
