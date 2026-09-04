#ifndef GAGGIMATE_SIM

// "Steam" — rising wisps built from chains of overlapping soft blobs; each
// blob's position/radius/alpha is a pure function of its age (analytic, no
// feedback buffer), with a 4L(1-L) lifecycle envelope so nothing pops.
// Design: anim-particles (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

constexpr int WISPS_MAX = 5;
constexpr int BLOBS_PER_WISP = 11;
constexpr int BLOBS_MAX = WISPS_MAX * BLOBS_PER_WISP;

struct Wisp {
    float x0, y0;
    float swayPhase1, swayPhase2, swayFreq1, swayFreq2;
};
struct Blob {
    uint8_t wisp;
    uint32_t birth;
    float lifetime;
    float seed;
};
struct BlobDraw {
    // Fixed-point draw state (all set once per frame in frame(), consumed
    // per-pixel in band()). xi/yi/Ri are rounded pixel-space ints; scaleQ is
    // a Q16.16 factor so idx = ((dx*dx+dy2)*scaleQ) >> 16 reproduces
    // (dist^2 * invR2 * 63) without any float ops in the band inner loop.
    // Bounded: (dx*dx+dy2) <= 2*Ri*Ri inside the bbox, so the product never
    // exceeds 2*63*65536 (~8.3M) regardless of R -- always safe in int32.
    int xi, yi, Ri;
    int32_t scaleQ;
    uint8_t a8, r, g, b;
    bool visible;
};

// alphaLUT holds the real (1-sqrt(i/63))^1.6 falloff curve, 64 entries,
// same size as the pre-asm-pass code.
//
// Two rounds of this pass tried to make the blob stamp's per-pixel skips
// (idx >= 64, alpha == 0) branch-free instead of tested: round 1 padded
// this table to 1024 entries (64..1023 zeroed) so an out-of-disc idx read
// back zero without a bounds check; round 2 kept the same branch-free
// shape with a `min(idx, 63)` clamp against the original 64-entry table
// instead. Both measured WORSE on the device than just testing and
// skipping. The reason surfaces once you count instructions per pixel
// instead of assuming branches are the expensive thing: GCC 14's own
// compile of the ORIGINAL two-branch loop (xtensa-asm14/AnimSteam.S,
// checked directly against this round) bails out of an out-of-disc pixel
// in about 8 instructions and an in-disc-but-invisible pixel in about 13,
// against roughly 37 for a full blend every branch-free version pays on
// EVERY pixel regardless of whether it contributes anything. Steam's
// blobs spend most of their bounding box outside the disc (the box is
// ~1.27x the disc's area) or near-zero alpha (the 4L(1-L) lifecycle
// envelope is capped at 0.4 and split across up to 55 concurrent blobs),
// so the two skips are taken far more often than not on real content --
// paying full blend cost unconditionally to avoid a branch this hardware
// predicts fine on a `continue` was a net loss. Round 3 restores the
// original two tests; see stampBlobs.
constexpr int ALPHA_LUT_N = 64;
Wisp wisps[WISPS_MAX];
Blob *blobs = nullptr;
BlobDraw *draws = nullptr;
uint8_t *alphaLUT = nullptr; // see ALPHA_LUT_N above
uint16_t *bgLUT = nullptr;
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
uint16_t *fillBcast = nullptr; // 8 copies of the fill colour; see fillRowPie
#endif
int wispCount = 0;
int builtCount = -1;
int allocH = 0; // height bgLUT was sized for
uint32_t rng = 0x1234abcd;
int g_active = 0;
uint32_t lastThemeGen = 0xFFFFFFFF;
int g_h = 480;

// alphaLUT, draws and bgLUT are all read from band() itself (per pixel, per
// (blob,row), and per row respectively) rather than only from the once-a-
// frame frame(), so round 2's placement rule -- allocHot for the per-pixel
// and per-row set, alloc for the rest -- puts all three here. blobs is
// read/written only in frame(), never in band(), so it stays on plain
// alloc() (PSRAM): one access per blob per frame costs nothing next to a
// per-pixel table landing in the wrong pool. Total ask here is well under
// the 9,216 B per-animation slab (ALPHA_LUT_N=64 + BLOBS_MAX*sizeof(BlobDraw)
// + up to 480*2 for bgLUT + 16 for fillBcast is about 2.4 KB), so allocHot
// is not expected to ever fall back to alloc() for this animation -- see
// the fillBcast comment in init() for the one place that fallback would
// matter if it ever did.
void *allocPreferHot(size_t size) {
    void *p = allocHot(size);
    return p != nullptr ? p : alloc(size);
}

void rebuildBg() {
    for (int y = 0; y < g_h; y++) {
        const float n = fabsf(y - g_h * 0.5f) / (g_h * 0.5f);
        uint8_t c[3];
        themeRGB(static_cast<int>(9.0f - n * 6.0f), c);
        bgLUT[y] = rgb565(c[0], c[1], c[2]);
    }
}

void buildWisps(int count, int w, int h, uint32_t tMs) {
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float rDisp = (w < h ? w : h) * 0.5f;
    const float y0 = h * 0.90f;
    const float dy = y0 - cy;
    const float halfSpan = sqrtf(fmaxf(0.0f, rDisp * rDisp - dy * dy)) * 0.7f;
    for (int i = 0; i < count; i++) {
        wisps[i] = {cx + (nextRandf(rng) * 2.0f - 1.0f) * halfSpan,
                    y0,
                    nextRandf(rng) * 6.2831853f,
                    nextRandf(rng) * 6.2831853f,
                    0.00045f + nextRandf(rng) * 0.0003f,
                    0.0013f + nextRandf(rng) * 0.0007f};
    }
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < BLOBS_PER_WISP; k++) {
            const int idx = i * BLOBS_PER_WISP + k;
            const float lifetime = 4200.0f + nextRandf(rng) * 1800.0f;
            blobs[idx] = {static_cast<uint8_t>(i), tMs - static_cast<uint32_t>(nextRandf(rng) * lifetime), lifetime,
                          nextRandf(rng) * 6.2831853f};
        }
    }
    builtCount = count;
}

bool init(int, int h) {
    if (blobs == nullptr) {
        blobs = static_cast<Blob *>(alloc(BLOBS_MAX * sizeof(Blob))); // frame()-only: cold, PSRAM
        draws = static_cast<BlobDraw *>(allocPreferHot(BLOBS_MAX * sizeof(BlobDraw)));
        // A fresh allocation holds garbage, not zeros, so no previous build
        // survives in it. builtCount is a file-scope static that would survive,
        // and if it happened to match the requested count buildWisps() would be
        // skipped and frame() would read uninitialised blobs.
        builtCount = -1;
    }
    if (blobs == nullptr || draws == nullptr) {
        return false;
    }
    if (alphaLUT == nullptr) {
        alphaLUT = static_cast<uint8_t *>(allocPreferHot(ALPHA_LUT_N));
        allocH = h;
        bgLUT = static_cast<uint16_t *>(allocPreferHot(h * sizeof(uint16_t)));
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
        // 16 B, always taken from allocHot in practice (this animation's
        // total ask is a small fraction of the slab -- see the comment
        // above allocPreferHot). Flagged because it is the one table here
        // whose alignment actually matters: fillRowPie loads it with
        // ee.vld.128.ip, which silently masks the low four address bits
        // instead of faulting on a misaligned pointer. allocHot rounds
        // every request up to 16 B and hands out offsets from a 16-byte-
        // aligned slab base (BgAnimCommon.cpp's hotRound), so this is safe
        // as long as it actually comes from the slab; alloc()'s PSRAM
        // fallback (reachable only if the slab were ever exhausted, which
        // this animation's footprint does not do) carries no such
        // guarantee and would need an explicit align-up if that path were
        // ever exercised.
        fillBcast = static_cast<uint16_t *>(allocPreferHot(8 * sizeof(uint16_t)));
        if (alphaLUT == nullptr || bgLUT == nullptr || fillBcast == nullptr) {
            return false;
        }
#else
        if (alphaLUT == nullptr || bgLUT == nullptr) {
            return false;
        }
#endif
        g_h = h;
        for (int i = 0; i < ALPHA_LUT_N; i++) {
            const float norm = sqrtf(i / 63.0f);
            const float a = powf(fmaxf(0.0f, 1.0f - norm), 1.6f);
            alphaLUT[i] = static_cast<uint8_t>(a * 255.0f);
        }
        rebuildBg();
        lastThemeGen = themeGen();
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        rebuildBg();
        lastThemeGen = themeGen();
    }
    const int count = 2 + (p[1] * 3) / 100;
    if (count != builtCount) {
        buildWisps(count, w, h, tMs);
    }
    wispCount = count;
    const float riseSpeed = 0.034f * speedMul(p[0]);
    const float swirl = 0.5f + (p[2] / 100.0f) * 1.7f;
    const float density = 0.5f + (p[3] / 100.0f) * 0.8f;
    const float maxHeight = h * 0.62f;
    // Blob radii and sway amplitudes below were written as absolute pixel
    // counts, which silently assumed the render target is always 480 wide.
    // It is not: the renderer can compute the animation at half resolution and
    // double it on the way out, and an absolute radius makes the blobs cover
    // four times the relative area there -- so the stamping cost stays flat
    // while everything else quarters. Scaling them to the render width keeps
    // the picture identical at any resolution and makes the cost scale with it.
    const float rscale = w * (1.0f / 480.0f);

    g_active = wispCount * BLOBS_PER_WISP;
    for (int i = 0; i < g_active; i++) {
        Blob &b = blobs[i];
        BlobDraw &d = draws[i];
        float age = static_cast<float>(tMs - b.birth);
        if (age > b.lifetime) {
            b.birth = tMs - static_cast<uint32_t>(fmodf(age, b.lifetime));
            age = static_cast<float>(tMs - b.birth);
        }
        const float L = age / b.lifetime;
        const Wisp &wp = wisps[b.wisp];
        const float rise = riseSpeed * age;
        const float heightFrac = fminf(1.0f, rise / maxHeight);
        const float y = wp.y0 - rise;
        const float R = (10.0f + 26.0f * heightFrac) * (0.85f + 0.3f * fastSinRad(b.seed)) * rscale;
        const float swayAmp = (5.0f + 22.0f * heightFrac) * swirl * rscale;
        const float x = wp.x0 + swayAmp * fastSinRad(wp.swayFreq1 * tMs + wp.swayPhase1 + b.seed) +
                        swayAmp * 0.35f * fastSinRad(wp.swayFreq2 * tMs + wp.swayPhase2 + b.seed * 1.7f);
        float alpha = density * 0.44f * 4.0f * L * (1.0f - L) * (1.0f - heightFrac * 0.3f);
        if (alpha > 0.4f) {
            alpha = 0.4f; // hard cap: steam stays vapor, never opaque
        }
        d.a8 = static_cast<uint8_t>(alpha * 255.0f);
        // Round to nearest pixel/Q16.16 once per blob per frame (cheap:
        // <=55 blobs/frame); the band loop below then stays all-integer.
        d.xi = static_cast<int>(x >= 0.0f ? x + 0.5f : x - 0.5f);
        d.yi = static_cast<int>(y >= 0.0f ? y + 0.5f : y - 0.5f);
        d.Ri = static_cast<int>(R + 0.5f);
        d.scaleQ = static_cast<int32_t>(63.0f * 65536.0f / (R * R) + 0.5f);
        // Wisps ride the theme's bright end, shifting slightly as they rise.
        uint8_t c[3];
        themeRGB(200 + static_cast<int>(heightFrac * 55.0f), c);
        d.r = c[0];
        d.g = c[1];
        d.b = c[2];
        d.visible = d.a8 > 0 && y > -30.0f;
    }
}

// Blob stamp, shared verbatim between bandPortable (bandRef, below) and the
// Xtensa band() further down: only the background fill differs between the
// two (see fillRowPie's header comment for why the fill got a real asm win
// while the stamp did not). Two per-pixel `continue`s, unchanged from the
// pre-asm-pass code -- see the ALPHA_LUT_N comment for why round 3 restored
// them after two rounds of branch-free versions both measured slower on
// the device. GCC 14 still forms a zero-overhead hardware LOOP for the xx
// loop despite the continues (confirmed directly in xtensa-asm14/AnimSteam.S):
// a `continue` just becomes a forward branch to the loop-body's end label,
// which is exactly what the hardware loop's automatic back-edge needs, so
// there is no zero-overhead-loop cost to keeping these tests.
void stampBlobs(uint16_t *dst, int y0, int rows, int w) {
    for (int i = 0; i < g_active; i++) {
        const BlobDraw &d = draws[i];
        if (!d.visible || d.yi + d.Ri < y0 || d.yi - d.Ri >= y0 + rows) {
            continue;
        }
        const int yy0 = d.yi - d.Ri > y0 ? d.yi - d.Ri : y0;
        const int yy1 = d.yi + d.Ri < y0 + rows - 1 ? d.yi + d.Ri : y0 + rows - 1;
        const int xx0 = d.xi - d.Ri > 0 ? d.xi - d.Ri : 0;
        const int xx1 = d.xi + d.Ri < w - 1 ? d.xi + d.Ri : w - 1;
        // Round 2 tried hoisting r/g/b/a8 into locals here, reasoning that
        // GCC couldn't prove they don't alias dst and would reload them
        // from d on every pixel that reached the blend. True as far as it
        // went, but checked against this round's actual xtensa-asm14
        // disassembly of the three-level (blob, row, pixel) loop nest this
        // function has, the hoisted locals didn't stay in registers either
        // -- GCC spilled all four to the stack across the outer loops and
        // reloaded them from THERE every pixel instead, which is the same
        // one-load-per-field-per-pixel cost as reading them from d, plus
        // four extra stores to spill them in the first place. Reading
        // straight from d, as the original did, needs no spill slots: d
        // itself is already live for d.scaleQ, d.xi and d.yi, so its
        // fields cost nothing beyond keeping that one pointer around.
        for (int yy = yy0; yy <= yy1; yy++) {
            const int dy = yy - d.yi;
            const int dy2 = dy * dy;
            uint16_t *row = dst + static_cast<size_t>(yy - y0) * w;
            for (int xx = xx0; xx <= xx1; xx++) {
                const int dx = xx - d.xi;
                // All-integer stamp lookup: (dx*dx+dy2) <= 2*Ri*Ri inside
                // this bbox, so the product with scaleQ (Q16.16) never
                // overflows int32 -- see BlobDraw comment.
                const int32_t idx = ((dx * dx + dy2) * d.scaleQ) >> 16;
                if (idx >= ALPHA_LUT_N) {
                    continue;
                }
                const uint8_t a = (static_cast<uint16_t>(alphaLUT[idx]) * d.a8) >> 8;
                if (a == 0) {
                    continue;
                }
                row[xx] = addScaled565(row[xx], d.r, d.g, d.b, a);
            }
        }
    }
}

// Portable reference: same output as the Xtensa-dispatched band() below,
// pixel for pixel. This is what the host bench builds (band() falls back to
// it directly on non-Xtensa targets) and what SleepAnimation::runAnimTest
// compares the device kernel against via the registry's bandRef field, so
// any algorithmic change here must be mirrored in band() and vice versa --
// see ASM_BRIEF.md's deliverable shape.
void bandPortable(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) {
        const uint16_t c = bgLUT[y0 + r];
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        // Fill two pixels per store: the band buffer is 4-byte aligned and
        // w is even (480), so pairing halves store traffic vs. one s16i/px.
        // Odd-width tail (defensive; never hit at w=480) falls back to a
        // single 16-bit store.
        const uint32_t c2 = (static_cast<uint32_t>(c) << 16) | c;
        uint32_t *row32 = reinterpret_cast<uint32_t *>(row);
        const int pairs = w >> 1;
        for (int x = 0; x < pairs; x++) {
            row32[x] = c2;
        }
        if (w & 1) {
            row[w - 1] = c;
        }
    }
    stampBlobs(dst, y0, rows, w);
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)

// Eight pixels of solid fill colour per store, zero-overhead hardware loop.
//
// The C++ pairs-store loop this replaces never got GCC's zero-overhead LOOP
// (confirmed in xtensa-asm14/AnimSteam.S: only the blob stamp got one) --
// it compiled to a branchy addi/addi/bnez per 2 pixels, and this call runs
// across the WHOLE screen every frame regardless of blob count (240 band()
// calls x 2 rows x 480px), so that per-pair taken branch was paid 230,400
// times a frame for a value that never changes within a row. `bc` must
// point at a 16-byte-aligned buffer holding the fill colour repeated eight
// times (built once per row by the caller, not per pixel); `wr` must be
// 16-byte aligned, true for every row of this panel's band buffer (CLAUDE.md
// "dst rows are 16-byte aligned") -- EE.VST.128.IP masks the low four
// address bits silently instead of trapping, so a misaligned wr would
// corrupt neighbouring pixels rather than fault. w8 (w/8) is passed
// pre-divided and is always > 0: w is always a multiple of 16 on this panel
// (480 full res, 240 half res), never a multiple of 8 only, so the caller's
// scalar tail below is defensive and never actually executes in production.
__attribute__((noinline)) static void fillRowPie(uint16_t *__restrict wr, const uint16_t *__restrict bc, int w8) {
    uint16_t *dst = wr;
    const uint16_t *bcp = bc;
    int n = w8;
    asm volatile("ee.vld.128.ip q0, %[bc], 0\n" // eight copies of the fill colour, loaded once
                 "loop %[n], 1f\n"
                 "ee.vst.128.ip q0, %[dst], 16\n"
                 "1:\n"
                 : [dst] "+r"(dst), [n] "+r"(n)
                 : [bc] "r"(bcp)
                 : "memory");
}

// Round 3 note on what did NOT survive here: before this round shipped,
// GCC 14's actual compile of stampBlobs's inner loop (xtensa-asm14/
// AnimSteam.S) was read instruction by instruction looking for a schedule
// a hand-written scalar mirror could beat -- the same check ASM_BRIEF.md
// asks for. It already reuses just two registers across the idx>=64
// compare and all three channel clamps (63 doubles as the idx bound and
// the 6-bit green clamp; 31 doubles as the 5-bit red/blue clamp and the
// dst-blue field mask) rather than holding four separate constants,
// already interleaves the four BlobDraw byte loads to cover their
// load-use latency, and already folds both `continue`s into forward
// branches inside the same zero-overhead hardware loop the unconditional
// kernels used. Nothing in that reading looked beatable, and the first
// two rounds of this file both shipped a hand-asm kernel that looked
// better under static analysis and then lost on the device -- writing a
// third one on a hunch, with no concrete schedule improvement to point
// to, was not a good bet. stampBlobs ships
// as plain C++ for GCC 14 to compile, unchanged between bandPortable and
// band() below -- see ASM_BRIEF.md's "ships as a call to bandRef" fallback.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) {
        const uint16_t c = bgLUT[y0 + r];
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        const int w8 = w >> 3;
        if (w8 > 0) {
            for (int k = 0; k < 8; k++) {
                fillBcast[k] = c;
            }
            fillRowPie(row, fillBcast, w8);
        }
        for (int x = w8 * 8; x < w; x++) { // defensive tail, see fillRowPie comment
            row[x] = c;
        }
    }
    stampBlobs(dst, y0, rows, w);
}

#else

void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandPortable(dst, y0, rows, w, tMs, p);
}

#endif

void release() {
    releaseTable(blobs, static_cast<size_t>(BLOBS_MAX) * sizeof(Blob));
    releaseTable(draws, static_cast<size_t>(BLOBS_MAX) * sizeof(BlobDraw));
    releaseTable(alphaLUT, ALPHA_LUT_N);
    releaseTable(bgLUT, static_cast<size_t>(allocH) * sizeof(uint16_t));
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    releaseTable(fillBcast, 8 * sizeof(uint16_t));
#endif
    allocH = 0;
    builtCount = -1;
    lastThemeGen = 0xFFFFFFFF;
}

} // namespace

extern const BgAnimation bg_anim_steam;
const BgAnimation bg_anim_steam = {
    "steam",
    "Steam",
    {{"speed", "Rise speed", 50}, {"count", "Wisps", 55}, {"swirl", "Swirl", 45}, {"density", "Density", 50}},
    init,
    frame,
    band,
    release,
    bandPortable,
};

#endif // GAGGIMATE_SIM
