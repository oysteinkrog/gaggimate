#ifndef GAGGIMATE_SIM

// "Tide": four broad, soft horizontal bands of theme colour rising and
// falling through each other over a near-black floor. A crossing adds the
// bands' raised-cosine profiles, capped below the top of the theme ramp so
// the overlap keeps its colour. This ports entry 29, id 'tide', including
// the Copper Tide header, in tools/animbench/web/anim_bench.html.
//
// The page's 64-entry bell and four sine paths build one Q4 value per row
// in frame(). bandRef() adds the eight signed Q4 Bayer offsets for the
// absolute y phase, shifts by four, clamps, and gathers eight RGB565 theme
// colours. That pattern repeats across the row. No row pairing, cached
// predecessor, or call-local phase: a single interlaced row is identical
// to that row in a full-frame call.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

// On by default; the portable reference remains available for device A/B.
#ifndef GM_BGANIM_TIDE_ASM
#define GM_BGANIM_TIDE_ASM 1
#endif

namespace {
using namespace bganim;

constexpr int BELL_N = 64;
constexpr int BASE_IDX = 12; // near-black floor between bands
constexpr int SUM_CAP = 214; // overlap headroom below the ramp's 255 endpoint
constexpr int CENTRE = 240;  // page coordinates, deliberately independent of h
constexpr int AMP = 180;     // vertical excursion in pixels, independent of w/h
constexpr size_t DITH_BYTES = 64 * sizeof(int16_t);
constexpr size_t PAT_BYTES = 8 * sizeof(uint16_t);
constexpr size_t ALIGN_PAD = 15; // room to align even an allocHot PSRAM fallback

uint8_t *bell = nullptr;      // 64 B, frame-only profile -> PSRAM
int32_t *rowQ4 = nullptr;     // 4*h B, one read per row -> slab
uint16_t *palette = nullptr;  // 512 B, eight gathers per row -> slab
void *dithOwner = nullptr;    // 128 + 15 B; aligned alias below -> slab
void *patOwner = nullptr;     // 16 + 15 B; aligned alias below -> slab
int16_t *dith = nullptr;
uint16_t *pattern = nullptr;
const int16_t *sine = nullptr; // borrowed shared sine, never release here
int allocH = 0;
int invW = 0;
int lastWidth = -1, lastGlow = -1;
uint32_t lastThemeGen = 0xFFFFFFFF;
bool paletteValid = false;

// At h=480: rowQ4 1,920 + palette 512 + dith 143 + pattern 31 =
// 2,606 requested bytes, 2,608 after the slab's 16-byte allocation rounding.
// Bell is 64 B in PSRAM. At h=240 the slab uses 1,648 B. The shared
// sinLut's 2,048 B belong to the separate 3,072 B boot reserve. Dither and
// pattern keep their allocation owners because release() must never free
// an aligned interior pointer. The extra alignment also covers host fleet
// tests that keep several animations resident and exhaust the shared slab.
void release();

bool init(int, int h) {
    if (allocH != 0 && allocH != h) {
        release();
    }
    sine = sinLut();
    if (sine == nullptr) {
        release();
        return false;
    }
    if (bell == nullptr) {
        bell = static_cast<uint8_t *>(alloc(BELL_N));
    }
    if (rowQ4 == nullptr) {
        allocH = h;
        rowQ4 = static_cast<int32_t *>(allocHot(static_cast<size_t>(h) * sizeof(int32_t)));
    }
    if (palette == nullptr) {
        palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    }
    if (dithOwner == nullptr) {
        dithOwner = allocHot(DITH_BYTES + ALIGN_PAD);
    }
    if (patOwner == nullptr) {
        patOwner = allocHot(PAT_BYTES + ALIGN_PAD);
    }
    if (bell == nullptr || rowQ4 == nullptr || palette == nullptr || dithOwner == nullptr || patOwner == nullptr) {
        // Includes every earlier successful allocation, so a retry starts
        // with no partial state and the slab's live count returns to zero.
        release();
        return false;
    }
    dith = reinterpret_cast<int16_t *>((reinterpret_cast<uintptr_t>(dithOwner) + ALIGN_PAD) & ~uintptr_t(15));
    pattern = reinterpret_cast<uint16_t *>((reinterpret_cast<uintptr_t>(patOwner) + ALIGN_PAD) & ~uintptr_t(15));
    return true;
}

void frame(uint32_t tMs, int, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (!paletteValid || gen != lastThemeGen) {
        buildThemeRamp(palette, 256);
        const float ampQ4 = ditherAmp(palette, 256) * 16.0f / 31.5f;
        for (int k = 0; k < 64; k++) {
            // bayerOffsets(..., unit=16): signed rounding away from zero,
            // exactly the page's lround helper, including negative ties.
            dith[k] = static_cast<int16_t>(lroundf((BAYER8[k] - 31.5f) * ampQ4));
        }
        lastThemeGen = gen;
        paletteValid = true;
    }
    if (lastGlow != p[2]) {
        // Positive Math.round in the page. Integer hundredths avoid a
        // float rounding boundary at a half-integer knob value.
        const int peak = 44 + (static_cast<int>(p[2]) * 62 + 50) / 100; // 44..106
        for (int i = 0; i < BELL_N; i++) {
            bell[i] = static_cast<uint8_t>(lroundf(peak * 0.5f * (1.0f + cosf(static_cast<float>(M_PI) * i / BELL_N))));
        }
        // cosf(pi/2) is a tiny negative float, while JavaScript's double
        // cosine is a tiny positive value. Keep Math.round(peak/2) exact
        // for odd peaks instead of rounding that midpoint down on device.
        bell[BELL_N / 2] = static_cast<uint8_t>((peak + 1) / 2);
        lastGlow = p[2];
    }
    if (lastWidth != p[1]) {
        // JavaScript's binary64 0.70 lands just below the tie at width
        // 45 and 85, so Math.round gives 31 and 59 there. Preserve those
        // two page values explicitly without soft-double math on Xtensa.
        const int tieDown = p[1] == 45 || p[1] == 85 ? 1 : 0;
        const int halfW = 60 + (static_cast<int>(p[1]) * 70 + 50) / 100 - tieDown; // 60..130 px
        invW = BELL_N * 256 / halfW; // floor, Q8 bell entries per pixel: 126..273
        lastWidth = p[1];
    }

    // Milliseconds times the same exponential speed curve as the page.
    // These are absolute-time phases, not accumulated deltas. Float is
    // intentional in frame(); no double libm on the device. At long uptime
    // float rounding can move a phase bucket relative to JavaScript double,
    // but the four periods and speeds are unchanged. uint64 conversion
    // before masking stays defined even at UINT32_MAX milliseconds.
    const float t = static_cast<float>(tMs) * speedMul(p[0]);
    const int ph0 = static_cast<int>(static_cast<uint64_t>(t * (1024.0f / 23000.0f)) & 1023u);
    const int ph1 = static_cast<int>((static_cast<uint64_t>(t * (1024.0f / 31000.0f)) + 296u) & 1023u);
    const int ph2 = static_cast<int>((static_cast<uint64_t>(t * (1024.0f / 41000.0f)) + 611u) & 1023u);
    const int ph3 = static_cast<int>((static_cast<uint64_t>(t * (1024.0f / 53000.0f)) + 858u) & 1023u);
    // The page uses an arithmetic >>9 on signed sine*180. Explicit floor
    // division preserves that rule on a portable C++17 implementation too.
    const int cy[4] = {
        CENTRE + (sine[ph0] * AMP + 512 * AMP) / 512 - AMP,
        CENTRE + (sine[ph1] * AMP + 512 * AMP) / 512 - AMP,
        CENTRE + (sine[ph2] * AMP + 512 * AMP) / 512 - AMP,
        CENTRE + (sine[ph3] * AMP + 512 * AMP) / 512 - AMP,
    };
    for (int y = 0; y < h; y++) {
        int sum = BASE_IDX;
        for (int k = 0; k < 4; k++) {
            const int delta = y - cy[k];
            const int d = delta < 0 ? -delta : delta;
            const int q = (d * invW) >> 8;
            if (q < BELL_N) {
                sum += bell[q];
            }
        }
        rowQ4[y] = (sum > SUM_CAP ? SUM_CAP : sum) << 4;
    }
}

GM_ANIM_IRAM void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        const int16_t *off = dith + (y & 7) * 8;
        const int base = rowQ4[y];
        for (int k = 0; k < 8; k++) {
            const int q4 = base + off[k];
            // Clamp before the shift to avoid an implementation-defined
            // negative signed shift; clamp(q4 >> 4,0,255) is identical.
            const int idx = q4 < 0 ? 0 : (q4 > 4080 ? 255 : q4 >> 4);
            pattern[k] = palette[idx];
        }
        uint16_t *out = dst + static_cast<size_t>(r) * w;
        for (int x = 0; x < w; x++) {
            out[x] = pattern[x & 7];
        }
    }
}

#if GM_BGANIM_TIDE_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14.2's compiled bandRef, read before writing this kernel:
//   .L9: extui idx,x,0,3; addx2 addr,idx,pat; l16ui c,addr,0;
//        addi x,x,1; s16i c,out,0; addi out,out,2;
//        addi n,n,-1; bnez n,.L9
// Eight instructions/pixel, including the back edge; its load-use gap is
// already filled by x++. Merely transcribing that schedule into LOOPNEZ
// removes two instructions and the taken branch. The additional edge is
// that all eight colours repeat: hold the pattern in a q register and
// replace the entire scalar fill body with EE.VST.128.IP, one instruction
// per eight pixels, including pointer advance. No per-pixel table traffic.
//
// PIE also adds all eight signed Q4 dither values to a broadcast base in
// one instruction. Shift/clamp and palette gathers stay in a scalar pair
// loop, 16 instructions/pair, four trips per row. This avoids additional
// vector constant loads and shift setup for only eight samples at BAND_H=2.
// Every load has an independent instruction before its consumer, including
// the high-half colour load before the pack. Scratch holds Q4 sums, then
// RGB565 colours in place; the gather has no vector equivalent.
//
// Production base is 192..3424, dith is -256..256, sum -64..3680. The
// signed saturating add is exact there. Even for arbitrary int16 inputs
// it is equivalent after the final 0..255 index clamp, which QEMU tests.
// off and scratch are 16-byte aligned by init(), including slab fallback.
// A scalar prefix aligns out; SAR_BYTE then rotates the repeating pattern
// by that prefix's byte length, preserving x&7 on 4-byte-aligned rows at
// 466/233 widths as well as 480/240. The tail writes only remaining pixels.
// EE.SRC.Q with identical sources was probed in QEMU at all 16 offsets
// before use. Its one loaded register rotates within itself, with no read
// beyond scratch's 16 bytes.
//
// GCC never allocates q registers, so q0/q1 need no compiler clobbers.
// No CPENABLE write: FreeRTOS owns lazy CP3 enable and its q/SAR_BYTE state.
// There are no calls while q registers are live and no nested hardware
// loops. IRAM avoids flash instruction fetch competing with panel traffic.
// The device's store bandwidth and task preemption still decide timing;
// host instruction counts and QEMU are correctness evidence only.
GM_ANIM_IRAM __attribute__((noinline)) void tideRowAsm(uint16_t *out, const int16_t *off,
                                                     const uint16_t *pal, int base, int w,
                                                     uint16_t *scratch) {
    if (w <= 0) {
        return;
    }
    const uint32_t packedBase = (uint32_t)(uint16_t)base * 0x00010001u;
    uint16_t *sp = scratch;
    int a, b, zero, cap, n;
    asm volatile("movi    %[zero], 0\n"
                 "movi    %[cap], 255\n"
                 "movi    %[n], 4\n"
                 "ee.vld.128.ip q0, %[off], 0\n"
                 "ee.movi.32.q q1, %[base], 0\n"
                 "ee.movi.32.q q1, %[base], 1\n"
                 "ee.movi.32.q q1, %[base], 2\n"
                 "ee.movi.32.q q1, %[base], 3\n"
                 "ee.vadds.s16 q0, q0, q1\n"
                 "ee.vst.128.ip q0, %[sp], 0\n"
                 "loop    %[n], 1f\n"
                 "l16si   %[a], %[sp], 0\n"
                 "l16si   %[b], %[sp], 2\n"
                 "srai    %[a], %[a], 4\n"
                 "srai    %[b], %[b], 4\n"
                 "max     %[a], %[a], %[zero]\n"
                 "max     %[b], %[b], %[zero]\n"
                 "min     %[a], %[a], %[cap]\n"
                 "min     %[b], %[b], %[cap]\n"
                 "addx2   %[a], %[a], %[pal]\n"
                 "addx2   %[b], %[b], %[pal]\n"
                 "l16ui   %[b], %[b], 0\n"
                 "l16ui   %[a], %[a], 0\n"
                 "slli    %[b], %[b], 16\n"
                 "or      %[a], %[a], %[b]\n"
                 "s32i    %[a], %[sp], 0\n"
                 "addi    %[sp], %[sp], 4\n"
                 "1:\n"
                 : [off] "+&r"(off), [sp] "+&r"(sp), [a] "=&r"(a), [b] "=&r"(b),
                   [zero] "=&r"(zero), [cap] "=&r"(cap), [n] "=&r"(n)
                 : [base] "r"(packedBase), [pal] "r"(pal)
                 : "memory");

    int prefix = 0;
    while (prefix < w && ((uintptr_t)out & 15u) != 0) {
        *out++ = scratch[prefix++];
    }
    const int remaining = w - prefix;
    const uint16_t *rotated = scratch + prefix;
    // USAR loads the aligned block containing rotated, and records its
    // low address bits. SRLI fills the vector load-use slot before SRC.Q.
    asm volatile("ee.ld.128.usar.ip q0, %[pat], 0\n"
                 "srli    %[n], %[remaining], 3\n"
                 "ee.src.q q1, q0, q0\n"
                 "loopnez %[n], 1f\n"
                 "ee.vst.128.ip q1, %[out], 16\n"
                 "1:\n"
                 : [pat] "+&r"(rotated), [out] "+&r"(out), [n] "=&r"(n)
                 : [remaining] "r"(remaining)
                 : "memory");
    for (int i = 0; i < (remaining & 7); i++) {
        out[i] = scratch[(prefix + i) & 7];
    }
}
#endif

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if GM_BGANIM_TIDE_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    (void)tMs;
    (void)p;
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        tideRowAsm(dst + static_cast<size_t>(r) * w, dith + (y & 7) * 8, palette, rowQ4[y], w, pattern);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(bell, BELL_N);
    releaseTable(rowQ4, static_cast<size_t>(allocH) * sizeof(int32_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    bganim::release(dithOwner, DITH_BYTES + ALIGN_PAD);
    bganim::release(patOwner, PAT_BYTES + ALIGN_PAD);
    dith = nullptr;
    pattern = nullptr;
    sine = nullptr;
    allocH = invW = 0;
    lastWidth = lastGlow = -1;
    lastThemeGen = 0xFFFFFFFF;
    paletteValid = false;
}

} // namespace

extern const BgAnimation bg_anim_tide;
const BgAnimation bg_anim_tide = {
    "tide",
    "Tide",
    {{"speed", "Speed", 50},
     {"width", "Band width", 50},
     {"glow", "Glow", 55},
     {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
