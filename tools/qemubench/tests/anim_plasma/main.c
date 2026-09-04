/* Real-Xtensa execution check for AnimPlasma.cpp's device-path kernel,
 * plasmaRowAsm: a fused hand-scheduled scalar kernel that computes
 * idx = ((ct[i] + rt) >> 4) & 255 with one EXTUI per pixel (matching what
 * GCC 14 already compiles bandRef() to -- see AnimPlasma.cpp's comment
 * above plasmaRowAsm for the round-2 finding that a separate PIE index
 * pass could not beat this), then gathers pal[idx[i]] and packs two pixels
 * per store, four pixels per iteration via the hardware zero-overhead
 * LOOPNEZ. This is not proven any other way: the host bench
 * (tools/animbench) only ever compiles AnimPlasma.cpp's C++ path (band()
 * dispatches to this kernel only under __XTENSA__, which the host is not),
 * and xtensa-asm14 only proves the instructions assemble and that GCC did
 * not spill a register around the block -- neither one actually EXECUTES
 * an EXTUI or a LOOPNEZ. This test does, under Espressif's
 * qemu-system-xtensa fork.
 *
 * This kernel uses no PIE (no EE.* instruction), so unlike round 1's
 * kernel this test does not need to enable CPENABLE at all.
 *
 * The asm block below is transcribed by hand from plasmaRowAsm in
 * AnimPlasma.cpp (mnemonics, operand names, load/store order all
 * unchanged) -- not regenerated or simplified, so a PASS here is direct
 * evidence about the exact sequence that file contains, matching
 * tools/qemubench/tests/blend_row/main.c and tests/anim_starfield/main.c's
 * precedent for transcribing rather than re-deriving.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as every other test in this
 * harness.
 */
#include <stdint.h>

#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void uart_putc(char c) {
    UART0_FIFO = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

static void uart_put_hex16(uint16_t v) {
    static const char hex[] = "0123456789abcdef";
    uart_putc(hex[(v >> 12) & 0xF]);
    uart_putc(hex[(v >> 8) & 0xF]);
    uart_putc(hex[(v >> 4) & 0xF]);
    uart_putc(hex[v & 0xF]);
}

static void uart_put_dec(int v) {
    if (v < 0) {
        uart_putc('-');
        v = -v;
    }
    char buf[12];
    int n = 0;
    if (v == 0) {
        buf[n++] = '0';
    }
    while (v > 0) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        uart_putc(buf[--n]);
    }
}

/* Verbatim instruction sequence and operand names from plasmaRowAsm in
 * src/display/ui/default/bganim/AnimPlasma.cpp. */
static void plasmaRowAsm(uint16_t *dst, const int16_t *ct, int rt, const uint16_t *pal, int nQuad) {
    const int16_t *ctp = ct;
    uint16_t *dstp = dst;
    int32_t t1, t2, t3, t4; /* scratch; values unused after the block */
    __asm__ volatile("loopnez %[n], 2f\n"
                      "l16si   %[t1], %[ctp], 0\n"
                      "l16si   %[t2], %[ctp], 2\n"
                      "l16si   %[t3], %[ctp], 4\n"
                      "l16si   %[t4], %[ctp], 6\n"
                      "add     %[t1], %[t1], %[rt]\n"
                      "add     %[t2], %[t2], %[rt]\n"
                      "add     %[t3], %[t3], %[rt]\n"
                      "add     %[t4], %[t4], %[rt]\n"
                      "extui   %[t1], %[t1], 4, 8\n"
                      "extui   %[t2], %[t2], 4, 8\n"
                      "extui   %[t3], %[t3], 4, 8\n"
                      "extui   %[t4], %[t4], 4, 8\n"
                      "addx2   %[t1], %[t1], %[pal]\n"
                      "addx2   %[t2], %[t2], %[pal]\n"
                      "l16ui   %[t1], %[t1], 0\n"
                      "addx2   %[t3], %[t3], %[pal]\n"
                      "l16ui   %[t2], %[t2], 0\n"
                      "addx2   %[t4], %[t4], %[pal]\n"
                      "l16ui   %[t3], %[t3], 0\n"
                      "l16ui   %[t4], %[t4], 0\n"
                      "slli    %[t2], %[t2], 16\n"
                      "or      %[t1], %[t1], %[t2]\n"
                      "slli    %[t4], %[t4], 16\n"
                      "or      %[t3], %[t3], %[t4]\n"
                      "s32i    %[t1], %[dstp], 0\n"
                      "s32i    %[t3], %[dstp], 4\n"
                      "addi    %[ctp], %[ctp], 8\n"
                      "addi    %[dstp], %[dstp], 8\n"
                      "2:\n"
                      : [ctp] "+r"(ctp), [dstp] "+r"(dstp), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3),
                        [t4] "=&r"(t4)
                      : [rt] "r"(rt), [pal] "r"(pal), [n] "r"(nQuad)
                      : "memory");
}

/* Scalar C reference: exactly AnimPlasma.cpp's bandRef() per-pixel math,
 * pal[((ct[i] + rt) >> 4) & 255], independent of loop shape or interleave
 * width. */
static void rowRef(uint16_t *dst, const int16_t *ct, int rt, const uint16_t *pal, int nQuad) {
    int n = nQuad * 4;
    for (int i = 0; i < n; i++) {
        int32_t v = (int32_t)ct[i] + rt;
        dst[i] = pal[(v >> 4) & 255];
    }
}

/* Synthetic palette: pal[i] = 0x1000 + i, so any addressing bug (wrong
 * stride, off-by-one, wrong base) shows up as a wrong hex value that
 * reveals which index was actually read. */
static uint16_t g_pal[256] __attribute__((aligned(16)));

/* Call 1: exhaustive index coverage through the FUSED kernel (not just the
 * gather in isolation): with rt = 0, ct[i] = i * 16 makes
 * ((ct[i] + 0) >> 4) & 255 == i exactly for every i in 0..255, so this
 * proves every one of the 256 possible palette addresses the real kernel
 * can produce, not just the gather half. ct values top out at 255*16=4080,
 * far inside int16. */
static int16_t g_ctIdx[256] __attribute__((aligned(16)));
static uint16_t g_outIdx[256] __attribute__((aligned(16)));
static uint16_t g_refIdx[256] __attribute__((aligned(16)));

/* Call 2: boundary/sign coverage, three rt values (0, +1024, -1024, the
 * documented rt extremes from AnimPlasma.cpp's range analysis). Covers
 * shift-boundary crossings from both sides of zero, the documented |ct|
 * extremes (+-1216), and stress values well past that (+-4080) while still
 * far under int16/int32 overflow -- proves the L16SI sign-extend and the
 * EXTUI bit-field trick agree with a plain C `>>` on negative values, which
 * is the one place a logical-vs-arithmetic shift bug would show up. 32
 * entries, n=8 quads, so both this call's full width AND a mid-loop
 * pointer advance (more than one LOOPNEZ iteration) are exercised. */
static const int16_t g_ctBoundary[32] __attribute__((aligned(16))) = {
    0,    -1,   1,    15,   -15,  16,   -16,   -17,   /* shift-boundary crossings near zero */
    1216, -1216, 1200, -1200, 192, -192, 100,  -100,  /* documented |ct| extremes and mid-range */
    2000, -2000, 500,  -500, 255,  -255, 4080, -4080, /* well past |ct| range, still << int32 overflow */
    31,   -31,   17,   -17,  8,    -8,   4095, -4096, /* more shift-boundary crossings */
};
static uint16_t g_outBoundary[32] __attribute__((aligned(16)));
static uint16_t g_refBoundary[32] __attribute__((aligned(16)));

/* Call 3: minimal trip count, nQuad=1 (one LOOPNEZ iteration, no
 * cross-iteration pointer advance to get wrong). */
static const int16_t g_ctMin[4] __attribute__((aligned(16))) = {300, -300, 1216, -1216};
static uint16_t g_outMin[4] __attribute__((aligned(16)));
static uint16_t g_refMin[4] __attribute__((aligned(16)));

int main(void) {
    for (int i = 0; i < 256; i++) {
        g_pal[i] = (uint16_t)(0x1000 + i);
        g_ctIdx[i] = (int16_t)(i * 16);
    }

    int mismatches = 0;
    int firstBadTag = -1, firstBadPos = -1;
    uint16_t firstBadGot = 0, firstBadWant = 0;

    /* Call 1: exhaustive index sweep, rt = 0. */
    plasmaRowAsm(g_outIdx, g_ctIdx, 0, g_pal, 256 / 4);
    rowRef(g_refIdx, g_ctIdx, 0, g_pal, 256 / 4);
    for (int i = 0; i < 256; i++) {
        if (g_outIdx[i] != g_refIdx[i]) {
            if (mismatches == 0) {
                firstBadTag = 1;
                firstBadPos = i;
                firstBadGot = g_outIdx[i];
                firstBadWant = g_refIdx[i];
            }
            mismatches++;
        }
    }

    /* Call 2: boundary/sign sweep, three rt values. */
    const int rts[3] = {0, 1024, -1024};
    for (int r = 0; r < 3; r++) {
        plasmaRowAsm(g_outBoundary, g_ctBoundary, rts[r], g_pal, 32 / 4);
        rowRef(g_refBoundary, g_ctBoundary, rts[r], g_pal, 32 / 4);
        for (int i = 0; i < 32; i++) {
            if (g_outBoundary[i] != g_refBoundary[i]) {
                if (mismatches == 0) {
                    firstBadTag = 20 + r;
                    firstBadPos = i;
                    firstBadGot = g_outBoundary[i];
                    firstBadWant = g_refBoundary[i];
                }
                mismatches++;
            }
        }
    }

    /* Call 3: minimal trip count. */
    plasmaRowAsm(g_outMin, g_ctMin, 333, g_pal, 1);
    rowRef(g_refMin, g_ctMin, 333, g_pal, 1);
    for (int i = 0; i < 4; i++) {
        if (g_outMin[i] != g_refMin[i]) {
            if (mismatches == 0) {
                firstBadTag = 30;
                firstBadPos = i;
                firstBadGot = g_outMin[i];
                firstBadWant = g_refMin[i];
            }
            mismatches++;
        }
    }

    uart_puts("GM_QEMUBENCH_PIE: plasmaRowAsm mismatches=");
    uart_put_dec(mismatches);
    uart_puts(" (of 256 exhaustive-index + 96 boundary [3 rt values x 32] + 4 min-trip)\n");

    if (mismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS plasmaRowAsm bit-exact vs C++ reference "
                   "(256 exhaustive-index lanes at rt=0, 96 boundary/sign lanes at rt in {0,1024,-1024}, "
                   "4 min-trip lanes)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL first mismatch tag=");
        uart_put_dec(firstBadTag);
        uart_puts(" pos=");
        uart_put_dec(firstBadPos);
        uart_puts(" got=0x");
        uart_put_hex16(firstBadGot);
        uart_puts(" want=0x");
        uart_put_hex16(firstBadWant);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Spin so QEMU has a stable state; nothing to return to. */
    }
}
