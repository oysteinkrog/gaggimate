#ifndef GAGGIMATE_SIM

// "Floor": the soft, endless plaid plane from entry 36 of anim_bench.html.
// A dark sky lifts into a narrow horizon glow at row 158. Below the opaque
// haze the plaid glides forward at 4.2 texels/s and sways on a 40 s yaw cycle.
// Distance removes contrast with a squared cosine envelope, then subtracts
// fog before the padded theme-ramp lookup. These are the preview's tables
// and integer operations, including its 64 x 256 quarter-texel v sampling.
// The projection constants are the page's literal pixel coordinates, also
// at smaller render widths; no width-dependent motion or scale is invented.
//
// The complete 16 KB texture lives in PSRAM. Each row streams just 64 bytes
// and composes the page's rowLut[tex[u]] into 64 hot int16 indices. This is
// exactly the same lookup, with flattening/fog paid once per texel instead
// of a dependent lookup per output pixel. Every pixel then uses an unsigned
// Q16.16 u accumulator, two SRAM gathers and the fixed 8 x 8 Bayer dither.
// Unsigned addition gives the page's modulo-2^32 |0/>>> behaviour without
// signed overflow. Rows are rebuilt from absolute y, including parity skips.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

#ifndef GM_BGANIM_FLOOR_ASM
#define GM_BGANIM_FLOOR_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int HZ = 158;          // page's horizon row, in panel pixels
constexpr int TU = 64;
constexpr int TV = 256;          // four v samples per nominal texel
constexpr int ZK = 20000;        // depth, texels times rows
constexpr int FOCAL = 240;       // focal distance and horizontal centre
constexpr int DMIN = 17;         // fully opaque rows below the horizon
constexpr int FIRST = HZ + DMIN;
constexpr int PAD = 320;        // room for subtracting all 255 fog indices
constexpr int PAL_N = 768;      // page's padded palette, clamped at both ends
constexpr int TEX_BASE = 106;
constexpr int TEX_LO = 40;
constexpr int TEX_HI = 170;
constexpr int DITHER_BIAS = 2;  // pcDither(2.4) rounds into [-2,2]
constexpr int TEX_BYTES = TU * TV + 15; // align PSRAM owner to 16 for PIE loads
constexpr float PI = 3.14159265358979323846f;

// 12 bytes per textured row. Fog and flattening remain unsigned bytes,
// exactly as in the page; u/du keep every one of their 16 fractional bits.
struct Row {
    uint32_t u, du;
    uint16_t vOffset;
    uint8_t fog, flat;
};
static_assert(sizeof(Row) == 12, "floor row budget assumes 12 byte records");

uint8_t *texOwner = nullptr; // PSRAM owner, includes 15 alignment bytes
uint8_t *tex = nullptr;      // aligned alias, never released separately
uint16_t *ramp = nullptr;   // PSRAM, read only on palette rebuild
uint16_t *palPad = nullptr;
uint16_t *sky = nullptr;
float *recip = nullptr;
uint8_t *fog = nullptr;
uint8_t *flat = nullptr;
Row *rowRec = nullptr;
int16_t *rowIndex = nullptr;
int16_t *dith = nullptr;
uint32_t *dithPacked = nullptr;
const int16_t *sine = nullptr; // borrowed shared 1024-entry, amplitude 512 LUT
int allocH = 0;
int nFloor = 0;
int lastScale = -1, lastBright = -1;
uint32_t lastThemeGen = 0xFFFFFFFF;

// At 480 rows, payload bytes (slab rounding in parentheses):
// palPad 1536, sky 316 (320), recip 1928 (1936), fog 482 (496),
// flat 482 (496), rowRec 3660 (3664), rowIndex 128, dith 128,
// dithPacked 32. Total slab 8736 / 9216 B, no PSRAM fallback.
// texOwner 16399 and ramp 512 use alloc(), 16911 B in PSRAM. The texture
// is a sequential 64-byte row stream only; per-pixel gathers use rowIndex.
// The sine table is borrowed from the separate 3072 B shared reservation.
void release();

bool init(int, int h) {
    if (allocH != 0 && allocH != h) {
        release();
    }
    if (h <= 0 || h > 480) {
        return false; // display sizes above 480 would exceed the slab budget
    }
    if (rowIndex != nullptr) {
        return true; // last allocation succeeds only after every other table
    }
    allocH = h;
    nFloor = h > FIRST ? h - FIRST : 0;
    sine = sinLut();
    if (sine == nullptr) {
        release();
        return false;
    }
    // Check each owner immediately. Every failed partial init tears down all
    // earlier owners, so the retry starts with empty heap and slab state.
#define FLOOR_ALLOC(ptr, type, allocator, bytes) \
    ptr = static_cast<type *>(allocator(bytes)); \
    if (ptr == nullptr) { release(); return false; }
    FLOOR_ALLOC(texOwner, uint8_t, alloc, TEX_BYTES)
    tex = reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(texOwner) + 15u) & ~uintptr_t(15));
    FLOOR_ALLOC(ramp, uint16_t, alloc, 256 * sizeof(uint16_t))
    FLOOR_ALLOC(palPad, uint16_t, allocHot, PAL_N * sizeof(uint16_t))
    FLOOR_ALLOC(sky, uint16_t, allocHot, HZ * sizeof(uint16_t))
    FLOOR_ALLOC(recip, float, allocHot, (h + 2) * sizeof(float))
    FLOOR_ALLOC(fog, uint8_t, allocHot, h + 2)
    FLOOR_ALLOC(flat, uint8_t, allocHot, h + 2)
    // One unused record at tiny heights avoids a zero-size allocation.
    FLOOR_ALLOC(rowRec, Row, allocHot, (nFloor ? nFloor : 1) * sizeof(Row))
    FLOOR_ALLOC(dith, int16_t, allocHot, 64 * sizeof(int16_t))
    FLOOR_ALLOC(dithPacked, uint32_t, allocHot, 8 * sizeof(uint32_t))
    FLOOR_ALLOC(rowIndex, int16_t, allocHot, TU * sizeof(int16_t))
#undef FLOOR_ALLOC
    for (int d = 1; d <= h + 1; d++) {
        recip[d] = static_cast<float>(ZK) / d;
        const float f = d <= DMIN ? 255.0f : d >= 132 ? 0.0f :
            255.0f * 0.5f * (1.0f + cosf(PI * (d - DMIN) / (132 - DMIN)));
        fog[d] = static_cast<uint8_t>(f + 0.5f);
        // The preview's executable formula reaches full contrast at d=150;
        // its earlier "row 95" comment is superseded by this squared curve.
        const float uu = d <= DMIN ? 0.0f : d >= 150 ? 1.0f :
            0.5f * (1.0f - cosf(PI * (d - DMIN) / (150 - DMIN)));
        flat[d] = static_cast<uint8_t>(255.0f * uu * uu + 0.5f);
    }
    return true;
}

void frame(uint32_t tMs, int, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    if (lastScale != p[2]) {
        // Integer positive rounding matches Math.round, without float ties.
        const int wide = 30 + (p[2] * 22 + 50) / 100;
        const int fine = 10 + (p[2] * 10 + 50) / 100;
        for (int vr = 0; vr < TV; vr++) {
            const int bv = sine[(vr * 4) & 1023];
            const int bv2 = sine[(vr * 8 + 128) & 1023];
            for (int u = 0; u < TU; u++) {
                const int bu = sine[(u * 16) & 1023];
                const int bu2 = sine[(u * 32 + 128) & 1023];
                // Signed right shifts intentionally floor, as JS >> does on
                // negative lobes. Both host GCC and Xtensa use that rule.
                int v = TEX_BASE + (((bu + bv) * wide) >> 10) +
                    (((bu2 + bv2) * fine) >> 10) + ((((bu * bv) >> 9) * 18) >> 9);
                tex[vr * TU + u] = static_cast<uint8_t>(v < TEX_LO ? TEX_LO : v > TEX_HI ? TEX_HI : v);
            }
        }
        lastScale = p[2];
    }
    const uint32_t gen = themeGen();
    if (lastBright != p[3] || lastThemeGen != gen) {
        // The page deliberately scales this animation's ramp, 176..256 Q8.
        buildThemeRamp(ramp, 176 + (p[3] * 8 + 5) / 10);
        for (int i = 0; i < PAL_N; i++) {
            const int t = i - PAD;
            palPad[i] = ramp[t < 0 ? 0 : t > 255 ? 255 : t];
        }
        for (int y = 0; y < HZ; y++) {
            const float f = static_cast<float>(y) / HZ;
            const float g = y <= HZ - 22 ? 0.0f : static_cast<float>(y - (HZ - 22)) / 22;
            const int idx = 14 + static_cast<int>(18.0f * f * f + 0.5f) +
                static_cast<int>(46.0f * g * g + 0.5f);
            sky[y] = ramp[idx];
        }
        for (int y = 0; y < 8; y++) {
            uint32_t packed = 0;
            for (int x = 0; x < 8; x++) {
                const int k = y * 8 + x;
                // Fixed 2.4 amplitude, not ditherAmp(): exactly pcDither.
                const int d = static_cast<int>(floorf((BAYER8[k] - 31.5f) / 31.5f * 2.4f + 0.5f));
                dith[k] = static_cast<int16_t>(d);
                packed |= static_cast<uint32_t>(d + DITHER_BIAS) << (4 * x);
            }
            dithPacked[y] = packed;
        }
        lastBright = p[3];
        lastThemeGen = gen;
    }
    // Float only in frame/init. The page uses double here; float can move a
    // quantized u/v boundary by one texel, but preserves its Q16.16 scheme.
    // Even UINT32_MAX at speed 100 keeps 4*(z+fz) below INT32_MAX.
    const float tt = static_cast<float>(tMs) * speedMul(p[0]);
    const float fz = tt * 0.0042f;
    const float yaw = sinf(tt / 40000.0f * (2.0f * PI)) * (0.10f + p[1] * 0.004f);
    const float tanA = tanf(yaw);
    for (int y = FIRST; y < h; y++) {
        const int d = y - HZ + 1;
        const float z = recip[d];
        const float sc = z / FOCAL;
        float u0 = tanA * z - 240.0f * sc; // camU=0, as on the page
        u0 -= floorf(u0 / TU) * TU;
        Row &r = rowRec[y - FIRST];
        r.u = static_cast<uint32_t>(u0 * 65536.0f);
        r.du = static_cast<uint32_t>(sc * 65536.0f);
        r.vOffset = static_cast<uint16_t>((static_cast<uint32_t>((z + fz) * 4.0f) & (TV - 1)) * TU);
        r.fog = fog[d];
        r.flat = flat[d];
    }
}

// Compose rowLut[tex[i]] once for each of the 64 u texels. Subtracting two
// here is exactly cancelled by the packed dither's +2, with no clamp or
// approximation. For tex 40..170, flat/fog 0..255, indices stay 103..487;
// adding dither+2 (0..4) stays within the 768-entry padded palette.
GM_ANIM_IRAM void buildRowRef(int16_t *out, const uint8_t *src, int fl, int anchor) {
    for (int i = 0; i < TU; i++) {
        out[i] = static_cast<int16_t>(anchor + (((static_cast<int>(src[i]) - TEX_BASE) * fl) >> 8));
    }
}

GM_ANIM_IRAM void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        if (y < FIRST) {
            const uint16_t c = y < HZ ? sky[y] : palPad[PAD];
            for (int x = 0; x < w; x++) {
                out[x] = c;
            }
            continue;
        }
        const Row &r = rowRec[y - FIRST];
        buildRowRef(rowIndex, tex + r.vOffset, r.flat, PAD + TEX_BASE - r.fog - DITHER_BIAS);
        uint32_t u = r.u;
        const uint32_t du = r.du;
        const int16_t *dr = dith + (y & 7) * 8;
        for (int x = 0; x < w; x++) {
            out[x] = palPad[rowIndex[(u >> 16) & (TU - 1)] + dr[x & 7] + DITHER_BIAS];
            u += du;
        }
    }
}

#if GM_BGANIM_FLOOR_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14's first bandRef had 115 instructions, two hardware loops: 8
// instructions per row texel and 13 per output pixel. Its pixel schedule is
// transcribed in tests/anim_floor/gcc_baseline.h and checked in QEMU too.
// Its loads already had independent use gaps. The edges below are PIE row
// arithmetic and fill, register-held dither, and one store per pixel pair.
// They save work GCC cannot express with its scalar q-register model.
// Device timing, not these counts or host timings, decides the speedup.
//
// All functions take only pointers/integers and are copied verbatim into
// tests/anim_floor/main.c. GCC never allocates q0..q7 and offers no clobber
// syntax for them. SAR setup and every user of it share one asm block.
// No kernel writes CPENABLE: the render task uses FreeRTOS's lazy CP3 save.

// 16 u texels per iteration: widen bytes, subtract 106, multiply by flat
// with SAR=8 (signed arithmetic floor), then add PAD+106-fog-2.
// All intermediates fit int16 even for arbitrary input bytes 0..255 and
// flat 0..255: (tex-106)*flat is in [-27030,37995] before the >>8.
// Saturating add/sub never saturate. 11 instructions/16 texels, versus
// GCC's 8/texel. The two halves hide each other's multiply-use latency;
// zero.q fills the load-use gap before zip. No predicted unhidden stalls.
// src is a 16-aligned PSRAM texture row; out is the 16-aligned hot rowIndex.
// Broadcasts read the low half of three 4-aligned stack scalar arguments,
// not a 128-bit span. Every 128-bit span is aligned by construction.
GM_ANIM_IRAM __attribute__((noinline)) void floorBuildRowAsm(int16_t *out, const uint8_t *src,
                                                           int fl, int anchor) {
    const uint32_t coeffs[3] = {106u, (uint32_t)fl, (uint32_t)anchor};
    const uint32_t *coeff = coeffs;
    int n = 4; // 64 texels / 16 bytes per vector input
    asm volatile("ee.vldbc.16.ip q2, %[coeff], 4\n"
                 "ee.vldbc.16.ip q3, %[coeff], 4\n"
                 "ee.vldbc.16.ip q4, %[coeff], 0\n"
                 "ssai 8\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[src], 16\n"
                 "ee.zero.q q1\n"
                 "ee.vzip.8 q0, q1\n"
                 "ee.vsubs.s16 q0, q0, q2\n"
                 "ee.vsubs.s16 q1, q1, q2\n"
                 "ee.vmul.s16 q0, q0, q3\n"
                 "ee.vmul.s16 q1, q1, q3\n"
                 "ee.vadds.s16 q0, q0, q4\n"
                 "ee.vadds.s16 q1, q1, q4\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "ee.vst.128.ip q1, %[out], 16\n"
                 "1:\n"
                 : [out] "+&r"(out), [src] "+&r"(src), [coeff] "+&r"(coeff)
                 : [n] "r"(n)
                 : "memory");
}

// Same two dependent gathers as the page after composing rowLut[tex].
// Rotate eight unsigned dither nibbles (+2 bias) by two pixels each time.
// This removes the dither load/address/x counter from GCC's scalar loop.
// 21 instructions/pair, 10.5/pixel, against GCC's 13/pixel. Each gather
// load has an independent instruction before use, including src (rotate)
// between the second colour load and its pack shift. The 57-byte body is
// comfortably within LOOP's 256-byte limit. Estimated issue floor is
// 10.5 cycles/pixel, excluding fetch/memory/RTOS costs and row setup.
// out needs only the public 4-byte alignment, not PIE's 16. u wraps modulo
// 2^32 and returns the post-pair cursor so the odd-width tail is exact.
GM_ANIM_IRAM __attribute__((noinline)) uint32_t floorPairRowAsm(uint16_t *out, const int16_t *row,
                                                              const uint16_t *pal, uint32_t u,
                                                              uint32_t du, uint32_t packed, int nPairs) {
    int t0, t1, t2, t3;
    asm volatile("ssai 8\n"
                 "loopnez %[n], 1f\n"
                 "extui %[t0], %[u], 16, 6\n"
                 "add %[u], %[u], %[du]\n"
                 "extui %[t1], %[u], 16, 6\n"
                 "add %[u], %[u], %[du]\n"
                 "addx2 %[t0], %[t0], %[row]\n"
                 "addx2 %[t1], %[t1], %[row]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "extui %[t2], %[packed], 0, 4\n"
                 "extui %[t3], %[packed], 4, 4\n"
                 "add %[t0], %[t0], %[t2]\n"
                 "add %[t1], %[t1], %[t3]\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "src %[packed], %[packed], %[packed]\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addi %[out], %[out], 4\n"
                 "1:\n"
                 : [out] "+&r"(out), [u] "+&r"(u), [packed] "+&r"(packed),
                   [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3)
                 : [row] "r"(row), [pal] "r"(pal), [du] "r"(du), [n] "r"(nPairs)
                 : "memory");
    return u;
}

// Sky and opaque haze: a scalar prefix reaches 16-byte alignment from any
// legal band destination, then one PIE store emits eight pixels. The scalar
// tail covers every width, including 233. No read or write crosses the span.
GM_ANIM_IRAM __attribute__((noinline)) void floorFillAsm(uint16_t *out, int n, int color) {
    while (n > 0 && ((uintptr_t)out & 15u)) {
        *out++ = (uint16_t)color;
        --n;
    }
    const uint32_t word = (uint32_t)color | ((uint32_t)color << 16);
    const int n8 = n >> 3;
    asm volatile("ee.movi.32.q q0, %[word], 0\n"
                 "ee.movi.32.q q0, %[word], 1\n"
                 "ee.movi.32.q q0, %[word], 2\n"
                 "ee.movi.32.q q0, %[word], 3\n"
                 "loopnez %[n], 1f\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "1:\n"
                 : [out] "+&r"(out)
                 : [word] "r"(word), [n] "r"(n8)
                 : "memory");
    for (int i = 0; i < (n & 7); i++) {
        out[i] = (uint16_t)color;
    }
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        if (y < FIRST) {
            floorFillAsm(out, w, y < HZ ? sky[y] : palPad[PAD]);
            continue;
        }
        const Row &r = rowRec[y - FIRST];
        // allocHot is 16-aligned within our budget. Keep its PSRAM fallback
        // safe too if a caller violates the one-resident-animation contract.
        if ((reinterpret_cast<uintptr_t>(rowIndex) & 15u) == 0) {
            floorBuildRowAsm(rowIndex, tex + r.vOffset, r.flat, PAD + TEX_BASE - r.fog - DITHER_BIAS);
        } else {
            buildRowRef(rowIndex, tex + r.vOffset, r.flat, PAD + TEX_BASE - r.fog - DITHER_BIAS);
        }
        const uint32_t u = floorPairRowAsm(out, rowIndex, palPad, r.u, r.du, dithPacked[y & 7], w >> 1);
        if (w & 1) {
            out[w - 1] = palPad[rowIndex[(u >> 16) & (TU - 1)] + dith[(y & 7) * 8 + ((w - 1) & 7)] +
                                DITHER_BIAS];
        }
    }
}
#else
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif

void release() {
    releaseTable(rowIndex, TU * sizeof(int16_t));
    releaseTable(dithPacked, 8 * sizeof(uint32_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(rowRec, static_cast<size_t>(nFloor ? nFloor : 1) * sizeof(Row));
    releaseTable(flat, static_cast<size_t>(allocH + 2));
    releaseTable(fog, static_cast<size_t>(allocH + 2));
    releaseTable(recip, static_cast<size_t>(allocH + 2) * sizeof(float));
    releaseTable(sky, HZ * sizeof(uint16_t));
    releaseTable(palPad, PAL_N * sizeof(uint16_t));
    releaseTable(ramp, 256 * sizeof(uint16_t));
    releaseTable(texOwner, TEX_BYTES);
    tex = nullptr;
    sine = nullptr;
    allocH = nFloor = 0;
    lastScale = lastBright = -1;
    lastThemeGen = 0xFFFFFFFF;
}

} // namespace

extern const BgAnimation bg_anim_floor;
const BgAnimation bg_anim_floor = {
    "floor",
    "Floor",
    {{"speed", "Speed", 50},
     {"yaw", "Yaw sway", 50},
     {"scale", "Plaid scale", 50},
     {"bright", "Brightness", 60}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
