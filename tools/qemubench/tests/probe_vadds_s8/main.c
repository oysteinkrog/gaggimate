/* Narrow, isolated probe: what exactly does ee.vadds.s8 do at the negative
 * saturation boundary? anim_ember2's QEMU test found ONE mismatch out of
 * 512 checked lanes, at a case where the true mathematical sum is -142
 * (should clamp to -128 under standard two's-complement saturating
 * semantics) but QEMU produced -127 instead. This probe pins
 * down whether that is a real -127 floor (asymmetric two's-complement
 * range but a -127 clamp target instead of -128) or something narrower to
 * that one input pair, by testing a battery of hand-chosen (a,b) pairs
 * that bracket every interesting boundary: exact -128 (no saturation
 * needed at all), one past it, deep past it, and the positive mirror
 * (127, one past, deep past) for comparison. No OS, no libc.
 *
 * Result (2026-09-05): under this QEMU fork every true sum <= -128 comes
 * back -127 and the positive side saturates to 127. The silicon does not
 * agree: the production animtest on the board, sweeping the parameter
 * sets, found the PIE landing on -128 where a -127 reference expected
 * -127 (two pixels at the all-100 set, one palette entry apart). So the
 * -127 floor is an emulator deviation, not a hardware fact; the ember
 * QEMU test carries both floors for that reason. Keep this probe as the
 * quickest way to see whether a newer QEMU build still has it.
 */
#include <stdint.h>

#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void uart_putc(char c) { UART0_FIFO = (uint32_t)(uint8_t)c; }
static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}
static void uart_put_dec(int v) {
    if (v < 0) { uart_putc('-'); v = -v; }
    char buf[12]; int n = 0;
    if (v == 0) buf[n++] = '0';
    while (v > 0) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) uart_putc(buf[--n]);
}

static __attribute__((aligned(16))) int8_t g_a[16];
static __attribute__((aligned(16))) int8_t g_b[16];
static __attribute__((aligned(16))) int8_t g_out[16];

static void vaddS8(int8_t *out, const int8_t *a, const int8_t *b) {
    __asm__ volatile("ee.vld.128.ip q0, %[a], 0\n"
                      "ee.vld.128.ip q1, %[b], 0\n"
                      "ee.vadds.s8 q0, q0, q1\n"
                      "ee.vst.128.ip q0, %[o], 0\n"
                      : [a] "+r"(a), [b] "+r"(b), [o] "+r"(out)
                      :
                      : "memory");
}

int main(void) {
    /* 16 cases, one per lane. */
    const int8_t av[16] = {-100, -100, -100, -128, -128, -128, 0, 100, 100, 100, 127, 127, -1, -1, -50, 50};
    const int8_t bv[16] = { -28,  -42, -100,    0,   -1, -100, 0,  27,  28,  100,  0,   1,  1, -1, -78, 78};
    for (int i = 0; i < 16; i++) { g_a[i] = av[i]; g_b[i] = bv[i]; }
    vaddS8(g_out, g_a, g_b);
    for (int i = 0; i < 16; i++) {
        uart_puts("lane "); uart_put_dec(i);
        uart_puts(": a="); uart_put_dec(av[i]);
        uart_puts(" b="); uart_put_dec(bv[i]);
        uart_puts(" true_sum="); uart_put_dec((int)av[i] + (int)bv[i]);
        uart_puts(" hw_result="); uart_put_dec(g_out[i]);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE: PASS probe complete (see lane dump above)\n");
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
