#ifndef GAGGIMATE_SIM

// "Refraction" - broad tonal channels bending gently across the face, as if
// seen through uneven glass, drifting sideways slowly. Brainstormed
// 2026-09-09 with GPT (Codex CLI, gpt-6-astra) in the replacement round of
// gm-4bd.
//
// A different kernel shape from the separable ones: a per-row offset into a
// wide texture row.
//
//   index = texPh[(y & 7) * 512 + ((x + rowOff[y]) & 511)]
//
// Two gathers, one add, one mask. The texture is one 512-entry profile built
// from harmonics whose periods all divide 512, so the wrap at 511 is seamless
// and the whole panel is one continuous field rather than a tiling. The bend
// is entirely in rowOff: each row samples the same profile at its own offset,
// which shears the channels, and because the offsets come from slow sine terms
// the shear varies smoothly down the panel.
//
// The dither is folded into the eight phase copies at texture positions rather
// than screen positions. Since 512 is a multiple of 8, that makes the Bayer
// pattern a horizontal translation of itself per row, by rowOff[y], which is a
// valid ordered dither: neighbouring rows differ by a couple of pixels of
// translation, so the pattern stays coherent instead of aliasing.
//
// The one way this look fails is tearing: if two neighbouring rows differ by
// an offset comparable to a channel's width, the channel breaks instead of
// leaning. The bend amplitudes are sized so the steepest per-row step is about
// six pixels at the top of the slider against channels tens of pixels wide,
// which reads as a lean.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

namespace {
using namespace bganim;

constexpr int TEX = 512;      // texture entries, a multiple of 8 for the dither
constexpr int TEX_MASK = 511;

uint8_t *profile = nullptr; // frame() only -> PSRAM
uint8_t *texPh = nullptr;   // read every pixel -> slab, 8 y-phase copies
uint16_t *rowOff = nullptr; // read once per row -> slab
uint16_t *palette = nullptr; // read every pixel -> slab

int16_t dithOff[64] = {0};

uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;
int allocH = 0;

// Table placement, against the 9,216 B per-animation slab:
//   texPh      4,096 B  read every pixel                   HOT
//   rowOff       960 B  read once per row                  HOT
//   palette      512 B  read every pixel, data-dependent   HOT
//   -------------------------------------------------------------
//              5,568 B of 9,216 B
//   profile      512 B  read only when the texture is
//                       rebuilt                            PSRAM
// This one leaves 3.6 KB of the slab unused. The texture is one row wide, not
// one per column, which is the whole saving of this kernel shape over the
// separable ones.
void release();

bool init(int, int h) {
    if (sinLut() == nullptr) {
        return false;
    }
    if (profile == nullptr) {
        profile = static_cast<uint8_t *>(alloc(TEX));
        if (profile == nullptr) {
            release(); // a partial set must not survive a failed init (gm-bzu.15)
            return false;
        }
    }
    if (texPh == nullptr) {
        texPh = static_cast<uint8_t *>(allocHot(8 * TEX));
        if (texPh == nullptr) {
            release();
            return false;
        }
    }
    if (rowOff == nullptr) {
        rowOff = static_cast<uint16_t *>(allocHot(h * sizeof(uint16_t)));
        if (rowOff == nullptr) {
            release();
            return false;
        }
        allocH = h;
    }
    if (palette == nullptr) {
        palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
        if (palette == nullptr) {
            release();
            return false;
        }
    }
    return true;
}

// The channel profile: four harmonics whose periods divide 512. The width
// slider moves weight between them, so a low setting is many narrow channels
// and a high one a few broad ones.
void buildTexture(int widthP, int contrastP) {
    const int16_t *sl = sinLut();
    // Weights in Q8. The two low harmonics carry the broad channels, the two
    // high ones the fine structure between them.
    const int wLow = 40 + widthP * 216 / 100;   // 40..256
    const int wHigh = 256 - widthP * 216 / 100; // 256..40
    const int amp = 40 + contrastP * 160 / 100; // 40..200, half the palette swing
    for (int t = 0; t < TEX; t++) {
        // SIN_N is 1024 and TEX is 512, so a harmonic of k cycles per texture
        // steps the sine table by 2k per entry and closes exactly at the wrap.
        const int h1 = sl[(t * 2) & (SIN_N - 1)];
        const int h2 = sl[(t * 4 + 300) & (SIN_N - 1)];
        const int h3 = sl[(t * 6 + 700) & (SIN_N - 1)];
        const int h5 = sl[(t * 10 + 150) & (SIN_N - 1)];
        // Each harmonic is +-512; the weighted sum is scaled back to +-amp.
        const int v = (h1 * wLow + h2 * (wLow / 2) + h3 * wHigh + h5 * (wHigh / 2)) / (3 * 512);
        int idx = 128 + (v * amp) / (256 * 2);
        profile[t] = static_cast<uint8_t>(idx < 0 ? 0 : (idx > 255 ? 255 : idx));
    }
    // Texture values are palette indices already, so the dither amplitude is in
    // index units with no scaling.
    const float damp = ditherAmp(palette, 256) * 0.5f;
    for (int k = 0; k < 64; k++) {
        const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (damp / 31.5f);
        dithOff[k] = static_cast<int16_t>(lroundf(d));
    }
    for (int ph = 0; ph < 8; ph++) {
        uint8_t *dst = texPh + static_cast<size_t>(ph) * TEX;
        const int16_t *off = &dithOff[ph * 8];
        for (int t = 0; t < TEX; t++) {
            int v = profile[t] + off[t & 7];
            dst[t] = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    (void)w;
    if (memcmp(p, lastP, 4) != 0 || themeGen() != lastThemeGen) {
        memcpy(lastP, p, 4);
        lastThemeGen = themeGen();
        buildThemeRamp(palette, 256);
        buildTexture(p[2], p[3]);
    }

    // Time. Unsigned, so the millis() wrap is a phase wrap.
    const uint32_t sp = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 0.25x..3x
    const uint32_t base = (tMs * sp) >> 4;
    const uint32_t scroll = base >> 6;        // the sideways drift
    const uint32_t bendPh1 = base >> 8;
    const uint32_t bendPh2 = (base * 3) >> 10;
    const int16_t *sl = sinLut();

    // Bend. Two vertical terms: a faster one with a small amplitude and a
    // slower one with a large one. The per-row step of the sum is what decides
    // whether the channels lean or tear, so the amplitudes are picked against
    // their own slopes, not against how far they travel.
    const int amp1 = static_cast<int>(p[1]) * 80 / 100;  // 0..80
    const int amp2 = static_cast<int>(p[1]) * 200 / 100; // 0..200
    // Vertical frequencies as sine-table steps per row, scaled from the render
    // height so the bend has the same shape at 480, 240 and 233.
    const int denom = h > 0 ? h : 1;
    const int f1Q4 = 1536 * 16 / denom; // about 1.5 waves down the panel
    const int f2Q4 = 560 * 16 / denom;  // about half a wave
    for (int y = 0; y < h; y++) {
        const int b1 = (sl[(((y * f1Q4) >> 4) + bendPh1) & (SIN_N - 1)] * amp1) / 256;
        const int b2 = (sl[(((y * f2Q4) >> 4) + bendPh2) & (SIN_N - 1)] * amp2) / 256;
        // Two TEX terms added before the mask keep the value non-negative
        // whatever the bend does, so the mask is the only reduction needed.
        rowOff[y] = static_cast<uint16_t>((static_cast<uint32_t>(b1 + b2 + 2 * TEX) + scroll) & TEX_MASK);
    }
}

// Portable on every target, so the descriptor's bandRef slot is nullptr, which
// is what BgAnim.h asks for when there is no second path to compare against.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        const uint8_t *tex = texPh + static_cast<size_t>(y & 7) * TEX;
        const int off = rowOff[y];
        int x = 0;
        for (; x + 1 < w; x += 2) {
            const uint16_t c0 = palette[tex[(x + off) & TEX_MASK]];
            const uint16_t c1 = palette[tex[(x + 1 + off) & TEX_MASK]];
            *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(c0) | (static_cast<uint32_t>(c1) << 16);
            dst += 2;
        }
        for (; x < w; x++) {
            *dst++ = palette[tex[(x + off) & TEX_MASK]];
        }
    }
}

void release() {
    releaseTable(profile, static_cast<size_t>(TEX));
    releaseTable(texPh, static_cast<size_t>(8 * TEX));
    releaseTable(rowOff, static_cast<size_t>(allocH) * sizeof(uint16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

extern const BgAnimation bg_anim_refraction;
const BgAnimation bg_anim_refraction = {
    "refraction",
    "Refraction",
    {{"speed", "Speed", 18}, {"bend", "Bend", 35}, {"width", "Channel width", 65}, {"contrast", "Contrast", 30}},
    init,
    frame,
    band,
    release,
    nullptr,
};

#endif // GAGGIMATE_SIM
