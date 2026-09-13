/* Narrow probe: does this QEMU fork compute ee.ldxq.32's address correctly?
 *
 * espressif/qemu issue #162 (QEMU-301) reports that ee.ldxq.32 and
 * ee.stxq.32 compute an indexed address four bytes too low. The sundial
 * kernel gathers its palette and its two lookup tables with ee.ldxq.32, so
 * whether the QEMU rung can cover those paths turns on this one question.
 *
 * ee.ldxq.32 qu, qs, as, sel4, sel8: take one 16-bit lane out of qs,
 * multiply it by 4, add as, clear the low two bits, load 32 bits from
 * there, and write them into one 32-bit lane of qu. Feed it indices 0 to 7
 * over a table whose entry i holds 1000 + i and print what comes back.
 * No OS, no libc.
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

static __attribute__((aligned(16))) uint32_t g_table[16];
static __attribute__((aligned(16))) uint16_t g_idx[8];
static __attribute__((aligned(16))) uint32_t g_lo[4];
static __attribute__((aligned(16))) uint32_t g_hi[4];

static void gather(const uint16_t *idx, const uint32_t *tab, uint32_t *lo, uint32_t *hi) {
    __asm__ volatile("ee.vld.128.ip q0, %[i], 0\n"
                     "ee.ldxq.32 q1, q0, %[t], 0, 0\n"
                     "ee.ldxq.32 q1, q0, %[t], 1, 1\n"
                     "ee.ldxq.32 q1, q0, %[t], 2, 2\n"
                     "ee.ldxq.32 q1, q0, %[t], 3, 3\n"
                     "ee.ldxq.32 q2, q0, %[t], 0, 4\n"
                     "ee.ldxq.32 q2, q0, %[t], 1, 5\n"
                     "ee.ldxq.32 q2, q0, %[t], 2, 6\n"
                     "ee.ldxq.32 q2, q0, %[t], 3, 7\n"
                     "ee.vst.128.ip q1, %[l], 0\n"
                     "ee.vst.128.ip q2, %[h], 0\n"
                     : [i] "+r"(idx), [t] "+r"(tab), [l] "+r"(lo), [h] "+r"(hi)
                     :
                     : "memory");
}

int main(void) {
    for (int i = 0; i < 16; i++) g_table[i] = (uint32_t)(1000 + i);
    for (int i = 0; i < 8; i++) g_idx[i] = (uint16_t)i;
    gather(g_idx, g_table, g_lo, g_hi);
    int bad = 0;
    for (int k = 0; k < 4; k++) {
        /* The way the sundial kernel uses it: sel8 selects the 16-bit
         * source lane and sel4 the 32-bit destination lane, so source
         * lanes 0 to 3 land in g_lo and 4 to 7 in g_hi. */
        uart_puts("lo["); uart_put_dec(k); uart_puts("]="); uart_put_dec((int)g_lo[k]);
        uart_puts(" want "); uart_put_dec(1000 + k); uart_puts("\n");
        uart_puts("hi["); uart_put_dec(k); uart_puts("]="); uart_put_dec((int)g_hi[k]);
        uart_puts(" want "); uart_put_dec(1000 + 4 + k); uart_puts("\n");
        if (g_lo[k] != (uint32_t)(1000 + k)) bad++;
        if (g_hi[k] != (uint32_t)(1000 + 4 + k)) bad++;
    }
    if (bad) uart_puts("GM_QEMUBENCH_PIE: FAIL ldxq32\n");
    else uart_puts("GM_QEMUBENCH_PIE: PASS ldxq32\n");
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
    return 0;
}
