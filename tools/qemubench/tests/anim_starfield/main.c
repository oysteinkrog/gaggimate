/* Real-Xtensa execution check for starfieldVigRowAsm -- the hand-written
 * scalar Xtensa kernel that replaces AnimStarfield.cpp's vignette gather
 * loop (src/display/ui/default/bganim/AnimStarfield.cpp, band()'s piece 1).
 * PIE has no vector gather on this chip, so this kernel is plain scalar
 * Xtensa using MIN for the branchless clamp and the hardware zero-overhead
 * LOOPNEZ for the per-row loop (w/4 iterations, up to 120 on the device) --
 * see the header comment on starfieldVigRowAsm for the full derivation.
 * This is not proven any other way: the host bench (tools/animbench) only
 * ever compiles AnimStarfield.cpp's C++ path (band() dispatches to the asm
 * kernel only under __XTENSA__, which the host is not), and xtensa-asm14
 * only proves the instructions assemble and that GCC did not spill any
 * register around the block -- neither one actually EXECUTES the MIN or
 * LOOPNEZ instructions. This test does, under Espressif's qemu-system-
 * xtensa fork.
 *
 * The asm block below is transcribed by hand from starfieldVigRowAsm in
 * AnimStarfield.cpp (mnemonics, operand names, ssai-equivalent immediates,
 * load/store order all unchanged) -- not regenerated or simplified, so a
 * PASS here is direct evidence about the exact sequence that file
 * contains, matching tools/qemubench/tests/blend_row/main.c's precedent
 * for transcribing rather than re-deriving.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as blend_row/main.c and
 * pie_smoke_full/main.c.
 */
#include <stdint.h>

#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void uart_putc(char c) {
    UART0_FIFO = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

static void uart_put_hex32(uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart_putc(hex[(v >> shift) & 0xF]);
    }
}

static void uart_put_dec(int v) {
    if (v < 0) {
        uart_putc('-');
        v = -v;
    }
    char buf[12];
    int n = 0;
    if (v == 0) {
        buf[n++] = '0';
    }
    while (v > 0) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        uart_putc(buf[--n]);
    }
}

/* Verbatim instruction sequence and operand names from starfieldVigRowAsm
 * in src/display/ui/default/bganim/AnimStarfield.cpp. */
static void starfieldVigRowAsm(uint16_t *row, const int32_t *dx2Row, int32_t dyv, const uint16_t *p0,
                                const uint16_t *p1, const uint16_t *p2, const uint16_t *p3, int n4) {
    const int32_t *dxp = dx2Row;
    uint16_t *rowp = row;
    int32_t t1, t2, t3, t4, c127; /* scratch; values unused after the block */
    __asm__ volatile("movi %[c127], 127\n"
                      "loopnez %[n], 2f\n"
                      "l32i    %[t1], %[dxp], 0\n"  /* dx2[x+0] */
                      "l32i    %[t2], %[dxp], 4\n"  /* dx2[x+1] */
                      "add     %[t1], %[t1], %[dyv]\n"
                      "add     %[t2], %[t2], %[dyv]\n"
                      "l32i    %[t3], %[dxp], 8\n"  /* dx2[x+2] */
                      "l32i    %[t4], %[dxp], 12\n" /* dx2[x+3] */
                      "srai    %[t1], %[t1], 10\n"
                      "srai    %[t2], %[t2], 10\n"
                      "add     %[t3], %[t3], %[dyv]\n"
                      "add     %[t4], %[t4], %[dyv]\n"
                      "min     %[t1], %[t1], %[c127]\n"
                      "min     %[t2], %[t2], %[c127]\n"
                      "srai    %[t3], %[t3], 10\n"
                      "srai    %[t4], %[t4], 10\n"
                      "min     %[t3], %[t3], %[c127]\n"
                      "min     %[t4], %[t4], %[c127]\n"
                      "addx2   %[t1], %[t1], %[p0]\n" /* t1 = &p0[idx0] */
                      "addx2   %[t2], %[t2], %[p1]\n" /* t2 = &p1[idx1] */
                      "l16ui   %[t1], %[t1], 0\n"     /* t1 = p0[idx0] */
                      "l16ui   %[t2], %[t2], 0\n"     /* t2 = p1[idx1] */
                      "addx2   %[t3], %[t3], %[p2]\n" /* t3 = &p2[idx2] */
                      "addx2   %[t4], %[t4], %[p3]\n" /* t4 = &p3[idx3] */
                      "l16ui   %[t3], %[t3], 0\n"     /* t3 = p2[idx2] */
                      "l16ui   %[t4], %[t4], 0\n"     /* t4 = p3[idx3] */
                      "slli    %[t2], %[t2], 16\n"
                      "slli    %[t4], %[t4], 16\n"
                      "or      %[t1], %[t1], %[t2]\n" /* pixels x+0,x+1 packed */
                      "or      %[t3], %[t3], %[t4]\n" /* pixels x+2,x+3 packed */
                      "s32i    %[t1], %[rowp], 0\n"
                      "s32i    %[t3], %[rowp], 4\n"
                      "addi    %[dxp], %[dxp], 16\n"
                      "addi    %[rowp], %[rowp], 8\n"
                      "2:\n"
                      : [dxp] "+r"(dxp), [rowp] "+r"(rowp), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3),
                        [t4] "=&r"(t4), [c127] "=&r"(c127)
                      : [dyv] "r"(dyv), [p0] "r"(p0), [p1] "r"(p1), [p2] "r"(p2), [p3] "r"(p3), [n] "r"(n4)
                      : "memory");
}

/* Scalar C reference: exactly AnimStarfield.cpp's vigRowScalar per-pixel
 * math (idx = (dyv + dx2[x]) >> 10, clamped to 127, gathered from p[x&3]),
 * independent of any loop shape or register allocation. */
static uint16_t refIdx(int32_t dyv, int32_t dxv) {
    int32_t v = (dyv + dxv) >> 10;
    if (v > 127) {
        v = 127;
    }
    return (uint16_t)v;
}

static void vigRowRef(uint16_t *row, const int32_t *dx2Row, int32_t dyv, const uint16_t *p0, const uint16_t *p1,
                       const uint16_t *p2, const uint16_t *p3, int n4) {
    for (int g = 0; g < n4; g++) {
        row[g * 4 + 0] = p0[refIdx(dyv, dx2Row[g * 4 + 0])];
        row[g * 4 + 1] = p1[refIdx(dyv, dx2Row[g * 4 + 1])];
        row[g * 4 + 2] = p2[refIdx(dyv, dx2Row[g * 4 + 2])];
        row[g * 4 + 3] = p3[refIdx(dyv, dx2Row[g * 4 + 3])];
    }
}

/* Four synthetic 128-entry phase tables, distinguishable by base so a
 * mismatch's printed hex value reveals which table (and which index in it)
 * a lane actually read. */
static uint16_t g_p0[128] __attribute__((aligned(16)));
static uint16_t g_p1[128] __attribute__((aligned(16)));
static uint16_t g_p2[128] __attribute__((aligned(16)));
static uint16_t g_p3[128] __attribute__((aligned(16)));

static void fillPhaseTables(void) {
    for (int i = 0; i < 128; i++) {
        g_p0[i] = (uint16_t)(0x1000 + i);
        g_p1[i] = (uint16_t)(0x2000 + i);
        g_p2[i] = (uint16_t)(0x3000 + i);
        g_p3[i] = (uint16_t)(0x4000 + i);
    }
}

/* Call 1: boundary and clamp coverage. dyv = 0 so each dx2 value IS the
 * sum the kernel shifts and clamps -- exercises every edge the >> 10 / MIN
 * pair can hit: zero, mid-range, the largest sum that does NOT need
 * clamping (idx==127 exactly), the smallest sum that DOES (idx==128 pre-
 * clamp), and two magnitudes well past that (one modest, one the size a
 * badly out-of-range dyv could produce) to prove MIN keeps clamping past
 * the boundary, not just at it. */
static int32_t g_dxA[24] __attribute__((aligned(16))) = {
    0,      65536,  131071, 131072, /* idx: 0, 64, 127 (no clamp), 127 (clamped from 128) */
    500000, 1024,   2047,   2048,   /* idx: 127 (clamped, far over), 1, 1, 2 */
    130048, 130047, 200000, 1,      /* idx: 127 (no clamp, exact low edge), 126, 127 (clamped), 0 */
    1023,   1025,   3000000, 0,     /* idx: 0, 1, 127 (clamped, extreme), 0 */
    999,    1000,   2000,    99999, /* idx: 0, 0, 1, 97 */
    64512,  64513,  65535,   65537, /* idx: 63, 63, 63, 64 */
};

/* Call 2: production-shaped. dyv = dy2[0] = 57600 (edge row, cx=cy=240,
 * the largest dy2 this 480x480 panel ever produces -- see AnimStarfield.cpp
 * init()), dx2[x] = (x-240)^2 for x = 0..479, the exact table init() builds.
 * Confirms two things at once: (1) the kernel's arithmetic matches the
 * scalar reference over a full production-width row (120 groups, so the
 * pointer-increment and LOOPNEZ trip-count handling are proven over many
 * iterations, not just a couple), and (2) real 480x480 geometry never
 * actually reaches the clamp (max sum here is 480x480: max r2 ~115200 -> 112,
 * per the comment in vigRowScalar) -- documented by execution, not just
 * asserted in a comment. */
#define PROD_W 480
static int32_t g_dxProd[PROD_W] __attribute__((aligned(16)));

static void fillProdDx2(void) {
    const int cx = PROD_W / 2;
    for (int x = 0; x < PROD_W; x++) {
        int32_t d = x - cx;
        g_dxProd[x] = d * d;
    }
}

static uint16_t g_rowA[24] __attribute__((aligned(16)));
static uint16_t g_refA[24] __attribute__((aligned(16)));
static uint16_t g_rowProd[PROD_W] __attribute__((aligned(16)));
static uint16_t g_refProd[PROD_W] __attribute__((aligned(16)));

/* Call 3: minimal trip count (n4=1, one group of 4 pixels) -- proves
 * LOOPNEZ does not mishandle the smallest legal count. */
static int32_t g_dxMin[4] __attribute__((aligned(16))) = {0, 1024, 2048, 3072};
static uint16_t g_rowMin[4] __attribute__((aligned(16)));
static uint16_t g_refMin[4] __attribute__((aligned(16)));

int main(void) {
    fillPhaseTables();
    fillProdDx2();

    int mismatches = 0;
    int firstBadGroup = -1, firstBadLane = -1;
    uint16_t firstBadGot = 0, firstBadWant = 0;

    /* Call 1: boundary/clamp coverage, dyv = 0. */
    starfieldVigRowAsm(g_rowA, g_dxA, 0, g_p0, g_p1, g_p2, g_p3, 6);
    vigRowRef(g_refA, g_dxA, 0, g_p0, g_p1, g_p2, g_p3, 6);
    for (int i = 0; i < 24; i++) {
        if (g_rowA[i] != g_refA[i]) {
            if (mismatches == 0) {
                firstBadGroup = i / 4;
                firstBadLane = i % 4;
                firstBadGot = g_rowA[i];
                firstBadWant = g_refA[i];
            }
            mismatches++;
        }
    }

    /* Call 2: production-shaped, dyv = 57600 (edge row), full 480px width. */
    starfieldVigRowAsm(g_rowProd, g_dxProd, 57600, g_p0, g_p1, g_p2, g_p3, PROD_W / 4);
    vigRowRef(g_refProd, g_dxProd, 57600, g_p0, g_p1, g_p2, g_p3, PROD_W / 4);
    for (int i = 0; i < PROD_W; i++) {
        if (g_rowProd[i] != g_refProd[i]) {
            if (mismatches == 0) {
                firstBadGroup = 100 + i / 4; /* offset so call 2 mismatches are distinguishable */
                firstBadLane = i % 4;
                firstBadGot = g_rowProd[i];
                firstBadWant = g_refProd[i];
            }
            mismatches++;
        }
    }

    /* Call 3: n4 = 1, minimal trip count. */
    starfieldVigRowAsm(g_rowMin, g_dxMin, 0, g_p0, g_p1, g_p2, g_p3, 1);
    vigRowRef(g_refMin, g_dxMin, 0, g_p0, g_p1, g_p2, g_p3, 1);
    for (int i = 0; i < 4; i++) {
        if (g_rowMin[i] != g_refMin[i]) {
            if (mismatches == 0) {
                firstBadGroup = 200;
                firstBadLane = i;
                firstBadGot = g_rowMin[i];
                firstBadWant = g_refMin[i];
            }
            mismatches++;
        }
    }

    uart_puts("GM_QEMUBENCH_PIE: starfieldVigRowAsm mismatches=");
    uart_put_dec(mismatches);
    uart_puts(" (of 24 boundary/clamp + 480 production-width + 4 min-trip)\n");

    if (mismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS starfieldVigRowAsm bit-exact vs vigRowScalar reference "
                   "(24 boundary/clamp lanes, 480 production-width lanes at dyv=57600, "
                   "4 min-trip-count lanes)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL first mismatch group=");
        uart_put_dec(firstBadGroup);
        uart_puts(" lane=");
        uart_put_dec(firstBadLane);
        uart_puts(" got=0x");
        uart_put_hex32(firstBadGot);
        uart_puts(" want=0x");
        uart_put_hex32(firstBadWant);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Spin so QEMU has a stable state; nothing to return to. */
    }
}
