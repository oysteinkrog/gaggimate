#ifndef GAGGIMATE_SIM

// "Fireflies": soft motes on sum-of-sines wander paths with individual
// pulses and an occasional synchronized shimmer ring. Sparse additive glow
// sprites over a per-scanline gradient. Design: anim-particles (Fable),
// 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

constexpr int FF_MAX = 40;

struct Firefly {
    float homeX, homeY;
    float wx1, wx2, wy1, wy2;
    float ax1, ax2, ay1, ay2;
    float px1, px2, py1, py2;
    float pulseFreq, pulsePhase;
    float radialNorm;
    float size;
    float hueMix;
};

// Per-pixel falloff index is computed in fixed point. The sprite center is
// kept as Q8.8 (cxQ8/cyQ8, 1/256 px precision) rather than rounded to a whole
// pixel: near the sprite core alphaLUT steps hard (255 -> ~195 from index 0
// to 1), so snapping the center to an integer pixel shifts which pixel
// straddles that boundary and produces a visible one-pixel flicker relative
// to the float reference. Q8 sub-pixel precision keeps the boundary in the
// same place the float math would put it.
//   dxQ8  = (px<<8) - cxQ8                          (Q8, px - x)
//   dx2Q4 = (dxQ8*dxQ8) >> 12                        (px^2 in Q4, 16ths)
//   idx   = ((dx2Q4+dy2Q4) * invR2Fixed) >> 20        (Q16.16 * invR2*63)
// (this comment used to describe an earlier >>16/>>16 scheme; the shifts
// above are what the code actually does, corrected 2026-09-04 asm pass.)
// invR2Fixed is bounded: |dxQ8|,|dyQ8| <= R*256 within the bounding box, so
// dx2Q4,dy2Q4 <= R*R*16 each, and the product (2*R*R*16) * (63<<16)/(R*R) ==
// 32*63*65536 ~= 132M regardless of R, well inside int32 range (~2.1B) for
// any firefly size, the R*R cancels, so this bound is uniform, not a
// per-size estimate. Verified again below where the asm kernel relies on it.
struct FfDraw {
    float x, y, R;      // R kept in float only for the per-firefly bbox calc
    int32_t cxQ8, cyQ8; // Q8.8 sub-pixel center
    int32_t invR2Fixed; // Q16.16, pre-scaled by the 63-entry alphaLUT span
    uint8_t a8;
    uint8_t r, g, b;
};

// Shimmer ring Gaussian LUT: expf(-(dr*dr)*61.7f) sampled uniformly in dr
// (not in dr*dr*61.7f, that domain is 64-wide but the curve's whole
// interesting structure sits inside dr in [-0.3, 0.3] i.e. a handful of
// sigmas (sigma=0.09), so uniform-x sampling wastes almost all its
// resolution on the flat near-zero tail). dr = radialNorm-ringPos in
// roughly [-1, 1]; sample |dr| in [0,1]. Built once in init(); frame() only
// ever indexes it, no libm per firefly.
constexpr int EXP_LUT_N = 256;
constexpr float EXP_LUT_DR_MAX = 1.0f;

Firefly *ff = nullptr;
FfDraw *draws = nullptr;
uint8_t *alphaLUT = nullptr; // 64 entries, indexed by normalized d^2
uint16_t *bgLUT = nullptr;   // per-scanline background
uint8_t *ffCol = nullptr;    // FF_MAX * 3, per-particle base color from the theme
float *expLUT = nullptr;     // EXP_LUT_N entries, expf(-(dr*dr)*61.7f) over |dr| in [0, EXP_LUT_DR_MAX)
int ffCount = 0;
int builtCount = -1;
int allocH = 0; // height bgLUT was sized for
uint32_t rng = 0x9e3779b9;
uint32_t lastThemeGen = 0xFFFFFFFF;
int g_h = 480;

// LUT replacement for expf(-(dr*dr)*61.7f), indexed directly by |dr|.
// dr magnitudes beyond the table domain contribute ~0 anyway.
inline float expLutLookup(float dr) {
    int idx = static_cast<int>(fabsf(dr) * (static_cast<float>(EXP_LUT_N - 1) / EXP_LUT_DR_MAX));
    if (idx >= EXP_LUT_N) {
        idx = EXP_LUT_N - 1;
    }
    return expLUT[idx];
}

// Particles glow in the theme's bright range (per-particle hueMix spreads
// them); the dusk background sits in the darkest few percent.
void rebuildThemeAssets() {
    for (int i = 0; i < FF_MAX; i++) {
        themeRGB(185 + static_cast<int>(ff[i].hueMix * 70.0f), &ffCol[i * 3]);
    }
    for (int y = 0; y < g_h; y++) {
        const float n = fabsf(y - g_h * 0.5f) / (g_h * 0.5f);
        uint8_t c[3];
        themeRGB(static_cast<int>(8.0f - n * 5.0f), c);
        bgLUT[y] = rgb565(c[0], c[1], c[2]);
    }
}

void spawnAll(int count, int w, int h) {
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float rMax = (w < h ? w : h) * 0.46f;
    for (int i = 0; i < count; i++) {
        const float theta = nextRandf(rng) * 6.2831853f;
        const float rr = rMax * sqrtf(nextRandf(rng)) * 0.92f;
        Firefly &f = ff[i];
        f.homeX = cx + cosf(theta) * rr;
        f.homeY = cy + sinf(theta) * rr;
        const float basePeriod = 9000.0f + nextRandf(rng) * 5000.0f;
        f.wx1 = 6.2831853f / basePeriod;
        f.wx2 = f.wx1 * 1.618f * (0.85f + nextRandf(rng) * 0.3f);
        f.wy1 = f.wx1 * 1.13f * (0.9f + nextRandf(rng) * 0.2f);
        f.wy2 = f.wx1 * 1.414f * (0.85f + nextRandf(rng) * 0.3f);
        f.ax1 = 16.0f + nextRandf(rng) * 10.0f;
        f.ax2 = 7.0f + nextRandf(rng) * 6.0f;
        f.ay1 = 16.0f + nextRandf(rng) * 10.0f;
        f.ay2 = 7.0f + nextRandf(rng) * 6.0f;
        f.px1 = nextRandf(rng) * 6.2831853f;
        f.px2 = nextRandf(rng) * 6.2831853f;
        f.py1 = nextRandf(rng) * 6.2831853f;
        f.py2 = nextRandf(rng) * 6.2831853f;
        f.pulseFreq = 6.2831853f / (2400.0f + nextRandf(rng) * 3600.0f);
        f.pulsePhase = nextRandf(rng) * 6.2831853f;
        f.radialNorm = rr / rMax;
        f.size = 0.8f + nextRandf(rng) * 0.5f;
        f.hueMix = nextRandf(rng);
    }
    builtCount = count;
}

bool init(int w, int h) {
    if (ff == nullptr) {
        // Placement split by reads per frame (BgAnimCommon.h's hot-slab
        // comment), not by size: ff/ffCol/expLUT are read once per firefly
        // per frame in frame() only, so they stream fine from PSRAM.
        // alphaLUT is gathered at a data-dependent index once per glow pixel
        // (tens of thousands of times a frame); draws is read once per
        // firefly per overlapping band() call (per row-group); bgLUT is read
        // once per row. All three match the hot slab's documented "per pixel
        // or per row" scope and together cost 2,144 B of the 9,216 B budget.
        ff = static_cast<Firefly *>(alloc(FF_MAX * sizeof(Firefly)));
        draws = static_cast<FfDraw *>(allocHot(FF_MAX * sizeof(FfDraw)));
        alphaLUT = static_cast<uint8_t *>(allocHot(64));
        allocH = h;
        bgLUT = static_cast<uint16_t *>(allocHot(h * sizeof(uint16_t)));
        ffCol = static_cast<uint8_t *>(alloc(FF_MAX * 3));
        expLUT = static_cast<float *>(alloc(EXP_LUT_N * sizeof(float)));
        if (ff == nullptr || draws == nullptr || alphaLUT == nullptr || bgLUT == nullptr || ffCol == nullptr ||
            expLUT == nullptr) {
            return false;
        }
        g_h = h;
        for (int i = 0; i < 64; i++) {
            float a = 1.0f - sqrtf(i / 63.0f);
            a = a < 0 ? 0 : a * a;
            alphaLUT[i] = static_cast<uint8_t>(a * 255.0f);
        }
        for (int i = 0; i < EXP_LUT_N; i++) {
            const float dr = i * (EXP_LUT_DR_MAX / (EXP_LUT_N - 1));
            expLUT[i] = expf(-(dr * dr) * 61.7f);
        }
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    const int count = 15 + (p[1] * 25) / 100;
    if (count != builtCount) {
        spawnAll(count, w, h);
        rebuildThemeAssets();
        lastThemeGen = themeGen();
    }
    if (themeGen() != lastThemeGen) {
        rebuildThemeAssets();
        lastThemeGen = themeGen();
    }
    ffCount = count;
    const float speed = speedMul(p[0]);
    const float glow = 0.7f + (p[2] / 100.0f) * 0.8f;
    const float shimAmt = p[3] / 100.0f;
    const float shimPeriod = 14000.0f - shimAmt * 8000.0f;
    const float t = tMs * speed;
    const float ringPos = fmodf(t, shimPeriod) / shimPeriod;

    for (int i = 0; i < ffCount; i++) {
        const Firefly &f = ff[i];
        FfDraw &d = draws[i];
        d.x = f.homeX + f.ax1 * fastSinRad(f.wx1 * t + f.px1) + f.ax2 * fastSinRad(f.wx2 * t + f.px2);
        d.y = f.homeY + f.ay1 * fastSinRad(f.wy1 * t + f.py1) + f.ay2 * fastSinRad(f.wy2 * t + f.py2);
        d.R = (6.0f + f.size * 8.0f) * glow;
        d.cxQ8 = static_cast<int32_t>(d.x * 256.0f + 0.5f);
        d.cyQ8 = static_cast<int32_t>(d.y * 256.0f + 0.5f);
        // Q16.16 scaled by the 63-entry alphaLUT span: idx = (dx*dx+dy*dy)*invR2Fixed >> 16.
        // Round (not truncate) here: truncating this reciprocal alone biases
        // every falloff index low, making every sprite render a hair larger
        // and brighter than the float reference (visible as a systematic,
        // not random, diff against golden).
        d.invR2Fixed = static_cast<int32_t>((63.0f * 65536.0f) / (d.R * d.R) + 0.5f);
        float pulse = fastSinRad(f.pulseFreq * t + f.pulsePhase);
        pulse = pulse < 0 ? 0 : pulse * pulse;
        float brightness = 0.28f + 0.72f * pulse;
        if (shimAmt > 0) {
            const float dr = f.radialNorm - ringPos;
            brightness += shimAmt * expLutLookup(dr); // sigma 0.09
        }
        d.a8 = brightness >= 1.0f ? 255 : static_cast<uint8_t>(brightness * 255.0f);
        d.r = ffCol[i * 3 + 0];
        d.g = ffCol[i * 3 + 1];
        d.b = ffCol[i * 3 + 2];
    }
}

// ---------------------------------------------------------------------
// band() split by cost, like Starfield's: the background fill below touches
// every one of the 230,400 pixels in a frame with one store each (cheap per
// pixel, most of the raw pixel count), while the glow splats touch far
// fewer pixels (a bounding box per firefly, ~40 of them) but pay several
// multiplies each. The asm pass below vectorises the fill (PIE, no per-pixel
// work to vectorise the OTHER way) and hand-schedules the glow's scalar math
// (no vector gather on this chip for the alphaLUT lookup, ASM_BRIEF.md).
//
// fillBgRowScalar/drawGlowSpanScalar are the exact per-pixel math band()
// used before this pass, merely pulled out of the old single function body
// and made branch-free (see below), bandRef() calls them in the same order
// the old band() ran them (background full-width, then every firefly's
// glow), so it is pixel-identical to the pre-asm code. band() further below
// calls the same two pieces via hand-written kernels; both paths share
// glowBBox() for the per-firefly clip, so they can never disagree about
// which pixels a firefly touches.
// ---------------------------------------------------------------------

inline void fillBgRowScalar(uint16_t *row, uint16_t c, int w) {
    for (int x = 0; x < w; x++) {
        row[x] = c;
    }
}

// One firefly's glow, one row, count consecutive pixels starting at dxQ8_0
// ((xx0<<8) - cxQ8), stepping by one pixel (256 in Q8) each iteration.
//
// ROUND 2: this used to clamp idx to 63 (MIN) instead of skipping, on the
// theory that alphaLUT[63]==0 makes the two equivalent and branch-free code
// is strictly better. Bit-exact, yes (proven in round 1 and confirmed again
// on the device in round 4), but the device measurement said otherwise:
// bandRef built this way ran 20-48% SLOWER than the pre-asm-pass code, and
// the branch-free asm kernel only clawed back to a wash against HEAD, not a
// win. The `continue` this replaced was not loop-control overhead -- it was
// a real early exit that skips a gather load, four more multiplies and a
// framebuffer read-modify-write for every pixel outside the inscribed circle
// (~21.5% of the bounding box by area, more near the corners). Removing it
// meant paying that full cost on every pixel, every time, which is strictly
// more device work than the branchy version ever did. Restored both
// `continue`s verbatim; see drawGlowSpanAsm below for the same fix in asm
// (a real BGEI/BEQZ branch, not a clamp).
inline void drawGlowSpanScalar(uint16_t *row, int32_t dxQ8_0, int32_t dy2Q4, int32_t invR2Fixed, uint8_t r, uint8_t g,
                                uint8_t b, uint8_t a8, int count) {
    int32_t dxQ8 = dxQ8_0;
    for (int i = 0; i < count; i++, dxQ8 += 256) {
        const int32_t dx2Q4 = (dxQ8 * dxQ8) >> 12;
        const int32_t idx = ((dx2Q4 + dy2Q4) * invR2Fixed) >> 20;
        if (idx >= 64) {
            continue;
        }
        const uint8_t a = (static_cast<uint16_t>(alphaLUT[idx]) * a8) >> 8;
        if (a == 0) {
            continue;
        }
        row[i] = addScaled565(row[i], r, g, b, a);
    }
}

struct GlowBBox {
    int yy0, yy1, xx0, xx1;
    bool empty;
};

// Per-firefly clip against this band() call's [y0, y0+rows) x [0, w). Shared
// by band() and bandRef() so they can never disagree about which pixels a
// firefly touches. fmaxf/fminf replaced with ternaries (both libcalls on
// this toolchain per OPTIMIZE.md; none of the operands here can be NaN, so
// the ternary is exactly the same comparison, not an approximation).
inline GlowBBox glowBBox(const FfDraw &d, int y0, int rows, int w) {
    GlowBBox b;
    if (d.y + d.R < y0 || d.y - d.R >= y0 + rows) {
        b.empty = true;
        return b;
    }
    const float fy0 = static_cast<float>(y0);
    const float fy1 = static_cast<float>(y0 + rows - 1);
    const float fw1 = static_cast<float>(w - 1);
    const float yLo = d.y - d.R, yHi = d.y + d.R, xLo = d.x - d.R, xHi = d.x + d.R;
    b.yy0 = static_cast<int>(yLo > fy0 ? yLo : fy0);
    b.yy1 = static_cast<int>(yHi < fy1 ? yHi : fy1);
    b.xx0 = static_cast<int>(xLo > 0.0f ? xLo : 0.0f);
    b.xx1 = static_cast<int>(xHi < fw1 ? xHi : fw1);
    b.empty = b.xx1 < b.xx0; // whole firefly off one horizontal edge
    return b;
}

// The spec. Host bench goldens run against this, and the device equivalence
// test (SleepAnimation::runAnimTest, /api/debug/animtest) checks band()'s
// asm kernels against it pixel for pixel.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) {
        fillBgRowScalar(dst + static_cast<size_t>(r) * w, bgLUT[y0 + r], w);
    }
    for (int i = 0; i < ffCount; i++) {
        const FfDraw &d = draws[i];
        const GlowBBox b = glowBBox(d, y0, rows, w);
        if (b.empty) {
            continue;
        }
        for (int yy = b.yy0; yy <= b.yy1; yy++) {
            const int32_t dyQ8 = (yy << 8) - d.cyQ8;
            const int32_t dy2Q4 = (dyQ8 * dyQ8) >> 12; // px^2 in Q4 (16ths), see note above
            const int32_t dxQ8_0 = (b.xx0 << 8) - d.cxQ8;
            uint16_t *row = dst + static_cast<size_t>(yy - y0) * w + b.xx0;
            drawGlowSpanScalar(row, dxQ8_0, dy2Q4, d.invR2Fixed, d.r, d.g, d.b, d.a8, b.xx1 - b.xx0 + 1);
        }
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// Broadcast-fill one row with a single RGB565 color, eight pixels per PIE
// store, the "obvious PIE store loop" ASM_BRIEF.md calls out: background
// rows here are one flat color end to end (bgLUT[y]), so there is no
// per-pixel work at all, only bytes to move.
//
// dst must be 16-byte aligned and nOct*8 == w; both hold in production
// (ASM_BRIEF.md: band() dst rows are 16-byte aligned, w is always 480 or
// 240, both multiples of 16 and so of 8). band() below checks and falls
// back to fillBgRowScalar otherwise.
//
// LOOPNEZ rather than the manual addi/bnez SleepAnimation.cpp's
// scale565Oct and AnimNebula.cpp's lerpRowPie use for their own PIE loops:
// this one runs w/8 times per row (60 or 30) and up to 480 rows a frame -
// the same "worth the extra setup instruction" threshold
// AnimStarfield.cpp's vignette kernel documents for its own loop, and those
// two PIE loops' few-iteration call sites are not.
__attribute__((noinline)) static void fillRowPie(uint16_t *__restrict dstIn, uint16_t color, int nOct) {
    alignas(16) static uint16_t bcast[8];
    for (int i = 0; i < 8; i++) {
        bcast[i] = color;
    }
    const uint16_t *src = bcast;
    uint16_t *wr = dstIn;
    asm volatile("ee.vld.128.ip q0, %[src], 0\n" // q0 = color x8, resident for the loop
                 "loopnez %[n], 2f\n"
                 "ee.vst.128.ip q0, %[wr], 16\n"
                 "2:\n"
                 : [wr] "+r"(wr)
                 : [src] "r"(src), [n] "r"(nOct)
                 : "memory");
}

// One firefly's glow, one row, hand-scheduled Xtensa scalar, pixel-exact
// with drawGlowSpanScalar above (same formula, same two `continue`s, same
// integer truncation order). PIE cannot vectorise this: alphaLUT is a
// data-dependent gather (idx varies per pixel), and this chip's PIE has no
// vector gather (ASM_BRIEF.md).
//
// ROUND 2: this used to clamp idx with MIN instead of branching, on the
// theory that branch-free is strictly better. The device disagreed: with
// the clamp, bandRef (same idea, in C++) measured 20-48% SLOWER than the
// pre-asm-pass code, and this asm kernel only broke even against it instead
// of winning. The `if (idx>=64) continue` it replaced was not loop-control
// overhead, it was a real early exit: skipping the gather, three more
// MULLs and a framebuffer read-modify-write for every pixel outside the
// inscribed circle (~21.5% of the bounding box by area, more near the
// corners). Clamping instead of skipping meant paying that full cost on
// every one of those pixels, on every call, which is strictly more device
// work than the branchy C ever did. Restored as two real branches: BGEI
// (idx>=64, a b4const-encodable immediate) right after idx is known, and
// BEQZ (a==0) right after alpha is known, both jumping to the same
// row/dxQ8 step at the loop tail. This also drops the two MOVI+MIN pairs
// the idx clamp used, so the kernel is smaller as well as conditionally
// cheaper.
//
// Register budget unchanged from round 1: row, dxQ8 (2, "+r") + dy2Q4,
// invR2Fixed, rCol, gCol, bCol, a8v, alut, n (8, "r", n is read once by
// LOOPNEZ and then reused as scratch, see the MOVI lines) + a, dst, res,
// base2 (4, "=&r" scratch) = 14. Confirmed no spill in xtensa-asm14 again
// this round (report).
__attribute__((noinline)) static void drawGlowSpanAsm(uint16_t *__restrict rowIn, int32_t dxQ8_0, int32_t dy2Q4,
                                                       int32_t invR2Fixed, uint8_t rCol, uint8_t gCol, uint8_t bCol,
                                                       uint8_t a8v, int count) {
    uint16_t *row = rowIn;
    int32_t dxQ8 = dxQ8_0;
    const uint8_t *alut = alphaLUT;
    int32_t a, dst, res, base2; // scratch; values unused after the block
    asm volatile(
        "loopnez %[n], 3f\n"
        // idx = ((dxQ8*dxQ8>>12) + dy2Q4) * invR2Fixed >> 20
        "mull  %[a], %[dxQ8], %[dxQ8]\n" // sq = dxQ8^2
        "srai  %[a], %[a], 12\n"         // dx2Q4
        "add   %[a], %[a], %[dy2Q4]\n"   // sum
        "mull  %[a], %[a], %[invR2]\n"   // prod (see the file-header bound proof: never overflows int32)
        "srai  %[a], %[a], 20\n"         // idx
        "bgei  %[a], 64, 4f\n"           // outside the circle: skip gather+blend+store, matches `if (idx>=64) continue`
        // a = alphaLUT[idx] * a8v >> 8  (uint8-range result, no mask needed)
        "add   %[a], %[alut], %[a]\n"
        "l8ui  %[a], %[a], 0\n"  // alphaLUT[idx]
        "mull  %[a], %[a], %[a8v]\n"
        "srli  %[a], %[a], 8\n" // a
        "beqz  %[a], 4f\n"      // fully transparent: skip blend+store, matches `if (a==0) continue`
        // addScaled565(dst, rCol, gCol, bCol, a), same shifts/clamps as the
        // scalar reference (>>11/5-bit, >>10/6-bit, >>11/5-bit)
        "l16ui %[dst], %[row], 0\n"
        "extui %[res], %[dst], 11, 5\n" // R base (res is dead before this, safe as scratch)
        "mull  %[n], %[rc], %[a]\n"
        "srai  %[n], %[n], 11\n"
        "add   %[res], %[res], %[n]\n"
        "movi  %[n], 31\n"
        "min   %[res], %[res], %[n]\n"
        "slli  %[res], %[res], 11\n" // res = R contribution, now the output accumulator
        "extui %[base2], %[dst], 5, 6\n" // G base
        "mull  %[n], %[gc], %[a]\n"
        "srai  %[n], %[n], 10\n"
        "add   %[base2], %[base2], %[n]\n"
        "movi  %[n], 63\n"
        "min   %[base2], %[base2], %[n]\n"
        "slli  %[base2], %[base2], 5\n"
        "or    %[res], %[res], %[base2]\n"
        "extui %[base2], %[dst], 0, 5\n" // B base
        "mull  %[n], %[bc], %[a]\n"
        "srai  %[n], %[n], 11\n"
        "add   %[base2], %[base2], %[n]\n"
        "movi  %[n], 31\n"
        "min   %[base2], %[base2], %[n]\n"
        "or    %[res], %[res], %[base2]\n"
        "s16i  %[res], %[row], 0\n"
        "4:\n"
        "addi  %[row], %[row], 2\n"
        "addi  %[dxQ8], %[dxQ8], 256\n"
        "3:\n"
        : [row] "+r"(row), [dxQ8] "+r"(dxQ8), [a] "=&r"(a), [dst] "=&r"(dst), [res] "=&r"(res), [base2] "=&r"(base2)
        : [dy2Q4] "r"(dy2Q4), [invR2] "r"(invR2Fixed), [rc] "r"(static_cast<int32_t>(rCol)),
          [gc] "r"(static_cast<int32_t>(gCol)), [bc] "r"(static_cast<int32_t>(bCol)),
          [a8v] "r"(static_cast<int32_t>(a8v)), [alut] "r"(alut), [n] "r"(count)
        : "memory");
}
#endif

void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    for (int r = 0; r < rows; r++) {
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        if ((w & 7) == 0) {
            fillRowPie(row, bgLUT[y0 + r], w >> 3);
        } else { // never hit in production: w is always 480 or 240
            fillBgRowScalar(row, bgLUT[y0 + r], w);
        }
    }
    for (int i = 0; i < ffCount; i++) {
        const FfDraw &d = draws[i];
        const GlowBBox b = glowBBox(d, y0, rows, w);
        if (b.empty) {
            continue;
        }
        for (int yy = b.yy0; yy <= b.yy1; yy++) {
            const int32_t dyQ8 = (yy << 8) - d.cyQ8;
            const int32_t dy2Q4 = (dyQ8 * dyQ8) >> 12;
            const int32_t dxQ8_0 = (b.xx0 << 8) - d.cxQ8;
            uint16_t *row = dst + static_cast<size_t>(yy - y0) * w + b.xx0;
            drawGlowSpanAsm(row, dxQ8_0, dy2Q4, d.invR2Fixed, d.r, d.g, d.b, d.a8, b.xx1 - b.xx0 + 1);
        }
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(ff, static_cast<size_t>(FF_MAX) * sizeof(Firefly));
    releaseTable(draws, static_cast<size_t>(FF_MAX) * sizeof(FfDraw));
    releaseTable(alphaLUT, 64);
    releaseTable(bgLUT, static_cast<size_t>(allocH) * sizeof(uint16_t));
    releaseTable(ffCol, static_cast<size_t>(FF_MAX) * 3);
    releaseTable(expLUT, static_cast<size_t>(EXP_LUT_N) * sizeof(float));
    allocH = 0;
    builtCount = -1;
    lastThemeGen = 0xFFFFFFFF;
}

} // namespace

extern const BgAnimation bg_anim_fireflies;
const BgAnimation bg_anim_fireflies = {
    "fireflies",
    "Fireflies",
    {{"speed", "Speed", 50}, {"count", "Count", 60}, {"glow", "Glow", 55}, {"shimmer", "Shimmer", 40}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
