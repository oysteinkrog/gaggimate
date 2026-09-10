#ifndef GAGGIMATE_SIM

// "Shafts": soft light fanning from (240, -160), swaying and breathing as
// it fades down the face. This is entry 'shafts' in anim_bench.html: a fixed
// angle/distance map indexes a 128 x 32 RGB565 cell table rebuilt each frame.
// Three shared-sine harmonics make the ray profile. Two successive Q8
// products apply the distance fade and the breathing gain before the palette
// gather. Both truncations matter; combining the products changes the image.
//
// The page header describes an older top cap and floor. Its current init()
// actually builds a monotone 178..82 fade with exponent 1.15. That formula,
// including Bayer8 dithering of BOTH map coordinates, is the reference here.
// Coordinates stay in the page's fixed pixel space even at smaller render
// sizes: SX/SY and the distance bounds do not scale with w/h in its init().

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

#ifndef GM_BGANIM_SHAFTS_ASM
#define GM_BGANIM_SHAFTS_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int A_N = 128;
constexpr int D_N = 32;
constexpr int CELL_N = A_N * D_N;
constexpr float SX = 240.0f, SY = -160.0f;
constexpr float PI = 3.14159265358979323846f;
constexpr float SPAN = 100.0f * PI / 180.0f; // 100 degree angular window
constexpr float D_LO = 160.0f, D_HI = 700.0f;

uint16_t *map = nullptr;
uint16_t *tex = nullptr;
uint8_t *workRaw = nullptr;
uint16_t *palette = nullptr;
uint8_t *ray = nullptr;
uint16_t *fall = nullptr;
uint16_t *broadcast = nullptr;
uint16_t *indices = nullptr;
const int16_t *sl = nullptr; // borrowed shared sine table, never released here
int allocW = 0, allocH = 0;
int lastBrightness = -1;
uint32_t lastThemeGen = 0xFFFFFFFF;

// Table budget at 480 x 480:
//   tex       8,192 B slab, the random gather read for every screen pixel.
//   map     460,800 B PSRAM, one sequential uint16 stream per frame. The
//                     page's group-A header specifies this bulk placement.
//   palette     512 B PSRAM, frame-time cell gathers only.
//   ray         128 B PSRAM, the page's byte-valued angular profile.
//   fall         64 B PSRAM, the same 32 byte-valued fades widened to uint16.
//   broadcast    32 B PSRAM, eight copies each of gain and current ray.
//   indices      64 B PSRAM, one angle's 32 Q8 product results for PIE.
// The last five views share an 800 B block plus 15 B alignment allowance.
// No per-row table and no private sine table. Slab use is 8,192 of 9,216 B.
constexpr int WORK_BYTES = 800;
constexpr int WORK_ALLOC = WORK_BYTES + 15;
void release();

bool init(int w, int h) {
    if (w <= 0 || h <= 0) {
        release();
        return false;
    }
    if (map != nullptr && w == allocW && h == allocH) {
        return true;
    }
    release();
    allocW = w;
    allocH = h;
    sl = sinLut();
    if (sl == nullptr) {
        release();
        return false;
    }
    tex = static_cast<uint16_t *>(allocHot(CELL_N * sizeof(uint16_t)));
    if (tex == nullptr) {
        release();
        return false;
    }
    workRaw = static_cast<uint8_t *>(alloc(WORK_ALLOC));
    if (workRaw == nullptr) {
        release();
        return false;
    }
    uint8_t *work = reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(workRaw) + 15u) & ~uintptr_t(15));
    palette = reinterpret_cast<uint16_t *>(work);
    ray = work + 512;
    fall = reinterpret_cast<uint16_t *>(work + 640);
    broadcast = reinterpret_cast<uint16_t *>(work + 704);
    indices = reinterpret_cast<uint16_t *>(work + 736);
    map = static_cast<uint16_t *>(alloc(static_cast<size_t>(w) * h * sizeof(uint16_t)));
    if (map == nullptr) {
        release();
        return false;
    }

    for (int d = 0; d < D_N; d++) {
        const float u = static_cast<float>(d) / (D_N - 1);
        fall[d] = static_cast<uint16_t>(82.0f + 96.0f * powf(1.0f - u, 1.15f) + 0.5f);
    }
    const float angK = (A_N - 1) / SPAN;
    const float half = SPAN * 0.5f;
    const float distK = (D_N - 1) / (D_HI - D_LO);
    for (int y = 0; y < h; y++) {
        const float dy = static_cast<float>(y) - SY;
        for (int x = 0; x < w; x++) {
            const float dx = static_cast<float>(x) - SX;
            // Same signed cell fraction for angle and distance, not palette
            // dither: [-31.5, 31.5]/64, indexed by absolute pixel coordinates.
            const float bay = (static_cast<float>(BAYER8[(y & 7) * 8 + (x & 7)]) - 31.5f) * (1.0f / 64.0f);
            int a = static_cast<int>(floorf((atan2f(dx, dy) + half) * angK + bay));
            int d = static_cast<int>(floorf((sqrtf(dx * dx + dy * dy) - D_LO) * distK + bay));
            a = a < 0 ? 0 : (a >= A_N ? A_N - 1 : a);
            d = d < 0 ? 0 : (d >= D_N ? D_N - 1 : d);
            map[static_cast<size_t>(y) * w + x] = static_cast<uint16_t>(a * D_N + d);
        }
    }
    return true;
}

// Math.round for positive phase, then modulo one 1024-entry sine turn.
// tMs * speedMul * 0.100 can exceed INT32_MAX before uptime wraps. Widen
// before narrowing to preserve defined modulo arithmetic at every uint32 tMs.
// Single-precision frame/init math can move a cell boundary or phase tie
// relative to JavaScript double math; the Q8 arithmetic itself is identical.
uint32_t phase1024(float phase) {
    return static_cast<uint32_t>(static_cast<uint64_t>(floorf(phase + 0.5f))) & (SIN_N - 1);
}

#if GM_BGANIM_SHAFTS_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14's bandRef .L3 loop, transcribed before scheduling the pair:
//   l16ui idx,src,0; addi src,src,2; addx2 idx,idx,cells;
//   l16ui idx,idx,0; s16i idx,out,0; addi out,out,2.
// Six instructions/pixel and a load-use interlock at the last load/store.
// The paired schedule below takes 11 instructions/two pixels (5.5/pixel),
// with an independent instruction after EVERY load and one s32i per pair.
// Its issue floor is 5.5 cycles/pixel before PSRAM streaming and call costs,
// versus roughly 7 for that scalar schedule. This is not a device timing.
// The map streams from PSRAM; cells stay in the slab. PIE has no uint16
// gather, so vector decoding would add scratch traffic without doing useful
// arithmetic. Keep the indices in registers, as the device precedents advise.
//
// out is 4-byte aligned by BgAnim.h. src needs only 2-byte alignment, which
// also covers absolute odd rows of a 233-wide map. n is a nonnegative pixel
// count; the scalar tail handles odd n without reading or writing padding.
// band() passes the entire contiguous band, paying one wrapper per call.
GM_ANIM_IRAM __attribute__((noinline)) void shaftsGatherAsm(uint16_t *out, const uint16_t *src,
                                                          const uint16_t *cells, int n) {
    int i0, i1;
    const int pairs = n >> 1;
    asm volatile("loopnez %[pairs], 1f\n"
                 "l16ui   %[i0], %[src], 0\n"
                 "l16ui   %[i1], %[src], 2\n"
                 "addx2   %[i0], %[i0], %[cells]\n"
                 "addx2   %[i1], %[i1], %[cells]\n"
                 "l16ui   %[i0], %[i0], 0\n"
                 "l16ui   %[i1], %[i1], 0\n"
                 "addi    %[src], %[src], 4\n"
                 "slli    %[i1], %[i1], 16\n"
                 "or      %[i0], %[i0], %[i1]\n"
                 "s32i    %[i0], %[out], 0\n"
                 "addi    %[out], %[out], 4\n"
                 "1:\n"
                 : [out] "+&r"(out), [src] "+&r"(src), [i0] "=&r"(i0), [i1] "=&r"(i1)
                 : [cells] "r"(cells), [pairs] "r"(pairs)
                 : "memory");
    if (n & 1) {
        *out = cells[*src];
    }
}

// Build one angle's 32 indices, preserving the page's TWO truncations:
// index[d] = (ray * ((fall[d] * gain) >> 8)) >> 8.
// All operands are byte-valued but widened to uint16 lanes. Even the wider
// synthetic 0..255 test range has intermediate products <=65025, so neither
// multiply loses bits beyond the specified SAR=8 shifts. In production,
// fall<=178, gain<=232 and ray<=255 imply index<=160, inside palette[256].
//
// All three spans start at aligned offsets in the explicitly aligned work
// allocation. Each vld/vst spans exactly 16 bytes and advances by 16; there
// is no overread or masked-address prefix. Four independent vectors fill
// load/multiply latency gaps. This is 19 instructions/32 cells, including
// the two broadcast loads and ssai, before the scalar palette gathers.
// GCC never allocates q0..q7, so they need no unsupported q clobber syntax.
// This leaf sets SAR and uses q0..q5 only. It never writes CPENABLE; the
// render task's lazy coprocessor exception owns FPU/PIE context activation.
GM_ANIM_IRAM __attribute__((noinline)) void shaftsIndicesAsm(uint16_t *out, const uint16_t *fade,
                                                           const uint16_t *factors) {
    asm volatile("ee.vld.128.ip q4, %[factors], 16\n"
                 "ee.vld.128.ip q5, %[factors], 16\n"
                 "ee.vld.128.ip q0, %[fade], 16\n"
                 "ee.vld.128.ip q1, %[fade], 16\n"
                 "ee.vld.128.ip q2, %[fade], 16\n"
                 "ee.vld.128.ip q3, %[fade], 16\n"
                 "ssai 8\n"
                 "ee.vmul.u16 q0, q0, q4\n"
                 "ee.vmul.u16 q1, q1, q4\n"
                 "ee.vmul.u16 q2, q2, q4\n"
                 "ee.vmul.u16 q3, q3, q4\n"
                 "ee.vmul.u16 q0, q0, q5\n"
                 "ee.vmul.u16 q1, q1, q5\n"
                 "ee.vmul.u16 q2, q2, q5\n"
                 "ee.vmul.u16 q3, q3, q5\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "ee.vst.128.ip q1, %[out], 16\n"
                 "ee.vst.128.ip q2, %[out], 16\n"
                 "ee.vst.128.ip q3, %[out], 16\n"
                 : [out] "+&r"(out), [fade] "+&r"(fade), [factors] "+&r"(factors)
                 :
                 : "memory");
}
#endif

void frame(uint32_t tMs, int, int, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (lastBrightness != p[2] || lastThemeGen != gen) {
        // pa_bright: channel brightness Q8, 80..256. Scale the RGB888 ramp
        // before RGB565 quantization, exactly as pa_ramp32/themeRamp do.
        const int bright = 80 + (static_cast<int>(p[2]) * 176 + 50) / 100;
        buildThemeRamp(palette, static_cast<uint16_t>(bright));
        lastBrightness = p[2];
        lastThemeGen = gen;
    }
    const float t = static_cast<float>(tMs) * speedMul(p[0]);
    const int k2 = 16 + (static_cast<int>(p[1]) * 16 + 50) / 100; // sharp harmonic, 16..32
    // Sway: +/-60 sine-table units, 20 s cycle, initial phase 1.1 rad.
    // floor(v+0.5) preserves JS Math.round's negative-half tie rule.
    const int sway = static_cast<int>(floorf(60.0f * sinf(t * (2.0f * PI / 20000.0f) + 1.1f) + 0.5f));
    const uint32_t p2 = phase1024(t * 0.100f); // 100 units/s, a shaft width per 15 s in the page
    const uint32_t p3 = phase1024(t * 0.028f); // 28 units/s, independent broad harmonics
    const int gain = 200 + (sl[(phase1024(t * (1024.0f / 9000.0f)) + 300) & (SIN_N - 1)] >> 4);
    // gain is 168..232 with a 9 s breath and a 300-unit initial phase.
    for (int a = 0; a < A_N; a++) {
        // Exact integer Math.round(a*k2/3) and Math.round(a*k2/8).
        const int s = sl[((a * k2 + 1) / 3 + sway + p3) & (SIN_N - 1)] +
                      (sl[(a * k2 + p2) & (SIN_N - 1)] >> 1) +
                      sl[((a * k2 + 4) / 8 + p3) & (SIN_N - 1)];
        const int v = (s + 1280) >> 3; // +/-1280 sum -> 0..320, clipped to byte profile
        ray[a] = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
#if GM_BGANIM_SHAFTS_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    for (int k = 0; k < 8; k++) {
        broadcast[k] = static_cast<uint16_t>(gain);
    }
    for (int a = 0; a < A_N; a++) {
        for (int k = 0; k < 8; k++) {
            broadcast[8 + k] = ray[a];
        }
        shaftsIndicesAsm(indices, fall, broadcast);
        shaftsGatherAsm(tex + a * D_N, indices, palette, D_N);
    }
#else
    for (int a = 0; a < A_N; a++) {
        for (int d = 0; d < D_N; d++) {
            tex[a * D_N + d] = palette[(ray[a] * ((fall[d] * gain) >> 8)) >> 8];
        }
    }
#endif
}

GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint16_t *__restrict src = map + static_cast<size_t>(y0) * w;
    const uint16_t *__restrict cells = tex;
    for (int n = rows * w; n > 0; n--) {
        *dst++ = cells[*src++];
    }
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_SHAFTS_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    (void)tMs;
    (void)p;
    shaftsGatherAsm(dst, map + static_cast<size_t>(y0) * w, tex, rows * w);
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(map, static_cast<size_t>(allocW) * allocH * sizeof(uint16_t));
    releaseTable(workRaw, WORK_ALLOC);
    releaseTable(tex, CELL_N * sizeof(uint16_t));
    // These are views, not owners. Only workRaw is returned to the allocator.
    palette = fall = broadcast = indices = nullptr;
    ray = nullptr;
    sl = nullptr;
    allocW = allocH = 0;
    lastBrightness = -1;
    lastThemeGen = 0xFFFFFFFF;
}

} // namespace

extern const BgAnimation bg_anim_shafts;
const BgAnimation bg_anim_shafts = {
    "shafts",
    "Shafts",
    {{"speed", "Speed", 50},
     {"density", "Shaft count", 50},
     {"brightness", "Brightness", 66},
     {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
