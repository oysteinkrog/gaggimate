/* Isolated checks for the PIE forms not on ASM_BRIEF's verified list. */
#include <stdint.h>
#define UART (*(volatile uint32_t *)0x60000000u)
static void puts_uart(const char *s) { while (*s) UART = (uint8_t)*s++; }
static int32_t a[4] __attribute__((aligned(16))) = {INT32_MIN, -65537, 65536, INT32_MAX};
static int32_t b[4] __attribute__((aligned(16))) = {-1, 65537, -65536, 1};
static int32_t out[4] __attribute__((aligned(16)));
static void dump(void) { for (int i=0;i<4;i++) { for(int k=28;k>=0;k-=4) UART="0123456789abcdef"[((uint32_t)out[i]>>k)&15]; UART=32; } UART=10; }
int main(void) {
    uint32_t cp = 8;
    __asm__ volatile("wsr %0, cpenable\nrsync" : : "r"(cp) : "memory");
    const int32_t *ap = a, *bp = b;
    int32_t *op = out;
    int ok = 1, lane;
    __asm__ volatile("ee.vld.128.ip q0, %1, 0\n"
                     "ee.vld.128.ip q1, %2, 0\n"
                     "ee.vmin.s32 q2, q0, q1\n"
                     "ee.vst.128.ip q2, %0, 0\n"
                     : "+r"(op), "+r"(ap), "+r"(bp) : : "memory");
    puts_uart("min "); dump();
    for (int i=0;i<4;i++) if (out[i] != (a[i]<b[i]?a[i]:b[i])) ok=0;
    __asm__ volatile("ee.vadds.s32 q2, q0, q1\n"
                     "ee.vst.128.ip q2, %0, 0\n" : "+r"(op) : : "memory");
    puts_uart("add "); dump();
    /* S32 add clamps symmetrically to -INT32_MAX, including negative overflow. */
    if(out[0]!=-INT32_MAX || out[1]!=0 || out[2]!=0 || out[3]!=INT32_MAX) ok=0;
    __asm__ volatile("ssai 16\nee.vsr.32 q2, q0\n"
                     "ee.vst.128.ip q2, %0, 0\n" : "+r"(op) : : "memory");
    puts_uart("shift "); dump();
    for (int i=0;i<4;i++) if(out[i] != (a[i] >> 16)) ok=0;
    __asm__ volatile("ee.movi.32.a q0, %0, 3\n" : "=r"(lane));
    out[0]=lane; puts_uart("move "); dump();
    if(lane!=INT32_MAX) ok=0;
    puts_uart(ok ? "GM_QEMUBENCH_PIE: PASS cube vmin.s32 vadds.s32 vsr.32 movi.32.a probe\n" :
                   "GM_QEMUBENCH_PIE: FAIL cube instruction probe\n");
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
