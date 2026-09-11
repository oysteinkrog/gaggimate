/* Freestanding execution of the verbatim production chevronsRowAsm.
 * Full 4096 x 4096 column/row operand matrix covers every phase reachable
 * at any time and any slider value, including 0 and 100. Also exercise
 * all halfword source alignments, every legal word destination alignment,
 * short lengths, vector tails, and 480/240/466/233 display widths.
 * A separate suite passes every RGB565 value through a permuted palette.
 * EE.MOVI.32.A was first checked in probe_movi under this directory.
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
static void put_u32(uint32_t v) {
    char buf[10];
    int n = 0;
    do { buf[n++] = (char)('0' + v % 10u); v /= 10u; } while (v != 0);
    while (n != 0) UART0_FIFO = (uint8_t)buf[--n];
}

GM_ANIM_IRAM __attribute__((noinline)) void chevronsRowAsm(uint16_t *out, const int16_t *ct,
                                                        const uint16_t *pal, int rt, int n) {
    if (n <= 0) {
        return;
    }
    if (((uintptr_t)ct & 3u) == 0) {
        for (; n >= 2 && ((uintptr_t)ct & 15u) != 0; n -= 2) {
            const uint32_t c0 = pal[((ct[0] + rt) >> 4) & 255];
            const uint32_t c1 = pal[((ct[1] + rt) >> 4) & 255];
            *(uint32_t *)out = c0 | (c1 << 16);
            ct += 2;
            out += 2;
        }
        const int blocks = n >> 3;
        if (blocks != 0) {
            const uint32_t packedRow = (uint32_t)rt | ((uint32_t)rt << 16);
            uint32_t sum, c0, c1;
            asm volatile("ee.movi.32.q q1, %[row], 0\n"
                         "ee.movi.32.q q1, %[row], 1\n"
                         "ee.movi.32.q q1, %[row], 2\n"
                         "ee.movi.32.q q1, %[row], 3\n"
                         "loopnez %[blocks], 1f\n"
                         "ee.vld.128.ip q0, %[ct], 16\n"
                         "addi %[n], %[n], -8\n"
                         "ee.vadds.s16 q0, q0, q1\n"
                         "ee.movi.32.a q0, %[sum], 0\n"
                         "extui %[c0], %[sum], 4, 8\n"
                         "extui %[c1], %[sum], 20, 8\n"
                         "addx2 %[c0], %[c0], %[pal]\n"
                         "addx2 %[c1], %[c1], %[pal]\n"
                         "l16ui %[c1], %[c1], 0\n"
                         "l16ui %[c0], %[c0], 0\n"
                         "slli %[c1], %[c1], 16\n"
                         "or %[c0], %[c0], %[c1]\n"
                         "s32i %[c0], %[out], 0\n"
                         "ee.movi.32.a q0, %[sum], 1\n"
                         "extui %[c0], %[sum], 4, 8\n"
                         "extui %[c1], %[sum], 20, 8\n"
                         "addx2 %[c0], %[c0], %[pal]\n"
                         "addx2 %[c1], %[c1], %[pal]\n"
                         "l16ui %[c1], %[c1], 0\n"
                         "l16ui %[c0], %[c0], 0\n"
                         "slli %[c1], %[c1], 16\n"
                         "or %[c0], %[c0], %[c1]\n"
                         "s32i %[c0], %[out], 4\n"
                         "ee.movi.32.a q0, %[sum], 2\n"
                         "extui %[c0], %[sum], 4, 8\n"
                         "extui %[c1], %[sum], 20, 8\n"
                         "addx2 %[c0], %[c0], %[pal]\n"
                         "addx2 %[c1], %[c1], %[pal]\n"
                         "l16ui %[c1], %[c1], 0\n"
                         "l16ui %[c0], %[c0], 0\n"
                         "slli %[c1], %[c1], 16\n"
                         "or %[c0], %[c0], %[c1]\n"
                         "s32i %[c0], %[out], 8\n"
                         "ee.movi.32.a q0, %[sum], 3\n"
                         "extui %[c0], %[sum], 4, 8\n"
                         "extui %[c1], %[sum], 20, 8\n"
                         "addx2 %[c0], %[c0], %[pal]\n"
                         "addx2 %[c1], %[c1], %[pal]\n"
                         "l16ui %[c1], %[c1], 0\n"
                         "l16ui %[c0], %[c0], 0\n"
                         "slli %[c1], %[c1], 16\n"
                         "or %[c0], %[c0], %[c1]\n"
                         "s32i %[c0], %[out], 12\n"
                         "addi %[out], %[out], 16\n"
                         "1:\n"
                         : [ct] "+&r"(ct), [out] "+&r"(out), [n] "+&r"(n),
                           [sum] "=&r"(sum), [c0] "=&r"(c0), [c1] "=&r"(c1)
                         : [row] "r"(packedRow), [pal] "r"(pal), [blocks] "r"(blocks)
                         : "memory");
        }
    }
    // GCC's original schedule for the 0..3 tail pairs, or the whole row
    // when ct is at 2 mod 4. Both pointers walk, and LOOPNEZ covers zero.
    const int pairs = n >> 1;
    uint32_t c0, c1;
    asm volatile("loopnez %[pairs], 2f\n"
                 "l16si %[c1], %[ct], 2\n"
                 "l16si %[c0], %[ct], 0\n"
                 "add %[c1], %[c1], %[rt]\n"
                 "extui %[c1], %[c1], 4, 8\n"
                 "add %[c0], %[c0], %[rt]\n"
                 "addx2 %[c1], %[c1], %[pal]\n"
                 "extui %[c0], %[c0], 4, 8\n"
                 "l16ui %[c1], %[c1], 0\n"
                 "addx2 %[c0], %[c0], %[pal]\n"
                 "l16ui %[c0], %[c0], 0\n"
                 "slli %[c1], %[c1], 16\n"
                 "or %[c1], %[c1], %[c0]\n"
                 "s32i %[c1], %[out], 0\n"
                 "addi %[ct], %[ct], 4\n"
                 "addi %[out], %[out], 4\n"
                 "2:\n"
                 : [ct] "+&r"(ct), [out] "+&r"(out), [c0] "=&r"(c0), [c1] "=&r"(c1)
                 : [rt] "r"(rt), [pal] "r"(pal), [pairs] "r"(pairs)
                 : "memory");
    if ((n & 1) != 0) {
        *out = pal[((ct[0] + rt) >> 4) & 255];
    }
}

/* Independent scalar reimplementation of bandRef's pixel expression. */
static void rowRef(uint16_t *out, const int16_t *ct, const uint16_t *pal, int rt, int n) {
    for (int x = 0; x < n; x++)
        out[x] = pal[((ct[x] + rt) >> 4) & 255];
}

#define CAP 4096
#define GUARD 8
static int16_t columns[CAP + 16] __attribute__((aligned(16)));
static uint16_t actual[CAP + 32] __attribute__((aligned(16)));
static uint16_t expected[CAP + 32] __attribute__((aligned(16)));
static uint16_t palette[256] __attribute__((aligned(16)));
static uint32_t calls, pixels, mismatches;
static uint32_t firstCall, firstLane, firstGot, firstWant;

static void record(uint32_t lane, uint32_t got, uint32_t want) {
    if (got == want) return;
    if (mismatches == 0) {
        firstCall = calls; firstLane = lane; firstGot = got; firstWant = want;
    }
    mismatches++;
}
static void runCase(int n, int srcOffset, int dstOffset, int rt) {
    uint16_t *out = actual + GUARD + dstOffset;
    uint16_t *ref = expected + GUARD + dstOffset;
    const int16_t *ct = columns + srcOffset;
    calls++;
    for (int i = -GUARD; i < n + GUARD; i++) out[i] = 0xdead;
    chevronsRowAsm(out, ct, palette, rt, n);
    rowRef(ref, ct, palette, rt, n);
    for (int x = 0; x < n; x++) record((uint32_t)x, out[x], ref[x]);
    for (int x = 1; x <= GUARD; x++) record(0x80000000u + x, out[-x], 0xdead);
    for (int x = 0; x < GUARD; x++) record((uint32_t)(n + x), out[n + x], 0xdead);
    pixels += (uint32_t)n;
}

static uint32_t randomState = 0x17c0ffeeu;
static uint32_t nextRandom(void) {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
}
static void fillPalette(int salt) {
    /* A permutation of the high byte makes all 256 entries distinct.
     * salt 0..255 covers all 65,536 possible RGB565 output words. */
    for (int i = 0; i < 256; i++) palette[i] = (uint16_t)((((i * 73) & 255) << 8) | salt);
}
static void alignmentCases(void) {
    static const int sizes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 16, 17,
                         31, 32, 33, 233, 240, 466, 480};
    static const int rowValues[] = {0, 1, 15, 16, 17, 2047, 2048, 4080, 4094, 4095};
    for (int i = 0; i < CAP + 16; i++) columns[i] = (int16_t)(nextRandom() & 4095u);
    fillPalette(0xa5);
    for (int r = 0; r < (int)(sizeof(rowValues) / sizeof(rowValues[0])); r++)
        for (int s = 0; s < 8; s++)
            for (int d = 0; d < 8; d += 2)
                for (int z = 0; z < (int)(sizeof(sizes) / sizeof(sizes[0])); z++)
                    runCase(sizes[z], s, d, rowValues[r]);
}
static void parameterCases(void) {
    /* Transform all 16 endpoint combinations through the page's frame
     * arithmetic. The synthetic sine ramps across the full -512..512
     * range, so these cases exercise the bounds without needing libm. */
    static const uint32_t times[] = {0u, 1u, 1990u, 4960u, 7930u, 0x15555556u,
                              0x7fffffffu, 0xfffffff0u, 0xffffffffu};
    static const int sizes[] = {480, 240, 466, 233};
    for (int p = 0; p < 16; p++) {
        const uint32_t sp = 4u + ((p & 1) ? 44u : 0u);
        const int folds = 2 + ((p & 2) ? 3 : 0);
        const int angle = 110 + ((p & 4) ? 180 : 0);
        fillPalette((p & 8) ? 255 : 0);
        for (int z = 0; z < 4; z++) {
            const int w = sizes[z], rowStep = folds * 4096 / w;
            const int round = w * 22 / 100;
            for (int t = 0; t < (int)(sizeof(times) / sizeof(times[0])); t++) {
                const uint32_t base = times[t] * sp;
                const uint32_t phase = (base >> 4) & 4095u;
                const uint32_t phW = base >> 10, phB = (base * 3u) >> 11;
                const int wob = 256 + ((((int)(phW % 1025u) - 512) * 30) >> 9);
                const int colStep = (((rowStep * angle) >> 8) * wob) >> 8;
                for (int ph = 0; ph < 8; ph++) {
                    /* Offset mirrors actual ctPh's y-phase start modulo 16. */
                    const int off = (ph * w) & 7;
                    for (int x = 0; x < w; x++) {
                        int adx = x - w / 2;
                        if (adx < 0) adx = -adx;
                        adx = adx >= round ? adx - (round >> 1) : adx * adx / (2 * round);
                        const uint32_t si = ((uint32_t)(x * 23) >> 4) - phB;
                        const int swell = (((int)(si % 1025u) - 512) * 760) >> 9;
                        /* Both dither endpoints, including values that wrap. */
                        const int dither = ((x + ph) & 1) ? 192 : -192;
                        columns[off + x] = (int16_t)((uint32_t)(adx * colStep + swell + dither) & 4095u);
                    }
                    const int rt = (int)(((uint32_t)(w - 8 + ph) * rowStep + phase) & 4095u);
                    runCase(w, off, 0, rt);
                }
            }
        }
    }
}
int main(void) {
    /* Bare-metal harness only: production leaves CPENABLE to FreeRTOS. */
    uint32_t enable = 8;
    asm volatile("wsr %0, cpenable\nisync\n" : : "r"(enable));
    alignmentCases();
    parameterCases();
    fillPalette(0x5a);
    for (int x = 0; x < CAP + 16; x++) columns[x] = (int16_t)(x & 4095);
    for (int rt = 0; rt < 4096; rt++) runCase(CAP, 0, 0, rt);
    for (int x = 0; x < 256; x++) columns[x] = (int16_t)(x * 16);
    for (int salt = 0; salt < 256; salt++) {
        fillPalette(salt);
        runCase(256, 0, 0, 0);
    }
    if (mismatches == 0) {
        puts_uart("GM_QEMUBENCH_PIE: PASS chevrons calls="); put_u32(calls);
        puts_uart(" pixels="); put_u32(pixels);
        puts_uart(" mismatches=0 (4096x4096 operands, 16 parameter extremes, all alignments, RGB565 sweep)\n");
    } else {
        puts_uart("GM_QEMUBENCH_PIE: FAIL chevrons call="); put_u32(firstCall);
        puts_uart(" lane="); put_u32(firstLane);
        puts_uart(" got="); put_u32(firstGot);
        puts_uart(" want="); put_u32(firstWant);
        puts_uart(" mismatches="); put_u32(mismatches); puts_uart("\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
