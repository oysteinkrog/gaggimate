#ifndef GAGGIMATE_SIM

// "Cube": the translucent orthographic cube in anim_bench.html, entry 43.
// Six projected squares contribute a constant interior and a 26 px inward
// feather. Back faces contribute 35% of their facing amplitude. Each face
// contribution is truncated before the next is added, as the page's Uint8
// accumulator does, then the overlap is capped at round(1.45 * AMP).
// A dithered 56..74 vertical theme ramp remains visible outside the cube.
// Spin takes 60 s, while tilt swings through 0.32..0.92 radians in 120 s.
// Size is the page's absolute 88..152 px half-size, including at 240 wide.
//
// The page walks four floating edge distances. Here frame() folds amp/26
// into each plane and rounds its coefficients to signed Q16.16 intensity.
// bandRef() clips the same four half-planes and walks them with integer
// adds: floor(min(plane0, plane1, plane2, plane3, amp)) is exactly the
// page's per-face truncation, apart from coefficient rounding. At 480x480
// the coefficient error is below 0.008 palette index. No row uses state
// from a previous band, including single-row interlace calls. The page's
// ceil(size*1.7321)+26+2 box only bounds clearing and repainting. Clearing
// one reusable row and gathering its background too is equivalent: every
// projected vertex lies within sqrt(3)*size of the centre.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

#ifndef GM_BGANIM_CUBE_ASM
#define GM_BGANIM_CUBE_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int FACE_N = 6;
constexpr int Q = 65536;       // Q16.16 intensity, not Q16.16 screen position
constexpr float FEATHER = 26.0f;
constexpr float BACK_AMP = 0.35f;
constexpr float TAU = 6.2831853071795864769f;

struct alignas(16) Face {
    // Four broadcasts of the four-pixel step, then amplitude and cap.
    // Each 16-byte block and each 160-byte record starts aligned for PIE.
    int32_t vectors[24];
    int32_t dx[4], dy[4], b[4];
    int32_t y0, y1, amplitude;
};
static_assert(sizeof(Face) == 160, "PIE face records must stay 16-byte aligned");

struct Geometry {
    float px[8], py[8], nz[6];
    float vx[4], vy[4], nx[4], ny[4], b[4];
    uint16_t order[6]; // four vertex indices packed in successive nibbles
};
static_assert(sizeof(Geometry) == 180, "update the table budget when geometry changes");

// At 480x480, all per-pixel and per-row tables occupy 4,128 slab bytes:
// field 1,920; faces 960; palette 512; bgRow 480; dither 256.
// Geometry is frame-only scratch, 180 bytes in PSRAM. There is no full
// framebuffer, cached background image, private trig LUT or static table.
int32_t *field = nullptr;     // [w], reused and cleared for every absolute row
Face *faces = nullptr;        // [6], read once per contributing face per row
uint16_t *palette = nullptr;  // [256], themeRamp with the page's glow scaling
uint8_t *bgRow = nullptr;     // [h], the page's 56 + round(18*y/(h-1))
int32_t *dither = nullptr;    // [64], signed whole palette-index offsets
Geometry *geometry = nullptr;
int allocW = 0, allocH = 0, cap = 0;
int lastGlow = -1;
uint32_t lastThemeGen = 0xFFFFFFFF;

void release();

bool init(int w, int h) {
    if (w <= 0 || h <= 0) {
        release();
        return false;
    }
    if (allocW == w && allocH == h && geometry != nullptr) return true;
    release();
    allocW = w;
    allocH = h;
    field = static_cast<int32_t *>(allocHot(static_cast<size_t>(w) * sizeof(int32_t)));
    faces = static_cast<Face *>(allocHot(FACE_N * sizeof(Face)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    bgRow = static_cast<uint8_t *>(allocHot(static_cast<size_t>(h)));
    dither = static_cast<int32_t *>(allocHot(64 * sizeof(int32_t)));
    geometry = static_cast<Geometry *>(alloc(sizeof(Geometry)));
    if (!field || !faces || !palette || !bgRow || !dither || !geometry) {
        // Roll back all allocations, even when failure occurred halfway.
        release();
        return false;
    }
    // allocHot aligns slab tables to 16 bytes. Its emergency PSRAM
    // fallback only promises heap alignment; reject such an allocation
    // if it cannot safely serve the two vector streams.
    if (((reinterpret_cast<uintptr_t>(field) | reinterpret_cast<uintptr_t>(faces)) & 15u) != 0) {
        release();
        return false;
    }
    for (int y = 0; y < h; ++y) {
        bgRow[y] = static_cast<uint8_t>(56 + (h > 1 ? (36 * y + h - 1) / (2 * (h - 1)) : 0));
    }
    for (int k = 0; k < 64; ++k) {
        // bayerOffsets(..., 1.5, 1) uses lround, including negative ties.
        dither[k] = static_cast<int32_t>(lroundf((BAYER8[k] - 31.5f) * (1.5f / 31.5f)));
    }
    // FACES from the page: +z, -z, +x, -x, +y, -y, in that order.
    geometry->order[0] = 0x7654;
    geometry->order[1] = 0x2301;
    geometry->order[2] = 0x6215;
    geometry->order[3] = 0x3740;
    geometry->order[4] = 0x3267;
    geometry->order[5] = 0x4510;
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (lastGlow != p[2] || lastThemeGen != gen) {
        // Glow's 184..256 Q8 channel gain is applied before RGB565
        // quantization, exactly as themeRamp() on the page does.
        buildThemeRamp(palette, 184 + (static_cast<int>(p[2]) * 72 + 50) / 100);
        lastGlow = p[2];
        lastThemeGen = gen;
    }
    // Derive time from tMs, as the page does. Reduce its Q24 millisecond
    // product before converting to float so days of uptime cannot erase
    // the fractional pose or overflow an integer conversion. 120,000 ms
    // is the common pose period; the 60 s spin repeats twice inside it.
    const uint32_t speedQ24 = static_cast<uint32_t>(speedMul(p[0]) * 16777216.0f + 0.5f);
    const uint64_t timeQ24 = static_cast<uint64_t>(tMs) * speedQ24;
    const float ts = static_cast<float>(timeQ24 % (120000ULL << 24)) * (0.001f / 16777216.0f);
    // The page's pose offsets: tilt centres on 0.62 rad with a 0.30 rad
    // swing and starts 0.4 rad into that swing; spin starts at 0.9 rad.
    const float ax = 0.62f + 0.30f * sinf(TAU * ts / 120.0f + 0.4f);
    const float ay = 0.9f + TAU * ts / 60.0f;
    const float ca = cosf(ax), sa = sinf(ax), cb = cosf(ay), sb = sinf(ay);
    const float s = 88.0f + p[1] * 0.64f;
    // Glow sets the brightest single face to 52..86 indices, independently
    // of the channel gain. Overlap caps at 1.45 times that, or 75..125.
    const int ampMax = 52 + (static_cast<int>(p[2]) * 34 + 50) / 100;
    cap = (ampMax * 145 + 50) / 100;
    Geometry &g = *geometry;
    for (int i = 0; i < 8; ++i) {
        // VERTS winds around z=-1, then z=+1: --, +-, ++, -+.
        const float X = ((i + 1) & 2) ? s : -s;
        const float Y = (i & 2) ? s : -s;
        const float Z = (i & 4) ? s : -s;
        const float x1 = X * cb + Z * sb, z1 = -X * sb + Z * cb;
        g.px[i] = w * 0.5f + x1;
        g.py[i] = h * 0.5f - (Y * ca - z1 * sa);
    }
    g.nz[0] = cb * ca;
    g.nz[1] = -g.nz[0];
    g.nz[2] = -sb * ca;
    g.nz[3] = -g.nz[2];
    g.nz[4] = sa;
    g.nz[5] = -sa;
    for (int f = 0; f < FACE_N; ++f) {
        Face &face = faces[f];
        const float nz = g.nz[f];
        const float amp = ampMax * (nz > 0.0f ? nz : -nz) * (nz > 0.0f ? 1.0f : BACK_AMP);
        face.y0 = 1;
        face.y1 = 0;
        if (amp < 0.6f) continue;
        float gx = 0.0f, gy = 0.0f, mny = 1e9f, mxy = -1e9f;
        for (int i = 0; i < 4; ++i) {
            const int v = (g.order[f] >> (4 * i)) & 15;
            g.vx[i] = g.px[v];
            g.vy[i] = g.py[v];
            gx += g.vx[i];
            gy += g.vy[i];
            if (g.vy[i] < mny) mny = g.vy[i];
            if (g.vy[i] > mxy) mxy = g.vy[i];
        }
        gx *= 0.25f;
        gy *= 0.25f;
        face.y0 = static_cast<int>(ceilf(mny));
        face.y1 = static_cast<int>(floorf(mxy));
        if (face.y0 < 0) face.y0 = 0;
        if (face.y1 >= h) face.y1 = h - 1;
        face.amplitude = static_cast<int>(amp);
        const float intensityQ = (amp / FEATHER) * Q;
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) & 3;
            const float ex = g.vx[j] - g.vx[i], ey = g.vy[j] - g.vy[i];
            float len = sqrtf(ex * ex + ey * ey);
            if (len == 0.0f) len = 1.0f;
            float nx = -ey / len, ny = ex / len;
            if (nx * (gx - g.vx[i]) + ny * (gy - g.vy[i]) < 0.0f) {
                nx = -nx;
                ny = -ny;
            }
            g.nx[i] = nx;
            g.ny[i] = ny;
            g.b[i] = -nx * g.vx[i] - ny * g.vy[i];
            face.dx[i] = static_cast<int32_t>(lroundf(nx * intensityQ));
            face.dy[i] = static_cast<int32_t>(lroundf(ny * intensityQ));
            face.b[i] = static_cast<int32_t>(lroundf(g.b[i] * intensityQ));
            for (int lane = 0; lane < 4; ++lane) face.vectors[i * 4 + lane] = face.dx[i] * 4;
            face.vectors[16 + i] = face.amplitude;
            face.vectors[20 + i] = cap;
        }
    }
}

// Solve the four inward half-planes at absolute y. Quantization can turn
// a nearly vertical normal's x component into zero; then its row intercept
// alone decides inclusion. Using the same coefficients for clipping and
// stepping guarantees every distance in the emitted span is nonnegative.
// At panel sizes <=480 and size<=100 all intermediate values, including
// unselected edges and four-pixel advances, fit comfortably in int32.
bool faceSpan(const Face &f, int y, int w, int &lo, int &hi, int32_t d[4]) {
    if (y < f.y0 || y > f.y1) return false;
    lo = 0;
    hi = w - 1;
    for (int i = 0; i < 4; ++i) {
        const int32_t b = f.b[i] + f.dy[i] * y;
        const int32_t n = f.dx[i];
        d[i] = b;
        if (n > 0 && b < 0) {
            const int x = (-b + n - 1) / n;
            if (x > lo) lo = x;
        } else if (n < 0) {
            if (b < 0) return false;
            const int x = b / -n;
            if (x < hi) hi = x;
        } else if (n == 0 && b < 0) {
            return false;
        }
    }
    if (lo > hi) return false;
    for (int i = 0; i < 4; ++i) d[i] += f.dx[i] * lo;
    return true;
}

static inline void cubeFacePixelsRef(int32_t *out, int n, int32_t d[4], const int32_t *step, int amplitude, int limit) {
    int32_t d0 = d[0], d1 = d[1], d2 = d[2], d3 = d[3];
    const int32_t s0 = step[0], s1 = step[1], s2 = step[2], s3 = step[3];
    for (int x = 0; x < n; ++x) {
        int32_t m = d0 < d1 ? d0 : d1;
        if (d2 < m) m = d2;
        if (d3 < m) m = d3;
        int c = m >> 16;
        if (c > amplitude) c = amplitude;
        const int a = out[x] + c;
        out[x] = a > limit ? limit : a;
        d0 += s0;
        d1 += s1;
        d2 += s2;
        d3 += s3;
    }
    d[0] = d0;
    d[1] = d1;
    d[2] = d2;
    d[3] = d3;
}

GM_ANIM_IRAM void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; ++row) {
        const int y = y0 + row;
        memset(field, 0, static_cast<size_t>(w) * sizeof(int32_t));
        for (int f = 0; f < FACE_N; ++f) {
            int lo, hi;
            int32_t d[4];
            if (faceSpan(faces[f], y, w, lo, hi, d)) cubeFacePixelsRef(field + lo, hi - lo + 1, d, faces[f].dx, faces[f].amplitude, cap);
        }
        const int32_t *dith = dither + (y & 7) * 8;
        const int background = bgRow[y];
        for (int x = 0; x < w; ++x) {
            // 56..74 + 0..125 + (-2..2) lies in 54..201. The page's
            // final clamp is therefore redundant for every parameter set.
            dst[x] = palette[background + field[x] + dith[x & 7]];
        }
        dst += w;
    }
}

#if GM_BGANIM_CUBE_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14's scalar .L14 loop was 16 instructions per face pixel: three
// MINs, L32I, SRAI, MIN, ADD, MIN, S32I, four cursor ADDs, pointer ADDI,
// decrement and BNEZ. Its loads were already scheduled without stalls.
// The final palette .L18 loop was 12 instructions per pixel in LOOP.
// These kernels transcribe that arithmetic, then use an edge GCC cannot:
// four independent Q16.16 pixels per PIE operation, with cursors resident
// in q0..q3, and direct q-to-AR extraction for palette gathers. No index
// scratch write/read round trip. The device A/B must decide speed, since
// setup and PSRAM destinations are absent from the instruction count.
//
// GCC never allocates q0..q7, so they need no compiler clobber. Production
// never writes CPENABLE: the task's lazy coprocessor exception owns it.
// All vector spans below are explicitly aligned. S32 saturating adds are
// exact ordinary adds here: no intermediate approaches either endpoint.
// The QEMU probe covers VMIN.S32, VADDS.S32, VSR.32 and MOVI.32.A, including
// the symmetric negative saturation endpoint -INT32_MAX outside our range.

// Verbatim in tools/qemubench/tests/anim_cube/main.c, including the scalar
// prefix/tail. out is 4-byte aligned, starts/step are four signed Q16.16
// values, and vectors is the aligned 24-int block at the front of Face.
// All visited distances are nonnegative; advances after the last pixel
// may be negative but stay in int32. No unaligned ee.vld/vst is issued.
GM_ANIM_IRAM __attribute__((noinline)) void cubeFaceSpanAsm(int32_t *out, int n, const int32_t *starts,
                                                          const int32_t *step, const int32_t *vectors) {
    int32_t d[4] = {starts[0], starts[1], starts[2], starts[3]};
    const int amplitude = vectors[16], limit = vectors[20];
    int prefix = (int)((16u - ((uintptr_t)out & 15u)) & 15u) / 4;
    if (prefix > n) prefix = n;
    cubeFacePixelsRef(out, prefix, d, step, amplitude, limit);
    out += prefix;
    n -= prefix;
    const int groups = n / 4;
    if (groups != 0) {
        int32_t lanes[16] __attribute__((aligned(16)));
        for (int e = 0; e < 4; ++e) {
            for (int j = 0; j < 4; ++j) lanes[e * 4 + j] = d[e] + step[e] * j;
        }
        const int32_t *lp = lanes;
        const int32_t *vp = vectors + 16;
        int32_t *op = out;
        // 18 instructions / four pixels = 4.5 per face pixel. Every load
        // has an independent instruction before consumption, across the
        // loop boundary too. The four broadcasts are reread sequentially
        // from the hot slab, freeing q4/q7 for the reduction and accumulator.
        asm volatile("ee.vld.128.ip q0, %[lp], 16\n"
                     "ee.vld.128.ip q1, %[lp], 16\n"
                     "ee.vld.128.ip q2, %[lp], 16\n"
                     "ee.vld.128.ip q3, %[lp], 16\n"
                     "ee.vld.128.ip q5, %[vp], 16\n"
                     "ee.vld.128.ip q6, %[vp], -16\n"
                     "ssai 16\n"
                     "loopnez %[n], 1f\n"
                     "ee.vmin.s32 q4, q0, q1\n"
                     "ee.vmin.s32 q4, q4, q2\n"
                     "ee.vmin.s32 q4, q4, q3\n"
                     "ee.vsr.32 q4, q4\n"
                     "ee.vmin.s32 q4, q4, q5\n"
                     "ee.vld.128.ip q7, %[out], 0\n"
                     "addi %[vp], %[vp], -64\n"
                     "ee.vadds.s32 q4, q4, q7\n"
                     "ee.vmin.s32 q4, q4, q6\n"
                     "ee.vst.128.ip q4, %[out], 16\n"
                     "ee.vld.128.ip q7, %[vp], 16\n"
                     "ee.vld.128.ip q4, %[vp], 16\n"
                     "ee.vadds.s32 q0, q0, q7\n"
                     "ee.vadds.s32 q1, q1, q4\n"
                     "ee.vld.128.ip q7, %[vp], 16\n"
                     "ee.vld.128.ip q4, %[vp], 16\n"
                     "ee.vadds.s32 q2, q2, q7\n"
                     "ee.vadds.s32 q3, q3, q4\n"
                     "1:\n"
                     : [out] "+&r"(op), [lp] "+&r"(lp), [vp] "+&r"(vp)
                     : [n] "r"(groups)
                     : "memory");
        out = op;
        const int advanced = groups * 4;
        for (int e = 0; e < 4; ++e) d[e] += step[e] * advanced;
        n -= advanced;
    }
    cubeFacePixelsRef(out, n, d, step, amplitude, limit);
}

// Eight palette indices are vector sums of the accumulator and the row's
// background plus Bayer offset. MOVI.32.A takes each index straight from
// its q register; ADDX2/L16UI gathers remain scalar because PIE has no
// vector gather. Four independent gathers hide all load-use gaps and
// S32I writes adjacent RGB565 pairs. out needs only the contract's 4-byte
// alignment; acc and the local base table are 16-byte aligned. Odd widths
// and all remainders use the same scalar formula after the last octet.
GM_ANIM_IRAM __attribute__((noinline)) void cubePaletteRowAsm(uint16_t *out, const int32_t *acc,
                                                            const int32_t *dith, int background,
                                                            const uint16_t *pal, int n) {
    int32_t base[8] __attribute__((aligned(16)));
    for (int i = 0; i < 8; ++i) base[i] = background + dith[i];
    const int groups = n / 8;
    if (groups != 0) {
        const int32_t *bp = base;
        int32_t t0, t1, t2, t3;
        // 41 instructions / eight pixels = 5.125 per output pixel, with
        // no immediate load-use dependency and no data-dependent branch.
        // The LOOPNEZ body is below the hardware's 256-byte limit.
        asm volatile("ee.vld.128.ip q2, %[bp], 16\n"
                     "ee.vld.128.ip q3, %[bp], 0\n"
                     "loopnez %[n], 1f\n"
                     "ee.vld.128.ip q0, %[acc], 16\n"
                     "ee.vld.128.ip q1, %[acc], 16\n"
                     "ee.vadds.s32 q0, q0, q2\n"
                     "ee.vadds.s32 q1, q1, q3\n"
                     "ee.movi.32.a q0, %[t0], 0\n"
                     "ee.movi.32.a q0, %[t1], 1\n"
                     "ee.movi.32.a q0, %[t2], 2\n"
                     "ee.movi.32.a q0, %[t3], 3\n"
                     "addx2 %[t0], %[t0], %[pal]\n"
                     "addx2 %[t1], %[t1], %[pal]\n"
                     "addx2 %[t2], %[t2], %[pal]\n"
                     "addx2 %[t3], %[t3], %[pal]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "l16ui %[t2], %[t2], 0\n"
                     "l16ui %[t3], %[t3], 0\n"
                     "slli %[t1], %[t1], 16\n"
                     "slli %[t3], %[t3], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "or %[t2], %[t2], %[t3]\n"
                     "s32i %[t0], %[out], 0\n"
                     "s32i %[t2], %[out], 4\n"
                     "ee.movi.32.a q1, %[t0], 0\n"
                     "ee.movi.32.a q1, %[t1], 1\n"
                     "ee.movi.32.a q1, %[t2], 2\n"
                     "ee.movi.32.a q1, %[t3], 3\n"
                     "addx2 %[t0], %[t0], %[pal]\n"
                     "addx2 %[t1], %[t1], %[pal]\n"
                     "addx2 %[t2], %[t2], %[pal]\n"
                     "addx2 %[t3], %[t3], %[pal]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "l16ui %[t2], %[t2], 0\n"
                     "l16ui %[t3], %[t3], 0\n"
                     "slli %[t1], %[t1], 16\n"
                     "slli %[t3], %[t3], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "or %[t2], %[t2], %[t3]\n"
                     "s32i %[t0], %[out], 8\n"
                     "s32i %[t2], %[out], 12\n"
                     "addi %[out], %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out), [acc] "+&r"(acc), [bp] "+&r"(bp),
                       [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3)
                     : [n] "r"(groups), [pal] "r"(pal)
                     : "memory");
    }
    for (int i = 0; i < n % 8; ++i) out[i] = pal[acc[i] + base[i]];
}
#endif

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_CUBE_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    (void)tMs;
    (void)p;
    for (int row = 0; row < rows; ++row) {
        const int y = y0 + row;
        memset(field, 0, static_cast<size_t>(w) * sizeof(int32_t));
        for (int f = 0; f < FACE_N; ++f) {
            int lo, hi;
            int32_t d[4];
            if (faceSpan(faces[f], y, w, lo, hi, d)) {
                cubeFaceSpanAsm(field + lo, hi - lo + 1, d, faces[f].dx, faces[f].vectors);
            }
        }
        cubePaletteRowAsm(dst, field, dither + (y & 7) * 8, bgRow[y], palette, w);
        dst += w;
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(geometry, sizeof(Geometry));
    releaseTable(dither, 64 * sizeof(int32_t));
    releaseTable(bgRow, static_cast<size_t>(allocH));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(faces, FACE_N * sizeof(Face));
    releaseTable(field, static_cast<size_t>(allocW) * sizeof(int32_t));
    allocW = allocH = cap = 0;
    lastGlow = -1;
    lastThemeGen = 0xFFFFFFFF;
}

} // namespace

extern const BgAnimation bg_anim_cube;
const BgAnimation bg_anim_cube = {
    "cube",
    "Cube",
    {{"speed", "Speed", 50},
     {"size", "Cube size", 50},
     {"glow", "Face glow", 55},
     {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
