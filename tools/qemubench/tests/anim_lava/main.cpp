/* QEMU self-test stub for AnimLava.cpp, round 4 (2026-09-04). Lava ships no
 * Xtensa kernel as of this round: band() is a direct call to bandRef(), the
 * portable C++ reference, with no hand-written asm and no restructuring --
 * see AnimLava.cpp's file-top round-4 comment for why (three rounds of hand
 * kernels each measured slower on the device than HEAD's own band() at
 * matched table placement). With no kernel there is nothing left for this
 * harness to cross-check against a C reference, so it is reduced to a stub
 * that reports that plainly rather than being deleted -- the directory and
 * build wiring (tools/qemubench/tests/anim_lava/) stay in place for a future
 * round that reintroduces a kernel with a proven win.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as every other test in this
 * directory.
 */
#include <cstdint>

#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void uart_putc(char c) { UART0_FIFO = (uint32_t)(uint8_t)c; }

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

extern "C" int main(void) {
    uart_puts("GM_QEMUBENCH_PIE: PASS lava ships no asm kernel as of round 4 -- band() calls bandRef() directly, "
               "nothing to cross-check\n");
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* spin */
    }
    return 0;
}
