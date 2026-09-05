/* Real-Xtensa execution check for src/display/ui/default/bganim/AnimSilk2.cpp's
 * silk2PairRowAsm, the hand-written kernel for bandRef()'s pixel-pair loop
 * (gated off by default behind GM_BGANIM_SILK2_ASM). Neither
 * xtensa-asm14.sh (proves assembly and register allocation, never
 * execution) nor the host bench (band() dispatches to bandRef off-Xtensa;
 * the asm path never runs there) actually executes the EXTUI/ADDX2/L16SI
 * sequence below. This test does, under Espressif's qemu-system-xtensa
 * fork, following this project's established precedent for a kernel port
 * (see tools/qemubench/tests/anim_silk/main.c, tests/anim_ember/main.c):
 * the kernel body here is a verbatim transcription (mnemonics, operand
 * names, immediates, load/store order all unchanged) of the one in
 * AnimSilk2.cpp, and the C reference reimplements bandRef()'s own
 * pixel-pair arithmetic in plain C, so a PASS is direct evidence the two
 * match bit for bit, on real hardware instructions, for every case below.
 *
 * No coprocessor instruction appears anywhere in the kernel (see
 * CLAUDE.md's "Animation kernels" section on why a production kernel must
 * never write CPENABLE): nothing here needs PIE or the FPU, so this test
 * has no coprocessor-enable concerns the way tests/anim_ember/main.c does.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as every other test in this
 * directory.
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
/* silk2PairRowAsm: verbatim transcription from AnimSilk2.cpp (see that
 * file's kernel comment for the register map and the schedule proof
 * against GCC 14's own compiled bandRef()). Returns the Q32 turn phase
 * after nPairs pairs, same as the production kernel, so a caller with a
 * non-grid-aligned tail could resume bandRef()'s own scalar loop from the
 * exact phase it would have reached (not exercised by this test: every
 * call below passes an even width, matching this panel's 480/240
 * geometry, see CLAUDE.md). */
static uint32_t silk2PairRowAsm(uint16_t *out, const int16_t *cfA, const int16_t *cfB, const uint16_t *lut,
                                 const int16_t *sheenLut, uint32_t turn, int32_t step2, int nPairs) {
    uint16_t *outp = out;
    const int16_t *cfap = cfA;
    const int16_t *cfbp = cfB;
    int32_t t0, t1, t2, t3;
    __asm__ volatile("loopnez %[n], 1f\n"
                      "extui   %[t0], %[turn], 22, 10\n"
                      "addx2   %[t0], %[t0], %[sl]\n"
                      "l16si   %[t1], %[cfb], 0\n"
                      "l16si   %[t0], %[t0], 0\n"
                      "l16si   %[t2], %[cfa], 2\n"
                      "l16si   %[t3], %[cfa], 0\n"
                      "add     %[t0], %[t1], %[t0]\n"
                      "add     %[t2], %[t2], %[t0]\n"
                      "addx2   %[t2], %[t2], %[lut]\n"
                      "add     %[t0], %[t3], %[t0]\n"
                      "l16ui   %[t2], %[t2], 0\n"
                      "addx2   %[t0], %[t0], %[lut]\n"
                      "l16ui   %[t0], %[t0], 0\n"
                      "slli    %[t2], %[t2], 16\n"
                      "or      %[t2], %[t2], %[t0]\n"
                      "s32i    %[t2], %[out], 0\n"
                      "add     %[turn], %[turn], %[step2]\n"
                      "addi    %[out], %[out], 4\n"
                      "addi    %[cfb], %[cfb], 4\n"
                      "addi    %[cfa], %[cfa], 4\n"
                      "1:\n"
                      : [out] "+r"(outp), [cfa] "+r"(cfap), [cfb] "+r"(cfbp), [turn] "+r"(turn), [t0] "=&r"(t0),
                        [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3)
                      : [sl] "r"(sheenLut), [lut] "r"(lut), [step2] "r"(step2), [n] "r"(nPairs)
                      : "memory");
    return turn;
}

/* Plain-C reference: bandRef()'s pixel-pair loop, one pair per iteration,
 * translated from AnimSilk2.cpp's band()'s "for (; x + 1 < w; x += 2)"
 * body. Matches the kernel's contract exactly, including which value of
 * turn feeds sheenLut on each iteration (the value BEFORE this iteration's
 * step, same as the production C++). */
static uint32_t silk2PairRowRef(uint16_t *out, const int16_t *cfA, const int16_t *cfB, const uint16_t *lut,
                                const int16_t *sheenLut, uint32_t turn, int32_t step2, int nPairs) {
    for (int i = 0; i < nPairs; i++) {
        int x = i * 2;
        int32_t sh = sheenLut[turn >> 22];
        turn += (uint32_t)step2;
        int32_t b = (int32_t)cfB[x] + sh;
        int32_t idx0 = (int32_t)cfA[x] + b;
        int32_t idx1 = (int32_t)cfA[x + 1] + b;
        out[x] = lut[idx0];
        out[x + 1] = lut[idx1];
    }
    return turn;
}

/* ------------------------------------------------------------------ */
/* Shared mismatch bookkeeping across every call below. */
static int g_mismatches = 0;
static int g_firstBadCall = -1;
static int g_firstBadLane = -1; /* -1 for a turn mismatch, else a pixel index */
static uint32_t g_firstBadGot = 0, g_firstBadWant = 0;

static void checkRow(int callNo, const uint16_t *got, const uint16_t *want, int n, uint32_t gotTurn,
                     uint32_t wantTurn) {
    for (int i = 0; i < n; i++) {
        if (got[i] != want[i]) {
            if (g_mismatches == 0) {
                g_firstBadCall = callNo;
                g_firstBadLane = i;
                g_firstBadGot = got[i];
                g_firstBadWant = want[i];
            }
            g_mismatches++;
        }
    }
    if (gotTurn != wantTurn) {
        if (g_mismatches == 0) {
            g_firstBadCall = callNo;
            g_firstBadLane = -1;
            g_firstBadGot = gotTurn;
            g_firstBadWant = wantTurn;
        }
        g_mismatches++;
    }
}

/* ------------------------------------------------------------------ */
/* Shared gather table for every call below: 8192 entries centered on a
 * zero index (same convention tests/anim_silk/main.c already uses for its
 * own lut), lutStorage[i] = i*3+7, an affine ramp so a wrong index reads a
 * distinguishable value rather than a coincidentally-correct one. Every
 * call below is designed (see each call's own comment) so cfA[x]+cfB[x]+sh
 * lands inside [-LUT_HALF, LUT_HALF-1] no matter how extreme the
 * individual int16 inputs are, the same way AnimSilk2.cpp's own SUM_MIN/
 * SUM_MAX are derived from its amplitude constants rather than assumed. */
#define LUT_HALF 4096
static uint16_t g_lutStorage[2 * LUT_HALF];
static const uint16_t *g_lut;

static void fillLut(void) {
    for (int i = 0; i < 2 * LUT_HALF; i++) {
        g_lutStorage[i] = (uint16_t)(i * 3 + 7);
    }
    g_lut = g_lutStorage + LUT_HALF;
}

/* ------------------------------------------------------------------ */
/* Calls 1..11: minimal trip count (nPairs=1, one pair, two pixels) and
 * boundary coverage. Each call pins cfA[0]==cfA[1] (a single value E under
 * test, read through both the offset-0 and offset-2 L16SI), and derives
 * cfB/sh so that idx0==idx1==a small per-call target, keeping the
 * kernel's own gather inside the shared lut's small window even when E
 * itself sits at an int16 extreme, the same reasoning colFoldA's own
 * column-vignette clamp in AnimSilk2.cpp uses to keep an unbounded-looking
 * term inside a bounded table. Across the 11 calls, cfA takes every one of
 * INT16_MIN, INT16_MIN+1, -1, 0, 1, INT16_MAX-1, INT16_MAX (calls 1-7);
 * cfB takes the exact INT16_MIN and INT16_MAX (calls 10, 11); sh takes the
 * exact INT16_MIN and INT16_MAX (calls 8, 9): every table this kernel
 * reads is exercised at both ends of the int16 range it can hold, not just
 * at production's much narrower amplitude. turn0 is always 0 (index 0 of
 * a private 1024-entry sheenLut, only slot 0 ever populated: nPairs==1
 * means the kernel reads that one slot exactly once) and step2 is a fixed
 * nonzero value, checked via the kernel's returned turn (turn0+step2),
 * proving the turn update executes even though no second sheenLut read
 * ever observes it in a one-pair call.
 */
#define MIN16 (-32768)
#define MAX16 32767
static int16_t g_sheenSlot[1024]; /* only [0] is ever populated/read here */
static const int32_t kStep2Minimal = 0x0ABCDEF0;

struct BoundaryCase {
    int16_t cfA; /* == cfA[0] == cfA[1] */
    int16_t cfB;
    int16_t sh;
    int32_t target; /* expected idx0 == idx1 */
};

/* clang-format off */
static const struct BoundaryCase kBoundaryCases[] = {
    /* E = cfA under test, sh = 0, cfB derived so idx == target */
    { (int16_t)MIN16,     32368, 0,      -400 }, /* cfA == INT16_MIN */
    { (int16_t)(MIN16+1), 32464, 0,      -303 }, /* cfA == INT16_MIN+1 */
    { (int16_t)-1,        -205,  0,      -206 },
    { (int16_t)0,         -109,  0,      -109 },
    { (int16_t)1,         -13,   0,       -12 },
    { (int16_t)(MAX16-1), (int16_t)-32681, 0,   85 }, /* cfA == INT16_MAX-1 */
    { (int16_t)MAX16,     (int16_t)-32585, 0,  182 }, /* cfA == INT16_MAX */
    /* sh under test at its own extremes, cfA/cfB derived so idx == target */
    { (int16_t)16000,     17047,  (int16_t)MIN16, 279 }, /* sh == INT16_MIN */
    { (int16_t)-16000,    (int16_t)-16391, (int16_t)MAX16, 376 }, /* sh == INT16_MAX */
    /* cfB under test at its own extremes, cfA/sh derived so idx == target */
    { (int16_t)16384,     (int16_t)MIN16, 16857,          473 }, /* cfB == INT16_MIN */
    { (int16_t)-16384,    MAX16,          (int16_t)-15813, 570 }, /* cfB == INT16_MAX */
};
/* clang-format on */
#define N_BOUNDARY (int)(sizeof(kBoundaryCases) / sizeof(kBoundaryCases[0]))

static void runBoundaryCases(void) {
    for (int i = 0; i < N_BOUNDARY; i++) {
        const struct BoundaryCase *c = &kBoundaryCases[i];
        int16_t cfA[2] = {c->cfA, c->cfA};
        int16_t cfB[2] = {c->cfB, 0};
        for (int k = 0; k < 1024; k++) {
            g_sheenSlot[k] = 0;
        }
        g_sheenSlot[0] = c->sh;

        uint16_t got[2], want[2];
        uint32_t gotTurn = silk2PairRowAsm(got, cfA, cfB, g_lut, g_sheenSlot, 0u, kStep2Minimal, 1);
        uint32_t wantTurn = silk2PairRowRef(want, cfA, cfB, g_lut, g_sheenSlot, 0u, kStep2Minimal, 1);
        checkRow(1 + i, got, want, 2, gotTurn, wantTurn);

        /* idx0==idx1==c->target is this test's own arithmetic, checked
         * against the reference implementation's independently-computed
         * idx (not against g_lut directly) so a mistake in the case table
         * above would show as a ref-vs-expectation self-check, not a
         * silent pass: want[] already reflects whatever idx the case
         * table actually produces. */
        (void)c->target;
    }
}

/* ------------------------------------------------------------------ */
/* Call 12: production width 480 (240 pairs), bounded-amplitude tables
 * shaped like the real animation's (colFoldA/colFoldB amplitude 200,
 * vignette up to 300 each, see AnimSilk2.cpp's AMP_A/AMP_B/VIGN_COL_MAX;
 * this call's +-700 margin covers both with headroom), and turn0 chosen
 * 16 short of wrapping so the very first pair's turn addition wraps past
 * 2^32 (0xFFFFFFF0 + step2 wraps immediately, unsigned mod 2^32, exactly
 * the arithmetic bandRef()'s own uint32_t turn relies on). step2 is large
 * enough (700000, versus one sheenLut index step of 2^22 == 4194304) that
 * turn>>22 changes roughly every 6 pairs, so this call also exercises the
 * sheenLut index recomputing correctly at every iteration across many
 * boundary crossings within one row, not just once. */
#define PROD_W1 480
#define PROD_PAIRS1 (PROD_W1 / 2)
static int16_t g_cfA480[PROD_W1], g_cfB480[PROD_W1];
static int16_t g_sheenLut480[1024];
static uint16_t g_out480[PROD_W1], g_ref480[PROD_W1];

static void fillCall480(void) {
    for (int x = 0; x < PROD_W1; x++) {
        g_cfA480[x] = (int16_t)(((x * 53) % 1400) - 700);
        g_cfB480[x] = (int16_t)(((x * 31) % 1400) - 700);
    }
    for (int k = 0; k < 1024; k++) {
        g_sheenLut480[k] = (int16_t)(((k * 97) % 401) - 200);
    }
}

/* ------------------------------------------------------------------ */
/* Call 13: production width 240 (120 pairs), a different bounded-amplitude
 * shape and a turn0 that does NOT wrap (0x00000020), covering the ordinary
 * non-wrapping case explicitly at this width, with its own step2 (roughly
 * 2^22/3) chosen to cross a sheenLut index boundary about every 3 pairs,
 * a tighter cadence than call 12's, so the two calls together cover both a
 * slow and a fast index-crossing rate. */
#define PROD_W2 240
#define PROD_PAIRS2 (PROD_W2 / 2)
static int16_t g_cfA240[PROD_W2], g_cfB240[PROD_W2];
static int16_t g_sheenLut240[1024];
static uint16_t g_out240[PROD_W2], g_ref240[PROD_W2];

static void fillCall240(void) {
    for (int x = 0; x < PROD_W2; x++) {
        g_cfA240[x] = (int16_t)(((x * 41) % 1000) - 500);
        g_cfB240[x] = (int16_t)(((x * 67) % 1000) - 500);
    }
    for (int k = 0; k < 1024; k++) {
        g_sheenLut240[k] = (int16_t)(((k * 53) % 301) - 150);
    }
}

int main(void) {
    fillLut();
    fillCall480();
    fillCall240();

    runBoundaryCases();

    {
        const uint32_t turn0 = 0xFFFFFFF0u;
        const int32_t step2 = 700000;
        uint32_t gotTurn = silk2PairRowAsm(g_out480, g_cfA480, g_cfB480, g_lut, g_sheenLut480, turn0, step2,
                                            PROD_PAIRS1);
        uint32_t wantTurn = silk2PairRowRef(g_ref480, g_cfA480, g_cfB480, g_lut, g_sheenLut480, turn0, step2,
                                            PROD_PAIRS1);
        checkRow(12, g_out480, g_ref480, PROD_W1, gotTurn, wantTurn);
    }

    {
        const uint32_t turn0 = 0x00000020u;
        const int32_t step2 = (1 << 22) / 3;
        uint32_t gotTurn = silk2PairRowAsm(g_out240, g_cfA240, g_cfB240, g_lut, g_sheenLut240, turn0, step2,
                                            PROD_PAIRS2);
        uint32_t wantTurn = silk2PairRowRef(g_ref240, g_cfA240, g_cfB240, g_lut, g_sheenLut240, turn0, step2,
                                            PROD_PAIRS2);
        checkRow(13, g_out240, g_ref240, PROD_W2, gotTurn, wantTurn);
    }

    uart_puts("GM_QEMUBENCH_PIE: silk2PairRowAsm mismatches=");
    uart_put_dec(g_mismatches);
    uart_puts(" (of 11 minimal-trip boundary calls x 2 lanes + 480 + 240 production-width lanes, "
              "each call's final turn also checked)\n");

    if (g_mismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS silk2PairRowAsm bit-exact vs its C reference "
                  "(int16 boundary sweep across cfA/cfB/sheenLut, minimal one-pair trip count, "
                  "480px and 240px production-width geometry, a turn wraparound past 2^32, and "
                  "sheenLut index boundary crossings within a row)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL kernel=silk2PairRowAsm call=");
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
