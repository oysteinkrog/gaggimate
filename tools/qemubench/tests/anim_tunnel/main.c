/* Harness-mode equivalence test for AnimTunnel.cpp. No libc, allocation or
 * constructors. Both production kernel definitions below are verbatim copies,
 * including constraints, noinline attributes and the gather's scalar tail.
 *
 * Gather coverage: every 0..4095 cell index and every uint16 RGB565 value;
 * all eight halfword source alignments and all four word output alignments
 * within a 16-byte block; zero trips, all tails, 233/240/466/480/960 widths.
 * PIE coverage: every sum 0..2048 crossed with every Q8 gain 0..256 (a superset
 * of production's 26..218); every aTerm 512..1536 and every wave -512..512;
 * zero through four vector groups; min/default/max pitch and brightness,
 * with phases on both sides of quadrant boundaries and the 1023-to-0 wrap.
 * Prefix/suffix canaries are compared along with every result.
 *
 * QEMU proves instruction semantics and equality, never device performance.
 * probe_movi/main.c separately checks EE.MOVI.32.Q's four word selectors.
 */
#include <stdint.h>

#define GM_ANIM_IRAM
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void uart_puts(const char *p) {
    while (*p) {
        if (*p == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*p++;
    }
}

static void uart_uint(uint32_t v) {
    char digits[10];
    int n = 0;
    do {
        digits[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n) UART0_FIFO = (uint8_t)digits[--n];
}

/* KERNELS_BEGIN */
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
/* KERNELS_END */

/* This is bandRef's inner loop, independently expressed in C. */
static void gatherRef(uint16_t *out, const uint16_t *map, const uint16_t *tex, int n) {
    for (int i = 0; i < n; i++) out[i] = tex[map[i]];
}

/* This is the page's cell expression and frame()'s portable branch, with both
 * integer truncations intact. It deliberately does not imitate the PIE ops. */
static void levelsRef(uint16_t *out, const int16_t *wave, const uint16_t *dim, int aTerm, int n) {
    for (int i = 0; i < n; i++) {
        int s = wave[i] + aTerm;
        out[i] = (uint16_t)(((40 + ((s * 180) >> 11)) * dim[i]) >> 8);
    }
}

#define N 4096
#define GUARD 0xa55au
static uint16_t cells[N] __attribute__((aligned(16)));
static uint16_t map[N + 8] __attribute__((aligned(16)));
static uint16_t got[N + 16] __attribute__((aligned(16)));
static uint16_t want[N + 16] __attribute__((aligned(16)));
static int16_t wave[32] __attribute__((aligned(16)));
static uint16_t dim[32] __attribute__((aligned(16)));
static uint16_t factors[16] __attribute__((aligned(16)));
static uint16_t levelGot[48] __attribute__((aligned(16)));
static uint16_t levelWant[48] __attribute__((aligned(16)));
static uint16_t palette[256] __attribute__((aligned(16)));
static uint16_t cellGot[N] __attribute__((aligned(16)));
static uint16_t cellWant[N] __attribute__((aligned(16)));
static uint32_t gatherCalls, gatherPixels, levelCalls, levelCells, composedFrames;

static int compare(const char *kernel, uint32_t call, const uint16_t *a, const uint16_t *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            uart_puts("GM_QEMUBENCH_PIE: FAIL kernel="); uart_puts(kernel);
            uart_puts(" call="); uart_uint(call);
            uart_puts(" lane="); uart_uint((uint32_t)i);
            uart_puts(" got="); uart_uint(a[i]);
            uart_puts(" want="); uart_uint(b[i]);
            uart_puts("\n");
            return 0;
        }
    }
    return 1;
}

static int checkGather(int n, int sourceOffset, int outputOffset) {
    for (int i = 0; i < N + 16; i++) got[i] = want[i] = GUARD;
    tunnelGatherAsm(got + outputOffset, map + sourceOffset, cells, n);
    gatherRef(want + outputOffset, map + sourceOffset, cells, n);
    gatherCalls++;
    gatherPixels += (uint32_t)n;
    return compare("gather", gatherCalls, got, want, N + 16);
}

static int checkLevels(int aTerm, int groups) {
    for (int i = 0; i < 48; i++) levelGot[i] = levelWant[i] = GUARD;
    /* +8 uint16 gives an aligned 16-byte prefix. Four groups fill exactly
     * 32 lanes, leaving another aligned block of untouched canaries. */
    tunnelLevelsAsm(levelGot + 8, wave, dim, factors, aTerm, groups);
    levelsRef(levelWant + 8, wave, dim, aTerm, groups * 8);
    levelCalls++;
    levelCells += (uint32_t)(groups * 8);
    return compare("levels", levelCalls, levelGot, levelWant, 48);
}

static int testGather(void) {
    /* An odd stride permutes all 4096 indices. Sixteen palette blocks cover
     * all 65536 possible output words, without a palette collision hiding a
     * bad index. Every alignment sees every index and every output value. */
    for (int block = 0; block < 16; block++) {
        for (int i = 0; i < N; i++) cells[i] = (uint16_t)(block * N + i);
        for (int i = 0; i < N + 8; i++) map[i] = (uint16_t)((i * 257 + block * 193) & (N - 1));
        for (int src = 0; src < 8; src++) {
            for (int dst = 0; dst < 8; dst += 2) {
                if (!checkGather(N, src, dst)) return 0;
            }
        }
    }
    static const int lengths[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17,
                                 31, 32, 33, 233, 240, 466, 480, 960, 4095, 4096};
    uint32_t rng = 0x31415926u;
    for (int i = 0; i < N; i++) cells[i] = (uint16_t)(i * 40503u + 101u);
    for (int i = 0; i < N + 8; i++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        map[i] = (uint16_t)(rng & (N - 1));
    }
    map[0] = 0; map[1] = N - 1; map[2] = 1; map[3] = N - 2;
    for (unsigned k = 0; k < sizeof(lengths) / sizeof(lengths[0]); k++) {
        for (int src = 0; src < 8; src++) {
            for (int dst = 0; dst < 8; dst += 2) {
                if (!checkGather(lengths[k], src, dst)) return 0;
            }
        }
    }
    return 1;
}

static int testLevels(void) {
    for (int i = 0; i < 8; i++) { factors[i] = 180; factors[i + 8] = 40; }
    /* Cross every possible sum with every gain, also testing beyond the
     * production gain endpoints. No saturating add should ever saturate. */
    for (int gain = 0; gain <= 256; gain++) {
        for (int half = 0; half < 2; half++) {
            for (int base = 0; base <= 1024; base += 32) {
                for (int i = 0; i < 32; i++) {
                    int v = base + i;
                    wave[i] = (int16_t)((v > 1024 ? 1024 : v) - 512);
                    dim[i] = (uint16_t)gain;
                }
                if (!checkLevels(512 + half * 1024, 4)) return 0;
            }
        }
    }
    /* Every legal broadcast value, varied lane inputs and all group counts.
     * These values also make the zero-trip case observable through canaries. */
    for (int aTerm = 512; aTerm <= 1536; aTerm++) {
        for (int i = 0; i < 32; i++) {
            wave[i] = (int16_t)(((aTerm + i * 37) % 1025) - 512);
            dim[i] = (uint16_t)(26 + ((aTerm + i * 13) % 193));
        }
        for (int groups = 0; groups <= 4; groups++) {
            if (!checkLevels(aTerm, groups)) return 0;
        }
    }
    return 1;
}

/* Synthetic periodic wave spans exactly the shared sine's operand range.
 * The kernels consume samples, not angles or floats, so libm is unnecessary. */
static int sample(int phase) {
    phase &= 1023;
    if (phase < 256) return phase * 2;
    if (phase < 768) return 1024 - phase * 2;
    return phase * 2 - 2048;
}

static int testParameters(void) {
    static const int pitch[] = {0, 50, 100};
    static const int brightness[] = {0, 50, 74, 100};
    static const int phases[] = {0, 1, 255, 256, 511, 512, 767, 768, 1023};
    /* Speed reaches these kernels only through wrapped sine phases. These
     * quadrant edges and wrap cover phases reachable at either speed extreme.
     * Compose both kernels into a complete 128x32 cell table for every case. */
    for (int b = 0; b < 4; b++) {
        int bright = 80 + (brightness[b] * 176 + 50) / 100;
        for (int i = 0; i < 256; i++) {
            int r = (i * bright) >> 8;
            int g = ((255 - i) * bright) >> 8;
            int blue = (((i * 73) & 255) * bright) >> 8;
            palette[i] = (uint16_t)(((r & 248) << 8) | ((g & 252) << 3) | (blue >> 3));
        }
        for (int p = 0; p < 3; p++) {
            int bandK = 14 + (pitch[p] * 20 + 50) / 100;
            for (int ph = 0; ph < 9; ph++) {
                for (int d = 0; d < 32; d++) {
                    wave[d] = (int16_t)sample(d * bandK - phases[ph]);
                    dim[d] = (uint16_t)(26 + d * 192 / 31);
                }
                for (int a = 0; a < 128; a++) {
                    int aTerm = sample(a * 16 + phases[ph]) + 1024;
                    if (!checkLevels(aTerm, 4)) return 0;
                    tunnelGatherAsm(cellGot + a * 32, levelGot + 8, palette, 32);
                    gatherRef(cellWant + a * 32, levelWant + 8, palette, 32);
                    gatherCalls++;
                    gatherPixels += 32;
                }
                composedFrames++;
                if (!compare("composed", composedFrames, cellGot, cellWant, N)) return 0;
                for (int i = 0; i < N; i++) cells[i] = cellGot[i];
                if (!checkGather(480, 0, 0) || !checkGather(240, 1, 2) || !checkGather(233, 7, 6)) return 0;
            }
        }
    }
    return 1;
}

int main(void) {
    /* Only the bare-metal test writes CPENABLE. The production kernel must
     * trigger FreeRTOS's coprocessor-disabled handler instead. */
    uint32_t enable = 8;
    asm volatile("wsr.cpenable %0\nrsync\n" : : "r"(enable) : "memory");
    if (testGather() && testLevels() && testParameters()) {
        uart_puts("GM_QEMUBENCH_PIE: PASS tunnel gather_calls="); uart_uint(gatherCalls);
        uart_puts(" pixels="); uart_uint(gatherPixels);
        uart_puts(" levels_calls="); uart_uint(levelCalls);
        uart_puts(" cells="); uart_uint(levelCells);
        uart_puts(" composed_frames="); uart_uint(composedFrames);
        uart_puts(" mismatches=0\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
