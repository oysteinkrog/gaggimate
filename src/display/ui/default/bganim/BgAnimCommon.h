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
// All thirteen animations now declare release() on the BgAnimation ABI, and
// SleepAnimation frees the outgoing animation's tables before the incoming one
// allocates. That changes what this number has to cover: the peak is
// max-over-animations, not sum-over-animations, so the ceiling is a placement
// policy -- it decides where the running animation's tables live -- rather than
// the bare safety limit it used to be, when four animations claimed the pool by
// position in the registry and the remaining nine got none of it.
//
// The peak has two terms. Some tables are shared fleet-wide, owned by
// BgAnimCommon, and deliberately outlive any single animation, so they are
// charged here permanently:
//
//   sinLut     2,048  shared by plasma, silk, caustics, aurora, ember, ...
//   cosTableF  1,024  shared by ripples, starfield, aurora
//   ---------------
//              3,072  never released (correctly -- see release() below)
//
// noiseTex256's 65,536 B is shared too but exceeds SRAM_ALLOC_LIMIT, so it goes
// to PSRAM and is not charged here. The second term is the largest single
// animation, which is silk:
//
//   g_lut     7,176  LUT_N x 2
//   rowAux   15,360  4 dither phases x 480 x 8
//   ---------------
//            22,536
//
// so the peak is 3,072 + 22,536 = 25,608 B. Nothing else comes close: the next
// largest SRAM-eligible sets are starfield's 11,084, caustics' 9,744 and
// aurora's 8,704, all of which now fit with room to spare.
//
// 28 KB covers the peak with 3,064 B of headroom and still leaves roughly 41 KB
// of the pool for what WiFi, BLE and TLS allocate at runtime -- about half the
// 53 KB at which the network stack died (see alloc()). The headroom is not
// slack: at the previous 24 KB ceiling, measured, silk got 18,696 B in SRAM and
// its FOURTH rowAux -- exactly 3,840 B -- spilled to PSRAM, and ra[x].dx2 /
// ra[x].dith are read PER PIXEL, so one row in four paid bus latency on every
// pixel to save 2 KB of a pool with 45 KB free. A ceiling that spills a
// per-pixel table is mis-set, not conservative.
//
// Adding a release() is not purely mechanical, which is why the rollout took a
// pass of its own: several animations gate their table CONTENT on separate
// theme/param sentinels, and a free-and-null that leaves one of those set hands
// back a reallocated buffer that nothing ever refills. release() has to reset
// every such sentinel. That is a correctness requirement and not only a memory
// one, since some tables are sized from w/h and init() alone will not resize
// them. It also must NOT free a borrowed table: an animation that cached
// sinLut() or noiseTex256() only drops its pointer, because those belong to the
// shared term above and other animations still hold them.
#ifndef GM_BGANIM_SRAM_BUDGET
#define GM_BGANIM_SRAM_BUDGET (28 * 1024)
#endif
constexpr size_t SRAM_TOTAL_BUDGET = GM_BGANIM_SRAM_BUDGET;

// Bytes alloc() has handed out from each pool since boot, so a bench run can
// tell whether an animation's tables actually landed where the policy above
// intends. Not synchronised: written on the render task at init, read over
// HTTP, and a torn 32-bit read here would only misreport a diagnostic.
extern size_t g_allocSram;
extern size_t g_allocPsram;

void *alloc(size_t size); // see SRAM_ALLOC_LIMIT for the placement policy

// Give back one table from alloc(). Takes the size because the budget counters
// have to be decremented by the same amount they were charged -- otherwise
// freeing would return the memory but not the right to allocate it again, and
// the ceiling would still lock the fleet into PSRAM after a few switches.
// Nulls the caller's pointer so the `if (ptr == nullptr)` init guards see a
// clean slate. Safe on nullptr.
void release(void *&p, size_t size);

// Type-safe wrapper. Only ever pass the pointer that OWNS the allocation:
// several animations keep alias pointers offset into a table they already hold
// (silk's contrastLUT/paletteExt/palette all point into g_lut, lava's lavaBase
// is lavaLUT + LUT_OFFSET), and handing one of those to free() is heap
// corruption. Null the aliases by hand instead.
template <typename T> inline void releaseTable(T *&p, size_t bytes) {
    void *tmp = p;
    release(tmp, bytes);
    p = static_cast<T *>(tmp);
}

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

// Tone controls, applied to the stops before any animation sees them.
//
// Overlaid text is unreadable on a bright background, and measurement (see
// tools/animbench/lumaprofile.cpp) puts that at 7 of the 13 animations on the
// default theme and 106 of the 234 animation/theme pairs, so it is a property
// of the theme rather than of any one animation. Every animation's colour
// reaches it through themeRGB(), directly or via buildThemeRamp/Wheel, and
// none of them writes an RGB565 literal -- which makes the stops the one place
// a tone change costs nothing per pixel and covers the whole fleet, custom hex
// themes included.
//
//   brightness256  Q8 scale on every channel. 256 leaves the theme alone.
//   knee           Highlight shoulder: channels above `knee` are compressed to
//                  a quarter of their remaining range, so mid-tones keep their
//                  colour and only highlights bend. 255 disables it.
//
// A shoulder is the better shape than scaling everything: at knee 76 (0.30 of
// full scale) the worst animation/theme pair in the fleet reaches contrast
// 4.63 with mid-tone colour intact, where the same result from brightness alone
// needs a scale of about 0.45 and takes the whole image toward grey (0.55, the
// deepest scale that still looks like the theme, only reaches 3.44).
void setThemeTone(int brightness256, int knee);

// RGB888 sample of the active theme gradient, pos 0 (darkest) .. 255.
void themeRGB(int pos, uint8_t out[3]);
// 256-entry RGB565 ramp across the active theme, scaled by brightness Q8
// (0..256). reversed=true puts the brightest stop at index 0.
//
// This brightness is a per-animation scale on one ramp, and every caller in the
// tree passes 256. The user-facing brightness control is setThemeTone(), which
// acts on the stops and so reaches themeRGB() callers too; do not wire a second
// control to this argument, or the two would multiply.
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
