/* Freestanding, harness-mode execution check for AnimLens.cpp.
 * The three production kernel functions below are copied verbatim, including
 * attributes, constraints and asm. The reference independently implements
 * the page's DDA and its expanded-RGB888 blend, dim and RGB565 quantization.
 * Only main enables CP3. Production kernels leave FreeRTOS in charge.
 */
#include <stdint.h>
#define GM_ANIM_IRAM
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void puts_uart(const char *s) {
    while (*s) {
        if (*s == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}
static void dec_uart(uint32_t v) {
    char b[12]; int n = 0;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) UART0_FIFO = (uint8_t)b[--n];
}

GM_ANIM_IRAM __attribute__((noinline)) uint32_t lensRunPairsAsm(uint16_t *out, const uint16_t *tex,
                                                             uint32_t u, uint32_t step, int nPairs) {
    uint32_t t0, t1, packed;
    if (nPairs <= 0) {
        return u;
    }
    if (step == 32768 && (u & 32768) == 0) {
        const uint32_t pairStep = 65536;
        asm volatile("loopnez %[n], 1f\n"
                     "extui %[t0], %[u], 16, 6\n"
                     "addx2 %[t0], %[t0], %[tex]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "add %[u], %[u], %[step]\n"
                     "slli %[t1], %[t0], 16\n"
                     "or %[t1], %[t1], %[t0]\n"
                     "s32i %[t1], %[out], 0\n"
                     "addi %[out], %[out], 4\n"
                     "1:\n"
                     : [out] "+&r"(out), [u] "+&r"(u), [t0] "=&r"(t0), [t1] "=&r"(t1)
                     : [tex] "r"(tex), [step] "r"(pairStep), [n] "r"(nPairs)
                     : "memory");
    } else if (step == 32768) {
        t0 = tex[(u >> 16) & 63];
        u += step;
        const uint32_t pairStep = 65536;
        asm volatile("loopnez %[n], 1f\n"
                     "extui %[t1], %[u], 16, 6\n"
                     "addx2 %[t1], %[t1], %[tex]\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "add %[u], %[u], %[step]\n"
                     "slli %[pack], %[t1], 16\n"
                     "or %[pack], %[pack], %[t0]\n"
                     "s32i %[pack], %[out], 0\n"
                     "mov %[t0], %[t1]\n"
                     "addi %[out], %[out], 4\n"
                     "1:\n"
                     : [out] "+&r"(out), [u] "+&r"(u), [t0] "+&r"(t0), [t1] "=&r"(t1),
                       [pack] "=&r"(packed)
                     : [tex] "r"(tex), [step] "r"(pairStep), [n] "r"(nPairs)
                     : "memory");
        u -= step;
    } else {
        asm volatile("loopnez %[n], 1f\n"
                     "extui %[t0], %[u], 16, 6\n"
                     "add %[u], %[u], %[step]\n"
                     "extui %[t1], %[u], 16, 6\n"
                     "addx2 %[t0], %[t0], %[tex]\n"
                     "addx2 %[t1], %[t1], %[tex]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "add %[u], %[u], %[step]\n"
                     "slli %[t1], %[t1], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "s32i %[t0], %[out], 0\n"
                     "addi %[out], %[out], 4\n"
                     "1:\n"
                     : [out] "+&r"(out), [u] "+&r"(u), [t0] "=&r"(t0), [t1] "=&r"(t1)
                     : [tex] "r"(tex), [step] "r"(step), [n] "r"(nPairs)
                     : "memory");
    }
    return u;
}

// Gather the feather's three data-dependent reads in a hardware loop. The
// GCC staging loop reloaded its radial constants from the stack per pixel;
// this 14-register block retains them, schedules every load-use gap, and
// closes 24 instructions/pixel with LOOP. e is rr2-inner2, de is 2*dx+1.
// Both are fresh at each block. Clamping precedes the Q24 reciprocal multiply
// so its product is at most 2^24 for radii 80..130 (span 1776..2976).
// The temp holding LOOP's count is free to reuse after the instruction has
// copied it to LCOUNT. There are no nested hardware loops or calls here.
GM_ANIM_IRAM __attribute__((noinline)) void lensStage8Asm(uint16_t *stage, const uint16_t *bg,
                                                       const uint16_t *fg, const uint16_t *rim,
                                                       uint32_t uB, uint32_t uL, int e, int de,
                                                       int cap, int inv) {
    int t0, t1, t2;
    const uint32_t stepB = 32768;
    asm volatile("movi %[t2], 8\n"
                 "loop %[t2], 1f\n"
                 "min %[t0], %[e], %[cap]\n"
                 "movi %[t1], 0\n"
                 "max %[t0], %[t0], %[t1]\n"
                 "mull %[t0], %[t0], %[inv]\n"
                 "movi %[t1], 256\n"
                 "srai %[t0], %[t0], 16\n"
                 "sub %[t0], %[t1], %[t0]\n"
                 "extui %[t1], %[ub], 16, 6\n"
                 "s16i %[t0], %[out], 32\n"
                 "addx2 %[t0], %[t0], %[rim]\n"
                 "extui %[t2], %[ul], 16, 6\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "addx2 %[t1], %[t1], %[bg]\n"
                 "addx2 %[t2], %[t2], %[fg]\n"
                 "s16i %[t0], %[out], 48\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t2], %[t2], 0\n"
                 "add %[e], %[e], %[de]\n"
                 "s16i %[t1], %[out], 0\n"
                 "s16i %[t2], %[out], 16\n"
                 "addi %[de], %[de], 2\n"
                 "add %[ub], %[ub], %[step]\n"
                 "addmi %[ul], %[ul], 16384\n"
                 "addi %[out], %[out], 2\n"
                 "1:\n"
                 : [out] "+&r"(stage), [ub] "+&r"(uB), [ul] "+&r"(uL), [e] "+&r"(e),
                   [de] "+&r"(de), [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2)
                 : [bg] "r"(bg), [fg] "r"(fg), [rim] "r"(rim), [cap] "r"(cap), [inv] "r"(inv),
                   [step] "r"(stepB)
                 : "memory");
}

// Eight independent feather pixels. stage holds bg[8], fg[8], alpha[8],
// dim[8], all uint16. The wrapper checks stage and constant alignment, then
// aligns out with a scalar prefix before calling this kernel.
// q0/q1 retain packed RGB565, q2/q3 retain Q8 alpha/dim, q4/q5 are channel
// temporaries, q6 assembles RGB565 and q7 streams the broadcast constants.
// GCC does not allocate q registers; no q clobber syntax exists. SAR is
// set before every multiply mode. Never writes CPENABLE: FreeRTOS owns it.
//
// Unsigned multiply expands masked R/G/B directly: (Rbits*33)>>13,
// (Gbits*65)>>9, (Bbits*33)>>2. Signed multiply preserves the page's
// floor of a negative channel delta. Delta and channel sums stay within
// [-255,255], so signed saturating add/sub never saturate. The last dim
// multiply folds in quantization: >>11 for R/B, >>10 for G, then repack.
// This preserves both page rounding stages exactly for all 65536 colours,
// alpha 0..256 and dim 0..256, including the actual rim's 186..256 range.
// 52 instructions for eight pixels, including all loads/stores (6.5/pixel),
// plus two exposed one-cycle dependencies: the G mask load into AND, and
// the final B multiply into OR. The stage model's warm-SRAM issue floor is
// therefore 6.75 cycles/pixel, excluding scalar gathering and call setup.
// This is an estimate, not a device measurement. The frame tables and
// staging slab matter more than a lower host time, as the other ports found.
// EE.VSUBS.S16 and signed multiply also have execution precedent in
// tools/qemubench/tests/anim_nebula/main.c; this kernel is checked anew.
GM_ANIM_IRAM __attribute__((noinline)) void lensBlend8Asm(uint16_t *out, const uint16_t *stage,
                                                       const uint16_t *ct) {
    asm volatile("ee.vld.128.ip q0, %[in], 16\n"
                 "ee.vld.128.ip q1, %[in], 16\n"
                 "ee.vld.128.ip q2, %[in], 16\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // R mask
                 "ee.vld.128.ip q3, %[in], 16\n"
                 "ee.andq q4, q0, q7\n"
                 "ee.andq q5, q1, q7\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // 33
                 "ssai 13\n"
                 "ee.vmul.u16 q4, q4, q7\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ssai 8\n"
                 "ee.vsubs.s16 q5, q5, q4\n"
                 "ee.vmul.s16 q5, q5, q2\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // R repack 2048
                 "ee.vadds.s16 q5, q5, q4\n"
                 "ssai 11\n"
                 "ee.vmul.u16 q5, q5, q3\n"
                 "ssai 0\n"
                 "ee.vmul.u16 q6, q5, q7\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // G mask
                 "ee.andq q4, q0, q7\n"
                 "ee.andq q5, q1, q7\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // 65
                 "ssai 9\n"
                 "ee.vmul.u16 q4, q4, q7\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ssai 8\n"
                 "ee.vsubs.s16 q5, q5, q4\n"
                 "ee.vmul.s16 q5, q5, q2\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // G repack 32
                 "ee.vadds.s16 q5, q5, q4\n"
                 "ssai 10\n"
                 "ee.vmul.u16 q5, q5, q3\n"
                 "ssai 0\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ee.vld.128.ip q7, %[ct], -80\n" // B mask at 96, rewind to shared 33 at 16
                 "ee.orq q6, q6, q5\n"
                 "ee.andq q4, q0, q7\n"
                 "ee.andq q5, q1, q7\n"
                 "ee.vld.128.ip q7, %[ct], 16\n" // 33
                 "ssai 2\n"
                 "ee.vmul.u16 q4, q4, q7\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ssai 8\n"
                 "ee.vsubs.s16 q5, q5, q4\n"
                 "ee.vmul.s16 q5, q5, q2\n"
                 "ssai 11\n"
                 "ee.vadds.s16 q5, q5, q4\n"
                 "ee.vmul.u16 q5, q5, q3\n"
                 "ee.orq q6, q6, q5\n"
                 "ee.vst.128.ip q6, %[out], 16\n"
                 : [out] "+&r"(out), [in] "+&r"(stage), [ct] "+&r"(ct)
                 :
                 : "memory");
}
static uint16_t blend_ref(uint16_t bg, uint16_t fg, int a, int m) {
    int shifts[3] = {11, 5, 0};
    int masks[3] = {31, 63, 31};
    uint16_t result = 0;
    for (int ch = 0; ch < 3; ch++) {
        int b = (bg >> shifts[ch]) & masks[ch];
        int f = (fg >> shifts[ch]) & masks[ch];
        if (ch == 1) {
            b = (b << 2) | (b >> 4); f = (f << 2) | (f >> 4);
        } else {
            b = (b << 3) | (b >> 2); f = (f << 3) | (f >> 2);
        }
        /* Signed division made explicitly floor, without depending on the
         * reference compiler's signed right-shift semantics. */
        int d = (f - b) * a;
        int interpolated = b + (d >= 0 ? d / 256 : -((-d + 255) / 256));
        int dimmed = interpolated * m / 256;
        int quantized = dimmed / (ch == 1 ? 4 : 8);
        result |= (uint16_t)(quantized << shifts[ch]);
    }
    return result;
}

/* The production featherAsm wrapper below differs only in C++ cast syntax.
 * check_host.sh verifies that copy as well as the verbatim kernel copies.
 * This independent C reference also counts calls, so a rejected alignment
 * must take one scalar call for the entire span and leave stage8 untouched.
 */
static uint16_t *stage8, *pieConst, *rimMul;
static int inner2, span, invSpan;
static const uint32_t GROUND_STEP = 32768u, LENS_STEP = 16384u;
static uint32_t ref_calls, ref_pixels;

static void featherRef(uint16_t *out, const uint16_t *bg, const uint16_t *fg, uint32_t uB,
                       uint32_t uL, int dx, int dy2, int n) {
    ref_calls++;
    ref_pixels += (uint32_t)n;
    for (int i = 0; i < n; i++) {
        int x = dx + i;
        int e = x*x + dy2 - inner2;
        if (e < 0) e = 0;
        if (e > span) e = span;
        int a = 256 - e * invSpan / 65536;
        out[i] = blend_ref(bg[(uB / 65536u) % 64u], fg[(uL / 65536u) % 64u], a, rimMul[a]);
        uB += 32768u;
        uL += 16384u;
    }
}

GM_ANIM_IRAM void featherAsm(uint16_t *out, const uint16_t *bg, const uint16_t *fg, uint32_t uB,
                            uint32_t uL, int dx, int dy2, int n) {
    // allocHot can fall back to PSRAM, whose allocator does not guarantee
    // 16-byte alignment. Slab residue can trigger that even though our own
    // tables fit. PIE masks low address bits instead of trapping, so prove
    // BOTH table addresses before staging or any vector load/store. A
    // misaligned table sends the whole span through the scalar reference.
    if ((((uintptr_t)stage8) | ((uintptr_t)pieConst)) & 15) {
        featherRef(out, bg, fg, uB, uL, dx, dy2, n);
        return;
    }
    // Only real, fully aligned 8-pixel spans reach ee.vst.128.ip. A caller
    // may begin a feather at any x, including a 4-byte-aligned band buffer
    // that is not itself 16-byte aligned. Prefix and tail never overread.
    int prefix = ((int)((16 - (((uintptr_t)out) & 15)) & 15)) / 2;
    if (prefix > n) {
        prefix = n;
    }
    featherRef(out, bg, fg, uB, uL, dx, dy2, prefix);
    out += prefix;
    uB += GROUND_STEP * prefix;
    uL += LENS_STEP * prefix;
    dx += prefix;
    n -= prefix;
    int rr2 = dx * dx + dy2;
    uint16_t *stage = stage8; // All four vectors follow the checked base by 16 B.
    while (n >= 8) {
        lensStage8Asm(stage, bg, fg, rimMul, uB, uL, rr2 - inner2, 2 * dx + 1, span, invSpan);
        lensBlend8Asm(out, stage, pieConst);
        uB += 8 * GROUND_STEP;
        uL += 8 * LENS_STEP;
        rr2 += 16 * dx + 64; // (dx+8)^2 - dx^2, exactly eight DDA steps
        dx += 8;
        out += 8;
        n -= 8;
    }
    featherRef(out, bg, fg, uB, uL, dx, dy2, n);
}

static uint32_t run_ref(uint16_t *out, const uint16_t *tex, uint32_t u, uint32_t step, int pairs) {
    for (int i = 0; i < pairs * 2; i++) {
        out[i] = tex[(u / 65536u) % 64u];
        u += step;
    }
    return u;
}

static uint16_t constants[56] __attribute__((aligned(16)));
static uint16_t stage[32] __attribute__((aligned(16)));
static uint16_t blend_out[24] __attribute__((aligned(16)));
static uint16_t tex[64] __attribute__((aligned(16)));
static uint16_t run_out[512] __attribute__((aligned(16)));
static uint16_t run_want[512] __attribute__((aligned(16)));
static uint32_t failures, first_call, first_lane, first_got, first_want;
static uint32_t run_calls, blend_calls, run_pixels, blend_pixels, stage_calls;
static uint16_t staged_out[48] __attribute__((aligned(16)));
static uint16_t staged_want[48] __attribute__((aligned(16)));
static uint16_t rim[257], tex_reverse[64];
static uint16_t alignment_ct[80] __attribute__((aligned(16)));
static uint16_t alignment_stage[56] __attribute__((aligned(16)));
static uint16_t alignment_out[64] __attribute__((aligned(16)));
static uint16_t alignment_want[64] __attribute__((aligned(16)));
static uint32_t alignment_calls, fallback_calls, alignment_pixels;

static void check(uint32_t got, uint32_t want, uint32_t call, uint32_t lane) {
    if (got != want) {
        if (!failures) {
            first_call = call; first_lane = lane; first_got = got; first_want = want;
        }
        failures++;
    }
}

static void init_tables(void) {
    for (int i = 0; i < 8; i++) {
        constants[i] = 0xf800; constants[8+i] = 33; constants[16+i] = 2048;
        constants[24+i] = 0x07e0; constants[32+i] = 65; constants[40+i] = 32;
        constants[48+i] = 31;
    }
    /* Distinct values for every gather address, including both colour ends. */
    for (int i = 0; i < 64; i++) tex[i] = (uint16_t)(i * 1040u + i);
    tex[0] = 0; tex[63] = 65535;
}

static void check_blend(void) {
    for (int i = 0; i < 24; i++) blend_out[i] = 0x5aa5;
    lensBlend8Asm(blend_out + 8, stage, constants);
    for (int i = 0; i < 24; i++) {
        uint16_t want = 0x5aa5;
        if (i >= 8 && i < 16) {
            int k = i - 8;
            want = blend_ref(stage[k], stage[8+k], stage[16+k], stage[24+k]);
        }
        check(blend_out[i], want, 100000u + blend_calls, (uint32_t)i);
    }
    blend_calls++;
    blend_pixels += 8;
}

static void test_blend(void) {
    /* All 65536 RGB565 words in each input, both positive and negative
     * channel deltas, every alpha/dim value 0..256, all eight SIMD lanes.
     * This includes every output of the p[2]=0 (80/256 brightness) and
     * p[2]=100 (256/256) ramps for any firmware theme. */
    for (uint32_t c = 0; c < 65536; c += 8) {
        for (int i = 0; i < 8; i++) {
            uint32_t v = c + (uint32_t)i;
            stage[i] = (uint16_t)v;
            stage[8+i] = (uint16_t)(65535u - v);
            stage[16+i] = (uint16_t)(v % 257u);
            stage[24+i] = (uint16_t)((v * 73u) % 257u);
        }
        check_blend();
    }
    /* Every alpha x dim pair, with channel endpoints and packed masks.
     * The actual rim uses alpha 1..256 and dim 186..256; testing 0..256
     * includes both ends and every value the 80..130 radius sweep reaches. */
    for (int a = 0; a <= 256; a++) {
        for (int m = 0; m <= 256; m++) {
            for (int i = 0; i < 8; i++) {
                uint16_t b = (i & 1) ? 0xffff : 0;
                uint16_t f = (uint16_t)~b;
                if (i & 2) { b &= 0x07e0; f &= 0xf81f; }
                if (i & 4) { b ^= 0xaaaa; f ^= 0x5555; }
                stage[i] = b; stage[8+i] = f;
                stage[16+i] = (uint16_t)a; stage[24+i] = (uint16_t)m;
            }
            check_blend();
        }
    }
}

/* The radial recurrence and all three staging gathers, independent of
 * the blend arithmetic. Guards protect both ends of the 64-byte stage. */
static void check_stage(uint32_t ub, uint32_t ul, int e, int de, int cap, int inv) {
    for (int i = 0; i < 48; i++) staged_out[i] = staged_want[i] = 0xa55a;
    lensStage8Asm(staged_out + 8, tex, tex_reverse, rim, ub, ul, e, de, cap, inv);
    for (int i = 0; i < 8; i++) {
        int clipped = e < 0 ? 0 : (e > cap ? cap : e);
        int a = 256 - (clipped * inv) / 65536;
        staged_want[8+i] = tex[(ub / 65536u) % 64u];
        staged_want[16+i] = tex_reverse[(ul / 65536u) % 64u];
        staged_want[24+i] = (uint16_t)a;
        staged_want[32+i] = rim[a];
        ub += 32768u; ul += 16384u; e += de; de += 2;
    }
    for (int i = 0; i < 48; i++) check(staged_out[i], staged_want[i], 200000u + stage_calls, (uint32_t)i);
    stage_calls++;
}

static void test_staging(void) {
    for (int k = 0; k <= 256; k++) rim[k] = (uint16_t)((k * 73) % 257);
    for (int k = 0; k < 64; k++) tex_reverse[k] = tex[63-k];
    /* All size knob settings, including 0 and 100, every lens row, and
     * chords crossing both sides of the annulus. Raw e tests below include
     * large signed inputs on both sides of the clamp and near each edge. */
    for (int p = 0; p <= 100; p++) {
        int r = 80 + (p * 50 + 50) / 100;
        int ri = r - 12, ri2 = ri * ri, cap = r * r - ri2, inv = 16777216 / cap;
        for (int y = -r; y <= r; y++) {
            /* A 17-pixel stride covers many fractional cursor starts while
             * keeping the exhaustive colour test inside the 30 s harness. */
            for (int x = -r; x <= r; x += 17) {
                uint32_t ub = 0xffffc000u + (uint32_t)(p * 31 + y) * 32768u;
                uint32_t ul = 0xfffffff0u + (uint32_t)(p * 17 + x) * 16384u;
                check_stage(ub, ul, x*x + y*y - ri2, 2*x+1, cap, inv);
            }
        }
        static const int raw[] = {-2147480000,-1,0,1,1775,1776,1777,2975,2976,2977,2147480000};
        static const int slope[] = {-259,-1,0,1,261};
        for (unsigned i = 0; i < sizeof(raw)/sizeof(raw[0]); i++)
            for (unsigned j = 0; j < sizeof(slope)/sizeof(slope[0]); j++)
                check_stage(0xffffffffu, 0x3fffffu, raw[i], slope[j], cap, inv);
    }
}

static const int pair_counts[] = {0,1,2,3,7,8,15,16,31,32,63,64,119,120,232,233,239,240};
static const uint32_t starts[] = {
    0,1,0x3fff,0x4000,0x7fff,0x8000,0xbfff,0xc000,0xffff,
    0x3f0000,0x3fffff,0xfffffff0u,0xffffffffu
};
static const uint32_t steps[] = {0,16384,32768,65536,0xffffffffu,0x80000000u};

static void check_run(uint32_t u, uint32_t step, int pairs, int offset) {
    for (int i = 0; i < 512; i++) run_out[i] = run_want[i] = 0xa55a;
    uint32_t got_u = lensRunPairsAsm(run_out + offset, tex, u, step, pairs);
    uint32_t want_u = run_ref(run_want + offset, tex, u, step, pairs);
    check(got_u, want_u, run_calls, 512);
    for (int i = 0; i < 512; i++) check(run_out[i], run_want[i], run_calls, (uint32_t)i);
    run_calls++;
    run_pixels += (uint32_t)pairs * 2u;
}

static void test_runs(void) {
    for (unsigned n = 0; n < sizeof(pair_counts)/sizeof(pair_counts[0]); n++)
        for (unsigned u = 0; u < sizeof(starts)/sizeof(starts[0]); u++)
            for (unsigned s = 0; s < sizeof(steps)/sizeof(steps[0]); s++)
                for (int offset = 0; offset < 8; offset += 2)
                    check_run(starts[u], steps[s], pair_counts[n], offset);
    /* Deterministic broad 32-bit cursor/step coverage. Speed parameters
     * change starting cursors, never the two production DDA steps. */
    uint32_t rng = 0x12345678u;
    for (int k = 0; k < 512; k++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        uint32_t u = rng;
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        uint32_t step = k % 3 == 0 ? 32768u : (k % 3 == 1 ? 16384u : rng);
        check_run(u, step, (int)(rng % 241u), (k & 3) * 2);
    }
}

static void test_alignment(void) {
    /* Every halfword offset modulo 16 for both tables and dst, including
     * the 4/8-byte PSRAM alignments. Aligned cases still run real PIE after
     * the output prefix; rejected cases must not write even inside stage8.
     * Guard words surround every table and output, and constants are read
     * only. Span lengths cover empty, short, exact blocks and both tails.
     */
    static const int sizes[] = {0,55,100};
    static const int counts[] = {0,1,7,8,9,15,16,17,31,32,33};
    rimMul = rim;
    for (unsigned s = 0; s < sizeof(sizes)/sizeof(sizes[0]); s++) {
        int r = 80 + (sizes[s] * 50 + 50) / 100;
        inner2 = (r - 12) * (r - 12);
        span = r*r - inner2;
        invSpan = 16777216 / span;
        for (int c = 0; c < 8; c++) {
            for (int j = 0; j < 80; j++) alignment_ct[j] = 0xa55a;
            pieConst = alignment_ct + 8 + c;
            for (int j = 0; j < 56; j++) pieConst[j] = constants[j];
            for (int a = 0; a < 8; a++) {
                stage8 = alignment_stage + 8 + a;
                for (int o = 0; o < 8; o++) {
                    for (unsigned k = 0; k < sizeof(counts)/sizeof(counts[0]); k++) {
                        const int n = counts[k], dx = o & 1 ? r - 24 : -r;
                        const int dy2 = o * o;
                        const uint32_t ub = 0xffffc000u + (uint32_t)o * 32768u;
                        const uint32_t ul = 0xfffffff0u + (uint32_t)k * 16384u;
                        for (int j = 0; j < 56; j++) alignment_stage[j] = 0xa55a;
                        for (int j = 0; j < 64; j++) alignment_out[j] = alignment_want[j] = 0xa55a;
                        featherRef(alignment_want + 8 + o, tex, tex_reverse, ub, ul, dx, dy2, n);
                        ref_calls = ref_pixels = 0;
                        featherAsm(alignment_out + 8 + o, tex, tex_reverse, ub, ul, dx, dy2, n);
                        const uint32_t call = 600000u + alignment_calls;
                        for (int j = 0; j < 64; j++) check(alignment_out[j], alignment_want[j], call, j);
                        for (int j = 0; j < 80; j++) {
                            uint16_t want = j >= 8+c && j < 64+c ? constants[j-8-c] : 0xa55a;
                            check(alignment_ct[j], want, call, 64+j);
                        }
                        for (int j = 0; j < 56; j++) {
                            if (c || a || j < 8+a || j >= 40+a)
                                check(alignment_stage[j], 0xa55a, call, 144+j);
                        }
                        if (c || a) {
                            check(ref_calls, 1, call, 200);
                            check(ref_pixels, (uint32_t)n, call, 201);
                            fallback_calls++;
                        } else {
                            int prefix = (8 - o) % 8;
                            if (prefix > n) prefix = n;
                            check(ref_calls, 2, call, 200);
                            check(ref_pixels, (uint32_t)(prefix + (n - prefix) % 8), call, 201);
                        }
                        alignment_calls++;
                        alignment_pixels += (uint32_t)n;
                    }
                }
            }
        }
    }
}

int main(void) {
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\nrsync\n" : : "r"(cp) : "memory");
    init_tables();
    test_runs();
    test_staging();
    test_blend();
    test_alignment();
    if (!failures) {
        puts_uart("GM_QEMUBENCH_PIE: PASS lens kernels: run_calls="); dec_uart(run_calls);
        puts_uart(" run_pixels="); dec_uart(run_pixels);
        puts_uart(" stage_calls="); dec_uart(stage_calls);
        puts_uart(" stage_pixels="); dec_uart(stage_calls * 8u);
        puts_uart(" blend_calls="); dec_uart(blend_calls);
        puts_uart(" blend_pixels="); dec_uart(blend_pixels);
        puts_uart(" alignment_calls="); dec_uart(alignment_calls);
        puts_uart(" fallback_calls="); dec_uart(fallback_calls);
        puts_uart(" alignment_pixels="); dec_uart(alignment_pixels);
        puts_uart(" mismatches=0 guards=OK\n");
    } else {
        puts_uart("GM_QEMUBENCH_PIE: FAIL lens kernels: mismatches="); dec_uart(failures);
        puts_uart(" first_call="); dec_uart(first_call);
        puts_uart(" lane="); dec_uart(first_lane);
        puts_uart(" got="); dec_uart(first_got);
        puts_uart(" want="); dec_uart(first_want); puts_uart("\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
