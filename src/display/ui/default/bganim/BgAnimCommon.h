#ifndef BGANIM_COMMON_H
#define BGANIM_COMMON_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

// Shared helpers for background animations. Everything here is safe to call
// from the render task (core 1); allocations prefer internal SRAM and fall
// back to PSRAM (never fail hard — the caller checks for nullptr).

namespace bganim {

constexpr int SIN_N = 1024; // entries in the shared sine LUT
constexpr int SIN_AMP = 512;

// Shared 1024-entry sine LUT, amplitude ±512. Built on first use.
const int16_t *sinLut();
inline int16_t sin1024(uint32_t idx) { return sinLut()[idx & (SIN_N - 1)]; }

void *alloc(size_t size); // prefer internal SRAM, fall back to PSRAM

inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Saturating additive blend for glow sprites (per-channel clamp).
inline uint16_t add565(uint16_t a, uint16_t b) {
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
inline uint32_t nextRand(uint32_t &s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
inline float nextRandf(uint32_t &s) { return (nextRand(s) >> 8) * (1.0f / 16777216.0f); }

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

inline uint8_t clamp8f(float v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }

// 256-entry float cosine table (built on first use) + radian-indexed helpers.
// Truncation toward zero keeps negative radians valid via the & 255 wrap.
const float *cosTableF();
inline float fastCosRad(float rad) { return cosTableF()[static_cast<int>(rad * (256.0f / 6.2831853f)) & 255]; }
inline float fastSinRad(float rad) { return fastCosRad(rad - 1.5707963f); }

// 4x4 ordered dither matrix, values 0..15.
extern const uint8_t BAYER4[16];
// 8x8 ordered dither matrix, values 0..63.
extern const uint8_t BAYER8[64];

// Alpha blend fg over bg, alpha Q8 (0..256).
inline uint16_t blendQ8(uint16_t bg, uint16_t fg, int aQ8) {
    const int br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    const int fr = (fg >> 11) & 0x1F, fgc = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    const int r = br + (((fr - br) * aQ8) >> 8);
    const int g = bgc + (((fgc - bgc) * aQ8) >> 8);
    const int b = bb + (((fb - bb) * aQ8) >> 8);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Saturating additive blend of an 8-bit RGB source scaled by 8-bit alpha
// (glow sprites: fireflies, steam, star streaks).
inline uint16_t addScaled565(uint16_t dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
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
inline float speedMul(uint8_t sp) {
    // exp2f((sp-50)/18.2) => 0.15 .. 6.7, exactly 1.0 at 50
    return exp2f((static_cast<int>(sp) - 50) * (1.0f / 18.2f));
}

} // namespace bganim

#endif // BGANIM_COMMON_H
