#ifndef GAGGIMATE_SIM

// "Kaleido": six mirrored pairs of soft, slowly reforming blotches. Port of
// entry 'kaleido' in tools/animbench/web/anim_bench.html. A fixed polar map
// folds the face into a 30 degree half-wedge and 64 radial cells. Each frame
// samples one drifting, tileable noise patch into a 64 x 64 field, smooths it
// along the angle, applies a ring vignette, and resolves the theme colours.
// bandRef() then has only one streamed map read and one hot colour gather.
//
// The page's executable constants are the spec. Its older prose mentions a
// 128-square source, two samples, levels 40..209 and 30/40 second wraps. The
// approved render actually uses one 256-square source, levels 44..175, and
// offsets of 3.2 and 1.6 source units/second at Speed 50. Keep those numbers.
// Angular source position is Q9, radial position Q3, smoothing divides by 4
// BEFORE the Q8 vignette multiply. Dither changes the map's cell selection,
// not the palette: (Bayer8 - 31.5)/64, suppressed at both mirror axes.
// Float32 init math can select a neighbouring cell at a rounding boundary;
// Xtensa's fused multiply-add can also round noise differently from the
// host. Both device render paths share those tables, so this never weakens
// their required pixel-exact parity. No floating point runs in band().

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

// Default on after assembly and QEMU parity checks. Device parity and an
// A/B timing against bandRef remain the release gate for a speed claim.
#ifndef GM_BGANIM_KALEIDO_ASM
#define GM_BGANIM_KALEIDO_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int A_N = 64;
constexpr int R_N = 64;
constexpr int SRC = 256;
constexpr int CELLS = A_N * R_N;
constexpr float PI = 3.14159265358979323846f;
constexpr float WEDGE = PI / 3.0f;
constexpr float HALF = PI / 6.0f;
// This is a fixed 240 px radius in the page's init(w,h), not min(w,h)/2.
constexpr float R_OUT = 240.0f;
constexpr size_t RAW_BYTES = CELLS + 15; // align the byte streams for PIE
constexpr size_t RIM_BYTES = R_N + 15;

uint16_t *map = nullptr;     // w*h indices, sequential PSRAM stream
uint16_t *tex = nullptr;     // 4096 RGB565 colours, random gathers in the slab
uint8_t *src = nullptr;      // 256*256 seeded noise, frame() only, PSRAM
uint8_t *rawOwner = nullptr; // owners include the 15-byte alignment allowance
uint8_t *rimOwner = nullptr;
uint8_t *raw = nullptr;      // aligned alias: 64*64 byte levels, PSRAM
uint8_t *rimDim = nullptr;   // aligned alias: 64 byte gains, PSRAM
uint8_t *lev = nullptr;      // 256 byte level compression, PSRAM
uint16_t *palette = nullptr; // 256 RGB565 entries, frame() only, PSRAM
int allocW = 0, allocH = 0;
int lastBrightness = -1;
uint32_t lastThemeGen = 0xFFFFFFFF;

// At 480x480: tex 8192 B is the entire hot-slab allocation, below 9216 B.
// map 460800 B is the bulk sequential stream explicitly described by the
// page's shared pa_* header, so it belongs in PSRAM rather than the slab.
// Other PSRAM tables: src 65536 B, raw 4096+15 B, rimDim 64+15 B, lev 256 B,
// palette 512 B. The noise lattice is a temporary 1024 B PSRAM allocation
// released before init succeeds. No table is hidden in static DRAM.
void release();

uint8_t *align16(uint8_t *p) {
    return reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(p) + 15u) & ~uintptr_t(15u));
}

// The page uses mulberry32, not the firmware's shared xorshift noise. All
// products here wrap modulo 2^32, including the 0x6d2b79f5 Weyl increment.
uint32_t noiseRand(uint32_t &seed) {
    seed += 0x6d2b79f5u;
    uint32_t t = (seed ^ (seed >> 15)) * (1u | seed);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return t ^ (t >> 14);
}

float smooth(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

void buildNoise(float *lattice) {
    uint32_t seed = 0x5eed21u;
    for (int i = 0; i < 256; i++) {
        // Float32 lattice storage matches the JS Float32Array, even though
        // JS subsequently evaluates its interpolation in double precision.
        lattice[i] = static_cast<float>(noiseRand(seed)) * (1.0f / 4294967296.0f);
    }
    for (int y = 0; y < SRC; y++) {
        const int iy = y >> 4; // 16 x 16 lattice, 16 source units per cell
        const float fy = smooth((y & 15) * (1.0f / 16.0f));
        for (int x = 0; x < SRC; x++) {
            const int ix = x >> 4;
            const float fx = smooth((x & 15) * (1.0f / 16.0f));
            const float v00 = lattice[iy * 16 + ix];
            const float v10 = lattice[iy * 16 + ((ix + 1) & 15)];
            const float v01 = lattice[((iy + 1) & 15) * 16 + ix];
            const float v11 = lattice[((iy + 1) & 15) * 16 + ((ix + 1) & 15)];
            const float a = v00 + (v10 - v00) * fx;
            const float b = v01 + (v11 - v01) * fx;
            src[y * SRC + x] = static_cast<uint8_t>((a + (b - a) * fy) * 255.0f + 0.5f);
        }
    }
}

bool init(int w, int h) {
    if (tex != nullptr && allocW == w && allocH == h) {
        return true;
    }
    release();
    allocW = w;
    allocH = h;
    tex = static_cast<uint16_t *>(allocHot(CELLS * sizeof(uint16_t)));
    map = static_cast<uint16_t *>(alloc(static_cast<size_t>(w) * h * sizeof(uint16_t)));
    src = static_cast<uint8_t *>(alloc(SRC * SRC));
    rawOwner = static_cast<uint8_t *>(alloc(RAW_BYTES));
    rimOwner = static_cast<uint8_t *>(alloc(RIM_BYTES));
    lev = static_cast<uint8_t *>(alloc(256));
    palette = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    float *lattice = static_cast<float *>(alloc(256 * sizeof(float)));
    if (!tex || !map || !src || !rawOwner || !rimOwner || !lev || !palette || !lattice) {
        releaseTable(lattice, 256 * sizeof(float));
        release();
        return false;
    }
    raw = align16(rawOwner);
    rimDim = align16(rimOwner);
    buildNoise(lattice);
    releaseTable(lattice, 256 * sizeof(float));

    for (int r = 0; r < R_N; r++) {
        const float u = r * (1.0f / (R_N - 1));
        const float d = (u - 0.52f) / 0.52f;
        const float v = 1.0f - d * d;
        // Ring gain peaks at radius 0.52, with a 104/256 central floor.
        rimDim[r] = static_cast<uint8_t>(104.0f + 132.0f * powf(v > 0.0f ? v : 0.0f, 0.6f) + 0.5f);
    }
    for (int v = 0; v < 256; v++) {
        lev[v] = static_cast<uint8_t>(44 + ((v * 132) >> 8));
    }
    const float cx = w * 0.5f - 0.5f, cy = h * 0.5f - 0.5f;
    for (int y = 0; y < h; y++) {
        const float dy = y - cy;
        for (int x = 0; x < w; x++) {
            const float dx = x - cx;
            const float r = sqrtf(dx * dx + dy * dy);
            const float th = atan2f(dy, dx) + PI + WEDGE * 0.25f;
            const float fold = fabsf(fmodf(th, WEDGE) - HALF);
            const float af = fold * ((A_N - 1) / HALF);
            const bool edge = af < 1.0f || af > A_N - 2;
            const float bay = edge ? 0.0f : (BAYER8[(y & 7) * 8 + (x & 7)] - 31.5f) * (1.0f / 64.0f);
            int ac = static_cast<int>(floorf(af + bay));
            int rc = static_cast<int>(floorf(r * ((R_N - 1) / R_OUT) + bay));
            ac = ac < 0 ? 0 : ac > A_N - 1 ? A_N - 1 : ac;
            rc = rc < 0 ? 0 : rc > R_N - 1 ? R_N - 1 : rc;
            map[static_cast<size_t>(y) * w + x] = static_cast<uint16_t>(ac * R_N + rc);
        }
    }
    return true;
}

#if GM_BGANIM_KALEIDO_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14's bandRef loop was transcribed before changing the schedule:
//   l16ui idx,in,0; addi in,in,2; addx2 idx,idx,cells;
//   l16ui idx,idx,0; s16i idx,out,0; addi out,out,2.
// Six instructions/pixel, hardware LOOP, one immediate load-to-store use.
// Four independent gathers below take 20 instructions/quad (5/pixel), with
// no immediate load-use dependency and two aligned S32I stores. Both input
// and output walk forward; no stack scratch or PIE decode is paid per pixel.
// The source only needs halfword alignment, including odd-width map rows.
// out must be 4-byte aligned, the BgAnim.h contract. This also supports
// in == out for resolving the PIE-built texture indices in place.
// A hot-table, no-cache-miss issue estimate is 5 cycles/pixel versus about
// 7 for the scalar reference. PSRAM stalls and device timing decide the win.
GM_ANIM_IRAM __attribute__((noinline)) void kaleidoGatherAsm(uint16_t *out, const uint16_t *in,
                                                           const uint16_t *cells, int nQuads) {
    uint32_t t0, t1, t2, t3;
    asm volatile("loopnez %[n], 1f\n"
                 "l16ui   %[t0], %[in], 0\n"
                 "l16ui   %[t1], %[in], 2\n"
                 "addx2   %[t0], %[t0], %[cells]\n"
                 "addx2   %[t1], %[t1], %[cells]\n"
                 "l16ui   %[t2], %[in], 4\n"
                 "l16ui   %[t3], %[in], 6\n"
                 "l16ui   %[t0], %[t0], 0\n"
                 "l16ui   %[t1], %[t1], 0\n"
                 "addx2   %[t2], %[t2], %[cells]\n"
                 "addx2   %[t3], %[t3], %[cells]\n"
                 "l16ui   %[t2], %[t2], 0\n"
                 "l16ui   %[t3], %[t3], 0\n"
                 "slli    %[t1], %[t1], 16\n"
                 "or      %[t0], %[t0], %[t1]\n"
                 "slli    %[t3], %[t3], 16\n"
                 "or      %[t2], %[t2], %[t3]\n"
                 "s32i    %[t0], %[out], 0\n"
                 "s32i    %[t2], %[out], 4\n"
                 "addi    %[in], %[in], 8\n"
                 "addi    %[out], %[out], 8\n"
                 "1:\n"
                 : [out] "+&r"(out), [in] "+&r"(in), [t0] "=&r"(t0), [t1] "=&r"(t1),
                   [t2] "=&r"(t2), [t3] "=&r"(t3)
                 : [cells] "r"(cells), [n] "r"(nQuads)
                 : "memory");
}

// Sixteen cells per iteration, two groups of eight unsigned 16-bit lanes.
// ZIP against zero widens the byte inputs. Saturating adds cannot saturate:
// even synthetic 0..255 inputs sum to at most 1020. With SAR=8, multiply
// by 64 implements the page's first >>2 exactly, then multiply by the
// vignette implements its >>8. Combining those shifts would round wrongly.
// The eight 64s are constructed in q7 without another allocated table.
//
// All spans are 16-byte aligned by construction: raw/rimDim are aligned
// aliases into overallocations, tex is allocHot, angular rows are 64 bytes,
// and the loop walks whole vectors. The caller checks tex alignment too
// because allocHot can fall back to PSRAM if another animation is resident.
// q0..q7 are compiler-unallocated PIE registers, so no q clobber syntax is
// needed. SAR belongs to the normal task context. Never write CPENABLE:
// FreeRTOS must perform CP3's lazy enable and save the previous owner.
// The independent low/high halves separate each vector load or multiply
// from its consumer. Body: 24 instructions/16 cells, 1.5/cell, followed
// by the shared 5/cell scalar palette gather. GCC's fused loop was 18/cell.
GM_ANIM_IRAM __attribute__((noinline)) void kaleidoSmoothAsm(uint16_t *out, const uint8_t *lo,
                                                           const uint8_t *mid, const uint8_t *hi,
                                                           const uint8_t *rim, int n16) {
    const uint32_t quarters = 0x00400040u; // two unsigned 16-bit copies of 64
    asm volatile("ee.movi.32.q q7, %[quarters], 0\n"
                 "ee.movi.32.q q7, %[quarters], 1\n"
                 "ee.movi.32.q q7, %[quarters], 2\n"
                 "ee.movi.32.q q7, %[quarters], 3\n"
                 "ssai 8\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[lo], 16\n"
                 "ee.vld.128.ip q1, %[mid], 16\n"
                 "ee.vld.128.ip q2, %[hi], 16\n"
                 "ee.zero.q q3\n"
                 "ee.vzip.8 q0, q3\n"
                 "ee.zero.q q4\n"
                 "ee.vzip.8 q1, q4\n"
                 "ee.zero.q q5\n"
                 "ee.vzip.8 q2, q5\n"
                 "ee.vadds.s16 q1, q1, q1\n"
                 "ee.vadds.s16 q4, q4, q4\n"
                 "ee.vadds.s16 q0, q0, q2\n"
                 "ee.vadds.s16 q3, q3, q5\n"
                 "ee.vadds.s16 q0, q0, q1\n"
                 "ee.vadds.s16 q3, q3, q4\n"
                 "ee.vmul.u16 q0, q0, q7\n"
                 "ee.vmul.u16 q3, q3, q7\n"
                 "ee.vld.128.ip q1, %[rim], 16\n"
                 "ee.zero.q q2\n"
                 "ee.vzip.8 q1, q2\n"
                 "ee.vmul.u16 q0, q0, q1\n"
                 "ee.vmul.u16 q3, q3, q2\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "ee.vst.128.ip q3, %[out], 16\n"
                 "1:\n"
                 : [out] "+&r"(out), [lo] "+&r"(lo), [mid] "+&r"(mid), [hi] "+&r"(hi), [rim] "+&r"(rim)
                 : [quarters] "r"(quarters), [n] "r"(n16)
                 : "memory");
}
#endif

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    const uint32_t gen = themeGen();
    if (lastBrightness != p[2] || lastThemeGen != gen) {
        // pa_bright is 80 + Math.round(p[2]*176/100), a channel scale on
        // the theme ramp before RGB565 quantization, not an index scale.
        buildThemeRamp(palette, 80 + (static_cast<int>(p[2]) * 176 + 50) / 100);
        lastBrightness = p[2];
        lastThemeGen = gen;
    }
    const float spd = speedMul(p[0]);
    // Rebuild from time just as the page does, so arbitrary frame order is
    // deterministic. Float32 can round an offset differently from JS after
    // long uptime; it stays far below UINT32_MAX even at millis() wrap.
    const uint32_t oy = static_cast<uint32_t>(tMs * 0.0032f * spd + 0.5f);
    const uint32_t ox = static_cast<uint32_t>(tMs * 0.0016f * spd + 0.5f);
    const uint32_t scA = 2 + (static_cast<uint32_t>(p[1]) * 2 + 50) / 100;
    const uint32_t scR = 4 + (static_cast<uint32_t>(p[1]) * 4 + 50) / 100;
    for (int a = 0; a < A_N; a++) {
        const uint32_t aSh = (a + ox) * scA;
        for (int r = 0; r < R_N; r++) {
            // Unsigned wrap preserves JS's bitwise ToInt32 product at long
            // uptimes. Only bits 9..16 survive >>9 and &255, so the sign
            // extension of JS's signed shift cannot affect the source index.
            const uint32_t sa = ((aSh * (r + 5u)) >> 9) & (SRC - 1);
            const uint32_t sr = (((r * scR) >> 3) + oy) & (SRC - 1);
            raw[a * R_N + r] = lev[src[sa * SRC + sr]];
        }
    }
    for (int a = 0; a < A_N; a++) {
        const int c = a * R_N;
        const int lo = (a == 0 ? 1 : a - 1) * R_N;
        const int hi = (a == A_N - 1 ? A_N - 2 : a + 1) * R_N;
#if GM_BGANIM_KALEIDO_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
        if ((reinterpret_cast<uintptr_t>(tex) & 15u) == 0) {
            kaleidoSmoothAsm(tex + c, raw + lo, raw + c, raw + hi, rimDim, R_N / 16);
            kaleidoGatherAsm(tex + c, tex + c, palette, R_N / 4);
            continue;
        }
#endif
        for (int r = 0; r < R_N; r++) {
            const int v = (raw[lo + r] + 2 * raw[c + r] + raw[hi + r]) >> 2;
            tex[c + r] = palette[(v * rimDim[r]) >> 8];
        }
    }
}

// The map is indexed by absolute y, including odd and skipped rows. No
// cached row or call-local pairing can make interlace change the image.
GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint16_t *__restrict cells = tex;
    const uint16_t *__restrict in = map + static_cast<size_t>(y0) * w;
    for (int n = rows * w; n > 0; n--) {
        *dst++ = cells[*in++];
    }
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_KALEIDO_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    const uint16_t *in = map + static_cast<size_t>(y0) * w;
    const int n = rows * w;
    kaleidoGatherAsm(dst, in, tex, n / 4);
    for (int i = n & ~3; i < n; i++) {
        dst[i] = tex[in[i]]; // 0..3 trailing pixels, including 233-wide rows
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(tex, CELLS * sizeof(uint16_t));
    releaseTable(map, static_cast<size_t>(allocW) * allocH * sizeof(uint16_t));
    releaseTable(src, SRC * SRC);
    releaseTable(rawOwner, RAW_BYTES);
    releaseTable(rimOwner, RIM_BYTES);
    releaseTable(lev, 256);
    releaseTable(palette, 256 * sizeof(uint16_t));
    raw = rimDim = nullptr;
    allocW = allocH = 0;
    lastBrightness = -1;
    lastThemeGen = 0xFFFFFFFF;
}

} // namespace

extern const BgAnimation bg_anim_kaleido;
const BgAnimation bg_anim_kaleido = {
    "kaleido",
    "Kaleido",
    {{"speed", "Speed", 50},
     {"scale", "Blotch scale", 50},
     {"brightness", "Brightness", 62},
     {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
