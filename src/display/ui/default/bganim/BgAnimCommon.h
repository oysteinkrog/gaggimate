#ifndef BGANIM_COMMON_H
#define BGANIM_COMMON_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

// Shared helpers for background animations. Everything here is safe to call
// from the render task (core 1); allocations prefer internal SRAM and fall
// back to PSRAM (never fail hard — the caller checks for nullptr).

namespace bganim {

// The per-pixel helpers below are called from inner loops that run 230,400
// times a frame, so they must actually be inlined -- and plain `inline` is a
// hint GCC was declining. Disassembly of the -O2 objects showed real callx8
// calls to clamp8f (starfield) and blendQ8 (orbits) from inside band(),
// paying a windowed-ABI register rotation for three operations' worth of
// work. always_inline is a directive, not a hint, so it holds.
#define BGANIM_INLINE inline __attribute__((always_inline))


constexpr int SIN_N = 1024; // entries in the shared sine LUT
constexpr int SIN_AMP = 512;

// Shared 1024-entry sine LUT, amplitude ±512. Built on first use.
const int16_t *sinLut();
BGANIM_INLINE int16_t sin1024(uint32_t idx) { return sinLut()[idx & (SIN_N - 1)]; }

// Buffers up to this size prefer internal SRAM (latency matters for small,
// randomly-indexed LUTs); larger ones go to PSRAM first so an animation's
// bulk tables cannot starve WiFi/BLE, which share the SRAM pool. See alloc().
#ifndef GM_BGANIM_SRAM_LIMIT
#define GM_BGANIM_SRAM_LIMIT 8192
#endif
constexpr size_t SRAM_ALLOC_LIMIT = GM_BGANIM_SRAM_LIMIT;

// Ceiling on the TOTAL internal SRAM alloc() will ever hand out, across every
// animation, for the life of the boot. The per-allocation limit above bounds
// one table; this bounds the sum, which is what actually ran the pool dry --
// the tables are never freed, so switching through the fleet accumulated 53 KB
// and took the network stack down with it (see the analysis in alloc()).
//
// 24 KB keeps roughly 45 KB of the pool free for what WiFi, BLE and TLS
// allocate at runtime, which is the point. What it does NOT do is give the
// running animation its tables -- there is no budget value that would, and an
// earlier version of this comment claimed otherwise. The real behaviour, walked
// through the registry order and confirmed byte-for-byte against a device
// trace:
//
//   plasma    2,944 + sinLut 2,048 shared        -> 4,992,  all SRAM
//   lava      2,432 + cosTableF 1,024 shared     -> 8,448,  all SRAM
//   silk      7,176 + 4 x 3,840 rowAux           -> 23,304, last two to PSRAM
//   starfield 11,084 eligible                    -> 24,548, 6 of 10 to PSRAM
//   everything after                             -> entirely PSRAM
//
// So four animations claim the pool and the remaining nine get none of it,
// including caustics' 8,192 B rgbLUT and aurora's 8,704 B of sine tables, both
// individually well under the limit. Which four is decided by position in the
// registry, not by how hot the tables are. Silk alone asks for 22,536 B, 92% of
// this ceiling, and does not fully fit even when it runs first from a cold boot.
//
// The mechanism that would actually fix this is a release entry point on the
// BgAnimation ABI, freeing the outgoing animation's tables on a switch, so the
// peak is max-over-animations (silk's 22.5 KB) instead of sum-over-animations.
// Six of the thirteen would be mechanical; the rest gate their table CONTENT on
// separate theme/param sentinels that a naive free-and-null would leave stale,
// handing back a reallocated buffer that never gets refilled. Mandala is worse
// than stale content: its fill flag is function-local and unreachable from
// outside, and its rescaleLUT pointer is offset into the allocation, so freeing
// the pointer the code holds would corrupt the heap.
//
// Until that exists, this ceiling is a safety limit, not a placement policy.
#ifndef GM_BGANIM_SRAM_BUDGET
#define GM_BGANIM_SRAM_BUDGET (24 * 1024)
#endif
constexpr size_t SRAM_TOTAL_BUDGET = GM_BGANIM_SRAM_BUDGET;

// Bytes alloc() has handed out from each pool since boot, so a bench run can
// tell whether an animation's tables actually landed where the policy above
// intends. Not synchronised: written on the render task at init, read over
// HTTP, and a torn 32-bit read here would only misreport a diagnostic.
extern size_t g_allocSram;
extern size_t g_allocPsram;

void *alloc(size_t size); // see SRAM_ALLOC_LIMIT for the placement policy

BGANIM_INLINE uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Saturating additive blend for glow sprites (per-channel clamp).
BGANIM_INLINE uint16_t add565(uint16_t a, uint16_t b) {
    uint32_t r = ((a >> 11) & 0x1F) + ((b >> 11) & 0x1F);
    uint32_t g = ((a >> 5) & 0x3F) + ((b >> 5) & 0x3F);
    uint32_t bl = (a & 0x1F) + (b & 0x1F);
    if (r > 0x1F)
        r = 0x1F;
    if (g > 0x3F)
        g = 0x3F;
    if (bl > 0x1F)
        bl = 0x1F;
    return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

// Deterministic PRNG (xorshift32) so device and web preview can match.
BGANIM_INLINE uint32_t nextRand(uint32_t &s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
BGANIM_INLINE float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }

BGANIM_INLINE float lerpf(float a, float b, float t) { return a + (b - a) * t; }

BGANIM_INLINE uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }

// 256-entry float cosine table (built on first use) + radian-indexed helpers.
// Truncation toward zero keeps negative radians valid via the & 255 wrap.
//
// The int64_t step is load-bearing, not decoration. Callers pass angles built
// from tMs, which reaches 4.3e9 before it wraps, so the scaled argument
// exceeds INT_MAX after a few days of uptime — and a float-to-int conversion
// that overflows is undefined, so the & 255 would be masking a value the
// conversion was never required to produce. int64_t covers every reachable
// argument by a wide margin (the largest frequency any animation uses keeps
// the product well under 1e12). Every call site is in frame(), a few hundred
// times per frame at most, so the wider conversion costs nothing measurable;
// do NOT copy this into a per-pixel loop.
const float *cosTableF();
BGANIM_INLINE float fastCosRad(float rad) {
    return cosTableF()[static_cast<int>(static_cast<int64_t>(rad * (256.0f / 6.2831853f)) & 255)];
}
BGANIM_INLINE float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }

// 4x4 ordered dither matrix, values 0..15.
extern const uint8_t BAYER4[16];
// 8x8 ordered dither matrix, values 0..63.
extern const uint8_t BAYER8[64];

// Alpha blend fg over bg, alpha Q8 (0..256).
BGANIM_INLINE uint16_t blendQ8(uint16_t bg, uint16_t fg, int aQ8) {
    const int br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    const int fr = (fg >> 11) & 0x1F, fgc = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    const int r = br + (((fr - br) * aQ8) >> 8);
    const int g = bgc + (((fgc - bgc) * aQ8) >> 8);
    const int b = bb + (((fb - bb) * aQ8) >> 8);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Saturating additive blend of an 8-bit RGB source scaled by 8-bit alpha
// (glow sprites: fireflies, steam, star streaks).
BGANIM_INLINE uint16_t addScaled565(uint16_t dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int dr = ((dst >> 11) & 0x1F) + ((r * a) >> 11);
    int dg = ((dst >> 5) & 0x3F) + ((g * a) >> 10);
    int db = (dst & 0x1F) + ((b * a) >> 11);
    if (dr > 0x1F)
        dr = 0x1F;
    if (dg > 0x3F)
        dg = 0x3F;
    if (db > 0x1F)
        db = 0x1F;
    return static_cast<uint16_t>((dr << 11) | (dg << 5) | db);
}

// Builds a 256-entry RGB565 palette by interpolating RGB keyframes around a
// wheel, scaled by brightness (0-256 = 0-100%).
void buildPalette(uint16_t *out, const uint8_t (*keys)[3], int nKeys, uint16_t brightness256);

// Shared 256x256 tileable value-noise texture (64KB, built on first use from
// a 16x16 lattice with quintic-smoothstep bilinear upsampling; periodic, so
// sampling with `& 255` never shows a seam). Ember and Nebula both read it at
// different scroll offsets/scales, so one asset serves both. Returns nullptr
// if the allocation failed.
const uint8_t *noiseTex256();

// ---- active color theme (see BgAnim.h for the theme model) ---------------
// Written by the UI task on settings change, read by the render task. Writes
// are double-buffered behind an atomic generation counter; animations poll
// themeGen() in frame() and rebuild their palettes when it changes.
void setThemeStops(const uint8_t (*stops)[3], int nStops);
uint32_t themeGen();
int themeStopCount();
const uint8_t (*themeStops())[3];

// RGB888 sample of the active theme gradient, pos 0 (darkest) .. 255.
void themeRGB(int pos, uint8_t out[3]);
// 256-entry RGB565 ramp across the active theme, scaled by brightness Q8
// (0..256). reversed=true puts the brightest stop at index 0.
void buildThemeRamp(uint16_t *out, uint16_t brightness256, bool reversed = false);
// Wheel variant: the last stop blends back into stop 0 so palette-cycling
// animations (plasma) wrap without a seam.
void buildThemeWheel(uint16_t *out, uint16_t brightness256);

// Universal speed-curve: maps a 0-100 speed param to a multiplier of the
// animation's tuned base rate — 0.15x at 0, 1x at 50, ~6.7x at 100.
BGANIM_INLINE float speedMul(uint8_t sp) {
    // exp2f((sp-50)/18.2) => 0.15 .. 6.7, exactly 1.0 at 50
    return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
}

} // namespace bganim

#endif // BGANIM_COMMON_H
