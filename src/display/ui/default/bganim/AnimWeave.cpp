#ifndef GAGGIMATE_SIM

// "Weave" - soft cellular light through loosely woven cloth. This is the
// 'weave' entry in tools/animbench/web/anim_bench.html: a tileable 64 x 64
// texture from three sine waves, rotated, breathed and translated by two
// Q16.16 cursors. The texture already contains theme RGB565 pixels, so the
// hot loop has one gather per pixel. The approved page adds no dither here.
//
// Eight parameters, all of them handled in frame() (gm-3vj.30). Three build
// the cell table: brightness, contrast and cross weave. Three set the
// motion: turn rate, drift and breath rate, alongside speed and scale. Each
// one is exactly its old hard-coded constant at 50, so the defaults render
// what this file rendered before them, and none of them reaches the pixel
// loop: band(), bandRef() and the Xtensa kernel read the same table and the
// same two cursors they always did.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#ifndef GM_BGANIM_WEAVE_ASM
#define GM_BGANIM_WEAVE_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int TILE_N = 64;
constexpr int TILE_PIXELS = TILE_N * TILE_N;
constexpr uint32_t SPAN = TILE_N << 16; // one Q16.16 tile, modulo 2^22
uint16_t *tex = nullptr;  // 8,192 B, slab: one random read per pixel
uint16_t *ramp = nullptr; // 512 B, PSRAM: only read when rebuilding tex
int32_t *phaseData = nullptr; // 96 B, slab: six aligned PIE vectors read per row
const int16_t *sine = nullptr; // borrowed shared 1,024-entry, +/-512 LUT
uint32_t lastThemeGen = 0;
int lastTexKey = -1; // brightness, contrast and cross weave, packed
uint32_t u0 = 0, v0 = 0;
int du = 0, dv = 0;

void release();

bool init(int, int) {
    sine = sinLut();
    if (sine == nullptr) {
        release();
        return false;
    }
    if (tex == nullptr) {
        tex = static_cast<uint16_t *>(allocHot(TILE_PIXELS * sizeof(uint16_t)));
    }
    if (ramp == nullptr) {
        ramp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    }
    if (phaseData == nullptr) {
        phaseData = static_cast<int32_t *>(allocHot(24 * sizeof(int32_t)));
    }
    if (tex == nullptr || ramp == nullptr || phaseData == nullptr) {
        // A failed init must leave no partial allocation for the next retry.
        release();
        return false;
    }
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    // Contrast: how far the cell table swings either side of the ramp's
    // middle, in thousandths of a palette step per sine unit. 8 at slider 0,
    // so the cloth is a faint mottle; the original 33 at 50; 66 at 100, where
    // the crossings print as a hard waffle.
    const int contrast = static_cast<int>(p[3]);
    const int amp = contrast <= 50 ? 8 + contrast * 25 / 50 : 33 + (contrast - 50) * 33 / 50;
    // Cross weave: the Q8 weight of the diagonal sine that turns a square
    // grid of bars into cloth. 128 at slider 50 is the >> 1 the table was
    // written with, and (s * 128) >> 8 is that shift for negative s too.
    const int crossW = static_cast<int>(p[4]) * 256 / 100;
    const int texKey = static_cast<int>(p[2]) | (amp << 8) | (crossW << 16);
    if (lastTexKey != texKey || lastThemeGen != gen) {
        // pa_bright: 80 + Math.round(p[2] * 176 / 100), Q8 brightness.
        // Scale RGB888 before packing, exactly as the page's themeRamp does.
        buildThemeRamp(ramp, 80 + (static_cast<int>(p[2]) * 176 + 50) / 100);
        for (int j = 0; j < TILE_N; j++) {
            for (int i = 0; i < TILE_N; i++) {
                const int value = sine[i * 16] + sine[j * 16] + ((sine[((i + j) * 16) & 1023] * crossW) >> 8);
                // 104 + Math.round(value * amp / 1000). Exact rational
                // arithmetic also preserves JS's ties toward +infinity for
                // negative v. amp is 33 at the default contrast.
                const int scaled = value * amp + 500;
                int idx = 104 + (scaled >= 0 ? scaled / 1000 : (scaled - 999) / 1000);
                idx = idx < 0 ? 0 : (idx > 255 ? 255 : idx);
                tex[j * TILE_N + i] = ramp[idx];
            }
        }
        lastTexKey = texKey;
        lastThemeGen = gen;
    }

    // Speed calibration, gm-33fm 2026-09-12: the base rate carries a
    // deliberate factor so that Speed 50 moves this animation about as
    // much per second as every other animation at Speed 50.
    // The 2.2f is that factor. Every clock below reads t, so one factor here
    // moves the turn, the drift and the breath together.
    // The clocks below are the page's, in the page's double arithmetic and
    // the page's operation order. In single precision the drift cursor came
    // out one Q16 unit off at frame 120 (cu 1251476 against the page's
    // 1251475, from fmodf of a product near 1.6e6), which moved two texel
    // boundaries by a pixel (gm-pciz). This runs once a frame, so the
    // software double is a few microseconds; nothing per pixel changes.
    const double t = static_cast<double>(tMs) * speedMul(p[0]) * 2.2;
    // Turn rate, drift and breath rate are all 1.0 at slider 50, so each one
    // multiplies its constant by exactly one and the default frame is the old
    // frame. Below 50 they run down to a standstill; above, up to three times
    // the old rate for the two clocks and twice the old speed for the drift.
    const auto rateMul = [](uint8_t v) { return v <= 50 ? v / 50.0 : 1 + (v - 50) * (2.0 / 50); };
    const double turnMul = rateMul(p[5]);
    const double breathMul = rateMul(p[7]);
    const double driftMul = p[6] / 50.0;
    // One turn in 120 seconds, using the same 1,024-step sine and cosine.
    // Math.round on a non-negative value is floor(v + 0.5).
    const int angle = static_cast<int>(floor(t * (1024.0 / 120000) * turnMul + 0.5)) & 1023;
    // One 40-second breath of +/-22.5 percent. The unbreathed scale is
    // 0.20..0.50 texels/pixel, hence even the largest breath stays below 1.
    // Breath rate changes how often it breathes and never how far, which is
    // what keeps |du| and |dv| inside the kernel's bound below.
    const double breath = 1 + 0.225 * sin(t * (2 * M_PI / 40000) * breathMul);
    const double sBase = 0.20 + p[1] * (0.30 / 100);
    const int scale = static_cast<int>(floor(65536 * sBase * breath + 0.5));
    du = (sine[(angle + 256) & 1023] * scale) >> 9;
    dv = (sine[angle] * scale) >> 9;
    // Translation is 5 and 3.2 px/s measured at the unbreathed scale.
    // Wrap before conversion so every uint32 timestamp stays in range.
    const double nom = 65536 * sBase;
    const uint32_t cu = static_cast<uint32_t>(floor(fmod(t * 0.005 * nom * driftMul, static_cast<double>(SPAN)) + 0.5));
    const uint32_t cv = static_cast<uint32_t>(floor(fmod(t * 0.0032 * nom * driftMul, static_cast<double>(SPAN)) + 0.5));
    // The page anchors at (240,240), including when w/h differ. Retain that
    // fixed panel origin, rather than silently changing the crop at 240 wide.
    // Unsigned phases define the page's |0 wrap without signed C++ overflow.
    u0 = cu - 240u * static_cast<uint32_t>(du) + 240u * static_cast<uint32_t>(dv);
    v0 = cv - 240u * static_cast<uint32_t>(dv) - 240u * static_cast<uint32_t>(du);
    for (int lane = 0; lane < 4; lane++) {
        // Four consecutive cursors, then their four-pixel increments. Shift
        // v left six bits so both axes can share SAR=16 during index packing.
        phaseData[lane] = lane * du;
        phaseData[4 + lane] = lane * dv * 64;
        phaseData[8 + lane] = 4 * du;
        phaseData[12 + lane] = 4 * dv * 64;
        phaseData[16 + lane] = 63;
        phaseData[20 + lane] = 0x0fc0;
    }
}

GM_ANIM_IRAM void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        uint32_t u = u0 - static_cast<uint32_t>(y) * static_cast<uint32_t>(dv);
        uint32_t v = v0 + static_cast<uint32_t>(y) * static_cast<uint32_t>(du);
        for (int x = 0; x < w; x++) {
            *dst++ = tex[((v >> 10) & 0x0fc0u) | ((u >> 16) & 63u)];
            u += static_cast<uint32_t>(du);
            v += static_cast<uint32_t>(dv);
        }
    }
}

#if GM_BGANIM_WEAVE_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14's original .L4 loop was transcribed before choosing this schedule:
// srli(v,10), extui(v,6,6), extui(u,16,6), slli(v,6), or, addx2,
// l16ui, add(u,du), s16i, add(v,dv), addi(out,2). Eleven instructions per
// pixel, already inside LOOP, and its load-use gap was already filled.
//
// The edge here is four independent cursors in PIE registers, including the
// shifts, masks and index OR. Pull each index directly into an AR with
// EE.MOVI.32.A, then interleave the four 16-bit gathers and emit two S32I
// pairs. No scratch decode buffer or additional PSRAM traffic. The 26-op
// LOOPNEZ body is 6.5 instructions/pixel, with no immediate load consumer.
// This is a schedule claim, not a device timing: the production A/B decides
// the winner, regardless of the host's scalar timing or these op counts.
//
// data is allocHot's 16-byte-aligned block of six 16-byte vectors. The kernel
// never vector-loads or vector-stores out, whose contract is only 4-byte
// alignment. q0/q1 hold u and v*64; q2/q3 their increments; q4/q5 their
// masks; q6/q7 are scratch. GCC never allocates q registers and has no q
// clobber syntax. FreeRTOS saves CP3 lazily; this function never enables it.
//
// Normalize the starting phases modulo 2^22. For at most 128 quads and
// |du|,|dv| <= 40,141 (round(65536*0.50*1.225)), even the final increment
// keeps |v*64| < 1.6e9. Thus EE.VADDS.S32 cannot saturate. Normalizing each
// <=512-pixel chunk also handles arbitrary uint32 phases and longer rows.
// U retains all 16 fractional bits. V*64 retains them too; its low six bits
// are zero, and SAR=16 followed by 0x0fc0 selects original bits 16..21.
GM_ANIM_IRAM __attribute__((noinline)) void weaveQuadAsm(uint16_t *out, const uint16_t *texture,
                                                        const int32_t *data, uint32_t u, uint32_t v, int nQuads) {
    u &= 0x003fffffu;
    v = (v & 0x003fffffu) << 6;
    int32_t t0, t1, t2, t3;
    asm volatile("ee.movi.32.q q0, %[u], 0\n"
                 "ee.movi.32.q q0, %[u], 1\n"
                 "ee.movi.32.q q0, %[u], 2\n"
                 "ee.movi.32.q q0, %[u], 3\n"
                 "ee.movi.32.q q1, %[v], 0\n"
                 "ee.movi.32.q q1, %[v], 1\n"
                 "ee.movi.32.q q1, %[v], 2\n"
                 "ee.movi.32.q q1, %[v], 3\n"
                 "ee.vld.128.ip q6, %[data], 16\n"
                 "ee.vld.128.ip q7, %[data], 16\n"
                 "ee.vadds.s32 q0, q0, q6\n"
                 "ee.vadds.s32 q1, q1, q7\n"
                 "ee.vld.128.ip q2, %[data], 16\n"
                 "ee.vld.128.ip q3, %[data], 16\n"
                 "ee.vld.128.ip q4, %[data], 16\n"
                 "ee.vld.128.ip q5, %[data], 16\n"
                 "ssai 16\n"
                 "loopnez %[n], 1f\n"
                 "ee.vsr.32 q6, q0\n"
                 "ee.vsr.32 q7, q1\n"
                 "ee.andq q6, q6, q4\n"
                 "ee.andq q7, q7, q5\n"
                 "ee.orq q6, q6, q7\n"
                 "ee.movi.32.a q6, %[t0], 0\n"
                 "ee.movi.32.a q6, %[t1], 1\n"
                 "addx2 %[t0], %[t0], %[tex]\n"
                 "addx2 %[t1], %[t1], %[tex]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "ee.movi.32.a q6, %[t2], 2\n"
                 "slli %[t1], %[t1], 16\n"
                 "ee.movi.32.a q6, %[t3], 3\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addx2 %[t2], %[t2], %[tex]\n"
                 "addx2 %[t3], %[t3], %[tex]\n"
                 "l16ui %[t2], %[t2], 0\n"
                 "l16ui %[t3], %[t3], 0\n"
                 "ee.vadds.s32 q0, q0, q2\n"
                 "slli %[t3], %[t3], 16\n"
                 "ee.vadds.s32 q1, q1, q3\n"
                 "or %[t2], %[t2], %[t3]\n"
                 "s32i %[t2], %[out], 4\n"
                 "addi %[out], %[out], 8\n"
                 "1:\n"
                 : [out] "+&r"(out), [data] "+&r"(data), [t0] "=&r"(t0), [t1] "=&r"(t1),
                   [t2] "=&r"(t2), [t3] "=&r"(t3)
                 : [u] "r"(u), [v] "r"(v), [tex] "r"(texture), [n] "r"(nQuads)
                 : "memory");
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int y = y0; y < y0 + rows; y++) {
        uint32_t u = u0 - static_cast<uint32_t>(y) * static_cast<uint32_t>(dv);
        uint32_t v = v0 + static_cast<uint32_t>(y) * static_cast<uint32_t>(du);
        int left = w;
        while (left >= 4) {
            const int quads = left >= 512 ? 128 : left / 4;
            weaveQuadAsm(dst, tex, phaseData, u, v, quads);
            const int pixels = quads * 4;
            u += static_cast<uint32_t>(pixels) * static_cast<uint32_t>(du);
            v += static_cast<uint32_t>(pixels) * static_cast<uint32_t>(dv);
            dst += pixels;
            left -= pixels;
        }
        // 0..3 scalar pixels, including the 233-wide panel's half-size tail.
        while (left-- > 0) {
            *dst++ = tex[((v >> 10) & 0x0fc0u) | ((u >> 16) & 63u)];
            u += static_cast<uint32_t>(du);
            v += static_cast<uint32_t>(dv);
        }
    }
}
#else
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif

void release() {
    releaseTable(tex, TILE_PIXELS * sizeof(uint16_t));
    releaseTable(ramp, 256 * sizeof(uint16_t));
    releaseTable(phaseData, 24 * sizeof(int32_t));
    sine = nullptr; // shared table, never owned here
    lastThemeGen = 0;
    lastTexKey = -1;
    u0 = v0 = 0;
    du = dv = 0;
}

} // namespace

extern const BgAnimation bg_anim_weave;
const BgAnimation bg_anim_weave = {
    "weave",
    "Weave",
    {{"speed", "Speed", 50},
     {"scale", "Weave scale", 50},
     {"brightness", "Brightness", 62},
     {"contrast", "Contrast", 50},
     {"cross", "Cross weave", 50},
     {"turn", "Turn rate", 50},
     {"drift", "Drift", 50},
     {"breath", "Breath rate", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
