/* Harness mode, no libc. The production kernel below is copied verbatim
 * from AnimHarmonograph.cpp. Synthetic rows cover the full proven operand
 * domains: ct -1866..306, rowTerm -1770..210, top 196..251, coverage 0..255.
 * The last coverage value extends production's 0..254 bound. Param 0/100
 * affects phases, geometry, top and palette; those reach this kernel only
 * through these bounded tables. Every valid output alignment modulo 16 and
 * scalar tails at the real 480/240/466/233 widths are checked with guards.
 * CPENABLE is set only in this bare-metal main, never by the kernel.
 */
#include <stdint.h>
#define GM_ANIM_IRAM
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}
static void uart_dec(uint32_t n) {
    char b[12]; int k = 0;
    do { b[k++] = '0' + n % 10; n /= 10; } while (n);
    while (k) UART0_FIFO = b[--k];
}

GM_ANIM_IRAM __attribute__((noinline)) void harmonographRowAsm(
    uint16_t *out, const int16_t *ct, const uint8_t *coverage, const uint16_t *pal,
    uint16_t *indices, const uint16_t *one, int rowValue, int topValue, int n16) {
    // Multiplication by 65537 replicates a nonnegative halfword into both
    // halves of a scalar word. Four moves then broadcast all eight lanes.
    const uint32_t rem = (uint32_t)((rowValue & 31) * 32) * 65537u;
    const uint32_t base = (uint32_t)(90 + (rowValue >> 5)) * 65537u;
    const uint32_t crest = (uint32_t)topValue * 65537u;
    asm volatile("ee.movi.32.q q4, %[rem], 0\n"
                 "ee.movi.32.q q4, %[rem], 1\n"
                 "ee.movi.32.q q4, %[rem], 2\n"
                 "ee.movi.32.q q4, %[rem], 3\n"
                 "ee.movi.32.q q5, %[base], 0\n"
                 "ee.movi.32.q q5, %[base], 1\n"
                 "ee.movi.32.q q5, %[base], 2\n"
                 "ee.movi.32.q q5, %[base], 3\n"
                 "ee.movi.32.q q7, %[crest], 0\n"
                 "ee.movi.32.q q7, %[crest], 1\n"
                 "ee.movi.32.q q7, %[crest], 2\n"
                 "ee.movi.32.q q7, %[crest], 3\n"
                 : : [rem] "r"(rem), [base] "r"(base), [crest] "r"(crest) : "memory");
    // Both blocks are in one noinline leaf. GCC cannot insert PIE work or
    // calls between them; its scalar register setup leaves q4/q5/q7 intact.
    uint32_t t0, t1;
    uint16_t *walk;
    const int pairs = 8;
    asm volatile("beqz %[n], 3f\n"
                 "1:\n"
                 "ee.vld.128.ip q0, %[g], 16\n"
                 "ee.vld.128.ip q2, %[ct], 16\n"
                 "ee.vld.128.ip q3, %[ct], 16\n"
                 "ee.zero.q q1\n"
                 "ee.vld.128.ip q6, %[one], 0\n"
                 "ee.vzip.8 q0, q1\n"
                 "ee.vadds.s16 q2, q2, q4\n"
                 "ee.vadds.s16 q3, q3, q4\n"
                 "ssai 10\n"
                 "ee.vmul.s16 q2, q2, q6\n"
                 "ee.vmul.s16 q3, q3, q6\n"
                 "ee.vadds.s16 q2, q2, q5\n"
                 "ee.vadds.s16 q3, q3, q5\n"
                 "ssai 8\n"
                 "ee.vsubs.s16 q6, q7, q2\n"
                 "ee.vmul.s16 q0, q6, q0\n"
                 "ee.vsubs.s16 q6, q7, q3\n"
                 "ee.vmul.s16 q1, q6, q1\n"
                 "ee.vadds.s16 q2, q2, q0\n"
                 "ee.vadds.s16 q3, q3, q1\n"
                 "mov %[walk], %[indices]\n"
                 "ee.vst.128.ip q2, %[walk], 16\n"
                 "ee.vst.128.ip q3, %[walk], -16\n"
                 "loop %[pairs], 2f\n"
                 "l16ui %[t0], %[walk], 0\n"
                 "l16ui %[t1], %[walk], 2\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "addi %[walk], %[walk], 4\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addi %[out], %[out], 4\n"
                 "2:\n"
                 "addi %[n], %[n], -1\n"
                 "bnez %[n], 1b\n"
                 "3:\n"
                 : [out] "+&r"(out), [ct] "+&r"(ct), [g] "+&r"(coverage),
                   [n] "+&r"(n16), [walk] "=&r"(walk), [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [pal] "r"(pal), [indices] "r"(indices), [one] "r"(one), [pairs] "r"(pairs)
                 : "memory");
}

/* Independent, direct translation of the page and bandRef inner loop.
 * No split row remainder and no vector lane layout appear in this spec. */
static void rowRef(uint16_t *out, const int16_t *ct, const uint8_t *coverage,
                   const uint16_t *pal, int rt, int top, int w) {
    for (int x = 0; x < w; ++x) {
        int i = 90 + ((ct[x] + rt * 32) >> 10);
        int g = coverage[x];
        if (g != 0) i += ((top - i) * g) >> 8;
        out[x] = pal[i < 0 ? 0 : i > 255 ? 255 : i];
    }
}

#define CAP 512
static int16_t ct[CAP] __attribute__((aligned(16)));
static uint8_t coverage[CAP] __attribute__((aligned(16)));
static uint16_t pal[256] __attribute__((aligned(16)));
static uint16_t ones[8] __attribute__((aligned(16)));
static uint16_t scratchGuard[32] __attribute__((aligned(16)));
static uint16_t got[CAP+32] __attribute__((aligned(16)));
static uint16_t want[CAP+32] __attribute__((aligned(16)));
static uint32_t calls, pixels, failures;
static int firstLane;
static uint32_t firstCall, firstGot, firstWant;

static void fail(int lane, uint32_t g, uint32_t w) {
    if (failures++ == 0) {
        firstCall = calls; firstLane = lane; firstGot = g; firstWant = w;
    }
}

static void check(int w, int align, int rt, int top, uint32_t seed, int fixedCt) {
    int offset = 8 + 2 * align; // 16,20,24,28 bytes, every legal word alignment
    for (int x = 0; x < CAP; ++x) {
        ct[x] = fixedCt < -1866 ? (int16_t)((seed + x * 73u) % 2173u - 1866) : fixedCt;
        coverage[x] = fixedCt < -1866 ? (uint8_t)(seed * 7u + x * 37u) : (uint8_t)x;
    }
    for (int k = 0; k < 256; ++k) pal[k] = (uint16_t)(seed * 257u + k * 4051u);
    for (int k = 0; k < 8; ++k) ones[k] = 1;
    for (int x = 0; x < CAP+32; ++x) got[x] = want[x] = 0xa55a;
    for (int x = 0; x < 32; ++x) scratchGuard[x] = 0xbeef;
    ++calls;
    harmonographRowAsm(got + offset, ct, coverage, pal, scratchGuard + 8, ones, rt, top, w >> 4);
    // The production dispatcher's non-vector tail, also checked against the
    // independent reference. Odd widths never require aligned pair stores.
    for (int x = w & ~15; x < w; ++x) {
        int i = 90 + ((ct[x] + rt * 32) >> 10);
        i += ((top - i) * coverage[x]) >> 8;
        got[offset+x] = pal[i];
    }
    rowRef(want + offset, ct, coverage, pal, rt, top, w);
    for (int x = 0; x < CAP+32; ++x)
        if (got[x] != want[x]) fail(x-offset, got[x], want[x]);
    for (int x = 0; x < 8; ++x) {
        if (scratchGuard[x] != 0xbeef) fail(-100-x, scratchGuard[x], 0xbeef);
        if (scratchGuard[24+x] != 0xbeef) fail(-200-x, scratchGuard[24+x], 0xbeef);
    }
    pixels += w;
}

int main(void) {
    uint32_t cp = 8; // CP3, exclusively owned by this freestanding harness
    asm volatile("wsr %0, cpenable\nrsync\n" :: "r"(cp) : "memory");
    static const int widths[] = {0,1,2,7,8,15,16,17,31,32,33,233,240,466,480};
    static const int rowEdges[] = {-1770,-1761,-1025,-1024,-1023,-33,-32,-31,-1,0,1,31,32,33,210};
    // All alignments, zero and minimum vector trips, every scalar tail,
    // and parameter extremes as they reach the top and colour operands.
    for (int a = 0; a < 4; ++a)
        for (int wi = 0; wi < 15; ++wi)
            for (int ri = 0; ri < 15; ++ri)
                for (int t = 0; t < 3; ++t)
                    check(widths[wi], a, rowEdges[ri], t == 0 ? 196 : t == 1 ? 229 : 251,
                          (uint32_t)(a * 123 + wi * 937 + ri * 17 + t), -2000);
    // Every rowTerm value, all 56 top values, full-width varying lanes.
    for (int rt = -1770; rt <= 210; ++rt)
        check(480, rt & 3, rt, 196 + (rt + 1770) % 56, (uint32_t)(rt + 1770), -2000);
    // Every ct x coverage combination at four row extremes/shift boundaries.
    for (int c = -1866; c <= 306; ++c) {
        check(256, c & 3, -1770, 196, (uint32_t)(c + 1866), c);
        check(256, c & 3, -1, 251, (uint32_t)(c + 2981), c);
        check(256, c & 3, 0, 196, (uint32_t)(c + 4321), c);
        check(256, c & 3, 210, 251, (uint32_t)(c + 5987), c);
    }
    if (!failures) {
        uart_puts("GM_QEMUBENCH_PIE: PASS harmonographRowAsm calls="); uart_dec(calls);
        uart_puts(" pixels="); uart_dec(pixels);
        uart_puts(" mismatches=0 guards=OK full-ranges tails alignments params=0/100\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL harmonographRowAsm mismatches="); uart_dec(failures);
        uart_puts(" first_call="); uart_dec(firstCall);
        uart_puts(" lane="); uart_dec((uint32_t)firstLane);
        uart_puts(" got="); uart_dec(firstGot); uart_puts(" want="); uart_dec(firstWant);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
