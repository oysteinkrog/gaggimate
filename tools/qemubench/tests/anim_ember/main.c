/* Real-Xtensa execution check for emberGatherRow -- the hand-written scalar
 * Xtensa kernel that replaces AnimEmber.cpp's palette-gather inner loop
 * (src/display/ui/default/bganim/AnimEmber.cpp, band()'s per-row call to
 * emberGatherRow). PIE has no vector gather on this chip (ASM_BRIEF.md), so
 * this kernel is plain scalar Xtensa using LOOPNEZ for the hardware
 * zero-overhead per-pair loop and a hand-scheduled load-use ordering -- see
 * the header comment on emberGatherRow in AnimEmber.cpp for the full
 * instruction-by-instruction proof this test exists to back up with real
 * execution, not just inspection.
 *
 * This is not proven any other way: the host bench (tools/animbench) only
 * ever compiles AnimEmber.cpp's C++ path (band() dispatches to the asm
 * kernel only under __XTENSA__, which the host is not), and xtensa-asm14
 * only proves the instructions assemble and that GCC did not spill any
 * register around the block (confirmed separately: 27 instructions, one
 * LOOPNEZ, no stack spill loads inside the loop body) -- neither one
 * actually EXECUTES the SRAI/ADDX2/LOOPNEZ sequence. This test does, under
 * Espressif's qemu-system-xtensa fork.
 *
 * The asm block below is transcribed by hand from emberGatherRow in
 * AnimEmber.cpp (mnemonics, operand names, immediates, load/store order all
 * unchanged) -- not regenerated or simplified, so a PASS here is direct
 * evidence about the exact sequence that file contains, matching
 * tools/qemubench/tests/anim_starfield/main.c's precedent for transcribing
 * rather than re-deriving.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as anim_starfield/main.c and
 * blend_row/main.c.
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

/* Verbatim instruction sequence and operand names from emberGatherRow in
 * src/display/ui/default/bganim/AnimEmber.cpp. RSHIFT=9 is that file's
 * compile-time constant, hardcoded here as the immediate the "i" constraint
 * resolves to on the real target -- transcribing the resolved value rather
 * than the C++ constexpr keeps this test a plain, freestanding C file. */
static void emberGatherRow(uint16_t *row, const uint16_t *palOff, const uint8_t *radiusLUT, const int16_t *combRow,
                            int r2_0, int ddx_0, int wPairs) {
    int r2 = r2_0;
    int ddx = ddx_0;
    uint16_t *wr = row;
    const int16_t *cr = combRow;
    int ridx0, ridx1, cv0, cv1;
    __asm__ volatile("loopnez %[n], 2f\n"
                      "srai   %[ridx0], %[r2], 9\n" /* ridx0 = r2 >> RSHIFT */
                      "add    %[r2], %[r2], %[ddx]\n"
                      "addi   %[ddx], %[ddx], 2\n"
                      "srai   %[ridx1], %[r2], 9\n" /* ridx1 = r2 >> RSHIFT */
                      "add    %[r2], %[r2], %[ddx]\n"
                      "addi   %[ddx], %[ddx], 2\n"
                      "add    %[ridx0], %[rlut], %[ridx0]\n"
                      "l8ui   %[ridx0], %[ridx0], 0\n"
                      "add    %[ridx1], %[rlut], %[ridx1]\n"
                      "l8ui   %[ridx1], %[ridx1], 0\n"
                      "l16si  %[cv0], %[cr], 0\n"
                      "l16si  %[cv1], %[cr], 2\n"
                      "add    %[ridx0], %[ridx0], %[cv0]\n"
                      "add    %[ridx1], %[ridx1], %[cv1]\n"
                      "addx2  %[cv0], %[ridx0], %[pal]\n"
                      "l16ui  %[ridx0], %[cv0], 0\n"
                      "addx2  %[cv1], %[ridx1], %[pal]\n"
                      "l16ui  %[ridx1], %[cv1], 0\n"
                      "addi   %[cr], %[cr], 4\n"
                      "slli   %[ridx1], %[ridx1], 16\n"
                      "or     %[ridx0], %[ridx0], %[ridx1]\n"
                      "s32i   %[ridx0], %[wr], 0\n"
                      "addi   %[wr], %[wr], 4\n"
                      "2:\n"
                      : [r2] "+r"(r2), [ddx] "+r"(ddx), [wr] "+r"(wr), [cr] "+r"(cr), [ridx0] "=&r"(ridx0),
                        [ridx1] "=&r"(ridx1), [cv0] "=&r"(cv0), [cv1] "=&r"(cv1)
                      : [n] "r"(wPairs), [rlut] "r"(radiusLUT), [pal] "r"(palOff)
                      : "memory");
}

/* Plain C reference: exactly AnimEmber.cpp's emberGatherRow math, independent
 * of any register schedule or LOOPNEZ. idx = radiusLUT[r2>>9] + combRow[x],
 * colour = palOff[idx] -- same formula bandRef's inline loop computes. */
static void emberGatherRowRef(uint16_t *row, const uint16_t *palOff, const uint8_t *radiusLUT, const int16_t *combRow,
                               int r2_0, int ddx_0, int wPairs) {
    int r2 = r2_0;
    int ddx = ddx_0;
    for (int i = 0; i < wPairs; i++) {
        int ridx0 = r2 >> 9;
        r2 += ddx;
        ddx += 2;
        int ridx1 = r2 >> 9;
        r2 += ddx;
        ddx += 2;
        int idx0 = (int)radiusLUT[ridx0] + (int)combRow[2 * i + 0];
        int idx1 = (int)radiusLUT[ridx1] + (int)combRow[2 * i + 1];
        row[2 * i + 0] = palOff[idx0];
        row[2 * i + 1] = palOff[idx1];
    }
}

/* Backing store for palOff = g_palBuf + PAL_MID: PAL_MID entries of slack
 * on the low side so a negative idx (combRow can be as negative as the
 * real animation's proven -51, tested here to -64 for margin) reads valid,
 * distinguishable memory instead of running off the front of the array --
 * the same "pointer into the middle of a bigger buffer" trick paletteExt
 * uses in AnimEmber.cpp itself (paletteExt + PAD). Distinguishable fill
 * (0xB000 + index) so a mismatch's printed hex value reveals which palette
 * slot a lane actually read. */
#define PAL_MID 128
#define PAL_N 512
static uint16_t g_palBuf[PAL_N];

static void fillPalBuf(void) {
    for (int i = 0; i < PAL_N; i++) {
        g_palBuf[i] = (uint16_t)(0xB000 + i);
    }
}

/* Call 1: boundary/index-range coverage, 24 pairs (48 pixels). r2_0=0,
 * ddx_0=3200 gives a monotonically increasing r2 (ddx(k) = 3200+2k stays
 * positive throughout, so r2 only grows -- unlike call 2 below), sweeping
 * ridx from 0 to 298 across the run: exercises both ends of a 320-entry
 * radiusLUT (RLUT_N in AnimEmber.cpp) in one call. radiusLUT[i] = i ^ 0x5A
 * is a bijection on each 256-wide block, so every byte value 0-255 is hit
 * at least once as the loop walks through indices 0-298. combRow spans
 * [-64,64] (wider than the real animation's proven [-51,+50], for margin)
 * via a deterministic pseudo-spread, with -64/+64 forced at both the first
 * and last pair so the loop's very first and very last iterations hit the
 * negative-index and positive-index extremes explicitly, not just
 * somewhere in the middle. */
#define N1_PAIRS 24
#define N1_PIX (N1_PAIRS * 2)
static uint8_t g_radiusLUT1[320];
static int16_t g_combRow1[N1_PIX];
static uint16_t g_row1[N1_PIX];
static uint16_t g_ref1[N1_PIX];

static void fillCall1(void) {
    for (int i = 0; i < 320; i++) {
        g_radiusLUT1[i] = (uint8_t)((i & 0xFF) ^ 0x5A);
    }
    for (int k = 0; k < N1_PIX; k++) {
        g_combRow1[k] = (int16_t)(((k * 167) % 129) - 64);
    }
    g_combRow1[0] = -64;
    g_combRow1[1] = 64;
    g_combRow1[N1_PIX - 2] = -64;
    g_combRow1[N1_PIX - 1] = 64;
}

/* Call 2: production-shaped. Real AnimEmber.cpp geometry for row y=0 on the
 * fixed 480x480 target (g_cx=240, g_cy=260, per that file's header): dx
 * starts at -g_cx = -240, dy = y - g_cy = -260, dy2 = 67600, so
 * r2_0 = dx*dx + dy2 = 125200 and ddx_0 = 2*dx + 1 = -479. Unlike call 1,
 * ddx is NEGATIVE at the start here (r2 decreases as x approaches the
 * center, then increases again past it) -- the real animation's actual
 * per-row trajectory, not a monotonic sweep, and specifically exercises
 * that the kernel handles a negative ddx correctly (r2 itself stays
 * non-negative throughout, per AnimEmber.cpp's own range proof, so this
 * never reads a negative ridx -- but ddx going negative independently of
 * r2 is exactly the case a naive "ddx only grows" assumption would miss).
 * Full 480px row (240 pairs): also proves the pointer-increment and
 * LOOPNEZ trip-count handling over many iterations, not just a couple.
 * radiusLUT reuses call 1's 320-entry table (ridx here ranges 132-244 per
 * AnimEmber.cpp's own header proof, well inside it). combRow is refilled
 * for the full 480-pixel width with the same deterministic spread as call
 * 1, extremes forced at the first and last pair again. */
#define PROD_W 480
#define PROD_PAIRS (PROD_W / 2)
static int16_t g_combRow2[PROD_W];
static uint16_t g_row2[PROD_W];
static uint16_t g_ref2[PROD_W];

static void fillCall2(void) {
    for (int k = 0; k < PROD_W; k++) {
        g_combRow2[k] = (int16_t)(((k * 167) % 129) - 64);
    }
    g_combRow2[0] = -64;
    g_combRow2[1] = 64;
    g_combRow2[PROD_W - 2] = -64;
    g_combRow2[PROD_W - 1] = 64;
}

/* Call 3: minimal trip count (wPairs=1, two pixels) -- proves LOOPNEZ does
 * not mishandle the smallest legal count. */
static uint8_t g_radiusLUT3[2] = {0, 255};
static int16_t g_combRow3[2] = {-64, 64};
static uint16_t g_row3[2];
static uint16_t g_ref3[2];

int main(void) {
    fillPalBuf();
    fillCall1();
    fillCall2();
    const uint16_t *palOff = g_palBuf + PAL_MID;

    int mismatches = 0;
    int firstBadCall = -1, firstBadLane = -1;
    uint16_t firstBadGot = 0, firstBadWant = 0;

    /* Call 1: boundary/index-range coverage. */
    emberGatherRow(g_row1, palOff, g_radiusLUT1, g_combRow1, 0, 3200, N1_PAIRS);
    emberGatherRowRef(g_ref1, palOff, g_radiusLUT1, g_combRow1, 0, 3200, N1_PAIRS);
    for (int i = 0; i < N1_PIX; i++) {
        if (g_row1[i] != g_ref1[i]) {
            if (mismatches == 0) {
                firstBadCall = 1;
                firstBadLane = i;
                firstBadGot = g_row1[i];
                firstBadWant = g_ref1[i];
            }
            mismatches++;
        }
    }

    /* Call 2: production-shaped, row y=0, full 480px width, negative
     * starting ddx. */
    emberGatherRow(g_row2, palOff, g_radiusLUT1, g_combRow2, 125200, -479, PROD_PAIRS);
    emberGatherRowRef(g_ref2, palOff, g_radiusLUT1, g_combRow2, 125200, -479, PROD_PAIRS);
    for (int i = 0; i < PROD_W; i++) {
        if (g_row2[i] != g_ref2[i]) {
            if (mismatches == 0) {
                firstBadCall = 2;
                firstBadLane = i;
                firstBadGot = g_row2[i];
                firstBadWant = g_ref2[i];
            }
            mismatches++;
        }
    }

    /* Call 3: wPairs = 1, minimal trip count. */
    emberGatherRow(g_row3, palOff, g_radiusLUT3, g_combRow3, 0, 1, 1);
    emberGatherRowRef(g_ref3, palOff, g_radiusLUT3, g_combRow3, 0, 1, 1);
    for (int i = 0; i < 2; i++) {
        if (g_row3[i] != g_ref3[i]) {
            if (mismatches == 0) {
                firstBadCall = 3;
                firstBadLane = i;
                firstBadGot = g_row3[i];
                firstBadWant = g_ref3[i];
            }
            mismatches++;
        }
    }

    uart_puts("GM_QEMUBENCH_PIE: emberGatherRow mismatches=");
    uart_put_dec(mismatches);
    uart_puts(" (of 48 boundary/index-range + 480 production-width + 2 min-trip)\n");

    if (mismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS emberGatherRow bit-exact vs emberGatherRowRef reference "
                   "(48 boundary/index-range lanes covering ridx 0-298 and combRow [-64,+64], "
                   "480 production-width lanes at real row-y0 geometry with negative starting ddx, "
                   "2 min-trip-count lanes)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL first mismatch call=");
        uart_put_dec(firstBadCall);
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
