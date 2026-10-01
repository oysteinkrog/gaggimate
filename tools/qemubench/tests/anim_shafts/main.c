/* Harness-mode, freestanding test of AnimShafts.cpp. Both functions below
 * are verbatim copies, including their signatures and asm constraints.
 * The page maps screen pixels through a uint16 index stream and applies two
 * separately truncated Q8 products when building its 128 x 32 cell table.
 * These tests cover the complete production operand cube, all 4096 map
 * indices and 65536 RGB565 values, production widths, tails and pointer residues.
 * No libc, heap, floating point, or constructors are needed. */
#include <stdint.h>
#define GM_ANIM_IRAM
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void puts_uart(const char *s) {
    while (*s) {
        if (*s == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}
static void put_dec(uint32_t v) {
    char b[12]; int n = 0;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) UART0_FIFO = (uint8_t)b[--n];
}

GM_ANIM_IRAM __attribute__((noinline)) void shaftsGatherAsm(uint16_t *out, const uint16_t *src,
                                                          const uint16_t *cells, int n) {
    int i0, i1;
    const int pairs = n >> 1;
    asm volatile("loopnez %[pairs], 1f\n"
                 "l16ui   %[i0], %[src], 0\n"
                 "l16ui   %[i1], %[src], 2\n"
                 "addx2   %[i0], %[i0], %[cells]\n"
                 "addx2   %[i1], %[i1], %[cells]\n"
                 "l16ui   %[i0], %[i0], 0\n"
                 "l16ui   %[i1], %[i1], 0\n"
                 "addi    %[src], %[src], 4\n"
                 "slli    %[i1], %[i1], 16\n"
                 "or      %[i0], %[i0], %[i1]\n"
                 "s32i    %[i0], %[out], 0\n"
                 "addi    %[out], %[out], 4\n"
                 "1:\n"
                 : [out] "+&r"(out), [src] "+&r"(src), [i0] "=&r"(i0), [i1] "=&r"(i1)
                 : [cells] "r"(cells), [pairs] "r"(pairs)
                 : "memory");
    if (n & 1) {
        *out = cells[*src];
    }
}

GM_ANIM_IRAM __attribute__((noinline)) void shaftsIndicesAsm(uint16_t *out, const uint16_t *fade,
                                                           const uint16_t *factors) {
    asm volatile("ee.vld.128.ip q4, %[factors], 16\n"
                 "ee.vld.128.ip q5, %[factors], 16\n"
                 "ee.vld.128.ip q0, %[fade], 16\n"
                 "ee.vld.128.ip q1, %[fade], 16\n"
                 "ee.vld.128.ip q2, %[fade], 16\n"
                 "ee.vld.128.ip q3, %[fade], 16\n"
                 "ssai 8\n"
                 "ee.vmul.u16 q0, q0, q4\n"
                 "ee.vmul.u16 q1, q1, q4\n"
                 "ee.vmul.u16 q2, q2, q4\n"
                 "ee.vmul.u16 q3, q3, q4\n"
                 "ee.vmul.u16 q0, q0, q5\n"
                 "ee.vmul.u16 q1, q1, q5\n"
                 "ee.vmul.u16 q2, q2, q5\n"
                 "ee.vmul.u16 q3, q3, q5\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "ee.vst.128.ip q1, %[out], 16\n"
                 "ee.vst.128.ip q2, %[out], 16\n"
                 "ee.vst.128.ip q3, %[out], 16\n"
                 : [out] "+&r"(out), [fade] "+&r"(fade), [factors] "+&r"(factors)
                 :
                 : "memory");
}

/* Independent C references: the actual bandRef gather and the frame's
 * inner cell formula, without packed stores or vector-shaped arithmetic. */
static __attribute__((noinline)) void gatherRef(uint16_t *out, const uint16_t *src,
                                               const uint16_t *cells, int n) {
    for (int i = 0; i < n; i++) out[i] = cells[src[i]];
}
static __attribute__((noinline)) void indicesRef(uint16_t *out, const uint16_t *fade,
                                                const uint16_t *factors) {
    for (int d = 0; d < 32; d++)
        out[d] = (factors[8 + (d & 7)] * ((fade[d] * factors[d & 7]) >> 8)) >> 8;
}
#define ALIGN16 __attribute__((aligned(16)))
#define CAP 1056
static uint16_t cells[4096] ALIGN16;
static uint16_t stream[CAP] ALIGN16;
static uint16_t got[CAP] ALIGN16, want[CAP] ALIGN16;
static uint16_t fade[32] ALIGN16, factors[16] ALIGN16;
static uint32_t calls, pixels, products;
static int failed;
static void mismatch(const char *kernel, int lane, uint16_t a, uint16_t b) {
    if (failed) return;
    failed = 1;
    puts_uart("GM_QEMUBENCH_PIE: FAIL "); puts_uart(kernel);
    puts_uart(" call="); put_dec(calls);
    puts_uart(" lane="); put_dec((uint32_t)lane);
    puts_uart(" got="); put_dec(a);
    puts_uart(" want="); put_dec(b); puts_uart("\n");
}
static void check(const char *kernel, int n) {
    for (int i = 0; i < n; i++) {
        if (got[i] != want[i]) { mismatch(kernel, i, got[i], want[i]); return; }
    }
}
static void clearOutputs(void) {
    for (int i = 0; i < CAP; i++) got[i] = want[i] = 0xA55A;
}
static void gatherCase(int n, int srcOff, int outOff) {
    clearOutputs();
    shaftsGatherAsm(got + outOff, stream + srcOff, cells, n);
    gatherRef(want + outOff, stream + srcOff, cells, n);
    calls++; pixels += (uint32_t)n;
    check("shaftsGatherAsm", CAP); /* Includes all prefix/tail canaries. */
}
static void testGather(void) {
    /* An odd multiplier permutes RGB565 values. Sixteen palettes cover
     * every 16-bit output without exhausting the harness's 128 KB IRAM.
     * Each pass sweeps all 4096 legal map indices, with a distinct colour
     * at every index, including both ends of the 128 x 32 texture. */
    for (int pass = 0; pass < 16 && !failed; pass++) {
        for (int i = 0; i < 4096; i++) cells[i] = (uint16_t)((i + pass * 4096u) * 40503u + 0x2468u);
        for (int base = 0; base < 4096 && !failed; base += 512) {
            const int so = (base / 512) & 7, oo = 2 * ((base / 512) & 3);
            for (int i = 0; i < 512; i++) stream[so + i] = (uint16_t)(base + i);
            gatherCase(512, so, oo);
        }
    }
    /* Includes zero/one pixel, pair boundaries, each production width,
     * a two-row 480-wide band, and a longer loop. out is always 4-aligned;
     * src tests every halfword residue modulo 16, including odd 233 rows. */
    static const int counts[] = {0,1,2,3,7,8,9,15,16,17,31,32,33,233,240,466,480,960,1024};
    for (int so = 0; so < 8 && !failed; so++) {
        for (int oo = 0; oo < 8 && !failed; oo += 2) {
            for (int i = 0; i < CAP; i++) stream[i] = (uint16_t)((i * 523u + so * 7919u) & 4095);
            for (unsigned c = 0; c < sizeof(counts)/sizeof(counts[0]) && !failed; c++)
                gatherCase(counts[c], so, oo);
        }
    }
    clearOutputs();
    shaftsGatherAsm(got, (const uint16_t *)0, (const uint16_t *)0, 0);
    calls++; check("zero-trip", CAP);
}
static void testProducts(void) {
    /* Exhaustive production cube: fall=82..178, gain=168..232, ray=0..255.
     * This covers speed/density/brightness endpoints 0 and 100 as they
     * reach the kernel, every Q8 shift boundary, zero and clipped rays,
     * and products above signed int16 range. A separate lane sweep below
     * extends each individual operand to the full byte range 0..255. */
    for (int gain = 168; gain <= 232 && !failed; gain++) {
        for (int k = 0; k < 8; k++) factors[k] = (uint16_t)gain;
        for (int rv = 0; rv < 256 && !failed; rv++) {
            for (int k = 0; k < 8; k++) factors[k + 8] = (uint16_t)rv;
            for (int base = 82; base <= 178 && !failed; base += 32) {
                /* Alternate aligned offsets so vector stores cannot hide
                 * writing before/after their 64-byte span. */
                const int off = 8 * (1 + ((gain + rv + base) & 1));
                for (int i = 0; i < 64; i++) got[i] = want[i] = 0xA55A;
                for (int d = 0; d < 32; d++) fade[d] = (uint16_t)(base + d > 178 ? 178 : base + d);
                /* Production must set SAR itself, irrespective of the
                 * preceding caller. Only this bare-metal main enables CP3. */
                __asm__ volatile("ssai 3" ::: "memory");
                shaftsIndicesAsm(got + off, fade, factors);
                indicesRef(want + off, fade, factors);
                calls++; products += 32;
                check("shaftsIndicesAsm", 64);
            }
        }
    }
    /* Distinct lane factors catch accidentally using the wrong vector or
     * broadcast half. The production caller fills uniform lanes. */
    for (int t = 0; t < 256 && !failed; t++) {
        for (int k = 0; k < 16; k++) factors[k] = (uint16_t)((t + k * 37) & 255);
        for (int d = 0; d < 32; d++) fade[d] = (uint16_t)((t * 17 + d * 53) & 255);
        for (int i = 0; i < 64; i++) got[i] = want[i] = 0xA55A;
        shaftsIndicesAsm(got + 8, fade, factors);
        indicesRef(want + 8, fade, factors);
        calls++; products += 32; check("shaftsIndicesAsm-lanes", 64);
    }
}
int main(void) {
    /* Bare-metal harness only. Production leaves CPENABLE to FreeRTOS. */
    const uint32_t cp3 = 8;
    __asm__ volatile("wsr %0, cpenable\nrsync" :: "r"(cp3) : "memory");
    puts_uart("shafts: begin gather\n");
    testGather();
    puts_uart("shafts: begin production Q8 cube\n");
    if (!failed) testProducts();
    if (!failed) {
        puts_uart("GM_QEMUBENCH_PIE: PASS shafts calls="); put_dec(calls);
        puts_uart(" gather_pixels="); put_dec(pixels);
        puts_uart(" q8_products="); put_dec(products);
        puts_uart(" mismatches=0 guards=0\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
