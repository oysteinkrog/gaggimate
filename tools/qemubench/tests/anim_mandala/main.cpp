/* QEMU execution check for Mandala's device kernel, 2026-09-05 redesign
 * (src/display/ui/default/bganim/AnimMandala.cpp): interpPairKernel, the
 * hand-scheduled Xtensa scalar kernel for the ordinary contiguous
 * block-pair case, run exactly as band() calls it, checked against a
 * scalar C reference that implements the same algorithm writeInterpPair()
 * does (already proven pixel-exact against the golden frames on the host
 * bench: this test is about the ASM, not the algorithm).
 *
 * This replaces the file's earlier coverage of mandalaBandFwd/mandalaBandBack
 * (round 2's per-pixel kernel), which no longer exist: the redesign samples
 * a block's magnitude once (mandalaIndex, still portable C++, exercised by
 * the host bench and by --shapes/fuzz, not by this bare-metal harness) and
 * interpolates the rest, so the only Xtensa-specific kernel left is the
 * one that turns four interpolated magnitudes into four palette gathers
 * and two packed 32-bit stores per block.
 *
 * The instruction sequence below is transcribed by hand from
 * interpPairKernel in AnimMandala.cpp (same mnemonics, same operand roles,
 * same asm volatile block structure) rather than #included directly,
 * matching every other test in this harness: the real function lives
 * inside a translation unit (anonymous namespace, BgAnimCommon
 * dependencies: alloc(), themeGen(), heap_caps_malloc) that is not
 * freestanding and cannot be pulled into this bare-metal harness
 * unmodified. xtensa-asm14.sh already proved the real file assembles with
 * zero register spills; this proves the exact instruction sequence
 * executes correctly and produces the exact values the algorithm calls
 * for.
 *
 * No PIE, no FPU: the kernel is pure integer Xtensa core ISA (l8ui, l16ui,
 * add/addx2, srli/slli, or, s32i, addi), so like the round-2 test this
 * harness needs no coprocessor bring-up at all: CPENABLE stays untouched,
 * satisfying "a production kernel must never write CPENABLE" trivially
 * (there is nothing here that would need to).
 *
 * Coverage:
 *  - nBlocks in {2, 3, 8, 120, 240}: 2 is the kernel's documented minimum
 *    (a trip count of nBlocks-1 = 1; band() never calls it below 2, see
 *    the kernel's own comment), 120 and 240 are the panel's two real
 *    widths (half-resolution debug path and the fixed 480x480 panel, see
 *    "nBlocks is w/2, 120 or 240" in the kernel's comment), 3 and 8 fill in
 *    an odd and a small-even case in between.
 *  - a[]/c[] (the two block-rows' real+interpolated-input magnitudes) and
 *    palette[] are filled with distinguishing (index-derived, not constant
 *    or all-zero) patterns so a wrong index, a swapped a/c lane or a
 *    swapped top/bottom store shows up as a mismatch instead of
 *    coincidentally reading the same value another index would have
 *    produced.
 *  - The reference recomputes the last block's clamp the same way
 *    writeInterpPair() does (b+1 < nBlocks ? a[b+1] : a[b]), so the
 *    kernel's "handle the clamp outside the loop" epilogue is checked
 *    against the same edge case the loop body would hit if it did not
 *    special-case it.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly,
 * same as every other test in this harness.
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

static void uart_put_hex16(uint16_t v) {
    static const char hex[] = "0123456789abcdef";
    uart_putc(hex[(v >> 12) & 0xF]);
    uart_putc(hex[(v >> 8) & 0xF]);
    uart_putc(hex[(v >> 4) & 0xF]);
    uart_putc(hex[v & 0xF]);
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

/* ---- transcribed from AnimMandala.cpp (verbatim mnemonics/operands) ---- */

__attribute__((noinline)) static void interpPairKernel(uint16_t *rowTop, uint16_t *rowBot, const uint8_t *a,
                                                         const uint8_t *c, const uint16_t *palette, int nBlocks) {
    int aCur = a[0];
    int cCur = c[0];
    const uint8_t *aPtr = a + 1;
    const uint8_t *cPtr = c + 1;
    int aNext, cNext, hMidTop, hMidBot, p0, t;
    int p1 = nBlocks - 1;
    asm volatile("loop %[p1], 2f\n"
                 "l8ui  %[aNext], %[aPtr], 0\n"
                 "l8ui  %[cNext], %[cPtr], 0\n"
                 "addi  %[aPtr], %[aPtr], 1\n"
                 "addi  %[cPtr], %[cPtr], 1\n"
                 "add   %[t], %[aCur], %[aNext]\n"
                 "srli  %[hMidTop], %[t], 1\n"
                 "add   %[t], %[cCur], %[cNext]\n"
                 "srli  %[hMidBot], %[t], 1\n"
                 "addx2 %[t], %[aCur], %[pal]\n"
                 "addx2 %[p1], %[hMidTop], %[pal]\n"
                 "l16ui %[p0], %[t], 0\n"
                 "add   %[t], %[aCur], %[cCur]\n"
                 "srli  %[t], %[t], 1\n"
                 "l16ui %[p1], %[p1], 0\n"
                 "addx2 %[t], %[t], %[pal]\n"
                 "slli  %[p1], %[p1], 16\n"
                 "or    %[p1], %[p1], %[p0]\n"
                 "s32i  %[p1], %[rowTop], 0\n"
                 "l16ui %[p0], %[t], 0\n"
                 "add   %[t], %[hMidTop], %[hMidBot]\n"
                 "srli  %[t], %[t], 1\n"
                 "addx2 %[t], %[t], %[pal]\n"
                 "l16ui %[p1], %[t], 0\n"
                 "addi  %[rowTop], %[rowTop], 4\n"
                 "slli  %[p1], %[p1], 16\n"
                 "or    %[p1], %[p1], %[p0]\n"
                 "s32i  %[p1], %[rowBot], 0\n"
                 "addi  %[rowBot], %[rowBot], 4\n"
                 "or    %[aCur], %[aNext], %[aNext]\n"
                 "or    %[cCur], %[cNext], %[cNext]\n"
                 "2:\n"
                 "addx2 %[t], %[aCur], %[pal]\n"
                 "add   %[p1], %[aCur], %[cCur]\n"
                 "l16ui %[p0], %[t], 0\n"
                 "srli  %[p1], %[p1], 1\n"
                 "addx2 %[p1], %[p1], %[pal]\n"
                 "l16ui %[p1], %[p1], 0\n"
                 "slli  %[t], %[p0], 16\n"
                 "or    %[t], %[t], %[p0]\n"
                 "s32i  %[t], %[rowTop], 0\n"
                 "slli  %[t], %[p1], 16\n"
                 "or    %[t], %[t], %[p1]\n"
                 "s32i  %[t], %[rowBot], 0\n"
                 : [aPtr] "+r"(aPtr), [cPtr] "+r"(cPtr), [aCur] "+r"(aCur), [cCur] "+r"(cCur), [rowTop] "+r"(rowTop),
                   [rowBot] "+r"(rowBot), [p1] "+r"(p1), [aNext] "=&r"(aNext), [cNext] "=&r"(cNext),
                   [hMidTop] "=&r"(hMidTop), [hMidBot] "=&r"(hMidBot), [p0] "=&r"(p0), [t] "=&r"(t)
                 : [pal] "r"(palette)
                 : "memory");
}

/* ---- scalar C reference: writeInterpPair()'s algorithm, verified pixel-
 * exact against golden frames on the host bench (tools/animbench). This
 * test is not re-checking the algorithm; it is checking that the asm
 * above computes the same thing. ---- */
static void refPair(const uint8_t *a, const uint8_t *c, const uint16_t *palette, int nBlocks, uint16_t *wantTop,
                     uint16_t *wantBot) {
    for (int b = 0; b < nBlocks; b++) {
        const int va = a[b];
        const int vb = (b + 1 < nBlocks) ? a[b + 1] : va;
        const int vc = c[b];
        const int vd = (b + 1 < nBlocks) ? c[b + 1] : vc;
        const int hMidTop = (va + vb) >> 1;
        const int hMidBot = (vc + vd) >> 1;
        wantTop[2 * b] = palette[va];
        wantTop[2 * b + 1] = palette[hMidTop];
        wantBot[2 * b] = palette[(va + vc) >> 1];
        wantBot[2 * b + 1] = palette[(hMidTop + hMidBot) >> 1];
    }
}

/* ---- test data ---- */

constexpr int MAX_BLOCKS = 240; // the panel's fixed widths call this with w/2: 120 or 240
alignas(16) static uint8_t aBuf[MAX_BLOCKS];
alignas(16) static uint8_t cBuf[MAX_BLOCKS];
alignas(16) static uint16_t paletteBuf[256];
alignas(16) static uint16_t gotTop[2 * MAX_BLOCKS];
alignas(16) static uint16_t gotBot[2 * MAX_BLOCKS];
alignas(16) static uint16_t wantTop[2 * MAX_BLOCKS];
alignas(16) static uint16_t wantBot[2 * MAX_BLOCKS];

static void buildTestData() {
    // Distinguishing, non-monotonic, index-derived patterns: a and c must
    // not agree (or the vertical-blend and horizontal-blend paths could
    // coincidentally read the same palette entry), and neither should be a
    // simple ramp (or an off-by-one lane swap could still land on a
    // plausible-looking value).
    for (int i = 0; i < MAX_BLOCKS; i++) {
        aBuf[i] = (uint8_t)((i * 37 + 11) & 0xFF);
        cBuf[i] = (uint8_t)((i * 53 + 197) & 0xFF);
    }
    for (int i = 0; i < 256; i++) {
        paletteBuf[i] = (uint16_t)(0x3000 + i * 0x0101);
    }
}

int totalMismatches = 0;
int firstBadCase = -1, firstBadIdx = -1;
uint16_t firstGot = 0, firstWant = 0;
int caseCounter = 0;

static void checkCase(int nBlocks) {
    for (int i = 0; i < 2 * nBlocks; i++) {
        if (gotTop[i] != wantTop[i]) {
            totalMismatches++;
            if (firstBadCase < 0) {
                firstBadCase = caseCounter;
                firstBadIdx = i;
                firstGot = gotTop[i];
                firstWant = wantTop[i];
            }
        }
        if (gotBot[i] != wantBot[i]) {
            totalMismatches++;
            if (firstBadCase < 0) {
                firstBadCase = caseCounter;
                firstBadIdx = 10000 + i; // offset marks a bottom-row mismatch
                firstGot = gotBot[i];
                firstWant = wantBot[i];
            }
        }
    }
    uart_puts("GM_QEMUBENCH_MANDALA: case nBlocks=");
    uart_put_dec(nBlocks);
    uart_puts(" top[0..3]=");
    for (int i = 0; i < 4 && i < 2 * nBlocks; i++) {
        uart_put_hex16(gotTop[i]);
        uart_putc(' ');
    }
    uart_puts("\n");
    caseCounter++;
}

int main(void) {
    buildTestData();

    static const int nBlocksCases[] = {2, 3, 8, 120, 240};
    for (int nBlocks : nBlocksCases) {
        for (int i = 0; i < 2 * nBlocks; i++) {
            gotTop[i] = 0xDEAD;
            gotBot[i] = 0xDEAD;
        }
        interpPairKernel(gotTop, gotBot, aBuf, cBuf, paletteBuf, nBlocks);
        refPair(aBuf, cBuf, paletteBuf, nBlocks, wantTop, wantBot);
        checkCase(nBlocks);
    }

    if (totalMismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS interpPairKernel bit-exact vs scalar reference (");
        uart_put_dec((int)(sizeof(nBlocksCases) / sizeof(nBlocksCases[0])));
        uart_puts(" nBlocks cases: 2, 3, 8, 120, 240, distinguishing a/c/palette patterns, last-block clamp "
                   "exercised in every case)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL mismatches=");
        uart_put_dec(totalMismatches);
        uart_puts(" first_bad_case=");
        uart_put_dec(firstBadCase);
        uart_puts(" first_bad_idx=");
        uart_put_dec(firstBadIdx);
        uart_puts(" got=");
        uart_put_hex16(firstGot);
        uart_puts(" want=");
        uart_put_hex16(firstWant);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
    }
}
