/* Oculus execution test, harness mode, no libc or OS. The row kernel is
 * copied verbatim from AnimOculus.cpp; the C reference independently states
 * bandRef's per-pixel equation. Every ct=0..2191 is crossed with every
 * rt=0..1904, the full conservative operand domain for all 0/100 parameter
 * combinations, every time phase and every theme/dither amplitude.
 * Additional cases exhaust all RGB565 palette words, all short tails, the
 * 233/240/466/480 widths, all eight padded column phases, and all four
 * contract-legal output alignments modulo 16. Guards catch stray writes.
 * Only this freestanding main enables CP3, once. Production never does.
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
static void put_dec(uint32_t v) {
    char b[10];
    int n = 0;
    do { b[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) UART0_FIFO = (uint8_t)b[--n];
}

// BEGIN VERBATIM PRODUCTION KERNEL
GM_ANIM_IRAM __attribute__((noinline)) void oculusRowAsm(uint16_t *out, const int16_t *ct,
                                                       const uint16_t *pal, int rt, int n) {
    if (n <= 0) return;
    const int blocks = n >> 3;
    if (blocks != 0) {
        const uint32_t packedRow = (uint32_t)rt * 0x00010001u;
        uint32_t lo, hi;
        asm volatile("ee.movi.32.q q1, %[rt], 0\n"
                     "ee.movi.32.q q1, %[rt], 1\n"
                     "ee.movi.32.q q1, %[rt], 2\n"
                     "ee.movi.32.q q1, %[rt], 3\n"
                     "loopnez %[n], 1f\n"
                     "ee.vld.128.ip q0, %[ct], 0\n"
                     "addi    %[ct], %[ct], 16\n"
                     "ee.vadds.s16 q0, q0, q1\n"
                     "ee.movi.32.a q0, %[hi], 0\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 0\n"
                     "ee.movi.32.a q0, %[hi], 1\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 4\n"
                     "ee.movi.32.a q0, %[hi], 2\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 8\n"
                     "ee.movi.32.a q0, %[hi], 3\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 12\n"
                     "addi    %[out], %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out), [ct] "+&r"(ct), [lo] "=&r"(lo), [hi] "=&r"(hi)
                     : [n] "r"(blocks), [rt] "r"(packedRow), [pal] "r"(pal)
                     : "memory");
    }
    const int pairs = (n & 7) >> 1;
    int lo, hi;
    asm volatile("loopnez %[n], 1f\n"
                 "l16si   %[hi], %[ct], 2\n"
                 "l16si   %[lo], %[ct], 0\n"
                 "add     %[hi], %[hi], %[rt]\n"
                 "srai    %[hi], %[hi], 4\n"
                 "add     %[lo], %[lo], %[rt]\n"
                 "addx2   %[hi], %[hi], %[pal]\n"
                 "srai    %[lo], %[lo], 4\n"
                 "l16ui   %[hi], %[hi], 0\n"
                 "addx2   %[lo], %[lo], %[pal]\n"
                 "l16ui   %[lo], %[lo], 0\n"
                 "slli    %[hi], %[hi], 16\n"
                 "or      %[hi], %[hi], %[lo]\n"
                 "s32i    %[hi], %[out], 0\n"
                 "addi    %[ct], %[ct], 4\n"
                 "addi    %[out], %[out], 4\n"
                 "1:\n"
                 : [out] "+&r"(out), [ct] "+&r"(ct), [lo] "=&r"(lo), [hi] "=&r"(hi)
                 : [n] "r"(pairs), [rt] "r"(rt), [pal] "r"(pal)
                 : "memory");
    if (n & 1) *out = pal[(*ct + rt) >> 4];
}
// END VERBATIM PRODUCTION KERNEL

static void oculusRowRef(uint16_t *out, const int16_t *ct,
                         const uint16_t *pal, int rt, int n) {
    for (int x = 0; x < n; x++) out[x] = pal[((int)ct[x] + rt) >> 4];
}

#define DOMAIN 2192
#define GUARD 8
#define STORAGE (DOMAIN + 4 * GUARD)
static int16_t columns[DOMAIN] __attribute__((aligned(16)));
static int16_t phases[8 * 512] __attribute__((aligned(16)));
static uint16_t palette[256] __attribute__((aligned(16)));
static uint16_t got[STORAGE] __attribute__((aligned(16)));
static uint16_t want[STORAGE] __attribute__((aligned(16)));
static uint32_t calls, pixels, mismatches, firstCall, firstLane, firstGot, firstWant;

static void check(const int16_t *ct, int rt, int n, int alignment) {
    const int at = GUARD + alignment * 2;
    const int active = n > 0 ? n : 0;
    const int end = at + active + GUARD;
    calls++;
    pixels += (uint32_t)active;
    for (int i = 0; i < end; i++) got[i] = want[i] = 0x35a9;
    oculusRowRef(want + at, ct, palette, rt, n);
    oculusRowAsm(got + at, ct, palette, rt, n);
    for (int i = 0; i < end; i++) {
        if (got[i] != want[i]) {
            if (!mismatches) {
                firstCall = calls;
                firstLane = (uint32_t)(i - at);
                firstGot = got[i];
                firstWant = want[i];
            }
            mismatches++;
        }
    }
}

int main(void) {
    const uint32_t cp = 8; /* CP3 enable belongs to bare-metal startup only. */
    __asm__ volatile("wsr %0, cpenable\nrsync\n" : : "r"(cp) : "memory");
    for (int x = 0; x < DOMAIN; x++) columns[x] = (int16_t)x;
    for (int i = 0; i < 256; i++) palette[i] = (uint16_t)((i * 257u) ^ 0xa55au);

    /* Exhaustive Cartesian product: 2192 * 1905 = 4,175,760 pixels.
     * This covers all low-nibble carries, both zero and 4095 sums, and
     * every boundary between adjacent palette entries. */
    for (int rt = 0; rt <= 1904; rt++) check(columns, rt, DOMAIN, rt & 3);

    /* All 65,536 RGB565 words through every palette position. At rt=0
     * the gather visits indices 0..136; rt=1904 visits 119..255. */
    for (int bank = 0; bank < 256; bank++) {
        for (int i = 0; i < 256; i++) palette[i] = (uint16_t)((i << 8) | bank);
        check(columns, 0, DOMAIN, bank & 3);
        check(columns, 1904, DOMAIN, (bank + 1) & 3);
    }

    /* Production row bases always select an aligned, padded column phase.
     * Exercise both standard and odd panel widths plus every small n, so
     * vector-zero, scalar-zero, one-pair, and every residual count execute.
     * Alternating endpoints and interior samples distinguish packed lanes. */
    static const int widths[] = {233, 240, 466, 480, 511};
    static const int rows[] = {0, 1, 15, 16, 17, 118, 119, 1903, 1904};
    for (int wi = -1; wi < 37; wi++) {
        const int n = wi < 32 ? wi : widths[wi - 32];
        const int stride = n > 0 ? (n + 7) & ~7 : 8;
        for (int ph = 0; ph < 8; ph++) {
            for (int x = 0; x < stride; x++) {
                const int selector = (x + ph) & 7;
                phases[ph * stride + x] = selector == 0 ? 0 :
                    (selector == 1 ? 2191 : (int16_t)((x * 137 + ph * 251) % DOMAIN));
            }
        }
        for (int ph = 0; ph < 8; ph++) {
            for (int ri = 0; ri < 9; ri++) {
                for (int align = 0; align < 4; align++) {
                    check(phases + ph * stride, rows[ri], n, align);
                }
            }
        }
    }

    if (mismatches) {
        puts_uart("GM_QEMUBENCH_PIE: FAIL oculusRowAsm call="); put_dec(firstCall);
        puts_uart(" lane="); put_dec(firstLane);
        puts_uart(" got="); put_dec(firstGot);
        puts_uart(" want="); put_dec(firstWant);
        puts_uart(" mismatches="); put_dec(mismatches);
        puts_uart("\n");
    } else {
        puts_uart("GM_QEMUBENCH_PIE: PASS oculusRowAsm calls="); put_dec(calls);
        puts_uart(" pixels="); put_dec(pixels);
        puts_uart(" mismatches=0 (full ct/rt domain, all RGB565 words, 8 phases, 4 alignments, tails, 233/240/466/480)\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
