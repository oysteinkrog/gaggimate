/* Instruction probes run before writing gyroidRowAsm. All addresses used
 * by 128-bit PIE loads/stores are explicitly aligned. No saturation at
 * a 32-bit limit is needed by the production arithmetic. */
#include <stdint.h>
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void puts_uart(const char *s) {
    while (*s) { if (*s == '\n') UART0_FIFO = '\r'; UART0_FIFO = (uint8_t)*s++; }
}
int main(void) {
    /* Bare-metal harness only. Production lets FreeRTOS enable CP3 lazily. */
    uint32_t cp = 255;
    __asm__ volatile("wsr %0, cpenable\nrsync\n" : : "r"(cp));
    int32_t field[4] __attribute__((aligned(16))) = {-49152, -256, 0, 49152};
    uint32_t columns[4] __attribute__((aligned(16))) = {0x80000100u, 0, 0x40000080u, 0x7fff0001u};
    uint32_t bias[4] __attribute__((aligned(16))) = {128,128,128,128};
    uint32_t mask[4] __attribute__((aligned(16))) = {65535,65535,65535,65535};
    int32_t magnitudes[4] __attribute__((aligned(16)));
    int32_t products[4] __attribute__((aligned(16)));
    int32_t highs[4] __attribute__((aligned(16)));
    int32_t lane;
    const void *f=field,*c=columns,*b=bias,*m=mask;
    void *a=magnitudes,*p=products,*h=highs;
    __asm__ volatile("ee.vld.128.ip q0, %[f], 0\n"
                     "ee.zero.q q7\n"
                     "ee.vsubs.s32 q1, q7, q0\n"
                     "ee.vmax.s32 q1, q1, q0\n"
                     "ssai 8\n"
                     "ee.vsr.32 q1, q1\n"
                     "ee.vst.128.ip q1, %[a], 0\n"
                     "ee.vld.128.ip q0, %[c], 0\n"
                     "ee.vld.128.ip q2, %[b], 0\n"
                     "ee.vld.128.ip q3, %[m], 0\n"
                     "ssai 0\n"
                     "ee.andq q1, q0, q3\n"
                     "ee.vsubs.s16 q1, q1, q2\n"
                     "ee.vmul.s16 q1, q1, q2\n"
                     "ssai 16\n"
                     "ee.vsr.32 q2, q0\n"
                     "ee.vsl.32 q1, q1\n"
                     "ee.andq q2, q2, q3\n"
                     "ee.vsr.32 q1, q1\n"
                     "ee.vst.128.ip q1, %[p], 0\n"
                     "ee.vst.128.ip q2, %[h], 0\n"
                     "ee.movi.32.a q1, %[lane], 3\n"
                     : [f] "+&r"(f),[c] "+&r"(c),[b] "+&r"(b),[m] "+&r"(m),
                       [a] "+&r"(a),[p] "+&r"(p),[h] "+&r"(h),[lane] "=&r"(lane)
                     : : "memory");
    const int32_t wantM[4]={192,1,0,192},wantP[4]={16384,-16384,0,-16256},wantH[4]={32768,0,16384,32767};
    int bad=lane!=-16256;
    for(int i=0;i<4;i++) bad |= magnitudes[i]!=wantM[i] || products[i]!=wantP[i] || highs[i]!=wantH[i];
    puts_uart(bad ? "GM_QEMUBENCH_PIE: FAIL gyroid instruction probes\n" :
                    "GM_QEMUBENCH_PIE: PASS gyroid instruction probes (signed multiply, widen, abs, shifts, lane move)\n");
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
