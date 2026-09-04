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

// Where an animation's tables live decides more of its band() time than the
// kernel does. Measured 2026-09-04 with the same firmware and every table
// pinned first to internal SRAM and then to PSRAM (full resolution,
// interlace off, ms per frame): plasma 9.1 vs 17.8, caustics 22.6 vs 37.5,
// starfield 15.6 vs 27.8, lava 26.0 vs 47.7, aurora 46.7 vs 67.6. A per-pixel
// lookup into PSRAM is a cache miss whenever the panel's own PSRAM traffic
// and LVGL on the other core have evicted the line, which is most of the
// time, so a small table read 230,400 times a frame is worth more in SRAM
// than any amount of instruction scheduling.
//
// Until this pass the placement was decided at init() against the free pool
// (see INTERNAL_RESERVE below), and after the 2026-09-04 DRAM reclaim the
// pool idles within a few kB of that reserve, so the same table landed in
// SRAM on one boot and PSRAM on the next and band_us swung 2x with it. The
// web UI paid the other side of the same coin: the internal DRAM left for
// WiFi's frame copies depended on which animation happened to be running.
//
// So the hot tables now come from a slab of fixed size, allocated once at
// build time in internal DRAM: the animation's cost to the pool is one
// constant, visible in the linker's RAM figure and the same on every boot,
// and the placement of a table is a decision the animation makes in source,
// not a coin the allocator flips at init(). Two ends, one slab:
//
//   allocHot(size)        per-animation tables read per pixel or per row.
//                         Bump-allocated from the bottom; the region resets
//                         when the last hot table is release()d, which
//                         SleepAnimation does before the next animation
//                         init()s. nullptr when the slab is full: the caller
//                         falls back to alloc() and the table lives in PSRAM,
//                         so an animation that asks for more than fits still
//                         renders, just slower. Choose what goes here by the
//                         pixel count that reads it, not by size.
//   allocHotShared(size)  boot-lifetime tables owned by BgAnimCommon and
//                         borrowed by several animations (sinLut, cosTableF).
//                         Carved from the top, never returned.
//   alloc(size)           everything else: PSRAM, always. Bulk tables swept
//                         sequentially (noise textures, per-frame fields)
//                         stream from PSRAM at close to SRAM speed because the
//                         cache prefetches the run; they were never the tables
//                         the placement decision mattered for.
//
// The slab is 12 KB. The pool idles near 48 KB of DMA-capable internal DRAM
// with both radios up and no animation; two browser tabs cold-loading died
// at 15-18 KB free before the asset gate and the gate now keeps at most three
// big responses in flight (~20 KB) and admits a second only while the pool
// is above its floor (WebUIPlugin::serveWebAsset). 12 KB leaves 36 KB, which
// the web UI's 2/3/4-tab tests are the acceptance for. It also caps a single
// animation's appetite: silk asked for 23.8 KB and starfield 15.9 KB when
// the pool allowed, and the measurements above show plasma's 11.6 KB and
// caustics' 10.8 KB already buying most of the SRAM win, so the diet is to
// rank tables by reads per frame and hand the slab to the top of the list.
// The shared term is 3,072 B (sinLut 2,048 + cosTableF 1,024), leaving
// 9,216 B for the resident animation.
//
// release() has to be called for every hot table, same as for alloc()ed
// ones: the bottom region only resets when its live count reaches zero, so a
// missed release keeps the slab from ever reusing that space and hotUsed()
// never returns to zero between animations. Releasing the most recently
// allocated hot table pops it immediately; releasing an older one does not
// reclaim it until the reset, so a table that changes content at runtime (a
// palette on a theme change) should be rebuilt in place, not freed and
// re-allocated. /api/debug/heap shows
// hot_used, hot_peak and hot_fail; a non-zero hot_fail is the count of
// allocHot() calls that had to fall back to PSRAM since boot.
#ifndef GM_BGANIM_HOT_SLAB
#define GM_BGANIM_HOT_SLAB (12 * 1024)
#endif
constexpr size_t HOT_SLAB_BYTES = GM_BGANIM_HOT_SLAB;
// Kept clear of allocHot() for the shared tables even before they exist:
// sinLut 2,048 + cosTableF 1,024. A new shared table adds its size here.
constexpr size_t HOT_SHARED_RESERVE = 3072;
void *allocHot(size_t size);
void *allocHotShared(size_t size);
// True for a pointer that came from either end of the slab.
bool isHot(const void *p);
// Bytes in use from the bottom (per-animation) end, from the top (shared)
// end, the peak bottom watermark since boot, and the fall-back count.
size_t hotUsed();
size_t hotShared();
size_t hotPeak();
uint32_t hotFailCount();

// Internal DRAM the animation must leave alone, whatever its own budget says.
// Since the hot slab above, this gates only SleepAnimation's band buffers
// (allocPreferInternal); table placement no longer consults the pool.
//
// A fixed budget on what the animation takes is only half the question, and
// the half that does not matter: what the radios need is not a bound on the
// animation's appetite but a floor under what is left. A fixed budget is a
// bound on the wrong side of the subtraction, and the first one was tuned on
// a build with GM_FAKE_CONTROLLER set, where BLE never starts and roughly 30 KB
// more internal DRAM is free than production ever has.
//
// The result on real hardware was total starvation rather than degradation:
// internal free sat at 1.4 KB, and WiFi could not allocate the 180 bytes a
// probe request needs, so the display never associated at all. The failing
// allocation is small, so this is not fragmentation -- the pool was simply
// gone. The display garbled at the same time and for the same reason.
//
// So the band buffers are gated on the free pool as it actually is at the
// moment of the request, not on a number chosen at build time. Reserve
// generously: the radios allocate in bursts long after the animation has
// taken its share, and there is nothing to reclaim it from once the animation
// holds it.
#ifndef GM_INTERNAL_RESERVE
#define GM_INTERNAL_RESERVE (48 * 1024)
#endif
constexpr size_t INTERNAL_RESERVE = GM_INTERNAL_RESERVE;

// The reserve internalHasRoomFor() actually checks against. Boots at
// INTERNAL_RESERVE; /api/debug/anim?reserve=N moves it for the life of the
// boot so a build can be measured with the band buffers forced internal (0)
// or forced to PSRAM (larger than the pool) without a rebuild between the
// two. Takes effect at the next allocation.
size_t internalReserve();
void setInternalReserve(size_t bytes);

// Whether `size` can come out of internal DRAM without cutting into that
// reserve. Checked against the DMA-capable internal pool specifically, because
// that is the one WiFi's frame buffers come from (caps 0x80c) and it is a
// subset of internal DRAM rather than all of it.
bool internalHasRoomFor(size_t size);

// Bytes alloc() and allocHot() have handed out from each pool since boot, so
// a bench run can tell whether an animation's tables actually landed where
// the policy above intends (hot slab bytes count as SRAM). Not synchronised: written on the render task at init, read over
// HTTP, and a torn 32-bit read here would only misreport a diagnostic.
extern size_t g_allocSram;
extern size_t g_allocPsram;

void *alloc(size_t size); // PSRAM; see the hot slab above for the placement policy

// Give back one table from alloc() or allocHot(). Takes the size because the
// counters have to be decremented by the same amount they were charged.
// Nulls the caller's pointer so the `if (ptr == nullptr)` init guards see a
// clean slate. Safe on nullptr. A hot table's bytes come back to the slab
// only once every hot table has been released, so release all of them.
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

// Ordered-dither amplitude for a palette, in palette-index units: half the
// spacing between the palette's RGB565 steps, which is the swing that turns a
// hard step into a dither cell.
//
// Measured PER CHANNEL, taking the fewest steps of any channel that actually
// moves. The contour a viewer sees is set by the slowest channel, not by the
// packed word: the Espresso ramp's word changes at 92 of 256 indices, yet its
// blue holds flat across ~10 indices at a time, and that flat blue is the
// stair. Counting changes on the packed word instead reports ~2.8 indices per
// step, collapses the amplitude to +-0.7, and leaves the banding untouched.
//
// A channel flatter than n/64 steps is a constant across this palette and has
// no staircase to break, so it is skipped rather than dragging the amplitude up.
// Call it whenever the palette is rebuilt (theme, brightness, knee): the
// spacing moves with all three, which is why this is derived and not a
// constant.
float ditherAmp(const uint16_t *pal, int n);

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
// Same, with a position per stop (0..255, ascending; before the first and
// after the last stop the end colour holds). pos == nullptr behaves as
// setThemeStops (equal spacing).
void setThemeStopsPos(const uint8_t (*stops)[3], const uint8_t *pos, int nStops);
uint32_t themeGen();
int themeStopCount();
const uint8_t (*themeStops())[3];
const uint8_t *themeStopPositions();
bool themeUniform(); // equal spacing: the original arithmetic is in use

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
