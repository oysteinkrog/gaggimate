#ifndef GAGGIMATE_SIM

// "Lava" — six soft metaballs on incommensurate orbits, cubic falloff with a
// t^6 hot core, palette-mapped. Design: anim-fluid (Fable), 2026-08-15.
// Perf pass (sleep17): band() originally called sqrtf() once per blob per
// touched row (~1440/frame) to get the exact per-row half-chord width, then
// re-derived integer x bounds from it every row. Both are unnecessary: the
// half-chord width sqrt(R2-dy2) is always <= the blob's full radius R, so a
// per-blob x-window of [bx-R, bx+R] is a safe *superset* of every row's true
// touched columns — no sqrt needed, and since bx/R don't depend on row, the
// integer window is computed once per blob in frame() instead of once per
// blob per row in band(). The inner pixel loop already has a cheap `tt<=0
// continue` early-out, so the extra scanned-but-empty columns near a blob's
// top/bottom (where the true chord is much narrower than 2R) cost only a
// few ALU ops each, not a sqrt. Net extra columns scanned vs the exact
// chord average ~27% (box area 4R^2 vs true disc area pi*R^2) — negligible
// next to a 90-cycle sqrtf removed for every one of those rows.
// The finalization loop also had two per-pixel float divides (field/1.6f
// clamp-normalize, dither/16.0f); both are replaced with a precomputed
// multiply constant / a 16-entry dither LUT (BAYER4 only has 16 distinct
// values), so band() now does zero libm and zero float divides per pixel.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

constexpr int NUM_BLOBS = 6;
// 1/1.6 exactly (1.6 = 8/5, so 1/1.6 = 0.625 = 5/8, exact in binary), folded
// with the *255 index scale so band() never divides by 1.6f per pixel.
constexpr float kFieldScale = 0.625f * 255.0f; // 159.375, exact

struct BlobDef {
    float fx1, fx2, fy1, fy2, ax1, ax2, ay1, ay2, px1, px2, py1, py2, cx, cy, R0, Rpulse, wR, phR;
};
struct BlobState {
    float bx, by, R2, invR2;
    int xlo, xhi; // precomputed per-blob column window (superset of the true chord), hoisted out of band()
};

BlobDef blobDef[NUM_BLOBS];
BlobState blob[NUM_BLOBS];
uint16_t *paletteLUT = nullptr;
float *fieldRow = nullptr; // one row of accumulated field
float ditherLUT[16];       // precomputed (BAYER4[k]/16 - 0.5) / 128 * 255, indexed by (y&3)*4+(x&3)
uint32_t lastThemeGen = 0xFFFFFFFF;
bool inited = false;

bool init(int w, int h) {
    if (paletteLUT == nullptr) {
        paletteLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    }
    if (fieldRow == nullptr) {
        fieldRow = static_cast<float *>(alloc(w * sizeof(float)));
    }
    if (paletteLUT == nullptr || fieldRow == nullptr) {
        return false;
    }
    if (!inited) {
        inited = true;
        for (int k = 0; k < 16; k++) {
            ditherLUT[k] = (BAYER4[k] / 16.0f - 0.5f) * (1.0f / 128.0f) * 255.0f;
        }
        for (int i = 0; i < NUM_BLOBS; i++) {
            const float ga = i * 2.39996323f; // golden angle spreads phases
            BlobDef &d = blobDef[i];
            d.fx1 = 0.55f + 0.11f * i;
            d.fx2 = 1.41421356f * (0.35f + 0.05f * i);
            d.fy1 = 0.63f + 0.09f * ((i * 3) % 5);
            d.fy2 = 1.73205081f * (0.30f + 0.04f * i);
            d.ax1 = w * 0.14f;
            d.ax2 = w * 0.07f;
            d.ay1 = h * 0.14f;
            d.ay2 = h * 0.07f;
            d.px1 = ga;
            d.px2 = ga * 2.1f;
            d.py1 = ga * 1.7f;
            d.py2 = ga * 0.6f;
            d.cx = w * 0.5f + w * 0.28f * cosf(ga);
            d.cy = h * 0.5f + h * 0.28f * sinf(ga * 1.3f);
            const float m = (w < h ? w : h);
            d.R0 = m * 0.19f;
            d.Rpulse = m * 0.05f;
            d.wR = 0.00011f + 0.00003f * i;
            d.phR = ga * 2.7f;
        }
        buildThemeRamp(paletteLUT, 256);
        lastThemeGen = themeGen();
    }
    return true;
}

float g_intensity = 1.0f;

void frame(uint32_t tMs, int w, int, const uint8_t p[4]) {
    const float omega0 = 6.2831853f / 45000.0f * speedMul(p[0]); // 45s base cycle at speed 50
    const float sizeMul = 0.6f + (p[1] / 100.0f);
    g_intensity = 0.5f + (p[2] / 100.0f) * 1.3f;
    if (themeGen() != lastThemeGen) {
        buildThemeRamp(paletteLUT, 256);
        lastThemeGen = themeGen();
    }
    const float t = tMs * omega0;
    for (int i = 0; i < NUM_BLOBS; i++) {
        const BlobDef &d = blobDef[i];
        BlobState &b = blob[i];
        b.bx = d.cx + d.ax1 * fastSinRad(t * d.fx1 + d.px1) + d.ax2 * fastSinRad(t * d.fx2 * 1.7f + d.px2);
        b.by = d.cy + d.ay1 * fastCosRad(t * d.fy1 * 1.13f + d.py1) + d.ay2 * fastSinRad(t * d.fy2 * 0.9f + d.py2);
        const float R = (d.R0 + d.Rpulse * fastSinRad(tMs * d.wR + d.phR)) * sizeMul;
        b.R2 = R * R;
        b.invR2 = 1.0f / b.R2;
        // Column window is a per-blob constant (doesn't depend on row/dy), so
        // it's computed once here instead of once per touched row in band().
        // Using the full radius R (not the per-row chord half-width) means no
        // sqrt is needed; band()'s inner loop still early-outs on tt<=0 for
        // the rows/columns outside the true chord. See file-top comment.
        int xlo = static_cast<int>(b.bx - R);
        int xhi = static_cast<int>(b.bx + R);
        if (xlo < 0) {
            xlo = 0;
        }
        if (xhi > w - 1) {
            xhi = w - 1;
        }
        b.xlo = xlo;
        b.xhi = xhi;
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const float intensity = g_intensity;
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        memset(fieldRow, 0, w * sizeof(float));
        for (int i = 0; i < NUM_BLOBS; i++) {
            const BlobState &b = blob[i];
            const float dy = y - b.by;
            const float dy2 = dy * dy;
            if (dy2 >= b.R2) {
                continue;
            }
            const float invR2 = b.invR2;
            const float bx = b.bx;
            // xlo/xhi are the precomputed full-radius window (superset of
            // the true chord for this row) — no per-row sqrt/bounds work.
            for (int x = b.xlo; x <= b.xhi; x++) {
                const float dx = x - bx;
                const float d2 = dx * dx + dy2;
                const float tt = 1.0f - d2 * invR2;
                if (tt <= 0) {
                    continue;
                }
                const float t3 = tt * tt * tt;
                fieldRow[x] += t3 * intensity + (tt > 0.7f ? t3 * t3 * intensity * 0.6f : 0.0f);
            }
        }
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        const int rowDitherBase = (y & 3) * 4;
        int x = 0;
        // Pair up stores into one 32-bit write (dst is row-aligned, w=480 is
        // even) — halves the number of store instructions in the hot loop.
        for (; x + 1 < w; x += 2) {
            float f0 = fieldRow[x];
            if (f0 > 1.6f) {
                f0 = 1.6f;
            }
            float f1 = fieldRow[x + 1];
            if (f1 > 1.6f) {
                f1 = 1.6f;
            }
            // f * kFieldScale + ditherLUT[...] == (f/1.6f + dith) * 255.0f
            // from the original code, with both per-pixel divides removed.
            int idx0 = static_cast<int>(f0 * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
            int idx1 = static_cast<int>(f1 * kFieldScale + ditherLUT[rowDitherBase + ((x + 1) & 3)]);
            if (idx0 < 0) {
                idx0 = 0;
            } else if (idx0 > 255) {
                idx0 = 255;
            }
            if (idx1 < 0) {
                idx1 = 0;
            } else if (idx1 > 255) {
                idx1 = 255;
            }
            const uint16_t p0 = paletteLUT[idx0];
            const uint16_t p1 = paletteLUT[idx1];
            *reinterpret_cast<uint32_t *>(out + x) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
        }
        for (; x < w; x++) {
            float f = fieldRow[x];
            if (f > 1.6f) {
                f = 1.6f;
            }
            int idx = static_cast<int>(f * kFieldScale + ditherLUT[rowDitherBase + (x & 3)]);
            if (idx < 0) {
                idx = 0;
            } else if (idx > 255) {
                idx = 255;
            }
            out[x] = paletteLUT[idx];
        }
    }
}

} // namespace

extern const BgAnimation bg_anim_lava;
const BgAnimation bg_anim_lava = {
    "lava",
    "Lava",
    {{"speed", "Speed", 50}, {"scale", "Blob size", 50}, {"glow", "Glow", 60}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
