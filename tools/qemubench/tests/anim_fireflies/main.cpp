/* Real-Xtensa execution check for this animation's one PIE kernel
 * (registry id 9, src/display/ui/default/bganim/AnimFireflies.cpp:
 * fillRowPie, the background run fill). Same idea as tools/qemubench/tests/
 * blend_row/main.c: the instruction sequence below is transcribed by hand
 * from that file (mnemonics, operand registers, immediates, load/store
 * order all unchanged), not regenerated or simplified, and run under
 * Espressif's qemu-system-xtensa fork via this repo's tools/qemubench
 * harness. This is the only rung of the verification ladder that actually
 * executes the EE.* instructions (ee.vld.128.ip/ee.vst.128.ip) and the
 * LOOPNEZ zero-overhead loop on real hardware semantics rather than
 * reading GCC's assembly output.
 *
 * The glow sprites no longer have a kernel here (gm-pciz, 2026-09-12): they
 * are drawn by the page's float expression with a double fallback, plain
 * C++ that the host bench and the fuzzers run as it is, so this test covers
 * the fill alone, against a plain scalar reference, freestanding (no libc,
 * no globals with constructors, no malloc).
 *
 * No OS, no drivers, no libc startup: prints over UART0 by writing its
 * FIFO register directly (ESP32-S3 UART0 base 0x60000000), same as every
 * other test in this harness.
 */
#include <stdint.h>

#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void uart_putc(char c) {
    UART0_FIFO = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_put_hex16(uint16_t v) {
    static const char hex[] = "0123456789abcdef";
    for (int shift = 12; shift >= 0; shift -= 4) uart_putc(hex[(v >> shift) & 0xF]);
}

static void fillRowRef(uint16_t *dst, uint16_t color, int w) {
    for (int i = 0; i < w; i++) {
        dst[i] = color;
    }
}

/* ---- kernel under test: verbatim from AnimFireflies.cpp ---- */

__attribute__((noinline)) static void fillRowPieAsm(uint16_t *dst, uint16_t color, int nOct) {
    static uint16_t bcast[8] __attribute__((aligned(16)));
    for (int i = 0; i < 8; i++) {
        bcast[i] = color;
    }
    const uint16_t *src = bcast;
    uint16_t *wr = dst;
    __asm__ volatile("ee.vld.128.ip q0, %[src], 0\n" /* q0 = color x8, resident for the loop */
                     "loopnez %[n], 2f\n"
                     "ee.vst.128.ip q0, %[wr], 16\n"
                     "2:\n"
                     : [wr] "+r"(wr)
                     : [src] "r"(src), [n] "r"(nOct)
                     : "memory");
}

int main(void) {
    uint32_t cp = 8; /* CP3 only; bare metal has no lazy-enable handler. */
    __asm__ volatile("wsr %0, cpenable\nrsync\n" ::"r"(cp) : "memory");

    int mismatches = 0;
    uint32_t firstBadTag = 0;

    /* fillRowPie vs the plain scalar fill: eight colours spanning the
     * RGB565 range including the all-0/all-1 extremes, at the group counts
     * the run fill can ask for (a whole 480 row is 60 groups, a 240 row 30,
     * a short run 1 or 2, and zero groups must touch nothing), with guard
     * words past the written range. */
    {
        static uint16_t got[496] __attribute__((aligned(16)));
        static uint16_t want[496] __attribute__((aligned(16)));
        static const uint16_t colors[8] = {0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0xA5A5, 0x1234, 0x8410};
        static const int groups[6] = {0, 1, 2, 4, 30, 60};
        for (int c = 0; c < 8; c++) {
            for (int gi = 0; gi < 6; gi++) {
                const int n = groups[gi] * 8;
                for (int i = 0; i < 496; i++) {
                    got[i] = 0xDEAD;
                    want[i] = 0xDEAD;
                }
                fillRowPieAsm(got, colors[c], groups[gi]);
                fillRowRef(want, colors[c], n);
                for (int i = 0; i < 496; i++) {
                    if (got[i] != want[i]) {
                        mismatches++;
                        if (firstBadTag == 0) {
                            firstBadTag = 0x10000000u | ((uint32_t)c << 20) | ((uint32_t)gi << 16) | (uint32_t)i;
                        }
                    }
                }
            }
        }
    }

    uart_puts("GM_QEMUBENCH_ANIM: fireflies mismatches=");
    uart_put_hex16((uint16_t)mismatches);
    uart_puts(" firstBadTag=");
    uart_put_hex16((uint16_t)(firstBadTag >> 16));
    uart_put_hex16((uint16_t)firstBadTag);
    uart_puts("\n");

    if (mismatches == 0) {
        uart_puts("GM_QEMUBENCH_ANIM: PASS fillRowPie bit-exact vs scalar reference "
                  "(8 colors x 6 group counts including zero, guard words past every range)\n");
        /* Also emit the PIE-prefixed marker run.sh's exit-code grep actually
         * looks for (see tools/qemubench/run.sh). */
        uart_puts("GM_QEMUBENCH_PIE: PASS fireflies fillRowPie bit-exact vs scalar reference\n");
    } else {
        uart_puts("GM_QEMUBENCH_ANIM: FAIL fireflies kernel mismatched\n");
        uart_puts("GM_QEMUBENCH_PIE: FAIL fireflies kernel mismatched\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Nothing to return to: spin so QEMU has a stable state. */
    }
}
