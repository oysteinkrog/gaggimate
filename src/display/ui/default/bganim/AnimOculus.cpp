#ifndef GAGGIMATE_SIM

// "Soft Oculus" - a broad circular opening sitting inside the round panel,
// its inner edge softly lit, its diameter breathing slowly. Brainstormed
// 2026-09-09 with GPT (Codex CLI, gpt-6-astra) as candidate 5 of gm-4bd.
//
// The kernel is the separable one again, but the two terms are chosen so that
// their sum is the squared radius: colTermPh holds (x - cx)^2 scaled and
// rowTerm holds (y - cy)^2 scaled, so index = (colTerm + rowTerm) >> 4 rises
// with r squared. Every circular feature is then a palette feature, and the
// aperture breathes by rebuilding the 256-entry palette once per frame rather
// than by doing anything per pixel.
//
// Why r squared and not r: a square root per pixel is out of the question, and
// r squared is separable while r is not. The cost is radial resolution near
// the centre, where one palette index covers several pixels of radius; the
// aperture edge lives out where one index is one to three pixels, and the
// centre is a flat dark field where there is nothing to resolve.
//
// Scale. Each term is scaled by the panel's own half-width, so the aperture
// sits at the same fraction of the panel at 480, 240 and 233 wide. Both terms
// are capped at HALF_SPAN, which the square's corners are the only points that
// could reach; the sum plus the dither then stays inside 0..4095 with no
// per-pixel clamp. The inscribed circle lands on index 118, so the palette's
// upper half is the never-seen corners and holds the outermost tone flat.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <string.h>

namespace {
using namespace bganim;

// Sum-unit budget, 16 sum units to a palette index. Each of the two terms is
// capped at HALF_SPAN, so the largest sum is 2 * HALF_SPAN plus the dither,
// which is 4,056 of the 4,095 the palette index allows.
constexpr int HALF_SPAN = 1900;
constexpr int DISC_IDX = HALF_SPAN / 16; // 118, the palette index of the panel's edge circle

int16_t *colTerm = nullptr;   // frame() only -> PSRAM
int16_t *colTermPh = nullptr; // read every pixel -> slab, 8 y-phase copies
int16_t *rowTerm = nullptr;   // read once per row -> slab
uint16_t *themeRamp = nullptr; // frame() only -> PSRAM
uint16_t *palette = nullptr;  // read every pixel -> slab

int16_t dithOff[64] = {0};

uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;
int lastA = -1;
bool geomValid = false;
int allocW = 0, allocH = 0;

// Table placement, against the 9,216 B per-animation slab:
//   colTermPh  7,680 B  read every pixel                   HOT
//   rowTerm      960 B  read once per row                  HOT
//   palette      512 B  read every pixel, data-dependent,
//                       and rebuilt every frame            HOT
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

// The aperture: dark inside, a lit ring at index A, a mid tone outside that
// falls away toward the panel's edge and holds flat past it.
void buildPalette(int a, int edge) {
    const int inner = a - edge > 1 ? a - edge : 1;
    const int outerStart = a + edge;
    const int outerSpan = DISC_IDX - outerStart > 1 ? DISC_IDX - outerStart : 1;
    for (int i = 0; i < 256; i++) {
        int base;
        if (i <= inner) {
            // Inside the opening: nearly black, lifting a little toward the rim
            // so the interior is not a dead flat patch.
            base = 6 + (i * 26) / (inner > 0 ? inner : 1);
        } else if (i >= outerStart) {
            int j = i - outerStart;
            if (j > outerSpan) {
                j = outerSpan;
            }
            base = 120 - (j * 60) / outerSpan;
        } else {
            // The crossing, a smoothstep in Q8 from the inner tone to 120.
            const int u = ((i - inner) << 8) / (outerStart - inner);
            const int t = (u * u * (768 - 2 * u)) >> 16;
            base = 32 + (((120 - 32) * (t < 0 ? 0 : (t > 256 ? 256 : t))) >> 8);
        }
        // The lit edge: a triangular highlight centred on the aperture radius.
        const int d = i - a;
        const int ad = d < 0 ? -d : d;
        if (ad < edge) {
            base += ((255 - base) * (edge - ad)) / edge;
        }
        palette[i] = themeRamp[base < 0 ? 0 : (base > 255 ? 255 : base)];
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const bool paramsChanged = memcmp(p, lastP, 4) != 0;
    const bool themeChanged = themeGen() != lastThemeGen;
    if (paramsChanged || themeChanged) {
        buildThemeRamp(themeRamp, 256);
        lastThemeGen = themeGen();
    }

    // Time: one slow breath plus a second, slower one so the diameter never
    // repeats on an obvious beat. Unsigned, so the millis() wrap is a phase
    // wrap and nothing more.
    const uint32_t sp = 4 + static_cast<uint32_t>(p[0]) * 44 / 100; // 0.25x..3x
    const uint32_t ph = (tMs * sp) >> 12;
    const int16_t *sl = sinLut();
    const int edge = 2 + static_cast<int>(p[3]) * 14 / 100;   // 2..16 index units
    const int breath = static_cast<int>(p[2]) * 20 / 100;     // 0..20 index units
    const int lo = 12 + edge + breath;
    const int hi = DISC_IDX - 8 - edge - breath;
    const int mean = lo + static_cast<int>(p[1]) * (hi > lo ? hi - lo : 0) / 100;
    int a = mean + ((sl[ph & (SIN_N - 1)] * breath) >> 9) + ((sl[((ph * 5) >> 3) & (SIN_N - 1)] * breath) >> 10);
    if (a < 4) {
        a = 4;
    }
    if (a > DISC_IDX - 4) {
        a = DISC_IDX - 4;
    }
    if (a != lastA || paramsChanged || themeChanged) {
        buildPalette(a, edge);
        lastA = a;
    }

    if (paramsChanged || themeChanged || !geomValid) {
        // The dither is sized from the palette as it stands now. The aperture
        // moves the palette's features but not the spacing of its steps, so
        // this does not have to be redone on every breath.
        const float amp = ditherAmp(palette, 256) * 0.5f;
        for (int k = 0; k < 64; k++) {
            const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp * 16.0f / 31.5f);
            dithOff[k] = static_cast<int16_t>(lroundf(d));
        }

        // Geometry. Each term is normalised by the panel's own half-extent, so
        // the aperture sits at the same fraction of the panel whatever the
        // render width is. The cap is only reachable outside the inscribed
        // circle, which the round panel never shows.
        const int cx = w / 2;
        const int cy = h / 2;
        const int sx = cx > 0 ? cx : 1;
        const int sy = cy > 0 ? cy : 1;
        const int kx = HALF_SPAN * 65536 / (sx * sx);
        const int ky = HALF_SPAN * 65536 / (sy * sy);
        for (int x = 0; x < w; x++) {
            const int dx = x - cx;
            int v = (dx * dx * kx) >> 16;
            colTerm[x] = static_cast<int16_t>(v > HALF_SPAN ? HALF_SPAN : v);
        }
        for (int y = 0; y < h; y++) {
            const int dy = y - cy;
            int v = (dy * dy * ky) >> 16;
            rowTerm[y] = static_cast<int16_t>(v > HALF_SPAN ? HALF_SPAN : v);
        }
        const int cap = 4095 - HALF_SPAN;
        for (int ph8 = 0; ph8 < 8; ph8++) {
            int16_t *dstPh = colTermPh + static_cast<size_t>(ph8) * w;
            const int16_t *off = &dithOff[ph8 * 8];
            for (int x = 0; x < w; x++) {
                int v = colTerm[x] + off[x & 7];
                v = v < 0 ? 0 : (v > cap ? cap : v);
                dstPh[x] = static_cast<int16_t>(v);
            }
        }
        geomValid = true;
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

void release() {
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(colTermPh, static_cast<size_t>(8 * allocW) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(themeRamp, 256 * sizeof(uint16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    allocW = allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
    lastA = -1;
    geomValid = false;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

extern const BgAnimation bg_anim_oculus;
const BgAnimation bg_anim_oculus = {
    "oculus",
    "Oculus",
    {{"speed", "Speed", 15}, {"diameter", "Diameter", 65}, {"breath", "Breath", 20}, {"edge", "Edge softness", 55}},
    init,
    frame,
    band,
    release,
    nullptr,
};

#endif // GAGGIMATE_SIM
