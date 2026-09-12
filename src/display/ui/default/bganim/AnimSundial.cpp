#ifndef GAGGIMATE_SIM

// "Sundial": a breathing matte disc with a broad, softly edged beam turning
// over it. This is entry 21 of tools/animbench/web/anim_bench.html, including
// its radial face, left-to-right light gradient, drifting two-sine surface,
// distance-based ray softness and radial beam falloff.
//
// The host PPM writer expands RGB565 with channel*255/max; the page uses
// bit replication. Their low RGB888 bits can differ by one with identical
// RGB565 pixels. The device gathers the original theme words directly.
//
// Brightness is Q4 (sixteenths of a theme-ramp index). Ray distances use the
// shared sine table's scale of 512; smoothstep inputs and outputs are Q8,
// including the endpoint 256. Every truncation below follows the page:
// signed shifts round downward, integer divisions round toward zero, and
// time products wrap as uint32_t before their unsigned shifts.
//
// The page materializes eight copies of colFace + surface + Bayer dither.
// Here colSurface[x] + dith[(y & 7)*8 + (x & 7)] represents exactly the same
// colPh entry without eight copies. colQ4 is folded into colFace when built,
// since the page never reads the unshaded column radius again. In particular,
// the radial beam uses that same dithered, shaded cp, not the bare radius.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

#ifndef GM_BGANIM_SUNDIAL_ASM
#define GM_BGANIM_SUNDIAL_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int FACE_HI = 104, FACE_K = 92, FLOOR = 4, SOFT_PX = 94;
constexpr int RAD_RIM = (4080 * FACE_K) >> 8; // 1466 Q4 at the inscribed radius
constexpr int RAD_LO = RAD_RIM * 6 / 10;      // 879; the code uses 60%, not the header's 55%
constexpr int RAD_SPAN = RAD_RIM - RAD_LO;    // 587, the radial smoothstep's width
constexpr int SOFT = SOFT_PX * 512, SOFT_HALF = SOFT / 2;
constexpr int INV_SOFT = 65536 * 256 / SOFT; // 348, exactly the page's truncated reciprocal
// The shared Bayer matrix balances its columns (every column sums to 252) but
// not its rows (168 to 336), and on a face this flat every eighth row came out
// a third of a dither swing brighter than its neighbour: row to row steps in
// mean brightness were 1.09 against 0.25 for columns on the goldens. The page
// rotates each Bayer column by DITHER_ROT[x] (an xor on the row index), which
// keeps the column sums and makes every row sum 252 too; of the 5,832
// rotations that balance both it has the least low frequency power, 2% above
// plain Bayer, with no row or column stripe component. The table is still
// indexed dith[(y & 7) * 8 + (x & 7)] everywhere, so the kernels are untouched.
constexpr uint8_t DITHER_ROT[8] = {0, 6, 2, 4, 5, 3, 7, 1};

int32_t *colFace = nullptr;    // static radius and light gradient, frame() only
int16_t *colSurface = nullptr; // colFace plus this frame's two surface sines
int16_t *rowQ4 = nullptr;
int16_t *surfRow = nullptr;
int16_t *halfPx = nullptr;
int16_t *dith = nullptr;
uint16_t *palette = nullptr;
uint16_t *smooth = nullptr;
uint16_t *radialAmp = nullptr;
int16_t *field = nullptr;
uint16_t *work = nullptr;      // 64 aligned bytes, beam terms then palette constants
const int16_t *sine = nullptr; // borrowed shared table, never released here

int allocW = 0, allocH = 0;
uint32_t lastThemeGen = 0xFFFFFFFF;
int lastShade = -1, lastContrast = -1;
bool geometryValid = false;
int faceBase = 0, beamQ4 = 0;
int d0x = 0, d0y = 0, d1x = 0, d1y = 0;

// At 480x480, all per-pixel and per-row tables fit the resident 9,216 B slab:
// colSurface, rowQ4, surfRow, halfPx, field: 960 B each, 4,800 B total.
// dith: 128 B; palette: 512 B; smooth: 514 B (528 aligned);
// radialAmp: 1,176 B (1,184 aligned). work: 64 B. Total: 7,216 B including alignment.
// colFace: 1,920 B in PSRAM, read only in the sequential frame() column pass.
// smooth/radialAmp are exact tabulations of bandRef's integer polynomials.
// field is one reusable row, overwritten from the absolute y on every call.
void release();

BGANIM_INLINE int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
BGANIM_INLINE int smoothQ8(int u) { return (u * u * (768 - 2 * u)) >> 16; }

bool init(int w, int h) {
    if (allocW != 0 && (allocW != w || allocH != h)) {
        release();
    }
    sine = sinLut();
    if (sine == nullptr) {
        release();
        return false;
    }
    allocW = w;
    allocH = h;
    if (colFace == nullptr)
        colFace = static_cast<int32_t *>(alloc(w * sizeof(int32_t)));
    if (colSurface == nullptr)
        colSurface = static_cast<int16_t *>(allocHot(w * sizeof(int16_t)));
    if (rowQ4 == nullptr)
        rowQ4 = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
    if (surfRow == nullptr)
        surfRow = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
    if (halfPx == nullptr)
        halfPx = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
    if (dith == nullptr)
        dith = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    if (palette == nullptr)
        palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    if (smooth == nullptr)
        smooth = static_cast<uint16_t *>(allocHot(257 * sizeof(uint16_t)));
    if (radialAmp == nullptr)
        radialAmp = static_cast<uint16_t *>(allocHot((RAD_SPAN + 1) * sizeof(uint16_t)));
    if (field == nullptr)
        field = static_cast<int16_t *>(allocHot(w * sizeof(int16_t)));
    if (work == nullptr)
        work = static_cast<uint16_t *>(allocHot(64));
    if (!work || !colFace || !colSurface || !rowQ4 || !surfRow || !halfPx || !dith || !palette || !smooth || !radialAmp ||
        !field) {
        release();
        return false;
    }
    for (int u = 0; u <= 256; u++)
        smooth[u] = static_cast<uint16_t>(smoothQ8(u));
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (!geometryValid || gen != lastThemeGen) {
        buildThemeRamp(palette, 256);
        const float amp = ditherAmp(palette, 256) * 0.6f;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                const int src = ((y ^ DITHER_ROT[x]) & 7) * 8 + x;
                dith[y * 8 + x] = static_cast<int16_t>(lroundf((BAYER8[src] - 31.5f) * (amp * 16.0f / 31.5f)));
            }
        }
        lastThemeGen = gen;
    }
    const int cx = w / 2, cy = h / 2;
    if (!geometryValid || lastShade != p[3]) {
        const int R = (cx < cy ? cx : cy) - 1;
        const int R2 = R * R > 0 ? R * R : 1;
        const int shade = p[3] * 24 / 100;
        const int denomW = w > 1 ? w - 1 : 1;
        for (int x = 0; x < w; x++) {
            const int dx = x - cx;
            // Negate BEFORE the arithmetic shift. Moving the minus past it
            // changes nearly every column by one Q4 unit.
            const int cq = (-static_cast<int>(static_cast<int64_t>(dx) * dx * 4080 / R2) * FACE_K) >> 8;
            colFace[x] = cq - x * shade * 16 / denomW;
        }
        for (int y = 0; y < h; y++) {
            const int dy = y - cy;
            rowQ4[y] = static_cast<int16_t>((-static_cast<int>(static_cast<int64_t>(dy) * dy * 4080 / R2) * FACE_K) >> 8);
            const int inside = R2 - dy * dy;
            halfPx[y] = static_cast<int16_t>(inside > 0 ? static_cast<int>(sqrtf(static_cast<float>(inside))) : -1);
        }
        lastShade = p[3];
    }
    beamQ4 = (60 + p[2] * 88 / 100) * 16; // 960..2368 Q4, unchanged by theme tone
    if (!geometryValid || lastContrast != p[2]) {
        for (int r = 0; r <= RAD_SPAN; r++) {
            const int ur = r * 256 / RAD_SPAN;
            radialAmp[r] = static_cast<uint16_t>((beamQ4 * smoothQ8(ur)) >> 8);
        }
        lastContrast = p[2];
    }
    geometryValid = true;

    // The page intentionally has its own linear speed knob, not speedMul().
    // sp=8 at default 10: the angle period is 1024*512/8 = 65.536 seconds,
    // or 22.91 pixels/s at radius 239. Breath periods are 16.384 and 26.2144
    // seconds; the header's "12 s" is approximate, not another time constant.
    const uint32_t sp = 4 + p[0] * 44 / 100;
    const uint32_t base = tMs * sp;
    const uint32_t angIdx = (base >> 9) & 1023;
    const uint32_t phBr = base >> 7, phBr2 = (base * 5u) >> 10;
    const int breathQ4 = ((sine[phBr & 1023] * 210) >> 9) + ((sine[phBr2 & 1023] * 120) >> 9);
    // Spatial steps 55/16 and 126/16 give wavelengths 297.891 and 130.032
    // pixels. At sp=8, the column waves drift -2.273 and +1.488 px/s;
    // the row waves drift +3.409 and -0.992 px/s. Do not scale with w/h:
    // the page holds these wavelengths and the 94-pixel softness fixed.
    const uint32_t phS1 = base >> 10, phS2 = (base * 3u) >> 11;
    for (int x = 0; x < w; x++) {
        const int sc = ((sine[(((x * 55) >> 4) + phS1) & 1023] * 70) >> 9) + ((sine[(((x * 126) >> 4) - phS2) & 1023] * 40) >> 9);
        colSurface[x] = static_cast<int16_t>(colFace[x] + sc);
    }
    for (int y = 0; y < h; y++) {
        surfRow[y] = static_cast<int16_t>(((sine[(((y * 55) >> 4) - phS2) & 1023] * 70) >> 9) +
                                          ((sine[(((y * 126) >> 4) + phS1) & 1023] * 40) >> 9));
    }
    const int wIdx = 96 + p[1] * 220 / 100; // 33.75..111.09375 degrees, always convex
    const int a1 = (angIdx + wIdx) & 1023;
    d0x = sine[(angIdx + 256) & 1023];
    d0y = sine[angIdx];
    d1x = sine[(a1 + 256) & 1023];
    d1y = sine[a1];
    faceBase = FACE_HI * 16 + breathQ4;
}

// floor/ceil rather than C's truncating division, for the page's signed
// half-line intersections. These divisions happen once per ray per row.
int floorDiv(int n, int d) {
    const int q = n / d, r = n % d;
    return q - (r != 0 && ((r < 0) != (d < 0)));
}
int ceilDiv(int n, int d) {
    const int q = n / d, r = n % d;
    return q + (r != 0 && ((r < 0) == (d < 0)));
}

void beamSpan(int y, int w, int &a, int &b) {
    const int half = halfPx[y], cx = w / 2, dy = y - allocH / 2;
    a = b = 0;
    if (half < 0)
        return;
    a = clampInt(cx - half, 0, w);
    b = clampInt(cx + half + 1, 0, w);
    if (d0y > 0) {
        const int xb = cx + floorDiv(d0x * dy + SOFT_HALF, d0y);
        if (xb + 1 < b)
            b = xb + 1;
    } else if (d0y < 0) {
        const int xb = cx + ceilDiv(d0x * dy + SOFT_HALF, d0y);
        if (xb > a)
            a = xb;
    } else if (d0x * dy <= -SOFT_HALF)
        b = a;
    if (d1y > 0) {
        const int xb = cx + ceilDiv(d1x * dy - SOFT_HALF, d1y);
        if (xb > a)
            a = xb;
    } else if (d1y < 0) {
        const int xb = cx + floorDiv(d1x * dy - SOFT_HALF, d1y);
        if (xb + 1 < b)
            b = xb + 1;
    } else if (d1x * dy >= SOFT_HALF)
        b = a;
    // A ray nearly parallel to a row can intersect beyond the panel. Empty
    // intersections render only the face, with no out-of-buffer prefix.
    if (b <= a || a >= w || b <= 0) {
        a = b = 0;
    }
}

BGANIM_INLINE int beamValue(int cp, int rowBase, int rowRad, int g0, int g1) {
    int v = rowBase + cp;
    const int u0 = clampInt(((g0 + SOFT_HALF) * INV_SOFT) >> 16, 0, 256);
    const int u1 = clampInt(((g1 + SOFT_HALF) * INV_SOFT) >> 16, 0, 256);
    if (u0 > 0 && u1 > 0) {
        int ur = (RAD_RIM - (rowRad - cp)) * 256 / RAD_SPAN;
        if (ur > 256)
            ur = 256;
        if (ur > 0) {
            const int amp = (beamQ4 * smoothQ8(ur)) >> 8;
            v += (amp * ((smoothQ8(u0) * smoothQ8(u1)) >> 8)) >> 8;
        }
    }
    return v;
}

GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r, dy = y - allocH / 2;
        const int rowBase = faceBase + rowQ4[y] + surfRow[y], rowRad = -rowQ4[y];
        const int16_t *off = dith + (y & 7) * 8;
        uint16_t *out = dst + static_cast<size_t>(r) * w;
        int a, b;
        beamSpan(y, w, a, b);
        int x = 0;
        for (; x < a; x++)
            out[x] = palette[clampInt((rowBase + colSurface[x] + off[x & 7]) >> 4, FLOOR, 255)];
        int g0 = d0x * dy - d0y * (a - w / 2), g1 = (a - w / 2) * d1y - dy * d1x;
        for (; x < b; x++) {
            const int cp = colSurface[x] + off[x & 7];
            const int v = beamValue(cp, rowBase, rowRad, g0, g1);
            out[x] = palette[clampInt(v >> 4, FLOOR, 255)];
            g0 -= d0y;
            g1 += d1y;
        }
        for (; x < w; x++)
            out[x] = palette[clampInt((rowBase + colSurface[x] + off[x & 7]) >> 4, FLOOR, 255)];
    }
}

#if GM_BGANIM_SUNDIAL_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14 baseline, saved before writing these kernels with xtensa-asm14.sh:
// bandRef is 431 instructions, no hardware loop. Its .L34 beam loop already
// strength-reduces the two ray products to Q16 accumulators. The full radial
// path still has three cubics, reciprocal division, spills and a bnez back
// edge. Retain its srai/min/max and walking-phase shape, replacing the exact
// integer curves by hot gathers and closing the gather with LOOP.
//
// PIE handles column+dither, eight beam-weight products, and the final
// row add/clamp/shift. The unavoidable curve and palette gathers stay scalar.
// There is no CPENABLE write: FreeRTOS owns lazy CP3 context. GCC never
// allocates q0..q7 and has no q clobber syntax; each block defines every q
// register it reads, and memory is clobbered. SAR is written inside each
// block using it. The 64-byte work area and field/column bases are allocHot
// aligned. The beam consumes a scalar prefix before any vector field access.
// All three functions are copied verbatim into tests/anim_sundial/main.c.

GM_ANIM_IRAM __attribute__((noinline)) void sundialColumnsAsm(int16_t *out, const int16_t *col, const int16_t *off, int n) {
    int blocks = n >> 4;
    int16_t *dst = out;
    const int16_t *src = col;
    // Six instructions per 16 pixels, 0.375/pixel. Two independent loads
    // precede the adds, hiding their load-use gaps. Bayer repeats every eight.
    asm volatile("ee.vld.128.ip q4, %[off], 0\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[src], 16\n"
                 "ee.vld.128.ip q1, %[src], 16\n"
                 "ee.vadds.s16 q0, q0, q4\n"
                 "ee.vadds.s16 q1, q1, q4\n"
                 "ee.vst.128.ip q0, %[dst], 16\n"
                 "ee.vst.128.ip q1, %[dst], 16\n"
                 "1:\n"
                 : [src] "+&r"(src), [dst] "+&r"(dst)
                 : [off] "r"(off), [n] "r"(blocks)
                 : "memory");
    for (int x = blocks * 16; x < n; x++)
        out[x] = col[x] + off[x & 7];
}

GM_ANIM_IRAM __attribute__((noinline)) void sundialBeamAsm(int16_t *pixels, uint16_t *work, const uint16_t *sm,
                                                           const uint16_t *rad, int g0q, int g1q, int step0, int step1,
                                                           int radialBias, int n) {
    // q = (g + 24064)*348, exactly the reference before >>16. No reduced
    // precision in the cursor. radialBias = 1466-rowRad; rad[r] includes the
    // contrast-scaled radial cubic for r clamped to 0..587. Production cp
    // stays within [-2254,264], steps within +/-178176, and |q| stays below
    // 100 million at 480 pixels, so the signed accumulators cannot overflow.
    // The two VMULs plus three loads, one field load, add and store cost
    // eight instructions per eight beam pixels, plus SSAI and block control.
    // Together with the scalar gather this is 26 instructions/beam pixel
    // before that setup, on top of the 6.625 instructions/pixel face path.
    // These are issue-count lower bounds, not measured LX7 cycle timings.
    while (n > 0) {
        if (((uintptr_t)pixels & 15u) != 0 || n < 8) {
            int u0 = g0q >> 16, u1 = g1q >> 16;
            u0 = u0 < 0 ? 0 : (u0 > 256 ? 256 : u0);
            u1 = u1 < 0 ? 0 : (u1 > 256 ? 256 : u1);
            int r = radialBias + *pixels;
            r = r < 0 ? 0 : (r > 587 ? 587 : r);
            *pixels += (rad[r] * ((sm[u0] * sm[u1]) >> 8)) >> 8;
            pixels++;
            n--;
            g0q += step0;
            g1q += step1;
            continue;
        }
        int16_t *src = pixels;
        uint16_t *tmp = work;
        int t0, t1;
        // Twenty-five instructions per pixel, at most 75 bytes in the 256-byte loop
        // limit. Three table loads each have an independent cursor/pointer
        // update before the store consumes the loaded value. Thirteen ARs,
        // no spill or division inside the loop. work holds three eight-lane
        // vectors: sm0, sm1, radial amplitude, at byte offsets 0, 16, 32.
        asm volatile("loop %[eight], 1f\n"
                     "srai %[t0], %[g0], 16\n"
                     "movi %[t1], 256\n"
                     "max %[t0], %[t0], %[zero]\n"
                     "min %[t0], %[t0], %[t1]\n"
                     "addx2 %[t0], %[t0], %[sm]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "add %[g0], %[g0], %[s0]\n"
                     "s16i %[t0], %[tmp], 0\n"
                     "srai %[t0], %[g1], 16\n"
                     "max %[t0], %[t0], %[zero]\n"
                     "min %[t0], %[t0], %[t1]\n"
                     "addx2 %[t0], %[t0], %[sm]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "add %[g1], %[g1], %[s1]\n"
                     "s16i %[t0], %[tmp], 16\n"
                     "l16si %[t0], %[src], 0\n"
                     "movi %[t1], 587\n"
                     "add %[t0], %[t0], %[bias]\n"
                     "max %[t0], %[t0], %[zero]\n"
                     "min %[t0], %[t0], %[t1]\n"
                     "addx2 %[t0], %[t0], %[rad]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "addi %[src], %[src], 2\n"
                     "s16i %[t0], %[tmp], 32\n"
                     "addi %[tmp], %[tmp], 2\n"
                     "1:\n"
                     : [src] "+&r"(src), [tmp] "+&r"(tmp), [g0] "+&r"(g0q), [g1] "+&r"(g1q), [t0] "=&r"(t0), [t1] "=&r"(t1)
                     : [sm] "r"(sm), [rad] "r"(rad), [s0] "r"(step0), [s1] "r"(step1), [bias] "r"(radialBias), [zero] "r"(0),
                       [eight] "r"(8)
                     : "memory");
        tmp = work;
        // 256*256 needs a 32-bit product. VMUL.U16 shifts that full product
        // before narrowing, preserving the endpoint 256 and BOTH >>8 stages.
        // The column and contribution sum stays in int16 at all knob extremes.
        // VMUL defines its result in stage 2: decrementing n before the add
        // fills the last multiply-use gap. The post-store advances pixels.
        asm volatile("ssai 8\n"
                     "ee.vld.128.ip q0, %[tmp], 16\n"
                     "ee.vld.128.ip q1, %[tmp], 16\n"
                     "ee.vld.128.ip q2, %[tmp], 0\n"
                     "ee.vmul.u16 q0, q0, q1\n"
                     "ee.vld.128.ip q3, %[px], 0\n"
                     "ee.vmul.u16 q0, q0, q2\n"
                     "addi %[n], %[n], -8\n"
                     "ee.vadds.s16 q3, q3, q0\n"
                     "ee.vst.128.ip q3, %[px], 16\n"
                     : [tmp] "+&r"(tmp), [px] "+&r"(pixels), [n] "+&r"(n)
                     :
                     : "memory");
    }
}

GM_ANIM_IRAM __attribute__((noinline)) void sundialPaletteAsm(uint16_t *out, int16_t *pixels, const uint16_t *pal, uint16_t *work,
                                                              int rowBase, int n) {
    // Four aligned vectors. Clamping in Q4 to [64,4080] before >>4 gives
    // exactly the page's palette clamp to [4,255]. Multiplication by one
    // with SAR=4 is the PIE arithmetic right shift for 16-bit lanes.
    for (int k = 0; k < 8; k++) {
        work[k] = rowBase;
        work[8 + k] = 64;
        work[16 + k] = 4080;
        work[24 + k] = 1;
    }
    int16_t *src = pixels, *dst = pixels;
    const uint16_t *constants = work;
    const int blocks = n >> 4;
    // Twelve instructions per 16 pixels, 0.75/pixel, with loads interleaved.
    asm volatile("ee.vld.128.ip q4, %[c], 16\n"
                 "ee.vld.128.ip q5, %[c], 16\n"
                 "ee.vld.128.ip q6, %[c], 16\n"
                 "ee.vld.128.ip q7, %[c], 0\n"
                 "ssai 4\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[src], 16\n"
                 "ee.vld.128.ip q1, %[src], 16\n"
                 "ee.vadds.s16 q0, q0, q4\n"
                 "ee.vadds.s16 q1, q1, q4\n"
                 "ee.vmax.s16 q0, q0, q5\n"
                 "ee.vmax.s16 q1, q1, q5\n"
                 "ee.vmin.s16 q0, q0, q6\n"
                 "ee.vmin.s16 q1, q1, q6\n"
                 "ee.vmul.s16 q0, q0, q7\n"
                 "ee.vmul.s16 q1, q1, q7\n"
                 "ee.vst.128.ip q0, %[dst], 16\n"
                 "ee.vst.128.ip q1, %[dst], 16\n"
                 "1:\n"
                 : [src] "+&r"(src), [dst] "+&r"(dst), [c] "+&r"(constants)
                 : [n] "r"(blocks)
                 : "memory");
    for (int x = blocks * 16; x < n; x++) {
        int v = (pixels[x] + rowBase) >> 4;
        pixels[x] = v < 4 ? 4 : (v > 255 ? 255 : v);
    }
    const int16_t *idx = pixels;
    uint16_t *op = out;
    int t0, t1;
    // Eleven instructions per pair, 5.5/pixel. Two independently addressed
    // loads separate every load from its consumer; the output needs only the
    // contract's four-byte alignment, since no vector store touches out.
    asm volatile("loopnez %[n], 1f\n"
                 "l16ui %[t0], %[idx], 0\n"
                 "l16ui %[t1], %[idx], 2\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "addi %[idx], %[idx], 4\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addi %[out], %[out], 4\n"
                 "1:\n"
                 : [idx] "+&r"(idx), [out] "+&r"(op), [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [pal] "r"(pal), [n] "r"(n >> 1)
                 : "memory");
    if (n & 1)
        out[n - 1] = pal[pixels[n - 1]];
}
#endif

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_SUNDIAL_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r, dy = y - allocH / 2;
        sundialColumnsAsm(field, colSurface, dith + (y & 7) * 8, w);
        int a, b;
        beamSpan(y, w, a, b);
        if (b > a) {
            const int g0 = d0x * dy - d0y * (a - w / 2), g1 = (a - w / 2) * d1y - dy * d1x;
            sundialBeamAsm(field + a, work, smooth, radialAmp, (g0 + SOFT_HALF) * INV_SOFT, (g1 + SOFT_HALF) * INV_SOFT,
                           -d0y * INV_SOFT, d1y * INV_SOFT, RAD_RIM + rowQ4[y], b - a);
        }
        sundialPaletteAsm(dst + static_cast<size_t>(r) * w, field, palette, work, faceBase + rowQ4[y] + surfRow[y], w);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(colFace, static_cast<size_t>(allocW) * sizeof(int32_t));
    releaseTable(colSurface, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(rowQ4, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(surfRow, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(halfPx, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(smooth, 257 * sizeof(uint16_t));
    releaseTable(radialAmp, (RAD_SPAN + 1) * sizeof(uint16_t));
    releaseTable(field, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(work, 64);
    sine = nullptr;
    allocW = allocH = 0;
    lastThemeGen = 0xFFFFFFFF;
    lastShade = lastContrast = -1;
    geometryValid = false;
    faceBase = beamQ4 = d0x = d0y = d1x = d1y = 0;
}

} // namespace

extern const BgAnimation bg_anim_sundial;
const BgAnimation bg_anim_sundial = {
    "sundial",
    "Sundial",
    {{"speed", "Speed", 50}, {"width", "Wedge width", 40}, {"contrast", "Contrast", 25}, {"shading", "Surface shading", 30}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
