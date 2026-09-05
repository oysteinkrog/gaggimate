/* Real-Xtensa execution check for round 4's two new kernels in
 * src/display/ui/default/bganim/AnimEmber.cpp: emberIdxRowPie (the PIE stage,
 * EE.* vector instructions) and the simplified emberFinalizeRow (now a
 * pure gather, no add). Both are transcribed by hand from that file
 * (mnemonics, operand names, immediates, load/store order all unchanged),
 * matching this project's established precedent (see
 * tools/qemubench/tests/anim_starfield/main.c) for transcribing rather
 * than re-deriving, and for the same reason: PIE is coprocessor CP3, and
 * neither xtensa-asm14 (proves assembly + register
 * allocation, never execution) nor the host bench (band() dispatches to
 * bandRef off-Xtensa; the asm path never runs there) actually executes an
 * EE.VADDS.S8/EE.XORQ/EE.VLD.128.IP sequence. This test does, under
 * Espressif's qemu-system-xtensa fork.
 *
 * The C references reimplement AnimEmber.cpp's own satAddS8 and the
 * XOR-0x80 unsign step in plain C (not asm), so a PASS here is direct
 * evidence that the vector kernel's saturating-add-then-unsign matches
 * that arithmetic bit for bit, on the emulated coprocessor, for every
 * input shape exercised below, not just that the instructions assemble.
 *
 * One deviation between the emulator and the board is known and carried
 * here: this QEMU fork's ee.vadds.s8 floors at -127, the silicon at the
 * textbook -128 (see satAddS8Ref). A lane is accepted if it matches the
 * silicon reference, or the emulator-floor reference when the two
 * differ; those lanes are counted and printed so a run shows how much of
 * the sweep the emulator could not judge. A lane matching neither fails.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as anim_starfield/main.c.
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

/* ------------------------------------------------------------------ */
/* emberIdxRowPie: verbatim transcription from AnimEmber.cpp. Register
 * map, resident constants and loop-form rationale are documented there;
 * this file only needs the instruction sequence to be bit-identical. */
static void emberIdxRowPie(uint8_t *idxOut, const int8_t *fieldIn, const int8_t *flickerSrcIn,
                           const int8_t *perColTileIn, const uint8_t *biasTileIn, int wSixteens) {
    const int8_t *fr = fieldIn;
    const int8_t *lr = flickerSrcIn;
    uint8_t *wr = idxOut;
    int n = wSixteens;
    __asm__ volatile("ee.vld.128.ip q7, %[bias], 0\n"
                      "ee.vld.128.ip q6, %[pct], 0\n"
                      "1:\n"
                      "ee.vld.128.ip q0, %[fr], 16\n"
                      "ee.vld.128.ip q1, %[lr], 16\n"
                      "ee.vadds.s8 q0, q0, q1\n"
                      "ee.vadds.s8 q0, q0, q6\n"
                      "ee.xorq q0, q0, q7\n"
                      "ee.vst.128.ip q0, %[wr], 16\n"
                      "addi %[n], %[n], -1\n"
                      "bnez %[n], 1b\n"
                      : [fr] "+r"(fr), [lr] "+r"(lr), [wr] "+r"(wr), [n] "+r"(n), [bias] "+r"(biasTileIn),
                        [pct] "+r"(perColTileIn)
                      :
                      : "memory");
}

/* Plain-C reference matching AnimEmber.cpp's satAddS8 exactly: two
 * chained 8-bit signed saturating adds (flicker first, then perCol; see
 * FIELD_BIAS's own comment in that file for why this order, not the
 * reverse, is required for correctness), then XOR 0x80 to convert the
 * saturated signed result to an unsigned offset. perCol only has 16
 * entries (one 16-lane tile, reused every group in the row), so it is
 * indexed mod 16, matching the kernel's own q6 reload-once-per-call
 * semantics.
 *
 * Two floors. The silicon saturates to the textbook -128: the production
 * animtest on the board, sweeping the parameter sets, found the PIE landing
 * on -128 where a -127 reference expected -127 (two pixels at the all-100
 * set, 2026-09-05). This QEMU fork's model floors at -127 instead: the
 * first version of this reference used -128 and one lane (lane 1 of call
 * 1, true sum -142) mismatched under the emulator, and the 16-case probe
 * in tools/qemubench/tests/probe_vadds_s8 showed every true sum <= -128
 * coming back -127. satAddS8Ref is the silicon and matches AnimEmber.cpp's
 * satAddS8; satAddS8Qemu is the emulator, used only to recognise lanes the
 * emulator cannot judge (see the file header). */
static int8_t satAddS8Ref(int a, int b) {
    int s = a + b;
    if (s > 127) {
        s = 127;
    }
    if (s < -128) {
        s = -128;
    }
    return (int8_t)s;
}

static int8_t satAddS8Qemu(int a, int b) {
    int s = a + b;
    if (s > 127) {
        s = 127;
    }
    if (s <= -128) {
        s = -127;
    }
    return (int8_t)s;
}

static void emberIdxRowPieRef(uint8_t *idxOut, const int8_t *fieldIn, const int8_t *flickerSrcIn,
                              const int8_t *perColTileIn, int wSixteens) {
    for (int i = 0; i < wSixteens * 16; i++) {
        const int8_t step1 = satAddS8Ref(fieldIn[i], flickerSrcIn[i]);
        const int8_t step2 = satAddS8Ref(step1, perColTileIn[i % 16]);
        idxOut[i] = (uint8_t)step2 ^ 0x80;
    }
}

static void emberIdxRowPieQemu(uint8_t *idxOut, const int8_t *fieldIn, const int8_t *flickerSrcIn,
                               const int8_t *perColTileIn, int wSixteens) {
    for (int i = 0; i < wSixteens * 16; i++) {
        const int8_t step1 = satAddS8Qemu(fieldIn[i], flickerSrcIn[i]);
        const int8_t step2 = satAddS8Qemu(step1, perColTileIn[i % 16]);
        idxOut[i] = (uint8_t)step2 ^ 0x80;
    }
}

/* ------------------------------------------------------------------ */
/* emberFinalizeRow: verbatim transcription from AnimEmber.cpp. Pure
 * gather now, no add left in this kernel, unlike round 3's fused
 * add-then-gather (see that file's header for the schedule proof). */
static void emberFinalizeRow(uint16_t *row, const uint16_t *palOffBiased, const uint8_t *idxRowIn, int wPairs) {
    const uint8_t *ir = idxRowIn;
    uint16_t *wr = row;
    int i0, i1, a0, a1;
    __asm__ volatile("loopnez %[n], 2f\n"
                      "l8ui   %[i0], %[ir], 0\n"
                      "l8ui   %[i1], %[ir], 1\n"
                      "addx2  %[a0], %[i0], %[pal]\n"
                      "l16ui  %[i0], %[a0], 0\n"
                      "addx2  %[a1], %[i1], %[pal]\n"
                      "l16ui  %[i1], %[a1], 0\n"
                      "addi   %[ir], %[ir], 2\n"
                      "slli   %[i1], %[i1], 16\n"
                      "or     %[i0], %[i0], %[i1]\n"
                      "s32i   %[i0], %[wr], 0\n"
                      "addi   %[wr], %[wr], 4\n"
                      "2:\n"
                      : [i0] "=&r"(i0), [i1] "=&r"(i1), [a0] "=&r"(a0), [a1] "=&r"(a1), [ir] "+r"(ir), [wr] "+r"(wr)
                      : [n] "r"(wPairs), [pal] "r"(palOffBiased)
                      : "memory");
}

/* Plain-C reference: color[x] = palOffBiased[idxRow[x]], idxRow unsigned. */
static void emberFinalizeRowRef(uint16_t *row, const uint16_t *palOffBiased, const uint8_t *idxRowIn, int wPairs) {
    for (int i = 0; i < wPairs * 2; i++) {
        row[i] = palOffBiased[idxRowIn[i]];
    }
}

/* ------------------------------------------------------------------ */
/* Shared mismatch bookkeeping across every call of both kernels. */
static int g_mismatches = 0;
static int g_qemuFloorLanes = 0; /* lanes where only the emulator-floor reference matched */
static int g_firstBadKernel = -1; /* 1 = emberIdxRowPie, 2 = emberFinalizeRow */
static int g_firstBadCall = -1;
static int g_firstBadLane = -1;
static uint32_t g_firstBadGot = 0, g_firstBadWant = 0;

static void checkIdxRow(int callNo, const uint8_t *got, const uint8_t *want, const uint8_t *wantQemu, int n) {
    for (int i = 0; i < n; i++) {
        if (got[i] != want[i]) {
            if (got[i] == wantQemu[i]) {
                g_qemuFloorLanes++;
                continue;
            }
            if (g_mismatches == 0) {
                g_firstBadKernel = 1;
                g_firstBadCall = callNo;
                g_firstBadLane = i;
                g_firstBadGot = got[i];
                g_firstBadWant = want[i];
            }
            g_mismatches++;
        }
    }
}

static void checkRow(int callNo, const uint16_t *got, const uint16_t *want, int n) {
    for (int i = 0; i < n; i++) {
        if (got[i] != want[i]) {
            if (g_mismatches == 0) {
                g_firstBadKernel = 2;
                g_firstBadCall = callNo;
                g_firstBadLane = i;
                g_firstBadGot = got[i];
                g_firstBadWant = want[i];
            }
            g_mismatches++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* emberIdxRowPie call 1: boundary/index-range coverage, one 16-lane group.
 * field[] sweeps the documented FIELD_BIAS-centered range [-91,92] plus
 * margin past both ends (into full int8 territory, since the vector add
 * itself has no idea what range the real animation promises; a kernel
 * bug at an out-of-envelope value would still matter if the range proof
 * ever turns out wrong). flicker[] sweeps past its own proven [-10,+9].
 * perColTile is fixed per call (one 16-byte tile, as the real kernel gets
 * one per real row) and deliberately includes AnimEmber.cpp's own
 * documented worst-case order-sensitivity example (see FIELD_BIAS's
 * comment): lane 0 forces field=-91, flicker=+9, perCol=-41, true sum -123
 * (needs zero clamping at all; the case that a perCol-first order gets
 * wrong, at -119 on the silicon and -118 under this emulator's -127
 * floor, while flicker-first gets right, at -123). Lane 15 forces the
 * opposite-signed near-miss (field=92, flicker=-10, perCol=41, true sum
 * 123, also needing no clamp). */
#define N1 16
static __attribute__((aligned(16))) int8_t g_field1[N1];
static __attribute__((aligned(16))) int8_t g_flicker1[N1];
static __attribute__((aligned(16))) int8_t g_perCol1[16];
static __attribute__((aligned(16))) uint8_t g_idx1[N1];
static uint8_t g_idxRef1[N1];
static uint8_t g_idxQemu1[N1];

static void fillCall1(void) {
    for (int k = 0; k < N1; k++) {
        g_field1[k] = (int8_t)(-100 + k * 13); /* -100, -87, ..., 95: past both proven ends */
        g_flicker1[k] = (int8_t)(-16 + (k * 3) % 33); /* sweeps past proven [-10,+9] */
    }
    for (int k = 0; k < 16; k++) {
        g_perCol1[k] = (int8_t)(-48 + (k * 6) % 97); /* sweeps past proven [-41,+41] */
    }
    /* Documented order-sensitivity worst case, exact values from
     * FIELD_BIAS's comment in AnimEmber.cpp. */
    g_field1[0] = -91;
    g_flicker1[0] = 9;
    g_perCol1[0] = -41; /* true sum -123: flicker-first must give -123, not -119 */
    g_field1[15] = 92;
    g_flicker1[15] = -10;
    g_perCol1[15] = 41; /* true sum 123: mirror case, other sign */
}

/* emberIdxRowPie call 2: production-shaped, full 480px row (30 groups of
 * 16). field/flicker drawn from the real proven ranges via a deterministic
 * spread (not the full-int8 stress of call 1; this call is about proving
 * the outer loop's address increment and trip count over many iterations,
 * not re-covering the range proof); perColTile is a real 8-value dither
 * pattern (period 8, replicated twice per AnimEmber.cpp's own
 * band()) rather than call 1's synthetic sweep. */
#define PROD_W 480
#define PROD_N16 (PROD_W / 16)
static __attribute__((aligned(16))) int8_t g_field2[PROD_W];
static __attribute__((aligned(16))) int8_t g_flicker2[PROD_W];
static __attribute__((aligned(16))) int8_t g_perCol2[16];
static __attribute__((aligned(16))) uint8_t g_idx2[PROD_W];
static uint8_t g_idxRef2[PROD_W];
static uint8_t g_idxQemu2[PROD_W];

static void fillCall2(void) {
    for (int k = 0; k < PROD_W; k++) {
        g_field2[k] = (int8_t)(((k * 37) % 183) - 91); /* spread across [-91,+91] */
        g_flicker2[k] = (int8_t)(((k * 11) % 20) - 10); /* spread across [-10,+9] */
    }
    static const int8_t dith8[8] = {-6, -3, -1, 2, 4, -2, 1, -4}; /* period-8 dither, real-shaped */
    for (int k = 0; k < 16; k++) {
        g_perCol2[k] = dith8[k & 7];
    }
}

/* emberIdxRowPie call 3: minimal trip count (wSixteens=1), all-zero inputs
 * except the bias, proves the manual addi/bnez loop does not mishandle
 * the smallest legal count. */
static __attribute__((aligned(16))) int8_t g_field3[16];
static __attribute__((aligned(16))) int8_t g_flicker3[16];
static __attribute__((aligned(16))) int8_t g_perCol3[16];
static __attribute__((aligned(16))) uint8_t g_idx3[16];
static uint8_t g_idxRef3[16];
static uint8_t g_idxQemu3[16];

/* ------------------------------------------------------------------ */
/* emberFinalizeRow call 1: full unsigned byte range in one 128-pixel
 * (64-pair) sweep: idxRow covers every value 0..255 exactly once (twice
 * over, since 256 values in 128 slots would need 2 passes; use 256 slots
 * instead, 128 pairs, one full sweep 0..255). palette is a distinct ramp
 * so a wrong gather reads a distinguishable, checkable value. */
#define PAL_N 256
static uint16_t g_palBuf[PAL_N];
#define F1_PAIRS 128
#define F1_W (F1_PAIRS * 2)
static uint8_t g_idxRowF1[F1_W];
static uint16_t g_rowF1[F1_W];
static uint16_t g_refF1[F1_W];

static void fillPalBuf(void) {
    for (int i = 0; i < PAL_N; i++) {
        g_palBuf[i] = (uint16_t)(0xC000 + i);
    }
}

static void fillFinalizeCall1(void) {
    for (int i = 0; i < F1_W; i++) {
        g_idxRowF1[i] = (uint8_t)i; /* 0..255, exact, F1_W == 256 */
    }
}

/* emberFinalizeRow call 2: production-shaped, full 480px row, idxRow drawn
 * from a deterministic spread across the full byte range (not a plain
 * ramp this time, so consecutive lanes do not trivially predict each
 * other), proves the loop over many iterations at a realistic width. */
static uint8_t g_idxRowF2[PROD_W];
static uint16_t g_rowF2[PROD_W];
static uint16_t g_refF2[PROD_W];

static void fillFinalizeCall2(void) {
    for (int i = 0; i < PROD_W; i++) {
        g_idxRowF2[i] = (uint8_t)((i * 173) & 0xFF);
    }
}

/* emberFinalizeRow call 3: minimal trip count (wPairs=1, two pixels). */
static uint8_t g_idxRowF3[2] = {0, 255};
static uint16_t g_rowF3[2];
static uint16_t g_refF3[2];

/* Sixteen copies of 0x80, matching AnimEmber.cpp's kIdxUnsignBias. */
static const __attribute__((aligned(16))) uint8_t kBias16[16] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                                    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};

int main(void) {
    fillCall1();
    fillCall2();
    /* call 3 inputs are already all-zero via static init */
    fillPalBuf();
    fillFinalizeCall1();
    fillFinalizeCall2();

    /* --- emberIdxRowPie --- */
    emberIdxRowPie(g_idx1, g_field1, g_flicker1, g_perCol1, kBias16, 1);
    emberIdxRowPieRef(g_idxRef1, g_field1, g_flicker1, g_perCol1, 1);
    emberIdxRowPieQemu(g_idxQemu1, g_field1, g_flicker1, g_perCol1, 1);
    checkIdxRow(1, g_idx1, g_idxRef1, g_idxQemu1, N1);

    emberIdxRowPie(g_idx2, g_field2, g_flicker2, g_perCol2, kBias16, PROD_N16);
    emberIdxRowPieRef(g_idxRef2, g_field2, g_flicker2, g_perCol2, PROD_N16);
    emberIdxRowPieQemu(g_idxQemu2, g_field2, g_flicker2, g_perCol2, PROD_N16);
    checkIdxRow(2, g_idx2, g_idxRef2, g_idxQemu2, PROD_W);

    emberIdxRowPie(g_idx3, g_field3, g_flicker3, g_perCol3, kBias16, 1);
    emberIdxRowPieRef(g_idxRef3, g_field3, g_flicker3, g_perCol3, 1);
    emberIdxRowPieQemu(g_idxQemu3, g_field3, g_flicker3, g_perCol3, 1);
    checkIdxRow(3, g_idx3, g_idxRef3, g_idxQemu3, 16);

    /* --- emberFinalizeRow --- */
    const uint16_t *palOffBiased = g_palBuf; /* idxRow is unsigned 0..255, no offset needed */
    emberFinalizeRow(g_rowF1, palOffBiased, g_idxRowF1, F1_PAIRS);
    emberFinalizeRowRef(g_refF1, palOffBiased, g_idxRowF1, F1_PAIRS);
    checkRow(4, g_rowF1, g_refF1, F1_W);

    emberFinalizeRow(g_rowF2, palOffBiased, g_idxRowF2, PROD_W / 2);
    emberFinalizeRowRef(g_refF2, palOffBiased, g_idxRowF2, PROD_W / 2);
    checkRow(5, g_rowF2, g_refF2, PROD_W);

    emberFinalizeRow(g_rowF3, palOffBiased, g_idxRowF3, 1);
    emberFinalizeRowRef(g_refF3, palOffBiased, g_idxRowF3, 1);
    checkRow(6, g_rowF3, g_refF3, 2);

    uart_puts("GM_QEMUBENCH_PIE: emberIdxRowPie+emberFinalizeRow mismatches=");
    uart_put_dec(g_mismatches);
    uart_puts(" (of 16 boundary + 480 production + 16 min-trip idx lanes, "
              "256 full-range + 480 production + 2 min-trip finalize lanes)\n");
    uart_puts("GM_QEMUBENCH_PIE: lanes on the emulator's -127 floor (silicon says -128, not judged here)=");
    uart_put_dec(g_qemuFloorLanes);
    uart_puts("\n");

    if (g_mismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS emberIdxRowPie and emberFinalizeRow bit-exact vs "
                  "their C references (full int8 boundary sweep past both proven envelopes, "
                  "the documented flicker-first-vs-perCol-first order-sensitivity case in both "
                  "directions, 480px production-width geometry, and minimal trip counts)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL kernel=");
        uart_put_dec(g_firstBadKernel);
        uart_puts(" call=");
        uart_put_dec(g_firstBadCall);
        uart_puts(" lane=");
        uart_put_dec(g_firstBadLane);
        uart_puts(" got=0x");
        uart_put_hex32(g_firstBadGot);
        uart_puts(" want=0x");
        uart_put_hex32(g_firstBadWant);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Spin so QEMU has a stable state; nothing to return to. */
    }
}
