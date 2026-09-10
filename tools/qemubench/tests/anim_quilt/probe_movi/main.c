/* Isolated EE.MOVI.32.A probe before using it in quiltRowAsm. The four
 * selectors must extract the corresponding word without a memory spill. */
#include <stdint.h>
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void puts_uart(const char *s) {
    while (*s) UART0_FIFO = (uint8_t)*s++;
}
int main(void) {
    uint32_t enable = 8;
    __asm__ volatile("wsr %0, cpenable\nisync\n" : : "r"(enable));
    uint32_t a = 0x76543210u, b = 0xfedcba98u;
    uint32_t c = 0xdeadbeefu, d = 0x80000001u;
    uint32_t r0, r1, r2, r3;
    __asm__ volatile("ee.movi.32.q q0, %[a], 0\n"
                     "ee.movi.32.q q0, %[b], 1\n"
                     "ee.movi.32.q q0, %[c], 2\n"
                     "ee.movi.32.q q0, %[d], 3\n"
                     "ee.movi.32.a q0, %[r0], 0\n"
                     "ee.movi.32.a q0, %[r1], 1\n"
                     "ee.movi.32.a q0, %[r2], 2\n"
                     "ee.movi.32.a q0, %[r3], 3\n"
                     : [r0] "=&r"(r0), [r1] "=&r"(r1), [r2] "=&r"(r2), [r3] "=&r"(r3)
                     : [a] "r"(a), [b] "r"(b), [c] "r"(c), [d] "r"(d) : "memory");
    if (r0 == a && r1 == b && r2 == c && r3 == d)
        puts_uart("GM_QEMUBENCH_PIE: PASS quilt EE.MOVI.32.A selectors 0..3\n");
    else puts_uart("GM_QEMUBENCH_PIE: FAIL quilt EE.MOVI.32.A\n");
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
