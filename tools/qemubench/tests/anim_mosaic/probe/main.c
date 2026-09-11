/* Instruction probes before using signed PIE min/max/subtract/multiply.
 * PIE reset saturation is symmetric: subtract clamps to -32767, not -32768.
 * Mosaic stays inside +/-4400 so that distinction cannot affect its pixels.
 * Each operation is isolated between known vector loads and a store. */
#include <stdint.h>
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void puts0(const char *s) {
    while (*s)
        UART0_FIFO = (uint8_t)*s++;
}
static void hex0(uint32_t x) {
    const char *h = "0123456789abcdef";
    for (int i = 28; i >= 0; i -= 4)
        UART0_FIFO = h[(x >> i) & 15];
}
static int check(const char *op, int i, int v, int want) {
    if (v == want)
        return 0;
    puts0(op);
    hex0(i);
    puts0(" got ");
    hex0(v);
    puts0(" want ");
    hex0(want);
    puts0("\n");
    return 1;
}
static int16_t a[8] __attribute__((aligned(16))) = {-32768, -4400, -1, 0, 1, 4400, 32766, 32767};
static int16_t b[8] __attribute__((aligned(16))) = {32767, 253, 128, 0, 1, -4400, -32768, 32767};
static int16_t got[8] __attribute__((aligned(16)));
int main(void) {
    uint32_t cp = 255;
    __asm__ volatile("wsr %0, cpenable\nrsync\n" ::"r"(cp));
    int bad = 0;
    __asm__ volatile("ee.vld.128.ip q0, %0, 0\nee.vld.128.ip q1, %1, 0\n"
                     "ee.vmin.s16 q2, q0, q1\nee.vst.128.ip q2, %2, 0\n" ::"r"(a),
                     "r"(b), "r"(got)
                     : "memory");
    for (int i = 0; i < 8; i++)
        bad += check("min ", i, got[i], a[i] < b[i] ? a[i] : b[i]);
    __asm__ volatile("ee.vld.128.ip q0, %0, 0\nee.vld.128.ip q1, %1, 0\n"
                     "ee.vmax.s16 q2, q0, q1\nee.vst.128.ip q2, %2, 0\n" ::"r"(a),
                     "r"(b), "r"(got)
                     : "memory");
    for (int i = 0; i < 8; i++)
        bad += check("max ", i, got[i], a[i] > b[i] ? a[i] : b[i]);
    __asm__ volatile("ee.vld.128.ip q0, %0, 0\nee.vld.128.ip q1, %1, 0\n"
                     "ee.vsubs.s16 q2, q0, q1\nee.vst.128.ip q2, %2, 0\n" ::"r"(a),
                     "r"(b), "r"(got)
                     : "memory");
    for (int i = 0; i < 8; i++) {
        int d = a[i] - b[i];
        if (d < -32767)
            d = -32767;
        if (d > 32767)
            d = 32767;
        bad += check("sub ", i, got[i], d);
    }
    __asm__ volatile("ssai 8\nee.vld.128.ip q0, %0, 0\nee.vld.128.ip q1, %1, 0\n"
                     "ee.vmul.s16 q2, q0, q1\nee.vst.128.ip q2, %2, 0\n" ::"r"(a),
                     "r"(b), "r"(got)
                     : "memory");
    for (int i = 0; i < 8; i++)
        bad += check("mul ", i, got[i], (int16_t)(((int32_t)a[i] * b[i]) >> 8));
    puts0(bad ? "GM_QEMUBENCH_PIE: FAIL mosaic instruction probes\n"
              : "GM_QEMUBENCH_PIE: PASS mosaic vmin/vmax/vsubs/vmul.s16 probes "
                "(32 lanes)\n");
    puts0("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {
    }
}
