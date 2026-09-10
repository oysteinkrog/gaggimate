#ifndef GAGGIMATE_SIM

// "Ribbon": one broad vertical strip twisting into a slow helix, with a
// rounded waist, a soft halo and a faint vertical wash behind it. This is
// entry 34, 'ribbon', in tools/animbench/web/anim_bench.html, including its
// softened design: 26 px half waist, 18 px halo, and restrained face shading.
//
// Eight sliders. Speed, Ribbon width, Twist and Brightness are the original
// four. Waist sets the half width left where the twist crosses (4, 26 or 52
// design px), Edge glow scales the halo profile (none, the design, or twice
// and clipped), Face shading scales the front and back lighting swings, and
// Backdrop scales the vertical wash behind the strip. All four of the new
// ones act in frame() or on the halo table, so bandRef and the three Xtensa
// kernels are exactly what they were. At slider 50 each one reproduces the
// constant it replaced by construction, so the goldens do not move.
//
// Coordinates and half widths are Q8 in the preview's 480 px design space.
// At 480 wide each output pixel advances 256 units; at 240 it advances 512,
// so the half-resolution path samples the same picture. Other widths truncate
// that step once in frame(): at 466 the row drifts less than 1.26 design px,
// at 233 less than 0.35 px. The y coordinate covers the preview's 480 rows. At 480
// every geometric constant and integer operation is the page's own.
//
// Two sine reads per row set width and face lighting. sqrt(sw*sw + 26*26)
// rounds the twist joins. The face walks an index accumulator in Q8, with a
// truncated step across the whole face. The halo samples the page's 289-byte
// cosine mixture at 1/16 px and blends the appropriate face edge into the
// background. Its division by 256, including the 255 crest, is intentional.
// A fixed 8x8 Bayer dither of amplitude 2.2 is added in palette-index units.
// The 256-entry theme ramp uses brightness 176 + round(bright * 0.8).

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#ifndef GM_BGANIM_RIBBON_ASM
#define GM_BGANIM_RIBBON_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int DESIGN = 480;
constexpr int CX_Q8 = 240 * 256;
constexpr int GLOW = 18;
constexpr int GT_N = GLOW * 16 + 1; // distance buckets 0..288, inclusive
constexpr int WAIST = 26;      // half width at the joins with Waist at 50
constexpr int FACE_RANGE = 64; // hi - lo = (base + 34) - (base - 30)
constexpr int GLOW_DEF = 50; // Edge glow default; equals the slider default in
                             // the table below, so a device left at the default
                             // never rebuilds the halo table at all
// The face index window. Face shading at 100 doubles the lighting swing, which
// would drive lo to -10 on the darkest row and 181 on the brightest; the face
// runs lo..lo+64 and the kernels gather the palette without a clamp, so lo is
// held inside this window. At slider 50 lo is 30..125 and neither bound binds,
// which is why the goldens do not move.
constexpr int LO_MIN = 8;
constexpr int LO_MAX = 150;

struct Row {
    uint16_t hw, x0, x1;
    uint16_t loBg; // lo in high byte, background index in low byte
    int32_t acc, inc; // Q8 index at x0, Q8 index step per output pixel
};
static_assert(sizeof(Row) == 16, "Ribbon's slab budget depends on a 16-byte row");

Row *rowRec = nullptr;
uint16_t *palette = nullptr;
uint8_t *gt = nullptr;
int16_t *dith = nullptr;
int16_t *faceWork = nullptr; // eight biased Q8 accumulators for the PIE span
const int16_t *sine = nullptr; // borrowed shared table, never released here
int allocH = 0;
int pixelQ8 = 256;
int lastBright = -1;
int lastGlow = -1;
uint32_t lastThemeGen = 0xFFFFFFFFu;

// At h=480: rowRec 7,680 B, palette 512 B, gt 289 B (304 B aligned),
// dith 128 B, faceWork 16 B. All are read per pixel or per row and use
// allocHot: 8,640 B of 9,216 B including alignment, no PSRAM tables.
// sinLut's 2,048 B belong to the separate 3,072 B shared reservation.
void release();

// The halo profile, sampled at 1/16 px out to the full 18 px. gp is the Edge
// glow slider and gp/50 scales the whole profile: at 50 the divisor makes the
// gain exactly 1.0f and a multiply by 1.0f cannot change a float, so this is
// the page's own mixture bit for bit. At 0 the table is all zeroes and the
// face has a hard edge with no halo; at 100 the profile is doubled and clipped
// at 255, which widens the part that is full face colour and leaves the outer
// falloff smooth. 289 cosf pairs, paid on a change and never per frame: init()
// builds the default and frame() rebuilds only when the slider differs from it.
void buildHalo(int gp) {
    const float gain = static_cast<float>(gp) / 50.0f;
    for (int k = 0; k < GT_N; k++) {
        // The page mixes a 42% raised-cosine shoulder ending at 3.5 px
        // with a 58% raised-cosine skirt ending at the full 18 px halo.
        const float u = k * (1.0f / 16.0f);
        const float nearU = u < 3.5f ? u * (1.0f / 3.5f) : 1.0f;
        const float near = 0.5f * (1.0f + cosf(static_cast<float>(M_PI) * nearU));
        const float far = 0.5f * (1.0f + cosf(static_cast<float>(M_PI) * u * (1.0f / GLOW)));
        const long v = lroundf(255.0f * (0.42f * near + 0.58f * far) * gain);
        gt[k] = static_cast<uint8_t>(v > 255 ? 255 : v);
    }
}

bool init(int w, int h) {
    // 960 B is the aligned total of the four fixed-size hot tables below.
    if (w <= 0 || w > 65535 || h <= 0 ||
        static_cast<size_t>(h) * sizeof(Row) + 960 > HOT_SLAB_BYTES - HOT_SHARED_RESERVE) {
        release();
        return false;
    }
    if (rowRec != nullptr && allocH == h) return true;
    release();
    sine = sinLut();
    if (sine == nullptr) {
        release();
        return false;
    }
    allocH = h; // recorded before allocations so every failure can release
    rowRec = static_cast<Row *>(allocHot(static_cast<size_t>(h) * sizeof(Row)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    gt = static_cast<uint8_t *>(allocHot(GT_N));
    dith = static_cast<int16_t *>(allocHot(64 * sizeof(int16_t)));
    faceWork = static_cast<int16_t *>(allocHot(8 * sizeof(int16_t)));
    if (rowRec == nullptr || palette == nullptr || gt == nullptr || dith == nullptr || faceWork == nullptr) {
        release();
        return false;
    }
    buildHalo(GLOW_DEF);
    lastGlow = GLOW_DEF;
    for (int k = 0; k < 64; k++) {
        // No half ties occur here, so lroundf and JS Math.round agree even
        // for negative offsets. The result is -2..2, independent of theme.
        dith[k] = static_cast<int16_t>(lroundf((BAYER8[k] - 31.5f) * (2.2f / 31.5f)));
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (lastBright != p[3] || lastThemeGen != gen) {
        // Unlike most fleet entries, this page explicitly scales the ramp.
        // Integer round(p[3]*4/5) exactly matches its Math.round(p[3]*0.8).
        buildThemeRamp(palette, 176 + (static_cast<int>(p[3]) * 4 + 2) / 5);
        lastBright = p[3];
        lastThemeGen = gen;
    }
    if (lastGlow != static_cast<int>(p[5])) {
        buildHalo(p[5]);
        lastGlow = p[5];
    }
    // Time remains a pure function of tMs, including speed changes. Q24
    // carries the float speed multiplier over the 0..100 range; the 64-bit
    // product stays below 2^59 even at millis() wrap. Reduce each cycle
    // before converting to float so days of uptime do not erase sub-row
    // phase precision. JS uses double speed math; its last bits can drift
    // from this float speed at long uptimes, on host and device alike.
    const uint32_t speedQ24 = static_cast<uint32_t>(speedMul(p[0]) * 16777216.0f);
    const uint64_t ttQ24 = static_cast<uint64_t>(tMs) * speedQ24;
    const float tPhase = static_cast<float>(ttQ24 % (24000ull << 24)) *
                         (1024.0f / (24000.0f * 16777216.0f)); // 24 s twist
    const float bgPh = static_cast<float>(ttQ24 % (21000ull << 24)) *
                       (1024.0f / (21000.0f * 16777216.0f)); // 21 s wash
    // Twist scales a 600-design-pixel pitch by 0.45..1.55. At twist=50
    // this is one sine turn per 600 rows, or 0.8 turns over the panel.
    const float ky = (1024.0f / 600.0f) * (0.45f + p[2] * 0.011f);
    const float halfMax = 58.0f + p[1] * 1.15f; // 58..173 px; 115.5 at width=50
    // Waist: the half width left where the twist crosses, in design px. Two
    // integer segments, 4..26..52, so slider 50 lands on the page's own WAIST
    // and the squared term below is the same int 676 the constant produced.
    // The floor of 4 keeps the face at least 8 px wide, which is what stops
    // the per-row division by the span from seeing zero and holds the face
    // index step small enough for the kernels' int16 accumulators.
    const int wp = p[4];
    const int waistPx = wp <= 50 ? 4 + ((WAIST - 4) * wp + 25) / 50 : WAIST + (WAIST * (wp - 50) + 25) / 50;
    const int waistSq = waistPx * waistPx;
    // Face shading: the front and back lighting swings, the page's 56 and 40 at
    // slider 50 (both integer divisions land on them exactly), 0 at 0 for a
    // flat strip with no light and dark faces, and double at 100.
    const int fwd = (56 * static_cast<int>(p[6]) + 25) / 50;
    const int back = (40 * static_cast<int>(p[6]) + 25) / 50;
    // Backdrop: the swing of the vertical wash in palette indices, the page's
    // 9 at slider 50, 0 at 0 for a flat ground and 18 at 100.
    const int washAmp = (9 * static_cast<int>(p[7]) + 25) / 50;
    const float yScale = static_cast<float>(DESIGN) / h;
    pixelQ8 = DESIGN * 256 / w;

    for (int y = 0; y < h; y++) {
        const float yy = y * yScale;
        const int th = static_cast<int>(tPhase + yy * ky);
        const int s = sine[th & 1023];
        const int c = sine[(th + 256) & 1023]; // quarter-turn offset gives cosine
        const float sw = (s < 0 ? -s : s) * halfMax * (1.0f / 512.0f);
        const int hw = static_cast<int>(sqrtf(sw * sw + waistSq) * 256.0f);
        // Asymmetric lighting: at Face shading 50 the front adds up to 56
        // indices to 100 and the back subtracts up to 40. Face edges are
        // lo and lo + 64, which is base-30 and base+34 while lo is unclamped.
        const int base = 100 + ((c > 0 ? c * fwd : c * back) >> 9);
        int lo = base - 30;
        if (lo < LO_MIN) {
            lo = LO_MIN;
        } else if (lo > LO_MAX) {
            lo = LO_MAX;
        }
        // Background indices 23 +/-9 at Backdrop 50, two sine slots per design row.
        const int bg = 23 + ((sine[static_cast<int>(yy * 2.0f + bgPh) & 1023] * washAmp) >> 9);
        int x0 = (CX_Q8 - hw - GLOW * 256) / pixelQ8;
        int x1 = (CX_Q8 + hw + GLOW * 256) / pixelQ8;
        if (x0 < 0) x0 = 0;
        if (x1 >= w) x1 = w - 1;
        const int span = 2 * hw;
        Row &r = rowRec[y];
        r.hw = static_cast<uint16_t>(hw);
        r.x0 = static_cast<uint16_t>(x0);
        r.x1 = static_cast<uint16_t>(x1);
        r.loBg = static_cast<uint16_t>((lo << 8) | bg);
        r.inc = FACE_RANGE * 256 * pixelQ8 / span;
        // Truncate the SUM, as JS does, not its negative fractional term.
        // A 64-bit numerator avoids overflow at the widest, brightest row.
        const int delta = x0 * pixelQ8 - CX_Q8 + hw;
        r.acc = static_cast<int>((static_cast<int64_t>(lo) * 256 * span +
                                  static_cast<int64_t>(delta) * FACE_RANGE * 256) / span);
    }
}

// The portable specification follows the page's pixel cases literally.
// Negative lighting and halo products use arithmetic right shifts, matching
// JS >> on both host and Xtensa. No float, division or libm occurs per pixel.
GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const Row r = rowRec[y]; // snapshot: output stores cannot alias row fields
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        const int bg = r.loBg & 255;
        const int lo = r.loBg >> 8;
        const uint16_t bgCol = palette[bg];
        const int16_t *dr = dith + (y & 7) * 8;
        for (int x = 0; x < r.x0; x++) out[x] = bgCol;
        int acc = r.acc;
        for (int x = r.x0; x <= r.x1; x++, acc += r.inc) {
            const int dq = x * pixelQ8 - CX_Q8;
            const int aq = dq < 0 ? -dq : dq;
            int idx;
            if (aq <= r.hw) {
                idx = acc >> 8;
            } else {
                const int d = (aq - r.hw) >> 4;
                if (d >= GT_N) {
                    out[x] = bgCol;
                    continue;
                }
                const int src = dq < 0 ? lo : lo + FACE_RANGE;
                idx = bg + (((src - bg) * gt[d]) >> 8);
            }
            idx += dr[x & 7];
            idx = idx < 0 ? 0 : idx > 255 ? 255 : idx;
            out[x] = palette[idx];
        }
        for (int x = r.x1 + 1; x < w; x++) out[x] = bgCol;
    }
}

#if GM_BGANIM_RIBBON_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// The first GCC 14 compile is retained in the bench build's baseline-ref.S.
// Its .L4/.L10/.L11 face path transcribes to 23 instructions per pixel:
// abs/sub/srai/blt, srai/j, extui/addx2/l16si/add, movi/min/movi/max,
// l32i/extui/addx2/l16ui, s16i/addi/add/add/addi. It already uses LOOP,
// but the dither load feeds ADD immediately and the palette load feeds the
// store immediately. The halo also spills its table and edge values.
//
// The edge beyond that schedule is to split the row into its known spans.
// PIE advances eight face accumulators, with Bayer offsets folded into the
// initial lanes. The accumulators are biased by -32768, so VADDS.S16 can
// cross index 128 without signed saturation; XORQ restores the unsigned Q8
// bits for the gathers. At panel widths 233..480 the indices stay in 27..192
// throughout the face, including dither, so no palette clamp is needed.
// All live biased accumulators fit int16, so saturation never changes them.
// The last, unused vector advance is harmless. Eight indices never spill to
// scratch between vector math and scalar gathers: MOVI.32.A reads the lanes.
//
// The compiler does not allocate q0-q7 and has no q-register clobber syntax.
// These blocks own the q registers they name. They never write CPENABLE;
// FreeRTOS handles CP3 lazily. MOVI.32.A/Q and XORQ were separately probed
// under QEMU before writing these kernels. Device parity and timing still
// decide whether this flag should stay on, regardless of host timings.

// BEGIN VERBATIM PRODUCTION KERNELS
// Prefix and tail are scalar; only fully aligned 16-byte destinations reach
// VST, which silently masks address bits. The main loop is one instruction
// per eight background pixels, plus setup once per span.
GM_ANIM_IRAM __attribute__((noinline)) void ribbonFillAsm(uint16_t *out, int color, int n) {
    if (n <= 0) return;
    while (n > 0 && ((uintptr_t)out & 15u) != 0) {
        *out++ = (uint16_t)color;
        --n;
    }
    const int blocks = n >> 3;
    if (blocks != 0) {
        const uint32_t packed = (uint32_t)color | ((uint32_t)color << 16);
        asm volatile("ee.movi.32.q q0, %[color], 0\n"
                     "ee.movi.32.q q0, %[color], 1\n"
                     "ee.movi.32.q q0, %[color], 2\n"
                     "ee.movi.32.q q0, %[color], 3\n"
                     "loopnez %[n], 1f\n"
                     "ee.vst.128.ip q0, %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out)
                     : [color] "r"(packed), [n] "r"(blocks)
                     : "memory");
    }
    for (int i = 0; i < (n & 7); i++) *out++ = (uint16_t)color;
}

// The distance is nonnegative Q8, stepped toward or away from the face.
// Caller trims buckets outside 0..288, exactly the page's no-dither case.
// slope is src-bg, -2..176; bg is 14..32; dr is -2..2. The result stays
// inside 12..192, so the page's clamp is redundant for every parameter set.
// 16 instructions per halo pixel, one MULL, no immediate load consumers.
// The independent bg add hides the profile load; distance stepping hides
// the palette load. The two serial gathers make this a scalar LOOPNEZ.
GM_ANIM_IRAM __attribute__((noinline)) void ribbonHaloAsm(uint16_t *out, const uint8_t *profile,
                                                       const uint16_t *pal, const int16_t *dr,
                                                       int x, int distance, int step, int slope, int bg, int n) {
    if (n <= 0) return;
    int t0, t1;
    asm volatile("loopnez %[n], 1f\n"
                 "extui %[t0], %[x], 0, 3\n"
                 "addx2 %[t0], %[t0], %[dr]\n"
                 "srai %[t1], %[d], 4\n"
                 "l16si %[t0], %[t0], 0\n"
                 "add %[t1], %[t1], %[gt]\n"
                 "l8ui %[t1], %[t1], 0\n"
                 "add %[t0], %[t0], %[bg]\n"
                 "mull %[t1], %[t1], %[slope]\n"
                 "addi %[x], %[x], 1\n"
                 "srai %[t1], %[t1], 8\n"
                 "add %[t0], %[t0], %[t1]\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "add %[d], %[d], %[step]\n"
                 "s16i %[t0], %[out], 0\n"
                 "addi %[out], %[out], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [x] "+&r"(x), [d] "+&r"(distance),
                   [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [gt] "r"(profile), [pal] "r"(pal), [dr] "r"(dr), [step] "r"(step),
                   [slope] "r"(slope), [bg] "r"(bg), [n] "r"(n)
                 : "memory");
}

// work is the 16-byte allocHot scratch, fully overwritten on every call.
// The scalar prefix aligns OUT to four bytes for paired S32I stores. The
// only VLD reads work, whose alignment band() proves, never the output or dither.
// Eight complete pixels are required before constructing any vector lanes.
//
// Loop body: XORQ, VADDS.S16, four ten-instruction gather/pack/store pairs,
// ADDI = 43 instructions per eight pixels, 5.375 per face pixel. Each pair
// loads both colors before consuming either, leaving no load-use interlock.
// The issued-op lower bound is 5.375 cycles/pixel, not a device measurement.
GM_ANIM_IRAM __attribute__((noinline)) void ribbonFaceAsm(uint16_t *out, const uint16_t *pal,
                                                       const int16_t *dr, int16_t *work,
                                                       int acc, int inc, int x, int n) {
    if (n <= 0) return;
    if (((uintptr_t)out & 2u) != 0) {
        *out++ = pal[(acc >> 8) + dr[x & 7]];
        acc += inc;
        ++x;
        --n;
    }
    const int blocks = n >> 3;
    if (blocks != 0) {
        for (int i = 0; i < 8; i++) {
            work[i] = (int16_t)(acc + i * inc + dr[(x + i) & 7] * 256 - 32768);
        }
        const uint32_t step8 = (uint32_t)(inc * 8);
        const uint32_t packedStep = step8 | (step8 << 16);
        const uint32_t sign = 0x80008000u; // undo the -32768 bias in each halfword
        uint32_t hi, lo;
        asm volatile("ee.movi.32.q q1, %[step], 0\n"
                     "ee.movi.32.q q1, %[step], 1\n"
                     "ee.movi.32.q q1, %[step], 2\n"
                     "ee.movi.32.q q1, %[step], 3\n"
                     "ee.movi.32.q q2, %[sign], 0\n"
                     "ee.movi.32.q q2, %[sign], 1\n"
                     "ee.movi.32.q q2, %[sign], 2\n"
                     "ee.movi.32.q q2, %[sign], 3\n"
                     "ee.vld.128.ip q0, %[work], 0\n"
                     "loopnez %[n], 1f\n"
                     "ee.xorq q3, q0, q2\n"
                     "ee.vadds.s16 q0, q0, q1\n"
                     "ee.movi.32.a q3, %[hi], 0\n"
                     "extui %[lo], %[hi], 8, 8\n"
                     "extui %[hi], %[hi], 24, 8\n"
                     "addx2 %[lo], %[lo], %[pal]\n"
                     "addx2 %[hi], %[hi], %[pal]\n"
                     "l16ui %[hi], %[hi], 0\n"
                     "l16ui %[lo], %[lo], 0\n"
                     "slli %[hi], %[hi], 16\n"
                     "or %[hi], %[hi], %[lo]\n"
                     "s32i %[hi], %[out], 0\n"
                     "ee.movi.32.a q3, %[hi], 1\n"
                     "extui %[lo], %[hi], 8, 8\n"
                     "extui %[hi], %[hi], 24, 8\n"
                     "addx2 %[lo], %[lo], %[pal]\n"
                     "addx2 %[hi], %[hi], %[pal]\n"
                     "l16ui %[hi], %[hi], 0\n"
                     "l16ui %[lo], %[lo], 0\n"
                     "slli %[hi], %[hi], 16\n"
                     "or %[hi], %[hi], %[lo]\n"
                     "s32i %[hi], %[out], 4\n"
                     "ee.movi.32.a q3, %[hi], 2\n"
                     "extui %[lo], %[hi], 8, 8\n"
                     "extui %[hi], %[hi], 24, 8\n"
                     "addx2 %[lo], %[lo], %[pal]\n"
                     "addx2 %[hi], %[hi], %[pal]\n"
                     "l16ui %[hi], %[hi], 0\n"
                     "l16ui %[lo], %[lo], 0\n"
                     "slli %[hi], %[hi], 16\n"
                     "or %[hi], %[hi], %[lo]\n"
                     "s32i %[hi], %[out], 8\n"
                     "ee.movi.32.a q3, %[hi], 3\n"
                     "extui %[lo], %[hi], 8, 8\n"
                     "extui %[hi], %[hi], 24, 8\n"
                     "addx2 %[lo], %[lo], %[pal]\n"
                     "addx2 %[hi], %[hi], %[pal]\n"
                     "l16ui %[hi], %[hi], 0\n"
                     "l16ui %[lo], %[lo], 0\n"
                     "slli %[hi], %[hi], 16\n"
                     "or %[hi], %[hi], %[lo]\n"
                     "s32i %[hi], %[out], 12\n"
                     "addi %[out], %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out), [hi] "=&r"(hi), [lo] "=&r"(lo)
                     : [work] "r"(work), [pal] "r"(pal), [step] "r"(packedStep),
                       [sign] "r"(sign), [n] "r"(blocks)
                     : "memory");
        const int done = blocks * 8;
        acc += done * inc;
        x += done;
    }
    for (int i = 0; i < (n & 7); i++, x++, acc += inc) {
        *out++ = pal[(acc >> 8) + dr[x & 7]];
    }
}

// Plain arguments keep the exact row splitting executable in the QEMU test
// too. The page's accumulator starts at the outer halo edge, so the face
// must resume acc + (face0-x0)*inc, never restart at lo*256. That distinction
// preserves the small drift from truncating inc, which is part of the look.
GM_ANIM_IRAM __attribute__((noinline)) void ribbonRowAsm(uint16_t *out, const uint16_t *pal,
                                                      const uint8_t *profile, const int16_t *dr,
                                                      int16_t *work, int w, int step, int hw,
                                                      int x0, int x1, int lo, int bg, int acc, int inc) {
    const int center = 240 * 256; // preview coordinate, same as CX_Q8
    const int left = center - hw;
    const int right = center + hw;
    int face0 = (left + step - 1) / step;
    int face1 = right / step + 1;
    if (face0 > w) face0 = w;
    if (face1 > w) face1 = w;
    const int color = pal[bg];
    ribbonFillAsm(out, color, x0);
    ribbonFillAsm(out + x1 + 1, color, w - x1 - 1);
    int x = x0;
    while (x < face0 && ((left - x * step) >> 4) >= 289) {
        out[x++] = (uint16_t)color; // page's out-of-profile case has no dither
    }
    ribbonHaloAsm(out + x, profile, pal, dr, x, left - x * step, -step, lo - bg, bg, face0 - x);
    ribbonFaceAsm(out + face0, pal, dr, work, acc + (face0 - x0) * inc, inc, face0, face1 - face0);
    ribbonHaloAsm(out + face1, profile, pal, dr, face1, face1 * step - right, step,
                  lo + 64 - bg, bg, x1 - face1 + 1);
}
// END VERBATIM PRODUCTION KERNELS
#endif

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_RIBBON_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    // allocHot hands back 16-byte aligned memory while a table sits in the
    // slab, because the slab is aligned and every size rounds up to 16. It
    // falls back to PSRAM once the slab is full, and that allocator promises
    // less. faceWork is the only table a vector load reads, and VLD masks the
    // low four address bits instead of trapping, so a misaligned scratch would
    // silently read the wrong eight accumulators and paint a wrong colour
    // across a face span. Prove it once per call and send a call that cannot
    // use the kernels through the reference instead.
    if ((reinterpret_cast<uintptr_t>(faceWork) & 15u) != 0) {
        bandRef(dst, y0, rows, w, tMs, p);
        return;
    }
    // Only absolute y chooses row records and Bayer phase. Scratch is rebuilt
    // on every row, including lone rows reached in either interlace parity.
    for (int row = 0; row < rows; row++) {
        const int y = y0 + row;
        const Row &r = rowRec[y];
        ribbonRowAsm(dst + static_cast<size_t>(row) * w, palette, gt, dith + (y & 7) * 8, faceWork,
                     w, pixelQ8, r.hw, r.x0, r.x1, r.loBg >> 8, r.loBg & 255, r.acc, r.inc);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(faceWork, 8 * sizeof(int16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(gt, GT_N);
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(rowRec, static_cast<size_t>(allocH) * sizeof(Row));
    sine = nullptr;
    allocH = 0;
    pixelQ8 = 256;
    lastBright = -1;
    lastGlow = -1;
    lastThemeGen = 0xFFFFFFFFu;
}

} // namespace

extern const BgAnimation bg_anim_ribbon;
const BgAnimation bg_anim_ribbon = {
    "ribbon",
    "Ribbon",
    {{"speed", "Speed", 50},
     {"width", "Ribbon width", 50},
     {"twist", "Twist", 50},
     {"bright", "Brightness", 62},
     {"waist", "Waist", 50},
     {"glow", "Edge glow", 50},
     {"shade", "Face shading", 50},
     {"wash", "Backdrop", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
