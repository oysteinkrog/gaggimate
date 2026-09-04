/* Real-Xtensa execution check for auroraPixelsAsm, the hand-scheduled scalar
 * Xtensa kernel for Aurora's x loop
 * (src/display/ui/default/bganim/AnimAurora.cpp). Round 2 changed the
 * per-pixel algorithm, not just the kernel's instruction schedule: the
 * two-curtain field is a smooth, low-frequency function of x, so this
 * kernel evaluates it (and the clamp/square/scale chain downstream of it)
 * only at every OTHER pixel -- a coarse column grid, spacing 2 -- and
 * linearly interpolates the ROWLUT INDEX for the pixel in between. That
 * halves the two sine-table gathers (the two biggest tables) and skips the
 * clamp/square/scale chain entirely for the interpolated half of the
 * pixels; only the final color gather and the dither lookup stay
 * per-pixel. See AnimAurora.cpp's file header (perf pass 4) for the full
 * derivation and the measured error this trades for the gather count.
 *
 * Same instruction sequence, same operand registers (as GCC assigns them
 * from this file's own operand list -- not required to match
 * AnimAurora.cpp's assignment, since both are the same source text run
 * through the same compiler options' allocator), same loopnez/extui/
 * addx4/addx2/addmi/mull forms, transcribed by hand from AnimAurora.cpp's
 * auroraPixelsAsm (not regenerated or simplified), run under Espressif's
 * qemu-system-xtensa fork via this repo's tools/qemubench harness -- same
 * approach as tests/blend_row/main.c for blendGroup8General.
 *
 * Test vectors, chosen to exercise every corner the C++ reference and the
 * asm kernel could disagree on:
 *   - w1[i] = ((i*37) & 1023) - 512, w2[i] = ((i*53) & 1023) - 512: signed
 *     table values spanning both sides of zero, so the branchless
 *     max(v,0) clamp (srai/and/sub) gets exercised both ways.
 *   - ph1 starts at index 1010 (of 1024), stepping ~8.47 indices/pair
 *     (2*STEP1=2168 in Q8 ticks); ph2 starts at index 1015, stepping
 *     ~5.54 indices/pair (2*STEP2=1418). Both wrap past 1023 back to 0
 *     within the first 2 pairs of the 16-pair run, exercising the
 *     "extui ..., 8, 10" index mask's wrap -- and wrap several more times
 *     over the run, exercising it repeatedly, not just once.
 *   - dfi (the dither-fold element index) wraps every 4 pairs (8 entries,
 *     2 consumed per pair), so a 16-pair run wraps it 4 times, exercising
 *     the "extui ..., 0, 5" byte-offset wrap on that side too.
 *   - rowScale = 358, the real p[1]=100 maximum from AnimAurora.cpp's
 *     INTEN14_MAX, so the mull/srli chain runs at its largest production
 *     multiplier rather than an arbitrary smaller one.
 *   - rowLUT is filled with a distinct pattern (0xA000+k) so a wrong index
 *     shows up as a wrong, recognisable value rather than an accidental
 *     match.
 *   - scur0 is computed the same way band() computes it (bootstrap sample
 *     at the row's unadvanced ph1/ph2), not an arbitrary constant, so the
 *     interpolation's first pair uses the real relationship between the
 *     bootstrap and the kernel's own first "next" sample.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as pie_smoke_full/main.c and
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
        buf[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        uart_putc(buf[--n]);
    }
}

/* STEP1/STEP2 and the DSTEP*_HI/LO immediate splits, transcribed from
 * AnimAurora.cpp's TICKS/STEP1/STEP2 and the DSTEP1/DSTEP2 constexprs
 * above auroraPixelsAsm. ADDMI's assembly-text immediate is the real value
 * added (must be a multiple of 256), not a pre-shifted field -- an earlier
 * draft got this backwards, passed the imm8 field value (8, 6) instead of
 * the real value (2048, 1536), and it silently assembled into the wrong
 * answer (see AnimAurora.cpp's comment on DSTEP1_HI for the full story).
 * 2*STEP1=2168 -> addmi 2048 + addi 120; 2*STEP2=1418 -> addmi 1536 + addi
 * -118. */
static constexpr uint32_t STEP1 = 1084;
static constexpr uint32_t STEP2 = 709;
static constexpr int32_t DSTEP1_HI = 2048, DSTEP1_LO = 120;
static constexpr int32_t DSTEP2_HI = 1536, DSTEP2_LO = -118;

/* Verbatim from AnimAurora.cpp's auroraPixelsAsm (round 3): same mnemonics,
 * same operand roles, same loopnez/extui/addx4/addx2/addmi/addi/movi/max/
 * mull/srli/l32i/l16ui/s16i sequence and ordering. Only the physical
 * registers may differ (GCC's own allocator choice for this file's operand
 * list); the instruction stream is unchanged. Round 3 replaced the round-2
 * branchless clamp with a native MAX and the packed 32-bit store with two
 * independent 16-bit stores, rescheduled against GCC 14's compiled bandRef
 * (see AnimAurora.cpp's comment for the full reasoning). Processes `pairs`
 * = w/2 pixel-pairs; scur0 is the caller's bootstrap sample at the row's
 * unadvanced ph1/ph2 (band()'s job on the device, main()'s job here). */
__attribute__((noinline)) static void auroraPixelsAsm(uint16_t *__restrict dst, const int32_t *__restrict w1,
                                                        const int32_t *__restrict w2,
                                                        const uint16_t *__restrict rowLUT,
                                                        const int32_t *__restrict df, uint32_t ph1, uint32_t ph2,
                                                        int32_t rowScale, int32_t scur0, int pairs) {
    uint32_t p1 = ph1, p2 = ph2;
    int32_t dfi = 0;
    int32_t scur = scur0;
    int32_t t1, t2, t3;
    asm volatile(
        "loopnez %[n], 2f\n"
        "addmi   %[p1], %[p1], %[ds1hi]\n"
        "addi    %[p1], %[p1], %[ds1lo]\n"
        "addmi   %[p2], %[p2], %[ds2hi]\n"
        "addi    %[p2], %[p2], %[ds2lo]\n"
        "extui   %[t1], %[p1], 8, 10\n"
        "addx4   %[t1], %[t1], %[w1]\n"
        "l32i    %[t1], %[t1], 0\n"
        "extui   %[t2], %[p2], 8, 10\n"
        "addx4   %[t2], %[t2], %[w2]\n"
        "l32i    %[t2], %[t2], 0\n"
        "add     %[t1], %[t1], %[t2]\n"
        "movi    %[t2], 0\n"
        "max     %[t1], %[t1], %[t2]\n"
        "mull    %[t1], %[t1], %[t1]\n"
        "extui   %[t2], %[dfi], 0, 5\n"
        "add     %[t2], %[df], %[t2]\n"
        "addi    %[dfi], %[dfi], 8\n"
        "srli    %[t1], %[t1], 12\n"
        "mull    %[t1], %[t1], %[rs]\n"
        "l32i    %[t3], %[t2], 0\n"
        "srli    %[t1], %[t1], 12\n"
        "add     %[t3], %[scur], %[t3]\n"
        "addx2   %[t3], %[t3], %[rl]\n"
        "l16ui   %[t3], %[t3], 0\n"
        "s16i    %[t3], %[dst], 0\n"
        "l32i    %[t2], %[t2], 4\n"
        "add     %[t3], %[scur], %[t1]\n"
        "srli    %[t3], %[t3], 1\n"
        "or      %[scur], %[t1], %[t1]\n"
        "add     %[t1], %[t3], %[t2]\n"
        "addx2   %[t1], %[t1], %[rl]\n"
        "l16ui   %[t1], %[t1], 0\n"
        "s16i    %[t1], %[dst], 2\n"
        "addi    %[dst], %[dst], 4\n"
        "2:\n"
        : [dst] "+r"(dst), [p1] "+r"(p1), [p2] "+r"(p2), [dfi] "+r"(dfi), [scur] "+r"(scur), [t1] "=&r"(t1),
          [t2] "=&r"(t2), [t3] "=&r"(t3)
        : [w1] "r"(w1), [w2] "r"(w2), [rl] "r"(rowLUT), [df] "r"(df), [rs] "r"(rowScale), [n] "r"(pairs),
          [ds1hi] "i"(DSTEP1_HI), [ds1lo] "i"(DSTEP1_LO), [ds2hi] "i"(DSTEP2_HI), [ds2lo] "i"(DSTEP2_LO)
        : "memory");
}

/* C reference: AnimAurora.cpp bandRef's sampleScaledSq/pair-loop, same
 * algorithm as the asm kernel above, written independently. */
static int32_t sampleScaledSq(const int32_t *w1, const int32_t *w2, uint32_t ph1, uint32_t ph2, int32_t rowScale) {
    const int32_t v = w1[(ph1 >> 8) & 1023] + w2[(ph2 >> 8) & 1023];
    const int32_t vc = v > 0 ? v : 0;
    return (((vc * vc) >> 12) * rowScale) >> 12;
}

static void auroraPixelsRef(uint16_t *out, const int32_t *w1, const int32_t *w2, const uint16_t *rowLUT,
                             const int32_t *df, uint32_t ph1, uint32_t ph2, int32_t rowScale, int32_t scur0,
                             int pairs) {
    int32_t scur = scur0;
    int bit = 0; // element index into df[8]: 0,2,4,6,0,2,... (2 consumed per pair)
    for (int i = 0; i < pairs; i++) {
        ph1 += STEP1;
        ph1 += STEP1;
        ph2 += STEP2;
        ph2 += STEP2;
        const int32_t snext = sampleScaledSq(w1, w2, ph1, ph2, rowScale);
        out[2 * i] = rowLUT[scur + df[bit & 7]];
        const int32_t sodd = (scur + snext) >> 1;
        out[2 * i + 1] = rowLUT[sodd + df[(bit + 1) & 7]];
        scur = snext;
        bit += 2;
    }
}

static int32_t w1tab[1024] __attribute__((aligned(16)));
static int32_t w2tab[1024] __attribute__((aligned(16)));
static uint16_t rowLUT[64] __attribute__((aligned(16)));
static int32_t df[8] __attribute__((aligned(16))) = {5, 7, 9, 11, 13, 15, 4, 6};

static uint16_t gotBuf[32] __attribute__((aligned(16)));
static uint16_t refBuf[32] __attribute__((aligned(16)));

int main(void) {
    for (int i = 0; i < 1024; i++) {
        w1tab[i] = static_cast<int32_t>(i * 37 & 1023) - 512;
        w2tab[i] = static_cast<int32_t>(i * 53 & 1023) - 512;
    }
    for (int i = 0; i < 64; i++) {
        rowLUT[i] = static_cast<uint16_t>(0xA000 + i);
    }

    const uint32_t ph1Start = 1010u << 8; // index 1010/1024, wraps within the run
    const uint32_t ph2Start = 1015u << 8; // index 1015/1024, wraps within the run
    const int32_t rowScale = 358;         // p[1]=100 production maximum
    const int w = 32;
    const int pairs = w / 2;

    // Bootstrap, same formula band() uses: scaledSq at the row's unadvanced
    // ph1/ph2, before the pair loop's first "next sample" advance.
    const int32_t scur0 = sampleScaledSq(w1tab, w2tab, ph1Start, ph2Start, rowScale);

    auroraPixelsAsm(gotBuf, w1tab, w2tab, rowLUT, df, ph1Start, ph2Start, rowScale, scur0, pairs);
    auroraPixelsRef(refBuf, w1tab, w2tab, rowLUT, df, ph1Start, ph2Start, rowScale, scur0, pairs);

    uart_puts("GM_QEMUBENCH_ANIM: got=");
    for (int i = 0; i < w; i++) {
        uart_put_hex16(gotBuf[i]);
        uart_putc(' ');
    }
    uart_puts("\nGM_QEMUBENCH_ANIM: ref=");
    for (int i = 0; i < w; i++) {
        uart_put_hex16(refBuf[i]);
        uart_putc(' ');
    }
    uart_puts("\n");

    int mismatches = 0;
    int firstBad = -1;
    for (int i = 0; i < w; i++) {
        if (gotBuf[i] != refBuf[i]) {
            if (firstBad < 0) {
                firstBad = i;
            }
            mismatches++;
        }
    }

    if (mismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS auroraPixelsAsm bit-exact vs C++ reference "
                   "(32/32 pixels, coarse-grid interpolation, wraparound + max-rowScale case)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL mismatches=");
        uart_put_dec(mismatches);
        uart_puts(" first_bad_index=");
        uart_put_dec(firstBad);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Spin so QEMU has a stable state; nothing to return to. */
    }
}
