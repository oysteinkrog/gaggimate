#ifndef BGANIM_COMMON_H
#define BGANIM_COMMON_H

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

// Builds a 256-entry RGB565 palette by interpolating RGB keyframes around a
// wheel, scaled by brightness (0-256 = 0-100%).
void buildPalette(uint16_t *out, const uint8_t (*keys)[3], int nKeys, uint16_t brightness256);

} // namespace bganim

#endif // BGANIM_COMMON_H
