/* Real-Xtensa execution check for src/display/ui/default/bganim/AnimCells.cpp's
 * cellsRowAsm, the hand-written kernel for bandRef()'s pixel loop (gated
 * behind GM_BGANIM_CELLS_ASM, on by default). Neither xtensa-asm14.sh
 * (proves assembly and register allocation, never execution) nor the host
 * bench (band() dispatches to bandRef off-Xtensa, so the asm path never
 * runs there) actually executes the L8UI/L32I/ADDX2/L16UI/S16I sequence
 * below. This test does, under Espressif's qemu-system-xtensa fork,
 * following this directory's precedent for a kernel port (see
 * tests/anim_silk2/main.c, tests/anim_tide/main.c): the loop body here is a
 * verbatim transcription of the one in AnimCells.cpp, mnemonics, operand
 * names, immediates and load/store order all unchanged, and the C
 * reference states the kernel's contract in plain C, so a PASS is direct
 * evidence the two match bit for bit on real hardware instructions for
 * every case below.
 *
 * What the kernel promises, and what is therefore checked here:
 *   out[x] = slot[x & 7][tex[x >> 2]] for every x in [0, w), and nothing
 *   outside that range is written.
 * The cases sweep the packed value over its whole seven-bit range, not
 * just the 0..100 the animation can produce, because the kernel masks
 * nothing and init() proves safety over the full 0..127. Widths run 0
 * through 33 so every trip count from zero upward and every tail length 0
 * through 7 is covered, then 233, 240, 466, 480 and 511, which are the
 * production and half-resolution widths this panel uses plus the odd
 * widths the band contract allows. Two pointer sets run each width: one
 * with all eight column phases on the same palette base, one with them
 * spread to the extremes of the dither range, so a swapped or dropped
 * phase cannot pass. Guard cells on both sides of the output catch a write
 * past either end.
 *
 * No coprocessor instruction appears anywhere in the kernel (see CLAUDE.md's
 * "Animation kernels" section on why a production kernel must never write
 * CPENABLE): nothing here needs PIE or the FPU, so this test has no
 * coprocessor-enable concerns. Every access is a scalar load or store, so
 * there is no 16-byte alignment precondition to probe either.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as every other test here.
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
/* cellsRowAsm: verbatim transcription from AnimCells.cpp. The production
 * definition carries GM_ANIM_IRAM and __attribute__((noinline)); neither
 * exists in this freestanding link and neither changes the instructions,
 * so only those two attributes are dropped. See that file's kernel comment
 * for the schedule proof against GCC 14's own compiled bandRef. */
static void cellsRowAsm(uint16_t *out, const uint8_t *tex, const uint16_t *const *slot, int w) {
    if (w <= 0) {
        return;
    }
    uint16_t *o = out;
    const uint8_t *t = tex;
    int n = w >> 3;
    int v0, v1, pa, pb, pc, pd;
    __asm__ volatile("loopnez %[n], 1f\n"
                     "l8ui    %[v0], %[t], 0\n"
                     "l8ui    %[v1], %[t], 1\n"
                     "l32i    %[pa], %[s], 0\n"
                     "l32i    %[pb], %[s], 4\n"
                     "addx2   %[pa], %[v0], %[pa]\n"
                     "addx2   %[pb], %[v0], %[pb]\n"
                     "l32i    %[pc], %[s], 8\n"
                     "l32i    %[pd], %[s], 12\n"
                     "l16ui   %[pa], %[pa], 0\n"
                     "l16ui   %[pb], %[pb], 0\n"
                     "addx2   %[pc], %[v0], %[pc]\n"
                     "addx2   %[pd], %[v0], %[pd]\n"
                     "s16i    %[pa], %[o], 0\n"
                     "s16i    %[pb], %[o], 2\n"
                     "l16ui   %[pc], %[pc], 0\n"
                     "l16ui   %[pd], %[pd], 0\n"
                     "l32i    %[pa], %[s], 16\n"
                     "l32i    %[pb], %[s], 20\n"
                     "s16i    %[pc], %[o], 4\n"
                     "s16i    %[pd], %[o], 6\n"
                     "addx2   %[pa], %[v1], %[pa]\n"
                     "addx2   %[pb], %[v1], %[pb]\n"
                     "l32i    %[pc], %[s], 24\n"
                     "l32i    %[pd], %[s], 28\n"
                     "l16ui   %[pa], %[pa], 0\n"
                     "l16ui   %[pb], %[pb], 0\n"
                     "addx2   %[pc], %[v1], %[pc]\n"
                     "addx2   %[pd], %[v1], %[pd]\n"
                     "s16i    %[pa], %[o], 8\n"
                     "s16i    %[pb], %[o], 10\n"
                     "l16ui   %[pc], %[pc], 0\n"
                     "l16ui   %[pd], %[pd], 0\n"
                     "addi    %[t], %[t], 2\n"
                     "s16i    %[pc], %[o], 12\n"
                     "s16i    %[pd], %[o], 14\n"
                     "addi    %[o], %[o], 16\n"
                     "1:\n"
                     : [o] "+&r"(o), [t] "+&r"(t), [v0] "=&r"(v0), [v1] "=&r"(v1), [pa] "=&r"(pa),
                       [pb] "=&r"(pb), [pc] "=&r"(pc), [pd] "=&r"(pd)
                     : [s] "r"(slot), [n] "r"(n)
                     : "memory");
    for (int x = w & ~7; x < w; x++) {
        out[x] = slot[x & 7][tex[x >> 2]];
    }
}

/* Plain-C reference: the kernel's contract, stated once. This is also
 * bandRef()'s arithmetic with the dither add and the clamp already folded
 * into slot[], which is exactly the substitution AnimCells.cpp's init()
 * proves is lossless for this Bayer table. */
static void cellsRowRef(uint16_t *out, const uint8_t *tex, const uint16_t *const *slot, int w) {
    for (int x = 0; x < w; x++) {
        out[x] = slot[x & 7][tex[x >> 2]];
    }
}

/* ------------------------------------------------------------------ */
#define MAX_W 511
#define GUARD 8
#define TEX_N 160
#define PAL_N 512

static uint16_t g_pal[PAL_N];
static uint8_t g_tex[TEX_N];
static uint16_t g_out[GUARD + MAX_W + GUARD];
static uint16_t g_ref[GUARD + MAX_W + GUARD];
static const uint16_t *g_slot[8];

static int g_mismatches = 0;
static int g_guardHits = 0;
static int g_calls = 0;
static long g_pixels = 0;
static int g_firstBadCall = -1;
static int g_firstBadX = -1;
static uint32_t g_firstBadGot = 0, g_firstBadWant = 0;

/* Distinct for every palette slot, so a wrong index cannot alias a right
 * one: 40503 is odd and coprime with 65536, so index -> value is injective
 * over the whole table. */
static void fillPal(void) {
    for (int i = 0; i < PAL_N; i++) {
        g_pal[i] = (uint16_t)((uint32_t)i * 40503u + 7u);
    }
}

/* texPattern 0: a ramp over the whole seven-bit range, so consecutive
 * texels differ and a texel read one place off is caught.
 * texPattern 1: every texel 0, the low end of the packed range.
 * texPattern 2: every texel 127, the high end the mask can yield.
 * texPattern 3: a coprime stride, so the 4-pixel groups see unrelated
 * values and a group boundary error shows up. */
static void fillTex(int pattern) {
    for (int i = 0; i < TEX_N; i++) {
        uint8_t v;
        switch (pattern) {
        case 0: v = (uint8_t)(i & 127); break;
        case 1: v = 0; break;
        case 2: v = 127; break;
        default: v = (uint8_t)((i * 53) & 127); break;
        }
        g_tex[i] = v;
    }
}

/* slotSet 0: all eight column phases on one base, which is what a flat
 * dither row would give and which catches a phase that reads the wrong
 * pointer only through the guards, so it runs alongside set 1.
 * slotSet 1: the production shape, base IDX_LO plus a dither offset per
 * phase, spread over -2..+2 and ordered so no two phases share a base.
 * slotSet 2: the widest legal spread, so an index that overflows into a
 * neighbouring phase's range is visible. */
static void fillSlots(int slotSet) {
    static const int prod[8] = {-2, 1, -1, 2, 0, -2, 2, -1};
    static const int wide[8] = {0, 50, 100, 150, 200, 250, 300, 350};
    for (int k = 0; k < 8; k++) {
        int base;
        if (slotSet == 0) {
            base = 50;
        } else if (slotSet == 1) {
            base = 50 + prod[k];
        } else {
            base = wide[k];
        }
        g_slot[k] = g_pal + base;
    }
}

static void runCall(int w, int pattern, int slotSet) {
    fillTex(pattern);
    fillSlots(slotSet);
    const uint16_t stamp = (uint16_t)(0xA500u + (g_calls & 0xFF));
    for (int i = 0; i < GUARD + MAX_W + GUARD; i++) {
        g_out[i] = stamp;
        g_ref[i] = stamp;
    }
    cellsRowAsm(g_out + GUARD, g_tex, g_slot, w);
    cellsRowRef(g_ref + GUARD, g_tex, g_slot, w);
    for (int x = 0; x < w; x++) {
        if (g_out[GUARD + x] != g_ref[GUARD + x]) {
            if (g_mismatches == 0) {
                g_firstBadCall = g_calls;
                g_firstBadX = x;
                g_firstBadGot = g_out[GUARD + x];
                g_firstBadWant = g_ref[GUARD + x];
            }
            g_mismatches++;
        }
    }
    /* Nothing before the row, and nothing from w onward, may have moved. */
    for (int i = 0; i < GUARD; i++) {
        if (g_out[i] != stamp) {
            g_guardHits++;
        }
    }
    for (int i = GUARD + w; i < GUARD + MAX_W + GUARD; i++) {
        if (g_out[i] != stamp) {
            g_guardHits++;
        }
    }
    g_calls++;
    g_pixels += w;
}

int main(void) {
    fillPal();

    /* Every trip count from zero up, and with it every tail length 0..7. */
    for (int w = 0; w <= 33; w++) {
        for (int pattern = 0; pattern < 4; pattern++) {
            for (int slotSet = 0; slotSet < 3; slotSet++) {
                runCall(w, pattern, slotSet);
            }
        }
    }

    /* Production geometry (480), the half-resolution path (240) and the
     * odd widths the band contract allows (233, 466, 511). */
    static const int widths[5] = {233, 240, 466, 480, 511};
    for (int i = 0; i < 5; i++) {
        for (int pattern = 0; pattern < 4; pattern++) {
            for (int slotSet = 0; slotSet < 3; slotSet++) {
                runCall(widths[i], pattern, slotSet);
            }
        }
    }

    uart_puts("GM_QEMUBENCH_PIE: cellsRowAsm mismatches=");
    uart_put_dec(g_mismatches);
    uart_puts(" guard_writes=");
    uart_put_dec(g_guardHits);
    uart_puts(" calls=");
    uart_put_dec(g_calls);
    uart_puts(" pixels=");
    uart_put_dec((int)g_pixels);
    uart_puts("\n");

    if (g_mismatches == 0 && g_guardHits == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS cellsRowAsm bit-exact vs its C reference "
                  "(packed values over the full 0..127 seven-bit range, four texel patterns, "
                  "three column-phase pointer sets, widths 0 through 33 for every trip count "
                  "and tail length, plus 233, 240, 466, 480 and 511, with guard cells on both "
                  "sides of every row unwritten)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL kernel=cellsRowAsm call=");
        uart_put_dec(g_firstBadCall);
        uart_puts(" x=");
        uart_put_dec(g_firstBadX);
        uart_puts(" got=0x");
        uart_put_hex32(g_firstBadGot);
        uart_puts(" want=0x");
        uart_put_hex32(g_firstBadWant);
        uart_puts(" guard_writes=");
        uart_put_dec(g_guardHits);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Spin so QEMU has a stable state; nothing to return to. */
    }
}
