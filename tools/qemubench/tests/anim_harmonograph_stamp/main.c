/* Harness mode, no libc. harmonographStampAsm below is copied verbatim from
 * AnimHarmonograph.cpp: one 11-row stamp max-composited into a coverage
 * buffer through the PIE, with the row placed inside a 32-byte aligned
 * window by SAR_BYTE funnel shifts. The reference is the portable stamp
 * loop, byte for byte, over a guarded buffer: every x offset modulo 16 (so
 * every funnel shift 0..15), every comet level 0..255, all sixteen stamp
 * shapes (here: pseudo-random rows with zero corners and a few extreme
 * rows), and existing coverage from 0 to 254. The guard checks that no byte
 * outside the eleven stamp rows and the stamp's own columns changed, which
 * is the property the PSRAM slack in production relies on. CPENABLE is set
 * only in this bare-metal main, never by the kernel.
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

#define SW 11
#define SH 11
#define STAMP16_ROW 16

GM_ANIM_IRAM __attribute__((noinline)) void harmonographStampAsm(uint8_t *base, int stride, const uint8_t *rows16, int k,
                                                                    const uint8_t *lvlPtr, int nrows) {
    const uint32_t bias = 0x80808080u;
    const uint8_t *usar = rows16 + k;
    asm volatile("ee.movi.32.q q7, %[bias], 0\n"
                 "ee.movi.32.q q7, %[bias], 1\n"
                 "ee.movi.32.q q7, %[bias], 2\n"
                 "ee.movi.32.q q7, %[bias], 3\n"
                 "ee.vldbc.8 q6, %[lvl]\n"
                 "ee.zero.q q1\n"
                 "ssai 8\n"
                 "loop %[n], 1f\n"
                 "ee.ld.128.usar.ip q0, %[usar], 16\n"
                 "ee.vmul.u8 q0, q0, q6\n"
                 "ee.src.q q2, q1, q0\n"
                 "ee.src.q q3, q0, q1\n"
                 "ee.vld.128.ip q4, %[out], 16\n"
                 "ee.vld.128.ip q5, %[out], -16\n"
                 "ee.xorq q2, q2, q7\n"
                 "ee.xorq q3, q3, q7\n"
                 "ee.xorq q4, q4, q7\n"
                 "ee.xorq q5, q5, q7\n"
                 "ee.vmax.s8 q4, q4, q2\n"
                 "ee.vmax.s8 q5, q5, q3\n"
                 "ee.xorq q4, q4, q7\n"
                 "ee.xorq q5, q5, q7\n"
                 "ee.vst.128.ip q4, %[out], 16\n"
                 "ee.vst.128.ip q5, %[out], -16\n"
                 "add %[out], %[out], %[stride]\n"
                 "1:\n"
                 : [out] "+&r"(base), [usar] "+&r"(usar)
                 : [n] "r"(nrows), [stride] "r"(stride), [lvl] "r"(lvlPtr), [bias] "r"(bias)
                 : "memory");
}

/* Independent reference: the portable loop from AnimHarmonograph.cpp without
 * the extents (a zero byte never raises a max, so the result is the same). */
static void stampRef(uint8_t *dst, int stride, int x0, const uint8_t *sb, int lvl) {
    for (int j = 0; j < SH; ++j) {
        uint8_t *out = dst + j * stride + x0;
        for (int i = 0; i < SW; ++i) {
            const int g = (sb[j * SW + i] * lvl) >> 8;
            const int o = out[i];
            out[i] = (uint8_t)(o > g ? o : g);
        }
    }
}

#define STRIDE 96
#define ROWS (SH + 4)
static uint8_t stamp[SW * SH];
static uint8_t stamp16[SH * STAMP16_ROW] __attribute__((aligned(16)));
static uint8_t got[STRIDE * ROWS + 64] __attribute__((aligned(16)));
static uint8_t want[STRIDE * ROWS + 64] __attribute__((aligned(16)));
static uint32_t calls, failures, firstCall, firstIdx, firstGot, firstWant;

static uint32_t rng = 12345u;
static uint32_t next(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static void check(int x0, int lvl, int shape) {
    ++calls;
    for (int j = 0; j < SH; ++j)
        for (int i = 0; i < SW; ++i) {
            const int corner = (i == 0 || i == SW - 1) && (j == 0 || j == SH - 1);
            uint8_t v;
            if (shape == 0) v = 255;
            else if (shape == 1) v = 0;
            else if (shape == 2) v = (uint8_t)(i * 23 + j * 17);
            else v = (uint8_t)(next() & 255);
            stamp[j * SW + i] = corner ? 0 : v;
        }
    for (int j = 0; j < SH; ++j)
        for (int i = 0; i < STAMP16_ROW; ++i)
            stamp16[j * STAMP16_ROW + i] = i < SW ? stamp[j * SW + i] : 0;
    for (int i = 0; i < STRIDE * ROWS + 64; ++i) {
        const uint8_t v = shape == 4 ? 254 : (uint8_t)(next() % 255);
        got[i] = want[i] = v;
    }
    /* Stamp row 0 lands on buffer row 2: rows 0..1 and 13..14 are guards. */
    uint8_t *dst = got + 2 * STRIDE + 16;
    uint8_t *ref = want + 2 * STRIDE + 16;
    const int base = (x0 - 1) & ~15;
    const uint8_t lvl8 = (uint8_t)lvl;
    harmonographStampAsm(dst + base, STRIDE, stamp16, 16 - (x0 - base), &lvl8, SH);
    stampRef(ref, STRIDE, x0, stamp, lvl);
    for (int i = 0; i < STRIDE * ROWS + 64; ++i) {
        if (got[i] != want[i]) {
            if (failures++ == 0) { firstCall = calls; firstIdx = (uint32_t)i; firstGot = got[i]; firstWant = want[i]; }
            return;
        }
    }
}

int main(void) {
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\nrsync\n" :: "r"(cp) : "memory");
    /* x0 from 1 to 48 covers every window offset three times, and both the
     * window that starts on the row's own boundary (x0 & 15 == 0, so base is
     * the boundary before) and the one where the stamp ends exactly at 32. */
    for (int shape = 0; shape < 5; ++shape)
        for (int x0 = 1; x0 <= 48; ++x0)
            for (int lvl = 0; lvl < 256; lvl += (shape == 3 ? 1 : 5))
                check(x0, lvl, shape);
    for (int x0 = 1; x0 <= 48; ++x0) { check(x0, 255, 3); check(x0, 78, 3); check(x0, 1, 3); }
    if (!failures) {
        uart_puts("GM_QEMUBENCH_PIE: PASS harmonographStampAsm calls="); uart_dec(calls);
        uart_puts(" mismatches=0 guards=OK offsets=1..48 levels=0..255 shapes=5\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL harmonographStampAsm mismatches="); uart_dec(failures);
        uart_puts(" first_call="); uart_dec(firstCall);
        uart_puts(" idx="); uart_dec(firstIdx);
        uart_puts(" got="); uart_dec(firstGot); uart_puts(" want="); uart_dec(firstWant);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
