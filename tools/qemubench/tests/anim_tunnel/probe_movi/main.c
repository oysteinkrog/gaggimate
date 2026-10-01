/* Probe the one PIE instruction used by Tunnel that ASM_BRIEF.md's executed
 * list does not cover: EE.MOVI.32.Q. Check every selector with distinct words
 * before using it to broadcast the angle term. Harness mode, no libc. */
#include <stdint.h>

#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void puts_uart(const char *p) {
    while (*p) {
        if (*p == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*p++;
    }
}

static uint32_t words[8] __attribute__((aligned(16)));

int main(void) {
    /* Bare-metal main only. Production lets FreeRTOS enable CP3 lazily. */
    uint32_t enable = 8;
    asm volatile("wsr.cpenable %0\nrsync\n" : : "r"(enable) : "memory");
    uint32_t *out = words + 4;
    uint32_t a = 0x00000000u, b = 0x7fff8000u;
    uint32_t c = 0x12345678u, d = 0xffffffffu;
    for (int i = 0; i < 8; i++) words[i] = 0xa55aa55au;
    asm volatile("ee.movi.32.q q6, %[a], 0\n"
                 "ee.movi.32.q q6, %[b], 1\n"
                 "ee.movi.32.q q6, %[c], 2\n"
                 "ee.movi.32.q q6, %[d], 3\n"
                 "ee.vst.128.ip q6, %[out], 0\n"
                 : [out] "+r"(out)
                 : [a] "r"(a), [b] "r"(b), [c] "r"(c), [d] "r"(d)
                 : "memory");
    int bad = words[4] != a || words[5] != b || words[6] != c || words[7] != d;
    for (int i = 0; i < 4; i++) bad |= words[i] != 0xa55aa55au;
    puts_uart(bad ? "GM_QEMUBENCH_PIE: FAIL tunnel EE.MOVI.32.Q selectors\n"
                  : "GM_QEMUBENCH_PIE: PASS tunnel EE.MOVI.32.Q selectors 0..3\n");
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
