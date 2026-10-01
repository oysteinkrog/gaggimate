/* One-instruction probes, run before writing the barrel PIE kernel.
 * Two groups.
 *
 * Group 1: VADDS/VSUBS/VMIN.S32 and VSR.32 are outside ASM_BRIEF's verified
 * list, and the barrel fold needs 32-bit lanes because the phase is 17 bits.
 *
 * Group 2: the narrow-and-multiply half of the kernel. The fold produces four
 * 32-bit lanes whose low halves hold hh and whose high halves are zero, and
 * the multiply wants eight 16-bit lanes, so the kernel needs to know exactly
 * which 16-bit elements EE.VUNZIP.16 keeps in the first register of the pair.
 * The probe pins that down rather than assuming it, then checks that
 * EE.VMUL.U16 takes its right shift from SAR and that EE.VADDS.S16 adds the
 * signed dither without surprises. Saturation is deliberately not probed:
 * this QEMU fork clamps EE.VADDS.S8 to -127 where the silicon gives -128
 * (tests/probe_vadds_s8), so the kernel is built to stay inside the range and
 * never depend on the clamp.
 *
 * Every span here is 16-byte aligned. Only bare-metal main enables CP3. */
#include <stdint.h>

static void puts_uart(const char *s) {
    while (*s) {
        *(volatile uint32_t *)0x60000000u = (uint8_t)*s++;
    }
}

static void put_dec(int v) {
    char b[12];
    int n = 0;
    if (v < 0) {
        *(volatile uint32_t *)0x60000000u = '-';
        v = -v;
    }
    if (v == 0) {
        b[n++] = '0';
    }
    while (v > 0) {
        b[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n > 0) {
        *(volatile uint32_t *)0x60000000u = (uint8_t)b[--n];
    }
}

static int32_t a[4] __attribute__((aligned(16))) = {0, 65536, 131071, 33554431};
static int32_t b[4] __attribute__((aligned(16))) = {131072, 65536, 1, 131071};
static int32_t got[4] __attribute__((aligned(16)));
static int32_t want[4];

#define PROBE(OP) __asm__ volatile("ee.vld.128.ip q0, %[a], 0\n" \
                                 "ee.vld.128.ip q1, %[b], 0\n" \
                                 "nop\n" OP "\n" \
                                 "ee.vst.128.ip q2, %[o], 0\n" \
                                 : : [a] "r"(a), [b] "r"(b), [o] "r"(got) : "memory")

static int check(void) {
    for (int i = 0; i < 4; i++) if (got[i] != want[i]) return 1;
    return 0;
}

/* Group 2 buffers. lo/hi are the two halves of a 4-lane 32-bit result the
 * fold would produce: lo carries hh, hi is zero. */
static int32_t g2a[4] __attribute__((aligned(16))) = {8738, 33314, 20000, 1};
static int32_t g2b[4] __attribute__((aligned(16))) = {255, 1, 128, 60000};
static uint16_t g2out0[8] __attribute__((aligned(16)));
static uint16_t g2out1[8] __attribute__((aligned(16)));

int main(void) {
    uint32_t cp = 8;
    __asm__ volatile("wsr %0, cpenable\nrsync\n" : : "r"(cp) : "memory");
    int bad = 0;
    PROBE("ee.vadds.s32 q2, q0, q1");
    for (int i = 0; i < 4; i++) want[i] = a[i] + b[i];
    bad += check();
    PROBE("ee.vsubs.s32 q2, q1, q0");
    for (int i = 0; i < 4; i++) want[i] = b[i] - a[i];
    bad += check();
    PROBE("ee.vmin.s32 q2, q0, q1");
    for (int i = 0; i < 4; i++) want[i] = a[i] < b[i] ? a[i] : b[i];
    bad += check();
    PROBE("ssai 17\nee.vsr.32 q2, q0");
    for (int i = 0; i < 4; i++) want[i] = a[i] >> 17;
    bad += check();
    PROBE("ssai 3\nee.vsr.32 q2, q0");
    for (int i = 0; i < 4; i++) want[i] = a[i] >> 3;
    bad += check();
    const int group1 = bad;

    /* EE.VUNZIP.16 on the pair (q0, q1): report where the eight even-indexed
     * 16-bit elements land. The kernel needs them all in q0. */
    __asm__ volatile("ee.vld.128.ip q0, %[a], 0\n"
                     "ee.vld.128.ip q1, %[b], 0\n"
                     "ee.vunzip.16 q0, q1\n"
                     "ee.vst.128.ip q0, %[o0], 0\n"
                     "ee.vst.128.ip q1, %[o1], 0\n"
                     : : [a] "r"(g2a), [b] "r"(g2b), [o0] "r"(g2out0), [o1] "r"(g2out1) : "memory");
    /* Expected if the pair reads as sixteen 16-bit elements, q0 first, and the
     * first register keeps the even indices: q0 = the four low halves of g2a
     * then the four low halves of g2b, q1 = the eight high halves. */
    int unzipOk = 1;
    for (int i = 0; i < 4; i++) {
        if (g2out0[i] != (uint16_t)g2a[i]) unzipOk = 0;
        if (g2out0[4 + i] != (uint16_t)g2b[i]) unzipOk = 0;
        if (g2out1[i] != (uint16_t)((uint32_t)g2a[i] >> 16)) unzipOk = 0;
        if (g2out1[4 + i] != (uint16_t)((uint32_t)g2b[i] >> 16)) unzipOk = 0;
    }
    bad += unzipOk ? 0 : 1;

    /* EE.VMUL.U16 with SAR = 16, then EE.VADDS.S16 of a signed offset.
     * g2out0 now holds eight 16-bit multiplicands (hh side), g2out1 is reused
     * as the shade side. */
    static uint16_t shade[8] __attribute__((aligned(16))) = {255, 0, 129, 1, 200, 33, 7, 64};
    static int16_t off[8] __attribute__((aligned(16))) = {20, 24, 22, -2, 2, 21, 23, 0};
    static uint16_t mres[8] __attribute__((aligned(16)));
    __asm__ volatile("ee.vld.128.ip q0, %[h], 0\n"
                     "ee.vld.128.ip q1, %[s], 0\n"
                     "ee.vld.128.ip q2, %[d], 0\n"
                     "ssai 16\n"
                     "ee.vmul.u16 q3, q0, q1\n"
                     "ee.vadds.s16 q3, q3, q2\n"
                     "ee.vst.128.ip q3, %[o], 0\n"
                     : : [h] "r"(g2out0), [s] "r"(shade), [d] "r"(off), [o] "r"(mres) : "memory");
    int mulOk = 1;
    for (int i = 0; i < 8; i++) {
        const uint32_t prod = (uint32_t)g2out0[i] * (uint32_t)shade[i];
        const uint16_t expect = (uint16_t)(((prod >> 16) & 0xFFFFu) + (uint32_t)(int32_t)off[i]);
        if (mres[i] != expect) mulOk = 0;
    }
    bad += mulOk ? 0 : 1;

    if (bad == 0) {
        puts_uart("GM_QEMUBENCH_PIE: PASS barrel instruction probes (vadds/vsubs/vmin.s32, "
                  "vsr.32 SAR=3/17, vunzip.16 pair order, vmul.u16 SAR=16, vadds.s16)\n");
    } else {
        puts_uart("GM_QEMUBENCH_PIE: FAIL barrel instruction probes group1=");
        put_dec(group1);
        puts_uart(" unzip=");
        put_dec(unzipOk);
        puts_uart(" mul=");
        put_dec(mulOk);
        puts_uart(" unzip_q0=");
        for (int i = 0; i < 8; i++) {
            put_dec((int)g2out0[i]);
            puts_uart(",");
        }
        puts_uart(" unzip_q1=");
        for (int i = 0; i < 8; i++) {
            put_dec((int)g2out1[i]);
            puts_uart(",");
        }
        puts_uart("\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
