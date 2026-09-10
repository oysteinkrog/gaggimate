#ifndef GAGGIMATE_SIM

// "Stripes": broad, soft parallel stripes with a slowly travelling beat,
// turning once per 150 seconds. This is entry 33 of anim_bench.html,
// including its softened 4/5 harmonics, 5:3 weights and raised base of 34.
// The unequal weights leave one quarter of the amplitude at a beat node.
//
// A 32-bit unsigned DDS cursor covers one complete 4096-entry table cycle.
// Its top 12 bits select a Q4 palette index; the bottom 20 retain sub-entry
// motion. Each pixel adds the screen-anchored 8x8 Bayer offset before the
// four-bit shift and the ordinary 256-entry theme-ramp gather. No wheel,
// row duplication, per-pixel float, or phase carried between band calls.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

#ifndef GM_BGANIM_STRIPES_ASM
#define GM_BGANIM_STRIPES_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int TAB = 4096;
constexpr int TURN_BITS = 20; // 32 - log2(TAB), not the size of a whole turn
constexpr int BASE = 34;     // keeps the beat node above near-black
constexpr float TURN = 4294967296.0f;

// Every owned table is hot, with no width-dependent allocations:
//   tabQ4    4096 uint16_t  8192 B  per-pixel field gather, slab
//   palette   256 uint16_t   512 B  per-pixel colour gather, slab
//   dith       64 int16_t    128 B  per-pixel Bayer offsets, slab
// Total 8832 B of the 9216 B animation allowance, including alignment.
// The shared 2048 B sine LUT is borrowed from the separate 3072 B reserve.
uint16_t *tabQ4 = nullptr;
uint16_t *palette = nullptr;
int16_t *dith = nullptr;
const int16_t *sl = nullptr;
uint32_t lastThemeGen = 0;
bool paletteValid = false;
uint32_t phase0 = 0, stepX = 0, stepY = 0;

void release();

bool init(int, int) {
    sl = sinLut();
    if (sl != nullptr && tabQ4 == nullptr) {
        tabQ4 = static_cast<uint16_t *>(allocHot(TAB * sizeof(uint16_t)));
    }
    if (sl != nullptr && palette == nullptr) {
        palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    }
    if (sl != nullptr && dith == nullptr) {
        dith = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    }
    if (sl == nullptr || tabQ4 == nullptr || palette == nullptr || dith == nullptr) {
        // Roll back the entire partial set so a retry starts with no live
        // slab allocations and no stale palette-generation sentinel.
        release();
        return false;
    }
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (!paletteValid || lastThemeGen != gen) {
        buildThemeRamp(palette, 256);
        // Full ditherAmp, in sixteenths of an index. bayerOffsets on the
        // page uses the same symmetric lround rule, including negatives.
        const float scale = ditherAmp(palette, 256) * 16.0f / 31.5f;
        for (int k = 0; k < 64; ++k) {
            dith[k] = static_cast<int16_t>(lroundf((BAYER8[k] - 31.5f) * scale));
        }
        lastThemeGen = gen;
        paletteValid = true;
    }
    // Integer forms of the page's positive Math.round: 70..135 indices,
    // 590..430 pixels per table cycle, or about 131..96 px per stripe.
    const int span = 70 + (static_cast<int>(p[2]) * 65 + 50) / 100;
    const int cyclePx = 590 - (static_cast<int>(p[1]) * 16 + 5) / 10;
    const float K = TURN / static_cast<float>(cyclePx);
    const float tsec = (static_cast<float>(tMs) * speedMul(p[0])) * 0.001f;
    // 21 sine entries/s is the page's nominal 10 px/s envelope drift.
    // Wide conversions keep days of uptime defined before the phase masks.
    const unsigned beatPhase = static_cast<uint64_t>(tsec * 21.0f) & 1023u;
    for (int i = 0; i < TAB; ++i) {
        const int s = sl[i & 1023] * 5 + sl[(((i * 5) >> 2) + beatPhase) & 1023] * 3 + 4096;
        // s is 0..8192. /512 converts s/8192 * span to Q4 exactly as
        // the page does, retaining its truncation before adding dither.
        tabQ4[i] = static_cast<uint16_t>((BASE << 4) + ((s * span) >> 9));
    }
    // Q8 interpolation between sine entries removes whole-entry angular
    // twitches. Arithmetic shifts round negative interpolation deltas down,
    // as JavaScript >> does on the page and both supported GCC targets do.
    const uint64_t aQ8 = static_cast<uint64_t>(tsec * (1024.0f * 256.0f / 150.0f));
    const unsigned ai = (aQ8 >> 8) & 1023u;
    const int af = aQ8 & 255u;
    const int cosA = sl[(ai + 256) & 1023] +
                     (((sl[(ai + 257) & 1023] - sl[(ai + 256) & 1023]) * af) >> 8);
    const int sinA = sl[ai] + (((sl[(ai + 1) & 1023] - sl[ai]) * af) >> 8);
    stepX = static_cast<uint32_t>(static_cast<int32_t>(cosA * K * (1.0f / 512.0f)));
    stepY = static_cast<uint32_t>(static_cast<int32_t>(sinA * K * (1.0f / 512.0f)));
    // Reduce the 15 px/s slide in pixels before scaling to a whole turn.
    // This is the page's modulo-2^32 p0, without a huge float product that
    // loses an entire turn at long uptimes. Frame setup uses float on both
    // host and device; the page uses double, so very late timestamps can
    // differ in phase rounding. All per-pixel operations are bit-exact.
    phase0 = static_cast<uint32_t>(static_cast<uint64_t>(fmodf(tsec * 15.0f, cyclePx) * K));
}

GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint16_t *__restrict tab = tabQ4;
    const uint16_t *__restrict pal = palette;
    for (int row = 0; row < rows; ++row) {
        const int y = y0 + row;
        uint32_t ph = phase0 + stepY * static_cast<uint32_t>(y);
        const int16_t *off = dith + (y & 7) * 8;
        for (int x = 0; x < w; ++x) {
            const int idx = (tab[ph >> TURN_BITS] + off[x & 7]) >> 4;
            // The page clamps to 0..255. Here tab is 544..2704 and dither
            // is at most +-256 (ditherAmp's cap of 16), so idx is 18..185
            // for every legal parameter/theme. The clamp is an identity.
            *dst++ = pal[idx];
            ph += stepX;
        }
    }
}

#if GM_BGANIM_STRIPES_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// BEGIN VERBATIM PRODUCTION KERNELS
// GCC 14.2, xtensa-asm14.sh with the firmware flags, emits a 14-instruction
// hardware loop for bandRef. This scalar body transcribes its order: all
// three loads have an independent instruction before the consuming op.
// It handles 0..7 trailing pixels, starting at x modulo 8 == 0, and also
// makes zero-width calls safe. Return the wrapped phase for QEMU to check.
GM_ANIM_IRAM __attribute__((noinline)) uint32_t stripesScalarAsm(uint16_t *out, const uint16_t *tab,
                                                               const int16_t *off, const uint16_t *pal,
                                                               uint32_t ph, uint32_t step, int n) {
    uint32_t x = 0, a, b;
    asm volatile("loopnez %[n], 1f\n"
                 "extui   %[a], %[x], 0, 3\n"
                 "extui   %[b], %[ph], 20, 12\n"
                 "addx2   %[a], %[a], %[off]\n"
                 "addx2   %[b], %[b], %[tab]\n"
                 "l16si   %[a], %[a], 0\n"
                 "l16ui   %[b], %[b], 0\n"
                 "add     %[ph], %[ph], %[step]\n"
                 "add     %[a], %[a], %[b]\n"
                 "srai    %[a], %[a], 4\n"
                 "addx2   %[a], %[a], %[pal]\n"
                 "l16ui   %[a], %[a], 0\n"
                 "addi    %[x], %[x], 1\n"
                 "s16i    %[a], %[out], 0\n"
                 "addi    %[out], %[out], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [ph] "+&r"(ph), [x] "+&r"(x), [a] "=&r"(a), [b] "=&r"(b)
                 : [n] "r"(n), [tab] "r"(tab), [off] "r"(off), [pal] "r"(pal), [step] "r"(step)
                 : "memory");
    return ph;
}

// The edge beyond GCC's scalar schedule is keeping the eight Bayer offsets
// in q1 for the whole row and adding them to eight field samples in one
// VADDS.S16. Scalar DDS/table gathers fill q0 in pixel pairs; MOVI.32.A
// then extracts pairs for two interleaved palette gathers and one S32I.
// There is no vector gather and no scratch-memory spill between stages.
//
// Main body: 4*11 sample/pack instructions + 1 vector add + 4*10 palette/
// store instructions + 1 output advance = 86 instructions per 8 pixels,
// 10.75/pixel against GCC's 14. All load-use gaps are filled, including
// across the loop back edge. That is an issue-count lower bound for hot
// SRAM, not a device timing claim. Production timing decides the default.
//
// tab entries 544..2704 and offsets -256..256 sum to 288..2960: signed
// saturation is inactive and extracting bits 4..11 is the exact >>4.
// off must address eight readable offsets at a 16-byte-aligned address.
// band() checks dith's alignment before calling this kernel because
// allocHot can fall back to PSRAM without guaranteeing that alignment.
// Each Bayer row is 16 bytes, so the check covers every row's VLD span.
// out needs only the contract's four-byte alignment, including widths 233
// and 466. S32I stores full pairs only; the scalar tail touches no padding.
//
// q0/q1 are free because GCC never allocates q registers and supplies no
// q-register clobber syntax. SAR is untouched. Never write CPENABLE: the
// FreeRTOS lazy CP3 exception owns enabling and saving the task's PIE state.
// MOVI.32.A selectors 0..3 were separately executed by the QEMU probe in
// tests/anim_stripes/probe_movi before this kernel was written.
GM_ANIM_IRAM __attribute__((noinline)) uint32_t stripesRowAsm(uint16_t *out, const uint16_t *tab,
                                                            const int16_t *off, const uint16_t *pal,
                                                            uint32_t ph, uint32_t step, int n) {
    if (n <= 0) return ph;
    const int blocks = n >> 3;
    if (blocks != 0) {
        const int16_t *d = off;
        uint32_t lo, hi;
        asm volatile("ee.vld.128.ip q1, %[d], 0\n"
                     "loopnez %[n], 1f\n"
                     "extui   %[lo], %[ph], 20, 12\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "extui   %[hi], %[ph], 20, 12\n"
                     "addx2   %[lo], %[lo], %[tab]\n"
                     "addx2   %[hi], %[hi], %[tab]\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[lo], %[lo], %[hi]\n"
                     "ee.movi.32.q q0, %[lo], 0\n"
                     "extui   %[lo], %[ph], 20, 12\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "extui   %[hi], %[ph], 20, 12\n"
                     "addx2   %[lo], %[lo], %[tab]\n"
                     "addx2   %[hi], %[hi], %[tab]\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[lo], %[lo], %[hi]\n"
                     "ee.movi.32.q q0, %[lo], 1\n"
                     "extui   %[lo], %[ph], 20, 12\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "extui   %[hi], %[ph], 20, 12\n"
                     "addx2   %[lo], %[lo], %[tab]\n"
                     "addx2   %[hi], %[hi], %[tab]\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[lo], %[lo], %[hi]\n"
                     "ee.movi.32.q q0, %[lo], 2\n"
                     "extui   %[lo], %[ph], 20, 12\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "extui   %[hi], %[ph], 20, 12\n"
                     "addx2   %[lo], %[lo], %[tab]\n"
                     "addx2   %[hi], %[hi], %[tab]\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[lo], %[lo], %[hi]\n"
                     "ee.movi.32.q q0, %[lo], 3\n"
                     "ee.vadds.s16 q0, q0, q1\n"
                     "ee.movi.32.a q0, %[hi], 0\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 0\n"
                     "ee.movi.32.a q0, %[hi], 1\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 4\n"
                     "ee.movi.32.a q0, %[hi], 2\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 8\n"
                     "ee.movi.32.a q0, %[hi], 3\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 12\n"
                     "addi    %[out], %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out), [ph] "+&r"(ph), [d] "+&r"(d), [lo] "=&r"(lo), [hi] "=&r"(hi)
                     : [n] "r"(blocks), [tab] "r"(tab), [pal] "r"(pal), [step] "r"(step)
                     : "memory");
    }
    if ((n & 7) != 0) return stripesScalarAsm(out, tab, off, pal, ph, step, n & 7);
    return ph;
}
// END VERBATIM PRODUCTION KERNELS

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    if ((reinterpret_cast<uintptr_t>(dith) & 15u) != 0) {
        bandRef(dst, y0, rows, w, tMs, p);
        return;
    }
    for (int row = 0; row < rows; ++row) {
        const int y = y0 + row;
        const uint32_t ph = phase0 + stepY * static_cast<uint32_t>(y);
        stripesRowAsm(dst, tabQ4, dith + (y & 7) * 8, palette, ph, stepX, w);
        dst += w;
    }
}
#else
// The portable reference is also the flag-off and GM_BGANIM_NO_ASM path.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif

void release() {
    releaseTable(tabQ4, TAB * sizeof(uint16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    sl = nullptr; // borrowed, never freed by this animation
    paletteValid = false;
    lastThemeGen = 0;
    phase0 = stepX = stepY = 0;
}

} // namespace

extern const BgAnimation bg_anim_stripes;
const BgAnimation bg_anim_stripes = {
    "stripes",
    "Stripes",
    {{"speed", "Speed", 50},
     {"pitch", "Stripe pitch", 50},
     {"depth", "Depth", 55},
     {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
