/* Freestanding harness-mode test. Production kernels below are verbatim.
 * GCC's original coverage loop lives in gcc_baseline.S. The reference
 * evaluates squared distances directly and evaluates both cubic smoothsteps,
 * independently of the production recurrences and lookup table. No libc.
 * Only this bare-metal main enables CP3, once, before the instruction probes.
 */
#include <stdint.h>
#define asm __asm__
#define GM_ANIM_IRAM
#include "pie_probe.h"
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void puts0(const char *s) { while (*s) UART0_FIFO = (uint8_t)*s++; }
static void dec(uint32_t v) {
    char b[11]; int n = 0;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) UART0_FIFO = (uint8_t)b[--n];
}
static uint32_t seed = 0xc2e5ce17u;
static uint32_t rand32(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static uint32_t calls, lanes, mismatches;
static void check(int kernel, int lane, uint32_t got, uint32_t want) {
    if (got == want) return;
    if (mismatches++ == 0) {
        puts0("GM_QEMUBENCH_PIE: FAIL crescent kernel="); dec(kernel);
        puts0(" call="); dec(calls); puts0(" lane="); dec(lane);
        puts0(" got="); dec(got); puts0(" want="); dec(want); puts0("\n");
    }
}
void crescentWeightsGcc(uint16_t *, int, int, int, int, int, int, const uint16_t *, int);
/* BEGIN VERBATIM PRODUCTION KERNELS */
GM_ANIM_IRAM __attribute__((noinline)) void crescentWeightsAsm(uint16_t *out, int qOuter, int dOuter,
                                                               int qCut, int dCut, int r2, int inv,
                                                               const uint16_t *sm, int n) {
    sm += 128;
    int uo, uc, lo, hi;
    asm volatile("movi %[lo], -128\n"
                 "movi %[hi], 128\n"
                 "loopnez %[n], 1f\n"
                 "srai %[uc], %[qc], 8\n"
                 "sub %[uc], %[uc], %[r2]\n"
                 "mull %[uo], %[qo], %[inv]\n"
                 "mull %[uc], %[uc], %[inv]\n"
                 "srai %[uo], %[uo], 16\n"
                 "srai %[uc], %[uc], 16\n"
                 "min %[uo], %[uo], %[hi]\n"
                 "min %[uc], %[uc], %[hi]\n"
                 "max %[uo], %[uo], %[lo]\n"
                 "max %[uc], %[uc], %[lo]\n"
                 "addx2 %[uo], %[uo], %[sm]\n"
                 "addx2 %[uc], %[uc], %[sm]\n"
                 "l16ui %[uo], %[uo], 0\n"
                 "l16ui %[uc], %[uc], 0\n"
                 "add %[qo], %[qo], %[dqo]\n"
                 "mul16u %[uo], %[uo], %[uc]\n"
                 "addi %[dqo], %[dqo], -2\n"
                 "srli %[uo], %[uo], 8\n"
                 "s16i %[uo], %[out], 0\n"
                 "add %[qc], %[qc], %[dqc]\n"
                 "addmi %[dqc], %[dqc], 512\n"
                 "addi %[out], %[out], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [qo] "+&r"(qOuter), [dqo] "+&r"(dOuter),
                   [qc] "+&r"(qCut), [dqc] "+&r"(dCut), [uo] "=&r"(uo), [uc] "=&r"(uc),
                   [lo] "=&r"(lo), [hi] "=&r"(hi)
                 : [r2] "r"(r2), [inv] "r"(inv), [sm] "r"(sm), [n] "r"(n)
                 : "memory");
}

// Eight pixels: narrow the exact 32-bit radCol values, add the factored
// dither/row vector, compute vignette, limb and coverage blend in signed Q4.
// q2..q6 hold row constants, q0/q1 arithmetic and q7 weights. GCC never
// allocates q0..q7, so there is no q-register clobber syntax. FreeRTOS owns
// CPENABLE and lazily saves CP3; this kernel never writes CPENABLE.
//
// col, weight and rowRad are 16-byte aligned by allocHot or aligned stack
// construction. Each iteration reads exactly eight columns and weights.
// Output uses only S32I, so every permitted 4-byte output alignment works,
// including odd-width single rows. A scalar tail handles the last 0..7.
// The output increment fills the last VMUL's use gap: subtract 16 only
// inside asm before LOOPNEZ and restore after it, never access that address.
// All vector loads/multiplies and scalar gathers have independent work in
// their use gaps. Direct MOVI.32.A extraction avoids a scratch-index spill.
// The loop is 57 instructions/eight pixels, 7.125 instructions/pixel, with
// no unfilled dependency gaps. PIE multiplier resource timing and PSRAM
// output traffic still need device timing; host speed is not that evidence.
//
// rad in [-616,9216], bgBase in [416,560], bodyBase in [1034,2070], weight
// in [0,256]: bg in [-304,609], body in [846,4878], the Q4 blend in [-304,4878].
// Thus idx in [-19,304], covered by palette[-32..319]. No signed saturation
// occurs. Padding repeats palette[3] below 3 and palette[255] above 255.
GM_ANIM_IRAM __attribute__((noinline)) void crescentShadeAsm(uint16_t *out, const int32_t *col,
                                                             const uint16_t *weight, const int16_t *rowRad,
                                                             const uint16_t *pal, int bgBase, int body,
                                                             int n) {
    const uint32_t coeffs[4] = {20u, 78u, (uint32_t)bgBase, (uint32_t)body};
    const uint32_t *coeff = coeffs;
    const int blocks = n >> 3;
    int low, high;
    asm volatile("ee.vldbc.16.ip q3, %[coeff], 4\n"
                 "ee.vldbc.16.ip q4, %[coeff], 4\n"
                 "ee.vldbc.16.ip q5, %[coeff], 4\n"
                 "ee.vldbc.16.ip q6, %[coeff], 0\n"
                 "ee.vld.128.ip q2, %[rr], 0\n"
                 "ssai 8\n"
                 "addi %[out], %[out], -16\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[col], 16\n"
                 "ee.vld.128.ip q1, %[col], 16\n"
                 "ee.vld.128.ip q7, %[weight], 16\n"
                 "ee.vunzip.16 q0, q1\n"
                 "ee.vadds.s16 q0, q0, q2\n"
                 "ee.vmul.s16 q1, q0, q3\n"
                 "ee.vmul.s16 q0, q0, q4\n"
                 "ee.vsubs.s16 q1, q5, q1\n"
                 "ee.vadds.s16 q0, q0, q6\n"
                 "ee.vsubs.s16 q0, q0, q1\n"
                 "ee.vmul.s16 q0, q0, q7\n"
                 "addi %[out], %[out], 16\n"
                 "ee.vadds.s16 q0, q0, q1\n"
                 "ee.movi.32.a q0, %[high], 0\n"
                 "sext %[low], %[high], 15\n"
                 "srai %[high], %[high], 20\n"
                 "srai %[low], %[low], 4\n"
                 "addx2 %[high], %[high], %[pal]\n"
                 "addx2 %[low], %[low], %[pal]\n"
                 "l16ui %[high], %[high], 0\n"
                 "l16ui %[low], %[low], 0\n"
                 "slli %[high], %[high], 16\n"
                 "or %[high], %[high], %[low]\n"
                 "s32i %[high], %[out], 0\n"
                 "ee.movi.32.a q0, %[high], 1\n"
                 "sext %[low], %[high], 15\n"
                 "srai %[high], %[high], 20\n"
                 "srai %[low], %[low], 4\n"
                 "addx2 %[high], %[high], %[pal]\n"
                 "addx2 %[low], %[low], %[pal]\n"
                 "l16ui %[high], %[high], 0\n"
                 "l16ui %[low], %[low], 0\n"
                 "slli %[high], %[high], 16\n"
                 "or %[high], %[high], %[low]\n"
                 "s32i %[high], %[out], 4\n"
                 "ee.movi.32.a q0, %[high], 2\n"
                 "sext %[low], %[high], 15\n"
                 "srai %[high], %[high], 20\n"
                 "srai %[low], %[low], 4\n"
                 "addx2 %[high], %[high], %[pal]\n"
                 "addx2 %[low], %[low], %[pal]\n"
                 "l16ui %[high], %[high], 0\n"
                 "l16ui %[low], %[low], 0\n"
                 "slli %[high], %[high], 16\n"
                 "or %[high], %[high], %[low]\n"
                 "s32i %[high], %[out], 8\n"
                 "ee.movi.32.a q0, %[high], 3\n"
                 "sext %[low], %[high], 15\n"
                 "srai %[high], %[high], 20\n"
                 "srai %[low], %[low], 4\n"
                 "addx2 %[high], %[high], %[pal]\n"
                 "addx2 %[low], %[low], %[pal]\n"
                 "l16ui %[high], %[high], 0\n"
                 "l16ui %[low], %[low], 0\n"
                 "slli %[high], %[high], 16\n"
                 "or %[high], %[high], %[low]\n"
                 "s32i %[high], %[out], 12\n"
                 "1:\n"
                 "addi %[out], %[out], 16\n"
                 : [out] "+&r"(out), [col] "+&r"(col), [weight] "+&r"(weight),
                   [coeff] "+&r"(coeff), [low] "=&r"(low), [high] "=&r"(high)
                 : [rr] "r"(rowRad), [pal] "r"(pal), [n] "r"(blocks)
                 : "memory");
    for (int x = 0; x < (n & 7); ++x) {
        const int rad = col[x] + rowRad[x];
        const int bg = bgBase - ((rad * 20) >> 8);
        const int lit = body + ((rad * 78) >> 8);
        out[x] = pal[(bg + (((lit - bg) * weight[x]) >> 8)) >> 4];
    }
}
/* END VERBATIM PRODUCTION KERNELS */
#define CAP 512
static uint16_t got[CAP] __attribute__((aligned(16)));
static uint16_t baseline[CAP] __attribute__((aligned(16)));
static uint16_t want[CAP] __attribute__((aligned(16)));
static uint16_t smStorage[260] __attribute__((aligned(16)));
static uint16_t palStorage[352] __attribute__((aligned(16)));
static int32_t columns[CAP] __attribute__((aligned(16)));
static uint16_t coverage[CAP] __attribute__((aligned(16)));
static int16_t rowRad[8] __attribute__((aligned(16)));
static uint16_t *sm = smStorage + 1; /* only 2-byte aligned: scalar gather */
static uint16_t *pal = palStorage + 32;

static int cubic(int u) {
    if (u < 0) u = 0;
    if (u > 256) u = 256;
    return (u * u * (768 - 2 * u)) >> 16;
}
static void weightCase(int qo, int dq, int qi, int di, int r2, int inv, int n, int offset) {
    ++calls; lanes += n;
    for (int i = 0; i < CAP; ++i) got[i] = baseline[i] = want[i] = 0xa55a;
    for (int x = 0; x < n; ++x) {
        /* Closed forms, not a second copy of the kernel's recurrence. */
        int outer = qo + dq * x - x * (x - 1);
        int cut = qi + di * x + 256 * x * (x - 1);
        int uo = 128 + ((outer * inv) >> 16);
        int uc = 128 + ((((cut >> 8) - r2) * inv) >> 16);
        want[offset + x] = (uint16_t)((cubic(uo) * cubic(uc)) >> 8);
    }
    crescentWeightsAsm(got + offset, qo, dq, qi, di, r2, inv, sm, n);
    crescentWeightsGcc(baseline + offset, qo, dq, qi, di, r2, inv, sm, n);
    for (int i = 0; i < CAP; ++i) {
        check(1, i, got[i], want[i]);
        check(2, i, baseline[i], want[i]);
    }
}
static void paletteFill(uint32_t salt) {
    for (int i = 3; i < 256; ++i) pal[i] = (uint16_t)(i * 257u + salt);
    for (int i = -32; i < 3; ++i) pal[i] = pal[3];
    for (int i = 256; i < 320; ++i) pal[i] = pal[255];
}
static void shadeCase(int n, int offset, int bgBase, int body) {
    ++calls; lanes += n;
    for (int i = 0; i < CAP; ++i) got[i] = want[i] = 0xa55a;
    for (int x = 0; x < n; ++x) {
        int rad = columns[x] + rowRad[x & 7];
        int bg = bgBase - ((rad * 20) >> 8);
        int lit = body + ((rad * 78) >> 8);
        int idx = (bg + (((lit - bg) * coverage[x]) >> 8)) >> 4;
        if (idx < 3) idx = 3;
        if (idx > 255) idx = 255;
        want[offset + x] = pal[idx];
    }
    crescentShadeAsm(got + offset, columns, coverage, rowRad, pal, bgBase, body, n);
    for (int i = 0; i < CAP; ++i) check(3, i, got[i], want[i]);
}
int main(void) {
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\nrsync\n" : : "r"(cp) : "memory");
    check(0, 0, pieProbe(), 0);
    for (int i = 0; i <= 256; ++i) sm[i] = (uint16_t)cubic(i);
    /* Every independent smoothstep index and both clamp boundaries. inv=2^16
     * makes these single-pixel synthetic inputs hit each integer exactly. */
    for (int uo = -129; uo <= 129; ++uo)
        for (int uc = -129; uc <= 129; ++uc)
            weightCase(uo, 0, (uc + 256) * 256, 0, 256, 65536, 1, (uo + 129) & 7);
    /* Every radius possible on 233/240/466/480, including size extremes;
     * full rows through maximal offsets (phase range 100), a centred cut,
     * and fractional Q4 centres, with negative and positive derivatives. */
    for (int r = 75; r <= 232; ++r) {
        int inv = 16777216 / (52 * r);
        for (int variant = 0; variant < 8; ++variant) {
            int dy = variant < 4 ? -240 + variant * 80 : 239 - (variant - 4) * 80;
            int shift = variant % 3 == 0 ? -r * 16 : (variant % 3 == 1 ? r * 16 : 0);
            int dxi = -240 * 16 - shift - variant * 2;
            int dyi = dy * 16 + shift + variant;
            weightCase(r*r - 240*240 - dy*dy, 479, dxi*dxi + dyi*dyi,
                       32*dxi+256, r*r, inv, 480, variant);
        }
    }
    /* Every invK in the full supported range, every length, all tails and
     * source/destination scalar alignments, including the zero-trip case. */
    for (int inv = 1390; inv <= 4301; ++inv)
        weightCase(50000, -301, 17000000, -50000, 32000, inv, 16, inv & 7);
    for (int n = 0; n <= 480; ++n)
        weightCase(-12400, 479, 37000000, -190000, 45200, 1543, n, n & 7);
    /* All rad values and all Q8 coverage values, with every body/background
     * value represented. The eight lane row vector varies to catch phase or
     * narrowing errors, and the column remainder is chosen independently. */
    for (int wgt = 0; wgt <= 256; ++wgt) {
        paletteFill(rand32());
        for (int start = -616; start <= 9216; start += 480) {
            int n = 9217-start < 480 ? 9217-start : 480;
            for (int k = 0; k < 8; ++k) rowRad[k] = (int16_t)(-616+k);
            for (int x = 0; x < n; ++x) {
                columns[x] = start+x-rowRad[x&7];
                coverage[x] = (uint16_t)wgt;
            }
            shadeCase(n, 8+(wgt&3)*2, 416+(wgt%145), 1034+((start+616+wgt)%1037));
        }
    }
    /* All legal 4-byte output alignments for every length, guard words on
     * both sides, and parameter corners including rad sign/maximum. */
    for (int offset = 8; offset < 16; offset += 2)
        for (int n = 0; n <= 480; ++n) {
            paletteFill(rand32());
            for (int k = 0; k < 8; ++k) rowRad[k] = -616;
            for (int x = 0; x < n; ++x) {
                columns[x] = (x & 1) ? 9832 : 0;
                coverage[x] = (uint16_t)(rand32()%257);
            }
            shadeCase(n, offset, (n&1) ? 416 : 560, (n&2) ? 1034 : 2070);
        }
    /* Exercise every RGB565 word independently of the field arithmetic. */
    for (uint32_t value = 0; value < 65536; ++value) {
        for (int i = 0; i < 352; ++i) palStorage[i] = (uint16_t)value;
        shadeCase(8, 8, 416, 1034);
    }
    if (!mismatches) {
        puts0("GM_QEMUBENCH_PIE: PASS crescent calls="); dec(calls);
        puts0(" lanes="); dec(lanes);
        puts0(" mismatches=0 (PIE probes, GCC baseline, masks, radii, reciprocals, Q4 centres, all weights/radii, tails, alignment, guards, RGB565)\n");
    }
    puts0("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
