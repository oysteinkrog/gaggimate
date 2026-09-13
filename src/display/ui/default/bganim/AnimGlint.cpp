#ifndef GAGGIMATE_SIM

// "Glint": a broad, bowed sheet of light with a soft bloom and bright core,
// swinging across a dithered vertical ground. This ports entry 23 of
// tools/animbench/web/anim_bench.html, including its integer clocks, rather
// than the earlier thin sliver that occupied this registry slot.
//
// The page's 256-entry profile combines smoothstep, a parabolic bloom and
// an eighth-power core, then bakes in eight Bayer phases. Each row has one
// clipped interval, a Q8 profile cursor and a Q8 amplitude. Geometry is Q4
// pixels so a moving edge retains its fractional profile position. The
// independently dithered ground is added AFTER amplitude scaling, then the
// sum is capped at 255 and gathered from a compressed theme ramp.
// The page expands RGB565 by bit replication and the bench PPM writer uses
// channel*255/max, so their RGB888 channels can differ by one even when the
// underlying RGB565 pixels agree. Neither changes the device's palette.
//
// Every row is derived from absolute y and this frame's row records. There
// is no paired-row approximation or state carried between band() calls.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#ifndef GM_BGANIM_GLINT_ASM
#define GM_BGANIM_GLINT_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int PROF_N = 256;
constexpr int PROF_STRIDE = 264; // page layout: 256 samples plus eight dark guards
constexpr int BG_LO = 5;         // bottom of the compressed theme ramp
constexpr int BG_HI = 226;       // page's highlight ceiling in the theme ramp
constexpr int BG_SPAN = 16;      // ground rises by 16 palette indices from top to bottom
constexpr int REC = 5;          // x0, exclusive x1, Q8 step K, Q8 cursor acc0, Q8 amplitude A
// The highlight's per-pixel cap lives in the palette, not in the pixel loop.
// scaledProf holds (prof * A) >> 8, at most (255 * 323) >> 8 = 321, and the
// ground index is capped at BG_PAT_MAX below, so the sum a pixel gathers with
// is at most 321 + 63 = 384. PAL_N covers that; every entry from 256 up
// repeats palette[255], which is what the reference's cap produces. Removing
// the cap takes two instructions out of every highlight pixel.
constexpr int BG_PAT_MAX = 63;
constexpr int PAL_N = 512;

// At 480 rows, every per-pixel/per-row table fits the 9,216 B hot slab:
// rowRec 4,800; profPh 2,112; palette 1,024; dith 128; bgPat 16;
// scaledProf 512; bgColors 32; bgRun 16. Total 8,640 B, leaving 576 B. All
// sizes are multiples of 16 at 480 and 240 rows; odd heights add allocator
// padding. The padded palette costs 512 B more than the bare 256 entries and
// bgQ4 pays for it: one int16 per row, read once in buildGround, so it is a
// sequential PSRAM sweep of 960 B a frame rather than a per-pixel gather.
// The 512 B source ramp is only read when the theme changes, so it is PSRAM.
// rowRec narrows the page's Int32Array to uint16_t: x <= 480, K <= 8,160,
// acc <= 65,535, A <= 323. No record field loses a bit at supported widths.
uint16_t *rowRec = nullptr;
uint8_t *profPh = nullptr;
uint16_t *palette = nullptr;
int16_t *bgQ4 = nullptr;
int16_t *dith = nullptr;
uint16_t *bgPat = nullptr;
uint16_t *scaledProf = nullptr; // row-local prof*A>>8, widened because A can exceed 256
uint16_t *bgColors = nullptr;   // eight ground colors, then a rotated aligned-store pattern
uint16_t *bgRun = nullptr;      // this run's eight ground indices, rotated to its start
uint16_t *ramp = nullptr;
const int16_t *sine = nullptr; // borrowed shared 1,024-entry, +/-512 sine table
int allocW = 0;
int allocH = 0;
uint32_t lastThemeGen = 0;
bool tablesValid = false;

void release();

// Explicit floor division for the page's signed >> operations. All callers
// are bounded far from INT_MIN. C++17 leaves negative right shift defined
// by the implementation; this spelling also defines the host reference.
BGANIM_INLINE int floorShift(int v, int bits) {
    return v >= 0 ? v >> bits : -1 - ((-1 - v) >> bits);
}

bool init(int w, int h) {
    if (w <= 0 || h <= 0) {
        release();
        return false;
    }
    if (allocW == w && allocH == h && rowRec != nullptr) {
        return true;
    }
    release();
    sine = sinLut();
    if (sine == nullptr) {
        return false;
    }
    allocW = w;
    allocH = h;
    rowRec = static_cast<uint16_t *>(allocHot(static_cast<size_t>(REC) * h * sizeof(uint16_t)));
    profPh = static_cast<uint8_t *>(allocHot(8 * PROF_STRIDE));
    palette = static_cast<uint16_t *>(allocHot(PAL_N * sizeof(uint16_t)));
    bgQ4 = static_cast<int16_t *>(alloc(static_cast<size_t>(h) * sizeof(int16_t)));
    dith = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    bgPat = static_cast<uint16_t *>(allocHot(8 * sizeof(uint16_t)));
    scaledProf = static_cast<uint16_t *>(allocHot(PROF_N * sizeof(uint16_t)));
    bgColors = static_cast<uint16_t *>(allocHot(16 * sizeof(uint16_t)));
    bgRun = static_cast<uint16_t *>(allocHot(8 * sizeof(uint16_t)));
    ramp = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
    if (!rowRec || !profPh || !palette || !bgQ4 || !dith || !bgPat || !scaledProf || !bgColors || !bgRun || !ramp) {
        // A retry must start with no live allocation and no stale dimensions.
        release();
        return false;
    }
    const int denom = h > 1 ? h - 1 : 1;
    for (int y = 0; y < h; y++) {
        bgQ4[y] = static_cast<int16_t>(y * BG_SPAN * 16 / denom);
    }
    return true;
}

void rebuildTheme() {
    buildThemeRamp(ramp, 256);
    for (int i = 0; i < 256; i++) {
        palette[i] = ramp[BG_LO + i * (BG_HI - BG_LO) / 255];
    }
    // Above 255 the padding repeats the top entry, so a gather with an
    // uncapped sum returns exactly what the reference's cap returns.
    for (int i = 256; i < PAL_N; i++) palette[i] = palette[255];
    // bayerOffsets(dith, ditherAmpJS(pal,256)*0.75,16) on the page.
    // Keep Q4 offsets until the use site: the profile floors them with >>4,
    // while the ground adds them to bgQ4 before flooring. Rounding offsets
    // directly to integer palette indices would change both patterns.
    const float amp = ditherAmp(palette, 256) * 0.75f;
    for (int k = 0; k < 64; k++) {
        dith[k] = static_cast<int16_t>(lroundf((static_cast<float>(BAYER8[k]) - 31.5f) *
                                               (amp * 16.0f / 31.5f)));
    }
    for (int ph = 0; ph < 8; ph++) {
        uint8_t *dst = profPh + ph * PROF_STRIDE;
        for (int i = 0; i < PROF_N; i++) {
            int tt = 2 * i - 255;
            if (tt < 0) tt = -tt;
            const int tri = 256 - tt;
            const int bloom = (tri * tri) >> 8;
            const int c1 = (bloom * bloom) >> 8;
            const int core = (c1 * c1) >> 8;
            // 768 = 3*256: tri^2*(3-2*tri) in the page's Q8 smoothstep.
            const int sm = (tri * tri * (768 - 2 * tri)) >> 16;
            // These separate truncations and weights are the approved mix.
            int v = ((sm * 160) >> 8) + ((bloom * 80) >> 8) + ((core * 30) >> 8);
            v += floorShift(dith[ph * 8 + (i & 7)], 4);
            dst[i] = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
        for (int i = PROF_N; i < PROF_STRIDE; i++) dst[i] = 0;
    }
    tablesValid = true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (!tablesValid || gen != lastThemeGen) {
        rebuildTheme();
        lastThemeGen = gen;
    }
    const int cy = h / 2;
    const int halfH = h > 2 ? h / 2 : 1;
    // Length covers 200..600 Q8 units of normalized half-height. Width is
    // 14..54 percent of w on either side of the crest, with a six-pixel floor.
    const int lenQ8 = 200 + static_cast<int>(p[1]) * 400 / 100;
    int hwMax = w * (14 + static_cast<int>(p[2]) * 40 / 100) / 100;
    if (hwMax < 6) hwMax = 6;

    // Page clocks, including both explicit uint32 wraps. At the default
    // Speed 50 the sweep, tilt, bow and pulse periods are 5.041231,
    // 40.329846, 80.659692 and 6.721641 s, and the whole set scales by
    // speedMul(), so the five sweep periods over the slider are 33.86,
    // 13.06, 5.04, 1.95 and 0.75 s. The page header's approximate 9 s pulse
    // does not replace these clocks.
    //
    // Speed follows the fleet's curve (bead gm-kh2s). The old private law,
    // 4 + p[0] * 44 / 100, ran 4 to 48, so the last quarter of the slider
    // bought 1.3x and the whole slider covered 12x of rate where the fleet
    // covers 45x. The multiplier is Q6 and 1664 at 50, the old 26 with six
    // more fraction bits, so the shifts below are the old 7, 10, 11 and 9
    // plus six and every clock at Speed 50 is unchanged. The largest shift
    // is 17, so a uint32 wrap of any of these products moves the sine index
    // by a whole number of 1024-entry cycles and never shows. Rounded, not
    // truncated: the nearest .5 boundary over Speed 0 to 100 is 19 float
    // ulps away, so exp2f on the host, exp2f on the device and Math.pow on
    // the page land on the same integer.
    //
    // The Speed 50 rate is left where the fleet calibration put it because
    // the half change time that would judge a move is not a stable function
    // of this animation's rate. Glint is near periodic on its sweep, the way
    // MOTION.md warns Ripples and Steam are, and at Speed 50 its sweep period
    // (5.0 s at 26, 6.2 s at 21, 7.7 s at 17) sits right under the 7.9 s
    // where the measurement starts pooling separations for its "unrelated"
    // level. Slowing the animation to 21 read 631 ms at Speed 50 and slowing
    // it further to 17 read 923, against 1010 at 26: a 1.53x slower picture
    // measured 9 percent faster. What the slider does to the picture is the
    // sweep period above, 45x end to end.
    const uint32_t speedQ6 = static_cast<uint32_t>(lroundf(1664.0f * speedMul(p[0])));
    const uint32_t base = tMs * speedQ6;
    const uint32_t phSweep = base >> 13;
    const uint32_t phTilt = base >> 16;
    const uint32_t phBow = base >> 17;
    const uint32_t phPulse = (base * 3u) >> 15;
    // Brightness 130..300, pulsed by (236 +/-40)/256. The combined Q8
    // amplitude is at most floor(300*276/256)=323, before the row taper.
    const int bright = ((130 + static_cast<int>(p[3]) * 170 / 100) *
                        (236 + floorShift(sine[phPulse & (SIN_N - 1)] * 40, 9))) >> 8;
    // Integer division before *16 matches the JS bitwise conversion of
    // (w*percentage/100)<<4. Sweep reaches about 44.1 px/s at 480, sp=8.
    const int sweepQ4 = w * 8 + floorShift(sine[phSweep & (SIN_N - 1)] * ((w * 24 / 100) * 16), 9);
    const int tiltQ4 = floorShift(sine[phTilt & (SIN_N - 1)] * ((w * 34 / 100) * 16), 9);
    const int bowQ4 = ((512 + sine[phBow & (SIN_N - 1)]) * ((w * 16 / 100) * 16)) >> 10;
    const int hwMaxQ4 = hwMax * 16;

    for (int y = 0; y < h; y++) {
        uint16_t *rec = rowRec + static_cast<size_t>(y) * REC;
        // Empty by default, including offscreen and zero-taper rows.
        rec[0] = 1; rec[1] = 0; rec[2] = 1; rec[3] = 0; rec[4] = 0;
        int u = (y - cy) * 256 / halfH; // truncation toward zero, as Math.trunc
        if (u < -256) u = -256;
        else if (u > 256) u = 256;
        const int uu = (u * u) >> 8;
        const int v = u * 256 / lenQ8;
        const int taper = 256 - ((v * v) >> 8);
        if (taper <= 0) continue;
        const int A = (bright * taper) >> 8;
        // Only a quarter of the width follows taper. 192/256 keeps the bloom
        // broad at its ends; 64 Q4 units give a minimum four-pixel half width.
        int hwQ4 = (hwMaxQ4 * (192 + (taper >> 2))) >> 8;
        if (hwQ4 < 64) hwQ4 = 64;
        const int cQ4 = sweepQ4 + tiltQ4 * u / 256 + bowQ4 * uu / 256;
        const int spanQ4 = 2 * hwQ4;
        const int lFullQ4 = cQ4 - hwQ4;
        const int rightQ4 = lFullQ4 + spanQ4;
        if (rightQ4 <= 0 || lFullQ4 >= w * 16) continue;
        // 65280 = 255*256: traverse profile indices 0..255 in Q8. Multiplying
        // by 16 converts a Q4 width to pixels without dropping its fraction.
        const int K = 65280 * 16 / spanQ4;
        const int x0 = lFullQ4 > 0 ? (lFullQ4 + 15) >> 4 : 0;
        int x1 = rightQ4 >> 4;
        if (x1 > w) x1 = w;
        if (x1 <= x0) continue;
        const int acc0 = ((x0 * 16) - lFullQ4) * K / 16;
        const int maxSteps = (65535 - acc0) / K;
        if (x1 > x0 + maxSteps + 1) x1 = x0 + maxSteps + 1;
        rec[0] = static_cast<uint16_t>(x0);
        rec[1] = static_cast<uint16_t>(x1);
        rec[2] = static_cast<uint16_t>(K);
        rec[3] = static_cast<uint16_t>(acc0);
        rec[4] = static_cast<uint16_t>(A);
    }
}

void buildGround(int y) {
    for (int k = 0; k < 8; k++) {
        const int v = floorShift(bgQ4[y] + dith[(y & 7) * 8 + k], 4);
        // The real ceiling is 28: bgQ4 reaches BG_SPAN*16 = 256 and the Q4
        // dither reaches 192, since ditherAmp() itself clamps at 16 and this
        // animation scales it by 0.75. BG_PAT_MAX is more than twice that, so
        // this clamp never fires; it is what makes the palette's padding a
        // bound the kernel can rely on rather than an argument about rounding.
        bgPat[k] = static_cast<uint16_t>(v < 0 ? 0 : (v > BG_PAT_MAX ? BG_PAT_MAX : v));
    }
}

// Portable specification, with exactly the page's two independent dither
// coordinates: profile index &7 inside the sheet, screen x &7 on the ground.
GM_ANIM_IRAM void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        buildGround(y);
        const uint16_t *rec = rowRec + static_cast<size_t>(y) * REC;
        const int x0 = rec[0], x1 = rec[1];
        if (x1 <= x0) {
            for (int x = 0; x < w; x++) row[x] = palette[bgPat[x & 7]];
            continue;
        }
        const uint8_t *prof = profPh + (y & 7) * PROF_STRIDE;
        const int K = rec[2], A = rec[4];
        int acc = rec[3];
        for (int x = 0; x < x0; x++) row[x] = palette[bgPat[x & 7]];
        for (int x = x0; x < x1; x++) {
            int v = ((prof[acc >> 8] * A) >> 8) + bgPat[x & 7];
            if (v > 255) v = 255;
            row[x] = palette[v];
            acc += K;
        }
        for (int x = x1; x < w; x++) row[x] = palette[bgPat[x & 7]];
    }
}

#if GM_BGANIM_GLINT_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14's original .L10 loop was transcribed before this kernel was written:
// srai/add/l8ui/extui/mull/addx2/l16ui/srai/add/min/extui/addx2/l16ui/
// add/s16i/addi/addi/addi/bnez, 19 instructions per pixel. Its load-use gaps
// were already filled, but it kept the multiply and back-edge branch.
// With the kernel present GCC outlines buildGround and the reference body
// becomes 20 instructions, materializing its 255 cap inside the pixel loop.
//
// The edge here is algebraic: A is constant across a row, so scale its small
// profile once with PIE before the two dependent gathers. This spends 512 B
// of slab, not a PSRAM index stream. The gather then runs in paired scalar
// LOOPNEZ form, with independent instructions after every load. Short or
// clipped runs still pay for the profile sweep; only device timing can tell
// whether that setup cost wins across the whole frame. Host timing exercises
// bandRef and cannot measure this trade.
//
// These plain-pointer kernels are copied verbatim into tests/anim_glint.
// GCC does not allocate q0..q7 and exposes no q-register clobber syntax.
// No production code writes CPENABLE: FreeRTOS owns lazy CP3 enable/save.
// SAR is saved by the task context, so the hoisted SSAI survives preemption.

// Widen 16 bytes to two eight-lane vectors, then apply the exact Q8 multiply.
// Seven instructions per 16 profile samples, with gaps after VLD and VMUL.
// The page's 264-byte stride alternates source alignment between 0 and 8;
// a scalar prefix of eight samples aligns both source and widened output.
// Testing BOTH addresses also makes a slab fallback safe: incompatible
// alignments simply complete the entire small table through the scalar path.
GM_ANIM_IRAM __attribute__((noinline)) void glintScaleAsm(uint16_t *out, const uint8_t *src, int A, int n) {
    while (n > 0 && (((uintptr_t)out | (uintptr_t)src) & 15u)) {
        *out++ = (uint16_t)((*src++ * A) >> 8);
        --n;
    }
    const int groups = n >> 4;
    const uint32_t aa = (uint32_t)A | ((uint32_t)A << 16);
    asm volatile("ee.movi.32.q q7, %[aa], 0\n"
                 "ee.movi.32.q q7, %[aa], 1\n"
                 "ee.movi.32.q q7, %[aa], 2\n"
                 "ee.movi.32.q q7, %[aa], 3\n"
                 "ssai 8\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[src], 16\n"
                 "ee.zero.q q1\n"
                 "ee.vzip.8 q0, q1\n"
                 "ee.vmul.u16 q0, q0, q7\n"
                 "ee.vmul.u16 q1, q1, q7\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "ee.vst.128.ip q1, %[out], 16\n"
                 "1:\n"
                 : [out] "+&r"(out), [src] "+&r"(src)
                 : [aa] "r"(aa), [n] "r"(groups)
                 : "memory");
    for (int i = 0; i < (n & 15); i++) out[i] = (uint16_t)((src[i] * A) >> 8);
}

// Ground is periodic in screen x, not in destination address. colors[8..15]
// is rotated for THIS row's next 16-byte-aligned destination. Prefix/tail
// use the unrotated colors[0..7]. Thus odd span edges and 466/233 widths are
// safe, as are the contract's destinations aligned to only four bytes.
// Steady state is one vector store per eight background pixels.
GM_ANIM_IRAM __attribute__((noinline)) void glintFillAsm(uint16_t *out, const uint16_t *colors, int x, int n) {
    while (n > 0 && (((uintptr_t)out & 15u) || ((uintptr_t)colors & 15u))) {
        *out++ = colors[x++ & 7];
        --n;
    }
    const int groups = n >> 3;
    if (groups > 0) {
        const uint16_t *pat = colors + 8;
        asm volatile("ee.vld.128.ip q0, %[pat], 0\n"
                     "loopnez %[n], 1f\n"
                     "ee.vst.128.ip q0, %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out), [pat] "+&r"(pat)
                     : [n] "r"(groups)
                     : "memory");
    }
    x += groups * 8;
    for (int i = 0; i < (n & 7); i++) out[i] = colors[(x + i) & 7];
}

// Two profile gathers and two palette gathers cannot be vectorised. The
// bg pair shares one address because x is even, so x&7 never wraps between
// its pixels. This is the tail kernel: it runs at most three times a row,
// after glintOctsAsm has taken the eight-pixel groups, and it still derives
// the ground address from x so that any start phase works. 23 instructions
// per pair, 11.5 per highlight pixel. Every scalar load has at least one
// independent instruction before consumption. No per-iteration spill and no
// branch back edge. Register budget is 12: three walking operands, five
// inputs and four scratch registers. Earlyclobber prevents input overlap.
// The cap the reference applies before its gather lives in the palette's
// padding now (PAL_N above), so neither kernel pays for it per pixel.
GM_ANIM_IRAM __attribute__((noinline)) uint32_t glintPairsAsm(uint16_t *out, const uint16_t *prof,
                                                            const uint16_t *pal, const uint16_t *bg,
                                                            uint32_t acc, int K, int x, int nPairs) {
    uint32_t t0, t1, t2, t3;
    asm volatile("loopnez %[n], 1f\n"
                 "srli %[t0], %[acc], 8\n"
                 "add %[acc], %[acc], %[K]\n"
                 "srli %[t1], %[acc], 8\n"
                 "addx2 %[t0], %[t0], %[prof]\n"
                 "extui %[t2], %[x], 0, 3\n"
                 "addx2 %[t1], %[t1], %[prof]\n"
                 "addx2 %[t2], %[t2], %[bg]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t3], %[t2], 0\n"
                 "l16ui %[t2], %[t2], 2\n"
                 "add %[t0], %[t0], %[t3]\n"
                 "add %[t1], %[t1], %[t2]\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "add %[acc], %[acc], %[K]\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addi %[out], %[out], 4\n"
                 "addi %[x], %[x], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [acc] "+&r"(acc), [x] "+&r"(x),
                   [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3)
                 : [prof] "r"(prof), [pal] "r"(pal), [bg] "r"(bg), [K] "r"(K), [n] "r"(nPairs)
                 : "memory");
    return acc;
}

// Four pixels an iteration, which is what takes the ground index out of the
// pixel loop. glintPairsAsm derives the ground address from x every pair;
// here the caller rotates eight ground indices into bgRun so a group reads
// them at fixed offsets, and the pointer flips between the two halves of that
// 16-byte table with one XOR per group, since the ground repeats every eight
// pixels. bgRun is 16-byte aligned, so flipping bit 3 stays inside it.
// 40 instructions per four pixels, 10 per highlight pixel, against 12.5 for
// the pair kernel: the ground address, the cap and half the loop bookkeeping
// are gone. Every scalar load still has an independent instruction before its
// consumer, and the body is small enough for a hardware loop, so there is no
// branch back edge. Register budget is 12. The uncapped sum is safe because
// of the palette padding described at PAL_N.
GM_ANIM_IRAM __attribute__((noinline)) uint32_t glintQuadsAsm(uint16_t *out, const uint16_t *prof,
                                                              const uint16_t *pal, const uint16_t *bgRot,
                                                              uint32_t acc, int K, int nQuads) {
    uint32_t t0, t1, t2, t3;
    const uint16_t *bg = bgRot;
    asm volatile("loopnez %[n], 1f\n"
                 "srli %[t0], %[acc], 8\n"
                 "add %[acc], %[acc], %[K]\n"
                 "srli %[t1], %[acc], 8\n"
                 "add %[acc], %[acc], %[K]\n"
                 "addx2 %[t0], %[t0], %[prof]\n"
                 "addx2 %[t1], %[t1], %[prof]\n"
                 "l16ui %[t2], %[bg], 0\n"
                 "l16ui %[t3], %[bg], 2\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "add %[t0], %[t0], %[t2]\n"
                 "add %[t1], %[t1], %[t3]\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "srli %[t0], %[acc], 8\n"
                 "add %[acc], %[acc], %[K]\n"
                 "srli %[t1], %[acc], 8\n"
                 "add %[acc], %[acc], %[K]\n"
                 "addx2 %[t0], %[t0], %[prof]\n"
                 "addx2 %[t1], %[t1], %[prof]\n"
                 "l16ui %[t2], %[bg], 4\n"
                 "l16ui %[t3], %[bg], 6\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "add %[t0], %[t0], %[t2]\n"
                 "add %[t1], %[t1], %[t3]\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 4\n"
                 "xor %[bg], %[bg], %[eight]\n"
                 "addi %[out], %[out], 8\n"
                 "1:\n"
                 : [out] "+&r"(out), [acc] "+&r"(acc), [bg] "+&r"(bg),
                   [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3)
                 : [prof] "r"(prof), [pal] "r"(pal), [K] "r"(K), [n] "r"(nQuads), [eight] "r"(8)
                 : "memory");
    return acc;
}

// Complete row glue is also copied to QEMU, including color-pattern rotation,
// clipped runs, the odd-x prefix and tail, and scratch rebuilt on every row.
GM_ANIM_IRAM __attribute__((noinline)) void glintRowAsm(uint16_t *row, const uint16_t *rec,
                                                      const uint8_t *prof, const uint16_t *pal,
                                                      const uint16_t *bg, uint16_t *scaled,
                                                      uint16_t *colors, uint16_t *bgRot, int w) {
    const int phase = (int)((0u - (uintptr_t)row) & 15u) >> 1;
    for (int k = 0; k < 8; k++) {
        colors[k] = pal[bg[k]];
        colors[8 + k] = pal[bg[(phase + k) & 7]];
    }
    const int x0 = rec[0], x1 = rec[1];
    if (x1 <= x0) {
        glintFillAsm(row, colors, 0, w);
        return;
    }
    glintScaleAsm(scaled, prof, rec[4], 256);
    glintFillAsm(row, colors, 0, x0);
    int x = x0;
    uint32_t acc = rec[3];
    const int K = rec[2];
    if (x & 1) {
        const int v = scaled[acc >> 8] + bg[x & 7];
        row[x] = pal[v];
        x++;
        acc += K;
    }
    const int quads = (x1 - x) >> 2;
    if (quads > 0) {
        for (int k = 0; k < 8; k++) bgRot[k] = bg[(x + k) & 7];
        acc = glintQuadsAsm(row + x, scaled, pal, bgRot, acc, K, quads);
        x += quads * 4;
    }
    const int pairs = (x1 - x) >> 1;
    acc = glintPairsAsm(row + x, scaled, pal, bg, acc, K, x, pairs);
    x += pairs * 2;
    if (x < x1) {
        row[x] = pal[scaled[acc >> 8] + bg[x & 7]];
    }
    glintFillAsm(row + x1, colors, x1, w - x1);
}
#endif

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_GLINT_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    (void)tMs;
    (void)p;
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        buildGround(y);
        glintRowAsm(dst + static_cast<size_t>(r) * w, rowRec + static_cast<size_t>(y) * REC,
                    profPh + (y & 7) * PROF_STRIDE, palette, bgPat, scaledProf, bgColors, bgRun, w);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(rowRec, static_cast<size_t>(REC) * allocH * sizeof(uint16_t));
    releaseTable(profPh, 8 * PROF_STRIDE);
    releaseTable(palette, PAL_N * sizeof(uint16_t));
    releaseTable(bgQ4, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(bgPat, 8 * sizeof(uint16_t));
    releaseTable(scaledProf, PROF_N * sizeof(uint16_t));
    releaseTable(bgColors, 16 * sizeof(uint16_t));
    releaseTable(bgRun, 8 * sizeof(uint16_t));
    releaseTable(ramp, 256 * sizeof(uint16_t));
    sine = nullptr;
    allocW = allocH = 0;
    lastThemeGen = 0;
    tablesValid = false;
}

} // namespace

extern const BgAnimation bg_anim_glint;
const BgAnimation bg_anim_glint = {
    "glint",
    "Glint",
    {{"speed", "Speed", 50},
     {"length", "Length", 35},
     {"width", "Width", 45},
     {"brightness", "Brightness", 55}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
