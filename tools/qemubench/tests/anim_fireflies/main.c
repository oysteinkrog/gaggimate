/* Real-Xtensa execution check for this animation's two PIE/scalar-asm
 * kernels (registry id 9, src/display/ui/default/bganim/AnimFireflies.cpp:
 * fillRowPie and drawGlowSpanAsm). Same idea as tools/qemubench/tests/
 * blend_row/main.c: the instruction sequences below are transcribed by
 * hand from that file (mnemonics, operand registers, immediates, load/
 * store order all unchanged), not regenerated or simplified, and run under
 * Espressif's qemu-system-xtensa fork via this repo's tools/qemubench
 * harness. This is the only rung of the verification ladder that actually
 * executes the EE.* instruction (ee.vld.128.ip/ee.vst.128.ip) and the
 * LOOPNEZ zero-overhead loop on real hardware semantics rather than
 * reading GCC's assembly output.
 *
 * Two kernels, two reference functions, both freestanding (no libc/libm,
 * no globals with constructors, no malloc):
 *   - fillRowPieAsm vs fillRowRef: the background broadcast-fill.
 *   - drawGlowSpanAsm vs drawGlowSpanRef: the per-firefly glow span. The
 *     reference is drawGlowSpanScalar from AnimFireflies.cpp verbatim
 *     (ROUND 2: two real early exits, `if (idx>=64) continue` and
 *     `if (a==0) continue`, restored after round 1's branch-free MIN clamp
 *     measured 20-48% slower on the device; see that file's comment on
 *     drawGlowSpanScalar/drawGlowSpanAsm for the full reasoning).
 *
 * No OS, no drivers, no libc startup: prints over UART0 by writing its
 * FIFO register directly (ESP32-S3 UART0 base 0x60000000), same as every
 * other test in this harness.
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

/* ---- portable references (freestanding transcriptions of
 * AnimFireflies.cpp's fillBgRowScalar / drawGlowSpanScalar /
 * addScaled565 from BgAnimCommon.h) ---- */

static uint8_t g_alphaLUT[64];

static uint16_t addScaled565Ref(uint16_t dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int dr = ((dst >> 11) & 0x1F) + ((r * a) >> 11);
    int dg = ((dst >> 5) & 0x3F) + ((g * a) >> 10);
    int db = (dst & 0x1F) + ((b * a) >> 11);
    if (dr > 0x1F) {
        dr = 0x1F;
    }
    if (dg > 0x3F) {
        dg = 0x3F;
    }
    if (db > 0x1F) {
        db = 0x1F;
    }
    return (uint16_t)((dr << 11) | (dg << 5) | db);
}

static void fillRowRef(uint16_t *dst, uint16_t color, int w) {
    for (int i = 0; i < w; i++) {
        dst[i] = color;
    }
}

static void drawGlowSpanRef(uint16_t *row, int32_t dxQ8_0, int32_t dy2Q4, int32_t invR2Fixed, uint8_t r, uint8_t g,
                             uint8_t b, uint8_t a8, int count) {
    int32_t dxQ8 = dxQ8_0;
    for (int i = 0; i < count; i++, dxQ8 += 256) {
        int32_t dx2Q4 = (dxQ8 * dxQ8) >> 12;
        int32_t idx = ((dx2Q4 + dy2Q4) * invR2Fixed) >> 20;
        if (idx >= 64) {
            continue;
        }
        uint8_t a = (uint8_t)(((uint16_t)g_alphaLUT[idx] * a8) >> 8);
        if (a == 0) {
            continue;
        }
        row[i] = addScaled565Ref(row[i], r, g, b, a);
    }
}

/* ---- kernels under test: verbatim from AnimFireflies.cpp ---- */

__attribute__((noinline)) static void fillRowPieAsm(uint16_t *dst, uint16_t color, int nOct) {
    static uint16_t bcast[8] __attribute__((aligned(16)));
    for (int i = 0; i < 8; i++) {
        bcast[i] = color;
    }
    const uint16_t *src = bcast;
    uint16_t *wr = dst;
    __asm__ volatile("ee.vld.128.ip q0, %[src], 0\n" /* q0 = color x8, resident for the loop */
                      "loopnez %[n], 2f\n"
                      "ee.vst.128.ip q0, %[wr], 16\n"
                      "2:\n"
                      : [wr] "+r"(wr)
                      : [src] "r"(src), [n] "r"(nOct)
                      : "memory");
}

__attribute__((noinline)) static void drawGlowSpanAsm(uint16_t *rowIn, int32_t dxQ8_0, int32_t dy2Q4, int32_t invR2Fixed, uint8_t rCol,
                             uint8_t gCol, uint8_t bCol, uint8_t a8v, int count) {
    uint16_t *row = rowIn;
    int32_t dxQ8 = dxQ8_0;
    const uint8_t *alut = g_alphaLUT;
    int32_t a, dst, res, base2; /* scratch; values unused after the block */
    __asm__ volatile("loopnez %[n], 3f\n"
                      /* idx = ((dxQ8*dxQ8>>12) + dy2Q4) * invR2Fixed >> 20 */
                      "mull  %[a], %[dxQ8], %[dxQ8]\n"
                      "srai  %[a], %[a], 12\n"
                      "add   %[a], %[a], %[dy2Q4]\n"
                      "mull  %[a], %[a], %[invR2]\n"
                      "srai  %[a], %[a], 20\n"
                      "bgei  %[a], 64, 4f\n" /* outside the circle: matches `if (idx>=64) continue` */
                      /* a = alphaLUT[idx] * a8v >> 8 */
                      "add   %[a], %[alut], %[a]\n"
                      "l8ui  %[a], %[a], 0\n"
                      "mull  %[a], %[a], %[a8v]\n"
                      "srli  %[a], %[a], 8\n"
                      "beqz  %[a], 4f\n" /* fully transparent: matches `if (a==0) continue` */
                      /* addScaled565(dst, rCol, gCol, bCol, a) */
                      "l16ui %[dst], %[row], 0\n"
                      "extui %[res], %[dst], 11, 5\n"
                      "mull  %[n], %[rc], %[a]\n"
                      "srai  %[n], %[n], 11\n"
                      "add   %[res], %[res], %[n]\n"
                      "movi  %[n], 31\n"
                      "min   %[res], %[res], %[n]\n"
                      "slli  %[res], %[res], 11\n"
                      "extui %[base2], %[dst], 5, 6\n"
                      "mull  %[n], %[gc], %[a]\n"
                      "srai  %[n], %[n], 10\n"
                      "add   %[base2], %[base2], %[n]\n"
                      "movi  %[n], 63\n"
                      "min   %[base2], %[base2], %[n]\n"
                      "slli  %[base2], %[base2], 5\n"
                      "or    %[res], %[res], %[base2]\n"
                      "extui %[base2], %[dst], 0, 5\n"
                      "mull  %[n], %[bc], %[a]\n"
                      "srai  %[n], %[n], 11\n"
                      "add   %[base2], %[base2], %[n]\n"
                      "movi  %[n], 31\n"
                      "min   %[base2], %[base2], %[n]\n"
                      "or    %[res], %[res], %[base2]\n"
                      "s16i  %[res], %[row], 0\n"
                      "4:\n"
                      "addi  %[row], %[row], 2\n"
                      "addi  %[dxQ8], %[dxQ8], 256\n"
                      "3:\n"
                      : [row] "+r"(row), [dxQ8] "+r"(dxQ8), [a] "=&r"(a), [dst] "=&r"(dst), [res] "=&r"(res),
                        [base2] "=&r"(base2)
                      : [dy2Q4] "r"(dy2Q4), [invR2] "r"(invR2Fixed), [rc] "r"((int32_t)rCol),
                        [gc] "r"((int32_t)gCol), [bc] "r"((int32_t)bCol), [a8v] "r"((int32_t)a8v), [alut] "r"(alut),
                        [n] "r"(count)
                      : "memory");
}

int main(void) {
    int mismatches = 0;
    uint32_t firstBadTag = 0;

    /* ---- Part 1: fillRowPie vs the plain scalar fill, eight colors
     * spanning the RGB565 range including the all-0/all-1 extremes. ---- */
    {
        static uint16_t got[32] __attribute__((aligned(16)));
        static uint16_t want[32] __attribute__((aligned(16)));
        static const uint16_t colors[8] = {0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0xA5A5, 0x1234, 0x8410};
        for (int c = 0; c < 8; c++) {
            for (int i = 0; i < 32; i++) {
                got[i] = 0xDEAD;
                want[i] = 0xDEAD;
            }
            fillRowPieAsm(got, colors[c], 4);
            fillRowRef(want, colors[c], 32);
            for (int i = 0; i < 32; i++) {
                if (got[i] != want[i]) {
                    mismatches++;
                    if (firstBadTag == 0) {
                        firstBadTag = 0x10000000u | ((uint32_t)c << 8) | (uint32_t)i;
                    }
                }
            }
        }
    }

    /* alphaLUT covering the full byte range with the production invariant
     * (index 63, the last entry, is exactly 0) preserved. ROUND 2's
     * drawGlowSpanRef/Asm no longer read past idx 63 at all (the BGEI/
     * `continue` skips the gather outright once idx>=64), so this value is
     * no longer load-bearing for correctness the way it was for round 1's
     * clamp - kept anyway so the LUT here matches AnimFireflies.cpp's
     * init() exactly. */
    for (int k = 0; k < 64; k++) {
        g_alphaLUT[k] = (uint8_t)(255 - (k * 255) / 63);
    }
    g_alphaLUT[63] = 0;
    g_alphaLUT[0] = 255;

    /* ---- Part 2: drawGlowSpanAsm vs drawGlowSpanRef. 3 dy2Q4 values x 3
     * invR2Fixed values (production min/mid/max, from the file-header bound
     * proof) x 4 dxQ8 starting offsets (one chosen to push idx past 63
     * partway through the span, exercising both BGEI early exits and the
     * in-circle path in the same call) x 7 (r,g,b,a8) combinations
     * (all-zero, all-max, a mid color, and three single-channel-saturated
     * cases plus a near-zero alpha that exercises the BEQZ early exit) x 16
     * pixels each, dst pre-seeded with a rotating mix of extremes (0x0000,
     * 0xFFFF, and three mid RGB565 values) so every channel's clamp is
     * exercised on both the low and high side. 1,344 pixels total. ---- */
    {
        static uint16_t got[16] __attribute__((aligned(16)));
        static uint16_t want[16] __attribute__((aligned(16)));
        static const uint16_t dstSeeds[6] = {0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0x8410};
        static const int32_t dy2Qs[3] = {0, 500, 4000};
        static const int32_t invR2s[3] = {6825, 20000, 54825};
        static const int32_t dxStarts[4] = {0, -2000, 3000, -8000};
        static const uint8_t rgbA[7][4] = {
            {0, 0, 0, 0}, {255, 255, 255, 255}, {200, 100, 50, 255}, {255, 0, 0, 128},
            {0, 255, 0, 64}, {0, 0, 255, 200}, {30, 30, 30, 1},
        };
        for (int di = 0; di < 3; di++) {
            for (int ri = 0; ri < 3; ri++) {
                for (int xi = 0; xi < 4; xi++) {
                    for (int ci = 0; ci < 7; ci++) {
                        for (int i = 0; i < 16; i++) {
                            uint16_t seed = dstSeeds[(i + di + ri + xi + ci) % 6];
                            got[i] = seed;
                            want[i] = seed;
                        }
                        drawGlowSpanAsm(got, dxStarts[xi], dy2Qs[di], invR2s[ri], rgbA[ci][0], rgbA[ci][1],
                                        rgbA[ci][2], rgbA[ci][3], 16);
                        drawGlowSpanRef(want, dxStarts[xi], dy2Qs[di], invR2s[ri], rgbA[ci][0], rgbA[ci][1],
                                        rgbA[ci][2], rgbA[ci][3], 16);
                        for (int i = 0; i < 16; i++) {
                            if (got[i] != want[i]) {
                                mismatches++;
                                if (firstBadTag == 0) {
                                    firstBadTag = 0x20000000u | ((uint32_t)di << 20) | ((uint32_t)ri << 16) |
                                                  ((uint32_t)xi << 12) | ((uint32_t)ci << 8) | (uint32_t)i;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    uart_puts("GM_QEMUBENCH_ANIM: fireflies mismatches=");
    uart_put_hex16((uint16_t)mismatches);
    uart_puts(" firstBadTag=");
    uart_put_hex16((uint16_t)(firstBadTag >> 16));
    uart_put_hex16((uint16_t)firstBadTag);
    uart_puts("\n");

    if (mismatches == 0) {
        uart_puts("GM_QEMUBENCH_ANIM: PASS fillRowPie + drawGlowSpanAsm bit-exact vs scalar reference "
                   "(8 colors x 32px fill; 3x3x4x7 x 16px = 1344 glow-span pixels covering both BGEI/BEQZ exits, "
                   "a8/color/dst extremes)\n");
        /* Also emit the PIE-prefixed marker run.sh's exit-code grep actually
         * looks for (see tools/qemubench/run.sh), ASM_BRIEF.md's own
         * "GM_QEMUBENCH_ANIM: PASS" wording predates that grep, so both are
         * printed rather than picking one and breaking the other. */
        uart_puts("GM_QEMUBENCH_PIE: PASS fireflies fillRowPie+drawGlowSpanAsm bit-exact vs scalar reference\n");
    } else {
        uart_puts("GM_QEMUBENCH_ANIM: FAIL fireflies kernels mismatched\n");
        uart_puts("GM_QEMUBENCH_PIE: FAIL fireflies kernels mismatched\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Nothing to return to: spin so QEMU has a stable state. */
    }
}
