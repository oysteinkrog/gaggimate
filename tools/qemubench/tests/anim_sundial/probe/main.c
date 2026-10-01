/* Probe the new signed lane min/max forms before using them in the kernel. */
#include <stdint.h>
#define UART (*(volatile uint32_t *)0x60000000u)
static void puts_uart(const char *s) {
    while (*s)
        UART = (uint8_t)*s++;
}
static int16_t x[8]
    __attribute__((aligned(16))) = {-32768, -1, 0, 63, 64, 4080, 4081, 32767};
static int16_t lo[8]
    __attribute__((aligned(16))) = {64, 64, 64, 64, 64, 64, 64, 64};
static int16_t hi[8] __attribute__((aligned(16))) = {4080, 4080, 4080, 4080,
                                                     4080, 4080, 4080, 4080};
static int16_t out[8] __attribute__((aligned(16)));
int main(void) {
    uint32_t cp = 8;
    __asm__ volatile("wsr %0, cpenable\nrsync\n" ::"r"(cp));
    int16_t *a = x, *b = lo, *c = hi, *d = out;
    __asm__ volatile("ee.vld.128.ip q0, %0, 0\n"
                     "ee.vld.128.ip q1, %1, 0\n"
                     "ee.vld.128.ip q2, %2, 0\n"
                     "ee.vmax.s16 q0, q0, q1\n"
                     "ee.vmin.s16 q0, q0, q2\n"
                     "ee.vst.128.ip q0, %3, 0\n" ::"r"(a),
                     "r"(b), "r"(c), "r"(d)
                     : "memory");
    int bad = 0;
    for (int i = 0; i < 8; i++) {
        int v = x[i];
        if (v < 64)
            v = 64;
        if (v > 4080)
            v = 4080;
        if (v != out[i])
            bad++;
    }
    puts_uart(bad ? "GM_QEMUBENCH_PIE: FAIL sundial min/max probe\n"
                  : "GM_QEMUBENCH_PIE: PASS sundial min/max probe\n");
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {
    }
}
