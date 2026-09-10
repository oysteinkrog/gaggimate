#ifndef GAGGIMATE_SIM

// "Rain" - sparse, soft streaks falling down a dark window. Each eight
// pixel lane has its own deterministic phase and one of three fall speeds.
// Four cross-section gains (0.18, 0.45, 0.80, 1.00), mirrored about the
// lane centre, soften the sides. A front-loaded tail fades above the head;
// a short quadratic glow rounds its lower tip.
//
// This is entry 'rain' in tools/animbench/web/anim_bench.html. Its executable
// values govern: floor 66 at the default, Q4 uint16 samples, tail 88..232,
// eight pixel lanes. The page's older header still says floor 44, uint8 and
// four pixel lanes, and its tail-range comment says 250. Those are not what
// the page renders.
//
// The logical table is four classes times a 2048 pixel cycle. Q4 would make
// that 16,384 B, before palette and lane phases, exceeding the hot slab.
// Lossless storage: rotate u by the maximum tail (232), keep its 247 active
// positions (232 above, the head, 14 below), and collapse every other
// position to entry 247, the constant floor. Each stored position carries
// the eight cross-section samples together, so PIE can load a whole lane.
// No sampling, tail length, rounding or cycle is changed by this layout.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#ifndef GM_BGANIM_RAIN_ASM
#define GM_BGANIM_RAIN_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int CYC = 2048;       // same 11-bit vertical wrap as the page
constexpr int LANE = 8;         // cross-section and Bayer row both repeat here
constexpr int TAIL = 160;       // default tail: 160 * (0.55 + 50 * 0.009)
constexpr int MAX_TAIL = 232;   // round(160 * (0.55 + 100 * 0.009))
constexpr int BELOW = 14;       // quadratic glow below the head
constexpr int FLOOR_SLOT = MAX_TAIL + BELOW + 1;
constexpr int PROFILE_N = FLOOR_SLOT + 1;
constexpr size_t PROFILE_BYTES = PROFILE_N * LANE * sizeof(uint16_t);

uint16_t *tabQ4 = nullptr;     // [248][8], rotated profile and shared floor
uint16_t *lanePhase = nullptr; // [ceil(w/8)], one 11-bit phase per lane
int16_t *dith = nullptr;       // [8][8], signed Q4 Bayer offsets
uint16_t *pal = nullptr;       // [256], theme ramp, no wheel
uint16_t *background = nullptr; // [8][8], floor RGB565 after dither/palette
uint16_t *seed = nullptr;      // [ceil(w/8)], read only in frame()
int allocW = 0;
int seedN = 0;
int lastTail = -1;
int lastGlow = -1;
int lastWidth = -1;
int lastFade = -1;
int lastBase = -1;
int lastGrain = -1;
uint32_t lastThemeGen = 0xFFFFFFFFu;
bool tablesValid = false;

// Lane cross-section, mirrored about the centre, as a gain on the streak's
// brightness above the floor. The middle set is the approved preview's
// 0.18/0.45/0.80/1.00, which "Drop width" 50 reproduces exactly: the mix
// factor is zero there and a float plus zero is the float. Narrow leaves a
// two pixel core with almost nothing beside it, wide spreads the streak
// across the whole eight pixel lane. Every entry is inside 0..1, so a Q4
// sample stays between floor * 16 and head * 16 at any width.
constexpr float GAIN_MID[4] = {0.18f, 0.45f, 0.80f, 1.00f};
constexpr float GAIN_NARROW[4] = {0.00f, 0.06f, 0.55f, 1.00f};
constexpr float GAIN_WIDE[4] = {0.62f, 0.82f, 0.94f, 1.00f};

// At 480 wide: tabQ4 3,968 B + lanePhase 120 B (128 B slab reservation)
// + dith 128 B + pal 512 B + background 128 B = 4,856 B payload,
// 4,864 B of the 9,216 B slab. seed is 120 B in PSRAM, swept once per
// frame. Every vector source is 16-byte aligned by
// allocHot and its row stride; no band-buffer alignment beyond BgAnim.h's
// four bytes will be assumed. No persistent table lives in static BSS.
void release();

// The page uses mulberry32, not bganim's xorshift32. Unsigned arithmetic
// reproduces Math.imul's low 32 bits, and the high eleven random bits are
// exactly floor(rnd() * 2048), without a float round trip.
uint32_t rainRand(uint32_t &s) {
    s += 0x6D2B79F5u;
    uint32_t t = (s ^ (s >> 15)) * (1u | s);
    t ^= t + (t ^ (t >> 7)) * (61u | t);
    return t ^ (t >> 14);
}

bool init(int w, int h) {
    if (w <= 0 || h <= 0) {
        release();
        return false;
    }
    if (tablesValid && allocW == w) {
        return true;
    }
    if (allocW != 0 && allocW != w) {
        release();
    }
    allocW = w;
    // Reserve the partial lane too, but frame() preserves the page's
    // zero phase there: Uint16Array(w / 8) truncates, and an undefined
    // seed becomes zero when the page applies its bitwise phase mask.
    seedN = (w + LANE - 1) / LANE;
    if (tabQ4 == nullptr) tabQ4 = static_cast<uint16_t *>(allocHot(PROFILE_BYTES));
    if (lanePhase == nullptr) lanePhase = static_cast<uint16_t *>(allocHot(seedN * sizeof(uint16_t)));
    if (dith == nullptr) dith = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    if (pal == nullptr) pal = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    if (background == nullptr) background = static_cast<uint16_t *>(allocHot(64 * sizeof(uint16_t)));
    if (seed == nullptr) seed = static_cast<uint16_t *>(alloc(seedN * sizeof(uint16_t)));
    if (tabQ4 == nullptr || lanePhase == nullptr || dith == nullptr || pal == nullptr || background == nullptr || seed == nullptr) {
        // Includes all successful allocations before and after the failed
        // one, so a retry cannot inherit a partial set or pin the slab.
        release();
        return false;
    }
    uint32_t s = 0x51aeu; // the approved preview's random seed
    for (int lane = 0; lane < seedN; lane++) seed[lane] = static_cast<uint16_t>(rainRand(s) >> 21);
    return true;
}

void frame(uint32_t tMs, int w, int, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    const bool themeDirty = !tablesValid || gen != lastThemeGen;
    if (themeDirty) {
        buildThemeRamp(pal, 256);
        lastThemeGen = gen;
    }
    // "Grain": a scale on the ordered dither, 0 to 1.4, and exactly the
    // page's 0.7 attenuation at 50 because 350/500 rounds to the same float
    // as the literal 0.7f. The attenuation is there because most pixels are
    // on the flat floor, where full amplitude reads as a cross-hatch. Units
    // are sixteenths of a palette index, not pixels. ditherAmp caps at 16,
    // so the offset is at most round(16 * 1.4 * 16) = 358 either way.
    const bool ditherDirty = themeDirty || lastGrain != p[7];
    if (ditherDirty) {
        const float amp = ditherAmp(pal, 256) * (static_cast<float>(p[7] * 7) / 500.0f);
        for (int k = 0; k < 64; k++) {
            dith[k] = static_cast<int16_t>(lroundf((BAYER8[k] - 31.5f) * (amp * 16.0f / 31.5f)));
        }
        lastGrain = p[7];
    }
    // "Base light": the palette index of the unlit face, 24 to 126, and 66
    // at 50, which is the approved preview's floor. Integer arithmetic, so
    // the default is the same 66 the table was built from. The low end is
    // 24 and not 0 because the dither offset is subtracted from it and the
    // kernel's bit field takes the sum unclamped: 24 * 16 - 358 = 26 >= 0.
    const int floorIdx = p[6] <= 50 ? 24 + p[6] * 42 / 50 : 66 + (p[6] - 50) * 60 / 50;
    if (ditherDirty || lastTail != p[1] || lastGlow != p[2] || lastWidth != p[3] || lastFade != p[4] ||
        lastBase != p[6]) {
        // Single precision on host and device; the page's double arithmetic
        // can round a boundary sample one Q4 unit differently. The profile
        // shape and its final integer dither/shift are otherwise identical.
        const int tail = static_cast<int>(lroundf(TAIL * (0.55f + p[1] * 0.009f)));
        const int head = 148 + static_cast<int>(lroundf(p[2] * 0.8f));
        const float invTail = 1.0f / tail;
        // "Drop width": mix the middle cross-section toward the narrow or
        // the wide set. The factor is zero at 50, so each gain is the
        // middle value plus zero, which is the middle value.
        const float *gTo = p[3] <= 50 ? GAIN_NARROW : GAIN_WIDE;
        const float gMix = static_cast<float>(p[3] <= 50 ? 50 - p[3] : p[3] - 50) * (1.0f / 50.0f);
        float gain[4];
        for (int cls = 0; cls < 4; cls++) gain[cls] = GAIN_MID[cls] + (gTo[cls] - GAIN_MID[cls]) * gMix;
        // "Tail fade": how much of the tail's brightness sits right under
        // the head. The two weights are n/100 and (100 - n)/100, so at
        // n = 45 they are the float values of the literals 0.45f and 0.55f
        // the profile was written with. Their sum is 1 at every n, so the
        // sample at the head is the head whatever the fade is. n = 5 is a
        // short bright dash, n = 85 an evenly lit streak.
        const int fadeN = 5 + p[4] * 8 / 10;
        const float front = static_cast<float>(fadeN) / 100.0f;
        const float slope = static_cast<float>(100 - fadeN) / 100.0f;
        // Every class outside the streak is exactly the floor in Q4. Cache
        // its final RGB565 here, including Bayer, once per rebuild.
        for (int k = 0; k < 64; k++) {
            int idx = (floorIdx * 16 + dith[k]) >> 4;
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;
            background[k] = pal[idx];
        }
        for (int i = 0; i < PROFILE_N; i++) {
            const int u = (i - MAX_TAIL) & (CYC - 1);
            const int d = (CYC - u) & (CYC - 1);
            float v = 0.0f;
            if (i != FLOOR_SLOT && d <= tail) {
                const float f = 1.0f - d * invTail;
                v = (head - floorIdx) * f * f * (front + slope * f);
            } else if (i != FLOOR_SLOT && u > 0 && u <= BELOW) {
                const float f = 1.0f - u * (1.0f / BELOW);
                v = (head - floorIdx) * f * f;
            }
            for (int cls = 0; cls < 4; cls++) {
                const uint16_t q4 = static_cast<uint16_t>(lroundf((floorIdx + v * gain[cls]) * 16.0f));
                tabQ4[i * LANE + cls] = q4;
                tabQ4[i * LANE + 7 - cls] = q4;
            }
        }
        lastTail = p[1];
        lastGlow = p[2];
        lastWidth = p[3];
        lastFade = p[4];
        lastBase = p[6];
    }
    tablesValid = true;

    // Page: (tMs * speedMul(p[0]) * spd[lane % 3]) >> 8. Rates at
    // speed 50 are 5000/256, 6000/256, 7000/256 px/s; a cycle is 2048 px.
    // uint64 conversion preserves the bits needed after JavaScript's
    // ToInt32 wrap even at the uint32 millis limit, without an overflowing
    // float-to-int32 cast. Float rounding can move a late-uptime phase
    // relative to the browser's double, but host and device use this same
    // single-precision clock. No per-band state or accumulated time drift.
    //
    // "Speed spread" pulls the three rates apart about the middle one of 6.
    // 50.0f / 50.0f is exactly 1, so at the default the rates are the same
    // 5, 6 and 7 as before. 0 puts every lane on the middle rate, 100 gives
    // 3, 6 and 9. Every rate stays positive, so the cast stays in range.
    const float t = static_cast<float>(tMs) * speedMul(p[0]);
    const float sk = p[5] <= 50 ? static_cast<float>(p[5]) / 50.0f
                                : 1.0f + static_cast<float>(p[5] - 50) * (2.0f / 50.0f);
    uint32_t fall[3];
    for (int i = 0; i < 3; i++) {
        const float rate = 6.0f + (i - 1) * sk;
        fall[i] = static_cast<uint32_t>(static_cast<uint64_t>(t * rate) >> 8);
    }
    for (int lane = 0; lane < seedN; lane++) {
        const uint32_t ph = lane < w / LANE ? (seed[lane] - fall[lane % 3]) & (CYC - 1) : 0;
        lanePhase[lane] = static_cast<uint16_t>(ph);
    }
}

// A Q4 sample is floor * 16 at its lowest and head * 16 at its highest,
// because every cross-section gain is inside 0..1 and the profile term v
// is inside 0..(head - floor). "Base light" holds floor in 24..126 and
// "Head glow" holds head in 148..228, so a sample is 384..3648. ditherAmp
// caps at 16 and "Grain" scales it by at most 1.4, so the Q4 offset is at
// most round(16 * 1.4 * 16) = 358 either way. The sum is therefore
// 26..4006, strictly inside 0..4095 at every parameter setting; the page's
// clamp never changes it, which is what lets the kernel read the palette
// index straight out of bits 4..11. Keep the clamp explicit in the
// portable specification.
// GCC's may_alias permits packed stores into the uint16_t band buffer.
// BgAnim.h guarantees four-byte row alignment for normal calls. The
// halfword fallback also permits single odd-width rows at any alignment.
typedef uint32_t RainPair __attribute__((__may_alias__));

GM_ANIM_IRAM __attribute__((always_inline)) inline void rainRowScalar(uint16_t *out, const uint16_t *phase,
    const uint16_t *tab, const int16_t *off, const uint16_t *palette, const uint16_t *bg, int y, int w) {
    const unsigned yBias = (unsigned)y + 232u;
    const int groups = ((uintptr_t)out & 3u) == 0 ? w >> 3 : 0;
    // Pack once per row. Background lanes then have only four stores,
    // with no per-pixel loads, palette gathers or index arithmetic.
    const uint32_t bg0 = (uint32_t)bg[0] | ((uint32_t)bg[1] << 16);
    const uint32_t bg1 = (uint32_t)bg[2] | ((uint32_t)bg[3] << 16);
    const uint32_t bg2 = (uint32_t)bg[4] | ((uint32_t)bg[5] << 16);
    const uint32_t bg3 = (uint32_t)bg[6] | ((uint32_t)bg[7] << 16);
    for (int lane = 0; lane < groups; lane++) {
        const unsigned slot = (phase[lane] + yBias) & 2047u;
        RainPair *packed = (RainPair *)(out + lane * 8);
        if (slot > 247u) {
            // Exactly one write per pixel, without a fill/overdraw pass.
            packed[0] = bg0;
            packed[1] = bg1;
            packed[2] = bg2;
            packed[3] = bg3;
        } else {
            for (int j = 0; j < 8; j += 2) {
                int a = (tab[slot * 8 + j] + off[j]) >> 4;
                int b = (tab[slot * 8 + j + 1] + off[j + 1]) >> 4;
                if (a < 0) a = 0; else if (a > 255) a = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;
                packed[j >> 1] = (uint32_t)palette[a] | ((uint32_t)palette[b] << 16);
            }
        }
    }
    // A trailing partial lane uses bounded halfword stores, never a full
    // vector access. The same path handles halfword-only aligned output.
    for (int x = groups * 8; x < w; x += 8) {
        const unsigned slot = (phase[x >> 3] + yBias) & 2047u;
        const int count = w - x < 8 ? w - x : 8;
        for (int j = 0; j < count; j++) {
            if (slot > 247u) out[x + j] = bg[j];
            else {
                int idx = (tab[slot * 8 + j] + off[j]) >> 4;
                if (idx < 0) idx = 0; else if (idx > 255) idx = 255;
                out[x + j] = palette[idx];
            }
        }
    }
}

GM_ANIM_IRAM void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const int k = (y & 7) * 8;
        rainRowScalar(dst + static_cast<size_t>(row) * w, lanePhase, tabQ4, dith + k, pal, background + k, y, w);
    }
}

#if GM_BGANIM_RAIN_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
static_assert(CYC == 2048 && MAX_TAIL == 232 && FLOOR_SLOT == 247 && LANE == 8,
              "rainRowAsm encodes the cycle, rotation, floor slot and lane width");

// GCC 14.2's -O2 bandRef was inspected again after the lane restructure:
// .L10 branches per lane, keeps three background words in registers and
// reloads the fourth from the stack, then writes four words. Its .L9
// hardware loop takes 26 instructions per active pair and spills live
// background words around it. The QEMU compiler_baseline.h retains the
// original 22-instruction pixel-loop transcription as an extra oracle.
//
// The edge here is structural: load the background into q2 once per row,
// then skip all profile and palette reads for slots 248..2047. One VST
// writes an entire background lane. Active lanes share one phase and one
// contiguous cross-section, so one VLD and one VADDS.S16 replace eight
// profile/dither loads and additions. MOVI.32.A extracts packed pairs
// directly from q0; no scratch-index stores or reloads.
// EXTUI selects Q4 bits 4..11 and 20..27. The proven 877..3827 sum range
// makes that identical to the reference's shift and clamp.
//
// Palette reads remain scalar gathers, interleaved high then low so SLLI
// hides the low load's latency and the high load has an independent load
// before its consumer. One S32I writes each active pair. LOOPNEZ executes
// 9 instructions per background lane (1.125/pixel), or 51 per active lane
// (6.375/pixel), with no exposed load-use gap in the documented model.
// MOVI occupies the phase load's dependency gap; the output increment
// occupies the active VLD-to-VADDS gap. A taken branch still costs cycles.
// At 248/2048 active slots the weighted issue count is 1.761/pixel,
// excluding row setup, branch penalties and memory stalls. This is not
// device timing; measure the default-on kernel against bandRef there.
//
// Every VLD/VST spans an aligned full lane. Allocation and 16-byte row
// strides align sources; the guard also handles a PSRAM fallback. Rows
// whose output starts at 4/8/12 mod 16 use the same sparse scalar lane
// path as bandRef. A partial final lane uses bounded halfword accesses.
// No pixel is filled and then overwritten, including these fallbacks.
//
// q0/q1/q2 are free: GCC never allocates q registers and has no q clobber
// syntax. MOVI.32.A selectors 0..3 were separately probed under QEMU
// before adoption. SAR is untouched. Never write CPENABLE here: FreeRTOS
// owns lazy coprocessor enable and context saves on the render task.
GM_ANIM_IRAM __attribute__((noinline)) void rainRowAsm(uint16_t *out, const uint16_t *phase,
    const uint16_t *tab, const int16_t *off, const uint16_t *palette, const uint16_t *bg, int y, int w) {
    const unsigned yBias = (unsigned)y + 232u;
    const int groups = (((uintptr_t)tab | (uintptr_t)off | (uintptr_t)bg | (uintptr_t)out) & 15u) == 0 ? w >> 3 : 0;
    if (groups > 0) {
        unsigned hi, lo, cap;
        const int16_t *ditherRow = off;
        const uint16_t *backgroundRow = bg;
        // Predecrement only inside asm, not C pointer arithmetic. The
        // in-loop increment then fills the VLD-to-VADDS dependency gap;
        // no access occurs before out, and out returns at the last group.
        asm volatile("ee.vld.128.ip q1, %[dith], 0\n"
                     "ee.vld.128.ip q2, %[bg], 0\n"
                     "addi    %[out], %[out], -16\n"
                     "loopnez %[n], 1f\n"
                     "l16ui   %[hi], %[phase], 0\n"
                     "movi    %[cap], 247\n"
                     "add     %[hi], %[hi], %[yb]\n"
                     "extui   %[hi], %[hi], 0, 11\n"
                     // Keep the conditional target close: a jump over all
                     // four gathers would need assembler branch relaxation.
                     "bgeu    %[cap], %[hi], 2f\n"
                     "addi    %[out], %[out], 16\n"
                     "ee.vst.128.ip q2, %[out], 0\n"
                     "j       3f\n"
                     "2:\n"
                     "slli    %[hi], %[hi], 4\n"
                     "add     %[hi], %[hi], %[tab]\n"
                     "ee.vld.128.ip q0, %[hi], 0\n"
                     "addi    %[out], %[out], 16\n"
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
                     "3:\n"
                     // Branches must join before a real final instruction:
                     // jumping straight to LEND bypasses the LOOP back edge.
                     "addi    %[phase], %[phase], 2\n"
                     "1:\n"
                     : [out] "+&r"(out), [phase] "+&r"(phase), [dith] "+&r"(ditherRow), [bg] "+&r"(backgroundRow),
                       [hi] "=&r"(hi), [lo] "=&r"(lo), [cap] "=&r"(cap)
                     : [tab] "r"(tab), [pal] "r"(palette), [yb] "r"(yBias), [n] "r"(groups)
                     : "memory");
        out += 8;
    }
    const int remaining = w - groups * 8;
    if (remaining > 0) rainRowScalar(out, phase, tab, off, palette, bg, y, remaining);
}
#endif

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_RAIN_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    (void)tMs;
    (void)p;
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const int k = (y & 7) * 8;
        rainRowAsm(dst + static_cast<size_t>(row) * w, lanePhase, tabQ4, dith + k, pal, background + k, y, w);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(tabQ4, PROFILE_BYTES);
    releaseTable(lanePhase, static_cast<size_t>(seedN) * sizeof(uint16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(pal, 256 * sizeof(uint16_t));
    releaseTable(background, 64 * sizeof(uint16_t));
    releaseTable(seed, static_cast<size_t>(seedN) * sizeof(uint16_t));
    allocW = seedN = 0;
    lastTail = lastGlow = lastWidth = lastFade = lastBase = lastGrain = -1;
    lastThemeGen = 0xFFFFFFFFu;
    tablesValid = false;
}

} // namespace

extern const BgAnimation bg_anim_rain;
const BgAnimation bg_anim_rain = {
    "rain",
    "Rain",
    {{"speed", "Speed", 50},
     {"tail", "Tail length", 50},
     {"glow", "Head glow", 55},
     {"width", "Drop width", 50},
     {"fade", "Tail fade", 50},
     {"spread", "Speed spread", 50},
     {"base", "Base light", 50},
     {"grain", "Grain", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
