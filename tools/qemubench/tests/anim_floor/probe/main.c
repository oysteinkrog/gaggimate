/* One-instruction probes before using additional PIE forms in Floor.
 * No libc. The shared harness provides vectors and the windowed ABI.
 */
#include <stdint.h>
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void puts0(const char *s) { while (*s) UART0_FIFO = (uint8_t)*s++; }
static uint16_t a[8] __attribute__((aligned(16)));
static int16_t b[8] __attribute__((aligned(16)));
static int16_t out[8] __attribute__((aligned(16)));
int main(void) {
    uint32_t cp = 8;
    __asm__ volatile("wsr %0, cpenable\nisync\n" : : "r"(cp));
    int bad = 0;
    for (int i = 0; i < 8; i++) { a[i] = (uint16_t)(i * 33); b[i] = 106; }
    {
        const uint16_t *p = a + 3;
        int16_t *o = out;
        __asm__ volatile("ee.vldbc.16.ip q0, %[p], 2\n"
                         "nop\nee.vst.128.ip q0, %[o], 0\n"
                         : [p] "+r"(p), [o] "+r"(o) : : "memory");
        for (int i = 0; i < 8; i++) bad += out[i] != 99;
        bad += p != a + 4;
    }
    {
        const uint16_t *pa = a;
        const int16_t *pb = b;
        int16_t *o = out;
        __asm__ volatile("ee.vld.128.ip q0, %[a], 0\n"
                         "ee.vld.128.ip q1, %[b], 0\n"
                         "nop\nee.vsubs.s16 q2, q0, q1\n"
                         "ee.vst.128.ip q2, %[o], 0\n"
                         : [a] "+r"(pa), [b] "+r"(pb), [o] "+r"(o) : : "memory");
        for (int i = 0; i < 8; i++) bad += out[i] != (int)a[i] - 106;
    }
    {
        for (int i = 0; i < 8; i++) b[i] = (int16_t)(i * 9 - 33);
        const uint16_t *pa = a;
        const int16_t *pb = b;
        int16_t *o = out;
        __asm__ volatile("ee.vld.128.ip q0, %[a], 0\n"
                         "ee.vld.128.ip q1, %[b], 0\n"
                         "ssai 8\nee.vmul.s16 q2, q0, q1\n"
                         "nop\nee.vst.128.ip q2, %[o], 0\n"
                         : [a] "+r"(pa), [b] "+r"(pb), [o] "+r"(o) : : "memory");
        for (int i = 0; i < 8; i++) bad += out[i] != (((int)a[i] * b[i]) >> 8);
    }
    {
        const uint32_t word = 0x8421f00du;
        int16_t *o = out;
        __asm__ volatile("ee.movi.32.q q0, %[v], 0\n"
                         "ee.movi.32.q q0, %[v], 1\n"
                         "ee.movi.32.q q0, %[v], 2\n"
                         "ee.movi.32.q q0, %[v], 3\n"
                         "ee.vst.128.ip q0, %[o], 0\n"
                         : [o] "+r"(o) : [v] "r"(word) : "memory");
        for (int i = 0; i < 8; i++) bad += (uint16_t)out[i] != (i & 1 ? 0x8421 : 0xf00d);
    }
    puts0(bad ? "GM_QEMUBENCH_PIE: FAIL floor PIE instruction probes\n" :
        "GM_QEMUBENCH_PIE: PASS floor PIE probes: vldbc.16.ip, vsubs.s16, vmul.s16, movi.32.q\n");
    puts0("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
