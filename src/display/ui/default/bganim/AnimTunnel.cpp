#ifndef GAGGIMATE_SIM

// "Tunnel": a soft wall turning around a dark throat while broad bands move
// out toward the rim. This is entry 'tunnel' in anim_bench.html, including its
// softened 128 angle x 32 depth cells, r^0.62 depth spacing and BAYER8 cell
// dither. The angular harmonic is 16: 128*16 is exactly two 1024-unit turns,
// so the left horizontal radius has no angular seam. Below radius 3 the map
// uses angle zero to avoid a pinwheel in the throat. Radius 240 and beyond
// uses depth zero, exactly as the page does outside the circular panel.
//
// init() bakes geometry and dither into one uint16 cell index per pixel.
// frame() builds the RGB565 cell table from the shared +/-512 sine and the
// theme ramp. The page's integer expression is retained without reassociation:
//   s = depthSine + angleSine + 1024;                 // 0..2048
//   idx = ((40 + ((s * 180) >> 11)) * depthDim) >> 8;
// It maps s to 40..220 before the Q8 depth gain (26..218), hence idx <= 187.
// The comment header on the page mentions an older endpoint of 214; its
// executable depthDim formula is 26+192*pow(u,1.15), ending at 218, used here.
// bandRef() then only streams the map and gathers colors. No row is copied
// from a neighbour or cached across calls: y0 selects the absolute map row.
//
// Every user parameter acts in frame(), on the tables the two kernels read.
// Neither kernel and neither pixel loop changed for any of them, and at the
// defaults each one reduces to the constant it replaced, so the output is the
// same word for word:
//   p3 "Band share"  splits the fixed 1024 unit level swing between the
//                    outward bands and the wall's own shading. Two Q9 gains
//                    that always sum to 1024, so s stays inside 0..2048
//                    whatever the split; 512 and 512 is the original pair,
//                    where both gains are the identity.
//   p4 "Contrast"    the level window, which lives in the kernel's `factors`
//                    table. span = (180 * g) >> 8 and floor = 130 - span/2,
//                    so the window keeps its midpoint; g = 256 gives back
//                    180 and 40. span stays under 255, so the widest s * span
//                    product is 522,240, inside what the existing 368,640
//                    already asks of ee.vmul.u16.
//   p5 "Depth curve" the depthDim exponent, (550 + p*12)/1000. Both operands
//                    are exact, so the division is exactly 1.15f at 50 and
//                    the 32 rounded table entries are unchanged. The ends
//                    of the table, 26 and 218, do not move with it.
//   p6 "Spiral"      how far the depth phase advances per angle cell, which
//                    turns the concentric bands into a corkscrew. Multiples
//                    of 8 units only: 128 angle cells then cover a whole
//                    number of 1024 unit sine periods and the left horizontal
//                    radius keeps no seam, the same reason ANG_K is 16. Zero
//                    at the default, and only then is the depth wave built
//                    once for the whole frame instead of once per angle.
//   p7 "Turn rate"   a second speed curve on the wall's rotation alone, so
//                    the turn and the outward bands can be paced apart.
//                    speedMul(50) is exactly 1, so the rate is untouched.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#ifndef GM_BGANIM_TUNNEL_ASM
#define GM_BGANIM_TUNNEL_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int A_N = 128;
constexpr int D_N = 32;
constexpr int ANG_K = 16;
constexpr float R_IN = 3.0f;
constexpr float R_OUT = 240.0f;
constexpr float DEPTH_P = 0.62f;
constexpr int CELL_N = A_N * D_N;
static_assert(SIN_N == 1024 && SIN_AMP == 512, "the page's phases and level bounds use this sine format");

// All frequently gathered display colors fit in the animation's 9,216 B
// slab: tex is 8,192 B. map is a bulk sequential stream, 2*w*h B in PSRAM
// (460,800 B at 480 square; 115,200 B at 240 square), not a random PSRAM LUT.
// The remaining tables are used only while building 4,096 cells in frame():
//   palette    512 B  the page's pa_ramp32(pa_bright(p[2])), as RGB565
//   depthDim    64 B  the page's 32 bytes widened losslessly for PIE lanes
//   depthWave   64 B  the 32 depth sine samples, reused across all angles
//   levels      64 B  one angle's 32 palette indices, scratch for PIE
//   factors     32 B  eight copies each of 180 and 40 for PIE
// They share one 736 B PSRAM allocation plus 15 B of alignment slack. The
// owner pointer is released, never its aligned alias. Every vector span is
// 16-byte aligned by construction, even when alloc() returns only 4-byte
// alignment. No animation tables occupy permanent BSS.
struct alignas(16) FrameTables {
    uint16_t palette[256];
    uint16_t depthDim[D_N];
    int16_t depthWave[D_N];
    uint16_t levels[D_N];
    uint16_t factors[16];
};
static_assert(sizeof(FrameTables) == 736, "update the table budget if the layout changes");
constexpr size_t FRAME_BYTES = sizeof(FrameTables) + 15;

uint16_t *tex = nullptr;
uint16_t *map = nullptr;
uint8_t *frameStorage = nullptr;
FrameTables *ft = nullptr;
const int16_t *sine = nullptr; // borrowed shared sine, outside our slab share
int allocW = 0;
int allocH = 0;
int lastBrightness = -1;
int lastCurve = -1;
int lastContrast = -1;
uint32_t lastThemeGen = 0xFFFFFFFFu;

void release();

// True when a float cell term is within 1/1024 of a cell boundary, where the
// float's own rounding (a few parts in ten million here) could have carried
// it across; init() recomputes those in double.
inline bool nearCellEdge(float v) {
    const float f = v - floorf(v);
    return f < (1.0f / 1024.0f) || f > (1023.0f / 1024.0f);
}

bool init(int w, int h) {
    if (map != nullptr && w == allocW && h == allocH) {
        return true;
    }
    release();
    if (w <= 0 || h <= 0) {
        return false;
    }
    sine = sinLut();
    if (sine == nullptr) {
        release();
        return false;
    }
    allocW = w;
    allocH = h;
    tex = static_cast<uint16_t *>(allocHot(CELL_N * sizeof(uint16_t)));
    frameStorage = static_cast<uint8_t *>(alloc(FRAME_BYTES));
    map = static_cast<uint16_t *>(alloc(static_cast<size_t>(w) * h * sizeof(uint16_t)));
    if (tex == nullptr || frameStorage == nullptr || map == nullptr) {
        release(); // includes successful allocations before the failed one
        return false;
    }
    ft = reinterpret_cast<FrameTables *>((reinterpret_cast<uintptr_t>(frameStorage) + 15u) & ~uintptr_t(15));
    // depthDim and factors now carry parameters, so frame() builds them on its
    // first call and whenever the parameter moves. Like the palette and tex,
    // they are meaningless until frame() has run, which every caller does.

    const float cx = w * 0.5f - 0.5f;
    const float cy = h * 0.5f - 0.5f;
    const float depthK = (D_N - 1) / powf(R_OUT, DEPTH_P);
    const float angK = A_N / (2.0f * static_cast<float>(M_PI));
    // The page builds this map in doubles. Single precision reproduces it
    // everywhere except where a cell fraction lands within a hair of a cell
    // boundary: at (86, 300) the angle term is 120.0000019 in double and
    // 119.9999924 in float, one cell apart, which was one pixel a frame
    // against the page (gm-pciz). So a term whose float value sits within
    // 1/1024 of a cell boundary is recomputed in double, the page's own
    // arithmetic in the page's own order. That is about one term in 500 and
    // it runs once at init(), so the software double costs nothing a frame.
    // R_OUT stays 240 even at other render sizes, matching init(w,h) on the page.
    const double cxD = w * 0.5 - 0.5, cyD = h * 0.5 - 0.5;
    const double depthKD = (D_N - 1) / pow(static_cast<double>(R_OUT), static_cast<double>(DEPTH_P));
    const double angKD = A_N / (2 * M_PI);
    for (int y = 0; y < h; y++) {
        const float dy = y - cy;
        for (int x = 0; x < w; x++) {
            const float dx = x - cx;
            const float r = sqrtf(dx * dx + dy * dy);
            const float bay = (BAYER8[(y & 7) * 8 + (x & 7)] - 31.5f) * (1.0f / 64.0f);
            int d = 0;
            if (r < R_OUT) {
                const float dv = powf(r, DEPTH_P) * depthK + bay;
                if (nearCellEdge(dv)) {
                    const double dxD = x - cxD, dyD = y - cyD;
                    const double rD = sqrt(dxD * dxD + dyD * dyD);
                    d = static_cast<int>(floor(pow(rD, static_cast<double>(DEPTH_P)) * depthKD +
                                               (BAYER8[(y & 7) * 8 + (x & 7)] - 31.5) / 64));
                } else {
                    d = static_cast<int>(floorf(dv));
                }
                d = d < 0 ? 0 : (d >= D_N ? D_N - 1 : d);
            }
            int a = 0;
            if (r >= R_IN) {
                const float av = (atan2f(dy, dx) + static_cast<float>(M_PI)) * angK + bay;
                if (nearCellEdge(av)) {
                    const double dxD = x - cxD, dyD = y - cyD;
                    a = static_cast<int>(floor((atan2(dyD, dxD) + M_PI) * angKD + (BAYER8[(y & 7) * 8 + (x & 7)] - 31.5) / 64));
                } else {
                    a = static_cast<int>(floorf(av));
                }
                // Unsigned low bits implement the page's positive modulo,
                // including floor(-epsilon) == -1 near the angle wrap.
                a = static_cast<int>(static_cast<uint32_t>(a) & (A_N - 1));
            }
            map[static_cast<size_t>(y) * w + x] = static_cast<uint16_t>(a * D_N + d);
        }
    }
    return true;
}

// Rounded tMs*rate*speed modulo 1024. A Q0.32 rate keeps sub-unit motion
// throughout millis()'s uint32 range, where a float product would lose whole
// sine cells. The largest rate is 0.1593*speedMul(100), which is just over
// 1 unit/ms since the speed calibration (gm-33fm), so the Q32 rate is held
// in uint64. The product can then pass 2^64 at the very top of millis(); the
// wrap is harmless because it keeps the low 64 bits and the phase is read
// out of bits 32 to 41.
// This is derived from absolute time, like the page, with no frame history.
// Rates are single precision on the device, so phase thresholds can differ
// slightly from the page's double clock. There is no per-pixel wide math.
uint32_t phaseAt(uint32_t tMs, float rate, float speed) {
    const uint64_t rateQ32 = static_cast<uint64_t>(rate * speed * 4294967296.0f);
    return static_cast<uint32_t>((static_cast<uint64_t>(tMs) * rateQ32 + (uint64_t(1) << 31)) >> 32) &
           (SIN_N - 1);
}

#if GM_BGANIM_TUNNEL_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14.2, firmware -O2 flags, xtensa-asm14.sh AnimTunnel: bandRef's .L3
// is this six-instruction hardware LOOP, transcribed before this kernel:
//   l16ui v,src,0; addi src,src,2; addx2 v,v,cells;
//   l16ui v,v,0; s16i v,out,0; addi out,out,2;
// The first load is scheduled; the color load immediately feeds its store.
// Interleave four independent pixels to hide that dependency, keep walking
// pointers, and write pairs with s32i. The body is 20 instructions / 4 pixels
// (5/pixel versus GCC's 6), with at least one independent instruction after
// every load before its use, including across the loop back edge. On the
// ideal cached-data issue model that is 5 cycles/pixel versus 6+1 for GCC;
// this excludes PSRAM misses, setup and preemption, and is not device timing.
// The real edge is removing the load-use interlock and halving output stores.
//
// Map reads remain 16-bit so a 233-wide odd row may start at 2 mod 4. Only
// out requires the contract's 4-byte alignment, preserved by the 8-byte walk.
// 0..3 tail pixels are scalar, with no overread. No PIE gather exists for
// arbitrary 16-bit cells; decoding them through vector scratch would add a
// memory round trip to this already minimal index calculation. PIE belongs
// in the cell builder below, where there is actual lane arithmetic.
// Both functions are copied verbatim into tests/anim_tunnel/main.c.
GM_ANIM_IRAM __attribute__((noinline)) void tunnelGatherAsm(uint16_t *out, const uint16_t *src,
                                                          const uint16_t *cells, int n) {
    int t0, t1, t2, t3;
    const int quads = n >> 2;
    asm volatile("loopnez %[n], 1f\n"
                 "l16ui   %[t0], %[src], 0\n"
                 "l16ui   %[t1], %[src], 2\n"
                 "addx2   %[t0], %[t0], %[cells]\n"
                 "addx2   %[t1], %[t1], %[cells]\n"
                 "l16ui   %[t0], %[t0], 0\n"
                 "l16ui   %[t1], %[t1], 0\n"
                 "l16ui   %[t2], %[src], 4\n"
                 "l16ui   %[t3], %[src], 6\n"
                 "slli    %[t1], %[t1], 16\n"
                 "addx2   %[t2], %[t2], %[cells]\n"
                 "or      %[t1], %[t1], %[t0]\n"
                 "addx2   %[t3], %[t3], %[cells]\n"
                 "l16ui   %[t2], %[t2], 0\n"
                 "l16ui   %[t3], %[t3], 0\n"
                 "s32i    %[t1], %[out], 0\n"
                 "slli    %[t3], %[t3], 16\n"
                 "or      %[t3], %[t3], %[t2]\n"
                 "s32i    %[t3], %[out], 4\n"
                 "addi    %[src], %[src], 8\n"
                 "addi    %[out], %[out], 8\n"
                 "1:\n"
                 : [out] "+&r"(out), [src] "+&r"(src), [t0] "=&r"(t0), [t1] "=&r"(t1),
                   [t2] "=&r"(t2), [t3] "=&r"(t3)
                 : [cells] "r"(cells), [n] "r"(quads)
                 : "memory");
    for (int tail = n & 3; tail > 0; tail--) {
        *out++ = cells[*src++];
    }
}

// Eight cells per iteration, preserving both truncations in the page's
// ((40 + ((s*180)>>11))*dim)>>8. s is 0..2048, so signed saturating adds
// never saturate; unsigned multiplies retain their full products before SAR
// shifts them. Products are <=368640 and <=47960, and levels are <=187.
// GCC's compiled cell loop has 15 instructions/cell including its palette
// gather. Here the arithmetic is 9 instructions/8 cells, then the scheduled
// scalar gather above maps the levels to RGB565. q0/q1 are working lanes,
// q4/q5 are 180/40, q6 broadcasts aTerm (512..1536) as four identical words.
// Every vector pointer is a 16-aligned member of FrameTables, and groups
// covers complete groups of eight, so no prefix or masked access is needed.
//
// GCC does not allocate q registers and has no q clobber syntax. SAR is set
// explicitly for each multiply stage. Both multiply results have the SAR
// change as an independent instruction before their consumer, and the two
// loads separate each other from use. No kernel writes CPENABLE: FreeRTOS
// owns the lazy CP3 enable/save path. The one-word insertion instruction was
// separately executed in tests/anim_tunnel/probe_movi before use here.
GM_ANIM_IRAM __attribute__((noinline)) void tunnelLevelsAsm(uint16_t *out, const int16_t *wave,
                                                          const uint16_t *dim, const uint16_t *factors,
                                                          int aTerm, int groups) {
    const uint32_t packed = (uint32_t)aTerm * 65537u; // duplicate aTerm in two uint16 lanes
    asm volatile("ee.vld.128.ip q4, %[factors], 16\n"
                 "ee.vld.128.ip q5, %[factors], 0\n"
                 "ee.movi.32.q q6, %[packed], 0\n"
                 "ee.movi.32.q q6, %[packed], 1\n"
                 "ee.movi.32.q q6, %[packed], 2\n"
                 "ee.movi.32.q q6, %[packed], 3\n"
                 "ssai 11\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[wave], 16\n"
                 "ee.vld.128.ip q1, %[dim], 16\n"
                 "ee.vadds.s16 q0, q0, q6\n"
                 "ee.vmul.u16 q0, q0, q4\n"
                 "ssai 8\n"
                 "ee.vadds.s16 q0, q0, q5\n"
                 "ee.vmul.u16 q0, q0, q1\n"
                 "ssai 11\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "1:\n"
                 : [out] "+&r"(out), [wave] "+&r"(wave), [dim] "+&r"(dim), [factors] "+&r"(factors)
                 : [packed] "r"(packed), [n] "r"(groups)
                 : "memory");
}
#endif

void frame(uint32_t tMs, int, int, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (lastBrightness != p[2] || lastThemeGen != gen) {
        // pa_bright: 80+round(p[2]*176/100), 80..256. This is the page's
        // animation knob, applied before RGB565 quantization. Theme tone is
        // already applied by buildThemeRamp, including custom positioned stops.
        const int bright = 80 + (static_cast<int>(p[2]) * 176 + 50) / 100;
        buildThemeRamp(ft->palette, static_cast<uint16_t>(bright));
        lastBrightness = p[2];
        lastThemeGen = gen;
    }
    if (lastCurve != p[5]) {
        // Exact operands, so the division is exactly 1.15f at the default and
        // the table below is the one init() used to bake. 0.55 to 1.75: a low
        // exponent brightens the wall right out of the throat, a high one
        // holds it dark until the rim. Both ends of the table stay put.
        const float curve = (550 + static_cast<int>(p[5]) * 12) / 1000.0f;
        for (int d = 0; d < D_N; d++) {
            ft->depthDim[d] = static_cast<uint16_t>(lroundf(26.0f + 192.0f * powf(d / 31.0f, curve)));
        }
        lastCurve = p[5];
    }
    if (lastContrast != p[4]) {
        // Q8 gain on the page's 180 unit window, about the window's midpoint
        // of 130. 256 is the identity and gives back 180 and 40; the ends are
        // 35 (a flat mid-tone wall) and 254 (near black to near white).
        const int gain = p[4] <= 50 ? 51 + (static_cast<int>(p[4]) * 205 + 25) / 50
                                    : 256 + ((static_cast<int>(p[4]) - 50) * 106 + 25) / 50;
        const int span = (180 * gain) >> 8;
        const int base = 130 - span / 2;
        for (int i = 0; i < 8; i++) {
            ft->factors[i] = static_cast<uint16_t>(span);
            ft->factors[i + 8] = static_cast<uint16_t>(base);
        }
        lastContrast = p[4];
    }
    const float speed = speedMul(p[0]);
    // Page rates: outward bands at 159.3 sine units/s (6.428... s per
    // cycle), angular phase at 20.14 units/s (50.838... s per cycle). Both
    // are the old 0.090 and 0.01138 times the 1.77 speed calibration
    // (gm-33fm), folded into one literal each so this file and the page read
    // the same decimal.
    // The wall's rotation takes a second speed curve of its own, exactly 1x
    // at 50, so the turn can be paced apart from the outward bands.
    const uint32_t dPhase = 0u - phaseAt(tMs, 0.1593f, speed);
    const uint32_t aPhase = phaseAt(tMs, 0.0201426f, speed * speedMul(p[7]));
    const int bandK = 14 + (static_cast<int>(p[1]) * 20 + 50) / 100; // 14..34, default 24
    // Q9 gains that always sum to 1024, so s keeps its 0..2048 range at every
    // split and neither the saturating add nor the u16 multiply can overflow.
    // 512 is the identity: (v * 512) >> 9 is v for both signs.
    const int gainD = (static_cast<int>(p[3]) * 1024 + 50) / 100;
    const int gainA = 1024 - gainD;
    // Depth phase carried per angle cell, in multiples of 8 units so 128 cells
    // span whole 1024 unit sine periods and the angle wrap keeps no seam.
    // -24..24, which is three turns of corkscrew either way.
    const int twistK = 8 * ((static_cast<int>(p[6]) - 50) * 3 / 50);
    const auto buildWave = [&](int aIdx) {
        const uint32_t phase = dPhase + static_cast<uint32_t>(aIdx * twistK);
        for (int d = 0; d < D_N; d++) {
            const int v = sine[(static_cast<uint32_t>(d * bandK) + phase) & (SIN_N - 1)];
            ft->depthWave[d] = static_cast<int16_t>((v * gainD) >> 9);
        }
    };
    if (twistK == 0) {
        buildWave(0); // one build for the frame, exactly the original loop
    }
    for (int a = 0; a < A_N; a++) {
        if (twistK != 0) {
            buildWave(a);
        }
        const int aTerm = ((sine[(a * ANG_K + aPhase) & (SIN_N - 1)] * gainA) >> 9) + 1024;
#if GM_BGANIM_TUNNEL_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
        tunnelLevelsAsm(ft->levels, ft->depthWave, ft->depthDim, ft->factors, aTerm, D_N / 8);
        tunnelGatherAsm(tex + a * D_N, ft->levels, ft->palette, D_N);
#else
        const int span = ft->factors[0];
        const int base = ft->factors[8];
        for (int d = 0; d < D_N; d++) {
            const int s = ft->depthWave[d] + aTerm;
            const int idx = ((base + ((s * span) >> 11)) * ft->depthDim[d]) >> 8;
            tex[a * D_N + d] = ft->palette[idx];
        }
#endif
    }
}

// Portable spec: the page's final tex[map[i]] loop, RGB565 instead of RGBA.
// A flat span is safe for every band shape because the map is fixed in
// absolute coordinates and there is no dependence on another output row.
GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const uint16_t *__restrict src = map + static_cast<size_t>(y0) * w;
    const uint16_t *__restrict cells = tex;
    for (int n = rows * w; n > 0; n--) {
        *dst++ = cells[*src++];
    }
}

// Default on. The QEMU test executes these literal kernels; host builds and
// GM_BGANIM_TUNNEL_ASM=0 retain the portable spec. Device parity and an A/B
// against useref=1 are still required before claiming a production speedup.
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_TUNNEL_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    tunnelGatherAsm(dst, map + static_cast<size_t>(y0) * w, tex, rows * w);
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(tex, CELL_N * sizeof(uint16_t));
    releaseTable(map, static_cast<size_t>(allocW) * allocH * sizeof(uint16_t));
    releaseTable(frameStorage, FRAME_BYTES);
    ft = nullptr;
    sine = nullptr;
    allocW = 0;
    allocH = 0;
    lastBrightness = -1;
    lastCurve = -1;
    lastContrast = -1;
    lastThemeGen = 0xFFFFFFFFu;
}

} // namespace

extern const BgAnimation bg_anim_tunnel;
const BgAnimation bg_anim_tunnel = {
    "tunnel",
    "Tunnel",
    {{"speed", "Speed", 50},
     {"pitch", "Band pitch", 50},
     {"brightness", "Brightness", 74},
     {"mix", "Band share", 50},
     {"contrast", "Contrast", 50},
     {"curve", "Depth curve", 50},
     {"spiral", "Spiral", 50},
     {"turn", "Turn rate", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
