#ifndef GAGGIMATE_SIM

// "Glint" - a nearly black face with one soft, curved, tapered highlight
// sweeping slowly across it, the way a light source crosses a curved glass
// front. Brainstormed 2026-09-09 with GPT (Codex CLI, gpt-6-astra) as
// candidate 9 of gm-4bd, and the darkest thing in the fleet: at rest almost
// the whole panel is one colour.
//
// The third run based renderer, and the one that puts its shape in the run
// itself rather than in a field. A row's highlight is one interval, and inside
// it the pixel walks a fixed point cursor across a single profile:
//
//   idx = acc >> 8;  acc += K;
//   row[x] = palette[(profPh[(y & 7) * PROF_STRIDE + idx] * A) >> 8];
//
// two gathers, one add, one multiply and two shifts. K is 65280 divided by the
// row's highlight width, so the same 256 entry profile stretches to whatever
// width the row needs and nothing per pixel depends on how wide that is. A is
// the row's amplitude, and multiplying by it after the gather is what tapers
// the highlight along its length: a row near an end spans a small part of the
// palette, so it is dimmer and its gradient is shallower.
//
// Outside the interval a row is a single colour, one store per pixel and no
// gather at all, because the palette is built so that entry 0 is exactly the
// background. That also means the highlight's dim edges fade into the
// background rather than ringing against it.
//
// The dither is folded into eight phase copies of the profile, so band() pays
// nothing for it. It is indexed by the profile position rather than by x,
// which is a scramble of x rather than a plain 8 pixel repeat, and that is
// fine: an ordered dither only needs a bounded pattern that does not correlate
// with the gradient. The dither is added before the multiply by A, so a dim
// row gets proportionally less of it, which is what a shallower gradient wants.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

constexpr int PROF_N = 256;      // useful profile entries
constexpr int PROF_STRIDE = 264; // eight zero entries of pad after them
constexpr int BG_BASE = 6;       // background position in the theme ramp
constexpr int REC = 5;           // uint16 per row: x0, x1, K, acc0, A

uint16_t *ramp = nullptr;    // theme ramp, read in frame() only -> PSRAM
uint16_t *rowRec = nullptr;  // five reads per row -> slab
uint8_t *profPh = nullptr;   // read every highlight pixel -> slab
uint16_t *palette = nullptr; // read every highlight pixel -> slab

int16_t dithOff[64] = {0};

uint8_t lastP[4] = {255, 255, 255, 255};
uint32_t lastThemeGen = 0xFFFFFFFF;
bool tablesValid = false;
int allocH = 0;

// Table placement, against the 9,216 B per-animation slab:
//   rowRec     4,800 B  five reads per row                 HOT
//   profPh     2,112 B  read every highlight pixel         HOT
//   palette      512 B  read every highlight pixel         HOT
//   -------------------------------------------------------------
//              7,424 B of 9,216 B
//   ramp         512 B  read in frame() only               PSRAM
// This is the smallest slab footprint of the ten, because the profile is one
// row of 256 entries rather than one entry per column.
void release();

bool init(int w, int h) {
    (void)w;
    if (sinLut() == nullptr) {
        return false;
    }
    // Every early return goes through release(), so a half built set is never
    // left behind for the retry to trip over (gm-bzu.15).
    if (ramp == nullptr) {
        ramp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
        if (ramp == nullptr) {
            release();
            return false;
        }
    }
    if (rowRec == nullptr) {
        rowRec = static_cast<uint16_t *>(allocHot(REC * h * sizeof(uint16_t)));
        if (rowRec == nullptr) {
            release();
            return false;
        }
        allocH = h;
    }
    if (profPh == nullptr) {
        profPh = static_cast<uint8_t *>(allocHot(8 * PROF_STRIDE));
        if (profPh == nullptr) {
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

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const bool paramsChanged = memcmp(p, lastP, 4) != 0;
    const bool themeChanged = themeGen() != lastThemeGen;
    if (paramsChanged || themeChanged || !tablesValid) {
        buildThemeRamp(ramp, 256);
        lastThemeGen = themeGen();
        // The palette is the theme ramp compressed into the band above the
        // background, so entry 0 is the background exactly. That is what lets
        // band() fill outside the highlight with one colour and still have the
        // highlight's own dim edges meet it without a seam.
        for (int i = 0; i < 256; i++) {
            palette[i] = ramp[BG_BASE + i * (255 - BG_BASE) / 255];
        }
        // Three quarters of ditherAmp()'s figure, the same fraction plasma
        // uses: the profile's own gradient is steep at the shoulders and flat
        // at the crest, so the full mean-spacing amplitude shows as a weave on
        // the crest. Offsets here are whole palette indices, because the
        // profile is stored as one byte per entry.
        const float amp = ditherAmp(palette, 256) * 0.75f;
        for (int k = 0; k < 64; k++) {
            const float d = (static_cast<float>(BAYER8[k]) - 31.5f) * (amp / 31.5f);
            dithOff[k] = static_cast<int16_t>(lroundf(d));
        }
        for (int ph = 0; ph < 8; ph++) {
            uint8_t *dst = profPh + static_cast<size_t>(ph) * PROF_STRIDE;
            const int16_t *off = &dithOff[ph * 8];
            for (int i = 0; i < PROF_N; i++) {
                // A quartic bump: a triangle squared twice, which gives a
                // broad crest and shoulders that reach zero smoothly. A plain
                // triangle leaves a visible crease down the middle of the
                // highlight and a hard line at each edge.
                int t = 2 * i - 255;
                if (t < 0) {
                    t = -t;
                }
                const int tri = 256 - t; // 1 at the ends, 256 at the crest
                const int q = (tri * tri) >> 8;
                int v = (q * q) >> 8;
                v = (v * 3 + q) >> 2; // a little fuller than the pure quartic
                v += off[i & 7];
                if (v < 0) {
                    v = 0;
                } else if (v > 255) {
                    v = 255;
                }
                dst[i] = static_cast<uint8_t>(v);
            }
            for (int i = PROF_N; i < PROF_STRIDE; i++) {
                dst[i] = 0; // pad, so a cursor one step past the end is dark
            }
        }
        tablesValid = true;
    }
    memcpy(lastP, p, 4);

    const int cy = h / 2;
    const int halfH = h > 2 ? h / 2 : 1;
    // Length: how far up and down the panel the highlight reaches before it
    // tapers away. 90 is a short streak across the middle, 490 runs off both
    // ends of the panel.
    const int lenQ8 = 90 + static_cast<int>(p[1]) * 400 / 100;
    // Half width of the highlight at its fullest, in pixels.
    int hwMax = w * (3 + static_cast<int>(p[2]) * 17 / 100) / 100;
    if (hwMax < 2) {
        hwMax = 2;
    }
    const int bright = 128 + static_cast<int>(p[3]) * 128 / 100;

    const float t = static_cast<float>(tMs) * 0.001f * speedMul(p[0]);
    // The sweep is a slow swing about the centre rather than a loop across and
    // back around. A looping sweep has to travel well past both edges so the
    // wrap is invisible, and that leaves the panel completely black for more
    // than half the cycle. A swing of a little under half the panel width keeps
    // some part of the highlight in view at all times and eases at the turns,
    // which is also what a reflection does.
    const float sweep = 0.5f * static_cast<float>(w) + fastSinRad(t * 0.24f) * 0.46f * static_cast<float>(w);
    // Tilt and bow drift on their own slow clocks, so no two passes of the
    // highlight lie on the same line.
    // Everything about the highlight's position is carried in sixteenths of a
    // pixel. Rounding it to whole pixels per row is visible: the left edge and
    // the profile's phase both step row to row, and the result is a streak
    // combed with horizontal lines rather than a smooth one.
    const int tiltQ4 = static_cast<int>(fastSinRad(t * 0.07f) * static_cast<float>(w) * 0.30f * 16.0f);
    const int bowQ4 =
        static_cast<int>((0.30f + 0.70f * fastCosRad(t * 0.045f)) * static_cast<float>(w) * 0.15f * 16.0f);
    const int sweepQ4 = static_cast<int>(sweep * 16.0f);
    const int hwMaxQ4 = hwMax * 16;

    for (int y = 0; y < h; y++) {
        uint16_t *rec = rowRec + static_cast<size_t>(y) * REC;
        // u runs -256 at the top of the panel to +256 at the bottom.
        int u = (y - cy) * 256 / halfH;
        if (u < -256) {
            u = -256;
        } else if (u > 256) {
            u = 256;
        }
        const int uu = (u * u) >> 8; // 0 at the middle, 256 at either end
        // Taper along the length: a parabola in u scaled by the length knob,
        // clamped at zero, so the highlight ends rather than wrapping.
        const int v = u * 256 / lenQ8;
        const int taper = 256 - ((v * v) >> 8);
        if (taper <= 0) {
            rec[0] = 1;
            rec[1] = 0;
            rec[2] = 1;
            rec[3] = 0;
            rec[4] = 0;
            continue;
        }
        const int A = bright * taper >> 8;
        int hwQ4 = hwMaxQ4 * taper >> 8;
        if (hwQ4 < 32) {
            hwQ4 = 32; // two pixels, which also keeps K inside a uint16
        }
        // The highlight's centre: the sweep, a tilt linear in u and a bow
        // quadratic in u. The bow is what makes it a curve rather than a line.
        // Divisions rather than shifts: u is signed here, and a right shift of
        // a negative value is implementation defined in C++17.
        const int cQ4 = sweepQ4 + (tiltQ4 * u / 256) + (bowQ4 * uu / 256);
        const int spanQ4 = 2 * hwQ4;
        const int lFullQ4 = cQ4 - hwQ4;
        const int rightQ4 = lFullQ4 + spanQ4;
        if (rightQ4 <= 0) {
            rec[0] = 1;
            rec[1] = 0;
            rec[2] = 1;
            rec[3] = 0;
            rec[4] = 0;
            continue;
        }
        const int K = 65280 * 16 / spanQ4;
        int x0 = 0;
        if (lFullQ4 > 0) {
            x0 = (lFullQ4 + 15) >> 4;
        }
        int x1 = rightQ4 >> 4;
        if (x1 > w) {
            x1 = w;
        }
        if (x1 <= x0) {
            rec[0] = 1;
            rec[1] = 0;
            rec[2] = 1;
            rec[3] = 0;
            rec[4] = 0;
            continue;
        }
        const int acc0 = ((x0 << 4) - lFullQ4) * K / 16;
        // Hold the cursor inside the profile's useful range by ending the run
        // early if the arithmetic would walk it past the last entry. The eight
        // zero entries of pad after the profile cover the rounding.
        const int maxSteps = (65535 - acc0) / K;
        if (x1 > x0 + maxSteps + 1) {
            x1 = x0 + maxSteps + 1;
        }
        rec[0] = static_cast<uint16_t>(x0);
        rec[1] = static_cast<uint16_t>(x1);
        rec[2] = static_cast<uint16_t>(K);
        rec[3] = static_cast<uint16_t>(acc0);
        rec[4] = static_cast<uint16_t>(A);
    }
}

// Portable on every target, so the descriptor's bandRef slot is nullptr, which
// is what BgAnim.h asks for when there is no second path to compare against.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint16_t bg = palette[0];
    for (int y = y0; y < y0 + rows; y++) {
        uint16_t *row = dst + static_cast<size_t>(y - y0) * w;
        const uint16_t *rec = rowRec + static_cast<size_t>(y) * REC;
        const int x0 = rec[0];
        const int x1 = rec[1];
        if (x1 <= x0) {
            for (int x = 0; x < w; x++) {
                row[x] = bg;
            }
            continue;
        }
        const uint8_t *prof = profPh + static_cast<size_t>(y & 7) * PROF_STRIDE;
        const int K = rec[2];
        const int A = rec[4];
        int acc = rec[3];
        for (int x = 0; x < x0; x++) {
            row[x] = bg;
        }
        for (int x = x0; x < x1; x++) {
            row[x] = palette[(prof[acc >> 8] * A) >> 8];
            acc += K;
        }
        for (int x = x1; x < w; x++) {
            row[x] = bg;
        }
    }
}

void release() {
    releaseTable(ramp, 256 * sizeof(uint16_t));
    releaseTable(rowRec, static_cast<size_t>(REC * allocH) * sizeof(uint16_t));
    releaseTable(profPh, static_cast<size_t>(8 * PROF_STRIDE));
    releaseTable(palette, 256 * sizeof(uint16_t));
    allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
    tablesValid = false;
    lastP[0] = lastP[1] = lastP[2] = lastP[3] = 255;
}

} // namespace

// Index bound, which is why the run loop needs no clamp. The profile entries
// are clamped to 0..255 as they are built, A is at most 256, so the product is
// at most 65,280 and the palette index at most 255. The cursor is bounded by
// the maxSteps clamp above: acc never exceeds 65,535, so acc >> 8 never
// exceeds 255, and the eight zero entries past PROF_N cover the rounding.
extern const BgAnimation bg_anim_glint;
const BgAnimation bg_anim_glint = {
    "glint",
    "Glint",
    {{"speed", "Speed", 10},
     {"length", "Length", 35},
     {"width", "Width", 45},
     {"brightness", "Brightness", 55}},
    init,
    frame,
    band,
    release,
    nullptr,
};

#endif // GAGGIMATE_SIM
