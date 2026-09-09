#ifndef GAGGIMATE_SIM

// "Folded Chevron" - broad angular folds crossing the face like a paper
// relief, each fold lit on one side and shaded on the other, advancing slowly
// along its own axis. Brainstormed 2026-09-09 with GPT (Codex CLI,
// gpt-6-astra) as candidate 3 of gm-4bd.
//
// The separable kernel with a wrap instead of a clamp:
//   index = ((colTermPh[x] + rowTerm[y]) >> 4) & 255
// The column term is the absolute distance from the centre column times the
// fold slope, which is what bends a stripe into a chevron; the row term is a
// linear phase in y that advances with time, which is what makes the folds
// march. Both stay non-negative, and the row term is reduced modulo 4096,
// which the mask makes exact: 4096 >> 4 is 256, so dropping whole multiples of
// 4096 cannot change the index.
//
// The relief comes from the palette, not from the geometry. The theme ramp is
// resampled through an asymmetric tent, a slow rise over three quarters of the
// repeat and a fast fall over the rest, with a thin highlight on the crest. A
// symmetric profile would read as stripes; the asymmetry is what makes each
// repeat look like a lit face and a shaded one.
//
// Both step sizes are derived from the render height, so the fold count and
// the fold angle are the same at 480, 240 and 233 wide.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

namespace {
using namespace bganim;

constexpr int WRAP = 4096;  // one palette lap in sum units
constexpr int RISE = 190;   // where the tent turns over, of 256

int16_t *colTerm = nullptr;   // frame() only -> PSRAM
int16_t *colTermPh = nullptr; // read every pixel -> slab, 8 y-phase copies
int16_t *rowTerm = nullptr;   // read once per row -> slab
uint16_t *themeRamp = nullptr; // frame() only -> PSRAM
uint16_t *palette = nullptr;  // read every pixel -> slab

int16_t dithOff[64] = {0};

uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;
bool colValid = false;
int allocW = 0, allocH = 0;

// Table placement, against the 9,216 B per-animation slab:
//   colTermPh  7,680 B  read every pixel                   HOT
//   rowTerm      960 B  read once per row                  HOT
//   palette      512 B  read every pixel, data-dependent   HOT
//   -------------------------------------------------------------
//              9,152 B of 9,216 B
//   colTerm      960 B  read only in frame()               PSRAM
//   themeRamp    512 B  read only when the palette is
//                       rebuilt                            PSRAM
void release();

bool init(int w, int h) {
    if (sinLut() == nullptr) {
        return false;
    }
    if (colTerm == nullptr) {
        colTerm = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
        if (colTerm == nullptr) {
            release(); // a partial set must not survive a failed init (gm-bzu.15)
            return false;
        }
        allocW = w;
    }
    if (colTermPh == nullptr) {
        colTermPh = static_cast<int16_t *>(allocHot(8 * allocW * sizeof(int16_t)));
        if (colTermPh == nullptr) {
            release();
            return false;
        }
    }
    if (rowTerm == nullptr) {
        rowTerm = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
        if (rowTerm == nullptr) {
            release();
            return false;
        }
        allocH = h;
    }
    if (themeRamp == nullptr) {
        themeRamp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
        if (themeRamp == nullptr) {
            release();
            return false;
        }
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

// The relief profile. Slow rise, fast fall, a thin crest highlight, and the
// two ends meeting closely enough that the wrap at 255 to 0 does not show.
void buildPalette(int contrastP) {
    buildThemeRamp(themeRamp, 256);
    const int lo = 18;
    const int hi = 60 + contrastP * 160 / 100; // 60..220
    for (int i = 0; i < 256; i++) {
        int v;
        if (i < RISE) {
            v = lo + (i * (hi - lo)) / RISE;
        } else {
            v = hi - ((i - RISE) * (hi - lo)) / (256 - RISE);
        }
        // The crest: a soft catch of light along the fold. Ten indices wide
        // rather than six, because at six the dither breaks the line into
        // beads in the host render instead of smoothing it.
        const int d = i - RISE;
        const int ad = d < 0 ? -d : d;
        if (ad < 10) {
            v += ((255 - v) * (10 - ad)) / 20;
        }
        palette[i] = themeRamp[v < 0 ? 0 : (v > 255 ? 255 : v)];
    }
    // A wrapping palette, like Plasma's wheel: its gradient is not uniform, so
    // the mean step spacing over-dithers the steep face. Plasma settled on
    // three quarters for the same reason and this profile is no steeper.
    const float amp = ditherAmp(palette, 256) * 0.75f;
    for (int k = 0; k < 64; k++) {
        const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 16.0f / 31.5f);
        dithOff[k] = static_cast<int16_t>(lroundf(d));
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    const bool paramsChanged = memcmp(p, lastP, 4) != 0;
    const bool themeChanged = themeGen() != lastThemeGen;
    if (paramsChanged || themeChanged) {
        buildPalette(p[3]);
        lastThemeGen = themeGen();
        colValid = false;
    }

    // Fold pitch: the number of folds down the panel, turned into sum units per
    // row. Derived from h, so the count is the same at every render size.
    const int denom = h > 0 ? h : 1;
    const int folds = 2 + static_cast<int>(p[1]) * 10 / 100; // 2..12 down the panel
    const int rowStep = folds * WRAP / denom;
    // Fold angle: the column term's step relative to the row's. At 1.0 the
    // folds sit at 45 degrees, which is what the middle of the slider gives.
    const int angleQ8 = 51 + static_cast<int>(p[2]) * 410 / 100; // 0.2x..1.8x in Q8

    if (!colValid) {
        const int cx = w / 2;
        const int colStep = (rowStep * angleQ8) >> 8;
        for (int x = 0; x < w; x++) {
            const int dx = x - cx;
            // Reduced modulo WRAP, which the index mask makes exact and which
            // is also what keeps the term inside int16: at the steepest
            // setting the raw product reaches about 44,000 at the panel edge.
            colTerm[x] = static_cast<int16_t>(((dx < 0 ? -dx : dx) * colStep) % WRAP);
        }
        for (int ph8 = 0; ph8 < 8; ph8++) {
            int16_t *dstPh = colTermPh + static_cast<size_t>(ph8) * w;
            const int16_t *off = &dithOff[ph8 * 8];
            for (int x = 0; x < w; x++) {
                int v = (colTerm[x] + off[x & 7]) % WRAP;
                if (v < 0) {
                    v += WRAP;
                }
                dstPh[x] = static_cast<int16_t>(v);
            }
        }
        colValid = true;
    }

    // Time. Unsigned, so the millis() wrap is a phase wrap. The phase is
    // reduced modulo WRAP before it reaches the table, which the index mask
    // makes exact.
    const uint32_t sp = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 0.25x..3x
    const uint32_t phase = ((tMs * sp) >> 7) % static_cast<uint32_t>(WRAP);
    for (int y = 0; y < h; y++) {
        rowTerm[y] = static_cast<int16_t>((static_cast<uint32_t>(y * rowStep) + phase) % static_cast<uint32_t>(WRAP));
    }
    memcpy(lastP, p, 4);
}

// Portable on every target, so the descriptor's bandRef slot is nullptr, which
// is what BgAnim.h asks for when there is no second path to compare against.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        const int rt = rowTerm[y];
        const int16_t *ct = colTermPh + static_cast<size_t>(y & 7) * w;
        int x = 0;
        for (; x + 1 < w; x += 2) {
            const uint16_t c0 = palette[((ct[x] + rt) >> 4) & 255];
            const uint16_t c1 = palette[((ct[x + 1] + rt) >> 4) & 255];
            *reinterpret_cast<uint32_t *>(dst) = static_cast<uint32_t>(c0) | (static_cast<uint32_t>(c1) << 16);
            dst += 2;
        }
        for (; x < w; x++) {
            *dst++ = palette[((ct[x] + rt) >> 4) & 255];
        }
    }
}

void release() {
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(colTermPh, static_cast<size_t>(8 * allocW) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(themeRamp, 256 * sizeof(uint16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    allocW = allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
    colValid = false;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

extern const BgAnimation bg_anim_chevrons;
const BgAnimation bg_anim_chevrons = {
    "chevrons",
    "Folded Chevron",
    {{"speed", "Speed", 15}, {"spacing", "Spacing", 65}, {"angle", "Angle", 50}, {"contrast", "Contrast", 35}},
    init,
    frame,
    band,
    release,
    nullptr,
};

#endif // GAGGIMATE_SIM
