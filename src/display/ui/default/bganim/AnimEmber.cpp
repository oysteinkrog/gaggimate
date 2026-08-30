#ifndef GAGGIMATE_SIM

// "Ember" — a warm glow breathing from below screen center, like coals in a
// hearth. Three incommensurate breathing periods (11.3s/17.7s/6.1s) so the
// pattern never visibly repeats; optional edge-of-perception flicker from the
// shared tileable noise texture. Radial field is an incremental r^2 walk (two
// adds per pixel) into a radius LUT — no sqrt in the loop.
//
// Palette is stored "padded": paletteExt has PAD clamp entries on each side
// of the real 256-entry ramp, so the per-pixel breathe/dither/flicker offset
// can be added to the radius index and used to index paletteExt directly,
// with no branch to clamp into [0,255] — the pad entries already hold the
// clamped edge colors. Range proof (worst-case params, p[1..3]=100): breathe
// in [-35,+35], dither in [-6,+6], flicker term in [-10,+9], so the combined
// index lands in [-51,+305]; PAD=64 covers that with margin.
//
// radiusLUT is padded the same way on the high side (RPAD entries repeating
// radiusLUT[255]) so `radiusLUT[ridx]` needs no >255 clamp either. Verified
// against real xtensa-esp32s3 codegen (tools/animbench/xtensa-asm.sh): the
// clamp's `movi #255` + `min` cost a spare register that forced the noiseRow
// pointer to spill to the stack and reload every pixel — removing the clamp
// dropped that reload too. Safe for this animation's fixed 480x480 target:
// max r^2 is at the farthest corner from center (g_cx=240,g_cy=260), giving
// r^2>>RSHIFT ≈ 244, well inside the 255+RPAD range with margin to spare.
//
// Breathe is frame-constant, so instead of subtracting it from every pixel
// it's folded once per row into perCol[8] alongside the dither term — the
// inner loop does one add (radiusLUT[ridx] + perCol[x&7]) instead of a
// subtract-then-add.
//
// Flicker adds noise*flickerAmp, a per-pixel int mul+shift; since flickerAmp
// is frame-constant, it's folded into a 256-entry LUT (flickerLUT) rebuilt
// only when the flicker param changes, turning the multiply into a lookup.
// The "is flicker on" test is hoisted out of the pixel loop entirely — band()
// picks one of two loop bodies (with/without noise term) once, not per pixel.
//
// combRow[x] = perCol[x&7] + flickerLUT[noiseRow[(x+g_sx)&255]], the two
// non-radius terms of rn, is precomputed once per row in a short sequential
// pass before the radius loop runs (doFlicker branch only). Integer addition
// is associative/commutative exactly, so summing these two terms first and
// adding radiusLUT[ridx] second gives the identical rn the three-term sum
// always did — this is a reassociation, not a behavior change. It matters
// because the un-reassociated per-pixel loop chained FOUR dependent loads
// (radiusLUT, perCol, noiseRow, then flickerLUT keyed off the noiseRow byte)
// into one scalar dependency chain feeding the palette lookup; the precompute
// pass turns that into loads with no cross-iteration dependency (only the
// loop counter carries), and the radius loop is left with just two
// independent loads (radiusLUT[ridx], combRow[x]) on the critical path.
// combRow is int16_t and read with a sign-extending load so the per-pixel
// path needs no separate sign-extend op — see the range proof above (rn's
// non-radius terms land in about [-51,+50], comfortably inside int16_t).
// palOff = paletteExt + PAD folds the "+PAD" constant into the base pointer
// once per row instead of adding it to rn on every pixel.
//
// Design: anim-atmosphere (Fable), 2026-08-15. Optimized: opt-ember,
// 2026-08-15; row-precomputed flicker/dither term, 2026-08-30.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

// Band-kernel code pinned to IRAM on device. The ESP32-S3's single icache
// (16 KB here) is shared by both cores, and LVGL's code footprint churns it
// from core 1 on every telemetry repaint; a flash refill for this loop then
// queues on the MSPI behind the scan-out refill's PSRAM stream. With the
// kernel's tables in SRAM (post-settle re-placement in bganim::alloc) and its
// output band buffer in SRAM, instruction fetch is the band bracket's last
// external dependency; pinning removes it. Host and sim builds compile the
// attribute away.
#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

namespace {
using namespace bganim;

// DDS phase steps: full circle = 2^32, periods 11.3s / 17.7s / 6.1s.
constexpr uint32_t STEP1 = static_cast<uint32_t>(4294967296.0 / 11300.0);
constexpr uint32_t STEP2 = static_cast<uint32_t>(4294967296.0 / 17700.0);
constexpr uint32_t STEP3 = static_cast<uint32_t>(4294967296.0 / 6100.0);
constexpr uint32_t PHOFF2 = static_cast<uint32_t>(1.7 / 6.2831853 * 4294967296.0);
constexpr uint32_t PHOFF3 = static_cast<uint32_t>(4.2 / 6.2831853 * 4294967296.0);
constexpr int RSHIFT = 9; // r^2 -> radiusLUT bucket

// Padding either side of the 256-entry palette ramp (see file header proof).
constexpr int PAD = 64;
constexpr int PAL_EXT_N = 256 + 2 * PAD;

// High-side padding on radiusLUT so ridx never needs a >255 clamp (see
// file header proof — safe margin for the fixed 480x480 target).
constexpr int RPAD = 64;
constexpr int RLUT_N = 256 + RPAD;

uint16_t *paletteExt = nullptr; // [PAL_EXT_N]; real ramp lives at paletteExt+PAD
uint16_t *palette = nullptr;    // = paletteExt + PAD, 256 entries, reversed theme ramp
uint8_t *radiusLUT = nullptr;   // [RLUT_N]; r^2>>RSHIFT -> normalized radius byte
int8_t *flickerLUT = nullptr;   // noise byte -> signed flicker contribution
const uint8_t *noise = nullptr;
int16_t *combRow = nullptr; // [allocW]; perCol+flicker combined, rebuilt per row (see file header)
int allocW = 0;             // width combRow was sized for; release() needs it back
uint32_t lastThemeGen = 0xFFFFFFFF;
uint8_t lastGlow = 255;
uint8_t lastFlickerParam = 255;
int g_cx = 240, g_cy = 260;
float g_maxR = 353.7f;
int g_breathe = 0, g_flickerAmp = 0, g_sx = 0, g_sy = 0;

// The ramp is reversed, so index 0 is the brightest stop and the glow's core
// lands squarely on it — in the middle of the screen, which is where the UI
// puts its readouts. Starting the ramp part way in keeps the hearth gradient
// and its falloff but takes the peak off the text. Purely a shift of where the
// curve begins: clamp8f still bounds the result to [0,255], so the index range
// the palette padding is sized for (see file header) is unchanged.
constexpr int CORE_FLOOR = 72;

void buildRadiusLut(uint8_t glow) {
    const float glowGain = 0.55f + 0.014f * glow;
    const float scale = 255.0f / (g_maxR * glowGain);
    for (int i = 0; i < 256; i++) {
        const float r = sqrtf(static_cast<float>(i << RSHIFT));
        radiusLUT[i] = clamp8f(CORE_FLOOR + r * scale);
    }
    // Pad entries repeat the outermost (fully-clamped) value so an
    // unclamped ridx past 255 (shouldn't happen on the real target, see
    // file header) still reads a sane color instead of the flicker LUT.
    for (int i = 256; i < RLUT_N; i++) {
        radiusLUT[i] = radiusLUT[255];
    }
}

// Fills the clamp padding around the freshly-rebuilt 256-entry ramp so
// paletteExt[PAD + rn] is valid for rn in [-PAD, 255+PAD] with no branch.
void extendPalette() {
    const uint16_t lo = palette[0];
    const uint16_t hi = palette[255];
    for (int i = 0; i < PAD; i++) {
        paletteExt[i] = lo;
        paletteExt[PAD + 256 + i] = hi;
    }
}

void buildFlickerLut(int flickerAmp) {
    for (int i = 0; i < 256; i++) {
        flickerLUT[i] = static_cast<int8_t>(((i - 128) * flickerAmp) >> 7);
    }
}

bool init(int w, int h) {
    if (paletteExt == nullptr) {
        paletteExt = static_cast<uint16_t *>(alloc(PAL_EXT_N * sizeof(uint16_t)));
        radiusLUT = static_cast<uint8_t *>(alloc(RLUT_N));
        flickerLUT = static_cast<int8_t *>(alloc(256));
        noise = noiseTex256();
        combRow = static_cast<int16_t *>(alloc(static_cast<size_t>(w) * sizeof(int16_t)));
        allocW = w;
        if (paletteExt == nullptr || radiusLUT == nullptr || flickerLUT == nullptr || noise == nullptr ||
            combRow == nullptr) {
            return false;
        }
        palette = paletteExt + PAD;
    }
    g_cx = w / 2;
    g_cy = h / 2 + (20 * h) / 480;
    const float dx = static_cast<float>(g_cx);
    const float dy = static_cast<float>(g_cy > h - g_cy ? g_cy : h - g_cy);
    g_maxR = sqrtf(dx * dx + dy * dy);
    lastThemeGen = 0xFFFFFFFF;
    lastGlow = 255;
    lastFlickerParam = 255;
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildThemeRamp(palette, 256, /*reversed=*/true); // brightest stop at the core
        extendPalette();
        lastThemeGen = themeGen();
    }
    if (p[1] != lastGlow) {
        buildRadiusLut(p[1]);
        lastGlow = p[1];
    }
    const float spd = speedMul(p[0]);
    // Speed scales virtual time; a param change causes one phase jump, which
    // the slow breathing envelope absorbs invisibly.
    // Virtual time is deliberately modular (every use below is a shift-and-mask
    // into a 1024-entry sine table), but tMs * spd reaches ~2.9e10 at the top
    // of the speed range before tMs wraps, and converting a float that large
    // straight to uint32_t is undefined rather than wrapping. Go through double
    // (float's 24-bit mantissa cannot hold tMs near its wrap anyway) and then
    // int64_t, where the conversion is defined, and let integer-to-unsigned do
    // the modular reduction.
    const uint32_t vt = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(tMs) * spd));
    const float pulseGain = p[3] / 100.0f;
    const float s1 = sin1024((vt * STEP1) >> 22) * (1.0f / SIN_AMP);
    const float s2 = sin1024(((vt * STEP2) + PHOFF2) >> 22) * (1.0f / SIN_AMP);
    const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
    g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
    g_flickerAmp = (10 * p[2]) / 100;
    if (p[2] != lastFlickerParam) {
        buildFlickerLut(g_flickerAmp);
        lastFlickerParam = p[2];
    }
    g_sx = static_cast<int>((vt * 6u) >> 10) & 255;
    g_sy = static_cast<int>((vt * 4u) >> 10) & 255;
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const bool doFlicker = g_flickerAmp != 0;
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        const int dy = y - g_cy;
        const int dy2 = dy * dy;
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
        // perCol combines the dither term with the (frame-constant) breathe
        // offset once per row, so the pixel loop does a single add instead
        // of a subtract-then-add — see file header.
        int8_t perCol[8];
        for (int k = 0; k < 8; k++) {
            perCol[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 5 - g_breathe);
        }
        int dx = -g_cx;
        int r2 = dx * dx + dy2;
        int ddx = 2 * dx + 1; // r2 delta for this step; += 2 per pixel thereafter

        // PAD folded into the base pointer once per row instead of into rn on
        // every pixel (see file header) -- valid because PAD is compile-time
        // constant and paletteExt[PAD + rn] == (paletteExt + PAD)[rn] exactly.
        const uint16_t *palOff = paletteExt + PAD;

        if (doFlicker) {
            const uint8_t *noiseRow = noise + ((y + g_sy) & 255) * 256;
            // Precompute the two non-radius terms of rn once per row (see file
            // header): no cross-iteration dependency here, unlike the radius
            // loop below, so this pass is just independent loads+adds+stores.
            for (int x = 0; x < w; x++) {
                combRow[x] = static_cast<int16_t>(perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]]);
            }
            // 2x unrolled, paired into one 32-bit store (dst is 4-byte
            // aligned and w is even -- see xtensa-asm addendum, and matches
            // the existing convention in AnimAurora.cpp's emitPair). Two
            // independent pixel computations sit between each loop branch
            // instead of one, giving the in-order core two non-dependent
            // load chains to interleave while a radiusLUT/palette load from
            // one pixel is still in flight for the other -- the unroll's
            // actual payoff is hiding load latency, not the halved store
            // count. Xtensa is little-endian, so the first pixel of the pair
            // is the low halfword.
            int x = 0;
            for (; x + 1 < w; x += 2) {
                const int ridx0 = r2 >> RSHIFT;
                const uint16_t c0 = palOff[radiusLUT[ridx0] + combRow[x]];
                r2 += ddx;
                ddx += 2;
                const int ridx1 = r2 >> RSHIFT;
                const uint16_t c1 = palOff[radiusLUT[ridx1] + combRow[x + 1]];
                r2 += ddx;
                ddx += 2;
                *reinterpret_cast<uint32_t *>(row + x) =
                    static_cast<uint32_t>(c0) | (static_cast<uint32_t>(c1) << 16);
            }
            if (x < w) { // odd leftover (w is 480 on the real target; kept for contract generality)
                const int ridx = r2 >> RSHIFT;
                row[x] = palOff[radiusLUT[ridx] + combRow[x]];
                r2 += ddx;
                ddx += 2;
            }
        } else {
            for (int x = 0; x < w; x++) {
                const int ridx = r2 >> RSHIFT;
                row[x] = palOff[radiusLUT[ridx] + perCol[x & 7]];
                r2 += ddx;
                ddx += 2;
            }
        }
    }
}

void release() {
    releaseTable(paletteExt, PAL_EXT_N * sizeof(uint16_t));
    // An alias into paletteExt (paletteExt + PAD), not its own allocation —
    // handing it to free() would be heap corruption. Just drop it.
    palette = nullptr;
    releaseTable(radiusLUT, RLUT_N);
    releaseTable(flickerLUT, 256);
    releaseTable(combRow, static_cast<size_t>(allocW) * sizeof(int16_t));
    // Borrowed: noiseTex256() is a 64 KB fleet-wide asset owned by
    // BgAnimCommon and shared with nebula. Dropping the pointer is all this
    // animation is entitled to do.
    noise = nullptr;
    // init() reinstates these unconditionally, but reset them here too so the
    // freed-and-nulled state is self-consistent: leaving a live sentinel next
    // to a null table is the failure mode this whole entry point exists to
    // avoid (see BgAnimCommon.h).
    lastThemeGen = 0xFFFFFFFF;
    lastGlow = 255;
    lastFlickerParam = 255;
}

} // namespace

extern const BgAnimation bg_anim_ember;
const BgAnimation bg_anim_ember = {
    "ember",
    "Ember",
    {{"speed", "Speed", 50}, {"glow", "Glow size", 45}, {"flicker", "Flicker", 20}, {"pulse", "Pulse", 50}},
    init,
    frame,
    band,
    release,
};

#endif // GAGGIMATE_SIM
