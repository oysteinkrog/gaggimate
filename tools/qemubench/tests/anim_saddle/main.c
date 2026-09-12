/* Freestanding, harness-mode parity test for AnimSaddle.cpp. Both production
 * kernel functions below are verbatim copies, including qualifiers and asm
 * constraints. The C reference independently implements the page's positive
 * bias, Q10 product, contour phase and 256-entry palette wrap.
 *
 * Coverage: every column value -1016..1016 against every row value -127..127
 * at eight phase boundaries; all 256 phases at seven row boundaries; zero
 * and short lengths, 233/240/466/480 widths, all valid output alignments,
 * both kernels, scalar tails and guard words. The row broadcast primitive
 * is also checked over all 65536 halfwords and all eight natural offsets.
 * No libc, heap, FPU or constructors. Only main enables CP3 in bare metal.
 */
#include <stdint.h>
#define GM_ANIM_IRAM
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void uart_putc(char c) { UART0_FIFO = (uint8_t)c; }
static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}
static void uart_put_dec(uint32_t n) {
    char b[12];
    int k = 0;
    do { b[k++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (k) uart_putc(b[--k]);
}

GM_ANIM_IRAM __attribute__((noinline)) void saddlePairsAsm(uint16_t *out, const int16_t *col,
                                                        const uint16_t *pal, int rt, int phase, int pairs) {
    int t0, t1;
    const int bias = 128 << 10;
    asm volatile("loopnez %[n], 1f\n"
                 "l16ui %[t1], %[col], 2\n"
                 "l16ui %[t0], %[col], 0\n"
                 "mul16s %[t1], %[t1], %[rt]\n"
                 "mul16s %[t0], %[t0], %[rt]\n"
                 "add %[t1], %[t1], %[bias]\n"
                 "srai %[t1], %[t1], 10\n"
                 "add %[t0], %[t0], %[bias]\n"
                 "add %[t1], %[t1], %[phase]\n"
                 "srai %[t0], %[t0], 10\n"
                 "extui %[t1], %[t1], 0, 8\n"
                 "add %[t0], %[t0], %[phase]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "extui %[t0], %[t0], 0, 8\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 0\n"
                 "addi %[col], %[col], 4\n"
                 "addi %[out], %[out], 4\n"
                 "1:\n"
                 : [out] "+&r"(out), [col] "+&r"(col), [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [pal] "r"(pal), [rt] "r"(rt), [bias] "r"(bias), [phase] "r"(phase), [n] "r"(pairs)
                 : "memory");
}

GM_ANIM_IRAM __attribute__((noinline)) void saddleBlocksAsm(uint16_t *out, const int16_t *col,
                                                         const int16_t *rowFactor, const uint16_t *pal,
                                                         int16_t *work, int blocks) {
    const int16_t *constants = work + 16;
    int t0, t1;
    asm volatile("ssai 10\n"
                 "ee.vldbc.16 q1, %[rt]\n"
                 "ee.vld.128.ip q2, %[constants], 16\n"
                 "ee.vld.128.ip q3, %[constants], 16\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[col], 16\n"
                 "ee.vld.128.ip q4, %[col], 16\n"
                 "ee.vmul.s16 q0, q0, q1\n"
                 "ee.vmul.s16 q4, q4, q1\n"
                 "ee.vadds.s16 q0, q0, q2\n"
                 "ee.vadds.s16 q4, q4, q2\n"
                 "ee.andq q0, q0, q3\n"
                 "ee.andq q4, q4, q3\n"
                 "ee.vst.128.ip q0, %[work], 16\n"
                 "ee.vst.128.ip q4, %[work], -16\n"
                 "l16ui %[t0], %[work], 0\n"
                 "l16ui %[t1], %[work], 2\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 0\n"
                 "l16ui %[t0], %[work], 4\n"
                 "l16ui %[t1], %[work], 6\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 4\n"
                 "l16ui %[t0], %[work], 8\n"
                 "l16ui %[t1], %[work], 10\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 8\n"
                 "l16ui %[t0], %[work], 12\n"
                 "l16ui %[t1], %[work], 14\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 12\n"
                 "l16ui %[t0], %[work], 16\n"
                 "l16ui %[t1], %[work], 18\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 16\n"
                 "l16ui %[t0], %[work], 20\n"
                 "l16ui %[t1], %[work], 22\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 20\n"
                 "l16ui %[t0], %[work], 24\n"
                 "l16ui %[t1], %[work], 26\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 24\n"
                 "l16ui %[t0], %[work], 28\n"
                 "l16ui %[t1], %[work], 30\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 28\n"
                 "addi %[out], %[out], 32\n"
                 "1:\n"
                 : [out] "+&r"(out), [col] "+&r"(col), [work] "+&r"(work),
                   [constants] "+&r"(constants), [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [rt] "r"(rowFactor), [pal] "r"(pal), [n] "r"(blocks)
                 : "memory");
}

#define CAP 2048
#define GUARD 0x5AA5u
static int16_t columns[CAP] __attribute__((aligned(16)));
static int16_t rows[8] __attribute__((aligned(16)));
static uint16_t palette[256] __attribute__((aligned(16)));
static int16_t workStorage[48] __attribute__((aligned(16)));
static uint16_t output[CAP + 32] __attribute__((aligned(16)));
static uint16_t reference[CAP] __attribute__((aligned(16)));
static int16_t broadcast[8] __attribute__((aligned(16)));
static uint32_t cases, pixels;

static void fail(const char *what, uint32_t lane, uint32_t got, uint32_t want) {
    uart_puts("GM_QEMUBENCH_PIE: FAIL saddle "); uart_puts(what);
    uart_puts(" case="); uart_put_dec(cases);
    uart_puts(" lane="); uart_put_dec(lane);
    uart_puts(" got="); uart_put_dec(got);
    uart_puts(" want="); uart_put_dec(want);
    uart_puts("\nGM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}

static void probeBroadcast(void) {
    for (int v = -32768; v <= 32767; v++) {
        const int lane = (unsigned)v & 7;
        rows[lane] = (int16_t)v;
        const int16_t *p = rows + lane;
        int16_t *q = broadcast;
        asm volatile("ee.vldbc.16 q0, %[src]\n"
                     "ee.vst.128.ip q0, %[dst], 0\n"
                     : [dst] "+r"(q) : [src] "r"(p) : "memory");
        for (int k = 0; k < 8; k++) {
            if (broadcast[k] != v) fail("broadcast", k, (uint16_t)broadcast[k], (uint16_t)v);
        }
    }
}

static void rowRef(uint16_t *dst, const int16_t *col, int row, int phase, int n) {
    for (int x = 0; x < n; x++) {
        /* The product plus bias is always positive over the whole operand
         * domain. No implementation-defined signed shift in this oracle. */
        const unsigned q = (unsigned)(col[x] * row + 131072) >> 10;
        dst[x] = palette[(q + (unsigned)phase) & 255];
    }
}

static void runCase(int n, int rt, int phase, int offset, int scalarOnly) {
    int16_t *work = workStorage + 8; /* 16-byte alignment plus a front guard */
    uint16_t *out = output + 8 + offset; /* offset=0/2/4/6, all 4-byte aligned */
    const int rslot = cases & 7;
    rows[rslot] = (int16_t)rt;
    for (int i = 0; i < 48; i++) workStorage[i] = (int16_t)GUARD;
    for (int i = 0; i < 8; i++) {
        work[16 + i] = (int16_t)(128 + phase);
        work[24 + i] = 255;
    }
    for (int i = 0; i < n + offset + 24; i++) output[i] = GUARD;
    rowRef(reference, columns, rt, phase, n);
    const int blocks = scalarOnly ? 0 : n >> 4;
    /* Always exercise the zero-trip kernel too. Its tables remain valid. */
    saddleBlocksAsm(out, columns, rows + rslot, palette, work, blocks);
    int x = blocks << 4;
    const int pairs = (n - x) >> 1;
    saddlePairsAsm(out + x, columns + x, palette, rt, phase, pairs);
    x += pairs * 2;
    if (x < n) {
        out[x] = palette[(((columns[x] * rt + (128 << 10)) >> 10) + phase) & 255];
    }
    for (int i = 0; i < n; i++) {
        if (out[i] != reference[i]) fail(scalarOnly ? "pairs" : "blocks", i, out[i], reference[i]);
    }
    for (int i = 0; i < offset + 8; i++) {
        if (output[i] != GUARD) fail("output-prefix", i, output[i], GUARD);
    }
    for (int i = 0; i < 8; i++) {
        if (out[n + i] != GUARD) fail("output-tail", i, out[n + i], GUARD);
        if ((uint16_t)workStorage[i] != GUARD) fail("scratch-prefix", i, (uint16_t)workStorage[i], GUARD);
        if ((uint16_t)work[32 + i] != GUARD) fail("scratch-tail", i, (uint16_t)work[32 + i], GUARD);
        if (work[16 + i] != 128 + phase) fail("bias", i, work[16 + i], 128 + phase);
        if (work[24 + i] != 255) fail("mask", i, work[24 + i], 255);
    }
    cases++;
    pixels += n;
}

int main(void) {
    /* Bare-metal harness only. Production never writes CPENABLE. */
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\n isync\n" : : "r"(cp) : "memory");
    probeBroadcast();
    for (int i = 0; i < 256; i++) palette[i] = (uint16_t)(i * 257u);
    for (int i = 0; i < CAP; i++) columns[i] = (int16_t)((i % 2033) - 1016);

    static const int phases[] = {0, 1, 63, 127, 128, 129, 254, 255};
    static const int rlimits[] = {-127, -126, -1, 0, 1, 126, 127};
    /* All 2033 column values times all 255 possible row factors. Speed
     * extremes and uint32 clock wraps are represented by phase boundaries;
     * Curvature, Drift and Contrast cannot widen these operand limits. */
    for (int rt = -127; rt <= 127; rt++) {
        for (int ph = 0; ph < 8; ph++) {
            runCase(CAP, rt, phases[ph], 2 * (ph & 3), 0);
        }
    }
    for (int ph = 0; ph < 256; ph++) {
        for (int r = 0; r < 7; r++) runCase(CAP, rlimits[r], ph, 2 * (r & 3), 0);
    }
    /* Scalar GCC transcription gets the same complete product domain. */
    for (int rt = -127; rt <= 127; rt++) runCase(CAP, rt, rt & 255, 0, 1);

    /* Short rows alternate positive/negative extrema, floor boundaries and
     * zero. All remainders modulo 16 and every legal output alignment occur. */
    static const int cedges[] = {-1016, 1016, -1015, 1015, -513, -512, -511, -1,
                          0, 1, 511, 512, 513, 1008, -1008, 127};
    for (int i = 0; i < CAP; i++) columns[i] = (int16_t)cedges[i & 15];
    for (int n = 0; n <= 33; n++) {
        for (int off = 0; off < 8; off += 2) {
            for (int r = 0; r < 7; r++) {
                for (int ph = 0; ph < 8; ph++) runCase(n, rlimits[r], phases[ph], off, 0);
            }
        }
    }
    static const int widths[] = {233, 240, 466, 480};
    for (int k = 0; k < 4; k++) {
        for (int off = 0; off < 8; off += 2) {
            for (int r = 0; r < 7; r++) {
                for (int ph = 0; ph < 8; ph++) runCase(widths[k], rlimits[r], phases[ph], off, 0);
            }
        }
    }
    uart_puts("GM_QEMUBENCH_PIE: PASS saddle cases="); uart_put_dec(cases);
    uart_puts(" pixels="); uart_put_dec(pixels);
    uart_puts(" mismatches=0 broadcast_values=65536 guards=OK\nGM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
