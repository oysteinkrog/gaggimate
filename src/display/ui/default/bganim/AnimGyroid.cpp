#ifndef GAGGIMATE_SIM

// "Gyroid": broad, softly lit passages through a moving gyroid section,
// sin(A)cos(B) + sin(z)cos(A) + sin(B)cos(z). This is entry 38 of
// tools/animbench/web/anim_bench.html, including its softened floor, wider
// linear passage profile and ordered Bayer dither. The floor and the dither
// amplitude are sliders now; at slider 50 they are the page's 46 and 2.2, so
// the picture at the defaults is the page's picture.
//
// The shared sine has amplitude 512. Dividing the first two factors by 4
// gives Q7 factors; their product and the other two products divided by 16
// are Q14. The page packs (128 + sin(A)/4) into C's low half and
// (16384 + sin(z)cos(A)/16) into its high half. Keep those unsigned biases
// exactly, including the high-half endpoint 32768. R and D are the page's
// signed row tables. All right shifts of signed values are arithmetic on
// both supported compilers, matching JavaScript >>, including negatives.
//
// Motion uses absolute tMs, not the sequence of frame or band calls. At
// speed 50 the A/z clock turns in 32 s and B in 64 s. k=4 at passage size
// 50 means 256 pixels per cell and phase drift of 8 and 4 pixels/s.
//
// Four of the eight sliders shape the section rather than the tone, and all
// four act in frame(). Aspect sets the row cell count against the column
// one, so the passages stretch or squash and only the row tables move. Morph
// rate drives the z clock on its own, so the section can hold still while
// the pattern keeps drifting, or open and close three times as fast. Ground
// level and Grain both fold into the dither table the kernel already gathers
// once per pixel, so neither costs an instruction in band(). Nothing here
// widens the field f, which still spans -49152..49152; what does widen is
// the dither lane, from 44..48 to 0..92, and with it the palette index.

#include "BgAnim.h"
#include "BgAnimCommon.h"

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

#ifndef GM_BGANIM_GYROID_ASM
#define GM_BGANIM_GYROID_ASM 1
#endif

namespace {
using namespace bganim;

// Ground level: the palette index the dark ground sits at, before the
// ordered dither. 4 at slider 0, the page's 46 at 50, 88 at 100. The lower
// end is held at 4 so that the floor plus the deepest negative Bayer offset
// (-4 at grain 100) can never index the palette below 0, which is the one
// bound the vector kernel does not clamp.
constexpr int floorFromParam(int v) { return 4 + (v * 84 + 50) / 100; }
// Grain: the ordered-dither amplitude in palette-index units. 0 at slider 0,
// the page's 2.2 at 50, 4.4 at 100. Written as an exact integer over 1000 so
// that slider 50 divides to the same float the literal 2.2f is.
inline float ampFromParam(int v) { return static_cast<float>(v * 44) / 1000.0f; }
constexpr int ditherKey(int ground, int grain) { return ground * 256 + grain; }
static_assert(floorFromParam(50) == 46, "slider 50 must be the page floor");

int floorIdx = floorFromParam(50);
uint32_t *C = nullptr;
int16_t *R = nullptr;
int16_t *D = nullptr;
int16_t *dith = nullptr;
uint16_t *palette = nullptr;
int32_t *dith32 = nullptr; // ground level + dith, widened for four PIE lanes
uint32_t *constants = nullptr; // four low-half biases, then four low-half masks
const int16_t *sine = nullptr; // borrowed shared table, never released here
int allocW = 0, allocH = 0;
int width = 66; // 42 + round(55 * 0.44), the default passage width
int lastBright = -1;
int lastDitherKey = ditherKey(50, 50); // what init() builds the table for
uint32_t lastThemeGen = 0xffffffffu;

// At 480x480: C 1920, R 960, D 960, dith 128, palette 512 bytes,
// plus dith32 256 and constants 32 bytes, all allocHot, total 4768 of
// the animation's 9216-byte slab. No PSRAM
// tables. The borrowed 2048-byte sine belongs to the shared 3072-byte
// reserve and is not charged to this animation.
void release();

// The floor lives inside dith32 because that is what the kernel gathers, so
// moving the ground level costs the pixel loop nothing. dith keeps the bare
// offsets for bandRef, which adds the floor itself.
void buildDither(int ground, float amp) {
    floorIdx = ground;
    for (int i = 0; i < 64; ++i) {
        // Math.round in pcDither rounds ties toward +infinity. floorf(v+.5)
        // also preserves that rule for negative values; lroundf does not.
        const float v = (static_cast<float>(BAYER8[i]) - 31.5f) / 31.5f * amp;
        dith[i] = static_cast<int16_t>(floorf(v + 0.5f));
        dith32[i] = ground + dith[i];
    }
}

bool init(int w, int h) {
    if (C != nullptr && allocW == w && allocH == h) {
        return true;
    }
    release();
    sine = sinLut();
    if (sine == nullptr) {
        release();
        return false;
    }
    allocW = w;
    allocH = h;
    C = static_cast<uint32_t *>(allocHot(static_cast<size_t>(w) * sizeof(uint32_t)));
    R = static_cast<int16_t *>(allocHot(static_cast<size_t>(h) * sizeof(int16_t)));
    D = static_cast<int16_t *>(allocHot(static_cast<size_t>(h) * sizeof(int16_t)));
    dith = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    dith32 = static_cast<int32_t *>(allocHot(64 * sizeof(int32_t)));
    constants = static_cast<uint32_t *>(allocHot(8 * sizeof(uint32_t)));
    if (C == nullptr || R == nullptr || D == nullptr || dith == nullptr || palette == nullptr ||
        dith32 == nullptr || constants == nullptr) {
        // A failed partial init must leave neither hot allocations nor stale
        // size/theme sentinels behind. Retrying init builds a fresh set.
        release();
        return false;
    }
    buildDither(floorFromParam(50), ampFromParam(50));
    lastDitherKey = ditherKey(50, 50);
    for (int i = 0; i < 4; ++i) {
        constants[i] = 128; // low half biased by 128, high half left at zero
        constants[4 + i] = 65535; // low-half mask in each 32-bit lane
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (lastBright != p[3] || lastThemeGen != gen) {
        // Exactly themeRamp(176 + round(p[3]*0.8)) from the page. Scaling
        // happens before RGB565 quantization, not on an already packed ramp.
        buildThemeRamp(palette, 176 + (static_cast<int>(p[3]) * 80 + 50) / 100);
        lastBright = p[3];
        lastThemeGen = gen;
    }
    const int ground = static_cast<int>(p[6]), grain = static_cast<int>(p[7]);
    if (lastDitherKey != ditherKey(ground, grain)) {
        buildDither(floorFromParam(ground), ampFromParam(grain));
        lastDitherKey = ditherKey(ground, grain);
    }
    const float tt = static_cast<float>(tMs) * speedMul(p[0]);
    const int k = 2 + (static_cast<int>(p[1]) * 4 + 50) / 100;
    // Aspect, Q8 against the column cell count: half at slider 0, the same at
    // 50, double at 100. The +128 rounds, so slider 50 gives back exactly k
    // for every k the passage-size slider can produce.
    const int asp = static_cast<int>(p[4]);
    const int aq = asp <= 50 ? 128 + (asp * 128 + 25) / 50 : 256 + ((asp - 50) * 256 + 25) / 50;
    int ky = (k * aq + 128) >> 8;
    if (ky < 1) {
        ky = 1; // one cell across the panel, never a zero-period row phase
    }
    // Morph rate on the z clock alone: frozen at slider 0, the page's rate at
    // 50 (where the multiply by exactly 1.0f leaves the tick count alone),
    // three times it at 100.
    const int mo = static_cast<int>(p[5]);
    const float zm = mo <= 50 ? static_cast<float>(mo) / 50.0f : 1.0f + static_cast<float>(mo - 50) / 25.0f;
    // The page's |0 truncates before its &1023. Float setup is intentional
    // on LX7; uint32 covers the whole uint32 tMs lifetime at speed 100
    // (under 924 million ticks), then the table lookup wraps the phase.
    // Float can move a late-uptime boundary by a tick versus JS doubles;
    // it never changes the rates or overflows a conversion.
    const float aTicks = (tt / 32000.0f) * 1024.0f;
    const uint32_t tA = static_cast<uint32_t>(aTicks);
    const uint32_t tB = static_cast<uint32_t>((tt / 64000.0f) * 1024.0f);
    const uint32_t tZ = static_cast<uint32_t>(aTicks * zm);
    const int z = sine[tZ & 1023u];
    const int zc = sine[(tZ + 256u) & 1023u];
    for (int x = 0; x < w; ++x) {
        const uint32_t A = static_cast<uint32_t>(k * x) - tA;
        const uint32_t low = 128 + (sine[A & 1023u] >> 2);
        const uint32_t high = 16384 + ((z * sine[(A + 256u) & 1023u]) >> 4);
        C[x] = (high << 16) | low;
    }
    for (int y = 0; y < h; ++y) {
        const uint32_t B = static_cast<uint32_t>(ky * y) - tB;
        R[y] = sine[(B + 256u) & 1023u] >> 2;
        D[y] = (sine[B & 1023u] * zc) >> 4;
    }
    width = 42 + (static_cast<int>(p[2]) * 44 + 50) / 100;
}

// Literal integer specification of the page's pixel loop. No row pairing
// or state from earlier band calls: Bayer phase is the absolute y & 7.
GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; ++row) {
        const int y = y0 + row;
        const int r = R[y], d = D[y];
        const int16_t *__restrict dr = dith + (y & 7) * 8;
        for (int x = 0; x < w; ++x) {
            const uint32_t pk = C[x];
            int f = (static_cast<int>(pk & 65535u) - 128) * r + static_cast<int>(pk >> 16) - 16384 + d;
            if (f < 0) {
                f = -f;
            }
            const int g = width - (f >> 8); // Q14 magnitude to 1/64 field units
            int i = g > 0 ? floorIdx + g : floorIdx;
            i += dr[x & 7];
            i = i < 0 ? 0 : (i > 255 ? 255 : i);
            dst[static_cast<size_t>(row) * w + x] = palette[i];
        }
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM) && GM_BGANIM_GYROID_ASM
// GCC 14's original bandRef .L4 loop was transcribed before this pass:
// l32i, extui(dither), extui(low), addi(-128), mull, extui(high), add,
// addmi(-16384), add(d), abs, srai(8), sub(width), addx2(dither), l16si,
// max(0), addi(46), add, min(255), max(0), extui, addx2(palette), l16ui,
// addi(x), s16i, addi(C), addi(dst). 26 instructions/pixel, hardware LOOP,
// no immediate load-use stalls. The advantage here is four-lane PIE work
// that GCC cannot emit, with direct q-to-AR moves instead of scratch SRAM.
//
// q0 = packed columns, later dither; q1 = product/field/index; q2 = bias,
// high half or negation; q3 = 0xffff per word; q4 = [r,0] halfwords;
// q5 = d-16384 per word; q6 = width per word; q7 = zero. A signed 16-bit
// product is widened before adding the other terms. This handles even
// unrelated extrema of C.low 0..256, C.high 0..32768, r -128..128 and
// d -16384..16384: f fits -49152..49152, and no saturating 32-bit operation
// reaches its limit. Masking the arithmetic high-half shift preserves
// C.high==32768. The index is dr + max(width-(abs(f)>>8),0), where dr is the
// ground level 4..88 plus the Bayer offset -4..4 and so spans 0..92; the
// index is then 0..178 for width 42..86, inside the palette at both ends
// with no clamp of its own. The ground level's lower stop of 4 is what makes
// the bottom end safe; see floorFromParam above.
//
// C, constants and dith32 come from the 16-byte-aligned hot slab. Each
// vector span starts at x=0 and advances four columns (16 bytes); the
// dither pointer alternates offsets 0 and 16 within one absolute-y row.
// Output uses scalar s32i only, so the contract's four-byte alignment is
// sufficient even for the 233-pixel single-row path. No vector load ever
// touches the scalar tail. No CPENABLE write: FreeRTOS owns lazy CP3 state.
// GCC never allocates q0-q7 and exposes no q-register clobber constraints.
// All q state lives inside this noinline leaf; there are no intervening calls.
// IRAM and hot-table placement are device choices. Host throughput cannot
// predict contention on the device's shared flash/PSRAM bus.
GM_ANIM_IRAM __attribute__((noinline)) void gyroidRowAsm(
    uint16_t *out, const uint32_t *col, const uint16_t *pal,
    const uint32_t *bias, const int32_t *dr, int r, int d, int wid, int w) {
    const uint32_t rword = (uint16_t)r; // signed low half, upper half zero
    const int delta = d - 16384;
    const uint32_t *mask = bias + 4;
    asm volatile("ee.movi.32.q q4, %[r], 0\n"
                 "ee.movi.32.q q4, %[r], 1\n"
                 "ee.movi.32.q q4, %[r], 2\n"
                 "ee.movi.32.q q4, %[r], 3\n"
                 "ee.movi.32.q q5, %[d], 0\n"
                 "ee.movi.32.q q5, %[d], 1\n"
                 "ee.movi.32.q q5, %[d], 2\n"
                 "ee.movi.32.q q5, %[d], 3\n"
                 "ee.movi.32.q q6, %[wid], 0\n"
                 "ee.movi.32.q q6, %[wid], 1\n"
                 "ee.movi.32.q q6, %[wid], 2\n"
                 "ee.movi.32.q q6, %[wid], 3\n"
                 "ee.vld.128.ip q3, %[mask], 0\n"
                 "ee.zero.q q7\n"
                 : [mask] "+&r"(mask)
                 : [r] "r"(rword), [d] "r"(delta), [wid] "r"(wid)
                 : "memory");
    int phase = 0;
    const int toggle = 16; // four int32 dither lanes, half the 8-column period
    const int nQuads = w >> 2;
    const int32_t *dp;
    uint32_t t0, t1, t2, t3;
    asm volatile("loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[col], 16\n"
                 "ee.vld.128.ip q2, %[bias], 0\n"
                 "ee.andq q1, q0, q3\n"
                 "ee.vsubs.s16 q1, q1, q2\n"
                 "ssai 0\n"
                 "ee.vmul.s16 q1, q1, q4\n"
                 "ssai 16\n"
                 "ee.vsr.32 q2, q0\n"
                 "ee.vsl.32 q1, q1\n"
                 "ee.andq q2, q2, q3\n"
                 "ee.vsr.32 q1, q1\n"
                 "ee.vadds.s32 q2, q2, q5\n"
                 "ee.vadds.s32 q1, q1, q2\n"
                 "ee.vsubs.s32 q2, q7, q1\n"
                 "ee.vmax.s32 q1, q1, q2\n"
                 "ssai 8\n"
                 "ee.vsr.32 q1, q1\n"
                 "ee.vsubs.s32 q1, q6, q1\n"
                 "add %[dp], %[dr], %[phase]\n"
                 "ee.vld.128.ip q0, %[dp], 0\n"
                 "ee.vmax.s32 q1, q1, q7\n"
                 "ee.vadds.s32 q1, q1, q0\n"
                 "ee.movi.32.a q1, %[t0], 0\n"
                 "ee.movi.32.a q1, %[t1], 1\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "ee.movi.32.a q1, %[t2], 2\n"
                 "slli %[t1], %[t1], 16\n"
                 "ee.movi.32.a q1, %[t3], 3\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addx2 %[t2], %[t2], %[pal]\n"
                 "addx2 %[t3], %[t3], %[pal]\n"
                 "l16ui %[t2], %[t2], 0\n"
                 "l16ui %[t3], %[t3], 0\n"
                 "xor %[phase], %[phase], %[toggle]\n"
                 "slli %[t3], %[t3], 16\n"
                 "or %[t2], %[t2], %[t3]\n"
                 "s32i %[t2], %[out], 4\n"
                 "addi %[out], %[out], 8\n"
                 "1:\n"
                 : [out] "+&r"(out), [col] "+&r"(col), [phase] "+&r"(phase),
                   [dp] "=&r"(dp), [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3),
                   [bias] "+&r"(bias)
                 : [dr] "r"(dr), [pal] "r"(pal), [toggle] "r"(toggle), [n] "r"(nQuads)
                 : "memory");
    for (int x = nQuads * 4; x < w; ++x) {
        const uint32_t pk = *col++;
        int f = ((int)(pk & 65535u) - 128) * r + (int)(pk >> 16) - 16384 + d;
        if (f < 0) f = -f;
        const int g = wid - (f >> 8);
        *out++ = pal[(g > 0 ? g : 0) + dr[x & 7]];
    }
}
#endif

void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM) && GM_BGANIM_GYROID_ASM
    (void)tMs;
    (void)p;
    for (int row = 0; row < rows; ++row) {
        const int y = y0 + row;
        gyroidRowAsm(dst + static_cast<size_t>(row) * w, C, palette, constants,
                     dith32 + (y & 7) * 8, R[y], D[y], width, w);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(C, static_cast<size_t>(allocW) * sizeof(uint32_t));
    releaseTable(R, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(D, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(dith32, 64 * sizeof(int32_t));
    releaseTable(constants, 8 * sizeof(uint32_t));
    sine = nullptr;
    allocW = allocH = 0;
    width = 66;
    floorIdx = floorFromParam(50);
    lastDitherKey = ditherKey(50, 50);
    lastBright = -1;
    lastThemeGen = 0xffffffffu;
}

} // namespace

extern const BgAnimation bg_anim_gyroid;
const BgAnimation bg_anim_gyroid = {
    "gyroid",
    "Gyroid",
    {{"speed", "Speed", 50},
     {"scale", "Passage size", 50},
     {"glow", "Passage width", 55},
     {"bright", "Brightness", 60},
     {"aspect", "Aspect", 50},
     {"morph", "Morph rate", 50},
     {"floor", "Ground level", 50},
     {"grain", "Grain", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
