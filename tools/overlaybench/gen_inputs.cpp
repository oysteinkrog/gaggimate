#include "gen_inputs.h"
#include "kernels/span_scan.h"
#include <cstring>

namespace ovb {
namespace {

// Fixed, non-libc PRNG so golden files are reproducible across hosts/libc
// versions (std::rand's sequence is implementation-defined).
struct SplitMix32 {
    uint32_t state;
    explicit SplitMix32(uint32_t seed) : state(seed) {}
    uint32_t next() {
        uint32_t z = (state += 0x9E3779B9u);
        z = (z ^ (z >> 16)) * 0x21f0aaadu;
        z = (z ^ (z >> 15)) * 0x735a2d97u;
        return z ^ (z >> 15);
    }
    int range(int lo, int hiExclusive) { return lo + static_cast<int>(next() % static_cast<uint32_t>(hiExclusive - lo)); }
};

void setPx(SyntheticOverlay &ov, int panelX, int panelY, uint16_t color, uint8_t alpha) {
    const int x = panelX + OVERLAY_EXT_MARGIN;
    const int y = panelY + OVERLAY_EXT_MARGIN;
    if (x < 0 || x >= ov.w || y < 0 || y >= ov.h) {
        return;
    }
    uint8_t *p = ov.px.data() + (static_cast<size_t>(y) * ov.w + x) * 3;
    p[0] = static_cast<uint8_t>(color & 0xFF);
    p[1] = static_cast<uint8_t>((color >> 8) & 0xFF);
    p[2] = alpha;
}

// A filled disc with a soft (2px) antialiased ring, like a clock face or a
// round icon glyph -- the "dial sector" content the mission brief calls out.
void drawDisc(SyntheticOverlay &ov, int cx, int cy, int r, uint16_t color) {
    for (int y = -r - 2; y <= r + 2; y++) {
        for (int x = -r - 2; x <= r + 2; x++) {
            const int d2 = x * x + y * y;
            const int rr = r * r;
            if (d2 > (r + 2) * (r + 2)) {
                continue;
            }
            uint8_t a;
            if (d2 <= (r - 2) * (r - 2)) {
                a = 255;
            } else if (d2 > rr) {
                // Outer AA falloff over ~2px.
                const int over = static_cast<int>((static_cast<long long>(d2) - rr));
                a = over > 600 ? 0 : static_cast<uint8_t>(255 - (over * 255) / 600);
            } else {
                a = 255;
            }
            if (a == 0) {
                continue;
            }
            setPx(ov, cx + x, cy + y, color, a);
        }
    }
}

// A small solid rectangle with a 1px antialiased border, like a status-bar
// icon (battery, wifi, droplet).
void drawIconRect(SyntheticOverlay &ov, int x0, int y0, int w, int h, uint16_t color) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
            setPx(ov, x0 + x, y0 + y, color, edge ? 140 : 255);
        }
    }
}

// One line of "text": a run of glyph-shaped blocks with antialiased left/
// right edges and a ragged top/bottom (real glyphs do not fill their cell),
// separated by PRNG-varied gaps -- some inside RUN_GAP_MERGE (4px, so they
// get swallowed into one run) and some outside it (so they stay separate
// runs), matching the comment in SleepAnimation.cpp about antialiased text
// being "full of one- and two-pixel gaps".
void drawTextLine(SyntheticOverlay &ov, int x0, int y0, int lineW, int lineH, uint16_t color, SplitMix32 &rng) {
    int x = x0;
    const int xEnd = x0 + lineW;
    while (x < xEnd) {
        const int glyphW = rng.range(3, 15);
        if (x + glyphW > xEnd) {
            break;
        }
        // Ragged top/bottom: a random inset per glyph, like ascenders/
        // descenders and x-height variation.
        const int topInset = rng.range(0, 3);
        const int botInset = rng.range(0, 3);
        for (int gy = 0; gy < lineH; gy++) {
            if (gy < topInset || gy >= lineH - botInset) {
                continue;
            }
            for (int gx = 0; gx < glyphW; gx++) {
                const bool leftEdge = gx == 0;
                const bool rightEdge = gx == glyphW - 1;
                uint8_t a = 220;
                if (leftEdge || rightEdge) {
                    a = static_cast<uint8_t>(60 + rng.range(0, 120));
                }
                setPx(ov, x + gx, y0 + gy, color, a);
            }
        }
        x += glyphW;
        // Gap: ~40% narrow enough to be swallowed by RUN_GAP_MERGE (<=4),
        // ~60% a real word/letter gap.
        x += (rng.next() & 3) == 0 ? rng.range(1, 4) : rng.range(5, 12);
    }
}

} // namespace

SyntheticOverlay genSyntheticOverlay(uint32_t seed) {
    SyntheticOverlay ov;
    ov.w = PANEL_W + 2 * OVERLAY_EXT_MARGIN;
    ov.h = PANEL_H + 2 * OVERLAY_EXT_MARGIN;
    ov.px.assign(static_cast<size_t>(ov.w) * ov.h * 3, 0);

    SplitMix32 rng(seed);

    // A large clock-style dial, roughly centered-upper, like the default UI's
    // temperature dial.
    drawDisc(ov, PANEL_W / 2, 150, 118, rgb565_ref(235, 235, 240));
    // Two smaller status icons in a corner, like the wifi/heat icons.
    drawIconRect(ov, 24, 24, 28, 20, rgb565_ref(80, 180, 240));
    drawIconRect(ov, 64, 24, 20, 20, rgb565_ref(240, 120, 60));
    // A small secondary dial (e.g. a gauge widget) lower on the screen.
    drawDisc(ov, 380, 400, 46, rgb565_ref(200, 220, 200));

    // Several lines of "text" (status strings, numeric readouts) scattered
    // down the panel, each a different width/position so per-row run counts
    // vary the way a real screen's varies between a title, a value, and a
    // unit label.
    const uint16_t textColor = rgb565_ref(20, 20, 24);
    drawTextLine(ov, 60, 300, 360, 22, textColor, rng);
    drawTextLine(ov, 100, 330, 280, 16, textColor, rng);
    drawTextLine(ov, 40, 430, 400, 18, textColor, rng);
    drawTextLine(ov, 150, 460, 180, 14, textColor, rng);
    // A denser cluster near the top, worst-case-ish for run count (the
    // comment in SleepAnimation.cpp says "a row through the clock and both
    // icons emits about eight" -- push a bit past that here).
    drawTextLine(ov, 20, 60, 440, 20, textColor, rng);

    // Sparse isolated antialiased dots (e.g. a progress indicator's dots),
    // to exercise short one-run spans far from everything else.
    for (int i = 0; i < 24; i++) {
        const int x = rng.range(0, PANEL_W);
        const int y = rng.range(200, 480);
        setPx(ov, x, y, rgb565_ref(255, 255, 255), static_cast<uint8_t>(rng.range(30, 256)));
    }

    return ov;
}

std::vector<uint8_t> deriveScrimSrc(const SyntheticOverlay &ov) {
    std::vector<uint8_t> scrimSrc(static_cast<size_t>(SCRIM_W) * SCRIM_H, 0);
    uint32_t scratchRuns[RUNS_PER_ROW];
    for (int y = 0; y < PANEL_H; y++) {
        uint8_t *cellRow = scrimSrc.data() + static_cast<size_t>(y >> SCRIM_SHIFT) * SCRIM_W;
        scanRow_ref(ov.panelRowAlpha(y), PANEL_W, scratchRuns, cellRow);
    }
    return scrimSrc;
}

std::vector<uint16_t> genSyntheticHalfRes(uint32_t seed) {
    constexpr int HW = PANEL_W / 2;
    constexpr int HH = PANEL_H / 2;
    std::vector<uint16_t> out(static_cast<size_t>(HW) * HH);
    SplitMix32 rng(seed);
    for (int y = 0; y < HH; y++) {
        for (int x = 0; x < HW; x++) {
            // Diagonal low-frequency bands (plasma-ish) plus a little
            // per-pixel dither so packed 32-bit stores see varied bit
            // patterns rather than a run the compiler could fold away.
            const int band = ((x * 3 + y * 5) & 0xFF);
            const int dither = static_cast<int>(rng.next() & 3) - 1;
            const int v = band + dither;
            const uint8_t r = static_cast<uint8_t>(v);
            const uint8_t g = static_cast<uint8_t>(255 - v);
            const uint8_t b = static_cast<uint8_t>((v * 2) & 0xFF);
            out[static_cast<size_t>(y) * HW + x] = rgb565_ref(r, g, b);
        }
    }
    return out;
}

} // namespace ovb
